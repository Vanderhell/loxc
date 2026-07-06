#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_simple.h"
#include "loxc_tab.h"

static int consumer_cpp_smoke() {
  loxc_ctx_t *ctx = 0;
  loxc_check_file_result_t check;
  const char *(*strerror_fn)(int) = loxc_strerror;

  memset(&check, 0, sizeof(check));
  (void)ctx;
  (void)check;
  (void)strerror_fn;
  return LOXC_OK;
}

int main() {
  return consumer_cpp_smoke();
}
