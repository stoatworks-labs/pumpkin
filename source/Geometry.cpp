#include "Geometry.h"

#include <algorithm>
#include <cmath>

namespace pumpkin::geometry
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

Pt mid( Pt a, Pt b )
{
	return { a.x + ( b.x - a.x ) * 0.5, a.y + ( b.y - a.y ) * 0.5 };
}

Pt quadratic( Pt p0, Pt p1, Pt p2, double u )
{
	const double a = ( 1 - u ) * ( 1 - u ), b = 2 * u * ( 1 - u ), c = u * u;
	return { a * p0.x + b * p1.x + c * p2.x, a * p0.y + b * p1.y + c * p2.y };
}

/// The i-th quadratic's three control points, as `tracePath` lays them.
void controls( const Shape& s, int i, Pt& p0, Pt& p1, Pt& p2 )
{
	const auto& p = s.pts;
	const int n   = static_cast< int >( p.size() );
	if( s.closed )
	{
		p0 = mid( p[ static_cast< size_t >( ( i + n - 1 ) % n ) ], p[ static_cast< size_t >( i ) ] );
		p1 = p[ static_cast< size_t >( i ) ];
		p2 = mid( p[ static_cast< size_t >( i ) ], p[ static_cast< size_t >( ( i + 1 ) % n ) ] );
	}
	else
	{
		//moveTo p0, then quadraticCurveTo( p[ i ], mid( p[ i ], p[ i + 1 ] ) )
		//for i = 1 .. n - 2, then a line to the last point.
		p0 = i == 0 ? p[ 0 ] : mid( p[ static_cast< size_t >( i ) ], p[ static_cast< size_t >( i + 1 ) ] );
		p1 = p[ static_cast< size_t >( i + 1 ) ];
		p2 = mid( p[ static_cast< size_t >( i + 1 ) ], p[ static_cast< size_t >( i + 2 ) ] );
	}
}

bool smoothed( const Shape& s )
{
	return s.smooth && s.pts.size() >= 3;
}

void growBox( float box[ 4 ], double x, double y )
{
	box[ 0 ] = std::min( box[ 0 ], static_cast< float >( x ) );
	box[ 1 ] = std::min( box[ 1 ], static_cast< float >( y ) );
	box[ 2 ] = std::max( box[ 2 ], static_cast< float >( x ) );
	box[ 3 ] = std::max( box[ 3 ], static_cast< float >( y ) );
}

void ellipseOf( const EllipseSpec& e, float out[ 4 ] )
{
	const double k = SmoothedRadius( e.n );
	out[ 0 ]       = static_cast< float >( e.cx );
	out[ 1 ]       = static_cast< float >( e.cy );
	out[ 2 ]       = static_cast< float >( e.rx * k );
	out[ 3 ]       = static_cast< float >( e.ry * k );
}

void rgb( uint32_t hex, float out[ 3 ] )
{
	out[ 0 ] = static_cast< float >( ( hex >> 16 ) & 0xFFu ) / 255.0f;
	out[ 1 ] = static_cast< float >( ( hex >> 8 ) & 0xFFu ) / 255.0f;
	out[ 2 ] = static_cast< float >( hex & 0xFFu ) / 255.0f;
}

/// One shape, from the same shape at expression 0 and at 1.
RenderShape render( const Shape& a, const Shape& b, int paint )
{
	RenderShape r;
	r.paint  = paint;
	r.box[ 0 ] = r.box[ 1 ] = 1e9f;
	r.box[ 2 ] = r.box[ 3 ] = -1e9f;
	if( a.hasFill || paint != kSolid )
	{
		rgb( a.fill, r.fill );
		r.fill[ 3 ] = static_cast< float >( a.alpha );
	}
	if( a.hasStroke )
	{
		rgb( a.stroke, r.stroke );
		r.stroke[ 3 ] = static_cast< float >( a.strokeWidth );
		r.strokeAlpha = static_cast< float >( a.alpha );
	}

	if( a.isEllipse && smoothed( a ) )
	{
		r.kind = kEllipse;
		ellipseOf( a.ellipse, r.ellipse0 );
		ellipseOf( b.ellipse, r.ellipse1 );
		r.cosR = static_cast< float >( std::cos( a.ellipse.rotation ) );
		r.sinR = static_cast< float >( std::sin( a.ellipse.rotation ) );
		for( const float* e : { r.ellipse0, r.ellipse1 } )
		{
			//The rotated ellipse's half-extents along x and y.
			const double hx = std::hypot( e[ 2 ] * r.cosR, e[ 3 ] * r.sinR );
			const double hy = std::hypot( e[ 2 ] * r.sinR, e[ 3 ] * r.cosR );
			growBox( r.box, e[ 0 ] - hx, e[ 1 ] - hy );
			growBox( r.box, e[ 0 ] + hx, e[ 1 ] + hy );
		}
	}
	else
	{
		r.kind = a.closed ? kPolygon : kPolyline;
		std::vector< Pt > va = a.pts, vb = b.pts;
		if( smoothed( a ) )
		{
			const std::vector< int > steps = StepsFor( a, b );
			va                             = Flatten( a, steps );
			vb                             = Flatten( b, steps );
		}
		for( size_t i = 0; i < va.size(); ++i )
		{
			r.vertices.insert( r.vertices.end(), { static_cast< float >( va[ i ].x ), static_cast< float >( va[ i ].y ),
			                                       static_cast< float >( vb[ i ].x ), static_cast< float >( vb[ i ].y ) } );
			growBox( r.box, va[ i ].x, va[ i ].y );
			growBox( r.box, vb[ i ].x, vb[ i ].y );
		}
	}
	const float pad = r.stroke[ 3 ] * 0.5f;
	r.box[ 0 ] -= pad;
	r.box[ 1 ] -= pad;
	r.box[ 2 ] += pad;
	r.box[ 3 ] += pad;
	return r;
}
} // namespace

