/**
 * Pumpkin's CPU half, ported to JavaScript.
 *
 * Everything the plugin works out on the CPU before its one shader pass, in
 * the plugin's own order and arithmetic:
 *
 *   Random.h       the test card's hash and mulberry32, bit for bit
 *   Lantern.cpp    the jack-o'-lantern (body, six faces, their expressions),
 *                  which face goes on which tile, each tile's night, and each
 *                  candle's flicker and each face's blink in time
 *   Geometry.cpp   the shape and vertex tables the shader paints from
 *   Controls.cpp   the parameters: names, groups, ranges, conversions
 *   Pumpkin.cpp    the constructor's declarations and defaults, and the CPU
 *                  half of ProcessOpenGL: the clock, the deal, each tile's
 *                  candle and expression into the tile texture, the uniforms
 *
 * No GL and no DOM here, so `demo/tools/crosscheck/` can run this very file
 * under node: against the test card's own numbers (the fixture the plugin's
 * `pktest --port` is held to), and against the plugin's C++ itself (the
 * tables, bit for bit; the declarations against `pktest --list`).
 *
 * **This is a port, and the plugin's C++ is itself a port.** The plugin
 * carries the Test Card's TypeScript (`src/patterns/halloween.ts`,
 * `art/halloween.ts`, `random.ts`) into C++ line for line; this carries the
 * C++ back into JavaScript, line for line, keeping the C++'s structure so the
 * two read side by side. JavaScript's numbers are IEEE doubles, as the C++'s
 * are, and the plugin core is built with `-ffp-contract=off`, so the same
 * operations in the same order give the same bits. The one thing that can
 * differ is the maths library: V8's Math.sin, cos, hypot, pow and log against
 * Apple's libm, by an ulp now and then. The cross-check measures that rather
 * than assuming it away.
 *
 * Float32 where the C++ has float: `Math.fround` at every point the C++ casts
 * or stores one, so a texel here is the texel the plugin uploads.
 */

const F = Math.fround;
const kPi = 3.14159265358979323846;
const kTau = kPi * 2.0;

const clamp01 = (v) => Math.max(0.0, Math.min(1.0, v));
const clamp = (v, lo, hi) => Math.min(hi, Math.max(lo, v));

/** std::lround: half away from zero. */
function lround(v) {
  return v < 0 ? -Math.round(-v) : Math.round(v);
}

//===========================================================================
// Random.h -- the test card's random.ts.
//===========================================================================

/** `Math.floor( v ) >>> 0`: ToUint32, which wraps negatives modulo 2^32. */
export function ToUint32(v) {
  let m = Math.floor(v) % 4294967296.0;
  if (m < 0.0) m += 4294967296.0;
  return m >>> 0;
}

/** `hash( ...values )`: FNV-1a's offset and prime, with a shift-xor per value. */
export function Hash(...values) {
  let h = 0x811c9dc5;
  for (const v of values) {
    h = (h ^ ToUint32(v)) >>> 0;
    h = Math.imul(h, 0x01000193) >>> 0;
    h = (h ^ (h >>> 13)) >>> 0;
  }
  return h;
}

/** `rng( seed )`: mulberry32, 0..1. */
export class Rng {
  constructor(seed) {
    this.a = seed >>> 0;
  }

  next() {
    this.a = (this.a + 0x6d2b79f5) >>> 0;
    let t = this.a;
    t = Math.imul(t ^ (t >>> 15), t | 1) >>> 0;
    t = (t ^ (t + Math.imul(t ^ (t >>> 7), t | 61))) >>> 0;
    return ((t ^ (t >>> 14)) >>> 0) / 4294967296.0;
  }

  /** `between( r, lo, hi )`. */
  Between(lo, hi) {
    return lo + (hi - lo) * this.next();
  }
}

//===========================================================================
// Lantern.cpp -- the test card's jack-o'-lantern.
//
// Units are the test card's: a pumpkin one unit wide, centred on (0, 0), y
// DOWN. A Shape is the C++ struct: points, smooth, closed, fill, alpha,
// stroke, and the ellipse it was made from, if it was.
//===========================================================================

function shape(fields) {
  return {
    pts: [],
    smooth: true,
    closed: true,
    hasFill: false,
    fill: 0,
    alpha: 1.0,
    hasStroke: false,
    stroke: 0,
    strokeWidth: 0.0,
    isEllipse: false,
    ellipse: { cx: 0.0, cy: 0.0, rx: 0.0, ry: 0.0, rotation: 0.0, n: 0 },
    ...fields,
  };
}

/** A copy, as `Shape h = wall;` copies (its own points and ellipse). */
function copyShape(s) {
  return { ...s, pts: s.pts.map((p) => ({ x: p.x, y: p.y })), ellipse: { ...s.ellipse } };
}

const P = (x, y) => ({ x, y });
const pts = (list) => list.map(([x, y]) => P(x, y));

function centroid(list) {
  let x = 0.0;
  let y = 0.0;
  for (const p of list) {
    x += p.x;
    y += p.y;
  }
  return P(x / list.length, y / list.length);
}

function scaleAbout(list, kx, ky, about = centroid(list)) {
  return list.map((p) => P(about.x + (p.x - about.x) * kx, about.y + (p.y - about.y) * ky));
}

