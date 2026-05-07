#include <execinfo.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "crash.h"

#define RED "\033[1;31m"
#define YELLOW "\033[1;33m"
#define YELLOW_NO_BOLD "\033[0;33m"
#define GREEN "\033[1;32m"
#define BLUE "\033[1;34m"
#define MAGENTA "\033[1;35m"
#define CYAN "\033[1;36m"
#define RESET "\033[0m"

#ifndef CRASH_DIR_PATH
#define CRASH_DIR_PATH "crash_dir"
#endif

#ifndef CRASH_FILE_PREFIX
#define CRASH_FILE_PREFIX "crash"
#endif

/***********************************************************************
 * @brief Sanitized Scope Section
 ***********************************************************************/
/**
 * @brief Last accessed taint map and address.
 *
 * The taint map and address are updated on every memory load or store
 * operation.
 */
uint16_t lastAccessTaintMap;
void *lastAccessAddress;

/**
 * @brief Saved last accessed taint map and address.
 *
 * The snapshot of the last accessed taint map and address.
 */
static uint16_t lastAccessTaintMapSave;
static void *lastAccessAddressSave;

/**
 * @brief Callback function for handling AddressSanitizer (ASAN) error reports.
 *
 * It is responsible for recording the last accessed memory address and the
 * corresponding taint map. After the execution of this function, the variables
 * `lastAccessTaintMap` and `lastAccessAddress` will no longer hold meaningful
 * data.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
__asan_on_error_report(const char *report) {
  lastAccessAddressSave = lastAccessAddress;
  lastAccessTaintMapSave = lastAccessTaintMap;

  analyzeCrash(report);
}

/**
 * @brief Flag to enable or disable ASAN malloc hooks.
 *
 * It should be set to true when LLVMFuzzerTestOneInput is called.
 */
static bool hook_asan_malloc;

void hookASANMalloc(void) { hook_asan_malloc = true; }

void unhookASANMalloc(void) { hook_asan_malloc = false; }

void __asan_malloc_hook(void *ptr, size_t size) {
  if (hook_asan_malloc) {
  }
}

void __asan_free_hook(void *ptr) {
  if (hook_asan_malloc) {
  }
}

/**
 * @brief Register the callback function for handling ASAN error reports.
 */
extern void __asan_set_error_report_callback(void (*)(const char *));
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
register_asan_report_cb(void) {
  __asan_set_error_report_callback(__asan_on_error_report);
}

/**
 * @brief Linked list of node structure to manage sanitized scopes.
 *
 * Each node contains a name of function in sanitized scope and a pointer to the
 * next node.
 */
struct sanitizedFunction *sanitizedScope = NULL;

/**
 * @brief Register a function name in sanitized scope.
 *
 * @param name Function name to be registered in sanitized scope.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
registerSanitizedScope(char *name) {
  if (isSanitizedScope(name)) {
    return;
  }
  struct sanitizedFunction *new_sanitized_function =
      (struct sanitizedFunction *)malloc(sizeof(struct sanitizedFunction));
  new_sanitized_function->name = name;
  new_sanitized_function->next = sanitizedScope;
  sanitizedScope = new_sanitized_function;
}

/**
 * @brief Check if a function name is in sanitized scope.
 *
 * @param name Function name to be checked.
 * @return true if the function name is in sanitized scope, false otherwise.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) bool
isSanitizedScope(char *name) {
  struct sanitizedFunction *sanitized_function = sanitizedScope;
  while (sanitized_function != NULL) {
    if (strncmp(sanitized_function->name, name,
                strlen(sanitized_function->name)) == 0) {
      return true;
    }
    sanitized_function = sanitized_function->next;
  }
  return false;
}

/***********************************************************************
 * @brief Crash Analysis Section
 ***********************************************************************/

/**
 * @brief Linked list of node structure to manage unique crash addresses.
 */
struct crash *crash_list = NULL;

/**
 * @brief Current Test Depth in Multi Layer Fuzzing
 */
uint64_t current_test_depth;

