#!/usr/bin/env bash
#
# Mutation testing: change ONE character of the shipped code and require that a
# check fails. A check that still passes against a mutant was not looking at
# the code it claims to cover -- and a harness that compiled its own copy of
# the shaders would pass every GLSL mutant here.
#
# Each mutant is a copy of the tree in a temporary directory (the FFGL SDK is
# symlinked, not copied), built arm64-only, with only the named check run.
#
#     tools/mutate.sh
#
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# file | the exact text | its one-character mutant | the check that must fail | what it is
MUTANTS=(
	"source/Shaders.cpp|over( acc, vec3( 0.0 ), 0.45 * clamp( 0.5 - sd / px, 0.0, 1.0 ) );|over( acc, vec3( 0.0 ), 0.95 * clamp( 0.5 - sd / px, 0.0, 1.0 ) );|--testcard|GLSL: the shadow twice as dark, 4 -> 9"
	"source/Shaders.cpp|float t = clamp( ( length( q ) - 0.2 ) / 0.9, 0.0, 1.0 );|float t = clamp( ( length( q ) - 0.2 ) / 0.3, 0.0, 1.0 );|--testcard|GLSL: the glow a third the size, 9 -> 3"
	"source/Shaders.cpp|pix.y     = Raster.y - 1 - pix.y;|pix.y     = Raster.y - 2 - pix.y;|--tiles|GLSL: every row read one row off, 1 -> 2"
	"source/Random.h|h ^= h >> 13;|h ^= h >> 12;|--port|C++: the hash's shift-xor one bit short, 13 -> 12"
	"source/Lantern.cpp|const double len = 0.14;|const double len = 0.15;|--port|C++: a blink a little longer, 0.14 -> 0.15"
	"source/Lantern.cpp|h.ellipse.ry *= 0.8;|h.ellipse.ry *= 0.9;|--smoothing|C++: a carved ellipse's hole cut taller than its points, 0.8 -> 0.9"
	"source/Geometry.cpp|return ( std::cos( kPi / n ) + 1.0 - 0.5 * s * s ) * 0.5;|return ( std::cos( kPi / n ) + 1.0 - 0.6 * s * s ) * 0.5;|--smoothing|C++: the smoothed n-gon's radius misjudged, 0.5 -> 0.6"
	"source/Pumpkin.cpp|tile.lit            = tile.face < 0 ? 0.0 : flicker ? clocks[ i ].Candle( t ) : 0.8;|tile.lit            = tile.face < 0 ? 0.0 : flicker ? clocks[ i ].Candle( t ) : 0.3;|--testcard|C++: a steady candle burns at 0.3, not 0.8"
)

caught=0
for entry in "${MUTANTS[@]}"; do
	IFS='|' read -r file original mutant check what <<<"$entry"
	tree="$WORK/tree"
	rm -rf "$tree"
	mkdir -p "$tree/external"
	cp -R "$REPO/source" "$REPO/tools" "$REPO/cmake" "$REPO/CMakeLists.txt" "$tree/"
	ln -s "$REPO/external/ffgl" "$tree/external/ffgl"

	python3 - "$tree/$file" "$original" "$mutant" <<'PY'
import sys, pathlib
path, original, mutant = pathlib.Path(sys.argv[1]), sys.argv[2], sys.argv[3]
text = path.read_text()
if text.count(original) != 1:
    sys.exit(f"mutation target found {text.count(original)} times in {path}: '{original}'")
if sum(a != b for a, b in zip(original, mutant)) != 1 or len(original) != len(mutant):
    sys.exit("a mutant must differ by exactly one character")
path.write_text(text.replace(original, mutant))
PY

	printf '\n== mutant: %s\n' "$what"
	cmake -S "$tree" -B "$tree/build" -DCMAKE_BUILD_TYPE=Release -DCMAKE_OSX_ARCHITECTURES=arm64 >/dev/null
	cmake --build "$tree/build" --target pktest -j"$(sysctl -n hw.ncpu)" >/dev/null 2>&1
	# Under a time limit: a mutant can hang (dice's inverted loop), and a hang
	# is a caught mutant, not a stalled run. macOS has no timeout(1).
	if perl -e 'alarm shift; exec @ARGV' 600 "$tree/build/pktest" "$check" >"$WORK/log" 2>&1; then
		printf '   FAIL  %s still PASSES -- the check does not cover this code\n' "$check"
	else
		printf '   ok    %s fails against the mutant:\n' "$check"
		{ grep -E '^  FAIL' "$WORK/log" || true; } | head -2 | cut -c1-160 | sed 's/^/        /'
		caught=$(( caught + 1 ))
	fi
done

printf '\nmutants: %d, caught: %d\n' "${#MUTANTS[@]}" "$caught"
[[ "$caught" -eq "${#MUTANTS[@]}" ]]