function mirrorPts(list) {
  return list.map((p) => P(-p.x, p.y));
}

function lerp(a, b, t) {
  return P(a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t);
}

function rotate(p, angle, about) {
  const c = Math.cos(angle);
  const s = Math.sin(angle);
  const x = p.x - about.x;
  const y = p.y - about.y;
  return P(about.x + x * c - y * s, about.y + x * s + y * c);
}

function ellipsePts(e) {
  const out = [];
  for (let i = 0; i < e.n; i++) {
    const a = (i / e.n) * kPi * 2.0;
    out.push(rotate(P(e.cx + Math.cos(a) * e.rx, e.cy + Math.sin(a) * e.ry), e.rotation, P(e.cx, e.cy)));
  }
  return out;
}

function ellipseShape(cx, cy, rx, ry, rotation, n) {
  const ellipse = { cx, cy, rx, ry, rotation, n };
  return shape({ isEllipse: true, ellipse, pts: ellipsePts(ellipse) });
}

const sharp = (list) => shape({ pts: list, smooth: false });
/** `round( pts )`, or `round( ellipse )`: smoothed. */
const round = (x) => (Array.isArray(x) ? shape({ pts: x, smooth: true }) : { ...x, smooth: true });

//---------------------------------------------------------------------------
// The body.
//---------------------------------------------------------------------------
const kRibs = [
  [-0.3, 0.02, 0.2, 0.32, 0xa3420b],
  [0.3, 0.02, 0.2, 0.32, 0xa3420b],
  [-0.165, 0.005, 0.215, 0.355, 0xc0540f],
  [0.165, 0.005, 0.215, 0.355, 0xc0540f],
  [0.0, 0.0, 0.2, 0.37, 0xd66717],
];

function pumpkinBody() {
  const out = [];
  for (const [cx, cy, rx, ry, fill] of kRibs) {
    const s = ellipseShape(cx, cy, rx, ry, 0.0, 40);
    s.hasFill = true;
    s.fill = fill;
    s.hasStroke = true;
    s.stroke = 0x6a2805;
    s.strokeWidth = 0.012;
    out.push(s);
  }
  {
    const s = ellipseShape(-0.07, -0.2, 0.05, 0.09, 0.3, 20);
    s.hasFill = true;
    s.fill = 0xf59a45;
    s.alpha = 0.3;
    out.push(s);
  }
  out.push(shape({
    pts: pts([[-0.045, -0.33], [0.04, -0.335], [0.06, -0.42], [0.1, -0.5], [0.065, -0.52], [0.02, -0.455], [-0.03, -0.41]]),
    hasFill: true,
    fill: 0x5d5a24,
  }));
  {
    const s = ellipseShape(0.083, -0.507, 0.022, 0.012, 0.5, 12);
    s.hasFill = true;
    s.fill = 0x9a9450;
    out.push(s);
  }
  out.push(shape({
    pts: pts([[-0.04, -0.35], [-0.1, -0.42], [-0.17, -0.41], [-0.16, -0.36], [-0.1, -0.33]]),
    hasFill: true,
    fill: 0x3d7428,
  }));
  out.push(shape({
    pts: pts([[0.04, -0.36], [0.12, -0.4], [0.17, -0.36], [0.15, -0.31], [0.11, -0.33], [0.13, -0.36]]),
    hasStroke: true,
    stroke: 0x4a7326,
    strokeWidth: 0.014,
    closed: false,
  }));
  return out;
}

//---------------------------------------------------------------------------
// The faces.
//---------------------------------------------------------------------------

/** A mouth of interlocking zig-zag teeth, eight a side, between two curves. */
function zigzagMouth(open) {
  const n = 8;
  const half = 0.3;
  const c = (x) => {
    const u = x / half;
    return Math.max(0.0, 1.0 - u * u);
  };
  const tooth = (x) => 0.045 * Math.sqrt(c(x));
  const top = [];
  const bottom = [];
  for (let i = 0; i <= 2 * n; i++) {
    const x = -half + (2 * half * i) / (2 * n);
    top.push(P(x, 0.11 + 0.06 * c(x) + (i % 2 ? tooth(x) : 0.0)));
  }
  for (let i = 0; i < 2 * n; i++) {
    const x = half - (2 * half * (i + 0.5)) / (2 * n);
    bottom.push(P(x, 0.11 + (0.17 + 0.04 * open) * c(x) - (i % 2 ? tooth(x) : 0.0)));
  }
  return top.concat(bottom);
}

/** A lens between two points, bulging `up` on one side and `down` on the other. */
function lens(a, b, up, down, n = 10) {
  const d = P(b.x - a.x, b.y - a.y);
  let l = Math.hypot(d.x, d.y);
  if (l === 0.0) l = 1.0;
  // The normal that points up the picture (y down, so negative y).
  let nrm = P(-d.y / l, d.x / l);
  if (nrm.y > 0) nrm = P(-nrm.x, -nrm.y);
  const out = [];
  for (let k = 0; k <= n; k++) {
    const p = lerp(a, b, k / n);
    const s = Math.sin((kPi * k) / n);
    out.push(P(p.x + nrm.x * up * s, p.y + nrm.y * up * s));
  }
  for (let k = n - 1; k > 0; k--) {
    const p = lerp(a, b, k / n);
    const s = Math.sin((kPi * k) / n);
    out.push(P(p.x - nrm.x * down * s, p.y - nrm.y * down * s));
  }
  return out;
}

