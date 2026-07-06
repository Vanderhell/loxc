#!/usr/bin/env python3
"""
bench_aggregate.py - Combine loxc_bench2 results with baseline-compressor
results and emit a human-readable Markdown report plus a merged CSV.

Inputs:
  --loxc <csv>         CSV produced by loxc_bench2
  --baselines <csv>    CSV produced by bench_baselines.sh
  --module <name>      Logical module name for the report
  --out-md <path>      Markdown report path
  --out-csv <path>     Merged long-form CSV

The merged CSV adds 'loxc' rows from the loxc_bench2 CSV (treating its
ext mode and emb mode as two pseudo-tools 'loxc-ext' and 'loxc-emb')
and then concatenates the baseline tools/levels.

The Markdown report covers:
  1. Run metadata (machine, compiler, dates).
  2. Per-file comparison tables: ratio + encode/decode throughput.
  3. Aggregate scoreboards (best ratio, fastest encode, fastest decode).
  4. Honest caveats about small-file fork overhead.
"""

from __future__ import annotations
import argparse, csv, os, platform, subprocess, sys, datetime
from collections import defaultdict

def read_csv(path):
    if not path or not os.path.exists(path):
        return []
    with open(path, newline="") as f:
        return list(csv.DictReader(f))

def system_info():
    info = {
        "date_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(timespec="seconds"),
        "host": platform.node(),
        "os": f"{platform.system()} {platform.release()}",
        "arch": platform.machine(),
        "python": platform.python_version(),
    }
    try:
        cc = subprocess.check_output(["cc", "--version"], stderr=subprocess.STDOUT).decode().splitlines()[0]
    except Exception:
        cc = "unknown"
    info["cc"] = cc
    try:
        cpuinfo = open("/proc/cpuinfo").read()
        for line in cpuinfo.splitlines():
            if "model name" in line:
                info["cpu"] = line.split(":",1)[1].strip()
                break
    except Exception:
        info["cpu"] = platform.processor() or "unknown"
    try:
        # MemTotal in kB
        for line in open("/proc/meminfo"):
            if line.startswith("MemTotal:"):
                kb = int(line.split()[1])
                info["mem"] = f"{kb//1024} MiB"
                break
    except Exception:
        info["mem"] = "unknown"
    return info

def loxc_rows_as_records(loxc_rows, module):
    """Convert loxc_bench2 rows into the same shape as baseline rows."""
    out = []
    for r in loxc_rows:
        if r.get("unsupported") in ("1", "true", "True"):
            continue
        raw_text = r.get("raw_bytes", "")
        ext_text = r.get("enc_external_bytes", "")
        emb_text = r.get("enc_embedded_bytes", "")
        if not raw_text or not ext_text or not emb_text:
            continue
        raw = int(raw_text)
        # external mode
        ext_bytes = int(ext_text)
        out.append({
            "path": r["path"],
            "raw_bytes": raw,
            "tool": f"loxc-ext({module})",
            "level": "-",
            "encoded_bytes": ext_bytes,
            "ratio_pct": float(r["enc_external_ratio_pct"]),
            "encode_median_ms": float(r["enc_median_ms"]),
            "decode_median_ms": float(r["dec_median_ms"]),
            "encode_mbps": float(r["enc_mbps_median"]),
            "decode_mbps": float(r["dec_mbps_median"]),
            "enc_p95_ms": float(r["enc_p95_ms"]),
            "dec_p95_ms": float(r["dec_p95_ms"]),
        })
        # embedded mode (self-contained .loxc)
        emb_bytes = int(emb_text)
        out.append({
            "path": r["path"],
            "raw_bytes": raw,
            "tool": f"loxc-emb({module})",
            "level": "-",
            "encoded_bytes": emb_bytes,
            "ratio_pct": float(r["enc_embedded_ratio_pct"]),
            "encode_median_ms": float(r["enc_median_ms"]),  # encode time same; only output differs
            "decode_median_ms": float(r["dec_median_ms"]),
            "encode_mbps": float(r["enc_mbps_median"]),
            "decode_mbps": float(r["dec_mbps_median"]),
            "enc_p95_ms": float(r["enc_p95_ms"]),
            "dec_p95_ms": float(r["dec_p95_ms"]),
        })
    return out

