/**
 * Pumpkin — browser demo.
 *
 * The Test Card's pumpkin patch as an FFGL source (SW Pumpkin, PK01): a
 * jack-o'-lantern on every LED tile, each carved with one of six faces, its
 * candle flickering and its face blinking, winking or chomping in its own
 * time. The one idea, from AGENTS.md: not a pumpkin like the test card's, the
 * test card's pumpkin. Nothing was redrawn by eye.
 *
 * ---------------------------------------------------------------------------
 * What runs here, and what does not
 * ---------------------------------------------------------------------------
 *
 * **The shader is the plugin's.** shaders.js is kCommon, kQuadVertex and
 * kPatchFragment from source/Shaders.cpp, spliced by
 * demo/tools/check_shaders.py and held to the C++ character for character by
 * it (tools/verify.sh runs it). The kit's `port()` changes the version line
 * and the precision defaults and nothing else.
 *
 * **The CPU half is a port** (port.js): the test card's hash and mulberry32
 * (Random.h), the lantern, the faces, which face goes on which tile, each
 * tile's night and each candle's and face's timing (Lantern.cpp), the shape
 * and vertex tables the shader paints from (Geometry.cpp), the parameters
 * (Controls.cpp) and ProcessOpenGL's CPU half (Pumpkin.cpp: the clock, the
 * deal, the tile texture, the uniforms). demo/tools/crosscheck/ holds it to
 * the test card's own numbers, to the plugin's C++, and its picture to the
 * test card's own pictures and to the plugin's harness's.
 *
 * **The host is this page.** It sends the plugin's parameters as Resolume
 * would (host floats, an option by its element's index, an integer as itself)
 * and its time in seconds. What it does not do is listed in `differences`.
 *
 * ---------------------------------------------------------------------------
 * Decisions this page made, and why
 * ---------------------------------------------------------------------------
 *
 * **No clip, no file, no Restart.** Pumpkin is a source with no inputs
 * (SetMinInputs( 0 ), SetMaxInputs( 0 )), so the kit's Clip picker and "Use
 * my own…" are removed, as astable's and graticule's pages remove them. And
 * Restart is removed too: it sends the host's clock back to 0, which the
 * plugin reads as a scrub, and the plugin lets no time pass across a jump, so
 * the candles carry on exactly where they were. A button that visibly does
 * nothing is worse than none. Pause and Step work: Step is one frame at
 * 60 fps, times Speed.
 *
 * **Integers are number fields.** Tile Width, Tile Height, Origin X, Origin Y
 * and Seed are FF_TYPE_INTEGER, in pixels and the test card's seed. The kit
 * has no integer control, so they are declared to it as text and turned into
 * number fields in their declared ranges (graticule's and zoetrope's answer).
 *
 * **The About block is not on the panel**, as on astable's and slipstream's
 * pages: a host shows it, a web page has links of its own at the top.
 */

import { mountDemo } from './vendor/demo.js';
import { PumpkinRenderer } from './pumpkin.js';
import { Declarations, SpeedFromParam, IntegerParam } from './port.js';

const REPO = 'https://github.com/stoatworks-labs/pumpkin';

/** Arena addresses a control by its name, lower case, spaces removed. */
const addressOf = (name) => name.toLowerCase().replace(/\s+/g, '');