/**
 * @brief Register a crash address.
 *
 * @param addr Crash address to be registered.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
registerCrash(void *addr) {
  struct crash *new_crash = (struct crash *)malloc(sizeof(struct crash));
  new_crash->addr = addr;
  new_crash->next = crash_list;
  crash_list = new_crash;
}

/**
 * @brief Check if a crash address is already registered.
 *
 * @param addr Crash address to be checked.
 * @return true if the crash address is already registered, false otherwise.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) bool
isDuplicateCrash(void *addr) {
  struct crash *crash = crash_list;
  while (crash != NULL) {
    /* +- 0x10 is high likely the same bug */
    // if (crash->addr >= addr - 0x10 && crash->addr <= addr + 0x10) {
    if (crash->addr == addr) {
      return true;
    }
    crash = crash->next;
  }
  return false;
}

/**
 * @brief Get the crash address from the ASAN error report.
 *
 * @param report ASAN error report.
 * @param index Index of the crash address in the report.
 * @return Crash address.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void *
getCrashAddress(const char *report, int index) {
  char addr[20];
  char index_str[6];

  memset(index_str, 0, 6);
  snprintf(index_str, 6, "#%d ", index);
  char *pc = strstr(report, index_str);
  if (pc == NULL) {
    return NULL;
  }
  pc += strlen(index_str);

  memset(addr, 0, 20);
  memcpy(addr, pc, 20);

  char *end = strchr(addr, ' ');
  if (end != NULL) {
    end[0] = '\0';
  } else {
    return NULL;
  }

  return (void *)strtoul(addr, NULL, 16);
}

/**
 * @brief Get the first function name from the ASAN error report.
 *
 * @param report ASAN error report.
 * @param function_name Buffer to store the function name.
 * @param len Length of the buffer.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
getCrashFunctionName(const char *report, char *function_name, int len) {
  char index_str[6];

  for (int i = 0; i < 10; i++) {
    memset(index_str, 0, 6);
    snprintf(index_str, 6, "#%d ", i);
    char *pc = strstr(report, index_str);
    if (pc != NULL) {
      pc += strlen(index_str);
      if (strstr(pc, "in ") != NULL) {
        pc = strstr(pc, "in ") + 3;
        char *bracket_end = strchr(pc, '(');
        char *space_end = strchr(pc, ' ');
        char *end = bracket_end < space_end ? bracket_end : space_end;

        /* TODO */
        // if ((memcmp(pc, "__interceptor_sigemptyset", 26) == 0) ||
        //     (memcmp(pc, "sigemptyset", 11) == 0)) {
        //   /* Restart */
        //   exit(0);
        // }

        if (memcmp(pc, "sanitize", 8) == 0) {
          /* Skip the sanitized function */
          continue;
        }

        if (memcmp(pc, "replicate_", 10) == 0) {
          /* The function is replicated function */
          if (end != NULL) {
            int function_name_len = end - pc;
            if (function_name_len > len) {
              function_name_len = len;
            }
            memcpy(function_name, pc, function_name_len);
            return;
          }
        } else {
          char *line_end = strchr(pc, '\n');
          char *header = strstr(pc, ".h");
          // if (header && header < line_end) {
          //   /* Skip the functions in header file */
          //   /* TODO: We should not skip all the banned function, for now, we just check it with whitelist */
          //   continue;
          // }
          if (end && end < line_end) {
            int function_name_len = end - pc;
            if (function_name_len > len) {
              function_name_len = len;
            }
            memcpy(function_name, pc, function_name_len);
            return;
          }
        }
      }
    }
  }
}