export const FACE = { Classic: 0, Fierce: 1, Happy: 2, Surprised: 3, Sly: 4, Goofy: 5 };
export const kFaceCount = 6;
const FACE_NAMES = ['Classic', 'Fierce', 'Happy', 'Surprised', 'Sly', 'Goofy'];
export function FaceName(face) {
  return FACE_NAMES[face] ?? '?';
}

function faceHoles(face, e) {
  switch (face) {
    case FACE.Classic: {
      const eye = pts([[-0.25, -0.02], [-0.155, -0.19], [-0.06, -0.02]]);
      const mouth = pts([
        [-0.3, 0.07], [-0.12, 0.15], [-0.12, 0.22], [-0.04, 0.22], [-0.04, 0.16], [0.3, 0.07],
        [0.24, 0.17], [0.13, 0.25], [0.13, 0.19], [0.05, 0.19], [0.05, 0.27], [-0.1, 0.28], [-0.24, 0.18],
      ]);
      // The jaw drops from the top lip: it chomps.
      return [sharp(eye), sharp(mirrorPts(eye)), sharp(pts([[-0.045, 0.075], [0, 0.005], [0.045, 0.075]])),
        sharp(scaleAbout(mouth, 1, 1 + 0.3 * e, P(0, 0.08)))];
    }
    case FACE.Fierce: {
      const eye = pts([[-0.27, -0.14], [-0.05, -0.05], [-0.08, 0.0], [-0.2, 0.0], [-0.26, -0.06]]);
      const squint = scaleAbout(eye, 1, 1 - 0.5 * e);
      return [sharp(squint), sharp(mirrorPts(squint)), sharp(pts([[-0.035, 0.06], [0, 0.0], [0.035, 0.06]])),
        sharp(zigzagMouth(e))];
    }
    case FACE.Happy: {
      const eye = (x) => ellipseShape(x, -0.08, 0.07, 0.07 * (1 - 0.85 * e), 0, 20);
      const top = [];
      const bottom = [];
      for (let i = 0; i <= 12; i++) {
        const x = -0.27 + (0.54 * i) / 12;
        const u = x / 0.27;
        const c = 1 - u * u;
        top.push(P(x, 0.09 + 0.09 * c));
        bottom.push(P(x, 0.09 + 0.2 * c));
      }
      bottom.reverse();
      return [round(eye(-0.15)), round(eye(0.15)), sharp(pts([[-0.035, 0.05], [0, -0.005], [0.035, 0.05]])),
        round(top.concat(bottom))];
    }
    case FACE.Surprised: {
      const g = 1 + 0.12 * e;
      return [
        round(ellipseShape(-0.14, -0.09, 0.055 * g, 0.085 * g, 0, 20)),
        round(ellipseShape(0.14, -0.09, 0.055 * g, 0.085 * g, 0, 20)),
        sharp(pts([[-0.025, 0.05], [0, 0.01], [0.025, 0.05]])),
        round(ellipseShape(0, 0.2, 0.075 * (1 + 0.25 * e), 0.095 * (1 + 0.3 * e), 0, 24)),
      ];
    }
    case FACE.Sly: {
      // Cat's eyes, outer corners up, narrowing to slits.
      const left = lens(P(-0.27, -0.12), P(-0.07, -0.04), 0.055 * (1 - 0.7 * e), 0.04 * (1 - 0.7 * e));
      return [
        sharp(left),
        sharp(mirrorPts(left)),
        sharp(pts([[-0.03, 0.06], [0, 0.02], [0.03, 0.06]])),
        sharp(pts([
          [-0.26, 0.12], [-0.12, 0.17], [-0.09, 0.17], [-0.075, 0.24], [-0.06, 0.17], [0.06, 0.17],
          [0.075, 0.24], [0.09, 0.17], [0.2, 0.14], [0.28, 0.08],
          [0.22, 0.17], [0.1, 0.27], [0, 0.29], [-0.12, 0.27], [-0.22, 0.19],
        ])),
      ];
    }
    case FACE.Goofy:
    default:
      // One big eye, which winks; one small one, which does not.
      return [
        round(ellipseShape(-0.15, -0.1, 0.09, 0.09 * (1 - 0.88 * e), 0, 22)),
        round(ellipseShape(0.16, -0.06, 0.045, 0.05, 0, 16)),
        round(ellipseShape(0, 0.04, 0.03, 0.03, 0, 12)),
        sharp(pts([
          [-0.28, 0.1], [-0.02, 0.15], [-0.02, 0.21], [0.05, 0.21], [0.05, 0.15], [0.24, 0.12],
          [0.2, 0.2], [0.05, 0.29], [-0.15, 0.27], [-0.25, 0.18],
        ])),
      ];
  }
}

