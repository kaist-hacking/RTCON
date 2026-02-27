#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#define MAX_STATE_BYTES 128
uint8_t state_buf[MAX_STATE_BYTES];

__uint8_t *fuzz_data_orig;
size_t fuzz_size_orig;

extern void registerNeverFree(void *);
extern void clearNeverFree(void);
extern void fuzzEntryFunction(__uint8_t *Data, size_t Size);

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) size_t
validateInput(const __uint8_t *Data, size_t Size) {
  if (Size < MAX_STATE_BYTES) {
    return 0;
  }

  size_t fuzz_size = Size - MAX_STATE_BYTES;
  size_t state_size = MAX_STATE_BYTES;

  const __uint8_t *state_data = Data + fuzz_size;

  /* Copy state_buf */
  memcpy(state_buf, state_data, state_size);

  /* Save the fuzz data */
  fuzz_data_orig = Data;
  fuzz_size_orig = Size;

  return fuzz_size;
}
/*------------------------------------------------------------------*/
/* Custom Data Input attribute weak */
__attribute__((weak, no_sanitize("coverage"), no_sanitize("address"))) void
testCustomData(__uint8_t *Data, size_t Size) {}

/*------------------------------------------------------------------*/

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
testRawData(__uint8_t *Data, size_t Size) {
  fuzzEntryFunction(Data, Size);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
testStringData(__uint8_t *Data, size_t Size) {
  if (Size < 1)
    return;
  Data[Size - 1] = '\0';
  fuzzEntryFunction(Data, Size - 1);
}

const char *__asan_default_options() {
  return "detect_stack_use_after_return=false:halt_on_error=false:log_path="
         "stdout:suppress_equal_pcs=false:detect_leaks=0:handle_segv=0";
}