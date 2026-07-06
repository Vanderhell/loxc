#include <string.h>

#include "loxc.h"
#include "loxc_base.h"
#include "loxc_simple.h"
#include "loxc_tab.h"

static int consumer_c_smoke(void) {
  loxc_check_file_result_t check;
  const char *(*strerror_fn)(int) = loxc_strerror;

  memset(&check, 0, sizeof(check));
  (void)strerror_fn;
  (void)check;
  return LOXC_OK;
}

int main(void) {
  return consumer_c_smoke();
}
