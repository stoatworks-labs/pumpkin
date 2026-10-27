#pragma once

#include "StoatworksAboutLinks.h"

#include <vector>

/**
    The host's parameters, and what they mean in the patch's units.

    Every ranged parameter the host sees is 0..1, because `SetParamInfo`
    clamps an `FF_TYPE_STANDARD` default into 0..1 before `SetParamRange` could
    widen it. The conversions live in Controls.cpp. Tile Width, Tile Height,
    Origin X, Origin Y and Seed are real integers (`FF_TYPE_INTEGER`), in the
    test card's own units (pixels, and its seed); options and booleans hold
    the element value itself.
*/
namespace pumpkin
{
/// Every control, by what it is -- NOT by the index a host sees. Host index i
/// is `HostOrder()[ i ]`, with the About block LAST (radar's and conway's
/// shape: a future User guide button moves no control).
enum ParamId : unsigned int
{
	// -- Patch: the LED tiles and which face goes where ----------------------
	PT_TILE_WIDTH = 0,
	PT_TILE_HEIGHT,
	PT_ORIGIN_X,
	PT_ORIGIN_Y,
	PT_SEED,

	// -- Faces: the test card's six, each a tick -----------------------------
	PT_FACE_FIRST,
	PT_FACE_LAST = PT_FACE_FIRST + 5,

	// -- Candles -------------------------------------------------------------
	PT_FLICKER,
	PT_EXPRESSIONS,
	PT_SPEED,
	PT_CLOCK,

	// -- Look ----------------------------------------------------------------
	PT_BACKGROUND,

	// -- The Stoatworks About block: a text line, then one button per link.
	PT_ABOUT_TEXT,
	PT_COUNT = PT_ABOUT_TEXT + 1 + stoatworks::about::kButtonCount
};

/// The ids in the order the host sees them.
const std::vector< unsigned int >& HostOrder();

/// The group each control is shown under, and its display name.
const char* GroupOf( unsigned int id );
const char* NameOf( unsigned int id );

/// Whose time the candles keep.
enum class Clock
{
	Resolume = 0,///< Resolume's clip time, times Speed
	Wall,        ///< this machine's clock, as the test card's live output keeps it
	Count
};
const char* ClockName( Clock clock );

/// What is behind the pumpkins.
enum class Background
{
	Night = 0,///< each tile its own night, as the test card draws them
	Dark,     ///< the test card's page colour on every tile: no grid to see
	None,     ///< transparent: the pumpkins, their glow and their shadows only
	Count
};
const char* BackgroundName( Background background );

constexpr int kMinTile = 16, kMaxTile = 4096;
constexpr int kMaxOrigin = 4096;
constexpr int kMaxSeed   = 9999;
constexpr int kDefaultTile = 168;///< the test card's default LED tile

/// How fast the candles and faces keep time: 0 at the bottom (still), then a
/// quarter to four times the test card's, geometrically, 1x at the middle.
double SpeedFromParam( float v );
float ParamFromSpeed( double speed );

int OptionIndex( float value, int count );
int IntegerParam( float value, int lo, int hi );

/// The test card's wall clock starts at 2026-01-01T00:00:00Z (`ENDLESS_EPOCH_S`).
constexpr double kEndlessEpochSeconds = 1767225600.0;

} // namespace pumpkin