/**
 * @brief Check if the crash function is in sanitized scope.
 *
 * @param report ASAN error report.
 * @return true if the crash function is in sanitized scope, false otherwise.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) bool
isInSanitizedScope(const char *report) {
  char crash_function_name[100];
  memset(crash_function_name, 0, 100);
  getCrashFunctionName(report, crash_function_name, 100);

  return isSanitizedScope(crash_function_name);
}

/**
 * @brief Check if the crash point is in fuzz scope.
 *
 * @return true if the crash point is in fuzz scope, false otherwise.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) bool
isInFuzzScope(const char *report) {
  void *array[20];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace(array, 20);
  strings = backtrace_symbols(array, size);

  for (i = 0; i < size; i++) {
    if (strstr(strings[i], "fuzzEntryFunction") != NULL) {
      free(strings);
      return true;
    }
  }

  free(strings);

  /* Sometimes backtrace does not work */
  if (strstr(report, "fuzzEntryFunction") != NULL) {
    return true;
  }

  /* IndirectLink lose the trace */
  if (strstr(report, "callIndirectLink") != NULL) {
    return true;
  }

  return false;
}

/***********************************************************************
 * @brief Crash Confidentiality Section
 ***********************************************************************/

#define MAX_STACK_SIZE 500
#define MAX_SANITIZE_CALL_PARAM 16

static char *untrackableFunctions[] = {"memcmp", "memcpy", "sigemptyset",
                                       "strtol", "strtok", "memset",
                                       "strcmp", "strlen", "strdup",
                                       "strncmp", "strcpy", "strncpy",
                                       "vsnprintf"};

/**
 * @brief Stack structure for managing taint scores of arguments in sanitized
 * calls.
 *
 * This stack structure is used to record the taint scores of arguments for each
 * sanitized call. Whenever a sanitized function is called, the taint scores of
 * its arguments are pushed onto this stack.
 */
struct stack {
  int top;
  int map[MAX_STACK_SIZE][MAX_SANITIZE_CALL_PARAM];
} sanitizeCallTaintScoreStack;

/**
 * @brief Get the current taint score map of the sanitized call.
 *
 * @return Pointer to the taint score map of the sanitized call.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int *
getSanitizeCallTaintScoreMap(void) {
  return sanitizeCallTaintScoreStack.map[sanitizeCallTaintScoreStack.top];
}

/**
 * @brief Get the taint score map of the previous sanitized call.
 *
 * @return Pointer to the taint score map of the previous sanitized call.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int *
getPrevSanitizeCallTaintScoreMap(void) {
  if (sanitizeCallTaintScoreStack.top == 0) {
    return NULL;
  }
  return sanitizeCallTaintScoreStack.map[sanitizeCallTaintScoreStack.top - 1];
}

/**
 * @brief Record the taint score of an argument in the taint score map in the
 * current sanitized call.
 *
 * @param index Index of the argument.
 * @param score Taint score of the argument.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
recordSanitizeCallTaintScore(uint32_t index, uint32_t score) {
  int *sanitizeCallTaintScoreMap = getSanitizeCallTaintScoreMap();
  sanitizeCallTaintScoreMap[index] = score;
}

/**
 * @brief Reset the taint score stack.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
resetSanitizeCallTaintScoreStack(void) {
  sanitizeCallTaintScoreStack.top = 0;
  memset(sanitizeCallTaintScoreStack.map, 0,
         MAX_STACK_SIZE * MAX_SANITIZE_CALL_PARAM * sizeof(int));
}

/**
 * @brief Push a new taint sanitize call.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
pushSanitizeCallTaintScoreStack(void) {
  if (++sanitizeCallTaintScoreStack.top >= MAX_STACK_SIZE) {
    /* Maybe because of recursive functions */
    // printf(YELLOW_NO_BOLD "Error: Max stack size reached" RESET "\n");
    raise(SIGUSR1);
  }
}

/**
 * @brief Pop the current taint sanitize call.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
popSanitizeCallTaintScoreStack(void) {
  if (--sanitizeCallTaintScoreStack.top < 0) {
    printf(RED "Error: Stack underflow" RESET "\n");
    exit(0);
  }
}

/**
 * @brief Calculate the crash confidentiality score.
 *
 * The crash confidentiality score is calculated based on the taint scores of
 * each argument of the sanitized call and the taint map of the last accessed
 * memory address.
 *
 * @return Crash confidentiality score.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
calculateCrashConfidentiality(void) {
  int score = 0;
  int *sanitizeCallTaintScoreMap = getPrevSanitizeCallTaintScoreMap();
  if (sanitizeCallTaintScoreMap == NULL) {
    /* Crash happens outside of sanitized scope, but no sanitized call is
     * invoked */
    printf(YELLOW_NO_BOLD "Error: No previous sanitize call" RESET "\n");
    return -1;
  }

  for (int i = 0; i < MAX_SANITIZE_CALL_PARAM; i++) {
    if (lastAccessTaintMapSave & (1 << i)) {
      /* ith Argument taints the address with access violation */
      if (score < sanitizeCallTaintScoreMap[i])
        score = sanitizeCallTaintScoreMap[i];
    }
  }
  return score;
}

