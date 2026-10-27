#pragma once

#include <cstdint>
#include <vector>

/**
    The test card's jack-o'-lantern, ported: the drawing (`art/halloween.ts`,
    `pumpkin()`), the timing (`halloween.ts`, `candleLevel` and
    `expressionLevel`) and which face goes on which tile (`tileKey`,
    `tileFace`). No GL, so `pktest --offline` checks all of it in CI.

    Units are the test card's: a pumpkin one unit wide, centred on (0, 0), y
    DOWN. The body runs from y = -0.37 to 0.37 and the stem rises to -0.52.

    The port keeps the test card's point lists exactly (`pktest --port`
    compares them point for point with what its TypeScript printed). Where a
    list was made by `ellipse()` it also keeps the ellipse, because the shader
    draws those as ellipses rather than as the 40-gon the canvas smoothed
    (Geometry.cpp says why that is the same picture).
*/
namespace pumpkin
{
struct Pt
{
	double x = 0.0, y = 0.0;
};

/// What `ellipse( cx, cy, rx, ry, rotation, n )` was asked for.
struct EllipseSpec
{
	double cx = 0.0, cy = 0.0, rx = 0.0, ry = 0.0, rotation = 0.0;
	int n = 0;
};

/// One of the test card's `Shape`s (the body) or `Hole`s (the carving).
/// Colours are 0xRRGGBB.
struct Shape
{
	std::vector< Pt > pts;
	/// `fillShapes` smooths unless told not to; a Hole is `sharp` or `round`.
	bool smooth = true;
	bool closed = true;
	bool hasFill = false;
	uint32_t fill = 0;
	double alpha = 1.0;
	bool hasStroke = false;
	uint32_t stroke = 0;
	double strokeWidth = 0.0;
	bool isEllipse = false;
	EllipseSpec ellipse;
};

/// The faces, in the test card's order (`PUMPKIN_FACES`), which is the order
/// `tileFace` deals them in.
enum class Face : int
{
	Classic = 0,
	Fierce,
	Happy,
	Surprised,
	Sly,
	Goofy,
	Count
};
constexpr int kFaceCount = static_cast< int >( Face::Count );
const char* FaceName( int face );

/// `pumpkin( face, expression )`. `face` -1 is a plain, uncarved pumpkin.
struct Lantern
{
	std::vector< Shape > body, walls, holes;
};
Lantern MakeLantern( int face, double expression );

/// `PUMPKIN_TOP` and `PUMPKIN_BOTTOM`: the extent the painter sizes by.
constexpr double kPumpkinTop    = -0.52;
constexpr double kPumpkinBottom = 0.37;

//---------------------------------------------------------------------------
// The patch.
//---------------------------------------------------------------------------

/// `tileKey( seed, tile, env )` for a tile whose top-left is (x, y) on the
/// surface, with the view at the origin and `localKey` 0: what the test card
/// keys a SPANNED surface on, where every output must agree.
uint32_t TileKey( int seed, double x, double y );

/// `tileFace`: the face carved on a tile, from the faces ticked (bit i is
/// face i), or -1 when none are.
int TileFace( uint32_t key, unsigned faceMask );

/// The loop the test card measures its speeds in. Endless, nothing is fitted
/// to it, and it cancels out of every phase but for rounding -- which is why
/// the port keeps it rather than simplifying it away.
constexpr double kLoopSeconds = 10.0;

/// A candle's brightness 0..1 at t (`candleLevel`, endless).
double CandleLevel( double t, uint32_t key );
/// How far into a blink, wink or chomp a face is, 0..1 (`expressionLevel`, endless).
double ExpressionLevel( double t, uint32_t key );

/// The same two, with everything that comes from the key drawn once, so a
/// frame only does the phases. `Level` is bit for bit `CandleLevel` and
/// `ExpressionLevel` (`pktest --port` checks it).
struct TileClock
{
	double candleCycles[ 3 ] = {}, candleOffset[ 3 ] = {};
	double goes = 0.0, expressionOffset = 0.0;

	static TileClock For( uint32_t key );
	double Candle( double t ) const;
	double Expression( double t ) const;
};

/// `TILE_NIGHTS`: each tile's own night sky, by `mod( col + 2 row, 6 )`.
extern const uint32_t kTileNights[ 6 ];
/// The page behind the tiles.
constexpr uint32_t kPageNight = 0x0b0710;
int NightIndex( int col, int row );

/// `mix( a, b, t )` from art/scenery.ts: per channel, rounded to a whole
/// code value (`Math.round`).
uint32_t MixHex( uint32_t a, uint32_t b, double t );

} // namespace pumpkin