const HINTS = {
  'Tile Width': 'Pixels, 16 to 4096 (FF_TYPE_INTEGER). 168 is the test card’s default LED tile.',
  'Tile Height': 'Pixels, 16 to 4096 (FF_TYPE_INTEGER).',
  'Origin X': 'Where the tile grid starts, in pixels (FF_TYPE_INTEGER). Tiles count from here, as the test card counts them, so a pumpkin keeps its face when the grid moves by a whole tile.',
  'Origin Y': 'Where the tile grid starts, in pixels (FF_TYPE_INTEGER).',
  Seed: 'Which face goes where, 0 to 9999 (FF_TYPE_INTEGER). Seed n here is the test card’s Seed n on a spanned surface.',
  Classic: 'Untick a face and the patch is dealt again from the rest, as on the test card. Untick all six and the pumpkins are plain: uncarved and unlit.',
  Fierce: 'Squints; zig-zag teeth.',
  Happy: 'Its eyes close in a blink.',
  Surprised: 'Gasps.',
  Sly: 'Cat’s eyes, narrowing to slits.',
  Goofy: 'One big eye, which winks.',
  Flicker: 'Off: a steady candle at 0.8, as the test card’s steady one burns.',
  Expressions: 'Blinks, winks, chomps, squints and gasps, each face every two and a half to four seconds, at a time of its own.',
  Speed: 'Still at the bottom, then a quarter to four times the test card’s pace, 1x in the middle. Acts on the Resolume clock only.',
  Clock: 'Resolume: the host’s time, times Speed (here the host is this page). Wall: seconds since 1 January 2026 by this machine’s clock, the test card’s own live clock; Speed does not touch it.',
  Background: 'Night: each tile its own night, each glow stopping at its tile’s edge, as the test card draws them. Dark: one night everywhere, the glows running on across the edges. None: transparent, to lay over the layers below.',
};

const DECLARED = Declarations();

/** The kit's control for each FFGL type. The kit has no integer: text, then a number field. */
const PARAMS = DECLARED.map((d) => {
  const p = { id: addressOf(d.name), name: d.name, group: d.group, hint: HINTS[d.name], decl: d };
  switch (d.type) {
    case 'integer':
      return { ...p, type: 'text', default: String(d.default) };
    case 'boolean':
      return { ...p, type: 'boolean', default: d.default > 0.5 ? 1 : 0 };
    case 'option':
      return { ...p, type: 'option', default: d.default, elements: d.elements };
    case 'standard':
      return {
        ...p,
        type: 'standard',
        default: d.default,
        // SpeedFromParam, on the float the host would send.
        display: (v) => {
          const s = SpeedFromParam(Math.fround(v));
          return s === 0 ? 'still' : `${s.toFixed(2)}x`;
        },
      };
    default:
      throw new Error(`no kit control for ${d.type} (${d.name})`);
  }
});

/**
 * The renderer the kit drives. Every frame it sends each control's value to
 * the plugin as a host would, then runs the frame: the host's time is the
 * kit's clock, in seconds; the Wall clock is this machine's.
 */
function createRenderer(gl, quad) {
  const renderer = new PumpkinRenderer(gl, quad);
  const missing = renderer.missingUniforms();
  if (missing.length) {
    // A uniform the program does not have is a control that would do nothing.
    throw new Error(`the patch shader has no uniform ${missing.join(', ')}`);
  }
  // An integer field the visitor has half typed keeps the last value the
  // plugin was sent, as a host does when nothing is sent.
  const lastInteger = new Map(PARAMS.filter((p) => p.decl.type === 'integer').map((p) => [p.id, p.decl.default]));

  function hostValue(p, v) {
    const d = p.decl;
    switch (d.type) {
      case 'integer': {
        const n = Number.parseInt(String(v).trim(), 10);
        if (Number.isFinite(n)) lastInteger.set(p.id, IntegerParam(n, d.min, d.max));
        return lastInteger.get(p.id);
      }
      case 'boolean': return v > 0.5 ? 1 : 0;
      case 'option': return Math.round(v);
      default: return Math.fround(v);
    }
  }

  return {
    render({ params, width, height, time }) {
      for (const p of PARAMS) renderer.core.Set(p.decl.id, hostValue(p, params.get(p.id)));
      renderer.draw(width, height, time, Date.now() / 1000);
    },
  };
}

