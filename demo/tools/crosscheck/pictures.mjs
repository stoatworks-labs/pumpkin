// The page's PICTURE, against the test card's own and against the plugin's.
//
// Serves demo/ on 127.0.0.1, opens render.html in headless Chrome (WebGL2:
// the page's own pumpkin.js, port.js and shader), and renders it at each of
// the test card's seven pictures (tools/fixtures/renders.txt: raster, tiles,
// origin, seed, faces, flicker, expressions, time). Then:
//
//   1. Against the test card's own picture, painted by its drawPumpkins in
//      Chrome (tools/fixtures/*.png), with the measures `pktest --testcard`
//      holds the plugin to, unchanged: mean difference at most 1 code value;
//      at most 0.5% of pixels more than 24 codes out; where the card's picture
//      is flat (its 3 x 3 block within 4 codes), at most 1 pixel in 10,000
//      more than 4 out and none more than 24. Required.
//   2. Against the plugin itself: `pktest --out` at the same settings and
//      story time (PKTEST=path/to/pktest), and at the same settings on Dark
//      and None, which the test card does not draw. Reported, and required to
//      pass the same measures: two GPU stacks (ANGLE's Metal against Apple's
//      OpenGL) need not round an edge the same way.
//   3. Negative controls, which must FAIL measure 1: the page a quarter of a
//      second early (pktest's own), and the next seed.
//
//     node demo/tools/crosscheck/pictures.mjs [--swiftshader]
//
// Needs Google Chrome (CHROME=... to point elsewhere). With --swiftshader,
// Chrome's software WebGL instead of this machine's GPU.

