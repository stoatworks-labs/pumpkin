# pumpkin

The Test Card's pumpkin patch for Resolume Arena/Avenue, as an FFGL **source**:
`SW Pumpkin` (`PK01`), a jack-o'-lantern on every LED tile, six faces, flickering
candles and blinks, winks and chomps, ported from `~/Projects/video/test-card`.
C++/GLSL, CMake MODULE → universal `.bundle` (macOS) + Windows `.dll`. MIT.
Bundle id `com.stoatworks.ffgl.pumpkin`.

Read `AGENTS.md` before touching the port (`Lantern.cpp`, `Random.h`), the
geometry table (`Geometry.cpp`) or the painter (`Shaders.cpp`).

## Commands (CMake)
- Configure: `cmake -B build -DCMAKE_BUILD_TYPE=Release`
- Fast dev build: add `-DCMAKE_OSX_ARCHITECTURES=arm64`
- Build: `cmake --build build`
- Install to Resolume: `cmake --install build` (never from `~/Projects`)
- Render: `./build/pktest --out /tmp/patch.png` (the defaults, 5 s in)
- All six faces close up (docs/faces.png): `./build/pktest --out /tmp/faces.png --size 960x540 --set "Tile Width=320" --set "Tile Height=270" --set Seed=69 --frames 133`
- List parameters: `./build/pktest --list`
- Set anything by name: `./build/pktest --set "Seed=7" --set "Background=None"` (options by name or index)
- As video: `./build/pktest --pipe --frames 1800 --size 1280x720 --script cues.txt | ffmpeg -f rawvideo -pix_fmt rgba -s 1280x720 -r 60 -i - out.mp4`
  A cue line is `frame  Parameter Name  value` (`#` starts a comment). A standard
  control is linear between keys; an option, boolean or integer steps. Frame *n*
  is clocked at n / `--fps` (60). An unknown name or an unparsable value exits 2.
- A draft of the fleet gate's expectation: `./build/pktest --expect`.
- Regenerate the test card's fixtures: `node tools/fixtures/testcard.mjs [path-to-test-card]`
  (needs Chrome; writes `tools/fixtures/testcard.txt`, `renders.txt` and the PNGs).

## Verify
- Everything: `tools/verify.sh` (a few minutes: reserved words, no trig in the GLSL,
  glslc, fresh universal build, the bundle through lipo/plist/codesign/oxbow
  probe+selftest, every check on the GPU and again on the software renderer, the
  offline set, the `--pipe` contract, the negative controls, the mutants, the sweep,
  the bench). Point it at oxbow with `OXBOW=/path/to/oxbow`.
- **Against the test card**: `--port` (no GL: hash, rng, faces, timing and drawing
  against its TypeScript's own numbers), `--testcard` (the picture against its own
  painter's, seven renders).
- **The port's shape**: `--linear` and `--smoothing` (no GL).
- **The plugin**: `--tiles`, `--clock`, `--still`, `--resize`; no GL: `--names`.
- One raster only: add `--size WxH`. The software renderer:
  `PKTEST_RENDERER=software ./build/pktest --still --size 320x180`.
- **The checks can fail**: `--negative` (13 wrong models), `tools/mutate.sh` (8 mutants).
- What CI runs: `--offline` and `tools/glslc.sh`.
- No dead controls: `python3 tools/sweep.py`.
- Cost: `--bench`.

## Notes
- **The port is the test card's arithmetic.** `-ffp-contract=off` on the core: a fused
  multiply-add is an ulp the JavaScript does not make.
- **Tiles count from the origin** (the test card's `ledTiles`); a tile's face is keyed
  on its surface position, its night on its column and row. Integer arithmetic only:
  GLSL leaves `/` and `%` with negative operands undefined (`floorDiv`).
- **Seed n is the test card's Seed n on a span** (`localKey` 0).
- **Clock Wall** is the test card's endless time: seconds since 2026-01-01Z by this
  machine's clock; Speed does not apply to it.
- Every host parameter is 0..1 except Tile Width/Height, Origin X/Y and Seed (real
  integers, pixels and the test card's seed). An option reads back 0..1 whatever its
  count: map by element index.
- No sin/cos/atan in the GLSL (verify.sh greps): the candles' sines are on the CPU in
  double; a rotated ellipse's cos and sin come in with the shape table.
- GLSL reserved words must not be identifiers (including `packed`, `smooth`,
  `sample`); verify.sh greps for the 4.10 list.
- `pumpkin_core` is an OBJECT library: the registration is a file-scope constructor
  nothing references.
- Not released, not registered, not on GitHub. `StoatworksAbout.h` and
  `ATTRIBUTIONS.md` are provisional hand copies until the backend's sync scripts
  generate them.

## Diagnostics

`source/Diag.{h,cpp}` is a log file only, with no crash handler (this runs
inside Resolume). It records a shader that failed to compile and the GL
vendor/renderer.

    ~/Library/Logs/pumpkin/pumpkin.YYYY-MM-DD.log
