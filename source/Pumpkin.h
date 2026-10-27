#pragma once

#include "Controls.h"
#include "Lantern.h"

#include <FFGLSDK.h>

// After FFGLSDK.h, which is where FFUInt32 comes from.
#include "StoatworksAboutParams.h"

#include <cstdint>
#include <vector>

namespace pumpkin
{
/**
    The plugin: a jack-o'-lantern on every LED tile, the test card's pumpkin
    patch, as an FFGL source.

    Per frame the CPU works out, in double, each visible tile's candle and
    expression (Lantern.cpp, the test card's timing), packs them into a small
    RGBA32F texture, and the shader paints every tile's lantern from it and
    from the shape tables (Geometry.cpp), which go up once at InitGL.

    The patch is keyed on the tiles' places, so a tile keeps its face when the
    raster changes size; the per-tile draws (key, face, clock) are cached until
    the grid, the seed or the faces ticked change.
*/
class PumpkinPlugin : public CFFGLPlugin
{
public:
	PumpkinPlugin();
	~PumpkinPlugin() override;

	FFResult InitGL( const FFGLViewportStruct* viewport ) override;
	FFResult ProcessOpenGL( ProcessOpenGLStruct* input ) override;
	FFResult DeInitGL() override;

	FFResult SetFloatParameter( unsigned int index, float value ) override;
	float GetFloatParameter( unsigned int index ) override;
	char* GetTextParameter( unsigned int index ) override;
	/// The base class's stub fails, and a failed default deletes the instance
	/// -- so without this no real host can load the plugin.
	FFResult SetTextParameter( unsigned int index, const char* value ) override;
	FFResult SetTime( double time ) override;

	unsigned int ParamCount() const
	{
		return static_cast< unsigned int >( HostOrder().size() );
	}
	int HostIndexOf( unsigned int id ) const
	{
		return id < PT_COUNT ? static_cast< int >( id ) : -1;
	}
	void SetById( unsigned int id, float value );
	float GetById( unsigned int id ) const
	{
		return id < PT_COUNT ? params[ id ] : 0.0f;
	}

	/// One tile's state as the shader is given it.
	struct TileState
	{
		int col = 0, row = 0;
		uint32_t key = 0;
		int face = -1;
		double lit = 0.0, expression = 0.0;
		int night = 0;
	};

	//-------------------------------------------------------------------
	// For the harness. Nothing in the plugin's own operation calls these.
	//-------------------------------------------------------------------
	void SetClockScaleForTest( double scale )
	{
		clockScale = scale;
	}
	double ClockScale() const
	{
		return clockScale;
	}
	/// The Wall clock reads this many seconds since 1970 instead of the system's.
	void SetWallClockForTest( double unixSeconds )
	{
		wallForTest = unixSeconds;
	}
	/// --clock's negative control: elapsed time from the host clock in float.
	void SetFloatClockForTest( bool on )
	{
		floatClockForTest = on;
	}
	/// --testcard's negative control: every face dealt `skew` along the list.
	void SetFaceSkewForTest( int skew )
	{
		faceSkewForTest = skew;
	}
	/// The Resolume clock's story time, set outright (the next frame is at it).
	void SetStoryForTest( double seconds )
	{
		story   = seconds;
		lastNow = -1.0;
	}
	/// --resize's negative control: the dealt tiles kept when only the raster changes.
	void SetStaleDealForTest( bool on )
	{
		staleDealForTest = on;
	}
	/// The time the candles are at: what Lantern.cpp's timing is given.
	double StoryTime() const
	{
		return lastT;
	}
	const std::vector< TileState >& Tiles() const
	{
		return tiles;
	}
	int TileCols() const
	{
		return gridCols;
	}
	int TileRows() const
	{
		return gridRows;
	}
	unsigned long long PatchRebuilds() const
	{
		return rebuilds;
	}

private:
	void UpdateClock();
	double WallSeconds() const;
	unsigned FaceMask() const;
	void Deal( int width, int height );

	float params[ PT_COUNT ] = {};

	ffglex::FFGLShader patchShader;
	ffglex::FFGLScreenQuad quad;
	GLuint tileTexture = 0, shapeTexture = 0, vertexTexture = 0;
	int tileTexCols = 0, tileTexRows = 0;
	std::vector< float > tileTexels;

	//The patch: the visible tiles, dealt once per grid/seed/faces.
	std::vector< TileState > tiles;
	std::vector< TileClock > clocks;
	int gridCols = 0, gridRows = 0, firstCol = 0, firstRow = 0;
	int dealtW = -1, dealtH = -1, dealtTW = -1, dealtTH = -1, dealtOX = 0, dealtOY = 0, dealtSeed = -1;
	unsigned dealtMask = ~0u;
	int dealtSkew      = 0;
	unsigned long long rebuilds = 0;

	//Time. Resolume has sent both seconds and milliseconds (see millpond).
	double hostTime = -1.0, lastRawTime = -1.0, lastWallTime = -1.0, wallStart = -1.0;
	double clockScale = 0.0;
	int secondsVotes = 0, millisVotes = 0;
	double now = 0.0, lastNow = -1.0;
	bool settledJump = false;
	double story     = 0.0;
	double lastT     = 0.0;
	double wallForTest = -1.0;

	bool floatClockForTest = false;
	bool staleDealForTest  = false;
	int faceSkewForTest    = 0;
};

} // namespace pumpkin
