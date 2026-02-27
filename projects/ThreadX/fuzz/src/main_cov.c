#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include "crash.h"
#include "fuzz.h"

/*-----------------------------------------------------------*/

#include "nx_api.h"

/*-----------------------------------------------------------*/

static sigjmp_buf main_jump_buffer;
static struct sigaction default_sa;

/* Basic signal handler */
void ground_signal_handler(int signum, siginfo_t *info, void *context) {
  siglongjmp(main_jump_buffer, 2);
}

/*-----------------------------------------------------------*/
extern void fuzzEntryFunction(__uint8_t *Data, size_t Size);
__attribute__((weak, no_sanitize("coverage"), no_sanitize("address"))) void
testNXPacketData(__uint8_t *Data, size_t Size) {
  NX_PACKET packet;
  memset(&packet, 0, sizeof(NX_PACKET));
  packet.nx_packet_prepend_ptr = Data;
  packet.nx_packet_length = Size;

  fuzzEntryFunction((uint8_t *)&packet, sizeof(NX_PACKET));
}

/*-----------------------------------------------------------*/
extern void resetHookCounter(void);
extern void registerNeverFree(void *);
extern void clearNeverFree(void);

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testRawData(Data, Size);
}

__attribute__((no_sanitize("address"))) int main(int argc, char *argv[]) {
  if (argc < 2) {
    printf("Usage: %s <filename>\n", argv[0]);
    return 1;
  }

  const char *filename = argv[1];
  FILE *file = fopen(filename, "rb");
  if (file == NULL) {
    printf("Failed to open file: %s\n", filename);
    return 1;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);

  char *Data = (char *)malloc(size);
  if (Data == NULL) {
    printf("Failed to allocate memory\n");
    fclose(file);
    return 1;
  }

  size_t bytesRead = fread(Data, 1, size, file);
  if (bytesRead != size) {
    printf("Failed to read file: %s\n", filename);
    free(Data);
    fclose(file);
    return 1;
  }

  fclose(file);

  register_asan_report_cb();

  MAIN_TRY {
    size_t fuzz_size = validateInput(Data, size);

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
    free(Data);
  }
  MAIN_CATCH {
    // Do nothing
  }
  MAIN_END_TRY;

  exit(0);
}