/**
 * @brief Check if the crash point is in an untrackable function.
 *
 * @return true if the crash point is in an untrackable function, false
 * otherwise.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) bool
isInUntrackableFunction(const char *report) {
  void *array[20];
  size_t size;
  char **strings;
  size_t i;

  size = backtrace(array, 20);
  strings = backtrace_symbols(array, size);

  for (i = 0; i < size; i++) {
    for (unsigned int j = 0; j < sizeof(untrackableFunctions) / sizeof(char *);
         j++) {
      if (strstr(strings[i], untrackableFunctions[j]) != NULL) {
        free(strings);
        return true;
      }
    }
  }

  free(strings);

  /* Sometimes backtrace does not work */
  for (unsigned int j = 0; j < sizeof(untrackableFunctions) / sizeof(char *);
       j++) {
    if (strstr(report, untrackableFunctions[j]) != NULL) {
      return true;
    }
  }

  return false;
}

/*-----------------------------------------------------------*/
extern uint8_t *fuzz_data_orig;
extern size_t fuzz_size_orig;

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
dumpLowConfidentialityCrash(void *crash_addr, char *func_name,
                            const char *report) {
  char crash_file_full_path[200];
  char crash_report_path[250];
  char crash_seed_path[250];
  char crash_user_seed_path[250];
  char crash_state_seed_path[250];

  FILE *crash_file;

  snprintf(crash_file_full_path, 200, "%s/low", CRASH_DIR_PATH);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating low conf crash directory" RESET "\n");
      exit(0);
    }
  }

  snprintf(crash_file_full_path, 200, "%s/low/%s", CRASH_DIR_PATH, func_name);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating low conf crash directory" RESET "\n");
      exit(0);
    }
  }

  snprintf(crash_file_full_path, 200, "%s/low/%s/%p", CRASH_DIR_PATH, func_name,
           crash_addr);

  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating low conf crash directory" RESET "\n");
      exit(0);
    }
  }

  /* Save the report */
  snprintf(crash_report_path, 250, "%s/low/%s/%p/report.txt", CRASH_DIR_PATH,
           func_name, crash_addr);
  crash_file = fopen(crash_report_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fprintf(crash_file, "%s", report);
  fclose(crash_file);

  /* Save the seed */
  snprintf(crash_seed_path, 250, "%s/low/%s/%p/seed", CRASH_DIR_PATH, func_name,
           crash_addr);
  crash_file = fopen(crash_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig, 1, fuzz_size_orig, crash_file);
  fclose(crash_file);

  /* Save the state seed */
  snprintf(crash_state_seed_path, 250, "%s/low/%s/%p/state_seed",
           CRASH_DIR_PATH, func_name, crash_addr);
  crash_file = fopen(crash_state_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig, 1, 128, crash_file);
  fclose(crash_file);

  /* Save the user seed */
  snprintf(crash_user_seed_path, 250, "%s/low/%s/%p/user_seed", CRASH_DIR_PATH,
           func_name, crash_addr);
  crash_file = fopen(crash_user_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig + 128, 1, fuzz_size_orig - 128, crash_file);
  fclose(crash_file);

  /* Register crash to prevent duplication */
  registerCrash(crash_addr);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
dumpHighConfidentialityCrash(void *crash_addr, char *func_name,
                             const char *report) {
  char crash_file_full_path[200];
  char crash_report_path[250];
  char crash_seed_path[250];
  char crash_user_seed_path[250];
  char crash_state_seed_path[250];

  FILE *crash_file;

  snprintf(crash_file_full_path, 200, "%s/high", CRASH_DIR_PATH);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating high conf crash directory" RESET "\n");
      exit(0);
    }
  }

  snprintf(crash_file_full_path, 200, "%s/high/%s", CRASH_DIR_PATH, func_name);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating high conf crash directory" RESET "\n");
      exit(0);
    }
  }

  snprintf(crash_file_full_path, 200, "%s/high/%s/%p", CRASH_DIR_PATH,
           func_name, crash_addr);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating high conf crash directory" RESET "\n");
      exit(0);
    }
  }

  /* Save the report */
  snprintf(crash_report_path, 250, "%s/high/%s/%p/report.txt", CRASH_DIR_PATH,
           func_name, crash_addr);
  crash_file = fopen(crash_report_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fprintf(crash_file, "depth: %d\n", current_test_depth);
  fprintf(crash_file, "%s", report);
  fclose(crash_file);

  /* Save the seed */
  snprintf(crash_seed_path, 250, "%s/high/%s/%p/seed", CRASH_DIR_PATH,
           func_name, crash_addr);
  crash_file = fopen(crash_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig, 1, fuzz_size_orig, crash_file);
  fclose(crash_file);

  /* Save the state seed */
  snprintf(crash_state_seed_path, 250, "%s/high/%s/%p/state_seed",
           CRASH_DIR_PATH, func_name, crash_addr);
  crash_file = fopen(crash_state_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig, 1, 128, crash_file);
  fclose(crash_file);

  /* Save the user seed */
  snprintf(crash_user_seed_path, 250, "%s/high/%s/%p/user_seed", CRASH_DIR_PATH,
           func_name, crash_addr);
  crash_file = fopen(crash_user_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig + 128, 1, fuzz_size_orig - 128, crash_file);
  fclose(crash_file);

  /* Register crash to prevent duplication */
  registerCrash(crash_addr);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
dumpMediumConfidentialityCrash(void *crash_addr, char *func_name, int score,
                               const char *report) {
  char crash_file_full_path[200];
  char crash_report_path[250];
  char crash_seed_path[250];
  char crash_user_seed_path[250];
  char crash_state_seed_path[250];

  FILE *crash_file;

  snprintf(crash_file_full_path, 200, "%s/medium", CRASH_DIR_PATH);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating medium conf crash directory" RESET "\n");
      exit(0);
    }
  }

  snprintf(crash_file_full_path, 200, "%s/medium/%s", CRASH_DIR_PATH,
           func_name);
  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating medium conf crash directory" RESET "\n");
      exit(0);
    }
  }

  snprintf(crash_file_full_path, 200, "%s/medium/%s/%p_%d", CRASH_DIR_PATH,
           func_name, crash_addr, score);

  if (access(crash_file_full_path, F_OK) == -1) {
    if (mkdir(crash_file_full_path, 0775) == -1) {
      printf(RED "Error creating medium conf crash directory" RESET "\n");
      exit(0);
    }
  }

  /* Save the report */
  snprintf(crash_report_path, 250, "%s/medium/%s/%p_%d/report.txt",
           CRASH_DIR_PATH, func_name, crash_addr, score);
  crash_file = fopen(crash_report_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fprintf(crash_file, "%s", report);
  fclose(crash_file);

  /* Save the seed */
  snprintf(crash_seed_path, 250, "%s/medium/%s/%p_%d/seed", CRASH_DIR_PATH,
           func_name, crash_addr, score);
  crash_file = fopen(crash_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig, 1, fuzz_size_orig, crash_file);
  fclose(crash_file);

  /* Save the state seed */
  snprintf(crash_state_seed_path, 250, "%s/medium/%s/%p_%d/state_seed",
           CRASH_DIR_PATH, func_name, crash_addr, score);
  crash_file = fopen(crash_state_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig, 1, 128, crash_file);
  fclose(crash_file);

  /* Save the user seed */
  snprintf(crash_user_seed_path, 250, "%s/medium/%s/%p_%d/user_seed",
           CRASH_DIR_PATH, func_name, crash_addr, score);
  crash_file = fopen(crash_user_seed_path, "w");
  if (crash_file == NULL) {
    printf(RED "Error creating crash file" RESET "\n");
    exit(0);
  }
  fwrite(fuzz_data_orig + 128, 1, fuzz_size_orig - 128, crash_file);
  fclose(crash_file);

  /* Register crash to prevent duplication */
  registerCrash(crash_addr);
}

