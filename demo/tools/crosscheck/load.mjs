// Loads demo/port.js for the cross-check: the file itself, or, with
// PORT_MUTANT=<n>, the same text with one deliberate change (MUTANTS below),
// so crosscheck.sh can show every check here fails on a port that is wrong.
// A check that cannot fail is not a check (the repo's tools/mutate.sh, for
// the port instead of the plugin).

import { readFileSync } from 'node:fs';
import { dirname, join } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const here = dirname(fileURLToPath(import.meta.url));
export const PORT_PATH = join(here, '..', '..', 'port.js');

// Each: what it breaks, the text in port.js, what it becomes. Each one must
// be caught by port.mjs (the test card's numbers) or by compare.mjs (the
// plugin's C++).
export const MUTANTS = [
  ['the hash\'s shift', 'h = (h ^ (h >>> 13)) >>> 0;', 'h = (h ^ (h >>> 12)) >>> 0;'],
  ['mulberry32\'s increment', 'this.a = (this.a + 0x6d2b79f5) >>> 0;', 'this.a = (this.a + 0x6d2b79f7) >>> 0;'],
  ['a hole\'s height', 'h.pts = scaleAbout(wall.pts, 0.88, 0.8, c);', 'h.pts = scaleAbout(wall.pts, 0.88, 0.81, c);'],
  ['the blink\'s length', 'const len = 0.14;', 'const len = 0.15;'],
  ['a candle\'s flicker', 'const kCandleAmp = [0.13, 0.09, 0.05];', 'const kCandleAmp = [0.13, 0.09, 0.06];'],
  ['the nights by column and row', 'const m = (col + row * 2) % 6;', 'const m = (col * 2 + row) % 6;'],
  ['the smoothing radius', '(Math.cos(kPi / n) + 1.0 - 0.5 * s * s) * 0.5', '(Math.cos(kPi / n) + 1.0 - 0.5 * s) * 0.5'],
  ['the flattening bound', 'const kFlattenTolerancePx = 0.1;', 'const kFlattenTolerancePx = 0.2;'],
  ['a quadratic\'s middle weight', 'const b = 2 * u * (1 - u);', 'const b = 2 * u * (1.01 - u);'],
  ['the Speed curve', 'const kSpeedHigh = 4.0;', 'const kSpeedHigh = 4.5;'],
  ['a face dealt from the wrong draw', 'return faces[Math.min(faces.length - 1, Math.floor(r.next() * faces.length))];',
    'return faces[Math.min(faces.length - 1, Math.floor(r.next() * faces.length + 0.5))];'],
];

export async function loadPort() {
  const which = process.env.PORT_MUTANT;
  if (which === undefined || which === '') return import(pathToFileURL(PORT_PATH).href);
  const mutant = MUTANTS[Number(which)];
  if (!mutant) throw new Error(`no mutant ${which}`);
  const source = readFileSync(PORT_PATH, 'utf8');
  const count = source.split(mutant[1]).length - 1;
  if (count !== 1) throw new Error(`mutant "${mutant[0]}": its text is in port.js ${count} times, not once`);
  return import(`data:text/javascript,${encodeURIComponent(source.replace(mutant[1], mutant[2]))}`);
}