/** A carved hole: the wall it cuts, a little smaller and higher. */
function holeOf(wall) {
  const h = copyShape(wall);
  const c = centroid(wall.pts);
  h.pts = scaleAbout(wall.pts, 0.88, 0.8, c);
  for (const p of h.pts) p.y -= 0.01;
  if (h.isEllipse) {
    h.ellipse.cx = c.x + (wall.ellipse.cx - c.x) * 0.88;
    h.ellipse.cy = c.y + (wall.ellipse.cy - c.y) * 0.8 - 0.01;
    h.ellipse.rx *= 0.88;
    h.ellipse.ry *= 0.8;
  }
  return h;
}

/** `pumpkin( face, expression )`. `face` -1 is a plain, uncarved pumpkin. */
export function MakeLantern(face, expression) {
  const out = { body: pumpkinBody(), walls: [], holes: [] };
  if (face < 0 || face >= kFaceCount) return out;
  out.walls = faceHoles(face, clamp01(expression));
  out.holes = out.walls.map(holeOf);
  return out;
}

/** `PUMPKIN_TOP` and `PUMPKIN_BOTTOM`: the extent the painter sizes by. */
export const kPumpkinTop = -0.52;
export const kPumpkinBottom = 0.37;

//---------------------------------------------------------------------------
// The patch.
//---------------------------------------------------------------------------

/** `tileKey( seed, tile, env )` on a spanned surface (localKey 0). */
export function TileKey(seed, x, y) {
  // Math.round: half up.
  return Hash(seed, Math.floor(x + 0.5), Math.floor(y + 0.5), 0.0);
}

/** `tileFace`: the face carved on a tile, or -1 when no face is ticked. */
export function TileFace(key, faceMask) {
  const faces = [];
  for (let f = 0; f < kFaceCount; ++f) if (faceMask & (1 << f)) faces.push(f);
  if (faces.length === 0) return -1;
  const r = new Rng(key);
  return faces[Math.min(faces.length - 1, Math.floor(r.next() * faces.length))];
}

export const kLoopSeconds = 10.0;

/** `phase( t, loopSeconds, cycles )`. */
function phase(t, loopSeconds, cycles) {
  const p = ((t / loopSeconds) * cycles) % 1.0;
  return p < 0 ? p + 1 : p;
}

/** `cyclesPerLoop( natural, true )`: endless, nothing is rounded. */
function endlessCycles(natural) {
  return Math.max(natural, Number.MIN_VALUE);
}

const kCandleHz = [1.3, 3.1, 5.3];
const kCandleAmp = [0.13, 0.09, 0.05];

/** A tile's candle and expression timing, everything from its key drawn once. */
export class TileClock {
  constructor() {
    this.candleCycles = [0.0, 0.0, 0.0];
    this.candleOffset = [0.0, 0.0, 0.0];
    this.goes = 0.0;
    this.expressionOffset = 0.0;
  }

  static For(key) {
    const c = new TileClock();
    {
      const r = new Rng(Hash(key, 501.0));
      for (let i = 0; i < 3; ++i) {
        c.candleCycles[i] = endlessCycles(kLoopSeconds * kCandleHz[i] * r.Between(0.85, 1.15));
        c.candleOffset[i] = r.next();
      }
    }
    {
      const r = new Rng(Hash(key, 502.0));
      c.goes = endlessCycles(kLoopSeconds / r.Between(2.5, 4));
      c.expressionOffset = r.next();
    }
    return c;
  }

  /** A candle's brightness 0..1 at t (`candleLevel`, endless). */
  Candle(t) {
    let v = 0.72;
    for (let i = 0; i < 3; ++i) {
      v += kCandleAmp[i] * Math.sin(kTau * phase(t, kLoopSeconds, this.candleCycles[i]) + this.candleOffset[i] * kTau);
    }
    return clamp01(v);
  }

  /** How far into a blink, wink or chomp a face is, 0..1 (`expressionLevel`). */
  Expression(t) {
    const p = (phase(t, kLoopSeconds, this.goes) + this.expressionOffset) % 1.0;
    const len = 0.14;
    return p < len ? Math.sin((kPi * p) / len) : 0.0;
  }
}

export const CandleLevel = (t, key) => TileClock.For(key).Candle(t);
export const ExpressionLevel = (t, key) => TileClock.For(key).Expression(t);

/** `TILE_NIGHTS`: each tile's own night sky, by `mod( col + 2 row, 6 )`. */
export const kTileNights = [0x1b1230, 0x10201c, 0x24122a, 0x111a2c, 0x20160e, 0x170f22];
/** The page behind the tiles. */
export const kPageNight = 0x0b0710;

export function NightIndex(col, row) {
  const m = (col + row * 2) % 6;
  return m < 0 ? m + 6 : m;
}

//===========================================================================
// Geometry.cpp -- the lantern as the shader draws it: a table of shapes and
// a table of vertices, each vertex at expression 0 and at 1.
//===========================================================================

export const KIND = { Ellipse: 0, Polygon: 1, Polyline: 2 };
export const PAINT = { Solid: 0, Wall: 1, Flame: 2 };
export const kTexelsPerShape = 8;
export const kVertexRow = 256;
const kFlattenSizePx = 4096.0;
const kFlattenTolerancePx = 0.1;

