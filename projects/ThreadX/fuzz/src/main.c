#include <assert.h>
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  if (Size < 100)
    return;
  Size = Size & ~(0x3);

  NX_PACKET packet;
  memset(&packet, 0, sizeof(NX_PACKET));

  packet.nx_packet_prepend_ptr = Data + 50;
  packet.nx_packet_append_ptr = Data + Size;

  packet.nx_packet_data_start = Data;
  packet.nx_packet_data_end = Data + Size;

  packet.nx_packet_length = Size - 50;

  fuzzEntryFunction((uint8_t *)&packet, sizeof(NX_PACKET));
}

/*-----------------------------------------------------------*/

NX_PACKET recv_packet;
char *inject_data;
int inject_data_size;
// unsigned int hook__nx_udp_socket_receive(void *socket_ptr, NX_PACKET **packet_ptr, unsigned int wait_option) {
//   if (inject_data_size < 100) {
//     return 0;
//   }
//   inject_data_size = inject_data_size & ~(0x3);

//   recv_packet.nx_packet_prepend_ptr = inject_data + 50;
//   recv_packet.nx_packet_append_ptr = inject_data + inject_data_size;

//   recv_packet.nx_packet_data_start = inject_data;
//   recv_packet.nx_packet_data_end = inject_data + inject_data_size;

//   recv_packet.nx_packet_length = inject_data_size - 50;

//   *packet_ptr = &recv_packet;
//   return 0;
// }

extern void __asan_unpoison_memory_region(void *addr, size_t size);
extern void __asan_poison_memory_region(void *addr, size_t size);

NX_PACKET alloc_packet;

#define ALLOC_SIZE 1024
char alloc_data[ALLOC_SIZE];
unsigned int hook__nx_packet_allocate(void *pool_ptr,  NX_PACKET **packet_ptr,
                          unsigned int packet_type, unsigned int wait_option) {
  *packet_ptr = &alloc_packet;
  if (inject_data_size + packet_type > ALLOC_SIZE) {
    return NX_INVALID_PARAMETERS;
  }

  memset(&alloc_packet, 0, sizeof(NX_PACKET));
  __asan_unpoison_memory_region(alloc_data, ALLOC_SIZE);

  memset(alloc_data, 0, inject_data_size + packet_type);
  __asan_poison_memory_region(alloc_data + inject_data_size + packet_type, ALLOC_SIZE - inject_data_size - packet_type);

  alloc_packet.nx_packet_prepend_ptr = alloc_data + packet_type;
  alloc_packet.nx_packet_append_ptr = alloc_data + packet_type;

  alloc_packet.nx_packet_data_start = alloc_data;
  alloc_packet.nx_packet_data_end = alloc_data + inject_data_size + packet_type;
  
  return NX_SUCCESS;
}

unsigned int hook__nx_packet_release(NX_PACKET *packet_ptr) {
  return 0;
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

extern void resetHookCounter(void);
extern void registerNeverFree(void *);
extern void clearNeverFree(void);

static void fuzzEntryFunctionHelper(__uint8_t *Data, size_t Size) {
  testRawData(Data, Size); // will be replaced!!!
}

__attribute__((no_sanitize("address"))) 
int LLVMFuzzerTestOneInput(const __uint8_t *Data, size_t Size) {

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

        inject_data = data;
        inject_data_size = fuzz_size;

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

/*-----------------------------------------------------------*/