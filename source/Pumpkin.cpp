#include "Pumpkin.h"

#include "Diag.h"
#include "GLState.h"
#include "Geometry.h"
#include "Shaders.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

using namespace ffglex;

namespace pumpkin
{
namespace
{
std::string glStringOrUnknown( GLenum name )
{
	const GLubyte* value = glGetString( name );
	return value ? reinterpret_cast< const char* >( value ) : "unknown";
}

double steadySeconds()
{
	using namespace std::chrono;
	static const steady_clock::time_point start = steady_clock::now();
	return duration_cast< duration< double > >( steady_clock::now() - start ).count();
}

constexpr int kClockVotes = 4;
/// Host seconds. A bigger forward step, or any backward one, is a jump -- a
/// clip trigger or a scrub -- and no time passes across it.
constexpr double kMaxFrameDelta = 1.0;

int floorDiv( int a, int b )
{
	return a >= 0 ? a / b : -( ( -a - 1 ) / b ) - 1;
}

void setIvec2( FFGLShader& shader, const char* name, int x, int y )
{
	glUniform2i( glGetUniformLocation( shader.GetGLID(), name ), x, y );
}

void setVec3s( FFGLShader& shader, const char* name, const uint32_t* hex, int count, bool codeValues )
{
	float v[ 3 * 8 ] = {};
	for( int i = 0; i < count; ++i )
		for( int c = 0; c < 3; ++c )
		{
			const float code   = static_cast< float >( ( hex[ i ] >> ( 16 - 8 * c ) ) & 0xFFu );
			v[ i * 3 + c ] = codeValues ? code : code / 255.0f;
		}
	glUniform3fv( glGetUniformLocation( shader.GetGLID(), name ), count, v );
}

GLuint makeFloatTexture( int width, int height, const float* texels )
{
	GLuint id = 0;
	glGenTextures( 1, &id );
	glBindTexture( GL_TEXTURE_2D, id );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA32F, width, height, 0, GL_RGBA, GL_FLOAT, texels );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE );
	glBindTexture( GL_TEXTURE_2D, 0 );
	return id;
}

//The test card's colours (halloween.ts, drawLantern).
const uint32_t kWall[ 2 ]  = { 0x7a3a10, 0xffd27a };
const uint32_t kFlame[ 6 ] = { 0xc86a10, 0xa84a08, 0x6a2000, 0xfff4b8, 0xffc23a, 0xff7212 };
const uint32_t kHalo[ 2 ]  = { 0xff9632, 0xff781e };//rgba( 255, 150, 50 ), rgba( 255, 120, 30 )
constexpr float kHaloAlpha = 0.42f;
} // namespace

static_assert( PT_COUNT - PT_ABOUT_TEXT == stoatworks::about::kParamCount,
               "the About run no longer matches StoatworksAbout.h" );

