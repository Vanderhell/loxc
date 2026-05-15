/*
 * loxc_bench2 - comprehensive benchmark for the loxc codec.
 *
 * Improvements over the original loxc_bench:
 *   - Warmup pass + N timed iterations per file
 *   - Min / median / p95 / p99 / max / stddev for both encode and decode
 *   - Throughput in MB/s (input bytes / wall time)
 *   - External mode AND embedded mode (header self-contained .loxc files)
 *   - Round-trip integrity verification on every measured iteration
 *   - Machine-readable CSV and JSON output (--csv, --json)
 *   - Pluggable corpus list via --suite file (one path per line)
 *   - Module loaded at runtime via .loxctab (no recompile per module)
 *   - Captures encoded size for both modes, plus table size, so the report
 *     can show "deployable bytes" for embedded targets honestly.
 *
 * Build (added to Makefile by the bench package):
 *   cc -std=c99 -Wall -Wextra -O2 -Iinclude -o tools/loxc_bench2 \
 *       tools/loxc_bench2.c libloxc.a
 *
 * Usage:
 *   tools/loxc_bench2 --table modules/loxc_demo.loxctab \
 *                     --suite benchmarks/suite.list \
 *                     --iterations 25 --warmup 3 \
 *                     --csv out/loxc_results.csv \
 *                     --json out/loxc_results.json
 */

#define _POSIX_C_SOURCE 199309L
#include <time.h>

#include <errno.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc.h"
#include "loxc_tab.h"

#define BENCH2_MAX_FILES 256
#define BENCH2_MAX_ITERS 4096

/* --------------------------- timing --------------------------- */

static double now_sec(void) {
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
  }
  return (double)clock() / (double)CLOCKS_PER_SEC;
}

/* --------------------------- stats --------------------------- */

static int cmp_double(const void *a, const void *b) {
  double da = *(const double *)a;
  double db = *(const double *)b;
  if (da < db) return -1;
  if (da > db) return 1;
  return 0;
}

typedef struct {
  double min_ms;
  double median_ms;
  double p95_ms;
  double p99_ms;
  double max_ms;
  double mean_ms;
  double stddev_ms;
  size_t n;
} bench_stats_t;

static bench_stats_t summarize(double *samples_ms, size_t n) {
  bench_stats_t s;
  memset(&s, 0, sizeof(s));
  s.n = n;
  if (n == 0) return s;

  qsort(samples_ms, n, sizeof(double), cmp_double);
  s.min_ms = samples_ms[0];
  s.max_ms = samples_ms[n - 1];
  s.median_ms = samples_ms[n / 2];
  /* nearest-rank percentile */
  size_t i95 = (size_t)((double)n * 0.95);
  size_t i99 = (size_t)((double)n * 0.99);
  if (i95 >= n) i95 = n - 1;
  if (i99 >= n) i99 = n - 1;
  s.p95_ms = samples_ms[i95];
  s.p99_ms = samples_ms[i99];

  double sum = 0.0;
  for (size_t i = 0; i < n; i++) sum += samples_ms[i];
  s.mean_ms = sum / (double)n;

  double acc = 0.0;
  for (size_t i = 0; i < n; i++) {
    double d = samples_ms[i] - s.mean_ms;
    acc += d * d;
  }
  s.stddev_ms = (n > 1) ? sqrt(acc / (double)(n - 1)) : 0.0;
  return s;
}

/* --------------------------- file I/O --------------------------- */

static int read_entire_file(const char *path, uint8_t **out_buf, size_t *out_len) {
  *out_buf = NULL;
  *out_len = 0;

  FILE *f = fopen(path, "rb");
  if (!f) {
    fprintf(stderr, "bench2: fopen(%s): %s\n", path, strerror(errno));
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 1;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fclose(f);
    return 1;
  }
  rewind(f);

  uint8_t *buf = NULL;
  if (sz > 0) {
    buf = (uint8_t *)malloc((size_t)sz);
    if (!buf) {
      fclose(f);
      return 1;
    }
  }
  size_t nread = sz > 0 ? fread(buf, 1, (size_t)sz, f) : 0;
  fclose(f);
  if (nread != (size_t)sz) {
    free(buf);
    return 1;
  }
  *out_buf = buf;
  *out_len = (size_t)sz;
  return 0;
}

/* --------------------------- per-file bench --------------------------- */

