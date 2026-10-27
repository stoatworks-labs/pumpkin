#include "Controls.h"

#include "Lantern.h"

#include <algorithm>
#include <cmath>

namespace pumpkin
{
namespace
{
constexpr double kSpeedLow = 0.25, kSpeedHigh = 4.0;
} // namespace

const std::vector< unsigned int >& HostOrder()
{
	static const std::vector< unsigned int > order = [] {
		std::vector< unsigned int > out;
		for( unsigned int id = 0; id < PT_COUNT; ++id )
			out.push_back( id );
		return out;
	}();
	return order;
}

const char* GroupOf( unsigned int id )
{
	if( id <= PT_SEED )
		return "Patch";
	if( id <= PT_FACE_LAST )
		return "Faces";
	if( id <= PT_CLOCK )
		return "Candles";
	if( id <= PT_BACKGROUND )
		return "Look";
	return "About";
}

const char* NameOf( unsigned int id )
{
	if( id >= PT_FACE_FIRST && id <= PT_FACE_LAST )
		return FaceName( static_cast< int >( id - PT_FACE_FIRST ) );
	switch( id )
	{
	case PT_TILE_WIDTH: return "Tile Width";
	case PT_TILE_HEIGHT: return "Tile Height";
	case PT_ORIGIN_X: return "Origin X";
	case PT_ORIGIN_Y: return "Origin Y";
	case PT_SEED: return "Seed";
	case PT_FLICKER: return "Flicker";
	case PT_EXPRESSIONS: return "Expressions";
	case PT_SPEED: return "Speed";
	case PT_CLOCK: return "Clock";
	case PT_BACKGROUND: return "Background";
	default: return "?";
	}
}

const char* ClockName( Clock clock )
{
	switch( clock )
	{
	case Clock::Resolume: return "Resolume";
	case Clock::Wall: return "Wall";
	default: return "?";
	}
}

const char* BackgroundName( Background background )
{
	switch( background )
	{
	case Background::Night: return "Night";
	case Background::Dark: return "Dark";
	case Background::None: return "None";
	default: return "?";
	}
}

double SpeedFromParam( float v )
{
	//Exactly zero at the bottom: every candle and face stands still.
	if( v <= 0.0f )
		return 0.0;
	return kSpeedLow * std::pow( kSpeedHigh / kSpeedLow, std::clamp( static_cast< double >( v ), 0.0, 1.0 ) );
}

float ParamFromSpeed( double speed )
{
	if( speed <= 0.0 )
		return 0.0f;
	const double s = std::clamp( speed, kSpeedLow, kSpeedHigh );
	return static_cast< float >( std::log( s / kSpeedLow ) / std::log( kSpeedHigh / kSpeedLow ) );
}

int OptionIndex( float value, int count )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), 0, count - 1 );
}

int IntegerParam( float value, int lo, int hi )
{
	return std::clamp( static_cast< int >( std::lround( value ) ), lo, hi );
}

} // namespace pumpkin
