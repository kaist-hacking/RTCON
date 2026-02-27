#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "crash.h"
#include "fuzz.h"

/*-----------------------------------------------------------*/

static sigjmp_buf main_jump_buffer;
static struct sigaction default_sa;

/* Basic signal handler */
void ground_signal_handler(int signum, siginfo_t *info, void *context) {
  siglongjmp(main_jump_buffer, 2);
}

/*-----------------------------------------------------------*/

extern void resetHookCounter(void);
extern void registerNeverFree(void *);
extern void clearNeverFree(void);

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testRawData(Data, Size);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) 
int LLVMFuzzerTestOneInput(const __uint8_t *Data, size_t Size) {
  register_asan_report_cb();

  hookASANMalloc();

  MAIN_TRY {
    size_t fuzz_size = validateInput(Data, Size);

    if (fuzz_size) {
      // Reset hook counter
      resetHookCounter();

      // Free previously allocated memory
      clearNeverFree();

      __uint8_t *data = (__uint8_t *)malloc(fuzz_size);
      if (data) {
        registerNeverFree(data);

        memset(data, 0, fuzz_size);
        memcpy(data, Data, fuzz_size);

        fuzzEntryFunctionHelper(data, fuzz_size);
      }
    }
  }
  MAIN_CATCH {
    // Do nothing
  }
  MAIN_END_TRY;

  unhookASANMalloc();

  return 0;
}