const mid = (a, b) => P(a.x + (b.x - a.x) * 0.5, a.y + (b.y - a.y) * 0.5);

function quadratic(p0, p1, p2, u) {
  const a = (1 - u) * (1 - u);
  const b = 2 * u * (1 - u);
  const c = u * u;
  return P(a * p0.x + b * p1.x + c * p2.x, a * p0.y + b * p1.y + c * p2.y);
}

/** The i-th quadratic's three control points, as `tracePath` lays them. */
function controls(s, i) {
  const p = s.pts;
  const n = p.length;
  if (s.closed) {
    return [mid(p[(i + n - 1) % n], p[i]), p[i], mid(p[i], p[(i + 1) % n])];
  }
  // moveTo p0, then quadraticCurveTo( p[ i ], mid( p[ i ], p[ i + 1 ] ) )
  // for i = 1 .. n - 2, then a line to the last point.
  return [i === 0 ? p[0] : mid(p[i], p[i + 1]), p[i + 1], mid(p[i + 1], p[i + 2])];
}

const smoothed = (s) => s.smooth && s.pts.length >= 3;

// std::min and std::max, which keep the FIRST argument on a tie, where
// Math.min( 0, -0 ) is -0: a table texel is compared bit for bit.
const stdMin = (a, b) => (b < a ? b : a);
const stdMax = (a, b) => (a < b ? b : a);

function growBox(box, x, y) {
  box[0] = stdMin(box[0], F(x));
  box[1] = stdMin(box[1], F(y));
  box[2] = stdMax(box[2], F(x));
  box[3] = stdMax(box[3], F(y));
}

function ellipseOf(e) {
  const k = SmoothedRadius(e.n);
  return [F(e.cx), F(e.cy), F(e.rx * k), F(e.ry * k)];
}

function rgb(hex) {
  return [F(((hex >> 16) & 0xff) / 255), F(((hex >> 8) & 0xff) / 255), F((hex & 0xff) / 255)];
}

/** One shape, from the same shape at expression 0 and at 1. */
function render(a, b, paint) {
  const r = {
    kind: KIND.Polygon,
    paint,
    fill: [0, 0, 0, 0],
    stroke: [0, 0, 0, 0],
    strokeAlpha: 0,
    box: [F(1e9), F(1e9), F(-1e9), F(-1e9)],
    ellipse0: [0, 0, 0, 0],
    ellipse1: [0, 0, 0, 0],
    cosR: 1,
    sinR: 0,
    vertices: [],
  };
  if (a.hasFill || paint !== PAINT.Solid) {
    r.fill = [...rgb(a.fill), F(a.alpha)];
  }
  if (a.hasStroke) {
    r.stroke = [...rgb(a.stroke), F(a.strokeWidth)];
    r.strokeAlpha = F(a.alpha);
  }

  if (a.isEllipse && smoothed(a)) {
    r.kind = KIND.Ellipse;
    r.ellipse0 = ellipseOf(a.ellipse);
    r.ellipse1 = ellipseOf(b.ellipse);
    r.cosR = F(Math.cos(a.ellipse.rotation));
    r.sinR = F(Math.sin(a.ellipse.rotation));
    for (const e of [r.ellipse0, r.ellipse1]) {
      // The rotated ellipse's half-extents along x and y. The C++ hands
      // std::hypot two floats, which is the float overload: hypotf.
      const hx = F(Math.hypot(F(e[2] * r.cosR), F(e[3] * r.sinR)));
      const hy = F(Math.hypot(F(e[2] * r.sinR), F(e[3] * r.cosR)));
      growBox(r.box, e[0] - hx, e[1] - hy);
      growBox(r.box, e[0] + hx, e[1] + hy);
    }
  } else {
    r.kind = a.closed ? KIND.Polygon : KIND.Polyline;
    let va = a.pts;
    let vb = b.pts;
    if (smoothed(a)) {
      const steps = StepsFor(a, b);
      va = Flatten(a, steps);
      vb = Flatten(b, steps);
    }
    for (let i = 0; i < va.length; ++i) {
      r.vertices.push(F(va[i].x), F(va[i].y), F(vb[i].x), F(vb[i].y));
      growBox(r.box, va[i].x, va[i].y);
      growBox(r.box, vb[i].x, vb[i].y);
    }
  }
  const pad = F(r.stroke[3] * 0.5);
  r.box[0] = F(r.box[0] - pad);
  r.box[1] = F(r.box[1] - pad);
  r.box[2] = F(r.box[2] + pad);
  r.box[3] = F(r.box[3] + pad);
  return r;
}

/** The radius of a smoothed regular n-gon, as a fraction of the n-gon's. */
export function SmoothedRadius(n) {
  const s = Math.sin(kPi / n);
  return (Math.cos(kPi / n) + 1.0 - 0.5 * s * s) * 0.5;
}

export function QuadraticCount(s) {
  const n = s.pts.length;
  return s.closed ? n : Math.max(0, n - 2);
}

