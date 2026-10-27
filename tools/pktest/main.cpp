/**
    pktest -- render Pumpkin offline, and hold it to the test card.

    It drives the REAL plugin class, through the same ProcessOpenGL a host
    calls, on a synthetic clock, in a headless CGL context. The checks hold the
    plugin to the test card's own answers in tools/fixtures/ (its TypeScript's
    numbers, and pictures its painter drew in Chrome), which
    tools/fixtures/testcard.mjs makes from ~/Projects/video/test-card.

        pktest --out /tmp/patch.png       the defaults, 5 s in
        pktest --list                     every parameter and its default
        pktest --pipe [--frames N]        raw RGBA frames on stdout
        pktest --film N                   N frames, raw RGBA on stdout
        pktest --offline                  the checks that need no GL context (CI)

    `--script` is the fleet's cue format: `frame  Parameter Name  value` lines,
    held before the first key and after the last. A STANDARD (0..1) control is
    linear between its keys; an option, a boolean, an event or an integer
    STEPS: it holds each key's value until the next key's frame.

    PKTEST_RENDERER=software asks for Apple's software renderer by id, on a
    Mac with a GPU: what a GPU-less CI runner falls back to.
*/

#include "Controls.h"
#include "Geometry.h"
#include "Lantern.h"
#include "Pumpkin.h"
#include "Random.h"
#include "Shaders.h"

#include <OpenGL/OpenGL.h>
#include <OpenGL/gl3.h>
#include <zlib.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <functional>
#include <map>
#include <sstream>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace pumpkin;

#ifndef PKTEST_FIXTURES
	#define PKTEST_FIXTURES "tools/fixtures"
#endif

