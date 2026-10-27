#pragma once

#include "Lantern.h"

#include <vector>

/**
    The jack-o'-lantern as the shader draws it: a table of shapes and a table
    of vertices, built once from Lantern.cpp's port and uploaded as two
    RGBA32F textures. No GL here, so `pktest --offline` checks the build.

    **The canvas smooths every body shape and every `round` hole** (quadratic
    curves through the edge midpoints, `tracePath`). Three kinds of thing come
    out of that, and each is drawn its own way:

    - An `ellipse()` n-gon, smoothed, is an ellipse: the curve of a regular
      n-gon swings between cos(pi/n) of the radius (at the midpoints, which it
      passes through) and 1 - sin^2(pi/n)/2 (at the apexes), so it is drawn as
      a true ellipse at the mean of the two -- within 3e-4 of the radius at
      n = 12, the coarsest the pumpkin has. Smoothing is affine, so a rotated
      or squashed n-gon is the same ellipse rotated or squashed.
    - Any other smoothed list (the stem, the leaf, the tendril, the happy
      mouth) is flattened: each quadratic cut into enough straight pieces
      that a pumpkin 4096 pixels wide is within 0.1 px of the curve.
    - A `sharp` list is a polygon, as it is.

    **Every vertex is linear in the expression.** Each face's blink, wink or
    chomp moves its points as a + e (b - a) -- scaled radii, scaled lenses,
    a jaw scaled about a fixed point, holes scaled about a centroid that moves
    linearly too -- and smoothing is linear in its control points. So each
    vertex is stored at e = 0 and e = 1 and the shader mixes them per tile.
    `pktest --linear` holds the port to that at e = 0.25 and 0.5.
*/
namespace pumpkin::geometry
{
enum Kind : int
{
	kEllipse  = 0,
	kPolygon  = 1,///< closed, filled, even-odd
	kPolyline = 2,///< open, stroked with round caps and joins
};

enum Paint : int
{
	kSolid = 0,///< the fill colour in the table
	kWall  = 1,///< the cut flesh: mix( #7a3a10, #ffd27a, lit )
	kFlame = 2,///< candlelight: the radial gradient, by lit
};

/// Texels per shape row in the shape texture.
///   0  kind, first vertex, vertex count, paint
///   1  fill r, g, b, alpha (0: no fill)
///   2  stroke r, g, b, width (0: no stroke)
///   3  bounding box min x, min y, max x, max y, over both expressions, stroke included
///   4  ellipse at e = 0: cx, cy, rx, ry (the radii already the smoothed n-gon's)
///   5  ellipse at e = 1
///   6  cos( rotation ), sin( rotation ), stroke alpha, 0
///   7  unused
constexpr int kTexelsPerShape = 8;
/// Vertices per row of the vertex texture: vertex i is at ( i % 256, i / 256 ).
constexpr int kVertexRow = 256;

/// The flattening's error bound: 0.1 px on a pumpkin this many pixels wide.
constexpr double kFlattenSizePx = 4096.0;
constexpr double kFlattenTolerancePx = 0.1;

struct RenderShape
{
	int kind = kPolygon, paint = kSolid;
	float fill[ 4 ] = {}, stroke[ 4 ] = {};
	float strokeAlpha = 0.0f;
	float box[ 4 ] = {};
	float ellipse0[ 4 ] = {}, ellipse1[ 4 ] = {};
	float cosR = 1.0f, sinR = 0.0f;
	/// x, y at e = 0 then x, y at e = 1, per vertex.
	std::vector< float > vertices;
};

struct Table
{
	std::vector< RenderShape > shapes;
	int bodyFirst = 0, bodyCount = 0;
	int faceFirst[ kFaceCount ] = {}, faceCount[ kFaceCount ] = {};

	/// The two textures' texels, RGBA. Shapes: kTexelsPerShape x shapes.size();
	/// vertices: kVertexRow x VertexRows(), zero-padded.
	std::vector< float > ShapeTexels() const;
	std::vector< float > VertexTexels() const;
	int VertexCount() const;
	int VertexRows() const;
};

/// Built once, on first use.
const Table& Build();

/// The radius of a smoothed regular n-gon, as a fraction of the n-gon's.
double SmoothedRadius( int n );

/// A smoothed list flattened (closed or open), `steps[ i ]` pieces for the
/// i-th quadratic. For the checks.
std::vector< Pt > Flatten( const Shape& shape, const std::vector< int >& steps );

/// Pieces per quadratic for the error bound, the larger of two expressions.
std::vector< int > StepsFor( const Shape& a, const Shape& b );

/// The point at parameter `u` on the smoothed curve's i-th quadratic, exactly.
Pt OnCurve( const Shape& shape, int quadratic, double u );
int QuadraticCount( const Shape& shape );

} // namespace pumpkin::geometry
