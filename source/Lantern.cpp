#include "Lantern.h"

#include "Random.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <initializer_list>
#include <limits>

namespace pumpkin
{
namespace
{
constexpr double kPi  = 3.14159265358979323846;
constexpr double kTau = kPi * 2.0;

double clamp01( double v )
{
	return std::max( 0.0, std::min( 1.0, v ) );
}

//---------------------------------------------------------------------------
// shapes.ts and the helpers at the top of art/halloween.ts.
//---------------------------------------------------------------------------
Pt centroid( const std::vector< Pt >& pts )
{
	double x = 0.0, y = 0.0;
	for( const Pt& p : pts )
	{
		x += p.x;
		y += p.y;
	}
	return { x / static_cast< double >( pts.size() ), y / static_cast< double >( pts.size() ) };
}

std::vector< Pt > scaleAbout( const std::vector< Pt >& pts, double kx, double ky, Pt about )
{
	std::vector< Pt > out;
	out.reserve( pts.size() );
	for( const Pt& p : pts )
		out.push_back( { about.x + ( p.x - about.x ) * kx, about.y + ( p.y - about.y ) * ky } );
	return out;
}

std::vector< Pt > scaleAbout( const std::vector< Pt >& pts, double kx, double ky )
{
	return scaleAbout( pts, kx, ky, centroid( pts ) );
}

std::vector< Pt > mirrorPts( const std::vector< Pt >& pts )
{
	std::vector< Pt > out;
	out.reserve( pts.size() );
	for( const Pt& p : pts )
		out.push_back( { -p.x, p.y } );
	return out;
}

Pt lerp( Pt a, Pt b, double t )
{
	return { a.x + ( b.x - a.x ) * t, a.y + ( b.y - a.y ) * t };
}

Pt rotate( Pt p, double angle, Pt about )
{
	const double c = std::cos( angle );
	const double s = std::sin( angle );
	const double x = p.x - about.x;
	const double y = p.y - about.y;
	return { about.x + x * c - y * s, about.y + x * s + y * c };
}

std::vector< Pt > ellipsePts( const EllipseSpec& e )
{
	std::vector< Pt > out;
	out.reserve( static_cast< size_t >( e.n ) );
	for( int i = 0; i < e.n; ++i )
	{
		const double a = ( static_cast< double >( i ) / e.n ) * kPi * 2.0;
		out.push_back( rotate( { e.cx + std::cos( a ) * e.rx, e.cy + std::sin( a ) * e.ry }, e.rotation, { e.cx, e.cy } ) );
	}
	return out;
}

Shape ellipseShape( double cx, double cy, double rx, double ry, double rotation, int n )
{
	Shape s;
	s.isEllipse = true;
	s.ellipse   = { cx, cy, rx, ry, rotation, n };
	s.pts       = ellipsePts( s.ellipse );
	return s;
}

std::vector< Pt > pts( std::initializer_list< Pt > list )
{
	return std::vector< Pt >( list );
}

Shape sharp( std::vector< Pt > p )
{
	Shape s;
	s.pts    = std::move( p );
	s.smooth = false;
	return s;
}

Shape round( std::vector< Pt > p )
{
	Shape s;
	s.pts    = std::move( p );
	s.smooth = true;
	return s;
}

Shape round( Shape ellipse )
{
	ellipse.smooth = true;
	return ellipse;
}

//---------------------------------------------------------------------------
// The body.
//---------------------------------------------------------------------------
struct Rib
{
	double cx, cy, rx, ry;
	uint32_t fill;
};
const Rib kRibs[] = {
	{ -0.3, 0.02, 0.2, 0.32, 0xa3420b },
	{ 0.3, 0.02, 0.2, 0.32, 0xa3420b },
	{ -0.165, 0.005, 0.215, 0.355, 0xc0540f },
	{ 0.165, 0.005, 0.215, 0.355, 0xc0540f },
	{ 0.0, 0.0, 0.2, 0.37, 0xd66717 },
};

std::vector< Shape > pumpkinBody()
{
	std::vector< Shape > out;
	for( const Rib& r : kRibs )
	{
		Shape s       = ellipseShape( r.cx, r.cy, r.rx, r.ry, 0.0, 40 );
		s.hasFill     = true;
		s.fill        = r.fill;
		s.hasStroke   = true;
		s.stroke      = 0x6a2805;
		s.strokeWidth = 0.012;
		out.push_back( s );
	}
	{
		Shape s   = ellipseShape( -0.07, -0.2, 0.05, 0.09, 0.3, 20 );
		s.hasFill = true;
		s.fill    = 0xf59a45;
		s.alpha   = 0.3;
		out.push_back( s );
	}
	{
		Shape s;
		s.pts     = pts( { { -0.045, -0.33 }, { 0.04, -0.335 }, { 0.06, -0.42 }, { 0.1, -0.5 }, { 0.065, -0.52 }, { 0.02, -0.455 }, { -0.03, -0.41 } } );
		s.hasFill = true;
		s.fill    = 0x5d5a24;
		out.push_back( s );
	}
	{
		Shape s   = ellipseShape( 0.083, -0.507, 0.022, 0.012, 0.5, 12 );
		s.hasFill = true;
		s.fill    = 0x9a9450;
		out.push_back( s );
	}
	{
		Shape s;
		s.pts     = pts( { { -0.04, -0.35 }, { -0.1, -0.42 }, { -0.17, -0.41 }, { -0.16, -0.36 }, { -0.1, -0.33 } } );
		s.hasFill = true;
		s.fill    = 0x3d7428;
		out.push_back( s );
	}
	{
		Shape s;
		s.pts         = pts( { { 0.04, -0.36 }, { 0.12, -0.4 }, { 0.17, -0.36 }, { 0.15, -0.31 }, { 0.11, -0.33 }, { 0.13, -0.36 } } );
		s.hasStroke   = true;
		s.stroke      = 0x4a7326;
		s.strokeWidth = 0.014;
		s.closed      = false;
		out.push_back( s );
	}
	return out;
}

//---------------------------------------------------------------------------
// The faces.
//---------------------------------------------------------------------------
/// A mouth of interlocking zig-zag teeth, eight a side, between two curves.
std::vector< Pt > zigzagMouth( double open )
{
	const int n       = 8;
	const double half = 0.3;
	auto c            = [ & ]( double x ) {
		const double u = x / half;
		return std::max( 0.0, 1.0 - u * u );
	};
	auto tooth = [ & ]( double x ) { return 0.045 * std::sqrt( c( x ) ); };
	std::vector< Pt > top, bottom;
	for( int i = 0; i <= 2 * n; i++ )
	{
		const double x = -half + ( 2 * half * i ) / ( 2 * n );
		top.push_back( { x, 0.11 + 0.06 * c( x ) + ( i % 2 ? tooth( x ) : 0.0 ) } );
	}
	for( int i = 0; i < 2 * n; i++ )
	{
		const double x = half - ( 2 * half * ( i + 0.5 ) ) / ( 2 * n );
		bottom.push_back( { x, 0.11 + ( 0.17 + 0.04 * open ) * c( x ) - ( i % 2 ? tooth( x ) : 0.0 ) } );
	}
	top.insert( top.end(), bottom.begin(), bottom.end() );
	return top;
}

/// A lens between two points, bulging `up` on one side and `down` on the other.
std::vector< Pt > lens( Pt a, Pt b, double up, double down, int n = 10 )
{
	const Pt d = { b.x - a.x, b.y - a.y };
	double l   = std::hypot( d.x, d.y );
	if( l == 0.0 )
		l = 1.0;
	//The normal that points up the picture (y down, so negative y).
	Pt nrm = { -d.y / l, d.x / l };
	if( nrm.y > 0 )
		nrm = { -nrm.x, -nrm.y };
	std::vector< Pt > out;
	for( int k = 0; k <= n; k++ )
	{
		const Pt p     = lerp( a, b, static_cast< double >( k ) / n );
		const double s = std::sin( ( kPi * k ) / n );
		out.push_back( { p.x + nrm.x * up * s, p.y + nrm.y * up * s } );
	}
	for( int k = n - 1; k > 0; k-- )
	{
		const Pt p     = lerp( a, b, static_cast< double >( k ) / n );
		const double s = std::sin( ( kPi * k ) / n );
		out.push_back( { p.x - nrm.x * down * s, p.y - nrm.y * down * s } );
	}
	return out;
}

std::vector< Shape > faceHoles( Face face, double e )
{
	switch( face )
	{
	case Face::Classic:
	{
		const std::vector< Pt > eye   = pts( { { -0.25, -0.02 }, { -0.155, -0.19 }, { -0.06, -0.02 } } );
		const std::vector< Pt > mouth = pts( {
			{ -0.3, 0.07 }, { -0.12, 0.15 }, { -0.12, 0.22 }, { -0.04, 0.22 }, { -0.04, 0.16 }, { 0.3, 0.07 },
			{ 0.24, 0.17 }, { 0.13, 0.25 }, { 0.13, 0.19 }, { 0.05, 0.19 }, { 0.05, 0.27 }, { -0.1, 0.28 }, { -0.24, 0.18 },
		} );
		//The jaw drops from the top lip: it chomps.
		return { sharp( eye ), sharp( mirrorPts( eye ) ), sharp( pts( { { -0.045, 0.075 }, { 0, 0.005 }, { 0.045, 0.075 } } ) ),
			     sharp( scaleAbout( mouth, 1, 1 + 0.3 * e, { 0, 0.08 } ) ) };
	}
	case Face::Fierce:
	{
		const std::vector< Pt > eye    = pts( { { -0.27, -0.14 }, { -0.05, -0.05 }, { -0.08, 0.0 }, { -0.2, 0.0 }, { -0.26, -0.06 } } );
		const std::vector< Pt > squint = scaleAbout( eye, 1, 1 - 0.5 * e );
		return { sharp( squint ), sharp( mirrorPts( squint ) ), sharp( pts( { { -0.035, 0.06 }, { 0, 0.0 }, { 0.035, 0.06 } } ) ),
			     sharp( zigzagMouth( e ) ) };
	}
	case Face::Happy:
	{
		auto eye = [ & ]( double x ) { return ellipseShape( x, -0.08, 0.07, 0.07 * ( 1 - 0.85 * e ), 0, 20 ); };
		std::vector< Pt > top, bottom;
		for( int i = 0; i <= 12; i++ )
		{
			const double x = -0.27 + ( 0.54 * i ) / 12;
			const double u = x / 0.27;
			const double c = 1 - u * u;
			top.push_back( { x, 0.09 + 0.09 * c } );
			bottom.push_back( { x, 0.09 + 0.2 * c } );
		}
		std::reverse( bottom.begin(), bottom.end() );
		top.insert( top.end(), bottom.begin(), bottom.end() );
		return { round( eye( -0.15 ) ), round( eye( 0.15 ) ), sharp( pts( { { -0.035, 0.05 }, { 0, -0.005 }, { 0.035, 0.05 } } ) ),
			     round( top ) };
	}
	case Face::Surprised:
	{
		const double g = 1 + 0.12 * e;
		return {
			round( ellipseShape( -0.14, -0.09, 0.055 * g, 0.085 * g, 0, 20 ) ),
			round( ellipseShape( 0.14, -0.09, 0.055 * g, 0.085 * g, 0, 20 ) ),
			sharp( pts( { { -0.025, 0.05 }, { 0, 0.01 }, { 0.025, 0.05 } } ) ),
			round( ellipseShape( 0, 0.2, 0.075 * ( 1 + 0.25 * e ), 0.095 * ( 1 + 0.3 * e ), 0, 24 ) ),
		};
	}
	case Face::Sly:
	{
		//Cat's eyes, outer corners up, narrowing to slits.
		const std::vector< Pt > left = lens( { -0.27, -0.12 }, { -0.07, -0.04 }, 0.055 * ( 1 - 0.7 * e ), 0.04 * ( 1 - 0.7 * e ) );
		return {
			sharp( left ),
			sharp( mirrorPts( left ) ),
			sharp( pts( { { -0.03, 0.06 }, { 0, 0.02 }, { 0.03, 0.06 } } ) ),
			sharp( pts( {
				{ -0.26, 0.12 }, { -0.12, 0.17 }, { -0.09, 0.17 }, { -0.075, 0.24 }, { -0.06, 0.17 }, { 0.06, 0.17 },
				{ 0.075, 0.24 }, { 0.09, 0.17 }, { 0.2, 0.14 }, { 0.28, 0.08 },
				{ 0.22, 0.17 }, { 0.1, 0.27 }, { 0, 0.29 }, { -0.12, 0.27 }, { -0.22, 0.19 },
			} ) ),
		};
	}
	case Face::Goofy:
	default:
		//One big eye, which winks; one small one, which does not.
		return {
			round( ellipseShape( -0.15, -0.1, 0.09, 0.09 * ( 1 - 0.88 * e ), 0, 22 ) ),
			round( ellipseShape( 0.16, -0.06, 0.045, 0.05, 0, 16 ) ),
			round( ellipseShape( 0, 0.04, 0.03, 0.03, 0, 12 ) ),
			sharp( pts( {
				{ -0.28, 0.1 }, { -0.02, 0.15 }, { -0.02, 0.21 }, { 0.05, 0.21 }, { 0.05, 0.15 }, { 0.24, 0.12 },
				{ 0.2, 0.2 }, { 0.05, 0.29 }, { -0.15, 0.27 }, { -0.25, 0.18 },
			} ) ),
		};
	}
}

/// A carved hole: the wall it cuts, a little smaller and higher. An ellipse
/// stays an ellipse (none of the carved ones is rotated, so scaling x and y
/// apart keeps it one).
Shape holeOf( const Shape& wall )
{
	Shape h    = wall;
	const Pt c = centroid( wall.pts );
	h.pts      = scaleAbout( wall.pts, 0.88, 0.8, c );
	for( Pt& p : h.pts )
		p.y -= 0.01;
	if( h.isEllipse )
	{
		h.ellipse.cx = c.x + ( wall.ellipse.cx - c.x ) * 0.88;
		h.ellipse.cy = c.y + ( wall.ellipse.cy - c.y ) * 0.8 - 0.01;
		h.ellipse.rx *= 0.88;
		h.ellipse.ry *= 0.8;
	}
	return h;
}

/// `phase( t, loopSeconds, cycles )`.
double phase( double t, double loopSeconds, double cycles )
{
	const double p = std::fmod( ( t / loopSeconds ) * cycles, 1.0 );
	return p < 0 ? p + 1 : p;
}

/// `cyclesPerLoop( natural, true )`: endless, nothing is rounded.
double endlessCycles( double natural )
{
	return std::max( natural, std::numeric_limits< double >::denorm_min() );
}

const double kCandleHz[ 3 ]  = { 1.3, 3.1, 5.3 };
const double kCandleAmp[ 3 ] = { 0.13, 0.09, 0.05 };
} // namespace

const char* FaceName( int face )
{
	switch( static_cast< Face >( face ) )
	{
	case Face::Classic: return "Classic";
	case Face::Fierce: return "Fierce";
	case Face::Happy: return "Happy";
	case Face::Surprised: return "Surprised";
	case Face::Sly: return "Sly";
	case Face::Goofy: return "Goofy";
	default: return "?";
	}
}

Lantern MakeLantern( int face, double expression )
{
	Lantern out;
	out.body = pumpkinBody();
	if( face < 0 || face >= kFaceCount )
		return out;
	out.walls = faceHoles( static_cast< Face >( face ), clamp01( expression ) );
	for( const Shape& w : out.walls )
		out.holes.push_back( holeOf( w ) );
	return out;
}

uint32_t TileKey( int seed, double x, double y )
{
	//Math.round: half up.
	return Hash( { static_cast< double >( seed ), std::floor( x + 0.5 ), std::floor( y + 0.5 ), 0.0 } );
}

int TileFace( uint32_t key, unsigned faceMask )
{
	int faces[ kFaceCount ];
	int count = 0;
	for( int f = 0; f < kFaceCount; ++f )
		if( faceMask & ( 1u << f ) )
			faces[ count++ ] = f;
	if( count == 0 )
		return -1;
	Rng r( key );
	return faces[ std::min( count - 1, static_cast< int >( std::floor( r() * count ) ) ) ];
}

TileClock TileClock::For( uint32_t key )
{
	TileClock c;
	{
		Rng r( Hash( { static_cast< double >( key ), 501.0 } ) );
		for( int i = 0; i < 3; ++i )
		{
			c.candleCycles[ i ] = endlessCycles( kLoopSeconds * kCandleHz[ i ] * r.Between( 0.85, 1.15 ) );
			c.candleOffset[ i ] = r();
		}
	}
	{
		Rng r( Hash( { static_cast< double >( key ), 502.0 } ) );
		c.goes             = endlessCycles( kLoopSeconds / r.Between( 2.5, 4 ) );
		c.expressionOffset = r();
	}
	return c;
}

double TileClock::Candle( double t ) const
{
	double v = 0.72;
	for( int i = 0; i < 3; ++i )
		v += kCandleAmp[ i ] * std::sin( kTau * phase( t, kLoopSeconds, candleCycles[ i ] ) + candleOffset[ i ] * kTau );
	return clamp01( v );
}

double TileClock::Expression( double t ) const
{
	const double p   = std::fmod( phase( t, kLoopSeconds, goes ) + expressionOffset, 1.0 );
	const double len = 0.14;
	return p < len ? std::sin( ( kPi * p ) / len ) : 0.0;
}

double CandleLevel( double t, uint32_t key )
{
	return TileClock::For( key ).Candle( t );
}

double ExpressionLevel( double t, uint32_t key )
{
	return TileClock::For( key ).Expression( t );
}

const uint32_t kTileNights[ 6 ] = { 0x1b1230, 0x10201c, 0x24122a, 0x111a2c, 0x20160e, 0x170f22 };

int NightIndex( int col, int row )
{
	const int m = ( col + row * 2 ) % 6;
	return m < 0 ? m + 6 : m;
}

uint32_t MixHex( uint32_t a, uint32_t b, double t )
{
	const double k = std::max( 0.0, std::min( 1.0, t ) );
	uint32_t out   = 0;
	for( int shift = 16; shift >= 0; shift -= 8 )
	{
		const double x = static_cast< double >( ( a >> shift ) & 0xFFu );
		const double y = static_cast< double >( ( b >> shift ) & 0xFFu );
		out |= static_cast< uint32_t >( std::floor( x + ( y - x ) * k + 0.5 ) ) << shift;
	}
	return out;
}

} // namespace pumpkin