/** Pieces per quadratic for the error bound, the larger of two expressions. */
export function StepsFor(a, b) {
  // A quadratic cut into N equal pieces of its parameter strays from each
  // chord by at most |P0 - 2 P1 + P2| / ( 4 N^2 ).
  const steps = [];
  for (let i = 0; i < QuadraticCount(a); ++i) {
    let bend = 0.0;
    for (const s of [a, b]) {
      const [p0, p1, p2] = controls(s, i);
      bend = Math.max(bend, Math.hypot(p0.x - 2 * p1.x + p2.x, p0.y - 2 * p1.y + p2.y));
    }
    const n = Math.sqrt((bend * kFlattenSizePx) / (4.0 * kFlattenTolerancePx));
    steps.push(clamp(Math.ceil(n), 1, 64));
  }
  return steps;
}

export function Flatten(s, steps) {
  const out = [];
  const count = QuadraticCount(s);
  for (let i = 0; i < count; ++i) {
    const [p0, p1, p2] = controls(s, i);
    if (i === 0) out.push(p0);
    const n = steps[i];
    for (let k = 1; k <= n; ++k) out.push(quadratic(p0, p1, p2, k / n));
  }
  if (s.closed) out.pop(); // the last quadratic ends where the first began
  else out.push(s.pts[s.pts.length - 1]);
  return out;
}

let builtTable = null;

/** `geometry::Build()`: built once, on first use. */
export function BuildTable() {
  if (builtTable) return builtTable;
  const t = { shapes: [], bodyFirst: 0, bodyCount: 0, faceFirst: [], faceCount: [] };
  const plain = MakeLantern(-1, 0.0);
  for (const s of plain.body) t.shapes.push(render(s, s, PAINT.Solid));
  t.bodyCount = t.shapes.length;
  for (let f = 0; f < kFaceCount; ++f) {
    const rest = MakeLantern(f, 0.0);
    const height = MakeLantern(f, 1.0);
    t.faceFirst[f] = t.shapes.length;
    // All the walls, then all the holes: the canvas fills them in that order.
    for (let i = 0; i < rest.walls.length; ++i) t.shapes.push(render(rest.walls[i], height.walls[i], PAINT.Wall));
    for (let i = 0; i < rest.holes.length; ++i) t.shapes.push(render(rest.holes[i], height.holes[i], PAINT.Flame));
    t.faceCount[f] = t.shapes.length - t.faceFirst[f];
  }

  t.vertexCount = t.shapes.reduce((n, s) => n + s.vertices.length / 4, 0);
  t.vertexRows = Math.max(1, Math.ceil(t.vertexCount / kVertexRow));

  // Table::ShapeTexels: kTexelsPerShape RGBA texels a shape, one shape a row.
  t.shapeTexels = new Float32Array(kTexelsPerShape * 4 * t.shapes.length);
  let first = 0;
  t.shapes.forEach((s, i) => {
    const count = s.vertices.length / 4;
    const head = [s.kind, first, count, s.paint];
    const extra = [s.cosR, s.sinR, s.strokeAlpha, 0];
    [head, s.fill, s.stroke, s.box, s.ellipse0, s.ellipse1, extra].forEach((texel, k) => {
      t.shapeTexels.set(texel, i * kTexelsPerShape * 4 + k * 4);
    });
    first += count;
  });

  // Table::VertexTexels: kVertexRow x VertexRows, zero-padded.
  t.vertexTexels = new Float32Array(kVertexRow * t.vertexRows * 4);
  let at = 0;
  for (const s of t.shapes) {
    t.vertexTexels.set(s.vertices, at);
    at += s.vertices.length;
  }
  builtTable = t;
  return t;
}

//===========================================================================
// Controls.h / Controls.cpp -- the parameters, by what they are.
//===========================================================================

export const PT = {
  TILE_WIDTH: 0,
  TILE_HEIGHT: 1,
  ORIGIN_X: 2,
  ORIGIN_Y: 3,
  SEED: 4,
  FACE_FIRST: 5,
  FACE_LAST: 10,
  FLICKER: 11,
  EXPRESSIONS: 12,
  SPEED: 13,
  CLOCK: 14,
  BACKGROUND: 15,
  ABOUT_TEXT: 16,
};

export const CLOCK = { Resolume: 0, Wall: 1, Count: 2 };
export const CLOCK_NAMES = ['Resolume', 'Wall'];
export const BACKGROUND = { Night: 0, Dark: 1, None: 2, Count: 3 };
export const BACKGROUND_NAMES = ['Night', 'Dark', 'None'];

export const kMinTile = 16;
export const kMaxTile = 4096;
export const kMaxOrigin = 4096;
export const kMaxSeed = 9999;
export const kDefaultTile = 168;
/** The test card's wall clock starts at 2026-01-01T00:00:00Z (`ENDLESS_EPOCH_S`). */
export const kEndlessEpochSeconds = 1767225600.0;

const kSpeedLow = 0.25;
const kSpeedHigh = 4.0;

export function GroupOf(id) {
  if (id <= PT.SEED) return 'Patch';
  if (id <= PT.FACE_LAST) return 'Faces';
  if (id <= PT.CLOCK) return 'Candles';
  if (id <= PT.BACKGROUND) return 'Look';
  return 'About';
}

