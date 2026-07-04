#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_tab.h"

static void usage(FILE *out) {
  fprintf(out,
          "Usage:\n"
          "  loxc compress [--table tab.loxctab | --module <name>] [--embed] input.txt output.loxc\n"
          "  loxc decompress [--table tab.loxctab | --module <name>] input.loxc output.txt\n"
          "  loxc info input.loxc\n");
}

static const char *err_name(int rc) {
  switch (rc) {
    case LOXC_OK: return "LOXC_OK";
    case LOXC_ERR_NULL: return "LOXC_ERR_NULL";
    case LOXC_ERR_TRUNCATED: return "LOXC_ERR_TRUNCATED";
    case LOXC_ERR_OVERFLOW: return "LOXC_ERR_OVERFLOW";
    case LOXC_ERR_INVALID_MAGIC: return "LOXC_ERR_INVALID_MAGIC";
    case LOXC_ERR_SYMBOL_NOT_FOUND: return "LOXC_ERR_SYMBOL_NOT_FOUND";
    case LOXC_ERR_INVALID_FORMAT: return "LOXC_ERR_INVALID_FORMAT";
    case LOXC_ERR_MODULE_NOT_FOUND: return "LOXC_ERR_MODULE_NOT_FOUND";
    case LOXC_ERR_REGISTRY_FULL: return "LOXC_ERR_REGISTRY_FULL";
    case LOXC_ERR_DUPLICATE_MODULE: return "LOXC_ERR_DUPLICATE_MODULE";
    case LOXC_ERR_INVALID_MODULE: return "LOXC_ERR_INVALID_MODULE";
    default: return "LOXC_ERR_UNKNOWN";
  }
}

static const char *strategy_name(uint8_t strategy) {
  switch (strategy) {
    case LOXC_STRATEGY_FLAT_FIXED_WIDTH: return "FLAT_FIXED_WIDTH";
    case LOXC_STRATEGY_HIERARCHICAL_8: return "HIERARCHICAL_8";
    case LOXC_STRATEGY_HIERARCHICAL_4: return "HIERARCHICAL_4";
    default: return "UNKNOWN";
  }
}

static int read_entire_file(const char *path, uint8_t **out_buf,
                            size_t *out_len) {
  *out_buf = NULL;
  *out_len = 0;

  FILE *f = fopen(path, "rb");
  if (f == NULL) {
    fprintf(stderr, "loxc: cannot open %s: %s\n", path, strerror(errno));
    return 1;
  }
  if (fseek(f, 0, SEEK_END) != 0) {
    fprintf(stderr, "loxc: cannot seek %s: %s\n", path, strerror(errno));
    fclose(f);
    return 1;
  }
  long sz = ftell(f);
  if (sz < 0) {
    fprintf(stderr, "loxc: cannot tell %s: %s\n", path, strerror(errno));
    fclose(f);
    return 1;
  }
  if (fseek(f, 0, SEEK_SET) != 0) {
    fprintf(stderr, "loxc: cannot seek %s: %s\n", path, strerror(errno));
    fclose(f);
    return 1;
  }

  uint8_t *buf = NULL;
  if (sz > 0) {
    buf = (uint8_t *)malloc((size_t)sz);
    if (buf == NULL) {
      fprintf(stderr, "loxc: out of memory reading %s\n", path);
      fclose(f);
      return 1;
    }
  }

  size_t nread = 0;
  if (sz > 0) nread = fread(buf, 1, (size_t)sz, f);
  fclose(f);
  if (nread != (size_t)sz) {
    fprintf(stderr, "loxc: short read %s (%zu/%ld bytes)\n", path, nread, sz);
    free(buf);
    return 1;
  }

  *out_buf = buf;
  *out_len = (size_t)sz;
  return 0;
}

static int header_is_embedded(const uint8_t *input, size_t input_len,
                              int *out_embedded) {
  *out_embedded = 0;
  if (input == NULL) return LOXC_ERR_NULL;
  if (input_len < LOXC_HEADER_SIZE_V2) return LOXC_ERR_TRUNCATED;

  loxc_reader_t r;
  int rc = loxc_reader_init(&r, input, input_len);
  if (rc != LOXC_OK) return rc;

  loxc_header_t h;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) return rc;
  *out_embedded = (h.flags & LOXC_FLAG_EMBEDDED_TABLE) != 0;
  return LOXC_OK;
}

static void print_flags(uint8_t flags) {
  printf("flags: 0x%02x", (unsigned)flags);
  if (flags == 0) {
    printf(" (none)");
  } else {
    printf(" (");
    int first = 1;
    if ((flags & LOXC_FLAG_EMBEDDED_TABLE) != 0) {
      printf("%sEMBEDDED_TABLE", first ? "" : "|");
      first = 0;
    }
    printf(")");
  }
  printf("\n");
}

