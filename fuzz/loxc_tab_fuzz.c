#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "loxc_tab.h"

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size) {
  loxc_module_t *module = NULL;
  loxc_tab_error_t err;

  if (loxc_module_load_from_memory_ex(data, size, "fuzz_table", &module, &err) ==
      LOXC_OK) {
    (void)loxc_module_unload(module);
  }
  return 0;
}

#if defined(LOXC_FUZZ_STANDALONE)
static int fuzz_file(const char *path) {
  FILE *f = fopen(path, "rb");
  uint8_t *buf = NULL;
  long file_size = 0;
  size_t nread = 0u;

  if (f == NULL) return 1;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return 1;
  }
  file_size = ftell(f);
  if (file_size < 0 || fseek(f, 0, SEEK_SET) != 0) {
    fclose(f);
    return 1;
  }

  if (file_size > 0) {
    buf = (uint8_t *)malloc((size_t)file_size);
    if (buf == NULL) {
      fclose(f);
      return 1;
    }
    nread = fread(buf, 1, (size_t)file_size, f);
  }
  fclose(f);
  if (nread != (size_t)file_size) {
    free(buf);
    return 1;
  }

  (void)LLVMFuzzerTestOneInput(buf, (size_t)file_size);
  free(buf);
  return 0;
}

int main(int argc, char **argv) {
  int rc = 0;
  for (int i = 1; i < argc; i++) {
    if (fuzz_file(argv[i]) != 0) rc = 1;
  }
  return rc;
}
#endif
