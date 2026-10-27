#pragma once

#include <cmath>
#include <cstdint>
#include <initializer_list>

/**
    The test card's seeded randomness (`src/patterns/random.ts`), bit for bit.

    Which face goes on which tile, how each candle flickers and when each face
    blinks all come out of these two functions, so a port that drifted by one
    bit would carve a different patch. `pktest --port` holds them to numbers
    the test card's own TypeScript printed (tools/fixtures/testcard.txt).

    JavaScript does this arithmetic in int32 and uint32 (`Math.imul`, `>>>`);
    uint32_t wraps the same way, so every step below is the same bits.
*/
namespace pumpkin
{
/// What the test card's `Math.floor( v ) >>> 0` makes of a number: ToUint32,
/// which wraps negatives (a tile left of the origin) modulo 2^32.
inline uint32_t ToUint32( double v )
{
	double m = std::fmod( std::floor( v ), 4294967296.0 );
	if( m < 0.0 )
		m += 4294967296.0;
	return static_cast< uint32_t >( m );
}

/// `hash( ...values )`: FNV-1a's offset and prime, with a shift-xor per value.
inline uint32_t Hash( std::initializer_list< double > values )
{
	uint32_t h = 0x811c9dc5u;
	for( const double v : values )
	{
		h ^= ToUint32( v );
		h *= 0x01000193u;
		h ^= h >> 13;
	}
	return h;
}

/// `rng( seed )`: mulberry32, 0..1.
class Rng
{
public:
	explicit Rng( uint32_t seed ) :
		a( seed )
	{
	}

	double operator()()
	{
		a += 0x6d2b79f5u;
		uint32_t t = a;
		t          = ( t ^ ( t >> 15 ) ) * ( t | 1u );
		t ^= t + ( t ^ ( t >> 7 ) ) * ( t | 61u );
		return static_cast< double >( t ^ ( t >> 14 ) ) / 4294967296.0;
	}

	/// `between( r, lo, hi )`.
	double Between( double lo, double hi )
	{
		return lo + ( hi - lo ) * ( *this )();
	}

private:
	uint32_t a;
};

} // namespace pumpkin
