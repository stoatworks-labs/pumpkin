/**
 * Pumpkin's GL half in WebGL2: what InitGL and the drawing end of
 * ProcessOpenGL do in source/Pumpkin.cpp, with the plugin's own shader.
 *
 * InitGL compiles one program (kVersion + kQuadVertex, and kVersion + kCommon
 * + kPatchFragment, from shaders.js, which demo/tools/check_shaders.py holds
 * to source/Shaders.cpp character for character) and uploads the lantern
 * once, as two RGBA32F textures: the shape table and the vertex table that
 * Geometry.cpp builds (here BuildTable() in port.js). Each frame the CPU half
 * (PumpkinCore.Frame in port.js) works out every tile's face, candle,
 * expression and night; this uploads them as a third RGBA32F texture, one
 * texel a tile, sets the uniforms exactly as ProcessOpenGL sets them, and
 * draws one full-frame quad.
 *
 * Kept apart from plugin.js so that demo/tools/crosscheck/render.html can
 * drive it at an exact time and read its pixels back, with nothing else of the
 * page around it.
 */

import { Program, bindTexture } from './vendor/gl.js';
import * as S from './shaders.js';
import {
  BuildTable, PumpkinCore, Vec3s, kTexelsPerShape, kVertexRow, kTileNights, kPageNight, kWall, kFlame, kHalo,
  kHaloAlpha, kFaceCount,
} from './port.js';

/** `makeFloatTexture`: RGBA32F, nearest, clamped. */
function makeFloatTexture(gl, width, height, texels) {
  const id = gl.createTexture();
  gl.bindTexture(gl.TEXTURE_2D, id);
  gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
  gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
  gl.pixelStorei(gl.UNPACK_PREMULTIPLY_ALPHA_WEBGL, false);
  gl.texImage2D(gl.TEXTURE_2D, 0, gl.RGBA32F, width, height, 0, gl.RGBA, gl.FLOAT, texels);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MIN_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_MAG_FILTER, gl.NEAREST);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_S, gl.CLAMP_TO_EDGE);
  gl.texParameteri(gl.TEXTURE_2D, gl.TEXTURE_WRAP_T, gl.CLAMP_TO_EDGE);
  gl.bindTexture(gl.TEXTURE_2D, null);
  return id;
}

export class PumpkinRenderer {
  constructor(gl, quad) {
    this.gl = gl;
    this.quad = quad;
    this.core = new PumpkinCore();

    // InitGL's two stages, joined as the plugin joins them.
    const vertex = S.VERSION + S.QUAD_VERTEX;
    const fragment = S.VERSION + S.COMMON + S.PATCH_FRAGMENT;
    this.program = new Program(gl, vertex, fragment, 'patch');

    // The lantern: built once, the same for every instance.
    const table = BuildTable();
    this.table = table;
    this.shapeTexture = makeFloatTexture(gl, kTexelsPerShape, table.shapes.length, table.shapeTexels);
    this.vertexTexture = makeFloatTexture(gl, kVertexRow, table.vertexRows, table.vertexTexels);
    this.tileTexture = null;
    this.tileCols = 0;
    this.tileRows = 0;

    // The constant uniforms, as ProcessOpenGL sets them every frame.
    this.faces = new Int32Array(kFaceCount * 2);
    for (let f = 0; f < kFaceCount; ++f) {
      this.faces[f * 2] = table.faceFirst[f];
      this.faces[f * 2 + 1] = table.faceCount[f];
    }
    this.nights = Vec3s(kTileNights, false);
    this.pageNight = Vec3s([kPageNight], false);
    this.wallColours = Vec3s(kWall, true);
    this.flameColours = Vec3s(kFlame, true);
    this.haloColours = Vec3s(kHalo, true);
  }

