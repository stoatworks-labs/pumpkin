#!/usr/bin/env bash
#
# The browser demo's JavaScript port of the CPU half, and its picture, against
# the test card and the plugin.
#
#     demo/tools/crosscheck/crosscheck.sh [PKTEST]      exit 1 on any difference
#
# Four checks:
#
#   1. THE TEST CARD'S NUMBERS. port.mjs runs demo/port.js -- the file the page
#      loads -- against tools/fixtures/testcard.txt, the numbers the test card's
#      own TypeScript printed, with pktest --port's measures: every hash, rng
#      draw, tile key and face exactly; every candle, blink and point of every
#      face to 1e-12.
#   2. THE PLUGIN'S C++. printer.cpp drives the plugin's own code -- the
#      PumpkinPlugin constructor through the FFGL SDK's getters, Random.h,
#      Lantern.cpp, Geometry.cpp, Controls.cpp -- through a fixed scenario, and
#      run.mjs drives port.js through the same. compare.mjs requires the same
#      lines and words, every float the same 32 bits (the shape and vertex
#      tables the GPU reads, the host's floats), and every double within 1e-12,
#      and reports how many doubles differ at all: the arithmetic is the same
#      operations in the same order (printer.cpp, like the plugin's core, is
#      built -ffp-contract=off), and only V8's Math and Apple's libm can differ.
#   3. MUTANTS. Each of load.mjs's one-line changes to port.js must fail check
#      1 or check 2. A check that cannot fail is not a check.
#   4. THE PICTURE. pictures.mjs renders the page's renderer in headless Chrome
#      at the test card's seven pictures and holds it to them with pktest
#      --testcard's measures, and to the plugin's own pktest renders at the
#      same settings (Dark and None too), with two negative controls. Needs
#      Google Chrome and a built pktest (the argument, or $PKTEST); skipped,
#      loudly, without them.
#
# Run by tools/verify.sh. Needs node and a C++ compiler; the FFGL submodule.
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO="$(cd "$HERE/../../.." && pwd)"
CXX="${CXX:-c++}"
PKTEST="${1:-${PKTEST:-}}"
CHROME="${CHROME:-/Applications/Google Chrome.app/Contents/MacOS/Google Chrome}"

command -v node >/dev/null 2>&1 || { echo "crosscheck: node is not installed"; exit 2; }
[[ -f "$REPO/external/ffgl/CMakeLists.txt" ]] || { echo "crosscheck: FFGL SDK missing -- git submodule update --init --recursive"; exit 2; }

tmp="$(mktemp -d)"
trap 'rm -rf "$tmp"' EXIT

#---------------------------------------------------------------------------
# 1. The test card's numbers.
#---------------------------------------------------------------------------
if ! out=$( node "$HERE/port.mjs" ); then
	printf '%s\n' "$out" | sed 's/^/   /'
	echo "FAIL the port differs from the test card's own numbers"
	exit 1
fi
echo "ok   the port against the test card's own numbers (tools/fixtures/testcard.txt):"
printf '%s\n' "$out" | grep '^ok' | sed 's/^ok  /    /'

#---------------------------------------------------------------------------
# 2. The plugin's C++.
#---------------------------------------------------------------------------
version="$( sed -n 's/^[[:space:]]*VERSION \([0-9.]*\)$/\1/p' "$REPO/CMakeLists.txt" | head -1 )"
"$CXX" -std=c++17 -O2 -ffp-contract=off -DGL_SILENCE_DEPRECATION -DPUMPKIN_VERSION="\"$version\"" \
	-I "$REPO/source" -I "$REPO/external/ffgl/source/lib" "$HERE/printer.cpp" \
	"$REPO/source/Controls.cpp" "$REPO/source/Diag.cpp" "$REPO/source/Geometry.cpp" "$REPO/source/Lantern.cpp" \
	"$REPO/source/Pumpkin.cpp" "$REPO/source/Shaders.cpp" "$REPO/external/ffgl/source/lib/FFGLSDK.cpp" \
	-framework OpenGL -w -o "$tmp/printer"
