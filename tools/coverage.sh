#!/usr/bin/env bash
# Coverage over lib/deck_core/src, src/ and src/bringup (spikes excluded).
# Runs every test suite in its env, saves one gcovr JSON per suite (PlatformIO rebuilds
# between suites), merges them, prints a summary and fails below MIN_LINE / MIN_BRANCH.
#   tools/coverage.sh                    gate at 95 % / 95 %
#   MIN_LINE=0 MIN_BRANCH=0 tools/coverage.sh   report only
set -euo pipefail
cd "$(dirname "$0")/.."

PIO=${PIO:-pio}
MIN_LINE=${MIN_LINE:-95}
MIN_BRANCH=${MIN_BRANCH:-95}
MAX_EXCL=10
OUT=.pio/coverage
if [ "$(uname)" = Darwin ]; then GCOV_EXE="xcrun llvm-cov gcov"; else GCOV_EXE=gcov; fi

COMMON=(--root . --filter 'lib/deck_core/src/' --filter 'src/' --exclude 'src/spike_'
        --exclude-unreachable-branches --exclude-throw-branches)

env_defined() { grep -q "^\[env:$1\]" platformio.ini; }

rm -rf "$OUT"
mkdir -p "$OUT/json"

for dir in test/native/test_*/; do
  suite="native/$(basename "$dir")"
  case "$suite" in
    native/test_sim_bringup) env=sim_bringup ;;
    native/test_sim_*) env=sim ;;
    *) env=native_cov ;;
  esac
  if ! env_defined "$env"; then
    echo "skip $suite (env $env not defined yet)"
    continue
  fi
  find ".pio/build/$env" -name '*.gcda' -delete 2>/dev/null || true
  "$PIO" test -e "$env" -f "$suite"
  gcovr "${COMMON[@]}" --gcov-executable "$GCOV_EXE" --object-directory ".pio/build/$env" \
    --json "$OUT/json/${env}_$(basename "$dir").json" ".pio/build/$env"
done

TRACES=()
for f in "$OUT"/json/*.json; do TRACES+=(--add-tracefile "$f"); done
gcovr "${TRACES[@]}" --txt "$OUT/summary.txt" --html-details "$OUT/index.html" \
  --print-summary --fail-under-line "$MIN_LINE" --fail-under-branch "$MIN_BRANCH"

excl=$( (grep -rE 'GCOVR_EXCL' src lib || true) | grep -vc '^src/spike_' || true)
echo "GCOVR_EXCL markers: $excl (max $MAX_EXCL)"
if [ "$excl" -gt "$MAX_EXCL" ]; then
  echo "too many GCOVR_EXCL markers"
  exit 1
fi
