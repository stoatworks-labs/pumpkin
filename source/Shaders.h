#pragma once

#include <string>

/**
    The GLSL, as text.

    One pass. Each pixel finds its LED tile in integer arithmetic, reads that
    tile's state (its face, its candle, how far into a blink it is, its night)
    and paints the tile's jack-o'-lantern in the test card's order: the night,
    the shadow, the glow, the body, the cut walls, the candlelit holes. The
    lantern itself is not in the GLSL: it is the shape and vertex tables
    Geometry.cpp builds from the port, so the shader is a small painter and
    the drawing has one copy.

    Coverage is the box filter of a pixel over each edge, from a signed
    distance: a fill is clamp( 0.5 - d / px ), a stroke the overlap of the
    pixel's footprint with the band either side of its line. Compositing is
    premultiplied "over", in the stored (sRGB-encoded) values, as a canvas
    composites.

    `kCommon` is a LIBRARY, not a shader: no #version, no main. The pass is
    assembled as kVersion + kCommon + its piece, so the harness's checks run
    the text the plugin runs; tools/glslc.sh reassembles it for glslc.
*/
namespace pumpkin::shaders
{
extern const char* const kVersion;
extern const char* const kCommon;

extern const char* const kQuadVertex;
extern const char* const kPatchFragment;

/// kVersion + kCommon + the piece.
std::string Assemble( const char* piece );

} // namespace pumpkin::shaders