/**
 * @brief Dump the crash report to a file.
 *
 * @param crash_addr Crash address.
 * @param func_name Function name of the crash.
 * @param confidentiality Confidentiality of the crash.
 * @param report ASAN error report.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
dumpCrash(void *crash_addr, char *func_name, int confidentiality,
          const char *report) {
  if (confidentiality == 100) {
    dumpHighConfidentialityCrash(crash_addr, func_name, report);
  } else if (confidentiality == 0) {
    dumpLowConfidentialityCrash(crash_addr, func_name, report);
  } else {
    dumpMediumConfidentialityCrash(crash_addr, func_name, confidentiality,
                                   report);
  }
}

/*-----------------------------------------------------------*/

__attribute__((no_sanitize("address"))) static void
handleHighConfidentialityCrash(const char *report) {
  void *crash_address;

  uint64_t hash = 0;
  int index = 0;
  while (1) {
    // We only care about the first 4 crash addresses
    if (index > 4) break;

    crash_address = getCrashAddress(report, index++);
    if (crash_address == NULL) {
      break;
    }
    hash = (hash << 5) ^ (uint64_t)crash_address;
  }

  /* Different hash on different layer */
  hash = (hash << 5) ^ current_test_depth;

  if (!isInFuzzScope(report)) {
    return;
  }

  if (isDuplicateCrash((void *)hash)) {
    return;
  }

  char function_name[100];
  memset(function_name, 0, 100);
  getCrashFunctionName(report, function_name, 100);

  /* Calculate confidentiality from taint score and taint map */
  if (isInUntrackableFunction(report)) {
    return;
  }

  printf(RED "[!] Unique high confidentiality crash detected @ 0x%llx [%s]" RESET
             "\n",
         hash, function_name);
  dumpCrash((void *)hash, function_name, 100, report);
}