namespace
{
using Floats = std::vector< float >;
using Bytes  = std::vector< unsigned char >;

//---------------------------------------------------------------------------
// Reporting.
//---------------------------------------------------------------------------
int g_failures = 0;
int g_checks   = 0;

std::string fmt( const char* format, ... )
{
	char buffer[ 4096 ];
	va_list args;
	va_start( args, format );
	std::vsnprintf( buffer, sizeof( buffer ), format, args );
	va_end( args );
	return buffer;
}

void Check( bool condition, const std::string& message )
{
	++g_checks;
	std::printf( "  %s  %s\n", condition ? "ok  " : "FAIL", message.c_str() );
	if( !condition )
		++g_failures;
}

int Verdict()
{
	std::printf( "\n  %s\n", g_failures == 0 ? "PASS" : "FAIL" );
	return g_failures == 0 ? 0 : 1;
}

//---------------------------------------------------------------------------
// PNG. zlib ships with the OS.
//---------------------------------------------------------------------------
void putU32( Bytes& out, uint32_t value )
{
	out.push_back( static_cast< unsigned char >( value >> 24 ) );
	out.push_back( static_cast< unsigned char >( value >> 16 ) );
	out.push_back( static_cast< unsigned char >( value >> 8 ) );
	out.push_back( static_cast< unsigned char >( value ) );
}

void putChunk( Bytes& out, const char* type, const Bytes& data )
{
	putU32( out, static_cast< uint32_t >( data.size() ) );
	const size_t start = out.size();
	out.insert( out.end(), type, type + 4 );
	out.insert( out.end(), data.begin(), data.end() );
	uLong crc = crc32( 0L, Z_NULL, 0 );
	crc       = crc32( crc, out.data() + start, static_cast< uInt >( 4 + data.size() ) );
	putU32( out, static_cast< uint32_t >( crc ) );
}

/// `rgba` is floats, row 0 at the BOTTOM (GL's order); the file is written top
/// row first, which is the only place anything here flips. Premultiplied
/// output is written as it is: a viewer shows the source over black.
bool writePng( const std::string& path, int width, int height, const Floats& rgba )
{
	Bytes raw;
	raw.reserve( static_cast< size_t >( height ) * ( 1 + static_cast< size_t >( width ) * 4 ) );
	for( int y = height - 1; y >= 0; --y )
	{
		raw.push_back( 0 );
		for( int x = 0; x < width; ++x )
			for( int c = 0; c < 4; ++c )
			{
				//Alpha written as 1: a premultiplied source over black is what
				//Resolume's Add shows, and a viewer that honours alpha would
				//otherwise show the dead cells as the page behind.
				const float v = c == 3 ? 1.0f : rgba[ ( static_cast< size_t >( y ) * width + x ) * 4 + c ];
				raw.push_back( static_cast< unsigned char >( std::lround( std::clamp( v, 0.0f, 1.0f ) * 255.0f ) ) );
			}
	}
	uLongf compressedSize = compressBound( static_cast< uLong >( raw.size() ) );
	Bytes compressed( compressedSize );
	if( compress2( compressed.data(), &compressedSize, raw.data(), static_cast< uLong >( raw.size() ), 6 ) != Z_OK )
		return false;
	compressed.resize( compressedSize );

	Bytes png = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n' };
	Bytes ihdr;
	putU32( ihdr, static_cast< uint32_t >( width ) );
	putU32( ihdr, static_cast< uint32_t >( height ) );
	ihdr.insert( ihdr.end(), { 8, 6, 0, 0, 0 } );
	putChunk( png, "IHDR", ihdr );
	putChunk( png, "IDAT", compressed );
	putChunk( png, "IEND", {} );
	FILE* file = std::fopen( path.c_str(), "wb" );
	if( !file )
		return false;
	const size_t written = std::fwrite( png.data(), 1, png.size(), file );
	std::fclose( file );
	return written == png.size();
}


//---------------------------------------------------------------------------
// The context. PKTEST_RENDERER=software asks for Apple's software renderer
// by id (plotter's recipe, via radar): what a GPU-less runner falls back to.
//---------------------------------------------------------------------------
bool g_software = false;

CGLContextObj createContext()
{
	const CGLPixelFormatAttribute accelerated[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAAccelerated,
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute fallback[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};
	const CGLPixelFormatAttribute generic[] = {
		kCGLPFAOpenGLProfile, static_cast< CGLPixelFormatAttribute >( kCGLOGLPVersion_GL4_Core ),
		kCGLPFARendererID, static_cast< CGLPixelFormatAttribute >( kCGLRendererGenericFloatID ),
		kCGLPFAColorSize, static_cast< CGLPixelFormatAttribute >( 24 ),
		kCGLPFAAlphaSize, static_cast< CGLPixelFormatAttribute >( 8 ),
		static_cast< CGLPixelFormatAttribute >( 0 )
	};

	CGLPixelFormatObj format = nullptr;
	GLint formatCount        = 0;
	const char* renderer     = std::getenv( "PKTEST_RENDERER" );
	if( renderer != nullptr && std::strcmp( renderer, "software" ) == 0 )
	{
		if( CGLChoosePixelFormat( generic, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
		g_software = true;
		std::fprintf( stderr, "pktest: PKTEST_RENDERER=software, Apple's software renderer\n" );
	}
	else if( CGLChoosePixelFormat( accelerated, &format, &formatCount ) != kCGLNoError || format == nullptr )
	{
		if( CGLChoosePixelFormat( fallback, &format, &formatCount ) != kCGLNoError || format == nullptr )
			return nullptr;
	}
	CGLContextObj context = nullptr;
	const CGLError error  = CGLCreateContext( format, nullptr, &context );
	CGLDestroyPixelFormat( format );
	if( error != kCGLNoError )
		return nullptr;
	CGLSetCurrentContext( context );
	return context;
}

const char* kindName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_BOOLEAN: return "bool";
	case FF_TYPE_EVENT: return "event";
	case FF_TYPE_OPTION: return "option";
	case FF_TYPE_BUFFER: return "buffer";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_TEXT: return "text";
	case FF_TYPE_INTEGER: return "integer";
	default: return "other";
	}
}

/// A control whose value is a choice, a switch, a press or a count: cues
/// STEP between keys for these, and only a standard control ramps.
bool stepsBetweenCues( unsigned int type )
{
	return type == FF_TYPE_OPTION || type == FF_TYPE_BOOLEAN || type == FF_TYPE_EVENT || type == FF_TYPE_INTEGER;
}

//---------------------------------------------------------------------------
// The cue sheet.
//---------------------------------------------------------------------------
using Track = std::vector< std::pair< int, float > >;

std::map< std::string, Track > loadScript( std::istream& in, const std::string& path, std::string& error )
{
	std::map< std::string, Track > tracks;
	std::string line;
	int lineNumber = 0;
	while( std::getline( in, line ) )
	{
		++lineNumber;
		const size_t hash = line.find( '#' );
		if( hash != std::string::npos )
			line.erase( hash );
		std::istringstream words( line );
		int frame = 0;
		if( !( words >> frame ) )
			continue;
		std::vector< std::string > parts;
		std::string word;
		while( words >> word )
			parts.push_back( word );
		if( parts.size() < 2 )
		{
			error = path + ":" + std::to_string( lineNumber ) + ": expected `frame Parameter Name value`";
			return {};
		}
		const float value = std::strtof( parts.back().c_str(), nullptr );
		parts.pop_back();
		std::string name = parts.front();
		for( size_t i = 1; i < parts.size(); ++i )
			name += " " + parts[ i ];
		tracks[ name ].emplace_back( frame, value );
	}
	for( auto& entry : tracks )
		std::stable_sort( entry.second.begin(), entry.second.end(),
		                  []( const std::pair< int, float >& a, const std::pair< int, float >& b ) { return a.first < b.first; } );
	return tracks;
}

/// The value at `frame`: held before the first key and after the last; between
/// two keys linear if `ramp`, otherwise the earlier key's value until the
/// later key's frame.
float valueAt( const Track& track, int frame, bool ramp )
{
	if( track.empty() )
		return 0.0f;
	if( frame <= track.front().first )
		return track.front().second;
	if( frame >= track.back().first )
		return track.back().second;
	for( size_t i = 0; i + 1 < track.size(); ++i )
	{
		const auto& a = track[ i ];
		const auto& b = track[ i + 1 ];
		if( frame >= a.first && frame < b.first )
		{
			if( !ramp )
				return a.second;
			const float t = static_cast< float >( frame - a.first ) / static_cast< float >( b.first - a.first );
			return a.second + ( b.second - a.second ) * t;
		}
	}
	return track.back().second;
}

//---------------------------------------------------------------------------
// PNG in, for the test card's pictures: 8-bit RGB or RGBA, not interlaced,
// any of the five row filters (Chrome's encoder uses them all). Rows top
// first, RGBA, opaque if the file had no alpha.
//---------------------------------------------------------------------------
bool readPng( const std::string& path, int& width, int& height, Bytes& rgba )
{
	std::ifstream file( path, std::ios::binary );
	if( !file )
		return false;
	const Bytes data( ( std::istreambuf_iterator< char >( file ) ), std::istreambuf_iterator< char >() );
	if( data.size() < 8 || std::memcmp( data.data(), "\x89PNG\r\n\x1a\n", 8 ) != 0 )
		return false;
	auto u32 = [ & ]( size_t at ) {
		return ( static_cast< uint32_t >( data[ at ] ) << 24 ) | ( static_cast< uint32_t >( data[ at + 1 ] ) << 16 )
		       | ( static_cast< uint32_t >( data[ at + 2 ] ) << 8 ) | data[ at + 3 ];
	};
	int depth = 0, type = 0, interlace = 0;
	Bytes idat;
	for( size_t at = 8; at + 8 <= data.size(); )
	{
		const uint32_t length = u32( at );
		const std::string kind( reinterpret_cast< const char* >( &data[ at + 4 ] ), 4 );
		const size_t body = at + 8;
		if( kind == "IHDR" )
		{
			width     = static_cast< int >( u32( body ) );
			height    = static_cast< int >( u32( body + 4 ) );
			depth     = data[ body + 8 ];
			type      = data[ body + 9 ];
			interlace = data[ body + 12 ];
		}
		else if( kind == "IDAT" )
			idat.insert( idat.end(), data.begin() + static_cast< long >( body ), data.begin() + static_cast< long >( body + length ) );
		at = body + length + 4;
	}
	if( depth != 8 || ( type != 2 && type != 6 ) || interlace != 0 || width <= 0 || height <= 0 )
		return false;
	const int channels = type == 6 ? 4 : 3;
	const size_t stride = static_cast< size_t >( width ) * channels;
	Bytes raw( ( stride + 1 ) * height );
	uLongf rawSize = static_cast< uLongf >( raw.size() );
	if( uncompress( raw.data(), &rawSize, idat.data(), static_cast< uLong >( idat.size() ) ) != Z_OK || rawSize != raw.size() )
		return false;
	Bytes rows( stride * height );
	for( int y = 0; y < height; ++y )
	{
		const unsigned char filter = raw[ y * ( stride + 1 ) ];
		const unsigned char* in    = &raw[ y * ( stride + 1 ) + 1 ];
		unsigned char* out         = &rows[ y * stride ];
		const unsigned char* up    = y > 0 ? &rows[ ( y - 1 ) * stride ] : nullptr;
		for( size_t i = 0; i < stride; ++i )
		{
			const int a = i >= static_cast< size_t >( channels ) ? out[ i - channels ] : 0;
			const int b = up ? up[ i ] : 0;
			const int c = ( up && i >= static_cast< size_t >( channels ) ) ? up[ i - channels ] : 0;
			int v       = in[ i ];
			switch( filter )
			{
			case 0: break;
			case 1: v += a; break;
			case 2: v += b; break;
			case 3: v += ( a + b ) / 2; break;
			case 4:
			{
				const int p = a + b - c, pa = std::abs( p - a ), pb = std::abs( p - b ), pc = std::abs( p - c );
				v += ( pa <= pb && pa <= pc ) ? a : pb <= pc ? b : c;
				break;
			}
			default: return false;
			}
			out[ i ] = static_cast< unsigned char >( v & 0xFF );
		}
	}
	rgba.assign( static_cast< size_t >( width ) * height * 4, 255 );
	for( size_t p = 0; p < static_cast< size_t >( width ) * height; ++p )
		for( int c = 0; c < channels; ++c )
			rgba[ p * 4 + c ] = rows[ p * channels + c ];
	return true;
}

//---------------------------------------------------------------------------
// The test card's numbers (tools/fixtures/testcard.txt).
//---------------------------------------------------------------------------
std::string g_fixtures = PKTEST_FIXTURES;

struct FixtureShape
{
	int face = -1;
	double e = 0.0;
	std::string list;
	int index = 0;
	bool smooth = true, closed = true;
	std::string fill, stroke;
	double alpha = 1.0, strokeWidth = 0.0;
	std::vector< Pt > pts;
};

struct Fixture
{
	struct HashCase
	{
		std::vector< double > values;
		uint32_t h;
	};
	struct RngCase
	{
		uint32_t seed;
		double r[ 5 ];
	};
	struct TileCase
	{
		int seed;
		double x, y;
		uint32_t key;
		std::vector< int > faces;
	};
	struct TimingCase
	{
		uint32_t key;
		double t, candle, expression;
	};
	std::vector< HashCase > hashes;
	std::vector< RngCase > rngs;
	std::vector< unsigned > masks;
	std::vector< TileCase > tiles;
	std::vector< TimingCase > timings;
	std::vector< FixtureShape > shapes;
	double top = 0.0, bottom = 0.0;
};

bool loadFixture( Fixture& f )
{
	std::ifstream file( g_fixtures + "/testcard.txt" );
	if( !file )
	{
		std::fprintf( stderr, "cannot read %s/testcard.txt -- run tools/fixtures/testcard.mjs\n", g_fixtures.c_str() );
		return false;
	}
	std::string line;
	while( std::getline( file, line ) )
	{
		std::istringstream in( line );
		std::string kind;
		in >> kind;
		if( kind == "hash" )
		{
			Fixture::HashCase c;
			int n = 0;
			in >> n;
			c.values.resize( static_cast< size_t >( n ) );
			for( double& v : c.values )
				in >> v;
			in >> c.h;
			f.hashes.push_back( c );
		}
		else if( kind == "rng" )
		{
			Fixture::RngCase c;
			in >> c.seed;
			for( double& r : c.r )
				in >> r;
			f.rngs.push_back( c );
		}
		else if( kind == "masks" )
		{
			int n = 0;
			in >> n;
			f.masks.resize( static_cast< size_t >( n ) );
			for( unsigned& m : f.masks )
				in >> m;
		}
		else if( kind == "tile" )
		{
			Fixture::TileCase c;
			in >> c.seed >> c.x >> c.y >> c.key;
			c.faces.resize( f.masks.size() );
			for( int& face : c.faces )
				in >> face;
			f.tiles.push_back( c );
		}
		else if( kind == "timing" )
		{
			Fixture::TimingCase c;
			in >> c.key >> c.t >> c.candle >> c.expression;
			f.timings.push_back( c );
		}
		else if( kind == "shape" )
		{
			FixtureShape s;
			int smooth = 1, closed = 1, n = 0;
			in >> s.face >> s.e >> s.list >> s.index >> smooth >> closed >> s.fill >> s.alpha >> s.stroke >> s.strokeWidth >> n;
			s.smooth = smooth != 0;
			s.closed = closed != 0;
			s.pts.resize( static_cast< size_t >( n ) );
			for( Pt& p : s.pts )
				in >> p.x >> p.y;
			f.shapes.push_back( s );
		}
		else if( kind == "extent" )
			in >> f.top >> f.bottom;
		if( !kind.empty() && kind[ 0 ] != '#' && in.fail() )
		{
			std::fprintf( stderr, "testcard.txt: cannot read: %.80s\n", line.c_str() );
			return false;
		}
	}
	return !f.hashes.empty() && !f.tiles.empty() && !f.timings.empty() && !f.shapes.empty();
}

/// One of the pictures the test card painted (tools/fixtures/renders.txt).
struct Render
{
	std::string name;
	int w = 0, h = 0, tw = 0, th = 0, ox = 0, oy = 0, seed = 0;
	unsigned mask = 0;
	bool flicker = true, expressions = true;
	double t = 0.0;
};

std::vector< Render > loadRenders()
{
	std::vector< Render > out;
	std::ifstream file( g_fixtures + "/renders.txt" );
	std::string line;
	while( std::getline( file, line ) )
	{
		if( line.empty() || line[ 0 ] == '#' )
			continue;
		std::istringstream in( line );
		Render r;
		int flicker = 1, expressions = 1;
		in >> r.name >> r.w >> r.h >> r.tw >> r.th >> r.ox >> r.oy >> r.seed >> r.mask >> flicker >> expressions >> r.t;
		r.flicker     = flicker != 0;
		r.expressions = expressions != 0;
		if( !in.fail() )
			out.push_back( r );
	}
	return out;
}

std::string hexOf( uint32_t v )
{
	return fmt( "%06x", v );
}

//---------------------------------------------------------------------------
// The rig.
//---------------------------------------------------------------------------
GLuint makeTexture( int width, int height, const float* pixels )
{
	GLuint id = 0;
	glGenTextures( 1, &id );
	glBindTexture( GL_TEXTURE_2D, id );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, pixels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return id;
}

struct Rig
{
	PumpkinPlugin plugin;
	int width = 0, height = 0;
	GLuint outputTexture = 0, outputFBO = 0;
	int frame          = 0;
	double fps         = 60.0;
	double clockOffset = 0.0;
	/// The host's clock unit: 1 sends seconds, 1000 milliseconds.
	double hostUnit = 1.0;

	ProcessOpenGLStruct process = {};

	~Rig()
	{
		plugin.DeInitGL();
		release();
	}

	void release()
	{
		if( outputFBO )
			glDeleteFramebuffers( 1, &outputFBO );
		if( outputTexture )
			glDeleteTextures( 1, &outputTexture );
		outputFBO = outputTexture = 0;
	}

	bool attach( int w, int h )
	{
		width         = w;
		height        = h;
		outputTexture = makeTexture( width, height, nullptr );
		glGenFramebuffers( 1, &outputFBO );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glFramebufferTexture2D( GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0 );
		if( glCheckFramebufferStatus( GL_FRAMEBUFFER ) != GL_FRAMEBUFFER_COMPLETE )
			return false;
		process.HostFBO = outputFBO;
		return true;
	}

	bool Init( int w, int h )
	{
		FFGLViewportStruct viewport = {};
		viewport.width              = static_cast< FFUInt32 >( w );
		viewport.height             = static_cast< FFUInt32 >( h );
		if( plugin.InitGL( &viewport ) != FF_SUCCESS )
		{
			std::fprintf( stderr, "InitGL failed -- see ~/Library/Logs/pumpkin\n" );
			return false;
		}
		plugin.SetClockScaleForTest( 1.0 );
		return attach( w, h );
	}

	/// The host's raster changes under a running instance: no InitGL (Arena
	/// calls neither InitGL nor FF_RESIZE on a running clip).
	bool Resize( int w, int h )
	{
		release();
		return attach( w, h );
	}

	void Set( unsigned int id, float value )
	{
		plugin.SetById( id, value );
	}

	double TimeOf( int f ) const
	{
		return clockOffset + static_cast< double >( f ) / fps;
	}

	bool Render( int frames = 1 )
	{
		for( int i = 0; i < frames; ++i )
		{
			plugin.SetTime( TimeOf( frame ) * hostUnit );
			++frame;
			glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
			glViewport( 0, 0, width, height );
			glClearColor( 0.0f, 0.0f, 0.0f, 0.0f );
			glClear( GL_COLOR_BUFFER_BIT );
			if( plugin.ProcessOpenGL( &process ) != FF_SUCCESS )
			{
				std::fprintf( stderr, "ProcessOpenGL failed\n" );
				return false;
			}
		}
		return true;
	}

	/// One frame at story time t exactly, on the Resolume clock.
	bool RenderAt( double t )
	{
		plugin.SetStoryForTest( t );
		return Render( 1 );
	}

	/// Floats, row 0 at the BOTTOM (GL's order).
	Floats Output() const
	{
		Floats pixels( static_cast< size_t >( width ) * height * 4 );
		glBindFramebuffer( GL_FRAMEBUFFER, outputFBO );
		glPixelStorei( GL_PACK_ALIGNMENT, 1 );
		glReadPixels( 0, 0, width, height, GL_RGBA, GL_FLOAT, pixels.data() );
		return pixels;
	}

	/// 8-bit RGBA, rows TOP first: the canvas's order.
	Bytes Picture() const
	{
		const Floats f = Output();
		Bytes out( f.size() );
		for( int y = 0; y < height; ++y )
			for( int x = 0; x < width * 4; ++x )
				out[ static_cast< size_t >( y ) * width * 4 + x ] = static_cast< unsigned char >(
					std::lround( std::clamp( f[ static_cast< size_t >( height - 1 - y ) * width * 4 + x ], 0.0f, 1.0f ) * 255.0f ) );
		return out;
	}
};

void setFaces( Rig& rig, unsigned mask )
{
	for( int f = 0; f < kFaceCount; ++f )
		rig.Set( PT_FACE_FIRST + static_cast< unsigned int >( f ), ( mask >> f ) & 1u ? 1.0f : 0.0f );
}

/// A rig set as one of the test card's renders.
void configure( Rig& rig, const Render& r )
{
	rig.Set( PT_TILE_WIDTH, static_cast< float >( r.tw ) );
	rig.Set( PT_TILE_HEIGHT, static_cast< float >( r.th ) );
	rig.Set( PT_ORIGIN_X, static_cast< float >( r.ox ) );
	rig.Set( PT_ORIGIN_Y, static_cast< float >( r.oy ) );
	rig.Set( PT_SEED, static_cast< float >( r.seed ) );
	setFaces( rig, r.mask );
	rig.Set( PT_FLICKER, r.flicker ? 1.0f : 0.0f );
	rig.Set( PT_EXPRESSIONS, r.expressions ? 1.0f : 0.0f );
	rig.Set( PT_CLOCK, static_cast< float >( Clock::Resolume ) );
	rig.Set( PT_BACKGROUND, static_cast< float >( Background::Night ) );
}

//---------------------------------------------------------------------------
// Parameters by display name: an option by its element's name or index; a
// value that is not wholly a number or a name is refused (polyhedral's trap:
// strtof( "Fixed" ) is 0, silently).
//---------------------------------------------------------------------------
struct NamedParameter
{
	std::string name;
	unsigned int index;
	float value;
	unsigned int type;
};

std::vector< NamedParameter > listParameters( PumpkinPlugin& plugin )
{
	std::vector< NamedParameter > list;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const char* const name = plugin.GetParamName( i );
		list.push_back( NamedParameter { name ? name : "?", i, plugin.GetFloatParameter( i ), plugin.GetParamType( i ) } );
	}
	return list;
}

bool parseValue( PumpkinPlugin& plugin, const NamedParameter& parameter, const std::string& text, float& value )
{
	char* end     = nullptr;
	const float v = std::strtof( text.c_str(), &end );
	if( !text.empty() && end && *end == '\0' )
	{
		value = v;
		return true;
	}
	if( parameter.type == FF_TYPE_OPTION )
		for( unsigned int e = 0; e < 16; ++e )
		{
			const char* element = plugin.GetParamElementName( parameter.index, e );
			if( element && text == element )
			{
				value = static_cast< float >( e );
				return true;
			}
		}
	if( parameter.type == FF_TYPE_BOOLEAN && ( text == "on" || text == "off" ) )
	{
		value = text == "on" ? 1.0f : 0.0f;
		return true;
	}
	return false;
}

bool applySetting( PumpkinPlugin& plugin, const std::string& assignment, std::string& error )
{
	const size_t equals = assignment.find( '=' );
	if( equals == std::string::npos )
	{
		error = "expected Name=Value";
		return false;
	}
	const std::string name  = assignment.substr( 0, equals );
	const std::string value = assignment.substr( equals + 1 );
	for( const NamedParameter& parameter : listParameters( plugin ) )
		if( parameter.name == name )
		{
			float v = 0.0f;
			if( !parseValue( plugin, parameter, value, v ) )
			{
				error = "'" + value + "' is not a number" + ( parameter.type == FF_TYPE_OPTION ? " or an option" : "" );
				return false;
			}
			plugin.SetFloatParameter( parameter.index, v );
			return true;
		}
	error = "no parameter called '" + name + "'";
	return false;
}

struct Cue
{
	Track track;
	bool ramp;
};

/// The cue sheet bound to the plugin's parameters, or an error naming the cue.
bool bindScript( PumpkinPlugin& plugin, const std::string& path, std::map< unsigned int, Cue >& out, std::string& error )
{
	std::ifstream file( path );
	if( !file )
	{
		error = "cannot open " + path;
		return false;
	}
	const std::map< std::string, Track > tracks = loadScript( file, path, error );
	if( !error.empty() )
		return false;
	const std::vector< NamedParameter > known = listParameters( plugin );
	for( const auto& entry : tracks )
	{
		bool found = false;
		for( const NamedParameter& parameter : known )
			if( parameter.name == entry.first )
			{
				out[ parameter.index ] = Cue { entry.second, !stepsBetweenCues( parameter.type ) };
				found                  = true;
			}
		if( !found )
		{
			error = "script names '" + entry.first + "', which is not a parameter (try --list)";
			return false;
		}
	}
	return true;
}

//===========================================================================
// --pipe and --film. Raw RGBA, top row first, on the synthetic clock.
//===========================================================================
/// Frames are made -- `count` of them, or, with `count` 0, until the reader
/// hangs up (so that mode only ever ends with exit 1; use a count for a take
/// that can end cleanly).
int runPipe( int width, int height, double fps, const std::string& scriptPath, int count,
             const std::vector< std::string >& settings )
{
	Rig rig;
	rig.fps = fps;
	if( !rig.Init( width, height ) )
		return 1;
	for( const std::string& setting : settings )
	{
		std::string error;
		if( !applySetting( rig.plugin, setting, error ) )
		{
			std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
			return 2;
		}
	}
	//A misspelt cue that silently did nothing would film a take that looks
	//deliberate and is wrong: refuse any name that is not a parameter.
	std::map< unsigned int, Cue > automation;
	if( !scriptPath.empty() )
	{
		std::string error;
		if( !bindScript( rig.plugin, scriptPath, automation, error ) )
		{
			std::fprintf( stderr, "%s\n", error.c_str() );
			return 2;
		}
	}
	for( int index = 0; count <= 0 || index < count; ++index )
	{
		for( const auto& cue : automation )
			rig.plugin.SetFloatParameter( cue.first, valueAt( cue.second.track, index, cue.second.ramp ) );
		if( !rig.Render( 1 ) )
			return 1;
		Bytes bytes = rig.Picture();
		//The source is premultiplied: written over black, alpha opaque.
		for( size_t i = 3; i < bytes.size(); i += 4 )
			bytes[ i ] = 255;
		size_t written = 0;
		while( written < bytes.size() )
		{
			const ssize_t put = write( STDOUT_FILENO, bytes.data() + written, bytes.size() - written );
			//The reader has gone. SIGPIPE is ignored in main(), so this is EPIPE
			//and not a silent 141: say so and stop.
			if( put <= 0 )
			{
				std::fprintf( stderr, "stdout closed at frame %d\n", index );
				return 1;
			}
			written += static_cast< size_t >( put );
		}
	}
	return 0;
}

//===========================================================================
// The checks.
//===========================================================================
struct Perturb
{
	bool portKeyNudged      = false;///< --port: each tile keyed one pixel along
	bool portCandleOrder    = false;///< --port: the frequencies drawn before the phases
	bool portHolesUnscaled  = false;///< --port: a hole expected the size of its wall
	bool linearSquared      = false;///< --linear: expressions expected to move as e^2
	bool smoothingOff       = false;///< --smoothing: ellipses drawn at the n-gon's own radius
	bool testcardSkew       = false;///< --testcard: every face dealt one along
	bool testcardLate       = false;///< --testcard: the plugin a quarter second behind
	bool tilesNightByPlace  = false;///< --tiles: a tile's night expected from its pixel place
	bool tilesOriginColours = false;///< --tiles: a whole-tile move expected to keep the nights too
	bool clockFloat         = false;///< --clock: elapsed time from a float host clock
	bool clockEpoch1970     = false;///< --clock: the Wall clock expected in seconds since 1970
	bool stillFlicker       = false;///< --still: the steady patch rendered with Flicker on
	bool resizeStale        = false;///< --resize: the tiles not dealt again when the raster grows
};

using CheckFn = int ( * )( const Perturb& );

/// The rasters a picture check runs at: the one developed at, and CI's.
struct Raster
{
	int w, h;
};
std::vector< Raster > kRasters = { { 1280, 720 }, { 320, 180 } };

/// Two pictures that should be the same. Exactly the same on a GPU; on
/// Apple's software renderer, which does not repeat a draw bit for bit (two
/// draws of one frame in one context differ by a float ulp or two), within
/// 1e-6 -- far above that noise and far below one 8-bit code (0.004).
struct Sameness
{
	size_t differ = 0;
	double worst  = 0.0;
};
Sameness compare( const Floats& a, const Floats& b )
{
	Sameness s;
	const double tolerance = g_software ? 1e-6 : 0.0;
	for( size_t i = 0; i < a.size() && i < b.size(); ++i )
	{
		const double d = std::fabs( static_cast< double >( a[ i ] ) - b[ i ] );
		s.worst        = std::max( s.worst, d );
		s.differ += d > tolerance;
	}
	if( a.size() != b.size() )
		s.differ += std::max( a.size(), b.size() );
	return s;
}

//===========================================================================
// --port (no GL): the port against the test card's own numbers. Hashes,
// keys and faces must agree exactly; the rng exactly (a uint32 over 2^32 is
// exact in a double); the timing and the drawing to 1e-12 -- the same double
// arithmetic in the same order, where only libm's sin and cos and V8's may
// differ, by an ulp.
//===========================================================================
int runPort( const Perturb& perturb )
{
	std::printf( "\n=== port: the randomness, the patch, the timing and the drawing against the test card's own numbers\n" );
	Fixture f;
	if( !loadFixture( f ) )
	{
		Check( false, "the test card's fixture loads" );
		return Verdict();
	}

	int wrong = 0;
	for( const auto& c : f.hashes )
	{
		uint32_t h = 0;
		switch( c.values.size() )
		{
		case 1: h = Hash( { c.values[ 0 ] } ); break;
		case 2: h = Hash( { c.values[ 0 ], c.values[ 1 ] } ); break;
		case 3: h = Hash( { c.values[ 0 ], c.values[ 1 ], c.values[ 2 ] } ); break;
		case 4: h = Hash( { c.values[ 0 ], c.values[ 1 ], c.values[ 2 ], c.values[ 3 ] } ); break;
		default: ++wrong; continue;
		}
		wrong += h != c.h;
	}
	for( const auto& c : f.rngs )
	{
		Rng r( c.seed );
		for( double expected : c.r )
			wrong += r() != expected;
	}
	Check( wrong == 0, fmt( "hash and rng: %zu hashes and %zu five-draw sequences, %d differ", f.hashes.size(), f.rngs.size(), wrong ) );

	int keys = 0, faces = 0;
	for( const auto& c : f.tiles )
	{
		const uint32_t key = TileKey( c.seed, c.x + ( perturb.portKeyNudged ? 1.0 : 0.0 ), c.y );
		keys += key != c.key;
		for( size_t m = 0; m < f.masks.size(); ++m )
			faces += TileFace( key, f.masks[ m ] ) != c.faces[ m ];
	}
	Check( keys == 0 && faces == 0,
	       fmt( "the patch: %zu tiles over 5 seeds and 3 grids, %d keys and %d of %zu faces differ (sets of faces: all, one, two, three, none)",
	            f.tiles.size(), keys, faces, f.tiles.size() * f.masks.size() ) );

	double candleErr = 0.0, exprErr = 0.0, clockErr = 0.0;
	for( const auto& c : f.timings )
	{
		TileClock clock = TileClock::For( c.key );
		if( perturb.portCandleOrder )
		{
			//The same six draws, read frequencies first, then the phases.
			Rng r( Hash( { static_cast< double >( c.key ), 501.0 } ) );
			const double hz[ 3 ] = { 1.3, 3.1, 5.3 };
			for( int i = 0; i < 3; ++i )
				clock.candleCycles[ i ] = kLoopSeconds * hz[ i ] * r.Between( 0.85, 1.15 );
			for( int i = 0; i < 3; ++i )
				clock.candleOffset[ i ] = r();
		}
		candleErr = std::max( candleErr, std::fabs( clock.Candle( c.t ) - c.candle ) );
		exprErr   = std::max( exprErr, std::fabs( clock.Expression( c.t ) - c.expression ) );
		clockErr  = std::max( { clockErr, std::fabs( CandleLevel( c.t, c.key ) - TileClock::For( c.key ).Candle( c.t ) ),
		                        std::fabs( ExpressionLevel( c.t, c.key ) - TileClock::For( c.key ).Expression( c.t ) ) } );
	}
	Check( candleErr <= 1e-12 && exprErr <= 1e-12,
	       fmt( "the timing: %zu candles and expressions, t from 0 to 3.15e8 s, worst %.2g and %.2g (1e-12)", f.timings.size(),
	            candleErr, exprErr ) );
	Check( clockErr == 0.0, fmt( "the per-tile clock (draws once, phases per frame) is CandleLevel and ExpressionLevel bit for bit (worst %.2g)", clockErr ) );

	int shapeWrong = 0, styleWrong = 0, countWrong = 0;
	double worst = 0.0;
	for( const FixtureShape& s : f.shapes )
	{
		const Lantern l                  = MakeLantern( s.face, s.e );
		const std::vector< Shape >* list = s.list == "body" ? &l.body : s.list == "wall" ? &l.walls : &l.holes;
		if( s.list == "hole" && perturb.portHolesUnscaled )
			list = &l.walls;
		if( s.index >= static_cast< int >( list->size() ) )
		{
			++countWrong;
			continue;
		}
		const Shape& mine = ( *list )[ static_cast< size_t >( s.index ) ];
		if( mine.pts.size() != s.pts.size() )
		{
			++countWrong;
			continue;
		}
		double err = 0.0;
		for( size_t i = 0; i < s.pts.size(); ++i )
			err = std::max( { err, std::fabs( mine.pts[ i ].x - s.pts[ i ].x ), std::fabs( mine.pts[ i ].y - s.pts[ i ].y ) } );
		worst = std::max( worst, err );
		shapeWrong += err > 1e-12;
		const bool style = mine.smooth == s.smooth && mine.closed == s.closed
		                   && ( s.fill == "-" ? !mine.hasFill || s.list != "body" : mine.hasFill && hexOf( mine.fill ) == s.fill )
		                   && ( s.stroke == "-" ? !mine.hasStroke : mine.hasStroke && hexOf( mine.stroke ) == s.stroke )
		                   && std::fabs( mine.alpha - s.alpha ) == 0.0 && std::fabs( mine.strokeWidth - s.strokeWidth ) == 0.0;
		styleWrong += !style;
	}
	size_t expectedShapes = 0;
	for( int face = -1; face < kFaceCount; ++face )
		for( double e : { 0.0, 0.25, 0.5, 1.0 } )
		{
			const Lantern l = MakeLantern( face, e );
			expectedShapes += l.body.size() + l.walls.size() + l.holes.size();
		}
	Check( shapeWrong == 0 && countWrong == 0 && expectedShapes == f.shapes.size(),
	       fmt( "the drawing: %zu shapes (plain and six faces, each at expression 0, 0.25, 0.5 and 1), %d off, %d missing, "
	            "worst point %.2g (1e-12)", f.shapes.size(), shapeWrong, countWrong, worst ) );
	Check( styleWrong == 0, fmt( "the colours, strokes, alphas and which shapes are smoothed and closed: %d differ", styleWrong ) );
	Check( f.top == kPumpkinTop && f.bottom == kPumpkinBottom, fmt( "the extent the painter sizes by: %g to %g", f.top, f.bottom ) );
	return Verdict();
}

//===========================================================================
// --linear (no GL): every vertex moves linearly with the expression, which is
// what lets the shader store each at e = 0 and e = 1 and mix them.
//===========================================================================
int runLinear( const Perturb& perturb )
{
	std::printf( "\n=== linear: each face's blink, wink or chomp moves every point as a + e (b - a)\n" );
	double worst = 0.0, worstEllipse = 0.0;
	int points   = 0;
	for( int face = 0; face < kFaceCount; ++face )
	{
		const Lantern a = MakeLantern( face, 0.0 ), b = MakeLantern( face, 1.0 );
		for( double e : { 0.25, 0.5, 0.75 } )
		{
			const Lantern m = MakeLantern( face, e );
			const double k  = perturb.linearSquared ? e * e : e;
			for( int list = 0; list < 2; ++list )
			{
				const auto& la = list ? a.holes : a.walls;
				const auto& lb = list ? b.holes : b.walls;
				const auto& lm = list ? m.holes : m.walls;
				for( size_t s = 0; s < lm.size(); ++s )
				{
					for( size_t i = 0; i < lm[ s ].pts.size(); ++i, ++points )
					{
						const Pt p = lm[ s ].pts[ i ], pa = la[ s ].pts[ i ], pb = lb[ s ].pts[ i ];
						worst      = std::max( { worst, std::fabs( pa.x + k * ( pb.x - pa.x ) - p.x ), std::fabs( pa.y + k * ( pb.y - pa.y ) - p.y ) } );
					}
					if( lm[ s ].isEllipse )
					{
						const EllipseSpec &em = lm[ s ].ellipse, &ea = la[ s ].ellipse, &eb = lb[ s ].ellipse;
						for( auto field : { &EllipseSpec::cx, &EllipseSpec::cy, &EllipseSpec::rx, &EllipseSpec::ry } )
							worstEllipse = std::max( worstEllipse, std::fabs( ea.*field + k * ( eb.*field - ea.*field ) - em.*field ) );
					}
				}
			}
		}
	}
	Check( worst <= 1e-12 && worstEllipse <= 1e-12,
	       fmt( "%d points over six faces at e = 0.25, 0.5 and 0.75, worst %.2g off the line; ellipses %.2g (1e-12)", points, worst,
	            worstEllipse ) );

	//The table the shader is given: its vertices mixed at e are the
	//flattened drawing at e, to float storage (a few float ulps at 0.6).
	const geometry::Table& table = geometry::Build();
	double tableWorst            = 0.0;
	for( int face = 0; face < kFaceCount; ++face )
	{
		const Lantern a = MakeLantern( face, 0.0 ), b = MakeLantern( face, 1.0 );
		for( double e : { 0.3, 0.6 } )
		{
			const Lantern m = MakeLantern( face, e );
			const double k  = perturb.linearSquared ? e * e : e;
			std::vector< const Shape* > ma, mb, mm;
			for( size_t i = 0; i < m.walls.size(); ++i )
				ma.push_back( &a.walls[ i ] ), mb.push_back( &b.walls[ i ] ), mm.push_back( &m.walls[ i ] );
			for( size_t i = 0; i < m.holes.size(); ++i )
				ma.push_back( &a.holes[ i ] ), mb.push_back( &b.holes[ i ] ), mm.push_back( &m.holes[ i ] );
			for( size_t s = 0; s < mm.size(); ++s )
			{
				const geometry::RenderShape& r = table.shapes[ static_cast< size_t >( table.faceFirst[ face ] ) + s ];
				if( r.kind == geometry::kEllipse )
					continue;
				std::vector< Pt > want = mm[ s ]->pts;
				if( mm[ s ]->smooth && want.size() >= 3 )
					want = geometry::Flatten( *mm[ s ], geometry::StepsFor( *ma[ s ], *mb[ s ] ) );
				for( size_t i = 0; i < want.size(); ++i )
				{
					const float* v = &r.vertices[ i * 4 ];
					tableWorst     = std::max( { tableWorst, std::fabs( v[ 0 ] + k * ( v[ 2 ] - v[ 0 ] ) - want[ i ].x ),
                                             std::fabs( v[ 1 ] + k * ( v[ 3 ] - v[ 1 ] ) - want[ i ].y ) } );
				}
			}
		}
	}
	Check( tableWorst <= 1e-6, fmt( "the shader's vertex table, mixed at e = 0.3 and 0.6, is the drawing at those e: worst %.2g (1e-6, float storage)", tableWorst ) );
	return Verdict();
}

//===========================================================================
// --smoothing (no GL): what the shader draws against the curve the canvas
// draws. The canvas traces each smoothed list as quadratics through its edge
// midpoints; sampled exactly, that curve must lie within 0.1 px of the
// shader's ellipse or polyline for a pumpkin 4096 px wide.
//===========================================================================
double ellipseDistance( Pt q, const float e[ 4 ], double cs, double sn )
{
	const double dx = q.x - e[ 0 ], dy = q.y - e[ 1 ];
	const double lx = cs * dx + sn * dy, ly = -sn * dx + cs * dy;
	const double ux = lx / e[ 2 ], uy = ly / e[ 3 ];
	const double l  = std::max( std::hypot( ux, uy ), 1e-12 );
	const double gx = ux / ( l * e[ 2 ] ), gy = uy / ( l * e[ 3 ] );
	return ( l - 1.0 ) / std::hypot( gx, gy );
}

double polylineDistance( Pt q, const std::vector< float >& v, int which, bool closed )
{
	const size_t n = v.size() / 4;
	double best    = 1e9;
	for( size_t i = 0; i < n; ++i )
	{
		if( !closed && i + 1 == n )
			break;
		const size_t j = ( i + 1 ) % n;
		const double ax = v[ i * 4 + which * 2 ], ay = v[ i * 4 + which * 2 + 1 ];
		const double bx = v[ j * 4 + which * 2 ], by = v[ j * 4 + which * 2 + 1 ];
		const double ex = bx - ax, ey = by - ay;
		const double t  = std::clamp( ( ( q.x - ax ) * ex + ( q.y - ay ) * ey ) / std::max( ex * ex + ey * ey, 1e-30 ), 0.0, 1.0 );
		best            = std::min( best, std::hypot( q.x - ax - t * ex, q.y - ay - t * ey ) );
	}
	return best;
}

int runSmoothing( const Perturb& perturb )
{
	std::printf( "\n=== smoothing: the shader's ellipses and polylines against the canvas's curves, at a pumpkin 4096 px wide\n" );
	const geometry::Table& table = geometry::Build();
	struct Source
	{
		const Shape* shape;
		int which;///< 0: e = 0, 1: e = 1
		size_t row;
	};
	const Lantern plain = MakeLantern( -1, 0.0 );
	std::vector< Lantern > rest, height;
	for( int f = 0; f < kFaceCount; ++f )
	{
		rest.push_back( MakeLantern( f, 0.0 ) );
		height.push_back( MakeLantern( f, 1.0 ) );
	}
	std::vector< Source > sources;
	for( size_t i = 0; i < plain.body.size(); ++i )
		sources.push_back( { &plain.body[ i ], 0, i } );
	for( int f = 0; f < kFaceCount; ++f )
		for( int which = 0; which < 2; ++which )
		{
			const Lantern& l = which ? height[ static_cast< size_t >( f ) ] : rest[ static_cast< size_t >( f ) ];
			size_t row       = static_cast< size_t >( table.faceFirst[ f ] );
			for( const Shape& s : l.walls )
				sources.push_back( { &s, which, row++ } );
			for( const Shape& s : l.holes )
				sources.push_back( { &s, which, row++ } );
		}

	double ellipseWorst = 0.0, lineWorst = 0.0;
	int ellipses = 0, lines = 0;
	for( const Source& src : sources )
	{
		const Shape& s = *src.shape;
		if( !s.smooth || s.pts.size() < 3 )
			continue;
		const geometry::RenderShape& r = table.shapes[ src.row ];
		float ell[ 4 ];
		std::copy( src.which ? r.ellipse1 : r.ellipse0, ( src.which ? r.ellipse1 : r.ellipse0 ) + 4, ell );
		if( perturb.smoothingOff && r.kind == geometry::kEllipse )
		{
			const double k = geometry::SmoothedRadius( s.ellipse.n );
			ell[ 2 ] /= static_cast< float >( k );
			ell[ 3 ] /= static_cast< float >( k );
		}
		for( int q = 0; q < geometry::QuadraticCount( s ); ++q )
			for( int k = 0; k <= 32; ++k )
			{
				const Pt p = geometry::OnCurve( s, q, k / 32.0 );
				if( r.kind == geometry::kEllipse )
					ellipseWorst = std::max( ellipseWorst, std::fabs( ellipseDistance( p, ell, r.cosR, r.sinR ) ) );
				else
					lineWorst = std::max( lineWorst, polylineDistance( p, r.vertices, src.which, r.kind == geometry::kPolygon ) );
			}
		( r.kind == geometry::kEllipse ? ellipses : lines )++;
	}
	const double px = geometry::kFlattenSizePx;
	Check( ellipseWorst * px <= geometry::kFlattenTolerancePx,
	       fmt( "%d smoothed n-gons drawn as ellipses: the canvas's curve within %.4f px of them (0.1)", ellipses, ellipseWorst * px ) );
	Check( lineWorst * px <= geometry::kFlattenTolerancePx,
	       fmt( "%d smoothed lists drawn flattened: the canvas's curve within %.4f px of the pieces (0.1)", lines, lineWorst * px ) );
	return Verdict();
}

//===========================================================================
// --testcard: the picture against the test card's own, painted by its
// drawPumpkins in Chrome (tools/fixtures/*.png), at each render's raster,
// tiles, seed, faces and time.
//
// Two rasterisers never agree on an edge pixel: Skia's coverage and this
// shader's distance-based coverage differ by up to about half a pixel's
// worth along every edge, and Skia composites each shape into 8 bits as it
// goes where the shader rounds once. So the measures are of the whole
// picture and of the pixels off the edges:
//   - the mean difference, over every channel, at most 1 code value;
//   - at most 0.5% of pixels more than 24 codes out (the edges of a 32 px
//     pumpkin are a few per cent of its tile, and they are mostly within that);
//   - where the test card's picture is flat (the 3 x 3 block round a pixel
//     all within 4 codes of it), at most 1 pixel in 10,000 more than 4 codes
//     out and none more than 24. Measured: flat areas agree to 1 or 2 codes,
//     except a pixel or so per render beside a junction of rib strokes, where
//     the card's block is flat and an edge here lands a fraction of a pixel
//     over (the worst, 14 codes, in steady-640x360).
// A face dealt wrongly, a candle at the wrong brightness or a shadow drawn
// darker breaks at least one of them.
//===========================================================================
constexpr int kFlatTolerance = 4;

int runTestcard( const Perturb& perturb )
{
	std::printf( "\n=== testcard: the picture against the test card's own drawPumpkins, painted in Chrome\n" );
	const std::vector< Render > renders = loadRenders();
	Check( renders.size() >= 5, fmt( "%zu of the test card's renders listed in renders.txt", renders.size() ) );
	for( const Render& r : renders )
	{
		int w = 0, h = 0;
		Bytes want;
		if( !readPng( g_fixtures + "/" + r.name + ".png", w, h, want ) || w != r.w || h != r.h )
		{
			Check( false, r.name + ".png reads, at its listed size" );
			continue;
		}
		Rig rig;
		if( !rig.Init( r.w, r.h ) )
			return 1;
		configure( rig, r );
		if( perturb.testcardSkew )
			rig.plugin.SetFaceSkewForTest( 1 );
		if( !rig.RenderAt( r.t + ( perturb.testcardLate ? -0.25 : 0.0 ) ) )
			return 1;
		const Bytes got = rig.Picture();
		const size_t pixels = static_cast< size_t >( w ) * h;
		double sum          = 0.0;
		size_t far = 0, flatWrong = 0;
		int worst = 0;
		std::vector< int > diff( pixels );
		for( size_t p = 0; p < pixels; ++p )
		{
			int d = 0;
			for( int c = 0; c < 3; ++c )
			{
				const int one = std::abs( static_cast< int >( got[ p * 4 + c ] ) - static_cast< int >( want[ p * 4 + c ] ) );
				sum += one;
				d = std::max( d, one );
			}
			diff[ p ] = d;
			far += d > 24;
			worst = std::max( worst, d );
		}
		//Flat in the test card's picture: every channel of the 3 x 3 block
		//within 4 codes of the centre's.
		int flatWorst = 0;
		size_t flatCount = 0, flatFar = 0;
		for( int y = 1; y + 1 < h; ++y )
			for( int x = 1; x + 1 < w; ++x )
			{
				const size_t p = static_cast< size_t >( y ) * w + x;
				bool flat      = true;
				for( int dy = -1; dy <= 1 && flat; ++dy )
					for( int dx = -1; dx <= 1 && flat; ++dx )
						for( int c = 0; c < 3 && flat; ++c )
							flat = std::abs( static_cast< int >( want[ ( p + dy * w + dx ) * 4 + c ] ) - static_cast< int >( want[ p * 4 + c ] ) ) <= 4;
				if( !flat )
					continue;
				++flatCount;
				flatWorst = std::max( flatWorst, diff[ p ] );
				flatWrong += diff[ p ] > kFlatTolerance;
				flatFar += diff[ p ] > 24;
			}
		const double mean = sum / ( pixels * 3.0 );
		const double farPct = 100.0 * far / pixels;
		Check( mean <= 1.0 && farPct <= 0.5 && flatWrong * 10000 <= flatCount && flatFar == 0,
		       fmt( "%-15s %4dx%-4d tiles %dx%d, seed %d, t %.2f: mean %.3f codes (1), %.3f%% of pixels > 24 out (0.5%%); "
		            "where the card is flat, %zu of %zu > %d out (1 in 10,000) and %zu > 24 (0), worst %d",
		            r.name.c_str(), r.w, r.h, r.tw, r.th, r.seed, r.t, mean, farPct, flatWrong, flatCount, kFlatTolerance, flatFar,
		            flatWorst ) );
	}
	return Verdict();
}

//===========================================================================
// --tiles: the grid. Every tile's corners are its own night, by its column
// and row as the test card counts them from the origin; and the patch is
// keyed on the surface, so moving the origin a whole tile changes no pumpkin.
//===========================================================================
int runTiles( const Perturb& perturb )
{
	std::printf( "\n=== tiles: each tile its own night by column and row; the patch keyed on the surface\n" );
	for( const Raster& raster : kRasters )
	{
		const int tw = std::max( 16, raster.w / 7 ), th = std::max( 16, raster.h / 3 );
		const int ox = tw / 3, oy = -th / 4;
		Rig rig;
		if( !rig.Init( raster.w, raster.h ) )
			return 1;
		rig.Set( PT_TILE_WIDTH, static_cast< float >( tw ) );
		rig.Set( PT_TILE_HEIGHT, static_cast< float >( th ) );
		rig.Set( PT_ORIGIN_X, static_cast< float >( ox ) );
		rig.Set( PT_ORIGIN_Y, static_cast< float >( oy ) );
		setFaces( rig, 0 );//plain pumpkins: no glow over the corners
		if( !rig.RenderAt( 3.0 ) )
			return 1;
		const Bytes pic = rig.Picture();
		int corners = 0, wrong = 0;
		for( int row = -2; row < raster.h / th + 2; ++row )
			for( int col = -2; col < raster.w / tw + 2; ++col )
			{
				const int x0 = ox + col * tw, y0 = oy + row * th;
				const int night = perturb.tilesNightByPlace ? NightIndex( x0 / tw, y0 / th ) : NightIndex( col, row );
				for( const int x : { x0, x0 + tw - 1 } )
					for( const int y : { y0, y0 + th - 1 } )
					{
						if( x < 0 || y < 0 || x >= raster.w || y >= raster.h )
							continue;
						++corners;
						const uint32_t c = kTileNights[ night ];
						const unsigned char* p = &pic[ ( static_cast< size_t >( y ) * raster.w + x ) * 4 ];
						wrong += p[ 0 ] != ( ( c >> 16 ) & 0xFF ) || p[ 1 ] != ( ( c >> 8 ) & 0xFF ) || p[ 2 ] != ( c & 0xFF );
					}
			}
		Check( corners > 8 && wrong == 0,
		       fmt( "%dx%d, tiles %dx%d from (%d, %d): %d tile corners, %d not their tile's night", raster.w, raster.h, tw, th, ox, oy, corners, wrong ) );

		//A whole tile along: the same tiles in the same places, so the same
		//pumpkins -- only the nights, counted by column and row, move on.
		Rig a, b;
		if( !a.Init( raster.w, raster.h ) || !b.Init( raster.w, raster.h ) )
			return 1;
		for( Rig* r : { &a, &b } )
		{
			r->Set( PT_TILE_WIDTH, static_cast< float >( tw ) );
			r->Set( PT_TILE_HEIGHT, static_cast< float >( th ) );
			r->Set( PT_BACKGROUND, static_cast< float >( perturb.tilesOriginColours ? Background::Night : Background::None ) );
		}
		b.Set( PT_ORIGIN_X, static_cast< float >( tw ) );
		b.Set( PT_ORIGIN_Y, static_cast< float >( -th ) );
		if( !a.RenderAt( 5.5 ) || !b.RenderAt( 5.5 ) )
			return 1;
		const Floats pa = a.Output(), pb = b.Output();
		const Sameness s = compare( pa, pb );
		Check( s.differ == 0, fmt( "%dx%d: the origin moved a whole tile (%d, %d), Background None: %zu of %zu values differ (worst %.2g)",
		                           raster.w, raster.h, tw, -th, s.differ, pa.size(), s.worst ) );
	}
	return Verdict();
}

//===========================================================================
// --clock: whose time the candles keep.
//===========================================================================
int runClock( const Perturb& perturb )
{
	std::printf( "\n=== clock: Resolume's time in double, Speed, a scrub, and the test card's wall clock\n" );
	//Resolume's clock has been measured at 499,217,238 ms, sent as ms.
	{
		Rig rig;
		if( !rig.Init( 160, 90 ) )
			return 1;
		rig.plugin.SetClockScaleForTest( 0.0 );//vote on the unit, as in a host
		rig.hostUnit    = 1000.0;
		rig.clockOffset = 499217.238;
		rig.plugin.SetFloatClockForTest( perturb.clockFloat );
		//The unit vote needs the wall clock to agree: the first frames are
		//paced in real time, as a host would pace them (langton's).
		for( int paced = 0; rig.plugin.ClockScale() == 0.0 && paced < 60; ++paced )
		{
			if( !rig.Render( 1 ) )
				return 1;
			std::this_thread::sleep_for( std::chrono::microseconds( 16667 ) );
		}
		if( !rig.Render( 2 ) )
			return 1;
		Check( rig.plugin.ClockScale() == 0.001, "the host's clock, voted on against the wall clock, is in milliseconds" );
		//Frame by frame, not the total: a float clock there moves in 1/32 s
		//steps, and over a whole number of them (2 s is 64) its errors cancel.
		const double s0 = rig.plugin.StoryTime();
		double worstFrame = 0.0;
		for( int k = 1; k <= 120; ++k )
		{
			if( !rig.Render( 1 ) )
				return 1;
			worstFrame = std::max( worstFrame, std::fabs( rig.plugin.StoryTime() - s0 - k / 60.0 ) );
		}
		const double advanced = rig.plugin.StoryTime() - s0;
		Check( worstFrame <= 1e-6,
		       fmt( "at 499,217,238 ms, sent in ms: 120 frames at 60 fps advance the candles %.9f s, every frame within %.2g s of "
		            "k / 60 (1e-6)", advanced, worstFrame ) );
		double worst = 0.0;
		for( const auto& tile : rig.plugin.Tiles() )
			if( tile.face >= 0 )
				worst = std::max( worst, std::fabs( tile.lit - CandleLevel( rig.plugin.StoryTime(), tile.key ) ) );
		Check( worst == 0.0, fmt( "each tile's candle is the test card's CandleLevel at that time, exactly (worst %.2g)", worst ) );
	}
	//Speed: a quarter to four times, and still at the bottom.
	for( const double speed : { 0.0, 0.5, 2.0 } )
	{
		Rig rig;
		if( !rig.Init( 160, 90 ) )
			return 1;
		rig.Set( PT_SPEED, ParamFromSpeed( speed ) );
		if( !rig.Render( 121 ) )
			return 1;
		Check( std::fabs( rig.plugin.StoryTime() - 2.0 * speed ) <= 1e-9,
		       fmt( "Speed %.2fx: 2 s of frames advance the candles %.9f s", speed, rig.plugin.StoryTime() ) );
	}
	//A scrub back, and a jump forward: no time passes across either. (From
	//20 s in: a host's clock is never negative.)
	{
		Rig rig;
		if( !rig.Init( 160, 90 ) )
			return 1;
		rig.clockOffset = 20.0;
		if( !rig.Render( 61 ) )
			return 1;
		rig.clockOffset -= 10.0;
		if( !rig.Render( 61 ) )
			return 1;
		rig.clockOffset += 50.0;
		if( !rig.Render( 61 ) )
			return 1;
		Check( std::fabs( rig.plugin.StoryTime() - 3.0 ) <= 1e-9,
		       fmt( "three seconds of frames either side of a 10 s scrub back and a 50 s jump: %.9f s pass (3)", rig.plugin.StoryTime() ) );
	}
	//The Wall clock: seconds since 2026-01-01 on this machine's clock, as
	//the test card's live output keeps time, whatever Resolume and Speed say.
	{
		Rig rig;
		if( !rig.Init( 160, 90 ) )
			return 1;
		rig.Set( PT_CLOCK, static_cast< float >( Clock::Wall ) );
		rig.Set( PT_SPEED, ParamFromSpeed( 3.0 ) );
		const double unix = 1798761600.5;//2027-01-01T00:00:00.5Z
		rig.plugin.SetWallClockForTest( unix );
		if( !rig.Render( 30 ) )
			return 1;
		const double want = perturb.clockEpoch1970 ? unix : unix - 1767225600.0;
		Check( std::fabs( rig.plugin.StoryTime() - want ) <= 1e-6,
		       fmt( "Wall: 2027-01-01T00:00:00.5Z is %.6f s into the test card's endless time (%.1f)", rig.plugin.StoryTime(), want ) );
	}
	return Verdict();
}

//===========================================================================
// --still: what moves and what does not, and what the backgrounds are.
//===========================================================================
size_t candlelit( const Floats& f )
{
	size_t n = 0;
	for( size_t i = 0; i < f.size(); i += 4 )
		n += f[ i ] > 0.9f && f[ i + 1 ] > 0.6f;
	return n;
}

int runStill( const Perturb& perturb )
{
	std::printf( "\n=== still: steady candles and still faces do not move; plain pumpkins have no candle; the backgrounds\n" );
	for( const Raster& raster : kRasters )
	{
		Rig rig;
		if( !rig.Init( raster.w, raster.h ) )
			return 1;
		rig.Set( PT_TILE_WIDTH, 40.0f );
		rig.Set( PT_TILE_HEIGHT, 45.0f );
		rig.Set( PT_FLICKER, perturb.stillFlicker ? 1.0f : 0.0f );
		rig.Set( PT_EXPRESSIONS, 0.0f );
		if( !rig.RenderAt( 1.0 ) )
			return 1;
		const Floats a = rig.Output();
		if( !rig.RenderAt( 7.3 ) )
			return 1;
		const Floats b    = rig.Output();
		const Sameness s1 = compare( a, b );
		Check( s1.differ == 0, fmt( "%dx%d, Flicker and Expressions off: t 1.0 and 7.3 differ in %zu values (0, worst %.2g)", raster.w,
		                            raster.h, s1.differ, s1.worst ) );

		rig.Set( PT_FLICKER, 1.0f );
		rig.Set( PT_EXPRESSIONS, 1.0f );
		if( !rig.RenderAt( 1.0 ) )
			return 1;
		const Floats c = rig.Output();
		if( !rig.RenderAt( 7.3 ) )
			return 1;
		const Floats d    = rig.Output();
		const Sameness s2 = compare( c, d );
		Check( s2.differ > c.size() / 100, fmt( "%dx%d, both on: they differ in %zu values", raster.w, raster.h, s2.differ ) );

		const size_t lit = candlelit( d );
		setFaces( rig, 0 );
		if( !rig.RenderAt( 7.3 ) )
			return 1;
		const size_t plain = candlelit( rig.Output() );
		Check( lit > 0 && plain == 0, fmt( "%dx%d: %zu candlelit pixels carved, %zu with no face ticked (0)", raster.w, raster.h, lit, plain ) );

		//Background None: transparent between, premultiplied throughout.
		//Wide tiles, or the glow (1.1 pumpkins across) reaches every corner.
		setFaces( rig, 0x3F );
		rig.Set( PT_TILE_WIDTH, 160.0f );
		rig.Set( PT_BACKGROUND, static_cast< float >( Background::None ) );
		if( !rig.RenderAt( 7.3 ) )
			return 1;
		const Floats n = rig.Output();
		size_t clear = 0, notPremultiplied = 0, opaque = 0;
		for( size_t i = 0; i < n.size(); i += 4 )
		{
			clear += n[ i + 3 ] == 0.0f;
			opaque += n[ i + 3 ] >= 0.999f;
			for( int ch = 0; ch < 3; ++ch )
				notPremultiplied += n[ i + ch ] > n[ i + 3 ] + 1e-6f;
		}
		Check( clear > 0 && opaque > 0 && notPremultiplied == 0,
		       fmt( "%dx%d, Background None: %zu pixels clear, %zu opaque, %zu values over their alpha (0)", raster.w, raster.h, clear,
		            opaque, notPremultiplied ) );
	}
	return Verdict();
}

//===========================================================================
// --resize: a running instance resized is a fresh one at the new size.
//===========================================================================
int runResize( const Perturb& perturb )
{
	std::printf( "\n=== resize: a running instance resized paints what a fresh one would\n" );
	const Raster steps[] = { { 320, 180 }, { 1280, 720 }, { 640, 360 } };
	Rig running;
	if( !running.Init( steps[ 0 ].w, steps[ 0 ].h ) )
		return 1;
	running.plugin.SetStaleDealForTest( perturb.resizeStale );
	if( !running.RenderAt( 4.0 ) )
		return 1;
	for( size_t i = 1; i < 3; ++i )
	{
		if( !running.Resize( steps[ i ].w, steps[ i ].h ) || !running.RenderAt( 4.0 ) )
			return 1;
		Rig fresh;
		if( !fresh.Init( steps[ i ].w, steps[ i ].h ) || !fresh.RenderAt( 4.0 ) )
			return 1;
		const Floats a   = running.Output(), b = fresh.Output();
		const Sameness s = compare( a, b );
		Check( s.differ == 0, fmt( "%dx%d -> %dx%d: %zu of %zu values differ from a fresh instance (worst %.2g)", steps[ i - 1 ].w,
		                           steps[ i - 1 ].h, steps[ i ].w, steps[ i ].h, s.differ, a.size(), s.worst ) );
	}
	return Verdict();
}

//===========================================================================
// --names (no GL).
//===========================================================================
int runNames( const Perturb& )
{
	std::printf( "\n=== names: every parameter unique (as Arena addresses them, too) and within FFGL's 16 characters\n" );
	PumpkinPlugin plugin;
	std::map< std::string, int > seen, address;
	int longNames = 0, dupes = 0, clashes = 0;
	for( unsigned int i = 0; i < plugin.ParamCount(); ++i )
	{
		const std::string name = plugin.GetParamName( i ) ? plugin.GetParamName( i ) : "";
		if( name.size() > 16 )
		{
			std::printf( "    too long: %s\n", name.c_str() );
			++longNames;
		}
		if( seen[ name ]++ > 0 )
			++dupes;
		//Arena's OSC/REST address: lower case, spaces removed.
		std::string key;
		for( char c : name )
			if( c != ' ' )
				key += static_cast< char >( std::tolower( static_cast< unsigned char >( c ) ) );
		if( address[ key ]++ > 0 )
			++clashes;
	}
	const unsigned int aboutFirst = plugin.ParamCount() - stoatworks::about::kParamCount;
	const bool aboutLast          = std::string( plugin.GetParamName( aboutFirst ) ) == "About";
	Check( longNames == 0 && dupes == 0 && clashes == 0 && aboutLast,
	       fmt( "SW Pumpkin: %u parameters, %d too long, %d duplicated, %d clashing addresses; the About block is last (%s)",
	            plugin.ParamCount(), longNames, dupes, clashes, aboutLast ? "yes" : "NO" ) );
	return Verdict();
}

//===========================================================================
// --bench
//===========================================================================
int runBench()
{
	struct Size
	{
		int w, h;
		const char* name;
	};
	const Size sizes[] = { { 1280, 720, "720p" }, { 1920, 1080, "1080p" }, { 3840, 2160, "4K" } };
	struct Case
	{
		int tile;
		const char* what;
	};
	const Case cases[] = { { 168, "168 px tiles (the default)" }, { 16, "16 px tiles (the smallest)" }, { 4096, "one tile" } };
	std::printf( "\n=== bench: after 60 frames of warm-up, each frame timed with glFinish both sides\n" );
	for( const Case& c : cases )
		for( const Size& size : sizes )
		{
			Rig rig;
			if( !rig.Init( size.w, size.h ) )
				return 1;
			rig.Set( PT_TILE_WIDTH, static_cast< float >( c.tile ) );
			rig.Set( PT_TILE_HEIGHT, static_cast< float >( c.tile ) );
			if( !rig.Render( 60 ) )
				return 1;
			glFinish();
			constexpr int kTimed = 60;
			std::vector< double > times;
			for( int f = 0; f < kTimed; ++f )
			{
				const auto start = std::chrono::steady_clock::now();
				if( !rig.Render( 1 ) )
					return 1;
				glFinish();
				times.push_back( std::chrono::duration< double, std::milli >( std::chrono::steady_clock::now() - start ).count() );
			}
			double mean = 0.0;
			for( double t : times )
				mean += t / kTimed;
			std::sort( times.begin(), times.end() );
			std::printf( "  %-28s %-6s median %5.2f ms/frame, mean %5.2f, worst %5.2f  (median %4.1f%% of a 60 fps frame)\n", c.what,
			             size.name, times[ kTimed / 2 ], mean, times.back(), 100.0 * times[ kTimed / 2 ] / ( 1000.0 / 60.0 ) );
		}
	return 0;
}

struct CheckEntry
{
	const char* flag;
	CheckFn run;
	bool offline;///< needs no GL context
};

const std::vector< CheckEntry >& checks()
{
	static const std::vector< CheckEntry > list = {
		{ "port", runPort, true },      { "linear", runLinear, true }, { "smoothing", runSmoothing, true },
		{ "testcard", runTestcard, false }, { "tiles", runTiles, false }, { "clock", runClock, false },
		{ "still", runStill, false },   { "resize", runResize, false }, { "names", runNames, true },
	};
	return list;
}

bool isOffline( const std::string& flag )
{
	for( const CheckEntry& c : checks() )
		if( flag == c.flag )
			return c.offline;
	return false;
}

//===========================================================================
// --negative
//===========================================================================
int runNegative( bool offlineOnly = false )
{
	struct Case
	{
		const char* name;
		CheckFn check;
		Perturb perturb;
		const char* what;
	};
	std::vector< Case > cases;
	auto add = [ & ]( const char* name, CheckFn fn, const char* what, std::function< void( Perturb& ) > set ) {
		Perturb p;
		set( p );
		cases.push_back( { name, fn, p, what } );
	};
	add( "port", runPort, "key each tile one pixel along", []( Perturb& p ) { p.portKeyNudged = true; } );
	add( "port", runPort, "draw the candle's three frequencies before its three phases", []( Perturb& p ) { p.portCandleOrder = true; } );
	add( "port", runPort, "expect each carved hole the size of its wall", []( Perturb& p ) { p.portHolesUnscaled = true; } );
	add( "linear", runLinear, "expect the expressions to move as e squared", []( Perturb& p ) { p.linearSquared = true; } );
	add( "smoothing", runSmoothing, "draw the ellipses at the n-gon's own radius, unsmoothed", []( Perturb& p ) { p.smoothingOff = true; } );
	add( "testcard", runTestcard, "deal every face one along", []( Perturb& p ) { p.testcardSkew = true; } );
	add( "testcard", runTestcard, "render a quarter of a second early", []( Perturb& p ) { p.testcardLate = true; } );
	add( "tiles", runTiles, "expect a tile's night by its pixel place, not its column and row", []( Perturb& p ) { p.tilesNightByPlace = true; } );
	add( "tiles", runTiles, "expect a whole-tile move to keep the nights too", []( Perturb& p ) { p.tilesOriginColours = true; } );
	add( "clock", runClock, "take elapsed time from the host clock in float", []( Perturb& p ) { p.clockFloat = true; } );
	add( "clock", runClock, "expect the Wall clock in seconds since 1970", []( Perturb& p ) { p.clockEpoch1970 = true; } );
	add( "still", runStill, "render the steady patch with Flicker on", []( Perturb& p ) { p.stillFlicker = true; } );
	add( "resize", runResize, "keep the dealt tiles when only the raster grows", []( Perturb& p ) { p.resizeStale = true; } );

	if( offlineOnly )
		cases.erase( std::remove_if( cases.begin(), cases.end(), []( const Case& c ) { return !isOffline( c.name ); } ), cases.end() );

	int unfalsifiable = 0;
	for( const Case& c : cases )
	{
		std::printf( "\n=== negative control: %s -- %s\n", c.name, c.what );
		const int before = g_failures;
		g_failures       = 0;
		c.check( c.perturb );
		const int observed = g_failures;
		g_failures         = before;
		if( observed > 0 )
			std::printf( "  ok    %s failed %d check%s, as it must\n", c.name, observed, observed == 1 ? "" : "s" );
		else
		{
			std::printf( "  FAIL  %s PASSED against a wrong model -- it cannot fail, so it is not a check\n", c.name );
			++unfalsifiable;
		}
	}
	std::printf( "\nnegative controls: %zu wrong models, %d of them undetected\n", cases.size(), unfalsifiable );
	std::printf( "\n  %s\n", unfalsifiable == 0 ? "PASS" : "FAIL" );
	return unfalsifiable == 0 ? 0 : 1;
}
} // namespace

