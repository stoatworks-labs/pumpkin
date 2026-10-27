#!/usr/bin/env node
/**
 * The test card's own answers, for pktest to hold the plugin to.
 *
 *     node tools/fixtures/testcard.mjs [~/Projects/video/test-card]
 *
 * Writes, next to this script:
 *
 *   testcard.txt      numbers its TypeScript printed: hash and rng, the face on
 *                     each tile for several seeds, grids and sets of faces, the
 *                     candle and expression at several times, and every face's
 *                     drawing at four expressions, point for point
 *   renders.txt       the configurations painted below, one a line
 *   <name>.png        each painted by the test card's own `drawPumpkins`, in
 *                     headless Chrome's software canvas
 *
 * Nothing of the test card is copied: its src/ is stripped of types into a
 * temporary folder (Node's own `stripTypeScriptTypes`, no packages), imported
 * from there by Node for the numbers and served to Chrome for the pictures.
 * The test card has no node_modules to need: its patterns import nothing but
 * each other.
 */
import { spawnSync } from 'node:child_process'
import { createServer } from 'node:http'
import { existsSync, mkdirSync, mkdtempSync, readFileSync, readdirSync, rmSync, statSync, writeFileSync } from 'node:fs'
import { stripTypeScriptTypes } from 'node:module'
import { tmpdir } from 'node:os'
import { dirname, extname, join, relative, resolve } from 'node:path'
import { fileURLToPath, pathToFileURL } from 'node:url'

const here = dirname(fileURLToPath(import.meta.url))
const card = resolve(process.argv[2] ?? join(process.env.HOME, 'Projects/video/test-card'))
const chrome = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome'
if (!existsSync(join(card, 'src/patterns/halloween.ts'))) throw new Error(`no test card at ${card}`)