static int write_entire_file(const char *path, const uint8_t *buf, size_t len) {
  FILE *f = fopen(path, "wb");
  if (f == NULL) {
    fprintf(stderr, "loxc: cannot write %s: %s\n", path, strerror(errno));
    return 1;
  }
  size_t nwritten = 0;
  if (len > 0) nwritten = fwrite(buf, 1, len, f);
  if (fclose(f) != 0) {
    fprintf(stderr, "loxc: cannot close %s: %s\n", path, strerror(errno));
    return 1;
  }
  if (nwritten != len) {
    fprintf(stderr, "loxc: short write %s (%zu/%zu bytes)\n", path, nwritten, len);
    return 1;
  }
  return 0;
}

static int command_compress(int argc, char **argv) {
  const char *table_path = NULL;
  const char *module_name = NULL;
  int embed = 0;
  int pos = 2;

  while (pos < argc && strncmp(argv[pos], "--", 2) == 0) {
    if (strcmp(argv[pos], "--table") == 0 && pos + 1 < argc) {
      table_path = argv[pos + 1];
      pos += 2;
    } else if (strcmp(argv[pos], "--module") == 0 && pos + 1 < argc) {
      module_name = argv[pos + 1];
      pos += 2;
    } else if (strcmp(argv[pos], "--embed") == 0) {
      embed = 1;
      pos++;
    } else {
      usage(stderr);
      return 2;
    }
  }

  if ((table_path == NULL && module_name == NULL) ||
      (table_path != NULL && module_name != NULL) ||
      argc - pos != 2) {
    usage(stderr);
    return 2;
  }

  loxc_module_t *loaded = NULL;
  if (table_path != NULL) {
    loaded = loxc_module_load_from_file(table_path);
    if (loaded == NULL) return 1;
    int rc = loxc_module_register(loaded);
    if (rc != LOXC_OK) {
      fprintf(stderr, "loxc: module register failed: %s (%d)\n", err_name(rc), rc);
      loxc_module_unload(loaded);
      return 1;
    }
    module_name = loaded->name;
  }

  const char *input_path = argv[pos];
  const char *output_path = argv[pos + 1];

  uint8_t *input = NULL;
  size_t input_len = 0;
  if (read_entire_file(input_path, &input, &input_len) != 0) {
    if (loaded != NULL) loxc_module_unload(loaded);
    return 1;
  }

  size_t out_cap = input_len * 2u + 4096u;
  if (out_cap < input_len) out_cap = input_len + 4096u;
  uint8_t *output = (uint8_t *)malloc(out_cap);
  if (output == NULL) {
    fprintf(stderr, "loxc: out of memory allocating output buffer\n");
    free(input);
    if (loaded != NULL) loxc_module_unload(loaded);
    return 1;
  }

  size_t actual = 0;
  int rc = LOXC_OK;
  for (int attempt = 0; attempt < 8; attempt++) {
    size_t cap_arg = out_cap;
    actual = 0;
    rc = loxc_compress_with_options(module_name, (const char *)input, input_len,
                                    output, &cap_arg, &actual, embed);
    if (rc != LOXC_ERR_OVERFLOW) break;
    size_t next_cap = out_cap * 2u + 4096u;
    if (next_cap < out_cap) break;
    uint8_t *grown = (uint8_t *)realloc(output, next_cap);
    if (grown == NULL) break;
    output = grown;
    out_cap = next_cap;
  }

  if (rc != LOXC_OK) {
    fprintf(stderr, "loxc: compress failed: %s (%d)\n", err_name(rc), rc);
    free(output);
    free(input);
    if (loaded != NULL) loxc_module_unload(loaded);
    return 1;
  }

  int wrc = write_entire_file(output_path, output, actual);
  if (wrc == 0) {
    fprintf(stderr, "loxc: compressed %zu bytes to %zu bytes\n", input_len, actual);
  }
  free(output);
  free(input);
  if (loaded != NULL) loxc_module_unload(loaded);
  return wrc;
}