__attribute__((no_sanitize("address"))) static void
handleLowConfidentialityCrash(const char *report) {
  void *crash_address;

  crash_address = getCrashAddress(report, 0);
  if (!isInFuzzScope(report)) {
    return;
  }

  if (isDuplicateCrash(crash_address)) {
    return;
  }

  char function_name[100];
  memset(function_name, 0, 100);
  getCrashFunctionName(report, function_name, 100);

  /* Calculate confidentiality from taint score and taint map */
  if (isInUntrackableFunction(report)) {
    printf(YELLOW "[*] Unique low confidentiality crash detected @ %p in "
                  "untrackable function [%s]." RESET "\n",
           crash_address, function_name);
    dumpCrash(crash_address, function_name, 0, report);
    return;
  }

  int taint_score = calculateCrashConfidentiality();
  if (taint_score < 0) {
    return;
  }

  if (taint_score == 100) {
    printf(YELLOW
           "[*] Unique low confidentiality crash detected @ %p [%s]" RESET "\n",
           crash_address, function_name);
  } else {
    printf(MAGENTA
           "Unique medium confidentiality crash detected @ %p [%s]" RESET "\n",
           crash_address, function_name);
  }
  dumpCrash(crash_address, function_name, 100 - taint_score, report);
}

/*-----------------------------------------------------------*/

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
analyzeCrash(const char *report) {
  /* Create crash directory */
  if (access(CRASH_DIR_PATH, F_OK) == -1) {
    if (mkdir(CRASH_DIR_PATH, 0775) == -1) {
      printf(RED "Error creating crash directory" RESET "\n");
      exit(0);
    }
  }

  if (isInSanitizedScope(report)) {
    handleHighConfidentialityCrash(report);
  } else {
    // handleLowConfidentialityCrash(report);
  }

  return 0;
}