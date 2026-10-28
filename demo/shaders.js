// GENERATED from source/Shaders.cpp by demo/tools/check_shaders.py --write.
// Do not edit: tools/verify.sh fails if a character of this differs from the
// plugin's. The one escape allowed is \` for a backtick inside a comment.
export const VERSION = '#version 410 core\n';

// kCommon, source/Shaders.cpp
export const COMMON = `
const int KIND_ELLIPSE  = 0;
const int KIND_POLYGON  = 1;
const int KIND_POLYLINE = 2;
const int PAINT_SOLID   = 0;
const int PAINT_WALL    = 1;
const int PAINT_FLAME   = 2;
const int VERTEX_ROW    = 256;

// a / b rounded down, b > 0. GLSL leaves / and % with a negative operand
// undefined, and tiles left of and above the origin have negative indices.
int floorDiv( int a, int b )
{
	return a >= 0 ? a / b : -( ( -a - 1 ) / b ) - 1;
}

// Premultiplied over: colour c at coverage a onto what is there.
void over( inout vec4 acc, vec3 c, float a )
{
	acc = vec4( c * a, a ) + acc * ( 1.0 - a );
}

// The test card's mix( a, b, t ): per channel, in code values, rounded half
// up (Math.round). a and b are 0..255.
vec3 mixHex( vec3 a, vec3 b, float t )
{
	return floor( a + ( b - a ) * clamp( t, 0.0, 1.0 ) + 0.5 ) / 255.0;
}
`;

// kQuadVertex, source/Shaders.cpp
export const QUAD_VERTEX = `
layout( location = 0 ) in vec4 vPosition;
layout( location = 1 ) in vec2 vUV;
out vec2 uv;
void main()
{
	gl_Position = vPosition;
	uv          = vUV;
}
`;

