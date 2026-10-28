// Holds run.mjs's output (the page's port) to printer.cpp's (the plugin's own
// code), line by line.
//
//   - Every line must be there on both sides, in the same order, with the
//     same words: the declarations, the table's layout, every key, face and
//     night, every option and integer conversion, every shape's kind.
//   - Every FLOAT (f:) must be the same 32 bits. Those are what the GPU reads:
//     the shape and vertex tables, and the host's floats.
//   - Every DOUBLE (d:) must be within 1e-12 of the plugin's, the tolerance
//     pktest --port holds the plugin to against the test card; and how many
//     differ at all, and by how many units in the last place, is reported.
//     The arithmetic is the same operations in the same order (the plugin's
//     core is built -ffp-contract=off, and so is printer.cpp); what can differ
//     is the maths library, V8's Math against Apple's libm.
//
//     node compare.mjs cpp.txt js.txt      exit 1 on any difference

import { readFileSync } from 'node:fs';

const [cppPath, jsPath] = process.argv.slice(2);
const cpp = readFileSync(cppPath, 'utf8').trimEnd().split('\n');
const js = readFileSync(jsPath, 'utf8').trimEnd().split('\n');

const view = new DataView(new ArrayBuffer(8));
const toDouble = (hex) => {
  view.setUint32(0, parseInt(hex.slice(0, 8), 16));
  view.setUint32(4, parseInt(hex.slice(8), 16));
  return view.getFloat64(0);
};
const ulps = (a, b) => {
  const big = (hex) => BigInt.asIntN(64, BigInt(`0x${hex}`));
  const ord = (v) => (v < 0n ? -(v & 0x7fffffffffffffffn) : v);
  const diff = ord(big(a)) - ord(big(b));
  return Number(diff < 0n ? -diff : diff);
};

const problems = [];
const stats = new Map(); // section -> { doubles, differ, worstUlps, worstAbs, floats }
const statOf = (section) => {
  if (!stats.has(section)) stats.set(section, { lines: 0, doubles: 0, differ: 0, nearZero: 0, worstUlps: 0, worstAbs: 0, floats: 0 });
  return stats.get(section);
};

if (cpp.length !== js.length) problems.push(`the plugin printed ${cpp.length} lines and the port ${js.length}`);
const n = Math.min(cpp.length, js.length);
for (let i = 0; i < n && problems.length < 12; i++) {
  const a = cpp[i].split(' ');
  const b = js[i].split(' ');
  const section = a[0];
  const s = statOf(section);
  s.lines += 1;
  if (a.length !== b.length || a[0] !== b[0]) {
    problems.push(`line ${i + 1}: the plugin printed\n      ${cpp[i].slice(0, 160)}\n    the port\n      ${js[i].slice(0, 160)}`);
    continue;
  }
  for (let k = 0; k < a.length; k++) {
    const x = a[k];
    const y = b[k];
    if (x.startsWith('d:') && y.startsWith('d:')) {
      s.doubles += 1;
      if (x === y) continue;
      s.differ += 1;
      const u = toDouble(x.slice(2));
      const v = toDouble(y.slice(2));
      const abs = Math.abs(u - v);
      // Units in the last place say nothing about two values either side of
      // zero (a cos of a quarter turn is 6e-17 one way and 1e-17 the other):
      // those are counted by their absolute difference only.
      if (Math.max(Math.abs(u), Math.abs(v)) < 1e-12) s.nearZero += 1;
      else s.worstUlps = Math.max(s.worstUlps, ulps(x.slice(2), y.slice(2)));
      s.worstAbs = Math.max(s.worstAbs, abs);
      if (!(abs <= 1e-12)) problems.push(`line ${i + 1} (${section}), word ${k}: ${toDouble(x.slice(2))} against ${toDouble(y.slice(2))}`);
    } else if (x.startsWith('f:') && y.startsWith('f:')) {
      s.floats += 1;
      if (x !== y) problems.push(`line ${i + 1} (${section} ${a[1]}), float ${k}: the plugin's ${x}, the port's ${y}`);
    } else if (x !== y) {
      problems.push(`line ${i + 1} (${section}), word ${k}: the plugin's "${x}", the port's "${y}"`);
    }
  }
}

for (const [section, s] of stats) {
  const parts = [`${s.lines} lines`];
  if (s.floats) parts.push(`${s.floats} floats identical`);
  if (s.doubles) {
    parts.push(s.differ === 0 ? `${s.doubles} doubles identical`
      : `${s.doubles} doubles, ${s.differ} differ (worst ${s.worstUlps} ulp${s.nearZero ? `, ${s.nearZero} of them within 1e-12 of zero` : ''}; `
        + `at most ${s.worstAbs.toExponential(1)} apart)`);
  }
  console.log(`  ${section.padEnd(15)} ${parts.join(', ')}`);
}
if (problems.length) {
  console.log(`FAIL ${problems.length >= 12 ? 'at least ' : ''}${problems.length} difference(s):`);
  for (const p of problems) console.log(`    ${p}`);
  process.exit(1);
}
console.log(`PASS ${n} lines`);
