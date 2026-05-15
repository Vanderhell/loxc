#!/usr/bin/env python3
"""
bench_make_corpora.py - generate the training and test corpora used by
bench_run.sh. Deterministic (fixed RNG seed) so results are reproducible.

Produces:
  trainings/extra/json_corpus.txt
  trainings/extra/logs_corpus.txt
  trainings/extra/csrc_corpus.txt
  benchmarks/corpora/json_test.json
  benchmarks/corpora/logs_test.txt
  benchmarks/corpora/csrc_test.c
  benchmarks/corpora/text_{1024,4096,16384,65536,262144,524288}.txt
  benchmarks/corpora/repetitive_16k.txt
"""

from __future__ import annotations
import json, os, random

random.seed(7)
os.makedirs("trainings/extra",      exist_ok=True)
os.makedirs("benchmarks/corpora",  exist_ok=True)

# ---------- JSON ----------
def make_json_doc(i):
    return json.dumps({
        "id": i,
        "user": f"user_{i}",
        "email": f"user_{i}@example.com",
        "active": (i % 3 != 0),
        "roles": random.sample(["admin","editor","viewer","auditor","owner"], k=2),
        "score": round(random.random() * 100, 2),
        "tags": [random.choice(["alpha","beta","gamma","delta","epsilon"]) for _ in range(3)],
        "address": {
            "city": random.choice(["Bratislava","Praha","Wien","Berlin","Budapest"]),
            "zip": f"{random.randint(10000,99999)}",
            "country": random.choice(["SK","CZ","AT","DE","HU"]),
        },
        "meta": {"created_at": "2026-05-14T10:00:00Z", "version": 3},
    })

with open("trainings/extra/json_corpus.txt","w") as f:
    f.write("\n".join(make_json_doc(i) for i in range(2000)) + "\n")
with open("benchmarks/corpora/json_test.json","w") as f:
    f.write("\n".join(make_json_doc(i) for i in range(2000, 2300)) + "\n")

# ---------- Logs ----------
def make_logs(n):
    levels  = ["INFO","WARN","ERROR","DEBUG"]
    services = ["auth","api","worker","cache","db"]
    msgs = [
        "request handled",
        "db query took",
        "cache miss for key",
        "auth failed for user",
        "background job scheduled",
        "configuration reloaded",
        "rate limit exceeded",
    ]
    out = []
    for _ in range(n):
        ts = f"2026-05-{random.randint(1,14):02d}T{random.randint(0,23):02d}:{random.randint(0,59):02d}:{random.randint(0,59):02d}Z"
        out.append(
            f"{ts} {random.choice(levels)} {random.choice(services)} "
            f"req_{random.randint(1000,9999)} {random.choice(msgs)} "
            f"latency={random.randint(1,500)}ms"
        )
    return "\n".join(out) + "\n"

logs_full = make_logs(30000)
cut = int(len(logs_full) * 0.85)
with open("trainings/extra/logs_corpus.txt","w") as f: f.write(logs_full[:cut])
with open("benchmarks/corpora/logs_test.txt","w") as f: f.write(logs_full[cut:])

# ---------- C source ----------
def make_c_func(i):
    return (
f"""static int process_record_{i}(struct ctx *c, const char *name, size_t len) {{
  if (c == NULL || name == NULL) return -1;
  if (len == 0) return 0;
  for (size_t j = 0; j < len; j++) {{
    int rc = handle_byte(c, (uint8_t)name[j]);
    if (rc != 0) return rc;
  }}
  return 0;
}}
""")

with open("trainings/extra/csrc_corpus.txt","w") as f:
    f.write("\n".join(make_c_func(i) for i in range(400)))
with open("benchmarks/corpora/csrc_test.c","w") as f:
    f.write("\n".join(make_c_func(i) for i in range(400, 460)))

# ---------- Size-scaling slices of the bundled sample text corpus ----------
if os.path.exists("trainings/demo_corpus.txt"):
    src = open("trainings/demo_corpus.txt","rb").read()
    for size in [1024, 4096, 16384, 65536, 262144, 524288]:
        if size <= len(src):
            with open(f"benchmarks/corpora/text_{size}.txt","wb") as f:
                f.write(src[:size])

# ---------- Highly repetitive (compressor best-case) ----------
with open("benchmarks/corpora/repetitive_16k.txt","w") as f:
    f.write(("the quick brown fox jumps over the lazy dog. " * 400)[:16384])

print("corpora generated.")