// kPatchFragment, source/Shaders.cpp
export const PATCH_FRAGMENT = `
uniform sampler2D Tiles;     // RGBA32F per tile: face (-1 none), lit, expression, night
uniform sampler2D Shapes;    // RGBA32F, 8 texels a shape, one shape a row
uniform sampler2D Vertices;  // RGBA32F: x, y at expression 0; x, y at 1
uniform ivec2 Raster;
uniform ivec2 ViewOrigin;
uniform ivec2 TileSize;
uniform ivec2 Origin;
uniform ivec2 FirstTile;
uniform float Size;          // the pumpkin's width in pixels
uniform vec2 Centre;         // its centre from the tile's top-left, in pixels
uniform ivec2 BodyShapes;    // first, count
uniform ivec2 FaceShapes[ 6 ];
uniform int Background;      // 0 night, 1 dark, 2 none
uniform vec3 Nights[ 6 ];
uniform vec3 PageNight;
uniform vec3 WallColours[ 2 ];   // 0..255: unlit, lit
uniform vec3 FlameColours[ 6 ];  // 0..255: stop 0, 0.45, 1, unlit then lit
uniform vec3 HaloColours[ 2 ];   // 0..255: stop 0, stop 1
uniform float HaloAlpha;         // at stop 0, times lit
uniform int GlowClippedForTest;  // --glow's negative control: Night's clipped glow on every background
out vec4 FragColor;

vec2 vertexAt( int i, float e )
{
	vec4 v = texelFetch( Vertices, ivec2( i % VERTEX_ROW, i / VERTEX_ROW ), 0 );
	return mix( v.xy, v.zw, e );
}

// Signed distance to an ellipse (negative inside), to first order: |u| - 1
// over its gradient, which is exact on the curve and good to a small
// fraction of a pixel within the few pixels either side that coverage reads.
float ellipseDistance( vec2 q, vec4 ell, vec2 cs )
{
	vec2 d = q - ell.xy;
	d      = vec2( cs.x * d.x + cs.y * d.y, -cs.y * d.x + cs.x * d.y );
	vec2 u = d / ell.zw;
	float l = max( length( u ), 1e-6 );
	vec2 g  = u / ( l * ell.zw );
	return ( l - 1.0 ) / max( length( g ), 1e-6 );
}

// Signed distance to a polygon, even-odd inside.
float polygonDistance( vec2 q, int first, int count, float e )
{
	vec2 a      = vertexAt( first + count - 1, e );
	float d2    = 1e20;
	bool inside = false;
	for( int i = 0; i < count; ++i )
	{
		vec2 b    = vertexAt( first + i, e );
		vec2 edge = b - a;
		vec2 w    = q - a;
		vec2 off  = w - edge * clamp( dot( w, edge ) / max( dot( edge, edge ), 1e-20 ), 0.0, 1.0 );
		d2        = min( d2, dot( off, off ) );
		if( ( a.y > q.y ) != ( b.y > q.y ) && q.x < a.x + ( b.x - a.x ) * ( q.y - a.y ) / ( b.y - a.y ) )
			inside = !inside;
		a = b;
	}
	return inside ? -sqrt( d2 ) : sqrt( d2 );
}

// Distance to an open polyline: round caps and joins fall out of it.
float polylineDistance( vec2 q, int first, int count, float e )
{
	vec2 a   = vertexAt( first, e );
	float d2 = dot( q - a, q - a );
	for( int i = 1; i < count; ++i )
	{
		vec2 b    = vertexAt( first + i, e );
		vec2 edge = b - a;
		vec2 w    = q - a;
		vec2 off  = w - edge * clamp( dot( w, edge ) / max( dot( edge, edge ), 1e-20 ), 0.0, 1.0 );
		d2        = min( d2, dot( off, off ) );
		a         = b;
	}
	return sqrt( d2 );
}

// A pixel's footprint (one px wide, centred d from the line) over the band
// of half-width h either side of it.
float band( float d, float h, float px )
{
	return clamp( ( min( d + 0.5 * px, h ) - max( d - 0.5 * px, -h ) ) / px, 0.0, 1.0 );
}

// The candlelight in the holes: a radial gradient about (0, 0.06), from 0.02
// to 0.4, stops at 0, 0.45 and 1.
vec3 flame( vec2 q, float lit )
{
	float t = clamp( ( length( q - vec2( 0.0, 0.06 ) ) - 0.02 ) / 0.38, 0.0, 1.0 );
	vec3 s0 = mixHex( FlameColours[ 0 ], FlameColours[ 3 ], lit );
	vec3 s1 = mixHex( FlameColours[ 1 ], FlameColours[ 4 ], lit );
	vec3 s2 = mixHex( FlameColours[ 2 ], FlameColours[ 5 ], lit );
	return t < 0.45 ? mix( s0, s1, t / 0.45 ) : mix( s1, s2, ( t - 0.45 ) / 0.55 );
}

// A candle's glow on its tile at r pumpkins from the centre: a radial
// gradient from 0.2 to 1.1, its stops unpremultiplied (the canvas's
// gradients interpolate that way).
void glow( inout vec4 acc, float r, float lit )
{
	float t = clamp( ( r - 0.2 ) / 0.9, 0.0, 1.0 );
	over( acc, mix( HaloColours[ 0 ], HaloColours[ 1 ], t ) / 255.0, mix( HaloAlpha * lit, 0.0, t ) );
}

void drawShape( int s, vec2 q, float px, float e, float lit, inout vec4 acc )
{
	vec4 box = texelFetch( Shapes, ivec2( 3, s ), 0 );
	if( any( lessThan( q, box.xy - px ) ) || any( greaterThan( q, box.zw + px ) ) )
		return;
	vec4 head   = texelFetch( Shapes, ivec2( 0, s ), 0 );
	vec4 fill   = texelFetch( Shapes, ivec2( 1, s ), 0 );
	vec4 stroke = texelFetch( Shapes, ivec2( 2, s ), 0 );
	vec4 extra  = texelFetch( Shapes, ivec2( 6, s ), 0 );
	int kind    = int( head.x );
	int paint   = int( head.w );
	float d;
	if( kind == KIND_ELLIPSE )
		d = ellipseDistance( q, mix( texelFetch( Shapes, ivec2( 4, s ), 0 ), texelFetch( Shapes, ivec2( 5, s ), 0 ), e ), extra.xy );
	else if( kind == KIND_POLYGON )
		d = polygonDistance( q, int( head.y ), int( head.z ), e );
	else
		d = polylineDistance( q, int( head.y ), int( head.z ), e );
	if( fill.a > 0.0 && kind != KIND_POLYLINE )
	{
		vec3 c = paint == PAINT_WALL ? mixHex( WallColours[ 0 ], WallColours[ 1 ], lit )
		       : paint == PAINT_FLAME ? flame( q, lit )
		       : fill.rgb;
		over( acc, c, fill.a * clamp( 0.5 - d / px, 0.0, 1.0 ) );
	}
	if( stroke.a > 0.0 )
		over( acc, stroke.rgb, extra.z * band( d, 0.5 * stroke.a, px ) );
}

void main()
{
	ivec2 pix = ivec2( gl_FragCoord.xy ) - ViewOrigin;
	pix.y     = Raster.y - 1 - pix.y;
	ivec2 tile = ivec2( floorDiv( pix.x - Origin.x, TileSize.x ), floorDiv( pix.y - Origin.y, TileSize.y ) );
	vec4 state = texelFetch( Tiles, tile - FirstTile, 0 );
	int face   = int( floor( state.x + 0.5 ) );
	float lit  = state.y;
	float e    = state.z;

	vec4 acc = vec4( 0.0 );
	if( Background == 0 )
		acc = vec4( Nights[ int( state.w ) ], 1.0 );
	else if( Background == 1 )
		acc = vec4( PageNight, 1.0 );

	vec2 corner = vec2( Origin + tile * TileSize );
	vec2 q      = ( vec2( pix ) + 0.5 - corner - Centre ) / Size;
	float px    = 1.0 / Size;

	// The shadow it sits on: an ellipse, black at 0.45.
	vec2 sq = ( q - vec2( 0.0, 0.35 ) ) / vec2( 0.46, 0.07 );
	float sl = max( length( sq ), 1e-6 );
	float sd = ( sl - 1.0 ) / max( length( sq / ( sl * vec2( 0.46, 0.07 ) ) ), 1e-6 );
	over( acc, vec3( 0.0 ), 0.45 * clamp( 0.5 - sd / px, 0.0, 1.0 ) );

	// The glow, behind it. On Night it stops at the tile's edge, as the test
	// card clips it. On Dark and None that edge has nothing to hide behind --
	// a step of colour, or over a clip a square of orange round every carved
	// tile -- so there each pixel takes the glows of its own tile and its
	// eight neighbours. A glow is 1.1 pumpkins from its centre, which is at
	// most 0.61 of a tile into the next, so the eight are all there can be;
	// the tile texture has a border of one tile all round for the ones off
	// the raster.
	if( Background == 0 || GlowClippedForTest == 1 )
	{
		if( face >= 0 )
			glow( acc, length( q ), lit );
	}
	else
		for( int dy = -1; dy <= 1; ++dy )
			for( int dx = -1; dx <= 1; ++dx )
			{
				ivec2 n  = tile + ivec2( dx, dy );
				vec4 ns  = texelFetch( Tiles, n - FirstTile, 0 );
				if( int( floor( ns.x + 0.5 ) ) < 0 )
					continue;
				vec2 nq = ( vec2( pix ) + 0.5 - vec2( Origin + n * TileSize ) - Centre ) / Size;
				glow( acc, length( nq ), ns.y );
			}

	for( int i = 0; i < BodyShapes.y; ++i )
		drawShape( BodyShapes.x + i, q, px, 0.0, lit, acc );
	if( face >= 0 )
		for( int i = 0; i < FaceShapes[ face ].y; ++i )
			drawShape( FaceShapes[ face ].x + i, q, px, e, lit, acc );

	FragColor = acc;
}
`;