//---------------------------------------------------------------------------
// The test card's src/, as plain JavaScript modules.
//---------------------------------------------------------------------------
const work = mkdtempSync(join(tmpdir(), 'pumpkin-testcard-'))
const web = join(work, 'web')
function convert(dir) {
  for (const name of readdirSync(dir)) {
    const path = join(dir, name)
    if (statSync(path).isDirectory()) {
      convert(path)
      continue
    }
    if (extname(name) !== '.ts' || name.endsWith('.test.ts') || name.endsWith('.d.ts')) continue
    let js
    try {
      js = stripTypeScriptTypes(readFileSync(path, 'utf8'), { mode: 'strip' })
    } catch {
      // Syntax that needs a compiler (idle/gl.ts's parameter properties) is
      // only in files the pumpkins never reach; an import of one fails loudly.
      continue
    }
    // Vite resolves extensionless imports; a browser and Node do not.
    js = js.replace(/(from\s+|import\s*\(\s*)(['"])(\.{1,2}\/[^'"]+)\2/g, (all, lead, q, spec) => {
      if (/\.[cm]?js$/.test(spec)) return all
      const target = resolve(dirname(path), spec)
      const ext = existsSync(target + '.ts') ? '.js' : existsSync(join(target, 'index.ts')) ? '/index.js' : ''
      return `${lead}${q}${spec}${ext}${q}`
    })
    const out = join(web, relative(join(card, 'src'), path)).replace(/\.ts$/, '.js')
    mkdirSync(dirname(out), { recursive: true })
    writeFileSync(out, js)
  }
}
convert(join(card, 'src'))
writeFileSync(join(web, 'package.json'), '{ "type": "module" }\n')

const P = (f) => pathToFileURL(join(web, 'patterns', f)).href
const random = await import(P('random.js'))
const hal = await import(P('halloween.js'))
const art = await import(P('art/halloween.js'))
const FACES = art.PUMPKIN_FACES
const faceIndex = (f) => (f === null ? -1 : FACES.indexOf(f))
const maskOf = (faces) => faces.reduce((m, f) => m | (1 << FACES.indexOf(f)), 0)
const hex = (s) => (s ? s.replace('#', '') : '-')

const commit = spawnSync('git', ['-C', card, 'rev-parse', '--short', 'HEAD'], { encoding: 'utf8' }).stdout.trim()
const lines = [`# The test card's own numbers, from ${card} at ${commit}, by tools/fixtures/testcard.mjs. Do not edit.`]

//---------------------------------------------------------------------------
// The randomness, raw.
//---------------------------------------------------------------------------
for (const v of [[1], [1, 2], [7, 168, 336, 0], [1234, -168, 0, 0], [0xffffffff, 5], [3.7, -0.5, 1e10]])
  lines.push(`hash ${v.length} ${v.join(' ')} ${random.hash(...v)}`)
for (const s of [0, 1, 99, 0x80000000, 4000000000]) {
  const r = random.rng(s)
  lines.push(`rng ${s} ${[r(), r(), r(), r(), r()].join(' ')}`)
}

//---------------------------------------------------------------------------
// Which face on which tile: keyed on the tile's place on the surface, with
// the view at the origin and localKey 0 (a span).
//---------------------------------------------------------------------------
const env = { view: { x: 0, y: 0 }, localKey: 0 }
const sets = [FACES, ['happy'], ['fierce', 'sly'], ['classic', 'surprised', 'goofy'], []]
lines.push(`masks ${sets.length} ${sets.map(maskOf).join(' ')}`)
for (const seed of [0, 1, 2, 77, 9999])
  for (const [tw, th, ox, oy] of [[168, 168, 0, 0], [240, 270, 0, 0], [100, 60, 37, -21]])
    for (let row = -1; row < 4; row++)
      for (let col = -1; col < 5; col++) {
        const tile = { x: ox + col * tw, y: oy + row * th }
        const key = hal.tileKey(seed, tile, env)
        const faces = sets.map((f) => faceIndex(hal.tileFace({ seed, faces: f, flicker: true, expressions: true }, key)))
        lines.push(`tile ${seed} ${tile.x} ${tile.y} ${key} ${faces.join(' ')}`)
      }

//---------------------------------------------------------------------------
// Candles and faces, endless (live output: no loop to fit them to).
//---------------------------------------------------------------------------
const keys = [0, 1, 12345, 0xdeadbeef, 2147483648, hal.tileKey(1, { x: 168, y: 0 }, env)]
const times = [0, 0.04, 1.234, 10, 77.7, 3600.5, 26e6 + 0.25, 3.15e8 + 0.125]
for (const key of keys) for (const t of times) lines.push(`timing ${key} ${t} ${hal.candleLevel(t, 10, key, true)} ${hal.expressionLevel(t, 10, key, true)}`)

//---------------------------------------------------------------------------
// Every face's drawing.
//---------------------------------------------------------------------------
const pts = (p) => `${p.length} ${p.map(([x, y]) => `${x} ${y}`).join(' ')}`
for (const face of [null, ...FACES])
  for (const e of [0, 0.25, 0.5, 1]) {
    const p = art.pumpkin(face, e)
    p.body.forEach((s, i) =>
      lines.push(`shape ${faceIndex(face)} ${e} body ${i} ${s.smooth !== false ? 1 : 0} ${s.closed !== false ? 1 : 0} ${hex(s.fill)} ${s.alpha ?? 1} ${hex(s.stroke)} ${s.strokeWidth ?? 0} ${pts(s.pts)}`),
    )
    for (const [list, holes] of [['wall', p.walls], ['hole', p.holes]])
      holes.forEach((h, i) => lines.push(`shape ${faceIndex(face)} ${e} ${list} ${i} ${h.smooth ? 1 : 0} 1 - 1 - 0 ${pts(h.pts)}`))
  }
lines.push(`extent ${art.PUMPKIN_TOP} ${art.PUMPKIN_BOTTOM}`)
writeFileSync(join(here, 'testcard.txt'), lines.join('\n') + '\n')

//---------------------------------------------------------------------------
// The pictures. A time is chosen for each so that some faces are part way
// through a blink, a wink or a chomp: the expressions are brief.
//---------------------------------------------------------------------------
const draw = await import(P('draw.js'))
function tilesOf(c) {
  const opts = { source: 'uniform', tileWidthPx: c.tw, tileHeightPx: c.th, originXPx: c.ox, originYPx: c.oy }
  return draw.ledTiles(c.w, c.h, opts, [], { x: 0, y: 0 }, 1)
}
function busiest(c, from, to) {
  // The time in [from, to) with the most tiles mid-expression, in steps of 0.05 s.
  let best = from
  let most = -1
  for (let t = from; t < to; t += 0.05) {
    let n = 0
    for (const tile of tilesOf(c)) {
      const key = hal.tileKey(c.seed, tile, env)
      if (!hal.tileFace(c, key)) continue
      const e = hal.expressionLevel(t, 10, key, true)
      if (e > 0.2 && e < 0.95) n++
    }
    if (n > most) [best, most] = [t, n]
  }
  return Math.round(best * 100) / 100
}
const configs = [
  { name: 'patch-640x360', w: 640, h: 360, tw: 160, th: 180, ox: 0, oy: 0, seed: 1, faces: FACES, flicker: true, expressions: true },
  { name: 'close-960x540', w: 960, h: 540, tw: 320, th: 270, ox: 0, oy: 0, seed: 1, faces: FACES, flicker: true, expressions: true },
  { name: 'wall-1280x720', w: 1280, h: 720, tw: 168, th: 168, ox: 0, oy: 0, seed: 1, faces: FACES, flicker: true, expressions: true },
  { name: 'small-320x180', w: 320, h: 180, tw: 64, th: 90, ox: 37, oy: -21, seed: 2, faces: ['fierce', 'sly', 'goofy'], flicker: true, expressions: true },
  { name: 'tiny-320x180', w: 320, h: 180, tw: 40, th: 45, ox: 0, oy: 0, seed: 7, faces: FACES, flicker: true, expressions: true },
  { name: 'plain-320x180', w: 320, h: 180, tw: 80, th: 90, ox: 0, oy: 0, seed: 1, faces: [], flicker: true, expressions: true },
  { name: 'steady-640x360', w: 640, h: 360, tw: 160, th: 120, ox: -40, oy: 30, seed: 3, faces: FACES, flicker: false, expressions: false },
]
for (const c of configs) c.t = c.expressions && c.faces.length ? busiest(c, 1, 31) : 12.5
const page = `<!doctype html><meta charset="utf-8"><pre id="out"></pre><script type="module">
const pre = document.getElementById('out')
try {
const { drawPumpkins } = await import('./patterns/halloween.js')
const { ledTiles } = await import('./patterns/draw.js')
const configs = ${JSON.stringify(configs)}
const out = {}
for (const c of configs) {
  const canvas = document.createElement('canvas')
  canvas.width = c.w
  canvas.height = c.h
  const ctx = canvas.getContext('2d')
  const opts = { source: 'uniform', tileWidthPx: c.tw, tileHeightPx: c.th, originXPx: c.ox, originYPx: c.oy }
  // env.ts's plainEnv, written out: importing env.ts would pull in the idle
  // savers, which only a compiler can strip.
  const env = { t: c.t, loopSeconds: 10, endless: true, world: { widthPx: c.w, heightPx: c.h }, view: { x: 0, y: 0 },
    mmPerPx: 2.6, pitchAssumed: true, tiles: ledTiles(c.w, c.h, opts, [], { x: 0, y: 0 }, 1), localKey: 0, image: null }
  drawPumpkins(ctx, c.w, c.h, { seed: c.seed, faces: c.faces, flicker: c.flicker, expressions: c.expressions }, env)
  out[c.name] = canvas.toDataURL('image/png')
}
pre.textContent = JSON.stringify(out)
} catch (e) {
  pre.textContent = 'ERROR ' + (e && e.stack ? e.stack : e)
}
</script>`
writeFileSync(join(web, 'page.html'), page)

const server = createServer((req, res) => {
  const path = join(web, decodeURIComponent(new URL(req.url, 'http://x').pathname))
  if (!path.startsWith(web) || !existsSync(path) || statSync(path).isDirectory()) {
    res.writeHead(404).end()
    return
  }
  const type = path.endsWith('.html') ? 'text/html' : path.endsWith('.js') ? 'text/javascript' : 'application/octet-stream'
  res.writeHead(200, { 'content-type': type }).end(readFileSync(path))
})
await new Promise((ok) => server.listen(0, '127.0.0.1', ok))
const url = `http://127.0.0.1:${server.address().port}/page.html`
const profile = join(work, 'profile')
const run = await new Promise((ok) => {
  // Software canvas, sRGB, one device pixel per CSS pixel: the picture the
  // test card's Download PNG would give on a machine with no GPU.
  const args = ['--headless=new', '--disable-gpu', '--disable-accelerated-2d-canvas', '--force-color-profile=srgb',
    '--force-device-scale-factor=1', '--no-first-run', '--no-default-browser-check', `--user-data-dir=${profile}`,
    '--virtual-time-budget=20000', '--dump-dom', url]
  import('node:child_process').then(({ execFile }) =>
    execFile(chrome, args, { maxBuffer: 256 * 1024 * 1024 }, (error, stdout, stderr) => ok({ error, stdout, stderr })),
  )
})
server.close()
const found = /<pre id="out">([\s\S]*?)<\/pre>/.exec(run.stdout)
if (!found || !found[1].trim()) throw new Error(`Chrome painted nothing: ${run.error ?? ''}`)
if (found[1].startsWith('ERROR')) throw new Error(`the page failed in Chrome: ${found[1]}`)
const pictures = JSON.parse(found[1].replace(/&quot;/g, '"').replace(/&amp;/g, '&'))
const manifest = [`# Painted by the test card's drawPumpkins (${commit}) in headless Chrome, by tools/fixtures/testcard.mjs. Do not edit.`,
  '# name width height tileW tileH originX originY seed faceMask flicker expressions t']
for (const c of configs) {
  writeFileSync(join(here, `${c.name}.png`), Buffer.from(pictures[c.name].split(',')[1], 'base64'))
  manifest.push([c.name, c.w, c.h, c.tw, c.th, c.ox, c.oy, c.seed, maskOf(c.faces), c.flicker ? 1 : 0, c.expressions ? 1 : 0, c.t].join(' '))
}
const version = spawnSync(chrome, ['--version'], { encoding: 'utf8' }).stdout.trim()
manifest.splice(1, 0, `# ${version}`)
writeFileSync(join(here, 'renders.txt'), manifest.join('\n') + '\n')
rmSync(work, { recursive: true, force: true })
console.log(`testcard.txt: ${lines.length} lines; ${configs.length} renders (${version}) from ${commit}`)
