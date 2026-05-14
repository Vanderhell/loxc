/* Enable clock_gettime/CLOCK_MONOTONIC with -std=c99. */
#define _POSIX_C_SOURCE 199309L
#include <time.h>

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#define LOXC_BENCH_HAS_CLOCK_GETTIME 1
#endif

#include "loxc.h"

#if defined(LOXC_BENCH_WITH_DEMO)
#include "loxc_demo.h"
#endif

static void usage(FILE *out) {
  fprintf(out,
          "Usage:\n"
          "  loxc_bench --module <name> [--file <path>]\n"
          "\n"
      "Notes:\n"
      "  - The module must be registered before benchmarking.\n"
      "    (e.g. call your generated loxc_mod_<name>_register() somewhere.)\n");
}

static int parse_args(int argc, char **argv, const char **out_module,
                      const char **out_file) {
  *out_module = NULL;
  *out_file = NULL;

  for (int i = 1; i < argc; i++) {
    if (strcmp(argv[i], "--module") == 0 && i + 1 < argc) {
      *out_module = argv[++i];
    } else if (strcmp(argv[i], "--file") == 0 && i + 1 < argc) {
      *out_file = argv[++i];
    } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
      usage(stdout);
      exit(0);
    } else {
      fprintf(stderr, "Error: unknown arg: %s\n", argv[i]);
      usage(stderr);
      return 1;
    }
  }

  if (*out_module == NULL) {
    fprintf(stderr, "Error: missing --module\n");
    usage(stderr);
    return 1;
  }
  return 0;
}

static int register_module_for_bench(const char *module_name) {
#if defined(LOXC_BENCH_WITH_DEMO)
  if (strcmp(module_name, "demo") == 0) {
    return loxc_mod_demo_register();
  }
#endif

  fprintf(stderr,
          "Error: module '%s' is not registered.\n"
          "Hint: link loxc_bench with your module and call loxc_mod_%s_register().\n",
          module_name, module_name);
  return 1;
}

static int read_entire_file(const char *path, uint8_t **out_buf,
                            size_t *out_len) {
  *out_buf = NULL;
  *out_len = 0;

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "Error: fopen(%s): %s\n", path, strerror(errno));
    return 1;
  }

  if (fseek(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "Error: fseek end (%s): %s\n", path, strerror(errno));
    fclose(f);
    return 1;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fprintf(stderr, "Error: ftell (%s): %s\n", path, strerror(errno));
    fclose(f);
    return 1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "Error: fseek set (%s): %s\n", path, strerror(errno));
    fclose(f);
    return 1;
  }

  uint8_t *buf = NULL;
  if (sz > 0) {
    buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL) {
      fprintf(stderr, "Error: malloc(%ld) failed\n", sz);
      fclose(f);
      return 1;
    }
  }

  size_t nread = 0;
  if (sz > 0) nread = fread(buf, 1, (size_t)sz, f);
  fclose(f);

  if (nread != (size_t)sz) {
    fprintf(stderr, "Error: read %zu/%ld bytes (%s)\n", nread, sz, path);
    free(buf);
    return 1;
  }

  *out_buf = buf;
  *out_len = (size_t)sz;
  return 0;
}

static double now_ms(void) {
#if LOXC_BENCH_HAS_CLOCK_GETTIME
  struct timespec ts;
  if (clock_gettime(CLOCK_MONOTONIC, &ts) == 0) {
    return (double)ts.tv_sec * 1000.0 + (double)ts.tv_nsec / 1e6;
  }
#endif
  return (double)clock() * (1000.0 / (double)CLOCKS_PER_SEC);
}

static void print_table_header(void) {
  printf("File                           | Size      | Encoded   | Ratio   | Enc Time | Dec Time | OK\n");
  printf("-------------------------------+-----------+-----------+---------+----------+----------+----\n");
}

static void print_table_row(const char *label, size_t input_size,
                            size_t encoded_size, double ratio_percent,
                            double enc_ms, double dec_ms, const char *ok) {
  printf("%-30s | %9zu | %9zu | %6.1f%% | %7.2fms | %7.2fms | %s\n", label,
         input_size, encoded_size, ratio_percent, enc_ms, dec_ms, ok);
}

static int try_compress_len(const char *module, const uint8_t *input,
                            size_t input_len, size_t len) {
  size_t out_cap = len * 2u;
  if (out_cap < len) out_cap = len;
  out_cap += 4096u;

  uint8_t *encoded = (uint8_t *)malloc(out_cap);
  if (encoded == NULL) return LOXC_ERR_OVERFLOW;

  size_t encoded_actual = 0;
  int rc = loxc_compress(module, (const char *)input, len, encoded, &out_cap,
                         &encoded_actual);
  free(encoded);
  return rc;
}