//---------------------------------------------------------------------------
PumpkinPlugin::PumpkinPlugin()
{
	SetMinInputs( 0 );
	SetMaxInputs( 0 );
	SetTimeSupported( true );

	//-------------------------------------------------------------------
	// Defaults: the test card's. 168-pixel tiles (its default LED tile),
	// Seed 1, every face, candles flickering, faces blinking, at its speed.
	//-------------------------------------------------------------------
	params[ PT_TILE_WIDTH ]  = static_cast< float >( kDefaultTile );
	params[ PT_TILE_HEIGHT ] = static_cast< float >( kDefaultTile );
	params[ PT_ORIGIN_X ]    = 0.0f;
	params[ PT_ORIGIN_Y ]    = 0.0f;
	params[ PT_SEED ]        = 1.0f;
	for( unsigned int id = PT_FACE_FIRST; id <= PT_FACE_LAST; ++id )
		params[ id ] = 1.0f;
	params[ PT_FLICKER ]     = 1.0f;
	params[ PT_EXPRESSIONS ] = 1.0f;
	params[ PT_SPEED ]       = ParamFromSpeed( 1.0 );
	params[ PT_CLOCK ]       = static_cast< float >( Clock::Resolume );
	params[ PT_BACKGROUND ]  = static_cast< float >( Background::Night );

	const std::vector< unsigned int >& order = HostOrder();
	for( unsigned int host = 0; host < order.size(); ++host )
	{
		const unsigned int id = order[ host ];
		const char* name      = NameOf( id );
		switch( id )
		{
		case PT_TILE_WIDTH:
		case PT_TILE_HEIGHT:
			//Only FF_TYPE_STANDARD has its default clamped into 0..1, so an
			//integer is declared with its real default and range.
			SetParamInfo( host, name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, static_cast< float >( kMinTile ), static_cast< float >( kMaxTile ) );
			break;
		case PT_ORIGIN_X:
		case PT_ORIGIN_Y:
			SetParamInfo( host, name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, static_cast< float >( -kMaxOrigin ), static_cast< float >( kMaxOrigin ) );
			break;
		case PT_SEED:
			SetParamInfo( host, name, FF_TYPE_INTEGER, params[ id ] );
			SetParamRange( host, 0.0f, static_cast< float >( kMaxSeed ) );
			break;
		case PT_FLICKER:
		case PT_EXPRESSIONS:
			SetParamInfo( host, name, FF_TYPE_BOOLEAN, params[ id ] > 0.5f );
			break;
		case PT_CLOCK:
		{
			const int count = static_cast< int >( Clock::Count );
			SetOptionParamInfo( host, name, static_cast< unsigned int >( count ), params[ id ] );
			for( int i = 0; i < count; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ), ClockName( static_cast< Clock >( i ) ),
				                     static_cast< float >( i ) );
			break;
		}
		case PT_BACKGROUND:
		{
			const int count = static_cast< int >( Background::Count );
			SetOptionParamInfo( host, name, static_cast< unsigned int >( count ), params[ id ] );
			for( int i = 0; i < count; ++i )
				SetParamElementInfo( host, static_cast< unsigned int >( i ),
				                     BackgroundName( static_cast< Background >( i ) ), static_cast< float >( i ) );
			break;
		}
		case PT_ABOUT_TEXT:
			SetParamInfo( host, "About", FF_TYPE_TEXT, stoatworks::about::defaultText() );
			break;
		default:
			if( id > PT_ABOUT_TEXT )
				SetParamInfo( host, stoatworks::about::buttons()[ id - PT_ABOUT_TEXT - 1 ].label, FF_TYPE_EVENT, false );
			else if( id >= PT_FACE_FIRST && id <= PT_FACE_LAST )
				SetParamInfo( host, name, FF_TYPE_BOOLEAN, params[ id ] > 0.5f );
			else
				SetParamInfo( host, name, FF_TYPE_STANDARD, params[ id ] );
			break;
		}
		SetParamGroup( host, GroupOf( id ) );
	}
}

PumpkinPlugin::~PumpkinPlugin() = default;

//---------------------------------------------------------------------------
FFResult PumpkinPlugin::InitGL( const FFGLViewportStruct* vp )
{
	diag::init();
	diag::info( std::string( "GL vendor=" ) + glStringOrUnknown( GL_VENDOR ) + " renderer="
	            + glStringOrUnknown( GL_RENDERER ) + " version=" + glStringOrUnknown( GL_VERSION ) );

	const std::string vertex   = std::string( shaders::kVersion ) + shaders::kQuadVertex;
	const std::string fragment = shaders::Assemble( shaders::kPatchFragment );
	if( !patchShader.Compile( vertex.c_str(), fragment.c_str() ) )
	{
		diag::error( "the patch shader failed to compile - the source will show nothing" );
		FFGLLog::LogToHost( "Pumpkin: shader failed to compile" );
		DeInitGL();
		return FF_FAIL;
	}
	if( !quad.Initialise() )
	{
		diag::error( "quad geometry failed to initialise" );
		DeInitGL();
		return FF_FAIL;
	}

	//The lantern: built once, the same for every instance.
	const geometry::Table& table = geometry::Build();
	const std::vector< float > shapes = table.ShapeTexels(), vertices = table.VertexTexels();
	shapeTexture  = makeFloatTexture( geometry::kTexelsPerShape, static_cast< int >( table.shapes.size() ), shapes.data() );
	vertexTexture = makeFloatTexture( geometry::kVertexRow, table.VertexRows(), vertices.data() );
	tileTexCols = tileTexRows = 0;

	diag::info( "initialised: " + std::to_string( table.shapes.size() ) + " shapes, " + std::to_string( table.VertexCount() )
	            + " vertices" );
	return CFFGLPlugin::InitGL( vp );
}