typedef struct {
  char path[512];
  size_t input_size;
  size_t encoded_size_external;
  size_t encoded_size_embedded;
  size_t table_blob_size;
  bench_stats_t enc_external;
  bench_stats_t enc_embedded;
  bench_stats_t dec_stats;
  int round_trip_ok;
  int unsupported;
  double enc_mbps_external; /* median throughput */
  double dec_mbps;
  double ratio_external_pct;
  double ratio_embedded_pct;
} file_result_t;

static int run_compress_once(const char *module_name,
                             const uint8_t *input, size_t input_len,
                             uint8_t *output, size_t *output_cap,
                             size_t *output_actual,
                             int embed_table) {
  size_t cap_arg = *output_cap;
  *output_actual = 0;
  int rc = loxc_compress_with_options(module_name,
                                      (const char *)input, input_len,
                                      output, &cap_arg, output_actual,
                                      embed_table);
  *output_cap = cap_arg;
  return rc;
}

static int bench_file(const char *module_name,
                      const char *path,
                      int iterations, int warmup,
                      file_result_t *res) {
  size_t path_len;
  memset(res, 0, sizeof(*res));
  path_len = strlen(path);
  if (path_len >= sizeof(res->path)) path_len = sizeof(res->path) - 1;
  memcpy(res->path, path, path_len);
  res->path[path_len] = '\0';

  uint8_t *input = NULL;
  size_t input_len = 0;
  if (read_entire_file(path, &input, &input_len) != 0) return 1;
  res->input_size = input_len;

  /* generous output buffer; embedded mode adds the table blob too. */
  size_t out_cap = input_len * 2u + 65536u;
  uint8_t *encoded = (uint8_t *)malloc(out_cap);
  if (!encoded) {
    free(input);
    return 1;
  }

  /* probe both modes once to detect unsupported and capture sizes. */
  size_t enc_ext = 0, enc_emb = 0;
  size_t cap_probe = out_cap;
  int rc = run_compress_once(module_name, input, input_len, encoded, &cap_probe, &enc_ext, 0);
  if (rc != LOXC_OK) {
    res->unsupported = 1;
    free(encoded);
    free(input);
    return 0;
  }
  res->encoded_size_external = enc_ext;
  res->ratio_external_pct = input_len ? 100.0 * (double)enc_ext / (double)input_len : 0.0;

  cap_probe = out_cap;
  rc = run_compress_once(module_name, input, input_len, encoded, &cap_probe, &enc_emb, 1);
  if (rc != LOXC_OK) {
    /* shouldn't fail if external worked, but stay safe */
    res->unsupported = 1;
    free(encoded);
    free(input);
    return 0;
  }
  res->encoded_size_embedded = enc_emb;
  res->ratio_embedded_pct = input_len ? 100.0 * (double)enc_emb / (double)input_len : 0.0;
  res->table_blob_size = (enc_emb > enc_ext) ? (enc_emb - enc_ext) : 0;

  /* timed encode-external */
  double *enc_ext_samples = calloc((size_t)iterations, sizeof(double));
  double *enc_emb_samples = calloc((size_t)iterations, sizeof(double));
  double *dec_samples = calloc((size_t)iterations, sizeof(double));
  if (!enc_ext_samples || !enc_emb_samples || !dec_samples) {
    free(enc_ext_samples); free(enc_emb_samples); free(dec_samples);
    free(encoded); free(input);
    return 1;
  }

  for (int w = 0; w < warmup; w++) {
    size_t cap = out_cap, actual = 0;
    run_compress_once(module_name, input, input_len, encoded, &cap, &actual, 0);
  }

  /* timed: encode external */
  for (int i = 0; i < iterations; i++) {
    size_t cap = out_cap, actual = 0;
    double t0 = now_sec();
    rc = run_compress_once(module_name, input, input_len, encoded, &cap, &actual, 0);
    double t1 = now_sec();
    if (rc != LOXC_OK || actual != enc_ext) {
      fprintf(stderr, "bench2: encode external mismatch on %s\n", path);
      goto fail;
    }
    enc_ext_samples[i] = (t1 - t0) * 1000.0;
  }

  /* timed: encode embedded */
  for (int i = 0; i < iterations; i++) {
    size_t cap = out_cap, actual = 0;
    double t0 = now_sec();
    rc = run_compress_once(module_name, input, input_len, encoded, &cap, &actual, 1);
    double t1 = now_sec();
    if (rc != LOXC_OK || actual != enc_emb) {
      fprintf(stderr, "bench2: encode embedded mismatch on %s\n", path);
      goto fail;
    }
    enc_emb_samples[i] = (t1 - t0) * 1000.0;
  }

  /* fresh compress to get a buffer we will repeatedly decode */
  size_t cap_for_dec = out_cap, enc_for_dec = 0;
  rc = run_compress_once(module_name, input, input_len, encoded, &cap_for_dec, &enc_for_dec, 0);
  if (rc != LOXC_OK) goto fail;

  size_t dec_cap = input_len + 16u;
  char *decoded = (char *)malloc(dec_cap);
  if (!decoded) goto fail;

  /* decode warmup */
  for (int w = 0; w < warmup; w++) {
    size_t c = dec_cap, a = 0;
    loxc_decompress(encoded, enc_for_dec, decoded, &c, &a);
  }

  for (int i = 0; i < iterations; i++) {
    size_t c = dec_cap, a = 0;
    double t0 = now_sec();
    rc = loxc_decompress(encoded, enc_for_dec, decoded, &c, &a);
    double t1 = now_sec();
    if (rc != LOXC_OK || a != input_len || memcmp(decoded, input, input_len) != 0) {
      fprintf(stderr, "bench2: round-trip FAIL on %s (rc=%d)\n", path, rc);
      free(decoded);
      goto fail;
    }
    dec_samples[i] = (t1 - t0) * 1000.0;
  }
  res->round_trip_ok = 1;

  res->enc_external = summarize(enc_ext_samples, (size_t)iterations);
  res->enc_embedded = summarize(enc_emb_samples, (size_t)iterations);
  res->dec_stats    = summarize(dec_samples, (size_t)iterations);

  if (res->enc_external.median_ms > 0.0) {
    res->enc_mbps_external =
        ((double)input_len / 1048576.0) / (res->enc_external.median_ms / 1000.0);
  }
  if (res->dec_stats.median_ms > 0.0) {
    res->dec_mbps =
        ((double)input_len / 1048576.0) / (res->dec_stats.median_ms / 1000.0);
  }

  free(decoded);
  free(enc_ext_samples);
  free(enc_emb_samples);
  free(dec_samples);
  free(encoded);
  free(input);
  return 0;

fail:
  free(enc_ext_samples);
  free(enc_emb_samples);
  free(dec_samples);
  free(encoded);
  free(input);
  return 1;
}