//---------------------------------------------------------------------------
int main( int argc, char** argv )
{
	std::string outPath = "/tmp/pumpkin.png";
	std::vector< std::string > settings;
	int width = 1280, height = 720, frames = 300;
	double fps = 60.0;
	std::string mode, scriptPath;
	int filmFrames = -1;
	bool sizeGiven = false, framesGiven = false;

	for( int i = 1; i < argc; ++i )
	{
		const std::string argument = argv[ i ];
		const bool hasNext         = i + 1 < argc;
		if( argument == "--help" || argument == "-h" )
		{
			std::printf( "pktest -- render Pumpkin offline and hold it to the test card\n\n"
			             "  --out PATH        render and write a PNG (default /tmp/pumpkin.png)\n"
			             "  --size WxH        render size (default 1280x720)\n"
			             "  --frames N        frames before reading back (default 300: 5 s)\n"
			             "  --fps N           the synthetic clock's rate (default 60)\n"
			             "  --set \"Name=V\"    set a parameter by its display name (an option by name or index). Repeatable.\n"
			             "  --list            every parameter and its default\n"
			             "  --pipe            raw RGBA out, --frames N of them (0/absent: until the reader hangs up)\n"
			             "  --film N          N frames, raw RGBA on stdout\n"
			             "  --script PATH     cues for --pipe/--film: 'frame Name value'\n"
			             "  --fixtures DIR    the test card's fixtures (default %s)\n"
			             "  --expect          a draft of the fleet Arena gate's expectation\n\n"
			             "  checks: --testcard --tiles --clock --still --resize\n"
			             "          no GL: --port --linear --smoothing --names\n"
			             "          --negative   --bench\n"
			             "  --offline         the checks and negative controls that need no GL context (CI)\n",
			             PKTEST_FIXTURES );
			return 0;
		}
		else if( argument == "--out" && hasNext )
			outPath = argv[ ++i ];
		else if( argument == "--set" && hasNext )
			settings.push_back( argv[ ++i ] );
		else if( argument == "--frames" && hasNext )
		{
			frames      = std::atoi( argv[ ++i ] );
			framesGiven = true;
		}
		else if( argument == "--fps" && hasNext )
			fps = std::strtod( argv[ ++i ], nullptr );
		else if( argument == "--fixtures" && hasNext )
			g_fixtures = argv[ ++i ];
		else if( argument == "--pipe" )
			mode = "pipe";
		else if( argument == "--film" && hasNext )
		{
			mode       = "film";
			filmFrames = std::max( 1, std::atoi( argv[ ++i ] ) );
		}
		else if( argument == "--script" && hasNext )
			scriptPath = argv[ ++i ];
		else if( argument == "--list" )
			mode = "list";
		else if( argument == "--size" && hasNext )
		{
			const std::string value = argv[ ++i ];
			const size_t cross      = value.find( 'x' );
			if( cross != std::string::npos )
			{
				width  = std::atoi( value.substr( 0, cross ).c_str() );
				height = std::atoi( value.substr( cross + 1 ).c_str() );
			}
			sizeGiven = true;
		}
		else if( argument.rfind( "--", 0 ) == 0 )
			mode = argument.substr( 2 );
		else
		{
			std::fprintf( stderr, "unknown argument '%s' (try --help)\n", argument.c_str() );
			return 2;
		}
	}
	if( width <= 0 || height <= 0 || !( fps > 0.0 ) )
	{
		std::fprintf( stderr, "--size and --fps must be positive\n" );
		return 2;
	}
	//A check given --size runs at that raster alone (the software pass).
	if( sizeGiven )
		kRasters = { { width, height } };

	if( mode == "expect" )
	{
		//A DRAFT of the fleet gate's expectation, from what the plugin really
		//declares: defaults are the floats the constructor sets.
		PumpkinPlugin plugin;
		std::printf( "{\n  \"plugin\": \"pumpkin\",\n  \"dlls\": [\"Pumpkin.dll\"],\n"
		             "  \"register\": [\n    {\"name\": \"SW Pumpkin\", \"uid\": \"PK01\", \"kind\": \"source\"}\n  ],\n"
		             "  \"params\": {\n    \"SW Pumpkin\": [\n" );
		const std::vector< NamedParameter > list = listParameters( plugin );
		for( size_t i = 0; i < list.size(); ++i )
		{
			const NamedParameter& p = list[ i ];
			std::string line        = "      {\"name\": \"" + p.name + "\", ";
			if( p.type == FF_TYPE_OPTION )
				line += "\"type\": \"ParamChoice\", \"default\": \"" + std::string( plugin.GetParamElementName( p.index, static_cast< unsigned int >( std::lround( p.value ) ) ) ) + "\"";
			else if( p.type == FF_TYPE_BOOLEAN )
				line += std::string( "\"type\": \"ParamBoolean\", \"default\": " ) + ( p.value > 0.5f ? "true" : "false" );
			else if( p.type == FF_TYPE_EVENT )
				line += "\"type\": \"ParamEvent\"";
			else if( p.type == FF_TYPE_TEXT )
				line += "\"type\": \"ParamString\", \"default_pattern\": \"^Pumpkin\\\\ v{version}\\\\ \\\\-\\\\ MIT\\\\ \\\\-\\\\ Stoatworks\\\\ Labs,\\\\ stoatworks\\\\-labs\\\\.com$\"";
			else
			{
				const float lo = p.type == FF_TYPE_INTEGER ? plugin.GetParamRange( p.index ).min : 0.0f;
				const float hi = p.type == FF_TYPE_INTEGER ? plugin.GetParamRange( p.index ).max : 1.0f;
				line += fmt( "\"type\": \"ParamRange\", \"min\": %.1f, \"max\": %.1f, \"default\": %.17g", lo, hi, static_cast< double >( p.value ) );
			}
			if( p.name == "Expressions" )
				line += ", \"note\": \"a blink is brief: a single grab may catch no face mid-blink and read it inconclusive\"";
			if( p.name == "Clock" )
				line += ", \"note\": \"Wall reads this machine's clock: the picture moves either way\"";
			line += i + 1 < list.size() ? "},\n" : "}\n";
			std::printf( "%s", line.c_str() );
		}
		std::printf( "    ]\n  }\n}\n" );
		return 0;
	}
	if( mode == "list" )
	{
		PumpkinPlugin plugin;
		std::printf( "%-3s %-18s %-9s %s\n", "id", "name", "kind", "default" );
		for( const NamedParameter& parameter : listParameters( plugin ) )
			std::printf( "%-3u %-18s %-9s %.4f\n", parameter.index, parameter.name.c_str(), kindName( parameter.type ), parameter.value );
		return 0;
	}

	//A reader that hangs up must end --pipe/--film with exit 1 and a message,
	//not SIGPIPE's silent 141: ignored here, the write fails with EPIPE.
	std::signal( SIGPIPE, SIG_IGN );

	if( mode == "offline" )
	{
		//No context at all: what a runner with no accelerated GL can run. The
		//skip is loud, so a green run is not read as one that checked pixels.
		int failed = 0;
		for( const CheckEntry& check : checks() )
			if( check.offline )
				failed |= check.run( Perturb {} );
		failed |= runNegative( true );
		std::printf( "\n  offline: the checks that need no GL context. The picture checks were NOT run --\n"
		             "  tools/verify.sh runs them against a real driver, at 320x180 and above, and again on the software renderer.\n"
		             "\n  %s\n", failed == 0 ? "PASS" : "FAIL" );
		return failed == 0 ? 0 : 1;
	}
	for( const CheckEntry& check : checks() )
		if( mode == check.flag && check.offline )
			return check.run( Perturb {} );

	CGLContextObj context = createContext();
	if( context == nullptr )
	{
		std::fprintf( stderr, "could not create an OpenGL 4.1 core context\n" );
		return 1;
	}

	int result = 0;
	bool ran   = false;
	for( const CheckEntry& check : checks() )
		if( mode == check.flag )
		{
			result = check.run( Perturb {} );
			ran    = true;
		}

	if( ran )
		;
	else if( mode == "pipe" )
		result = runPipe( width, height, fps, scriptPath, framesGiven ? frames : 0, settings );
	else if( mode == "film" )
		result = runPipe( width, height, fps, scriptPath, filmFrames, settings );
	else if( mode == "negative" )
		result = runNegative();
	else if( mode == "bench" )
		result = runBench();
	else if( !mode.empty() )
	{
		std::fprintf( stderr, "unknown mode --%s (try --help)\n", mode.c_str() );
		result = 2;
	}
	else
	{
		Rig rig;
		rig.fps = fps;
		if( !rig.Init( width, height ) )
			result = 1;
		else
		{
			for( const std::string& setting : settings )
			{
				std::string error;
				if( !applySetting( rig.plugin, setting, error ) )
				{
					std::fprintf( stderr, "--set %s: %s\n", setting.c_str(), error.c_str() );
					return 2;
				}
			}
			if( !rig.Render( std::max( frames, 1 ) ) )
				result = 1;
			else if( writePng( outPath, width, height, rig.Output() ) )
				std::printf( "wrote %s -- %dx%d, %d frames at %g fps, the candles at %.3f s, %dx%d tiles\n", outPath.c_str(), width,
				             height, frames, fps, rig.plugin.StoryTime(), rig.plugin.TileCols(), rig.plugin.TileRows() );
			else
				result = 1;
		}
	}

	CGLSetCurrentContext( nullptr );
	CGLDestroyContext( context );
	return result;
}
