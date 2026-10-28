// The page's port against the test card's own numbers: tools/fixtures/
// testcard.txt, which tools/fixtures/testcard.mjs printed from the test
// card's TypeScript, and which the plugin's `pktest --port` holds the C++ to.
// The same measures as pktest --port, on the JavaScript:
//
//   - hash and rng exactly (a uint32 over 2^32 is exact in a double);
//   - every tile's key and its face under each set of faces exactly;
//   - each candle's level and each face's expression to 1e-12;
//   - every point of every shape of every face at four expressions to 1e-12,
//     with its smoothing, closure, fill, alpha, stroke and width exactly;
//   - the extent the painter sizes by exactly.
//
// The 1e-12 is pktest's: the same double arithmetic in the same order, where
// only V8's sin and cos and the test card's (V8's too, in Chrome) may differ.
//
//     node demo/tools/crosscheck/port.mjs      exit 1 on any difference

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath } from 'node:url';
import { loadPort } from './load.mjs';

const here = dirname(fileURLToPath(import.meta.url));
const fixture = join(here, '..', '..', '..', 'tools', 'fixtures', 'testcard.txt');
const P = await loadPort();

const hashes = [];
const rngs = [];
let masks = [];
const tiles = [];
const timings = [];
const shapes = [];
let extent = null;
for (const line of readFileSync(fixture, 'utf8').split('\n')) {
  const w = line.trim().split(/\s+/);
  switch (w[0]) {
    case 'hash': {
      const n = Number(w[1]);
      hashes.push({ values: w.slice(2, 2 + n).map(Number), h: Number(w[2 + n]) });
      break;
    }
    case 'rng':
      rngs.push({ seed: Number(w[1]), r: w.slice(2, 7).map(Number) });
      break;
    case 'masks':
      masks = w.slice(2, 2 + Number(w[1])).map(Number);
      break;
    case 'tile':
      tiles.push({ seed: Number(w[1]), x: Number(w[2]), y: Number(w[3]), key: Number(w[4]), faces: w.slice(5).map(Number) });
      break;
    case 'timing':
      timings.push({ key: Number(w[1]), t: Number(w[2]), candle: Number(w[3]), expression: Number(w[4]) });
      break;
    case 'shape': {
      const n = Number(w[11]);
      const pts = [];
      for (let i = 0; i < n; i++) pts.push({ x: Number(w[12 + 2 * i]), y: Number(w[13 + 2 * i]) });
      shapes.push({
        face: Number(w[1]), e: Number(w[2]), list: w[3], index: Number(w[4]), smooth: w[5] === '1', closed: w[6] === '1',
        fill: w[7], alpha: Number(w[8]), stroke: w[9], strokeWidth: Number(w[10]), pts,
      });
      break;
    }
    case 'extent':
      extent = { top: Number(w[1]), bottom: Number(w[2]) };
      break;
    default:
      break;
  }
}

let failed = 0;
const check = (ok, text) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${text}`);
  if (!ok) failed += 1;
};

if (!hashes.length || !rngs.length || !tiles.length || !timings.length || !shapes.length || !extent) {
  check(false, 'testcard.txt loads');
  process.exit(1);
}

let wrong = 0;
for (const c of hashes) wrong += P.Hash(...c.values) !== c.h ? 1 : 0;
for (const c of rngs) {
  const r = new P.Rng(c.seed);
  for (const expected of c.r) wrong += r.next() !== expected ? 1 : 0;
}
check(wrong === 0, `hash and rng: ${hashes.length} hashes and ${rngs.length} five-draw sequences, ${wrong} differ`);

let keys = 0;
let faces = 0;
for (const c of tiles) {
  const key = P.TileKey(c.seed, c.x, c.y);
  keys += key !== c.key ? 1 : 0;
  masks.forEach((mask, m) => { faces += P.TileFace(key, mask) !== c.faces[m] ? 1 : 0; });
}
const seeds = new Set(tiles.map((c) => c.seed)).size;
check(keys === 0 && faces === 0,
  `the patch: ${tiles.length} tiles over ${seeds} seeds, ${keys} keys and ${faces} of ${tiles.length * masks.length} faces differ `
  + `(sets of faces: ${masks.join(', ')})`);

let candleErr = 0;
let exprErr = 0;
let clockErr = 0;
let far = 0;
for (const c of timings) {
  candleErr = Math.max(candleErr, Math.abs(P.CandleLevel(c.t, c.key) - c.candle));
  exprErr = Math.max(exprErr, Math.abs(P.ExpressionLevel(c.t, c.key) - c.expression));
  const clock = P.TileClock.For(c.key);
  clockErr = Math.max(clockErr, Math.abs(clock.Candle(c.t) - c.candle), Math.abs(clock.Expression(c.t) - c.expression));
  far = Math.max(far, c.t);
}
check(candleErr <= 1e-12 && exprErr <= 1e-12 && clockErr <= 1e-12,
  `timing: ${timings.length} instants to t = ${far} s (${(far / 31557600).toFixed(1)} years): candle within ${candleErr.toExponential(1)}, `
  + `expression within ${exprErr.toExponential(1)}, TileClock within ${clockErr.toExponential(1)} (1e-12)`);

const hex = (v) => v.toString(16).padStart(6, '0');
let shapeWrong = 0;
let pointErr = 0;
let points = 0;
const lanterns = new Map();
for (const s of shapes) {
  const k = `${s.face} ${s.e}`;
  if (!lanterns.has(k)) lanterns.set(k, P.MakeLantern(s.face, s.e));
  const got = lanterns.get(k)[s.list === 'body' ? 'body' : s.list === 'wall' ? 'walls' : 'holes'][s.index];
  if (!got || got.pts.length !== s.pts.length || got.smooth !== s.smooth || got.closed !== s.closed) {
    shapeWrong += 1;
    continue;
  }
  if (s.list === 'body') {
    const fill = got.hasFill ? hex(got.fill) : '-';
    const stroke = got.hasStroke ? hex(got.stroke) : '-';
    if (fill !== s.fill || got.alpha !== s.alpha || stroke !== s.stroke || (got.hasStroke ? got.strokeWidth : 0) !== s.strokeWidth) {
      shapeWrong += 1;
    }
  }
  s.pts.forEach((p, i) => {
    pointErr = Math.max(pointErr, Math.abs(got.pts[i].x - p.x), Math.abs(got.pts[i].y - p.y));
    points += 1;
  });
}
check(shapeWrong === 0 && pointErr <= 1e-12,
  `the drawing: ${shapes.length} shapes (the body and six faces' walls and holes at four expressions), ${shapeWrong} differ in kind or paint; `
  + `${points} points within ${pointErr.toExponential(1)} (1e-12)`);

check(extent.top === P.kPumpkinTop && extent.bottom === P.kPumpkinBottom,
  `the extent: ${P.kPumpkinTop} to ${P.kPumpkinBottom}`);

console.log(failed ? `\n${failed} FAILED` : '\nPASS');
process.exit(failed ? 1 : 0);