import { spawn, spawnSync } from 'node:child_process';
import { createServer } from 'node:http';
import { existsSync, mkdtempSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { dirname, extname, join, normalize } from 'node:path';
import { fileURLToPath } from 'node:url';
import { inflateSync } from 'node:zlib';

const here = dirname(fileURLToPath(import.meta.url));
const demo = join(here, '..', '..');
const fixtures = join(demo, '..', 'tools', 'fixtures');
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const PKTEST = process.env.PKTEST ?? '';
const swiftshader = process.argv.includes('--swiftshader');

//---------------------------------------------------------------------------
// PNG in: 8-bit RGB or RGBA, not interlaced (what Chrome and pktest write).
//---------------------------------------------------------------------------
function readPng(path) {
  const buf = readFileSync(path);
  let pos = 8;
  let width = 0;
  let height = 0;
  let type = 0;
  const idat = [];
  while (pos < buf.length) {
    const len = buf.readUInt32BE(pos);
    const kind = buf.toString('latin1', pos + 4, pos + 8);
    const data = buf.subarray(pos + 8, pos + 8 + len);
    if (kind === 'IHDR') {
      width = data.readUInt32BE(0);
      height = data.readUInt32BE(4);
      if (data[8] !== 8 || data[12] !== 0) throw new Error(`${path}: not 8-bit non-interlaced`);
      type = data[9];
    } else if (kind === 'IDAT') idat.push(data);
    pos += 12 + len;
  }
  const channels = type === 6 ? 4 : type === 2 ? 3 : 0;
  if (!channels) throw new Error(`${path}: colour type ${type}`);
  const raw = inflateSync(Buffer.concat(idat));
  const stride = width * channels;
  const rows = Buffer.alloc(height * stride);
  for (let y = 0; y < height; y++) {
    const filter = raw[y * (stride + 1)];
    const line = raw.subarray(y * (stride + 1) + 1, (y + 1) * (stride + 1));
    for (let x = 0; x < stride; x++) {
      const a = x >= channels ? rows[y * stride + x - channels] : 0;
      const b = y > 0 ? rows[(y - 1) * stride + x] : 0;
      const c = x >= channels && y > 0 ? rows[(y - 1) * stride + x - channels] : 0;
      let v = line[x];
      if (filter === 1) v += a;
      else if (filter === 2) v += b;
      else if (filter === 3) v += (a + b) >> 1;
      else if (filter === 4) {
        const p = a + b - c;
        const pa = Math.abs(p - a);
        const pb = Math.abs(p - b);
        const pc = Math.abs(p - c);
        v += pa <= pb && pa <= pc ? a : pb <= pc ? b : c;
      }
      rows[y * stride + x] = v & 0xff;
    }
  }
  const rgba = new Uint8Array(width * height * 4);
  for (let p = 0; p < width * height; p++) {
    for (let k = 0; k < 4; k++) rgba[p * 4 + k] = k < channels ? rows[p * channels + k] : 255;
  }
  return { width, height, rgba };
}

//---------------------------------------------------------------------------
// pktest --testcard's measures, on two RGBA pictures of one size.
//---------------------------------------------------------------------------
function measure(got, want, w, h) {
  const pixels = w * h;
  const diff = new Int32Array(pixels);
  let sum = 0;
  let far = 0;
  let worst = 0;
  let differ = 0;
  for (let p = 0; p < pixels; p++) {
    let d = 0;
    for (let c = 0; c < 3; c++) {
      const one = Math.abs(got[p * 4 + c] - want[p * 4 + c]);
      sum += one;
      d = Math.max(d, one);
    }
    diff[p] = d;
    far += d > 24 ? 1 : 0;
    differ += d > 0 ? 1 : 0;
    worst = Math.max(worst, d);
  }
  let flatCount = 0;
  let flatWrong = 0;
  let flatFar = 0;
  let flatWorst = 0;
  for (let y = 1; y + 1 < h; y++) {
    for (let x = 1; x + 1 < w; x++) {
      const p = y * w + x;
      let flat = true;
      for (let dy = -1; dy <= 1 && flat; dy++) {
        for (let dx = -1; dx <= 1 && flat; dx++) {
          for (let c = 0; c < 3 && flat; c++) flat = Math.abs(want[(p + dy * w + dx) * 4 + c] - want[p * 4 + c]) <= 4;
        }
      }
      if (!flat) continue;
      flatCount++;
      flatWorst = Math.max(flatWorst, diff[p]);
      flatWrong += diff[p] > 4 ? 1 : 0;
      flatFar += diff[p] > 24 ? 1 : 0;
    }
  }
  const mean = sum / (pixels * 3);
  const farPct = (100 * far) / pixels;
  const pass = mean <= 1.0 && farPct <= 0.5 && flatWrong * 10000 <= flatCount && flatFar === 0;
  const text = `mean ${mean.toFixed(3)} codes (1), ${farPct.toFixed(3)}% > 24 out (0.5%); flat: ${flatWrong} of ${flatCount} > 4 out `
    + `(1 in 10,000), ${flatFar} > 24 (0); worst ${worst}, ${((100 * differ) / pixels).toFixed(2)}% of pixels differ at all`;
  return { pass, text, mean, worst, differ };
}

//---------------------------------------------------------------------------
// A static server for demo/, and Chrome over the DevTools protocol.
//---------------------------------------------------------------------------
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css' };
const server = createServer((req, res) => {
  const path = normalize(join(demo, decodeURIComponent(new URL(req.url, 'http://x').pathname)));
  if (!path.startsWith(demo) || !existsSync(path)) {
    res.writeHead(404);
    res.end();
    return;
  }
  res.writeHead(200, { 'Content-Type': TYPES[extname(path)] ?? 'application/octet-stream' });
  res.end(readFileSync(path));
});
await new Promise((resolve) => server.listen(Number(process.env.PICTURES_PORT ?? 0), '127.0.0.1', resolve));
const origin = `http://127.0.0.1:${server.address().port}`;

const profile = mkdtempSync(join(tmpdir(), 'pumpkin-pictures-'));
const flags = swiftshader ? ['--use-gl=angle', '--use-angle=swiftshader', '--enable-unsafe-swiftshader'] : [];
const chrome = spawn(CHROME, ['--headless=new', '--no-first-run', '--no-default-browser-check', ...flags,
  `--user-data-dir=${profile}`, '--remote-debugging-port=0', 'about:blank'], { stdio: 'ignore' });

let failed = 0;
const say = (ok, text) => {
  console.log(`${ok ? 'ok  ' : 'FAIL'} ${text}`);
  if (!ok) failed++;
};

async function devtools() {
  const file = join(profile, 'DevToolsActivePort');
  for (let i = 0; i < 150; i++) {
    if (existsSync(file)) {
      const port = readFileSync(file, 'utf8').split('\n')[0];
      if (port) {
        const list = await (await fetch(`http://127.0.0.1:${port}/json/list`)).json();
        const page = list.find((t) => t.type === 'page');
        if (page) return page.webSocketDebuggerUrl;
      }
    }
    await new Promise((r) => setTimeout(r, 200));
  }
  throw new Error('Chrome never opened its DevTools port');
}

let socket;
let nextId = 0;
const pending = new Map();
function call(method, params = {}) {
  const id = ++nextId;
  socket.send(JSON.stringify({ id, method, params }));
  return new Promise((resolve, reject) => pending.set(id, { resolve, reject }));
}
async function evaluate(expression) {
  const r = await call('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true });
  if (r.exceptionDetails) throw new Error(r.exceptionDetails.exception?.description ?? r.exceptionDetails.text);
  return r.result.value;
}

const errors = [];
try {
  socket = new WebSocket(await devtools());
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  socket.onmessage = (event) => {
    const msg = JSON.parse(event.data);
    if (msg.id && pending.has(msg.id)) {
      const { resolve, reject } = pending.get(msg.id);
      pending.delete(msg.id);
      if (msg.error) reject(new Error(msg.error.message));
      else resolve(msg.result);
    } else if (msg.method === 'Runtime.exceptionThrown') {
      errors.push(msg.params.exceptionDetails.exception?.description ?? msg.params.exceptionDetails.text);
    } else if (msg.method === 'Runtime.consoleAPICalled' && msg.params.type === 'error') {
      errors.push(msg.params.args.map((a) => a.value ?? a.description).join(' '));
    }
  };
  await call('Runtime.enable');
  await call('Page.enable');
  await call('Page.navigate', { url: `${origin}/tools/crosscheck/render.html` });
  for (let i = 0; i < 100 && (await evaluate('window.ready === true').catch(() => false)) !== true; i++) {
    await new Promise((r) => setTimeout(r, 100));
  }
  if ((await evaluate('window.ready === true')) !== true) throw new Error(`render.html never became ready: ${errors.join('; ')}`);
  console.log(`     WebGL2 renderer: ${await evaluate('window.renderer')}`);

  const render = async (o) => {
    const r = await evaluate(`JSON.stringify(window.renderAt(${JSON.stringify(o)}))`);
    const out = JSON.parse(r);
    if (out.error !== 0) throw new Error(`GL error 0x${out.error.toString(16)}`);
    return { ...out, rgba: new Uint8Array(Buffer.from(out.pixels, 'base64')) };
  };

  const renders = readFileSync(join(fixtures, 'renders.txt'), 'utf8').split('\n')
    .filter((l) => l.trim() && !l.startsWith('#'))
    .map((l) => {
      const [name, w, h, tw, th, ox, oy, seed, mask, flicker, expressions, t] = l.trim().split(/\s+/);
      return { name, w: +w, h: +h, tw: +tw, th: +th, ox: +ox, oy: +oy, seed: +seed, mask: +mask,
        flicker: flicker === '1', expressions: expressions === '1', t: +t };
    });
  say(renders.length >= 5, `${renders.length} of the test card's renders listed in renders.txt`);

  const pktest = (o, background, alpha) => {
    const out = join(profile, `pk-${o.name}-${background}${alpha ? '-alpha' : ''}.png`);
    const faces = ['Classic', 'Fierce', 'Happy', 'Surprised', 'Sly', 'Goofy'];
    const args = ['--out', out, '--size', `${o.w}x${o.h}`, '--frames', String(Math.round(o.t * 60) + 1),
      '--set', `Tile Width=${o.tw}`, '--set', `Tile Height=${o.th}`, '--set', `Origin X=${o.ox}`, '--set', `Origin Y=${o.oy}`,
      '--set', `Seed=${o.seed}`, ...faces.flatMap((n, f) => ['--set', `${n}=${(o.mask >> f) & 1}`]),
      '--set', `Flicker=${o.flicker ? 1 : 0}`, '--set', `Expressions=${o.expressions ? 1 : 0}`,
      '--set', `Background=${background}`, ...(alpha ? ['--alpha'] : [])];
    const run = spawnSync(PKTEST, args, { encoding: 'utf8' });
    if (run.status !== 0) throw new Error(`pktest ${args.join(' ')}: ${run.stderr || run.stdout}`);
    return readPng(out).rgba;
  };

  console.log('\n  the page against the test card\'s own pictures (pktest --testcard\'s measures):');
  for (const o of renders) {
    const want = readPng(join(fixtures, `${o.name}.png`));
    const got = await render(o);
    const m = measure(got.rgba, want.rgba, o.w, o.h);
    say(m.pass, `${o.name.padEnd(15)} ${o.w}x${o.h} tiles ${o.tw}x${o.th}, seed ${o.seed}, t ${o.t}: ${m.text}`);
  }

  console.log('\n  negative controls, which must fail against the test card:');
  for (const [what, change] of [['a quarter of a second early', (o) => ({ ...o, t: o.t - 0.25 })],
    ['the next seed', (o) => ({ ...o, seed: o.seed + 1 })]]) {
    let caught = 0;
    for (const o of renders) {
      const want = readPng(join(fixtures, `${o.name}.png`));
      const got = await render(change(o));
      caught += measure(got.rgba, want.rgba, o.w, o.h).pass ? 0 : 1;
    }
    // A plain patch (no face ticked) is the same whatever the seed, and a plain
    // or steady one is the same at every instant: those cannot be caught.
    const need = renders.filter((o) => o.mask && (what === 'the next seed' || o.flicker || o.expressions)).length;
    say(caught >= need, `${what}: ${caught} of ${renders.length} renders caught (${need} must be)`);
  }

  if (PKTEST) {
    console.log('\n  the page against the plugin itself (pktest --out, the same settings and story time):');
    let worstMean = 0;
    let worst = 0;
    let differ = 0;
    let total = 0;
    for (const o of renders) {
      for (const background of ['Night', 'Dark', 'None']) {
        if (background !== 'Night' && !['wall-1280x720', 'small-320x180'].includes(o.name)) continue;
        const index = { Night: 0, Dark: 1, None: 2 }[background];
        const got = await render({ ...o, background: index });
        const want = pktest(o, background, false);
        const m = measure(got.rgba, want, o.w, o.h);
        worstMean = Math.max(worstMean, m.mean);
        worst = Math.max(worst, m.worst);
        differ += m.differ;
        total += o.w * o.h;
        let alphaText = '';
        if (background === 'None') {
          const wantAlpha = pktest(o, background, true);
          let alphaWorst = 0;
          for (let p = 3; p < got.rgba.length; p += 4) alphaWorst = Math.max(alphaWorst, Math.abs(got.rgba[p] - wantAlpha[p]));
          alphaText = `; alpha within ${alphaWorst} codes`;
          if (alphaWorst > 2) m.pass = false;
        }
        say(m.pass, `${o.name.padEnd(15)} ${background.padEnd(5)}: ${m.text}${alphaText}`);
      }
    }
    console.log(`     ${differ} of ${total} pixels differ at all; worst mean ${worstMean.toFixed(4)} codes, worst pixel ${worst} code(s)`);
  } else {
    console.log('\n  skipped: the page against pktest -- set PKTEST=path/to/pktest');
  }
  say(errors.length === 0, `no errors in the page${errors.length ? `: ${errors.join('; ')}` : ''}`);
} catch (error) {
  say(false, error.message);
} finally {
  socket?.close();
  chrome.kill();
  server.close();
  await new Promise((r) => setTimeout(r, 300));
  rmSync(profile, { recursive: true, force: true });
}

console.log(failed ? `\n${failed} FAILED` : '\nPASS');
process.exit(failed ? 1 : 0);