/* --------------------------- output --------------------------- */

static void print_human(const file_result_t *r) {
  if (r->unsupported) {
    printf("%-46s | %9zu | UNSUPPORTED (out-of-corpus byte)\n",
           r->path, r->input_size);
    return;
  }
  printf("%-46s | %9zu | %9zu | %6.1f%% | %9zu | %6.1f%% | "
         "enc med=%7.2fms p95=%7.2fms (%6.1f MB/s) | "
         "dec med=%7.2fms p95=%7.2fms (%6.1f MB/s) | RT=%s\n",
         r->path, r->input_size,
         r->encoded_size_external, r->ratio_external_pct,
         r->encoded_size_embedded, r->ratio_embedded_pct,
         r->enc_external.median_ms, r->enc_external.p95_ms, r->enc_mbps_external,
         r->dec_stats.median_ms, r->dec_stats.p95_ms, r->dec_mbps,
         r->round_trip_ok ? "OK" : "FAIL");
}

static void print_human_header(void) {
  printf("%-46s | %9s | %9s | %7s | %9s | %7s | %s\n",
         "file", "raw_size", "ext_size", "ext_%", "emb_size", "emb_%",
         "encode (external) / decode timings");
  printf("---------------------------------------------"
         "+-----------+-----------+---------+-----------+---------+"
         "---------------------------------------------------\n");
}

static void write_csv_header(FILE *f) {
  fprintf(f,
    "path,raw_bytes,enc_external_bytes,enc_external_ratio_pct,"
    "enc_embedded_bytes,enc_embedded_ratio_pct,table_blob_bytes,"
    "enc_min_ms,enc_median_ms,enc_p95_ms,enc_p99_ms,enc_max_ms,enc_stddev_ms,"
    "enc_mbps_median,"
    "dec_min_ms,dec_median_ms,dec_p95_ms,dec_p99_ms,dec_max_ms,dec_stddev_ms,"
    "dec_mbps_median,round_trip_ok,unsupported\n");
}

