#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "crash.h"
#include "fuzz.h"

#include "net/gnrc/pkt.h"

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

extern void fuzzEntryFunction(__uint8_t *Data, size_t Size);


struct os_mbuf {
    /**
     * Current pointer to data in the structure
     */
    uint8_t *om_data;
    /**
     * Flags associated with this buffer, see OS_MBUF_F_* defintions
     */
    uint8_t om_flags;
    /**
     * Length of packet header
     */
    uint8_t om_pkthdr_len;
    /**
     * Length of data in this buffer
     */
    uint16_t om_len;

    /**
     * The mbuf pool this mbuf was allocated out of
     */
    void *om_omp;

    void *om_next;

    /**
     * Pointer to the beginning of the data, after this buffer
     */
    uint8_t om_databuf[0];
};

struct os_mbuf_pkthdr {
    /**
     * Overall length of the packet.
     */
    uint16_t omp_len;
    /**
     * Flags
     */
    uint16_t omp_flags;

    void *omp_next;
};

void testOsMbuf(__uint8_t *Data, size_t Size) {
  if (Size < 1 + sizeof(struct os_mbuf_pkthdr)) {
    // om_flags and pkthdr are mandatory
    return;
  }
  void *buf = malloc(sizeof(struct os_mbuf) + Size - 1);
  if (buf) {
    // It would be freed in next iteration
    registerNeverFree(buf);

    memset(buf, 0, sizeof(struct os_mbuf) + Size - 1);
    memcpy(buf + sizeof(struct os_mbuf), Data + 1, Size - 1);

    struct os_mbuf *mbuf = (struct os_mbuf *)buf;

    struct os_mbuf_pkthdr *pkthdr = (struct os_mbuf_pkthdr *)(buf + 1);
    pkthdr->omp_len = Size - 1;
    mbuf->om_pkthdr_len = sizeof(struct os_mbuf_pkthdr);

    mbuf->om_flags = Data[0];
    mbuf->om_data = buf + sizeof(struct os_mbuf) + sizeof(struct os_mbuf_pkthdr);
    mbuf->om_len = Size - 1 - sizeof(struct os_mbuf_pkthdr);

    fuzzEntryFunction(mbuf, Size);
  }
}

void testGnrcPktSnip(__uint8_t *Data, size_t Size) {
  gnrc_pktsnip_t buf;
  /* Defined as the default */
  if (Size > 2048) {
    return;
  }
  /* Type field is mandatory */
  if (Size < 1) {
    return;
  }
  
  memset(&buf, 0, sizeof(gnrc_pktsnip_t));
  buf.data = Data + 1;
  buf.size = Size - 1;
  buf.users = 1;
  buf.type = Data[0];
  fuzzEntryFunction(&buf, Size);
}

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testRawData(Data, Size);
}

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

#define FLUSH_INTERVAL 300
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
extern void resetMemoryPoison(void);

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) 
int LLVMFuzzerTestOneInput(const __uint8_t *Data, size_t Size) {
  resetMemoryPoison();

#ifdef FUZZ_COVERAGE
  periodic_coverage_flush();
#endif

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