export function NameOf(id) {
  if (id >= PT.FACE_FIRST && id <= PT.FACE_LAST) return FaceName(id - PT.FACE_FIRST);
  return {
    [PT.TILE_WIDTH]: 'Tile Width',
    [PT.TILE_HEIGHT]: 'Tile Height',
    [PT.ORIGIN_X]: 'Origin X',
    [PT.ORIGIN_Y]: 'Origin Y',
    [PT.SEED]: 'Seed',
    [PT.FLICKER]: 'Flicker',
    [PT.EXPRESSIONS]: 'Expressions',
    [PT.SPEED]: 'Speed',
    [PT.CLOCK]: 'Clock',
    [PT.BACKGROUND]: 'Background',
  }[id] ?? '?';
}

/** 0 at the bottom (still), then a quarter to four times the test card's. */
export function SpeedFromParam(v) {
  // Exactly zero at the bottom: every candle and face stands still.
  if (v <= 0.0) return 0.0;
  return kSpeedLow * Math.pow(kSpeedHigh / kSpeedLow, clamp(v, 0.0, 1.0));
}

export function ParamFromSpeed(speed) {
  if (speed <= 0.0) return F(0.0);
  const s = clamp(speed, kSpeedLow, kSpeedHigh);
  return F(Math.log(s / kSpeedLow) / Math.log(kSpeedHigh / kSpeedLow));
}

export function OptionIndex(value, count) {
  return clamp(lround(value), 0, count - 1);
}

export function IntegerParam(value, lo, hi) {
  return clamp(lround(value), lo, hi);
}

/**
 * The constructor's declarations, in host order, About block left off: name,
 * group, FFGL type, the default the constructor sets (a host float), and the
 * range or the element list. What PumpkinPlugin::PumpkinPlugin() declares.
 */
export function Declarations() {
  const out = [];
  for (let id = 0; id < PT.ABOUT_TEXT; ++id) {
    const d = { id, name: NameOf(id), group: GroupOf(id) };
    switch (id) {
      case PT.TILE_WIDTH:
      case PT.TILE_HEIGHT:
        Object.assign(d, { type: 'integer', default: kDefaultTile, min: kMinTile, max: kMaxTile });
        break;
      case PT.ORIGIN_X:
      case PT.ORIGIN_Y:
        Object.assign(d, { type: 'integer', default: 0, min: -kMaxOrigin, max: kMaxOrigin });
        break;
      case PT.SEED:
        Object.assign(d, { type: 'integer', default: 1, min: 0, max: kMaxSeed });
        break;
      case PT.FLICKER:
      case PT.EXPRESSIONS:
        Object.assign(d, { type: 'boolean', default: 1 });
        break;
      case PT.SPEED:
        Object.assign(d, { type: 'standard', default: ParamFromSpeed(1.0) });
        break;
      case PT.CLOCK:
        Object.assign(d, { type: 'option', default: CLOCK.Resolume, elements: CLOCK_NAMES });
        break;
      case PT.BACKGROUND:
        Object.assign(d, { type: 'option', default: BACKGROUND.Night, elements: BACKGROUND_NAMES });
        break;
      default: // the faces
        Object.assign(d, { type: 'boolean', default: 1 });
        break;
    }
    out.push(d);
  }
  return out;
}

//===========================================================================
// Pumpkin.cpp -- the CPU half of ProcessOpenGL.
//===========================================================================

//The test card's colours (halloween.ts, drawLantern).
export const kWall = [0x7a3a10, 0xffd27a];
export const kFlame = [0xc86a10, 0xa84a08, 0x6a2000, 0xfff4b8, 0xffc23a, 0xff7212];
export const kHalo = [0xff9632, 0xff781e]; //rgba( 255, 150, 50 ), rgba( 255, 120, 30 )
export const kHaloAlpha = F(0.42);

const kMaxFrameDelta = 1.0;

function floorDiv(a, b) {
  return a >= 0 ? Math.trunc(a / b) : -Math.trunc((-a - 1) / b) - 1;
}

/** `setVec3s`: 0xRRGGBB as code values (0..255) or as 0..1 floats. */
export function Vec3s(hexes, codeValues) {
  const v = new Float32Array(hexes.length * 3);
  hexes.forEach((hex, i) => {
    for (let c = 0; c < 3; ++c) {
      const code = (hex >>> (16 - 8 * c)) & 0xff;
      v[i * 3 + c] = codeValues ? code : F(code / 255);
    }
  });
  return v;
}

/**
 * One instance's CPU state: its parameters (host floats, as `params[]`), its
 * clock and its dealt patch. `Frame()` is ProcessOpenGL up to the draw.
 */
export class PumpkinCore {
  constructor() {
    this.params = new Float32Array(PT.ABOUT_TEXT);
    for (const d of Declarations()) this.params[d.id] = d.default;

    this.tiles = [];
    this.clocks = [];
    this.gridCols = 0;
    this.gridRows = 0;
    this.firstCol = 0;
    this.firstRow = 0;
    this.dealt = null;

    // Time. The plugin votes on whether its host sends seconds or
    // milliseconds; the page is the host here and sends seconds, and says so,
    // as the plugin's own harness does (SetClockScaleForTest( 1.0 )).
    this.clockScale = 1.0;
    this.lastNow = -1.0;
    this.story = 0.0;
    this.lastT = 0.0;
  }

