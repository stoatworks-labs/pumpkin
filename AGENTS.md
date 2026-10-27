# pumpkin — for agents

The why behind the code. `CLAUDE.md` has the commands; this file has the
reasoning, the traps that were actually hit, and what is and is not known.
Built 2026-10-27 in one session, from Allan's one-line request ("a new resolume
plugin that replicates the pumpkin pattern from our test-card suite"). The
pattern is the Test Card's **Pumpkin patch** (`~/Projects/video/test-card`,
`src/patterns/halloween.ts` and `art/halloween.ts`, built 2026-10-21). The
fleet's briefs are `~/Projects/resolume/specs/BRIEF.md` and `BRIEF-ADDENDUM.md`.

## The one idea

**Not a pumpkin like the test card's: the test card's pumpkin.** The lantern,
the six faces and their expressions, the candle flicker, which face goes on
which tile and each tile's night are the test card's code, ported line for
line, and the harness holds the port to what the test card's own TypeScript
prints and the picture to what its own painter draws. Nothing was redrawn by
eye. Change the test card's pumpkin and this plugin is out of date until
`tools/fixtures/testcard.mjs` is rerun and the port follows (see "When the test
card changes").

## How a frame goes

1. **Time** (`Pumpkin.cpp`): the host's elapsed time, frame to frame, in double,
   times Speed, into `story` (the unit voted against the wall clock: rosette's
   code via langton). A jump back or over 1 s passes no time. With Clock Wall, t
   is instead this machine's clock in seconds since 2026-01-01T00:00:00Z, which
   is exactly what the test card's live output uses in endless mode
   (`loop.ts`, `endlessTime`), so the two flicker in step on synced machines.
2. **Deal** (`Deal`): the visible tiles, counted from the origin as the test
   card's `ledTiles` counts them, each with its key (`tileKey`: a hash of the
   seed and the tile's surface position), its face (`tileFace`), its night
   (`TILE_NIGHTS[ mod( col + 2 row, 6 ) ]`) and its `TileClock` (the rng draws
   behind its candle and its expressions). Cached until the raster, the tiles,
   the origin, the seed or the faces ticked change.
3. **Candles** (CPU, double): each tile's `lit` (0 if no face, 0.8 if Flicker is
   off, else `candleLevel`) and expression (0 unless a face and Expressions,
   else `expressionLevel`), into an RGBA32F texture, one texel a tile.
4. **Paint** (one fragment pass): each pixel finds its tile in integer
   arithmetic and paints that tile's lantern in the test card's order: night,
   shadow, glow, body, cut walls, candlelit holes.

### The drawing is a table, not GLSL (`Geometry.cpp`)

