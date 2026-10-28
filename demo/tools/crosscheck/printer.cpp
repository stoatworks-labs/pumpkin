// The C++ half of the browser demo's cross-check of its CPU port: the
// plugin's own code -- the PumpkinPlugin constructor's declarations through
// the FFGL SDK's getters, as a host reads them, and Random.h, Lantern.cpp,
// Geometry.cpp and Controls.cpp -- printed through a fixed scenario.
// demo/tools/crosscheck/run.mjs prints the same from demo/port.js and
// compare.mjs holds the two together. Read the two side by side: each block
// here has its twin there.
//
// A double prints as d:<its 64 bits>, a float as f:<its 32>, so the
// comparison is of bits, never of decimal renderings.
//
//     built and run by crosscheck.sh

#include "Controls.h"
#include "Geometry.h"
#include "Lantern.h"
#include "Pumpkin.h"
#include "Random.h"

#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace pumpkin;

namespace
{
std::string d( double v )
{
	uint64_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char out[ 24 ];
	std::snprintf( out, sizeof out, "d:%016" PRIx64, bits );
	return out;
}

std::string f( float v )
{
	uint32_t bits;
	std::memcpy( &bits, &v, sizeof bits );
	char out[ 16 ];
	std::snprintf( out, sizeof out, "f:%08" PRIx32, bits );
	return out;
}

const char* typeName( unsigned int type )
{
	switch( type )
	{
	case FF_TYPE_INTEGER: return "integer";
	case FF_TYPE_BOOLEAN: return "boolean";
	case FF_TYPE_STANDARD: return "standard";
	case FF_TYPE_OPTION: return "option";
	default: return "other";
	}
}

void floats( const char* label, int index, const float* v, size_t n )
{
	std::printf( "%s %d", label, index );
	for( size_t i = 0; i < n; ++i )
		std::printf( " %s", f( v[ i ] ).c_str() );
	std::printf( "\n" );
}

void shape( const char* list, int face, double e, int index, const Shape& s )
{
	std::printf( "lantern %d %s %s %d %d %d %zu", face, d( e ).c_str(), list, index, s.smooth ? 1 : 0, s.closed ? 1 : 0, s.pts.size() );
	for( const Pt& p : s.pts )
		std::printf( " %s %s", d( p.x ).c_str(), d( p.y ).c_str() );
	if( s.isEllipse )
		std::printf( " ellipse %s %s %s %s %s %d", d( s.ellipse.cx ).c_str(), d( s.ellipse.cy ).c_str(), d( s.ellipse.rx ).c_str(),
		             d( s.ellipse.ry ).c_str(), d( s.ellipse.rotation ).c_str(), s.ellipse.n );
	std::printf( "\n" );
}
} // namespace

