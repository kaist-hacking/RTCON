#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <setjmp.h>

#include "crash.h"
#include "fuzz.h"

#include <zephyr/net/buf.h>

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

/*-----------------------------------------------------------*/

/* TODO: They would be changed by test_selector.py. It is not good design*/

/*-----------------------------------------------------------*/

extern void fuzzEntryFunction(__uint8_t *Data, size_t Size);
void testNetBufSimple(__uint8_t *Data, size_t Size) {
  struct net_buf_simple buf;
  buf.data = Data;
  buf.__buf = Data;
  buf.size = Size;
  buf.len = Size;
  fuzzEntryFunction(&buf, Size);
}

void testNetBuf(__uint8_t *Data, size_t Size) {
  // For metadata
  char data[1024];
  struct net_buf *buf = (struct net_buf *)data;
  buf->data = Data;
  buf->__buf = Data;
  buf->size = Size;
  buf->len = Size;
  fuzzEntryFunction(buf, Size);
}

/*-----------------------------------------------------------*/

#if (TEST_IDX == 0)

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testRawData(Data, Size);
}

#elif (TEST_IDX == 1)

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testNetBuf(Data, Size);
}

/* TODO */
/* Optional Param Checks */
bool checkParamValid(void **param, int lengthIndex) {

  /* First check the validity */  
  struct net_buf *buf = *(struct net_buf **)param;
  if (buf->len > 2048)
    return false;

  /* Type is different */
  if (lengthIndex != -1) {
    return false;
  }

  return true;
}

/* TODO */
/* Change input type net_buf to other test type */
bool checkCrossValid(void **input, int input_index, uint64_t *length, int length_index) {
  if (length_index != -1) {
    struct net_buf *buf = *(struct net_buf **)input;
    uint8_t *data = buf->data;
    *(uint8_t **)input = data;
  }
  return true;
}

#elif (TEST_IDX == 2)

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testNetBufSimple(Data, Size);
}

/* TODO */
bool checkParamValid(void **param, int lengthIndex) {
  struct net_buf_simple *buf = *(struct net_buf **)param;
  if (buf->len > 2048)
    return false;
  
  if (lengthIndex != -1) {
    return false;
  }

  return true;
}

/* TODO */
/* Change input type net_buf to other test type */
bool checkCrossValid(void **input, int input_index, uint64_t *length, int length_index) {
  if (length_index != -1) {
    struct net_buf_simple *buf = *(struct net_buf_simple **)input;
    uint8_t *data = buf->data;
    *(uint8_t **)input = data;
  }
  return true;
}

#endif

/*-----------------------------------------------------------*/

#ifdef FUZZ_COVERAGE
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>

#define COVERAGE_INTERVAL (30 * 60)
#define COVERAGE_UPDATE_INTERVAL 60

#ifndef COVERAGE_DIR_PATH
#define COVERAGE_DIR_PATH "coverage_dir"
#endif

#define FLUSH_INTERVAL 5000
uint32_t iteration = 0;

extern int __llvm_profile_write_file(void);
extern void __llvm_profile_set_filename(char *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
periodic_coverage_flush(void) {
  if ((iteration++ % FLUSH_INTERVAL) == 0) {
    /* Set coverage file name */
    char coverage_temp_file_name[256];
    char coverage_file_name[256];
    snprintf(coverage_temp_file_name, 256, "%s/%ld.profraw.tmp", COVERAGE_DIR_PATH,
             time(NULL) / COVERAGE_INTERVAL);
    snprintf(coverage_file_name, 256, "%s/%ld.profraw", COVERAGE_DIR_PATH,
             time(NULL) / COVERAGE_INTERVAL);
    __llvm_profile_set_filename(coverage_temp_file_name);

    __llvm_profile_write_file();

    /* If new coverage temp file is bigger than the previous one, update it */
    if (access(coverage_temp_file_name, F_OK) != -1) {
      if (access(coverage_file_name, F_OK) != -1) {
        struct stat st_prev, st_new;
        stat(coverage_file_name, &st_prev);
        stat(coverage_temp_file_name, &st_new);

        if (st_new.st_size >= st_prev.st_size) {
          rename(coverage_temp_file_name, coverage_file_name);
        } else {
          unlink(coverage_temp_file_name);
        }
      } else {
        rename(coverage_temp_file_name, coverage_file_name);
      }
    }
  }
}
#endif

/*-----------------------------------------------------------*/
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
LLVMFuzzerTestOneInput(const __uint8_t *Data, size_t Size) {

#ifdef FUZZ_COVERAGE
  periodic_coverage_flush();
#endif

  register_asan_report_cb();

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

  return 0;
}