  /**
   * One frame into whatever framebuffer is bound, at width x height, its
   * viewport at the origin. `hostSeconds` is the host's clock; `wall` this
   * machine's, in seconds since 1970. Returns the CPU half's frame.
   */
  draw(width, height, hostSeconds, wall) {
    const gl = this.gl;
    const frame = this.core.Frame(width, height, hostSeconds, wall);

    if (this.tileTexture === null || this.tileCols !== frame.cols || this.tileRows !== frame.rows) {
      if (this.tileTexture) gl.deleteTexture(this.tileTexture);
      this.tileTexture = makeFloatTexture(gl, frame.cols, frame.rows, frame.texels);
      this.tileCols = frame.cols;
      this.tileRows = frame.rows;
    } else {
      gl.bindTexture(gl.TEXTURE_2D, this.tileTexture);
      gl.pixelStorei(gl.UNPACK_ALIGNMENT, 4);
      gl.pixelStorei(gl.UNPACK_FLIP_Y_WEBGL, false);
      gl.texSubImage2D(gl.TEXTURE_2D, 0, 0, 0, frame.cols, frame.rows, gl.RGBA, gl.FLOAT, frame.texels);
      gl.bindTexture(gl.TEXTURE_2D, null);
    }

    const p = this.program;
    const u = frame.uniforms;
    const loc = (name) => p.location(name);
    gl.disable(gl.BLEND);
    gl.viewport(0, 0, width, height);
    p.use();
    bindTexture(gl, 0, this.tileTexture);
    bindTexture(gl, 1, this.shapeTexture);
    bindTexture(gl, 2, this.vertexTexture);
    p.setSampler('Tiles', 0);
    p.setSampler('Shapes', 1);
    p.setSampler('Vertices', 2);
    gl.uniform2i(loc('Raster'), u.Raster[0], u.Raster[1]);
    gl.uniform2i(loc('ViewOrigin'), 0, 0);
    gl.uniform2i(loc('TileSize'), u.TileSize[0], u.TileSize[1]);
    gl.uniform2i(loc('Origin'), u.Origin[0], u.Origin[1]);
    gl.uniform2i(loc('FirstTile'), u.FirstTile[0], u.FirstTile[1]);
    gl.uniform1f(loc('Size'), u.Size);
    gl.uniform2f(loc('Centre'), u.Centre[0], u.Centre[1]);
    gl.uniform2i(loc('BodyShapes'), this.table.bodyFirst, this.table.bodyCount);
    gl.uniform2iv(loc('FaceShapes'), this.faces);
    gl.uniform1i(loc('Background'), u.Background);
    gl.uniform3fv(loc('Nights'), this.nights);
    gl.uniform3fv(loc('PageNight'), this.pageNight);
    gl.uniform3fv(loc('WallColours'), this.wallColours);
    gl.uniform3fv(loc('FlameColours'), this.flameColours);
    gl.uniform3fv(loc('HaloColours'), this.haloColours);
    gl.uniform1f(loc('HaloAlpha'), kHaloAlpha);
    gl.uniform1i(loc('GlowClippedForTest'), 0);
    this.quad.draw();
    for (let unit = 0; unit < 3; ++unit) bindTexture(gl, unit, null);
    gl.activeTexture(gl.TEXTURE0);
    return frame;
  }

  /** Uniform names the program does not have: none, or a control is dead. */
  missingUniforms() {
    const names = ['Tiles', 'Shapes', 'Vertices', 'Raster', 'ViewOrigin', 'TileSize', 'Origin', 'FirstTile', 'Size',
      'Centre', 'BodyShapes', 'FaceShapes', 'Background', 'Nights', 'PageNight', 'WallColours', 'FlameColours',
      'HaloColours', 'HaloAlpha', 'GlowClippedForTest'];
    return names.filter((name) => this.program.location(name) === null);
  }

  dispose() {
    const gl = this.gl;
    for (const t of [this.tileTexture, this.shapeTexture, this.vertexTexture]) if (t) gl.deleteTexture(t);
    this.program.dispose();
  }
}