//---------------------------------------------------------------------------
double PumpkinPlugin::WallSeconds() const
{
	if( wallForTest >= 0.0 )
		return wallForTest;
	using namespace std::chrono;
	return duration_cast< duration< double > >( system_clock::now().time_since_epoch() ).count();
}

void PumpkinPlugin::UpdateClock()
{
	const double wallNow = steadySeconds();
	if( wallStart < 0.0 )
		wallStart = wallNow;
	const double raw = hostTime;

	//Resolume has been seen sending seconds and milliseconds through SetTime:
	//vote on the unit against the wall clock (rosette's code, via radar).
	if( clockScale == 0.0 && raw >= 0.0 && lastRawTime >= 0.0 && lastWallTime >= 0.0 )
	{
		const double hostDelta = raw - lastRawTime;
		const double wallDelta = wallNow - lastWallTime;
		if( hostDelta > 0.0 && wallDelta >= 0.0005 )
		{
			const double ratio = hostDelta / wallDelta;
			if( ratio > 0.1 && ratio < 10.0 )
				++secondsVotes;
			else if( ratio > 100.0 && ratio < 10000.0 )
				++millisVotes;
			if( secondsVotes >= kClockVotes || millisVotes >= kClockVotes )
			{
				clockScale  = millisVotes > secondsVotes ? 0.001 : 1.0;
				settledJump = true;
			}
		}
	}
	if( raw >= 0.0 )
		lastRawTime = raw;
	lastWallTime = wallNow;
	now          = ( raw >= 0.0 && clockScale != 0.0 ) ? raw * clockScale : wallNow - wallStart;
}

unsigned PumpkinPlugin::FaceMask() const
{
	unsigned mask = 0;
	for( int f = 0; f < kFaceCount; ++f )
		if( params[ PT_FACE_FIRST + static_cast< unsigned int >( f ) ] > 0.5f )
			mask |= 1u << f;
	return mask;
}

//---------------------------------------------------------------------------
// The tiles, each with its key, face, night and candle: drawn once a
// grid, a seed and a set of faces. Tiles count from the origin, as the test
// card's `ledTiles` does, so a tile left of or above it is negative.
//---------------------------------------------------------------------------
void PumpkinPlugin::Deal( int width, int height )
{
	const int tw   = IntegerParam( params[ PT_TILE_WIDTH ], kMinTile, kMaxTile );
	const int th   = IntegerParam( params[ PT_TILE_HEIGHT ], kMinTile, kMaxTile );
	const int ox   = IntegerParam( params[ PT_ORIGIN_X ], -kMaxOrigin, kMaxOrigin );
	const int oy   = IntegerParam( params[ PT_ORIGIN_Y ], -kMaxOrigin, kMaxOrigin );
	const int seed = IntegerParam( params[ PT_SEED ], 0, kMaxSeed );
	const unsigned mask = FaceMask();
	const bool sameRaster = ( width == dealtW && height == dealtH ) || ( staleDealForTest && dealtW > 0 );
	if( sameRaster && tw == dealtTW && th == dealtTH && ox == dealtOX && oy == dealtOY && seed == dealtSeed
	    && mask == dealtMask && faceSkewForTest == dealtSkew )
		return;
	dealtW = width, dealtH = height, dealtTW = tw, dealtTH = th, dealtOX = ox, dealtOY = oy, dealtSeed = seed;
	dealtMask = mask, dealtSkew = faceSkewForTest;
	++rebuilds;

	//The visible tiles and a border of one all round: on Dark and None a
	//neighbour's glow reaches in from off the raster.
	firstCol = floorDiv( -ox, tw ) - 1;
	firstRow = floorDiv( -oy, th ) - 1;
	gridCols = floorDiv( width - 1 - ox, tw ) + 1 - firstCol + 1;
	gridRows = floorDiv( height - 1 - oy, th ) + 1 - firstRow + 1;
	tiles.assign( static_cast< size_t >( gridCols ) * gridRows, TileState {} );
	clocks.assign( tiles.size(), TileClock {} );
	for( int r = 0; r < gridRows; ++r )
		for( int c = 0; c < gridCols; ++c )
		{
			TileState& t = tiles[ static_cast< size_t >( r ) * gridCols + c ];
			t.col        = firstCol + c;
			t.row        = firstRow + r;
			t.key        = TileKey( seed, ox + static_cast< double >( t.col ) * tw, oy + static_cast< double >( t.row ) * th );
			t.face       = TileFace( t.key, mask );
			if( t.face >= 0 && faceSkewForTest != 0 )
				t.face = ( t.face + faceSkewForTest ) % kFaceCount;
			t.night = NightIndex( t.col, t.row );
			clocks[ static_cast< size_t >( r ) * gridCols + c ] = TileClock::For( t.key );
		}
}

