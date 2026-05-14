#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "loxc.h"
#include "loxc_tab.h"

int main(void) {
  loxc_module_t *module = loxc_module_load_from_file("modules/loxc_demo.loxctab");
  assert(module != NULL);
  assert(loxc_module_register(module) == LOXC_OK);

  const char *text = "Embedded mode keeps the table in the file.";
  uint8_t encoded[16384];
  size_t encoded_cap = sizeof(encoded);
  size_t encoded_len = 0;
  assert(loxc_compress_with_options("demo", text, strlen(text), encoded,
                                    &encoded_cap, &encoded_len, 1) == LOXC_OK);

  loxc_module_unload(module);

  char decoded[16384];
  size_t decoded_cap = sizeof(decoded);
  size_t decoded_len = 0;
  assert(loxc_decompress(encoded, encoded_len, decoded, &decoded_cap,
                         &decoded_len) == LOXC_OK);
  assert(decoded_len == strlen(text));
  assert(memcmp(decoded, text, decoded_len) == 0);

  printf("02_embedded_mode: input=%zu encoded=%zu decoded=%zu\n",
         strlen(text), encoded_len, decoded_len);
  return 0;
}