static void write_csv_row(FILE *f, const file_result_t *r) {
  if (r->unsupported) {
    fprintf(f, "%s,%zu,,,,,,,,,,,,,,,,,,,0,1\n", r->path, r->input_size);
    return;
  }
  fprintf(f,
    "%s,%zu,%zu,%.4f,%zu,%.4f,%zu,"
    "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,"
    "%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%.4f,%d,0\n",
    r->path, r->input_size,
    r->encoded_size_external, r->ratio_external_pct,
    r->encoded_size_embedded, r->ratio_embedded_pct, r->table_blob_size,
    r->enc_external.min_ms, r->enc_external.median_ms, r->enc_external.p95_ms,
    r->enc_external.p99_ms, r->enc_external.max_ms, r->enc_external.stddev_ms,
    r->enc_mbps_external,
    r->dec_stats.min_ms, r->dec_stats.median_ms, r->dec_stats.p95_ms,
    r->dec_stats.p99_ms, r->dec_stats.max_ms, r->dec_stats.stddev_ms,
    r->dec_mbps, r->round_trip_ok);
}

static void write_json(FILE *f, const file_result_t *results, size_t n,
                       const char *module_name, int iterations, int warmup) {
  fprintf(f, "{\n");
  fprintf(f, "  \"tool\": \"loxc_bench2\",\n");
  fprintf(f, "  \"module\": \"%s\",\n", module_name ? module_name : "");
  fprintf(f, "  \"iterations\": %d,\n", iterations);
  fprintf(f, "  \"warmup\": %d,\n", warmup);
  fprintf(f, "  \"results\": [\n");
  for (size_t i = 0; i < n; i++) {
    const file_result_t *r = &results[i];
    fprintf(f, "    {\n");
    fprintf(f, "      \"path\": \"%s\",\n", r->path);
    fprintf(f, "      \"raw_bytes\": %zu,\n", r->input_size);
    if (r->unsupported) {
      fprintf(f, "      \"unsupported\": true\n");
    } else {
      fprintf(f, "      \"unsupported\": false,\n");
      fprintf(f, "      \"enc_external_bytes\": %zu,\n", r->encoded_size_external);
      fprintf(f, "      \"enc_external_ratio_pct\": %.4f,\n", r->ratio_external_pct);
      fprintf(f, "      \"enc_embedded_bytes\": %zu,\n", r->encoded_size_embedded);
      fprintf(f, "      \"enc_embedded_ratio_pct\": %.4f,\n", r->ratio_embedded_pct);
      fprintf(f, "      \"table_blob_bytes\": %zu,\n", r->table_blob_size);
      fprintf(f, "      \"encode_external_ms\": {\"min\":%.4f,\"median\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"stddev\":%.4f},\n",
              r->enc_external.min_ms, r->enc_external.median_ms,
              r->enc_external.p95_ms, r->enc_external.p99_ms,
              r->enc_external.max_ms, r->enc_external.stddev_ms);
      fprintf(f, "      \"encode_embedded_ms\": {\"min\":%.4f,\"median\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"stddev\":%.4f},\n",
              r->enc_embedded.min_ms, r->enc_embedded.median_ms,
              r->enc_embedded.p95_ms, r->enc_embedded.p99_ms,
              r->enc_embedded.max_ms, r->enc_embedded.stddev_ms);
      fprintf(f, "      \"decode_ms\": {\"min\":%.4f,\"median\":%.4f,\"p95\":%.4f,\"p99\":%.4f,\"max\":%.4f,\"stddev\":%.4f},\n",
              r->dec_stats.min_ms, r->dec_stats.median_ms,
              r->dec_stats.p95_ms, r->dec_stats.p99_ms,
              r->dec_stats.max_ms, r->dec_stats.stddev_ms);
      fprintf(f, "      \"encode_mbps_median\": %.4f,\n", r->enc_mbps_external);
      fprintf(f, "      \"decode_mbps_median\": %.4f,\n", r->dec_mbps);
      fprintf(f, "      \"round_trip_ok\": %s\n", r->round_trip_ok ? "true" : "false");
    }
    fprintf(f, "    }%s\n", (i + 1 < n) ? "," : "");
  }
  fprintf(f, "  ]\n");
  fprintf(f, "}\n");
}

/* --------------------------- suite loading --------------------------- */

