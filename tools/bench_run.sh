#!/usr/bin/env bash
# bench_run.sh - end-to-end loxc benchmark suite.
#
# What it does, in order:
#   1. Builds libloxc.a, loxc_train, loxc_cli, loxc_bench, loxc_bench2.
#   2. Generates fresh training/test corpora into trainings/extra and
#      benchmarks/corpora (idempotent: skipped if files already exist).
#   3. Trains domain-specific modules (sample text, json, logs, csrc).
#   4. Runs loxc_bench2 for each module against its matching suite.
#   5. Runs baseline-compressor benchmarks on the union suite.
#   6. Aggregates everything into BENCHMARKS_FULL.md plus merged CSVs.
#
# Usage:
#   tools/bench_run.sh [iterations]
#
# Default iterations: 25.

set -euo pipefail

ITER="${1:-25}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

mkdir -p bench_out modules trainings/extra benchmarks/corpora

echo "== STEP 1: build =="
make -s libloxc.a tools/loxc_train tools/loxc_cli tools/loxc_bench
cc -std=c99 -Wall -Wextra -O2 -Iinclude -o tools/loxc_bench2 tools/loxc_bench2.c libloxc.a -lm

echo "== STEP 2: ensure corpora =="
# plain_sample_text.txt slice referenced by the old BENCHMARKS.md
if [[ ! -s benchmarks/plain_sample_text.txt ]]; then
  head -c 30000 trainings/demo_corpus.txt > benchmarks/plain_sample_text.txt
fi
# Generate extra corpora if any of them is missing.
if [[ ! -s trainings/extra/json_corpus.txt ]] || \
   [[ ! -s trainings/extra/logs_corpus.txt ]] || \
   [[ ! -s trainings/extra/csrc_corpus.txt ]] || \
   [[ ! -s benchmarks/corpora/text_65536.txt ]]; then
  python3 tools/bench_make_corpora.py
fi

echo "== STEP 3: train modules =="
train() {
  local input="$1" out="$2" name="$3" mid="$4"
  local table_bytes
  if [[ -s "${out}.loxctab" ]] && [[ "${out}.loxctab" -nt "$input" ]]; then
    echo "  - up to date: ${out}.loxctab"
    return
  fi
  ./tools/loxc_train --input "$input" --output "$out" --module-name "$name" --module-id "$mid" >/dev/null
  table_bytes=$(wc -c < "${out}.loxctab" | tr -d '[:space:]')
  echo "  - trained: ${out}.loxctab (${table_bytes} B)"
}
train trainings/demo_corpus.txt        modules/loxc_demo demo 10
train trainings/extra/json_corpus.txt  modules/loxc_json json 20
train trainings/extra/logs_corpus.txt  modules/loxc_logs logs 21
train trainings/extra/csrc_corpus.txt  modules/loxc_csrc csrc 22

echo "== STEP 4: per-module loxc suites =="
# Each suite is the test data appropriate for that module, plus a couple of
# "out-of-domain" files to show the unsupported-byte behaviour.
cat > bench_out/suite_demo.list <<EOF
trainings/demo_corpus.txt
benchmarks/plain_sample_text.txt
benchmarks/corpora/text_1024.txt
benchmarks/corpora/text_4096.txt
benchmarks/corpora/text_16384.txt
benchmarks/corpora/text_65536.txt
benchmarks/corpora/text_262144.txt
benchmarks/corpora/text_524288.txt
benchmarks/corpora/repetitive_16k.txt
EOF
cat > bench_out/suite_json.list <<EOF
benchmarks/corpora/json_test.json
EOF
cat > bench_out/suite_logs.list <<EOF
benchmarks/corpora/logs_test.txt
EOF
cat > bench_out/suite_csrc.list <<EOF
benchmarks/corpora/csrc_test.c
EOF

run_loxc() {
  local mod="$1" suite="$2" tag="$3"
  ./tools/loxc_bench2 \
    --table "modules/loxc_${mod}.loxctab" \
    --suite "$suite" \
    --iterations "$ITER" --warmup 3 \
    --csv "bench_out/loxc_${tag}.csv" \
    --json "bench_out/loxc_${tag}.json" \
    > "bench_out/loxc_${tag}.txt"
}
run_loxc demo bench_out/suite_demo.list demo
run_loxc json bench_out/suite_json.list json
run_loxc logs bench_out/suite_logs.list logs
run_loxc csrc bench_out/suite_csrc.list csrc

echo "== STEP 5: baseline-compressor benchmarks =="
# We use the union of all suites for baselines, deduplicated.
cat bench_out/suite_demo.list bench_out/suite_json.list \
    bench_out/suite_logs.list bench_out/suite_csrc.list \
  | grep -vE '^\s*(#|$)' | sort -u > bench_out/suite_union.list

./tools/bench_baselines.sh bench_out/suite_union.list bench_out/baselines.csv "$ITER"

echo "== STEP 6: aggregate =="
python3 tools/bench_aggregate.py \
  --loxc bench_out/loxc_demo.csv \
  --baselines bench_out/baselines.csv \
  --module demo \
  --iterations "$ITER" \
  --out-md  bench_out/BENCHMARKS_FULL.md \
  --out-csv bench_out/merged.csv

# Domain-specific append: produce a small "domain wins" supplement
python3 - <<'PY' > bench_out/DOMAIN_WINS.md
import csv, os
def best(path, key):
    rows = list(csv.DictReader(open(path)))
    if not rows: return None
    return min(rows, key=key) if 'min' in key.__name__ else max(rows, key=key)

# Show what each domain-tuned loxc module gets you on its target test file.
print("# Domain-specific module results\n")
print("Each row shows what a module trained on a domain corpus achieves on a")
print("*held-out* test file from that same domain. The held-out data was not")
print("used during training.\n")
print("| domain | table size | test file | raw | ext size | ext % | emb % | dec MB/s |")
print("|---|---|---|---|---|---|---|---|")
for tag, suite, mod in [("sample-text","loxc_demo.csv","demo"),
                        ("json","loxc_json.csv","json"),
                        ("logs","loxc_logs.csv","logs"),
                        ("csrc","loxc_csrc.csv","csrc")]:
    path = f"bench_out/{suite}"
    if not os.path.exists(path): continue
    tab = os.path.getsize(f"modules/loxc_{mod}.loxctab")
    for r in csv.DictReader(open(path)):
        if r.get("unsupported") == "1": continue
        if not r.get("enc_external_bytes"): continue
        # Pick the first non-training-corpus row for clarity
        if "trainings/" in r["path"]: continue
        print(f"| {tag} | {tab} B | `{r['path']}` | {r['raw_bytes']} | "
              f"{r['enc_external_bytes']} | {float(r['enc_external_ratio_pct']):.1f}% | "
              f"{float(r['enc_embedded_ratio_pct']):.1f}% | "
              f"{float(r['dec_mbps_median']):.1f} |")
        break
PY

echo ""
echo "== DONE =="
echo "Outputs in bench_out/:"
ls -la bench_out/
echo ""
echo "Open bench_out/BENCHMARKS_FULL.md for the report."