static int command_decompress(int argc, char **argv) {
  const char *table_path = NULL;
  const char *module_name = NULL;
  int pos = 2;

  while (pos < argc && strncmp(argv[pos], "--", 2) == 0) {
    if (strcmp(argv[pos], "--table") == 0 && pos + 1 < argc) {
      table_path = argv[pos + 1];
      pos += 2;
    } else if (strcmp(argv[pos], "--module") == 0 && pos + 1 < argc) {
      module_name = argv[pos + 1];
      pos += 2;
    } else {
      usage(stderr);
      return 2;
    }
  }

  if ((table_path != NULL && module_name != NULL) || argc - pos != 2) {
    usage(stderr);
    return 2;
  }

  const char *input_path = argv[pos];
  const char *output_path = argv[pos + 1];

  uint8_t *input = NULL;
  size_t input_len = 0;
  if (read_entire_file(input_path, &input, &input_len) != 0) return 1;

  int embedded = 0;
  int hrc = header_is_embedded(input, input_len, &embedded);
  if (hrc != LOXC_OK) {
    fprintf(stderr, "loxc: cannot read input header: %s (%d)\n", err_name(hrc), hrc);
    free(input);
    return 1;
  }

  loxc_module_t *loaded = NULL;
  if (!embedded) {
    if (table_path == NULL && module_name == NULL) {
      fprintf(stderr, "loxc: external-table file requires --table or --module\n");
      free(input);
      return 2;
    }
    if (table_path != NULL) {
      loaded = loxc_module_load_from_file(table_path);
      if (loaded == NULL) {
        free(input);
        return 1;
      }
      int rc = loxc_module_register(loaded);
      if (rc != LOXC_OK) {
        fprintf(stderr, "loxc: module register failed: %s (%d)\n", err_name(rc), rc);
        loxc_module_unload(loaded);
        free(input);
        return 1;
      }
    }
  }

  size_t out_cap = input_len * 4u + 4096u;
  if (out_cap < input_len) out_cap = input_len + 4096u;
  uint8_t *output = (uint8_t *)malloc(out_cap);
  if (output == NULL) {
    fprintf(stderr, "loxc: out of memory allocating output buffer\n");
    if (loaded != NULL) loxc_module_unload(loaded);
    free(input);
    return 1;
  }

  size_t actual = 0;
  int rc = LOXC_OK;
  for (int attempt = 0; attempt < 10; attempt++) {
    size_t cap_arg = out_cap;
    actual = 0;
    rc = loxc_decompress(input, input_len, (char *)output, &cap_arg, &actual);
    if (rc != LOXC_ERR_OVERFLOW) break;
    size_t next_cap = out_cap * 2u + 4096u;
    if (next_cap < out_cap) break;
    uint8_t *grown = (uint8_t *)realloc(output, next_cap);
    if (grown == NULL) break;
    output = grown;
    out_cap = next_cap;
  }

  if (rc != LOXC_OK) {
    fprintf(stderr, "loxc: decompress failed: %s (%d)\n", err_name(rc), rc);
    free(output);
    if (loaded != NULL) loxc_module_unload(loaded);
    free(input);
    return 1;
  }

  int wrc = write_entire_file(output_path, output, actual);
  if (wrc == 0) {
    fprintf(stderr, "loxc: decompressed %zu bytes to %zu bytes\n", input_len, actual);
  }
  free(output);
  if (loaded != NULL) loxc_module_unload(loaded);
  free(input);
  return wrc;
}

static int command_info(int argc, char **argv) {
  if (argc != 3) {
    usage(stderr);
    return 2;
  }

  uint8_t *input = NULL;
  size_t input_len = 0;
  if (read_entire_file(argv[2], &input, &input_len) != 0) return 1;

  loxc_reader_t r;
  int rc = loxc_reader_init(&r, input, input_len);
  if (rc != LOXC_OK) {
    fprintf(stderr, "loxc: cannot initialize reader: %s (%d)\n", err_name(rc), rc);
    free(input);
    return 1;
  }

  loxc_header_t h;
  rc = loxc_header_read(&r, &h);
  if (rc != LOXC_OK) {
    fprintf(stderr, "loxc: cannot read header: %s (%d)\n", err_name(rc), rc);
    free(input);
    return 1;
  }

  size_t header_bytes = loxc_header_size(&h);
  printf("file: %s\n", argv[2]);
  printf("magic_prefix: LXC\n");
  printf("magic_layout: 'L' 'X' 'C' module_id\n");
  printf("module_id: %u\n", (unsigned)h.module_id);
  printf("version: %u\n", (unsigned)h.version);
  print_flags(h.flags);
  printf("strategy: %s (%u)\n", strategy_name(h.strategy_id),
         (unsigned)h.strategy_id);
  printf("payload_len: %u\n", (unsigned)h.payload_len);
  printf("level_count: %u\n", (unsigned)h.level_count);
  printf("uncompressed_len: %u\n", (unsigned)h.uncompressed_len);
  printf("crc32: unsupported in v2\n");
  printf("header_bytes: %zu\n", header_bytes);
  printf("file_bytes: %zu\n", input_len);
  if (input_len >= header_bytes) {
    printf("payload_bytes: %zu\n", input_len - header_bytes);
  }

  free(input);
  return 0;
}

int main(int argc, char **argv) {
  if (argc < 2 || strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
    usage(argc < 2 ? stderr : stdout);
    return argc < 2 ? 2 : 0;
  }

  if (strcmp(argv[1], "compress") == 0) return command_compress(argc, argv);
  if (strcmp(argv[1], "decompress") == 0) return command_decompress(argc, argv);
  if (strcmp(argv[1], "info") == 0) return command_info(argc, argv);

  fprintf(stderr, "loxc: unknown command: %s\n", argv[1]);
  usage(stderr);
  return 2;
}