const mounted = mountDemo({
  name: 'Pumpkin',
  pluginId: 'PK01',
  kind: 'source',
  tagline:
    'The Test Card’s pumpkin patch: a jack-o’-lantern on every LED tile, each carved with one of six faces, its candle flickering and its face blinking, winking or chomping in its own time. Not a pumpkin like the test card’s: the test card’s pumpkin, ported line for line, so the same tiles and the same seed carve the same patch.',
  repo: REPO,
  page: 'https://stoatworks-labs.com/software/pumpkin/',
  video: 'https://www.youtube.com/watch?v=N6E7XCnqee0',

  // The stock banner says the page runs "on generated clips". Pumpkin is a
  // SOURCE with no inputs, so that would be the banner making the kind of
  // claim it exists to prevent.
  blurb:
    'It is SW Pumpkin’s own GLSL, ported from the repository to WebGL2 unedited, painting a lantern table and candles worked out each frame by a JavaScript port of the plugin’s CPU half — same parameters, same maths, no install. Pumpkin is a source: it reads no video, and nothing on this page is a clip.',

  // Background None is transparent and premultiplied, so what sits behind it
  // is a real question. In Resolume that would be the layers below.
  showBackdrop: true,

  // A source has no clip. Empty rather than a list nothing reads; the two
  // transport controls the kit builds from this are removed below.
  sources: [],

  params: PARAMS.map(({ decl, ...p }) => ({ ...p })),

  differences: [
    'The CPU half is a port, not the plugin’s code. The plugin works out on the CPU, in double, which face goes on which tile (the test card’s hash and mulberry32), each candle’s flicker and each face’s blink at this instant, and once, the table of shapes and vertices its shader paints the lantern from. Here all of that is JavaScript (port.js), line for line from Random.h, Lantern.cpp, Geometry.cpp, Controls.cpp and Pumpkin.cpp. A check in the repository runs this very file against the test card’s own numbers (its hashes, the face on 450 tiles under five sets of faces, its candles and blinks at 48 instants out to ten years, every point of every face at four expressions: all identical) and against the plugin’s C++ (the shape and vertex tables the GPU reads, float for float; every other number within 1e-12, where the browser’s maths library and Apple’s differ in the last bit), and shows that eleven deliberately broken ports fail it. Only that check, not this page, is a reason to believe the port.',
    'There is no clip, no "use my own file" and no Restart. Pumpkin is a source with no inputs, so the kit’s clip controls are removed rather than left present and dead. Restart is removed because it would send the host’s clock back to zero, which the plugin takes as a scrub: no time passes across a jump, so the candles would carry on exactly where they were. Pause and Step work, and Step is one frame at 60 fps, times Speed.',
    'The host is this page. It sends its time in seconds and tells the port so; the plugin votes over its first frames on whether Resolume is sending seconds or milliseconds, and that vote is not run here, as it is not in the plugin’s own harness. On Clock Wall the time is this browser’s clock, so a pumpkin here flickers in step with the same pumpkin on a test card output only as far as the two machines’ clocks agree.',
    'Tile Width, Tile Height, Origin X, Origin Y and Seed are FF_TYPE_INTEGER in the plugin, and number fields here, clamped to their declared ranges. A field half typed keeps the last value sent. The About block (a text line and four link buttons) is not on the panel.',
    'The plugin has never been loaded into Resolume on macOS: there it has run in oxbow, a real FFGL host that is not Resolume, and its own harness. On Windows it loads and renders in Resolume Arena 7.27.1 under software rendering. The page’s picture was measured once, in headless Chrome on one Mac (its GPU, and Chrome’s software WebGL): a mean of 0.24 to 0.81 code values from the test card’s own seven pictures, as the plugin is, and at most one code value from the plugin’s own renders at the same settings. Another browser or GPU was not measured, and a pixel here is not evidence about Resolume.',
  ],

  createRenderer,
});

//---------------------------------------------------------------------------
// The page's own adjustments to the kit's furniture, after it is built.
//---------------------------------------------------------------------------
if (mounted) {
  // FF_TYPE_INTEGER: a number field in the declared range.
  for (const p of PARAMS) {
    if (p.decl.type !== 'integer') continue;
    const field = document.getElementById(`p-${p.id}`);
    if (!field) continue;
    field.type = 'number';
    field.min = String(p.decl.min);
    field.max = String(p.decl.max);
    field.step = '1';
    field.inputMode = 'numeric';
  }

  // A source has no clip and takes no file; Restart would do nothing (above).
  for (const field of document.querySelectorAll('.transport__field')) {
    if (field.querySelector('.transport__label')?.textContent === 'Clip') field.remove();
  }
  document.querySelector('.transport__file')?.remove();
  for (const button of document.querySelectorAll('.transport .btn')) {
    if (button.textContent === 'Restart') button.remove();
  }
}