//---------------------------------------------------------------------------
FFResult PumpkinPlugin::ProcessOpenGL( ProcessOpenGLStruct* pgl )
{
	if( pgl == nullptr )
		return FF_FAIL;

	ScopedGLState restore;
	const GLint* hostViewport = restore.saved.viewport;
	const int width           = hostViewport[ 2 ];
	const int height          = hostViewport[ 3 ];
	if( width <= 0 || height <= 0 || patchShader.GetGLID() == 0 )
		return FF_FAIL;
	glDisable( GL_BLEND );

	//-------------------------------------------------------------------
	// Time: the host's, as real elapsed seconds, frame to frame, in double,
	// times Speed. Resolume's clock has been measured at ~499 million ms,
	// where a float resolves 32 ms; nothing here ever holds it in one.
	//-------------------------------------------------------------------
	UpdateClock();
	if( settledJump )
	{
		lastNow     = -1.0;
		settledJump = false;
	}
	if( lastNow >= 0.0 )
	{
		const double step = floatClockForTest
		                        ? static_cast< double >( static_cast< float >( now ) - static_cast< float >( lastNow ) )
		                        : now - lastNow;
		if( step >= 0.0 && step <= kMaxFrameDelta )
			story += step * SpeedFromParam( params[ PT_SPEED ] );
	}
	lastNow = now;
	const Clock clockMode = static_cast< Clock >( OptionIndex( params[ PT_CLOCK ], static_cast< int >( Clock::Count ) ) );
	const double t        = clockMode == Clock::Wall ? WallSeconds() - kEndlessEpochSeconds : story;
	lastT                 = t;

	//-------------------------------------------------------------------
	// The tiles, then this frame's candles and faces, into the tile texture.
	//-------------------------------------------------------------------
	Deal( width, height );
	const bool flicker     = params[ PT_FLICKER ] > 0.5f;
	const bool expressions = params[ PT_EXPRESSIONS ] > 0.5f;
	tileTexels.resize( tiles.size() * 4 );
	for( size_t i = 0; i < tiles.size(); ++i )
	{
		TileState& tile = tiles[ i ];
		//halloween.ts, drawPumpkins: no face, no candle; a steady one burns at 0.8.
		tile.lit            = tile.face < 0 ? 0.0 : flicker ? clocks[ i ].Candle( t ) : 0.8;
		tile.expression     = tile.face >= 0 && expressions ? clocks[ i ].Expression( t ) : 0.0;
		tileTexels[ i * 4 ]     = static_cast< float >( tile.face );
		tileTexels[ i * 4 + 1 ] = static_cast< float >( tile.lit );
		tileTexels[ i * 4 + 2 ] = static_cast< float >( tile.expression );
		tileTexels[ i * 4 + 3 ] = static_cast< float >( tile.night );
	}
	if( tileTexture == 0 || tileTexCols != gridCols || tileTexRows != gridRows )
	{
		if( tileTexture )
			glDeleteTextures( 1, &tileTexture );
		tileTexture = makeFloatTexture( gridCols, gridRows, tileTexels.data() );
		tileTexCols = gridCols;
		tileTexRows = gridRows;
	}
	else
	{
		glBindTexture( GL_TEXTURE_2D, tileTexture );
		glPixelStorei( GL_UNPACK_ALIGNMENT, 4 );
		glTexSubImage2D( GL_TEXTURE_2D, 0, 0, 0, gridCols, gridRows, GL_RGBA, GL_FLOAT, tileTexels.data() );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}
	if( tileTexture == 0 || shapeTexture == 0 || vertexTexture == 0 )
	{
		diag::error( "could not allocate the textures" );
		return FF_FAIL;
	}

	//-------------------------------------------------------------------
	// The patch.
	//-------------------------------------------------------------------
	const geometry::Table& table = geometry::Build();
	const int tw      = dealtTW, th = dealtTH;
	const double size = std::min( tw * 0.8, ( th * 0.84 ) / ( kPumpkinBottom - kPumpkinTop ) );
	glBindFramebuffer( GL_FRAMEBUFFER, pgl->HostFBO );
	glViewport( hostViewport[ 0 ], hostViewport[ 1 ], hostViewport[ 2 ], hostViewport[ 3 ] );
	{
		ScopedShaderBinding shader( patchShader.GetGLID() );
		bindUnit( 0, tileTexture );
		bindUnit( 1, shapeTexture );
		bindUnit( 2, vertexTexture );
		patchShader.Set( "Tiles", 0 );
		patchShader.Set( "Shapes", 1 );
		patchShader.Set( "Vertices", 2 );
		setIvec2( patchShader, "Raster", width, height );
		setIvec2( patchShader, "ViewOrigin", hostViewport[ 0 ], hostViewport[ 1 ] );
		setIvec2( patchShader, "TileSize", tw, th );
		setIvec2( patchShader, "Origin", dealtOX, dealtOY );
		setIvec2( patchShader, "FirstTile", firstCol, firstRow );
		patchShader.Set( "Size", static_cast< float >( size ) );
		patchShader.Set( "Centre", static_cast< float >( tw / 2.0 ),
		                 static_cast< float >( th / 2.0 - size * ( ( kPumpkinTop + kPumpkinBottom ) / 2 ) ) );
		setIvec2( patchShader, "BodyShapes", table.bodyFirst, table.bodyCount );
		int faces[ kFaceCount * 2 ];
		for( int f = 0; f < kFaceCount; ++f )
		{
			faces[ f * 2 ]     = table.faceFirst[ f ];
			faces[ f * 2 + 1 ] = table.faceCount[ f ];
		}
		glUniform2iv( glGetUniformLocation( patchShader.GetGLID(), "FaceShapes" ), kFaceCount, faces );
		patchShader.Set( "Background", OptionIndex( params[ PT_BACKGROUND ], static_cast< int >( Background::Count ) ) );
		setVec3s( patchShader, "Nights", kTileNights, 6, false );
		setVec3s( patchShader, "PageNight", &kPageNight, 1, false );
		setVec3s( patchShader, "WallColours", kWall, 2, true );
		setVec3s( patchShader, "FlameColours", kFlame, 6, true );
		setVec3s( patchShader, "HaloColours", kHalo, 2, true );
		patchShader.Set( "HaloAlpha", kHaloAlpha );
		patchShader.Set( "GlowClippedForTest", glowClippedForTest ? 1 : 0 );
		quad.Draw();
		unbindTextureUnits( 3 );
	}
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult PumpkinPlugin::DeInitGL()
{
	patchShader.FreeGLResources();
	quad.Release();
	for( GLuint* id : { &tileTexture, &shapeTexture, &vertexTexture } )
		if( *id )
		{
			glDeleteTextures( 1, id );
			*id = 0;
		}
	tileTexCols = tileTexRows = 0;
	return FF_SUCCESS;
}

//---------------------------------------------------------------------------
FFResult PumpkinPlugin::SetTime( double time )
{
	hostTime = time;
	return FF_SUCCESS;
}

char* PumpkinPlugin::GetTextParameter( unsigned int index )
{
	if( index == PT_ABOUT_TEXT )
	{
		static const std::string text = stoatworks::about::textParam( 0 );
		return const_cast< char* >( text.c_str() );
	}
	return CFFGLPlugin::GetTextParameter( index );
}

FFResult PumpkinPlugin::SetTextParameter( unsigned int index, const char* value )
{
	if( index == PT_ABOUT_TEXT )
		return FF_SUCCESS;
	return CFFGLPlugin::SetTextParameter( index, value );
}

FFResult PumpkinPlugin::SetFloatParameter( unsigned int index, float value )
{
	if( index >= PT_COUNT )
		return FF_FAIL;
	if( index >= PT_ABOUT_TEXT )
		return stoatworks::about::handleParam( index - PT_ABOUT_TEXT, value ) ? FF_SUCCESS : FF_FAIL;
	params[ index ] = value;
	return FF_SUCCESS;
}

float PumpkinPlugin::GetFloatParameter( unsigned int index )
{
	return index < PT_COUNT ? params[ index ] : 0.0f;
}

void PumpkinPlugin::SetById( unsigned int id, float value )
{
	const int host = HostIndexOf( id );
	if( host >= 0 )
		SetFloatParameter( static_cast< unsigned int >( host ), value );
}

} // namespace pumpkin