def baseline_rows_to_records(baseline_rows):
    out = []
    for r in baseline_rows:
        try:
            out.append({
                "path": r["path"],
                "raw_bytes": int(r["raw_bytes"]),
                "tool": r["tool"],
                "level": r["level"],
                "encoded_bytes": int(r["encoded_bytes"]),
                "ratio_pct": float(r["ratio_pct"]),
                "encode_median_ms": float(r["encode_median_ms"]),
                "decode_median_ms": float(r["decode_median_ms"]),
                "encode_mbps": float(r["encode_mbps"]),
                "decode_mbps": float(r["decode_mbps"]),
                "enc_p95_ms": float("nan"),
                "dec_p95_ms": float("nan"),
            })
        except (KeyError, ValueError):
            continue
    return out

def fmt_bytes(n):
    if n < 1024: return f"{n} B"
    if n < 1024*1024: return f"{n/1024:.1f} KiB"
    return f"{n/1048576:.2f} MiB"

def md_table(headers, rows):
    out = ["| " + " | ".join(headers) + " |",
           "|" + "|".join(["---"]*len(headers)) + "|"]
    for r in rows:
        out.append("| " + " | ".join(str(x) for x in r) + " |")
    return "\n".join(out)

def per_file_table(records_by_path, only_path):
    rows = []
    for rec in records_by_path[only_path]:
        rows.append([
            rec["tool"] + (f":{rec['level']}" if rec["level"] != "-" else ""),
            fmt_bytes(rec["encoded_bytes"]),
            f"{rec['ratio_pct']:.1f}%",
            f"{rec['encode_median_ms']:.2f}",
            f"{rec['encode_mbps']:.1f}",
            f"{rec['decode_median_ms']:.2f}",
            f"{rec['decode_mbps']:.1f}",
        ])
    # sort by ratio asc
    rows.sort(key=lambda r: float(r[2].rstrip("%")))
    headers = ["tool", "encoded", "ratio", "enc median (ms)", "enc MB/s", "dec median (ms)", "dec MB/s"]
    return md_table(headers, rows)