The lantern is uploaded once as two RGBA32F textures: shapes (8 texels each:
kind, colours, box, ellipse at e = 0 and 1, rotation) and vertices (x, y at
e = 0 and e = 1). The shader is a small generic painter. That keeps one copy
of the drawing (the port's), which the offline checks can see.

- **The canvas smooths almost everything.** `fillShapes` traces every body shape
  with quadratic curves through its edge midpoints, and every `round` hole too.
  A smoothed regular n-gon is, to 3e-4 of its radius at n = 12 (5e-6 at n = 40),
  a circle of radius (cos(π/n) + 1 − sin²(π/n)/2) / 2 — the curve swings between
  the first at the midpoints and the second at the apexes — and smoothing is
  affine, so every `ellipse()` shape is drawn as a true ellipse at that radius.
  The other smoothed lists (stem, leaf, tendril, happy mouth) are flattened,
  each quadratic into enough pieces for 0.1 px at a pumpkin 4096 px wide.
  `--smoothing` samples the canvas's curve exactly and measures both.
- **Every vertex is linear in the expression** (scaled radii, a lens's
  bulge, a jaw scaled about a fixed point, a hole scaled about its wall's
  centroid, which moves linearly too; smoothing is linear in its control
  points), so the table stores e = 0 and e = 1 and the shader mixes per tile.
  Flattening uses one step count for both ends (the larger), or they would not
  pair up. `--linear` checks the port at three e and the table at two.
- **Coverage** is the box filter of a pixel over a signed distance: a fill is
  `clamp( 0.5 - d / px )`, a stroke the overlap of the pixel's footprint with
  the band either side of its line (right for strokes thinner than a pixel,
  as the ribs' 0.012 is on any pumpkin under 83 px). Ellipse distance is first
  order (|u| − 1 over its gradient), exact on the curve.
- **Compositing** is premultiplied "over" in the stored values, as a canvas
  composites; gradients interpolate unpremultiplied, as the canvas's do.
  `mix()` rounds to whole code values, so the shader's `mixHex` does too.

## Would this hold on another rasteriser, at another raster?

- `--port`, `--linear`, `--smoothing`, `--names`: no GL; CPU doubles. Exact or
  1e-12 (the same operations in the same order; only libm's sin/cos and V8's
  may differ, by an ulp). Raster-free.
- `--testcard`: holds at every render's own raster (320x180 to 1280x720, tiles
  40x45 to 320x270) on this Mac's GPU and on Apple's software renderer. Its
  tolerances are rasteriser differences, not fitted numbers: Skia's coverage and
  this shader's differ along every edge, and Skia rounds to 8 bits per shape.
  Measured: mean 0.24–0.81 codes, 0.001–0.10% of pixels over 24 codes out (all
  edges), flat areas within 1–2 codes but for 1 pixel in 180,000 (14 codes,
  beside a junction of rib strokes, where the card's 3x3 block is flat and an
  edge here lands a fraction of a pixel over). A Mesa or Windows rasteriser
  would have to be measured; nothing here depends on GPU-specific behaviour.
- `--tiles`: exact (8-bit corners; a whole-tile move bit for bit on the GPU).
- `--still`, `--resize`, the whole-tile move: exact on the GPU. **Apple's
  software renderer does not repeat a draw bit for bit**: two draws of the same
  frame in one context differ by a float ulp or two (1.5e-8 measured). The
  addendum saw it across contexts; here it is within one. These comparisons
  allow 1e-6 on the software renderer only (`compare()`), far above that and
  far below one 8-bit code.
- `--clock`: doubles; per frame to 1e-6 s.

## Traps hit

- **FMA contraction broke the port.** arm64 clang fuses `lo + (hi − lo) * r()`
  into one rounding; JavaScript rounds twice. A candle's frequency was an ulp
  out, which is 1e-7 of brightness by t = 3.15e8 s. `-ffp-contract=off` on the
  core (CMakeLists.txt); `--port`'s far times are what saw it.
- **A float clock's error telescopes.** The first `--clock` summed 2 s of
  frames at 499 million ms; 2 s is 64 of a float's 1/32 s steps there, so the
  float clock's errors cancelled and its negative control passed. It now checks
  every frame (langton's shape).
- **The harness ran faster than a host,** so the seconds/ms vote never settled
  until the first frames were paced in real time (langton's).
- **A host's clock is never negative.** A scrub check that went below 0 fell
  through to the wall-clock fallback; it starts 20 s in.
- **The glow reaches every corner of a square tile** (1.1 pumpkins across), so
  "Background None is clear somewhere" needs wide tiles.
- **Node 26 has no TypeScript transform mode.** `stripTypeScriptTypes` only
  strips; `idle/gl.ts` has parameter properties, so the fixture script skips
  what it cannot strip and builds the paint env inline (env.ts imports the idle
  savers). The pumpkins import nothing that needs a compiler.
- **A blink is brief.** At most instants only a few faces are mid-way, so the
  sweep reads Expressions at the moment the fixture script found busiest for
  those tiles (seed 7, 6.2 s), not at its usual 160 frames.

## What `localKey` means here

The test card keys a tile on `hash( seed, x + view.x, y + view.y, localKey )`.
`localKey` is 0 on a spanned surface and an output's own number otherwise.
The plugin has one surface and uses 0, so **Seed n here is the test card's
Seed n on a span**. An unspanned test card output (localKey ≠ 0) deals a
different patch for the same seed; the README says so.

## When the test card changes

1. `node tools/fixtures/testcard.mjs` (it reads `~/Projects/video/test-card` by
   default and says which commit; Chrome must be installed).
2. `pktest --port` and `--testcard` say what moved. Follow it in
   `Lantern.cpp` (art and timing), `Random.h`, the colours in `Pumpkin.cpp`'s
   anonymous namespace and the painter order in `Shaders.cpp`.
3. If a face gains a non-linear movement, `--linear` fails: the table would
   need more than two ends.

## Verified, and how

`tools/verify.sh` on this Mac (Apple Silicon), 2026-10-27, all green: the
universal build (no warnings outside the SDK), lipo, plist, ad-hoc codesign,
`oxbow probe` (SW Pumpkin, PK01, source) and `oxbow selftest` (120 frames);
every check on the GPU and on the software renderer; `--offline`; the pipe
contract; 13 negative controls, 13 caught; 8 mutants, 8 caught (two GLSL in the
painter, one in the row flip, the hash's shift, the blink's length, a hole's
ellipse, the smoothing radius, the steady candle); 16 controls swept, none dead.

## Assumed, not verified

- **Never loaded into Resolume**, on either platform. oxbow is a real FFGL host
  that is not Resolume. Never built on Windows (CI would be the first place).
- That Resolume's composition texture shows 8-bit sRGB-encoded values as the
  canvas does: the comparison is in stored values, like everything in the fleet.
- That Chrome's software canvas is the test card's picture. Its Download PNG
  on a GPU canvas may differ from the fixtures by a code or so at edges.
- The Wall clock agreeing with a test card on another machine needs both
  clocks synced (NTP): the plugin reads the system clock, nothing more.

## Not done, open

- No presets, no OpenFX port, no browser demo, no user guide, no Arena gate
  (`pktest --expect` drafts the expectation).
- Not registered: `StoatworksAbout.h` and `ATTRIBUTIONS.md` are provisional hand
  copies; no `.github/FUNDING.yml` or issue templates (the backend's sync makes
  them); no website page. Not on GitHub.
- 16 px tiles at 4K cost 9.8 ms a frame (32,400 lanterns, each candle's three
  sines on the CPU each frame): fine at the default 168 px (1.6 ms at 4K).
- Beat sync (a blink on the beat, candles that flare on a kick) was not asked
  for and is not here.
- The test card's other per-tile and Halloween patterns (the hamsters, the
  spooky forest, the haunted house) would port the same way.