int main()
{
	//-------------------------------------------------------------------
	// The declarations, as a host reads them, About block left off.
	//-------------------------------------------------------------------
	{
		PumpkinPlugin plugin;
		for( unsigned int i = 0; i < PT_ABOUT_TEXT; ++i )
		{
			const unsigned int type = plugin.GetParamType( i );
			std::printf( "decl %u %s|%s|%s|%s", i, plugin.GetParamName( i ), plugin.GetParamGroup( i ).c_str(), typeName( type ),
			             f( plugin.GetFloatParameter( i ) ).c_str() );
			if( type == FF_TYPE_INTEGER )
			{
				const RangeStruct range = plugin.GetParamRange( i );
				std::printf( "|%g|%g", static_cast< double >( range.min ), static_cast< double >( range.max ) );
			}
			if( type == FF_TYPE_OPTION )
				for( unsigned int e = 0; e < plugin.GetNumParamElements( i ); ++e )
					std::printf( "|%s", plugin.GetParamElementName( i, e ) );
			std::printf( "\n" );
		}
	}

	//-------------------------------------------------------------------
	// The tables the GPU reads.
	//-------------------------------------------------------------------
	{
		const geometry::Table& t = geometry::Build();
		std::printf( "table meta %d %d", t.bodyFirst, t.bodyCount );
		for( int i = 0; i < kFaceCount; ++i )
			std::printf( " %d %d", t.faceFirst[ i ], t.faceCount[ i ] );
		std::printf( " %zu %d %d\n", t.shapes.size(), t.VertexCount(), t.VertexRows() );
		const std::vector< float > shapes = t.ShapeTexels(), vertices = t.VertexTexels();
		for( size_t i = 0; i < t.shapes.size(); ++i )
			floats( "table-shape", static_cast< int >( i ), &shapes[ i * geometry::kTexelsPerShape * 4 ], geometry::kTexelsPerShape * 4 );
		for( int row = 0; row < t.VertexRows(); ++row )
			floats( "table-vertex", row, &vertices[ static_cast< size_t >( row ) * geometry::kVertexRow * 4 ], geometry::kVertexRow * 4 );
	}

	//-------------------------------------------------------------------
	// The controls' conversions.
	//-------------------------------------------------------------------
	for( int i = 0; i <= 1000; ++i )
	{
		const float v = static_cast< float >( i ) / 1000.0f;
		std::printf( "speed %d %s %s\n", i, f( v ).c_str(), d( SpeedFromParam( v ) ).c_str() );
	}
	for( double s : { 0.0, 0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.999, 4.0, 9.0 } )
		std::printf( "paramFromSpeed %s %s\n", d( s ).c_str(), f( ParamFromSpeed( s ) ).c_str() );
	for( float v : { -3.0f, -0.5f, 0.0f, 0.49f, 0.5f, 1.0f, 1.5f, 2.0f, 2.5f, 7.0f } )
		std::printf( "option %s %d %d\n", f( v ).c_str(), OptionIndex( v, 2 ), OptionIndex( v, 3 ) );
	for( float v : { -5000.0f, -4096.5f, -2.5f, -1.5f, -0.5f, 0.0f, 0.5f, 15.5f, 16.0f, 167.5f, 4095.5f, 4096.0f, 9999.5f, 1e6f } )
		std::printf( "integer %s %d %d %d\n", f( v ).c_str(), IntegerParam( v, kMinTile, kMaxTile ), IntegerParam( v, -kMaxOrigin, kMaxOrigin ),
		             IntegerParam( v, 0, kMaxSeed ) );

	//-------------------------------------------------------------------
	// The patch: keys and faces over a grid either side of the origin, and
	// each tile's night.
	//-------------------------------------------------------------------
	const unsigned masks[] = { 63, 1, 32, 5, 18, 41, 62, 0 };
	for( int seed : { 0, 1, 2, 7, 69, 1234, 9999 } )
		for( int y = -3; y <= 3; ++y )
			for( int x = -4; x <= 4; ++x )
				for( int tw : { 16, 168, 333 } )
				{
					const double px = 37 + static_cast< double >( x ) * tw, py = -21 + static_cast< double >( y ) * ( tw - 7 );
					const uint32_t key = TileKey( seed, px, py );
					std::printf( "key %d %d %d %d %u", seed, x, y, tw, key );
					for( unsigned m : masks )
						std::printf( " %d", TileFace( key, m ) );
					std::printf( "\n" );
				}
	for( int row = -13; row <= 13; ++row )
		for( int col = -13; col <= 13; ++col )
			std::printf( "night %d %d %d\n", col, row, NightIndex( col, row ) );

	//-------------------------------------------------------------------
	// The timing: each tile's draws, and its candle and expression from a
	// frame in to ten years and more.
	//-------------------------------------------------------------------
	const double times[] = { 0.0, 0.04, 1.0 / 60.0, 1.234, 2.2, 6.2, 12.5, 21.6, 24.65, 3600.5, 86400.25, 26000000.25, 315000000.125, 420000000.0 };
	for( int seed : { 1, 7 } )
		for( int x = -2; x <= 6; ++x )
			for( int y = -1; y <= 4; ++y )
			{
				const uint32_t key = TileKey( seed, x * 168.0, y * 168.0 );
				const TileClock c   = TileClock::For( key );
				std::printf( "clock %u", key );
				for( int i = 0; i < 3; ++i )
					std::printf( " %s %s", d( c.candleCycles[ i ] ).c_str(), d( c.candleOffset[ i ] ).c_str() );
				std::printf( " %s %s\n", d( c.goes ).c_str(), d( c.expressionOffset ).c_str() );
				for( double t : times )
					std::printf( "level %u %s %s %s\n", key, d( t ).c_str(), d( c.Candle( t ) ).c_str(), d( c.Expression( t ) ).c_str() );
			}

	//-------------------------------------------------------------------
	// The drawing: every shape of every face at five expressions.
	//-------------------------------------------------------------------
	for( int face = -1; face < kFaceCount; ++face )
		for( double e : { 0.0, 0.125, 0.5, 0.875, 1.0 } )
		{
			const Lantern l = MakeLantern( face, e );
			for( size_t i = 0; i < l.body.size(); ++i )
				shape( "body", face, e, static_cast< int >( i ), l.body[ i ] );
			for( size_t i = 0; i < l.walls.size(); ++i )
				shape( "wall", face, e, static_cast< int >( i ), l.walls[ i ] );
			for( size_t i = 0; i < l.holes.size(); ++i )
				shape( "hole", face, e, static_cast< int >( i ), l.holes[ i ] );
		}
	for( int n : { 12, 16, 20, 22, 24, 40 } )
		std::printf( "smoothed %d %s\n", n, d( geometry::SmoothedRadius( n ) ).c_str() );
	return 0;
}
