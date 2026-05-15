#!/usr/bin/env bash
# Benchmark standard compressors (gzip, zstd, xz, lz4, brotli, bzip2) on the
# same set of files used by loxc_bench2, producing a CSV directly comparable
# with the loxc_bench2 CSV.
#
# Usage:
#   tools/bench_baselines.sh <suite.list> <out.csv> [iterations]
#
# iterations defaults to 25. We measure encode and decode wall time for each
# tool/level combo, using piping to /dev/null so we don't pollute results
# with filesystem write latency. Each file is read once into memory by the
# tool (they all support stdin), giving us a fair encode-from-RAM measurement
# similar to how loxc_bench2 reads the input into a buffer up front.

set -euo pipefail

SUITE="${1:?suite list required}"
OUT="${2:?output CSV path required}"
ITER="${3:-25}"

mkdir -p "$(dirname "$OUT")"

echo "path,raw_bytes,tool,level,encoded_bytes,ratio_pct,encode_median_ms,decode_median_ms,encode_mbps,decode_mbps" > "$OUT"

# Build the file list once, ignoring comments and blanks.
mapfile -t FILES < <(grep -vE '^\s*(#|$)' "$SUITE")

# Tool/level pairs we measure.
declare -a TOOLS=(
  "gzip:1"   "gzip:6"   "gzip:9"
  "zstd:1"   "zstd:3"   "zstd:9"   "zstd:19"
  "xz:1"     "xz:6"     "xz:9"
  "lz4:1"    "lz4:9"
  "brotli:1" "brotli:6" "brotli:11"
  "bzip2:1"  "bzip2:9"
  "raw:0"
)

median_of() {
  # stdin = whitespace-separated floats, prints median
  python3 -c "
import sys,statistics
xs=[float(x) for x in sys.stdin.read().split() if x]
print(f'{statistics.median(xs):.4f}' if xs else '0')
"
}

bench_pair() {
  local file="$1" tool="$2" level="$3"
  local raw enc enc_path
  raw=$(wc -c < "$file" | tr -d '[:space:]')

  enc_path=$(mktemp)
  case "$tool" in
    gzip)   gzip   -"$level" -c "$file" > "$enc_path" ;;
    zstd)   zstd   -"$level" -q -c "$file" > "$enc_path" ;;
    xz)     xz     -"$level" -c "$file" > "$enc_path" ;;
    lz4)    lz4    -"$level" -c -q "$file" > "$enc_path" 2>/dev/null ;;
    brotli) brotli --quality="$level" -c "$file" > "$enc_path" ;;
    bzip2)  bzip2  -"$level" -c "$file" > "$enc_path" ;;
    raw)    cat "$file" > "$enc_path" ;;
  esac
  enc=$(wc -c < "$enc_path" | tr -d '[:space:]')

  local ratio enc_times="" dec_times=""
  ratio=$(python3 -c "print(f'{100.0*$enc/$raw:.4f}' if $raw else '0')")

  # Encode timings: read file, pipe through compressor to /dev/null.
  for ((i=0; i<ITER; i++)); do
    local t0 t1 dt
    t0=$(date +%s%N)
    case "$tool" in
      gzip)   gzip   -"$level" -c "$file" > /dev/null ;;
      zstd)   zstd   -"$level" -q -c "$file" > /dev/null ;;
      xz)     xz     -"$level" -c "$file" > /dev/null ;;
      lz4)    lz4    -"$level" -c -q "$file" > /dev/null 2>/dev/null ;;
      brotli) brotli --quality="$level" -c "$file" > /dev/null ;;
      bzip2)  bzip2  -"$level" -c "$file" > /dev/null ;;
      raw)    cat "$file" > /dev/null ;;
    esac
    t1=$(date +%s%N)
    dt=$(python3 -c "print(f'{($t1-$t0)/1e6:.4f}')")
    enc_times="$enc_times $dt"
  done

  # Decode timings: read the encoded file we already produced.
  for ((i=0; i<ITER; i++)); do
    local t0 t1 dt
    t0=$(date +%s%N)
    case "$tool" in
      gzip)   gzip   -d -c "$enc_path" > /dev/null ;;
      zstd)   zstd   -d -q -c "$enc_path" > /dev/null ;;
      xz)     xz     -d -c "$enc_path" > /dev/null ;;
      lz4)    lz4    -d -c -q "$enc_path" > /dev/null 2>/dev/null ;;
      brotli) brotli -d -c "$enc_path" > /dev/null ;;
      bzip2)  bzip2  -d -c "$enc_path" > /dev/null ;;
      raw)    cat "$enc_path" > /dev/null ;;
    esac
    t1=$(date +%s%N)
    dt=$(python3 -c "print(f'{($t1-$t0)/1e6:.4f}')")
    dec_times="$dec_times $dt"
  done

  local enc_med dec_med enc_mbps dec_mbps
  enc_med=$(echo "$enc_times" | median_of)
  dec_med=$(echo "$dec_times" | median_of)
  enc_mbps=$(python3 -c "ms=$enc_med; print(f'{(($raw/1048576.0)/(ms/1000.0)):.4f}' if ms>0 else '0')")
  dec_mbps=$(python3 -c "ms=$dec_med; print(f'{(($raw/1048576.0)/(ms/1000.0)):.4f}' if ms>0 else '0')")

  echo "$file,$raw,$tool,$level,$enc,$ratio,$enc_med,$dec_med,$enc_mbps,$dec_mbps" >> "$OUT"
  rm -f "$enc_path"
}

for FILE in "${FILES[@]}"; do
  [[ -f "$FILE" ]] || { echo "skip (missing): $FILE" >&2; continue; }
  echo "==> $FILE" >&2
  for pair in "${TOOLS[@]}"; do
    tool="${pair%%:*}"
    level="${pair##*:}"
    command -v "$tool" >/dev/null 2>&1 || { [[ "$tool" == "raw" ]] || continue; }
    bench_pair "$FILE" "$tool" "$level"
  done
done

echo "wrote $OUT" >&2
