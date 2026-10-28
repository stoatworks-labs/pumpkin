// The JavaScript half of the browser demo's cross-check of its CPU port:
// demo/port.js -- the very file the page loads -- printed through printer.cpp's
// scenario, the same way. compare.mjs holds the two together. Read this
// beside printer.cpp: each block mirrors one there.
//
//     node demo/tools/crosscheck/run.mjs > js.txt     (PORT_MUTANT=n: a mutant)

import { loadPort } from './load.mjs';

const P = await loadPort();
const F = Math.fround;

const view = new DataView(new ArrayBuffer(8));
const d = (v) => {
  view.setFloat64(0, v);
  return `d:${view.getUint32(0).toString(16).padStart(8, '0')}${view.getUint32(4).toString(16).padStart(8, '0')}`;
};
const f = (v) => {
  view.setFloat32(0, v);
  return `f:${view.getUint32(0).toString(16).padStart(8, '0')}`;
};

const out = [];
const print = (line) => out.push(line);

// The declarations, About block left off. %g of an integer range is the integer.
for (const decl of P.Declarations()) {
  let line = `decl ${decl.id} ${decl.name}|${decl.group}|${decl.type}|${f(decl.default)}`;
  if (decl.type === 'integer') line += `|${decl.min}|${decl.max}`;
  if (decl.type === 'option') line += decl.elements.map((e) => `|${e}`).join('');
  print(line);
}

// The tables the GPU reads.
{
  const t = P.BuildTable();
  let meta = `table meta ${t.bodyFirst} ${t.bodyCount}`;
  for (let i = 0; i < P.kFaceCount; ++i) meta += ` ${t.faceFirst[i]} ${t.faceCount[i]}`;
  print(`${meta} ${t.shapes.length} ${t.vertexCount} ${t.vertexRows}`);
  const per = P.kTexelsPerShape * 4;
  for (let i = 0; i < t.shapes.length; ++i) {
    print(`table-shape ${i} ${[...t.shapeTexels.subarray(i * per, (i + 1) * per)].map(f).join(' ')}`);
  }
  const row = P.kVertexRow * 4;
  for (let r = 0; r < t.vertexRows; ++r) {
    print(`table-vertex ${r} ${[...t.vertexTexels.subarray(r * row, (r + 1) * row)].map(f).join(' ')}`);
  }
}

// The controls' conversions.
for (let i = 0; i <= 1000; ++i) {
  const v = F(F(i) / F(1000));
  print(`speed ${i} ${f(v)} ${d(P.SpeedFromParam(v))}`);
}
for (const s of [0.0, 0.1, 0.25, 0.5, 1.0, 1.5, 2.0, 3.999, 4.0, 9.0]) print(`paramFromSpeed ${d(s)} ${f(P.ParamFromSpeed(s))}`);
for (const v of [-3.0, -0.5, 0.0, 0.49, 0.5, 1.0, 1.5, 2.0, 2.5, 7.0].map(F)) {
  print(`option ${f(v)} ${P.OptionIndex(v, 2)} ${P.OptionIndex(v, 3)}`);
}
for (const v of [-5000.0, -4096.5, -2.5, -1.5, -0.5, 0.0, 0.5, 15.5, 16.0, 167.5, 4095.5, 4096.0, 9999.5, 1e6].map(F)) {
  print(`integer ${f(v)} ${P.IntegerParam(v, P.kMinTile, P.kMaxTile)} ${P.IntegerParam(v, -P.kMaxOrigin, P.kMaxOrigin)} `
    + `${P.IntegerParam(v, 0, P.kMaxSeed)}`);
}

// The patch.
const masks = [63, 1, 32, 5, 18, 41, 62, 0];
for (const seed of [0, 1, 2, 7, 69, 1234, 9999]) {
  for (let y = -3; y <= 3; ++y) {
    for (let x = -4; x <= 4; ++x) {
      for (const tw of [16, 168, 333]) {
        const px = 37 + x * tw;
        const py = -21 + y * (tw - 7);
        const key = P.TileKey(seed, px, py);
        print(`key ${seed} ${x} ${y} ${tw} ${key} ${masks.map((m) => P.TileFace(key, m)).join(' ')}`);
      }
    }
  }
}
for (let row = -13; row <= 13; ++row) for (let col = -13; col <= 13; ++col) print(`night ${col} ${row} ${P.NightIndex(col, row)}`);

// The timing.
const times = [0.0, 0.04, 1.0 / 60.0, 1.234, 2.2, 6.2, 12.5, 21.6, 24.65, 3600.5, 86400.25, 26000000.25, 315000000.125, 420000000.0];
for (const seed of [1, 7]) {
  for (let x = -2; x <= 6; ++x) {
    for (let y = -1; y <= 4; ++y) {
      const key = P.TileKey(seed, x * 168.0, y * 168.0);
      const c = P.TileClock.For(key);
      let line = `clock ${key}`;
      for (let i = 0; i < 3; ++i) line += ` ${d(c.candleCycles[i])} ${d(c.candleOffset[i])}`;
      print(`${line} ${d(c.goes)} ${d(c.expressionOffset)}`);
      for (const t of times) print(`level ${key} ${d(t)} ${d(c.Candle(t))} ${d(c.Expression(t))}`);
    }
  }
}

// The drawing.
function shape(list, face, e, index, s) {
  let line = `lantern ${face} ${d(e)} ${list} ${index} ${s.smooth ? 1 : 0} ${s.closed ? 1 : 0} ${s.pts.length}`;
  for (const p of s.pts) line += ` ${d(p.x)} ${d(p.y)}`;
  if (s.isEllipse) {
    const el = s.ellipse;
    line += ` ellipse ${d(el.cx)} ${d(el.cy)} ${d(el.rx)} ${d(el.ry)} ${d(el.rotation)} ${el.n}`;
  }
  print(line);
}
for (let face = -1; face < P.kFaceCount; ++face) {
  for (const e of [0.0, 0.125, 0.5, 0.875, 1.0]) {
    const l = P.MakeLantern(face, e);
    l.body.forEach((s, i) => shape('body', face, e, i, s));
    l.walls.forEach((s, i) => shape('wall', face, e, i, s));
    l.holes.forEach((s, i) => shape('hole', face, e, i, s));
  }
}
for (const n of [12, 16, 20, 22, 24, 40]) print(`smoothed ${n} ${d(P.SmoothedRadius(n))}`);

process.stdout.write(`${out.join('\n')}\n`);
