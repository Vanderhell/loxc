#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#define LOXC_BENCH_HAS_CLOCK_GETTIME 1
#endif

#include "loxc.h"

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

static int bench_one(const char *module, const char *path) {
  uint8_t *input = NULL;
  size_t input_len = 0;
  if (read_entire_file(path, &input, &input_len) != 0) return 1;

  /* Conservative capacity guess to avoid a retry path in the harness. */
  size_t out_cap = (input_len * 2u) + 4096u;
  if (out_cap < input_len) out_cap = input_len + 4096u;
  uint8_t *encoded = (uint8_t *)malloc(out_cap);
  if (encoded == NULL) {
    fprintf(stderr, "Error: malloc encoded failed\n");
    free(input);
    return 1;
  }

  size_t encoded_actual = 0;
  double t0 = now_ms();
  int rc = loxc_compress(module, (const char *)input, input_len, encoded,
                         &out_cap, &encoded_actual);
  double t1 = now_ms();
  const double enc_ms = t1 - t0;

  if (rc != LOXC_OK) {
    fprintf(stderr, "Error: loxc_compress(%s, %s) failed rc=%d\n", module, path, rc);
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

  printf("%-30s | %9zu | %9zu | %6.1f%% | %7.2fms | %7.2fms | %s\n",
         path, input_len, encoded_actual, ratio, enc_ms, dec_ms, ok ? "OK" : "FAIL");

  free(decoded);
  free(encoded);
  free(input);
  return ok ? 0 : 1;
}

static int run_default_suite(const char *module) {
  const char *files[] = {
    "trainings/demo_corpus.txt",
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

  if (file != NULL) {
    print_table_header();
    return bench_one(module, file);
  }
  return run_default_suite(module);
}