def scoreboard(records, key, reverse, top=10, fmt=None):
    fmt = fmt or (lambda x: f"{x:.2f}")
    rows = []
    for rec in sorted(records, key=lambda r: r[key], reverse=reverse)[:top]:
        rows.append([
            rec["path"],
            rec["tool"] + (f":{rec['level']}" if rec["level"] != "-" else ""),
            fmt(rec[key]),
            f"{rec['ratio_pct']:.1f}%",
            fmt_bytes(rec["raw_bytes"]),
        ])
    return md_table(["file","tool","value","ratio","raw size"], rows)

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--loxc", help="loxc_bench2 CSV", default=None)
    ap.add_argument("--baselines", help="bench_baselines.sh CSV", default=None)
    ap.add_argument("--module", default="demo")
    ap.add_argument("--out-md", required=True)
    ap.add_argument("--out-csv", required=True)
    ap.add_argument("--iterations", type=int, default=25)
    args = ap.parse_args()

    loxc_records = loxc_rows_as_records(read_csv(args.loxc), args.module) if args.loxc else []
    base_records = baseline_rows_to_records(read_csv(args.baselines)) if args.baselines else []
    all_records = loxc_records + base_records

    # group by path
    by_path = defaultdict(list)
    for r in all_records:
        by_path[r["path"]].append(r)
    paths = sorted(by_path.keys(), key=lambda p: by_path[p][0]["raw_bytes"])

    info = system_info()

    md = []
    md.append(f"# loxc benchmark report")
    md.append("")
    md.append(f"Generated: **{info['date_utc']}**  ")
    md.append(f"Host: `{info['host']}`  ")
    md.append(f"CPU: `{info.get('cpu','unknown')}`  ")
    md.append(f"Memory: `{info.get('mem','unknown')}`  ")
    md.append(f"OS / arch: `{info['os']} / {info['arch']}`  ")
    md.append(f"Compiler: `{info['cc']}`  ")
    md.append(f"loxc module under test: `{args.module}`  ")
    md.append(f"Iterations per measurement: **{args.iterations}** (after warmup)")
    md.append("")
    md.append("## How to read this report")
    md.append("")
    md.append("Each measured pair (tool, level, file) reports median wall-clock latency over the")
    md.append("iteration count, plus the resulting compressed size. Throughput is computed as")
    md.append("`(input bytes / median wall time)` and is only meaningful for files large enough")
    md.append("that the per-invocation overhead is amortized. For sub-64 KiB files the baseline")
    md.append("CLI tools' fork/exec cost dominates and their throughput numbers should be read")
    md.append("as an upper bound on latency, not as the codec's raw speed. The loxc numbers are")
    md.append("measured in-process and do not have that overhead.")
    md.append("")
    md.append("Loxc is reported in two rows per file:")
    md.append("- `loxc-ext` — external mode. The .loxc payload requires the matching .loxctab")
    md.append("  table at decode time. The table size is amortized over many payloads.")
    md.append("- `loxc-emb` — embedded mode. The .loxc file is self-contained: it carries the")
    md.append("  full table inside the header, so a fresh decoder can read it with no extra")
    md.append("  files. The table blob is constant per module, so the overhead shrinks as the")
    md.append("  payload grows.")
    md.append("")
    md.append("Round-trip integrity is verified for every loxc iteration (decoded buffer is")
    md.append("`memcmp`'d against the original input). Any failure aborts the measurement.")
    md.append("")
    md.append("## Per-file comparison")
    md.append("")
    md.append("Tables are sorted by compressed size ascending: best ratio at the top.")
    md.append("")
    for p in paths:
        md.append(f"### `{p}` ({fmt_bytes(by_path[p][0]['raw_bytes'])})")
        md.append("")
        md.append(per_file_table(by_path, p))
        md.append("")

    md.append("## Aggregate scoreboards")
    md.append("")
    md.append("### Best ratio across all files")
    md.append("")
    md.append(scoreboard(all_records, key="ratio_pct", reverse=False,
                         fmt=lambda x: f"{x:.2f}%"))
    md.append("")
    md.append("### Fastest decode throughput across all files")
    md.append("")
    md.append(scoreboard(all_records, key="decode_mbps", reverse=True,
                         fmt=lambda x: f"{x:.1f} MB/s"))
    md.append("")
    md.append("### Fastest encode throughput across all files")
    md.append("")
    md.append(scoreboard(all_records, key="encode_mbps", reverse=True,
                         fmt=lambda x: f"{x:.1f} MB/s"))
    md.append("")

    md.append("## Notes & honest caveats")
    md.append("")
    md.append("- **Domain match matters.** loxc requires a trained table covering the input's")
    md.append("  byte distribution. If the table doesn't cover the input, the encoder returns")
    md.append("  `LOXC_ERR_SYMBOL_NOT_FOUND` and the file is reported as UNSUPPORTED in the")
    md.append("  per-file table. Use a module trained on a representative corpus.")
    md.append("- **No LZ77.** Without backreferences, loxc cannot match gzip/zstd/brotli on")
    md.append("  ratio. Its win is in the decoder: table lookup + bitstream walk.")
    md.append("- **Embedded mode includes the table.** For small payloads the table blob")
    md.append("  dominates the .loxc file size. Report both modes so the deployment decision")
    md.append("  is informed.")
    md.append("- **Baseline tools are measured via CLI.** Fork/exec adds a fixed cost (typically")
    md.append("  1-3 ms on Linux). On files below ~64 KiB this hides the codec speed. The")
    md.append("  fair comparison points are the larger files in the suite.")
    md.append("- **Single-threaded.** All measurements are single-threaded by design.")

    md.append("")
    md.append("---")
    md.append("Generated by `tools/bench_aggregate.py`.")

    with open(args.out_md, "w") as f:
        f.write("\n".join(md))

    with open(args.out_csv, "w", newline="") as f:
        w = csv.writer(f)
        w.writerow(["path","raw_bytes","tool","level","encoded_bytes","ratio_pct",
                    "encode_median_ms","decode_median_ms","encode_mbps","decode_mbps"])
        for r in all_records:
            w.writerow([r["path"], r["raw_bytes"], r["tool"], r["level"],
                        r["encoded_bytes"], f"{r['ratio_pct']:.4f}",
                        f"{r['encode_median_ms']:.4f}",
                        f"{r['decode_median_ms']:.4f}",
                        f"{r['encode_mbps']:.4f}",
                        f"{r['decode_mbps']:.4f}"])

    print(f"wrote {args.out_md}", file=sys.stderr)
    print(f"wrote {args.out_csv}", file=sys.stderr)

if __name__ == "__main__":
    main()