"$tmp/printer" > "$tmp/cpp.txt"
node "$HERE/run.mjs" > "$tmp/js.txt"
lines=$( wc -l < "$tmp/cpp.txt" | tr -d ' ' )
[[ "$lines" -gt 5000 ]] || { echo "FAIL the plugin's side printed only $lines lines"; exit 1; }
if ! out=$( node "$HERE/compare.mjs" "$tmp/cpp.txt" "$tmp/js.txt" ); then
	printf '%s\n' "$out" | sed 's/^/   /'
	echo "FAIL the port differs from the plugin's C++"
	exit 1
fi
echo "ok   the port against the plugin's own C++, $lines lines:"
printf '%s\n' "$out" | grep -v '^PASS' | sed 's/^/  /'

#---------------------------------------------------------------------------
# 3. Mutants.
#---------------------------------------------------------------------------
count=$( node -e "import('$HERE/load.mjs').then((m) => console.log(m.MUTANTS.length))" )
caught=0
for (( i = 0; i < count; i++ )); do
	name=$( node -e "import('$HERE/load.mjs').then((m) => console.log(m.MUTANTS[$i][0]))" )
	if PORT_MUTANT=$i node "$HERE/port.mjs" >/dev/null 2>&1 \
	   && PORT_MUTANT=$i node "$HERE/run.mjs" > "$tmp/mutant.txt" \
	   && node "$HERE/compare.mjs" "$tmp/cpp.txt" "$tmp/mutant.txt" >/dev/null 2>&1; then
		echo "FAIL a mutant of the port passed both checks: $name"
		exit 1
	fi
	caught=$(( caught + 1 ))
done
echo "ok   $caught of $count mutants of the port caught (hash, rng, holes, blinks, candles, nights, smoothing, flattening, curves, Speed, the deal)"

#---------------------------------------------------------------------------
# 4. The picture.
#---------------------------------------------------------------------------
if [[ ! -x "$CHROME" ]]; then
	echo "   SKIPPED the picture: no Chrome at $CHROME (set CHROME=...)"
elif [[ -z "$PKTEST" || ! -x "$PKTEST" ]]; then
	echo "   SKIPPED the picture: no pktest (pass its path, or set PKTEST=...)"
else
	for mode in gpu swiftshader; do
		flag=(); [[ "$mode" == swiftshader ]] && flag=( --swiftshader )
		if ! out=$( PKTEST="$PKTEST" CHROME="$CHROME" node "$HERE/pictures.mjs" "${flag[@]}" 2>&1 ); then
			printf '%s\n' "$out" | grep -E 'FAIL|renderer' | sed 's/^/   /'
			echo "FAIL the page's picture ($mode)"
			exit 1
		fi
		echo "ok   the page's picture in headless Chrome ($mode): $( printf '%s\n' "$out" | sed -n 's/^ *WebGL2 renderer: //p' )"
		printf '%s\n' "$out" | grep -E '^ok   [a-z]+-[0-9]+x[0-9]+ +[0-9]+x[0-9]+ tiles' \
			| sed -E 's/.*: mean ([0-9.]+) codes.*/\1/' | sort -n | awk 'NR == 1 { lo = $1 } { hi = $1; n++ }
				END { printf "    against the test card'"'"'s %d pictures: mean %s to %s codes, every one within pktest --testcard'"'"'s measures\n", n, lo, hi }'
		printf '%s\n' "$out" | grep -E 'caught' | sed 's/^ok  */    negative control, /'
		printf '%s\n' "$out" | grep -E '^ +[0-9]+ of [0-9]+ pixels differ at all;' \
			| sed -E 's/^ +/    against the plugin'"'"'s own renders (pktest --out, Night, Dark and None): /'
	done
fi