double SmoothedRadius( int n )
{
	const double s = std::sin( kPi / n );
	return ( std::cos( kPi / n ) + 1.0 - 0.5 * s * s ) * 0.5;
}

int QuadraticCount( const Shape& shape )
{
	const int n = static_cast< int >( shape.pts.size() );
	return shape.closed ? n : std::max( 0, n - 2 );
}

Pt OnCurve( const Shape& shape, int i, double u )
{
	Pt p0, p1, p2;
	controls( shape, i, p0, p1, p2 );
	return quadratic( p0, p1, p2, u );
}

std::vector< int > StepsFor( const Shape& a, const Shape& b )
{
	//A quadratic cut into N equal pieces of its parameter strays from each
	//chord by at most |P0 - 2 P1 + P2| / ( 4 N^2 ).
	std::vector< int > steps;
	for( int i = 0; i < QuadraticCount( a ); ++i )
	{
		double bend = 0.0;
		for( const Shape* s : { &a, &b } )
		{
			Pt p0, p1, p2;
			controls( *s, i, p0, p1, p2 );
			bend = std::max( bend, std::hypot( p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y ) );
		}
		const double n = std::sqrt( bend * kFlattenSizePx / ( 4.0 * kFlattenTolerancePx ) );
		steps.push_back( std::clamp( static_cast< int >( std::ceil( n ) ), 1, 64 ) );
	}
	return steps;
}

std::vector< Pt > Flatten( const Shape& shape, const std::vector< int >& steps )
{
	std::vector< Pt > out;
	const int count = QuadraticCount( shape );
	for( int i = 0; i < count; ++i )
	{
		Pt p0, p1, p2;
		controls( shape, i, p0, p1, p2 );
		if( i == 0 )
			out.push_back( p0 );
		const int n = steps[ static_cast< size_t >( i ) ];
		for( int k = 1; k <= n; ++k )
			out.push_back( quadratic( p0, p1, p2, static_cast< double >( k ) / n ) );
	}
	if( shape.closed )
		out.pop_back();//the last quadratic ends where the first began
	else
		out.push_back( shape.pts.back() );
	return out;
}

const Table& Build()
{
	static const Table table = [] {
		Table t;
		const Lantern plain = MakeLantern( -1, 0.0 );
		t.bodyFirst         = 0;
		for( const Shape& s : plain.body )
			t.shapes.push_back( render( s, s, kSolid ) );
		t.bodyCount = static_cast< int >( t.shapes.size() );
		for( int f = 0; f < kFaceCount; ++f )
		{
			const Lantern rest = MakeLantern( f, 0.0 ), height = MakeLantern( f, 1.0 );
			t.faceFirst[ f ]   = static_cast< int >( t.shapes.size() );
			//All the walls, then all the holes: the canvas fills them in that order.
			for( size_t i = 0; i < rest.walls.size(); ++i )
				t.shapes.push_back( render( rest.walls[ i ], height.walls[ i ], kWall ) );
			for( size_t i = 0; i < rest.holes.size(); ++i )
				t.shapes.push_back( render( rest.holes[ i ], height.holes[ i ], kFlame ) );
			t.faceCount[ f ] = static_cast< int >( t.shapes.size() ) - t.faceFirst[ f ];
		}
		return t;
	}();
	return table;
}

int Table::VertexCount() const
{
	int n = 0;
	for( const RenderShape& s : shapes )
		n += static_cast< int >( s.vertices.size() / 4 );
	return n;
}

int Table::VertexRows() const
{
	return std::max( 1, ( VertexCount() + kVertexRow - 1 ) / kVertexRow );
}

std::vector< float > Table::ShapeTexels() const
{
	std::vector< float > out( static_cast< size_t >( kTexelsPerShape ) * 4 * shapes.size(), 0.0f );
	int first = 0;
	for( size_t i = 0; i < shapes.size(); ++i )
	{
		const RenderShape& s = shapes[ i ];
		float* row           = &out[ i * kTexelsPerShape * 4 ];
		const int count      = static_cast< int >( s.vertices.size() / 4 );
		const float head[ 4 ] = { static_cast< float >( s.kind ), static_cast< float >( first ), static_cast< float >( count ),
			                      static_cast< float >( s.paint ) };
		const float extra[ 4 ] = { s.cosR, s.sinR, s.strokeAlpha, 0.0f };
		const float* texels[]  = { head, s.fill, s.stroke, s.box, s.ellipse0, s.ellipse1, extra };
		for( int k = 0; k < 7; ++k )
			std::copy( texels[ k ], texels[ k ] + 4, row + k * 4 );
		first += count;
	}
	return out;
}

std::vector< float > Table::VertexTexels() const
{
	std::vector< float > out( static_cast< size_t >( kVertexRow ) * VertexRows() * 4, 0.0f );
	size_t at = 0;
	for( const RenderShape& s : shapes )
	{
		std::copy( s.vertices.begin(), s.vertices.end(), out.begin() + static_cast< long >( at ) );
		at += s.vertices.size();
	}
	return out;
}

} // namespace pumpkin::geometry