static void report_first_unsupported_byte(const char *module,
                                          const uint8_t *input,
                                          size_t input_len) {
  if (input_len == 0) return;

  int rc0 = try_compress_len(module, input, input_len, 1);
  if (rc0 == LOXC_ERR_SYMBOL_NOT_FOUND) {
    fprintf(stderr, "unsupported: pos=0 byte=0x%02x\n", (unsigned)input[0]);
    return;
  }
  if (rc0 != LOXC_OK) return;

  size_t lo = 1;          /* known OK */
  size_t hi = input_len;  /* known bad in caller */
  while (hi - lo > 1) {
    size_t mid = lo + (hi - lo) / 2u;
    int rc = try_compress_len(module, input, input_len, mid);
    if (rc == LOXC_ERR_SYMBOL_NOT_FOUND) {
      hi = mid;
    } else {
      lo = mid;
    }
  }

  size_t pos = hi - 1;
  uint8_t b = input[pos];
  if (b >= 0x20u && b <= 0x7eu) {
    fprintf(stderr, "unsupported: pos=%zu byte=0x%02x ('%c')\n", pos,
            (unsigned)b, (char)b);
  } else {
    fprintf(stderr, "unsupported: pos=%zu byte=0x%02x\n", pos, (unsigned)b);
  }
}

static int bench_one(const char *module, const char *path) {
  uint8_t *input = NULL;
  size_t input_len = 0;
  if (read_entire_file(path, &input, &input_len) != 0) return 1;

  /* Start with a generous buffer and grow on demand. */
  size_t out_cap = input_len * 2u;
  if (out_cap < input_len) out_cap = input_len;
  out_cap += 4096u;
  uint8_t *encoded = (uint8_t *)malloc(out_cap);
  if (encoded == NULL) {
    fprintf(stderr, "Error: malloc encoded failed\n");
    free(input);
    return 1;
  }

  double t0 = now_ms();
  size_t encoded_actual = 0;
  int rc = LOXC_OK;
  for (int attempt = 0; attempt < 6; attempt++) {
    size_t cap_arg = out_cap;
    encoded_actual = 0;
    rc = loxc_compress(module, (const char *)input, input_len, encoded,
                       &cap_arg, &encoded_actual);
    if (rc != LOXC_ERR_OVERFLOW) break;

    size_t next_cap = out_cap * 2u;
    if (next_cap < out_cap) break;
    next_cap += 4096u;
    uint8_t *grown = (uint8_t *)realloc(encoded, next_cap);
    if (grown == NULL) break;
    encoded = grown;
    out_cap = next_cap;
  }
  double t1 = now_ms();
  const double enc_ms = t1 - t0;

  if (rc != LOXC_OK) {
    if (rc == LOXC_ERR_SYMBOL_NOT_FOUND) {
      printf("%-30s | %9zu | %9s | %7s | %8s | %8s | %s\n", path, input_len,
             "-", "-", "-", "-", "UNSUPPORTED");
      report_first_unsupported_byte(module, input, input_len);
      free(encoded);
      free(input);
      return 0;
    }
    fprintf(stderr, "Error: loxc_compress(%s, %s) failed rc=%d\n", module, path,
            rc);
    free(encoded);
    free(input);
    return 1;
  }

  size_t dec_cap = input_len + 16u;
  char *decoded = (char *)malloc(dec_cap);
  if (decoded == NULL) {
    fprintf(stderr, "Error: malloc decoded failed\n");
    free(encoded);
    free(input);
    return 1;
  }

  size_t decoded_actual = 0;
  t0 = now_ms();
  rc = loxc_decompress(encoded, encoded_actual, decoded, &dec_cap, &decoded_actual);
  t1 = now_ms();
  const double dec_ms = t1 - t0;

  int ok = 1;
  if (rc != LOXC_OK) ok = 0;
  if (ok && decoded_actual != input_len) ok = 0;
  if (ok && memcmp(decoded, input, input_len) != 0) ok = 0;

  const double ratio =
      input_len ? (100.0 * (double)encoded_actual / (double)input_len) : 0.0;

  print_table_row(path, input_len, encoded_actual, ratio, enc_ms, dec_ms,
                  ok ? "OK" : "FAIL");

  free(decoded);
  free(encoded);
  free(input);
  return ok ? 0 : 1;
}

static int run_default_suite(const char *module) {
  const char *files[] = {
    "trainings/demo_corpus.txt",
    "benchmarks/plain_sample_text.txt",
    "benchmarks/tiny.txt",
    "benchmarks/small.txt",
    "benchmarks/medium.txt",
    "benchmarks/source.c",
    "benchmarks/data.json",
  };

  print_table_header();

  int failures = 0;
  for (size_t i = 0; i < (sizeof(files) / sizeof(files[0])); i++) {
    const char *path = files[i];
    FILE *probe = fopen(path, "rb");
    if (probe == NULL) {
      printf("%-30s | %9s | %9s | %7s | %8s | %8s | %s\n",
             path, "-", "-", "-", "-", "-", "SKIP");
      continue;
    }
    fclose(probe);
    if (bench_one(module, path) != 0) failures++;
  }
  return failures ? 1 : 0;
}

int main(int argc, char **argv) {
  const char *module = NULL;
  const char *file = NULL;
  if (parse_args(argc, argv, &module, &file) != 0) return 2;

  if (register_module_for_bench(module) != 0) return 3;

  if (file != NULL) {
    print_table_header();
    return bench_one(module, file);
  }
  return run_default_suite(module);
}
