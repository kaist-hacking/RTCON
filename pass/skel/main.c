#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <libgen.h>

#include "crash.h"
#include "fuzz.h"

/*-----------------------------------------------------------*/
/* This should be replaced by the pass generated function */
// __attribute__((weak))
// void fuzzEntryFunction(void) {
//     printf("Empty fuzzEntryFunction\n");
//     exit(0);
// }

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

extern void testCustomData(__uint8_t *Data, size_t Size);
static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  // testRawData(Data, Size);
  testCustomData(Data, Size);
}

static __uint8_t *data = NULL;
static size_t fuzz_size = 0;
static uint32_t data_offset = 0;

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) 
int LLVMFuzzerTestOneInput(const __uint8_t *Data, size_t Size) {
  register_asan_report_cb();

  hookASANMalloc();

  MAIN_TRY {
    fuzz_size = validateInput(Data, Size);

    if (fuzz_size) {
      // Reset hook counter
      resetHookCounter();

      // Free previously allocated memory
      clearNeverFree();

      data = (__uint8_t *)malloc(fuzz_size);
      data_offset = 0;
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

/******************************************************************************
 * Default hook functions
 ******************************************************************************/

/* It is necessary to define a dummy file for the target program to use */
FILE dummy_file;
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) FILE *
hook_fopen(const char *filename, const char *mode) {
  return &dummy_file;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
hook_fgetc(FILE *stream) {
  if (data && (data_offset < fuzz_size)) {
    int ret = data[data_offset];
    data_offset++;
    return ret;
  } else {
    return EOF;
  }
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) size_t
hook_fread(int *buf, size_t size, size_t count, FILE *stream) {
  if (data && (data_offset < fuzz_size)) {
    size_t i;
    for (i = 0; i < (size_t)(size * count); i++) {
      if (data_offset >= fuzz_size) {
        break;
      }
      buf[i] = data[data_offset];
      data_offset++;
    }
    return i;
  } else {
    return 0;
  }
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) char *
hook_fgets(char *str, int n, FILE *stream) {
  if (data && (data_offset < fuzz_size)) {
    size_t i;
    for (i = 0; i < (size_t)(n - 1); i++) {
      if (data_offset >= fuzz_size) {
        break;
      }
      str[i] = data[data_offset];
      data_offset++;
      if (str[i] == '\n') {
        i++;
        break;
      }
    }
    str[i] = '\0';
    return str;
  } else {
    return NULL;
  }
}

/******************************************************************************
 * Redirect stdout and stderr to files
 ******************************************************************************/
__attribute__((no_sanitize("coverage"), no_sanitize("address")))
void redirect_stdio(const char *argv0) {
    char *base = strdup(argv0);
    if (!base) return;

    char *prog = basename(base);

    char out_name[512];
    // char err_name[512];

    snprintf(out_name, sizeof(out_name), "%s_stdout", prog);
    // snprintf(err_name, sizeof(err_name), "%s_stderr", prog);

    int out_fd = open(out_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    // int err_fd = open(err_name, O_WRONLY | O_CREAT | O_TRUNC, 0644);

    if (out_fd >= 0) {
        dup2(out_fd, STDOUT_FILENO);
        close(out_fd);
    }

    // if (err_fd >= 0) {
    //     dup2(err_fd, STDERR_FILENO);
    //     close(err_fd);
    // }

    free(base);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address")))
int LLVMFuzzerInitialize(int *argc, char ***argv) {
  redirect_stdio((*argv)[0]);
  return 0;
}