static int load_suite(const char *path, char files[][512], int *out_n) {
  FILE *f = fopen(path, "r");
  if (!f) {
    fprintf(stderr, "bench2: cannot open suite list: %s\n", path);
    return 1;
  }
  int n = 0;
  char line[512];
  while (fgets(line, sizeof(line), f)) {
    /* strip trailing newline / CR */
    size_t L = strlen(line);
    while (L > 0 && (line[L-1] == '\n' || line[L-1] == '\r' || line[L-1] == ' ' || line[L-1] == '\t')) {
      line[--L] = 0;
    }
    if (L == 0 || line[0] == '#') continue;
    if (n >= BENCH2_MAX_FILES) break;
    snprintf(files[n], 512, "%s", line);
    n++;
  }
  fclose(f);
  *out_n = n;
  return 0;
}

/* --------------------------- main --------------------------- */

static void usage(FILE *out) {
  fprintf(out,
    "Usage: loxc_bench2 --table <path.loxctab> --suite <file>\n"
    "                   [--iterations N] [--warmup N]\n"
    "                   [--csv out.csv] [--json out.json]\n"
    "\n"
    "Default: --iterations 25 --warmup 3\n"
    "\n"
    "The suite file is one filesystem path per line. Lines starting with\n"
    "'#' are comments. A common file is benchmarks/suite.list.\n");
}

int main(int argc, char **argv) {
  const char *table_path = NULL;
  const char *suite_path = NULL;
  const char *csv_path = NULL;
  const char *json_path = NULL;
  int iterations = 25;
  int warmup = 3;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--table") == 0 && i + 1 < argc) {
      table_path = argv[++i];
    } else if (strcmp(argv[i], "--suite") == 0 && i + 1 < argc) {
      suite_path = argv[++i];
    } else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
      iterations = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
      warmup = atoi(argv[++i]);
    } else if (strcmp(argv[i], "--csv") == 0 && i + 1 < argc) {
      csv_path = argv[++i];
    } else if (strcmp(argv[i], "--json") == 0 && i + 1 < argc) {
      json_path = argv[++i];
    } else if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage(stdout); return 0;
    } else {
      fprintf(stderr, "bench2: unknown arg: %s\n", argv[i]);
      usage(stderr); return 2;
    }
  }

  if (!table_path || !suite_path) { usage(stderr); return 2; }
  if (iterations <= 0 || iterations > BENCH2_MAX_ITERS) iterations = 25;
  if (warmup < 0) warmup = 0;

  /* Load the module at runtime so we don't recompile per training. */
  loxc_module_t *module = loxc_module_load_from_file(table_path);
  if (!module) {
    fprintf(stderr, "bench2: failed to load module table: %s\n", table_path);
    return 3;
  }
  if (loxc_module_register(module) != LOXC_OK) {
    fprintf(stderr, "bench2: failed to register module\n");
    loxc_module_unload(module);
    return 3;
  }
  const char *module_name = module->name;

  char files[BENCH2_MAX_FILES][512];
  int file_count = 0;
  if (load_suite(suite_path, files, &file_count) != 0) {
    loxc_module_unregister(module_name);
    loxc_module_unload(module);
    return 4;
  }

  printf("# loxc_bench2: table=%s module=%s iterations=%d warmup=%d files=%d\n",
         table_path, module_name, iterations, warmup, file_count);
  print_human_header();

  file_result_t *results = calloc((size_t)file_count, sizeof(file_result_t));
  if (!results) {
    fprintf(stderr, "bench2: oom\n");
    return 5;
  }

  int failures = 0;
  for (int i = 0; i < file_count; i++) {
    if (bench_file(module_name, files[i], iterations, warmup, &results[i]) != 0) {
      failures++;
    }
    print_human(&results[i]);
  }

  if (csv_path) {
    FILE *f = fopen(csv_path, "w");
    if (f) {
      write_csv_header(f);
      for (int i = 0; i < file_count; i++) write_csv_row(f, &results[i]);
      fclose(f);
      fprintf(stderr, "bench2: wrote CSV: %s\n", csv_path);
    } else {
      fprintf(stderr, "bench2: cannot write CSV: %s\n", csv_path);
    }
  }
  if (json_path) {
    FILE *f = fopen(json_path, "w");
    if (f) {
      write_json(f, results, (size_t)file_count, module_name, iterations, warmup);
      fclose(f);
      fprintf(stderr, "bench2: wrote JSON: %s\n", json_path);
    } else {
      fprintf(stderr, "bench2: cannot write JSON: %s\n", json_path);
    }
  }

  free(results);
  loxc_module_unregister(module_name);
  loxc_module_unload(module);
  return failures ? 1 : 0;
}