  Set(id, value) {
    if (id >= 0 && id < PT.ABOUT_TEXT) this.params[id] = value; // a host float, as SetFloatParameter stores it
  }

  /** The Resolume clock's story time, set outright (the next frame is at it). */
  SetStoryForTest(seconds) {
    this.story = seconds;
    this.lastNow = -1.0;
  }

  FaceMask() {
    let mask = 0;
    for (let f = 0; f < kFaceCount; ++f) if (this.params[PT.FACE_FIRST + f] > 0.5) mask |= 1 << f;
    return mask;
  }

  /**
   * The tiles, each with its key, face, night and candle: drawn once a grid,
   * a seed and a set of faces. Tiles count from the origin, as the test
   * card's `ledTiles` does, so a tile left of or above it is negative.
   */
  Deal(width, height) {
    const p = this.params;
    const tw = IntegerParam(p[PT.TILE_WIDTH], kMinTile, kMaxTile);
    const th = IntegerParam(p[PT.TILE_HEIGHT], kMinTile, kMaxTile);
    const ox = IntegerParam(p[PT.ORIGIN_X], -kMaxOrigin, kMaxOrigin);
    const oy = IntegerParam(p[PT.ORIGIN_Y], -kMaxOrigin, kMaxOrigin);
    const seed = IntegerParam(p[PT.SEED], 0, kMaxSeed);
    const mask = this.FaceMask();
    const key = [width, height, tw, th, ox, oy, seed, mask].join(' ');
    if (this.dealt && this.dealt.key === key) return;
    this.dealt = { key, tw, th, ox, oy };

    // The visible tiles and a border of one all round: on Dark and None a
    // neighbour's glow reaches in from off the raster.
    this.firstCol = floorDiv(-ox, tw) - 1;
    this.firstRow = floorDiv(-oy, th) - 1;
    this.gridCols = floorDiv(width - 1 - ox, tw) + 1 - this.firstCol + 1;
    this.gridRows = floorDiv(height - 1 - oy, th) + 1 - this.firstRow + 1;
    this.tiles = [];
    this.clocks = [];
    for (let r = 0; r < this.gridRows; ++r) {
      for (let c = 0; c < this.gridCols; ++c) {
        const col = this.firstCol + c;
        const row = this.firstRow + r;
        const k = TileKey(seed, ox + col * tw, oy + row * th);
        this.tiles.push({ col, row, key: k, face: TileFace(k, mask), lit: 0.0, expression: 0.0, night: NightIndex(col, row) });
        this.clocks.push(TileClock.For(k));
      }
    }
  }

  /**
   * ProcessOpenGL's CPU half for one frame: the host's time in seconds and
   * this machine's clock in seconds since 1970 in; the tile texture's texels
   * and every uniform out.
   */
  Frame(width, height, hostSeconds, wallUnixSeconds) {
    const p = this.params;

    // Time: the host's, as real elapsed seconds, frame to frame, in double,
    // times Speed. A jump back or over a second passes no time.
    const now = hostSeconds * this.clockScale;
    if (this.lastNow >= 0.0) {
      const step = now - this.lastNow;
      if (step >= 0.0 && step <= kMaxFrameDelta) this.story += step * SpeedFromParam(p[PT.SPEED]);
    }
    this.lastNow = now;
    const clockMode = OptionIndex(p[PT.CLOCK], CLOCK.Count);
    const t = clockMode === CLOCK.Wall ? wallUnixSeconds - kEndlessEpochSeconds : this.story;
    this.lastT = t;

    // The tiles, then this frame's candles and faces, into the tile texture.
    this.Deal(width, height);
    const flicker = p[PT.FLICKER] > 0.5;
    const expressions = p[PT.EXPRESSIONS] > 0.5;
    const texels = new Float32Array(this.tiles.length * 4);
    this.tiles.forEach((tile, i) => {
      // halloween.ts, drawPumpkins: no face, no candle; a steady one burns at 0.8.
      tile.lit = tile.face < 0 ? 0.0 : flicker ? this.clocks[i].Candle(t) : 0.8;
      tile.expression = tile.face >= 0 && expressions ? this.clocks[i].Expression(t) : 0.0;
      texels[i * 4] = tile.face;
      texels[i * 4 + 1] = tile.lit;
      texels[i * 4 + 2] = tile.expression;
      texels[i * 4 + 3] = tile.night;
    });

    const { tw, th, ox, oy } = this.dealt;
    const size = Math.min(tw * 0.8, (th * 0.84) / (kPumpkinBottom - kPumpkinTop));
    return {
      t,
      texels,
      cols: this.gridCols,
      rows: this.gridRows,
      uniforms: {
        Raster: [width, height],
        TileSize: [tw, th],
        Origin: [ox, oy],
        FirstTile: [this.firstCol, this.firstRow],
        Size: F(size),
        Centre: [F(tw / 2.0), F(th / 2.0 - size * ((kPumpkinTop + kPumpkinBottom) / 2))],
        Background: OptionIndex(p[PT.BACKGROUND], BACKGROUND.Count),
      },
    };
  }
}
