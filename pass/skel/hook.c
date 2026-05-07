#include <assert.h>
#include <setjmp.h>
#include <signal.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <stdio.h>

#include "crash.h"
#include "hashmap.h"
#include "hook.h"

extern int __asan_address_is_poisoned(void const volatile *addr);

static void clearPlayGround(void);

/******************************************************************************
 * Signal Handling
 ******************************************************************************/
#define MAX_SIGJMP_DEPTH 100
static sigjmp_buf jump_buffer[MAX_SIGJMP_DEPTH];
static struct sigaction sa_old[MAX_SIGJMP_DEPTH];
static int sigjmp_depth = 0;

/* Signal handler for TRY/CATCH */
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
signal_handler(int signum, siginfo_t *info, void *context) {
  siglongjmp(jump_buffer[sigjmp_depth - 1], 1);
}

/******************************************************************************
 * States
 ******************************************************************************/
static uint32_t hookCounter;
static uint64_t linkcounterSingleRun;

#define MAX_LINKS_PER_RUN 5

uint64_t cmpIndexes[MAX_CMPS_PER_EXECUTION];
uint8_t cmpStates[MAX_CMPS_PER_EXECUTION];

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
resetHookCounter(void) {
  hookCounter = 0;
  sigjmp_depth = 0;
  linkcounterSingleRun = 0;

  memset((void *)cmpIndexes, 0, sizeof(uint64_t) * MAX_CMPS_PER_EXECUTION);
  memset((void *)cmpStates, 0, sizeof(uint8_t) * MAX_CMPS_PER_EXECUTION);
  clearPlayGround();
}

extern uint8_t state_buf[MAX_CMPS_PER_EXECUTION / 4];

__attribute__((no_sanitize("coverage"))) uint8_t getCmpStates(uint64_t val) {
  for (int i = 0; i < MAX_CMPS_PER_EXECUTION; i++) {
    // naive hash value
    // __builtin_return_address(0) is inside the sanitizeCmp function
    uint64_t hashval = (uint64_t)val ^ (uint64_t)__builtin_return_address(1);

    if (cmpIndexes[i] == hashval) {
      return cmpStates[i];
    }

    if (cmpIndexes[i] == 0) {
      cmpIndexes[i] = hashval;

      /* Extract 2 bits */
      uint8_t b = state_buf[i / 4];
      cmpStates[i] = (b >> 2 * (i % 4)) & 0x3;
      return cmpStates[i];
    }
  }

  printf("Reaches max cmp counter\n");
  return 0;
}

__attribute__((no_sanitize("coverage"))) uint16_t
getSwitchStates(uint64_t val) {
  for (int i = 0; i < MAX_CMPS_PER_EXECUTION; i++) {
    // naive hash value
    // __builtin_return_address(0) is inside the sanitizeCmp function
    uint64_t hashval = (uint64_t)val ^ (uint64_t)__builtin_return_address(1);

    if (cmpIndexes[i] == hashval) {
      return cmpStates[i];
    }

    if (cmpIndexes[i] == 0) {
      // Use 4 bits
      cmpIndexes[i] = hashval;
      cmpIndexes[i + 1] = hashval;

      /* Extract 4 bits */
      uint8_t b1 = state_buf[i / 4];
      uint8_t b2 = state_buf[(i + 1) / 4];

      cmpStates[i] = (b1 >> 2 * (i % 4)) & 0x3;
      cmpStates[i + 1] = (b2 >> 2 * ((i + 1) % 4)) & 0x3;

      return cmpStates[i] | (cmpStates[i + 1] << 2);
    }
  }

  printf("Reaches max cmp counter\n");
  return 0;
}

/******************************************************************************
 * Playground Functions
 ******************************************************************************/

struct playGround {
  uint64_t hashval;
  void *out;
};

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static int
compare(const void *a, const void *b, void *udata) {
  UNUSED_ARG(udata);
  return a == b;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static uint64_t
hash(const void *item, uint64_t seed0, uint64_t seed1) {
  const struct playGround *pg = item;
  return hashmap_sip(&pg->hashval, sizeof(uint64_t), seed0, seed1);
}

static struct hashmap *playGroundMap;

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
clearPlayGround(void) {
  struct playGround *pg;
  size_t iter = 0;
  void *item;

  if (!playGroundMap) {
    return;
  }

  while (hashmap_iter(playGroundMap, &iter, &item)) {
    pg = (struct playGround *)item;
    if (pg->out)
      free(pg->out);
  }

  hashmap_free(playGroundMap);
  playGroundMap = NULL;
}

__attribute__((no_sanitize("coverage"),
               no_sanitize("address"))) static struct playGround *
getPlayGround(void *in) {
  if (!playGroundMap) {
    playGroundMap = hashmap_new(sizeof(struct playGround), 0, 0, 0, hash,
                                compare, NULL, NULL);
  }

  // naive hash value
  // __builtin_return_address(0) is inside the sanitizer function
  uint64_t hashval = (uint64_t)in ^ (uint64_t)__builtin_return_address(1);

  struct playGround *pg;
  if ((pg = (struct playGround *)hashmap_get(
           playGroundMap, &(struct playGround){.hashval = hashval}))) {
    return pg;
  }

  hashmap_set(
      playGroundMap,
      &(struct playGround){.hashval = hashval, .out = calloc(PLAYGROUND_SIZE, 1)});
  pg = (struct playGround *)hashmap_get(
      playGroundMap, &(struct playGround){.hashval = hashval});

  return pg;
}

/******************************************************************************
 * Sanitize Load/Store Functions
 ******************************************************************************/
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
sanitizeLoad(void *addr) {
  TRY {
    /* TODO: Just return this time */
    RESET_MAX_HOOKS;

    /* Incosistency between ASAN report */
    volatile uint64_t tmp = *(uint64_t *)addr;
    if (__asan_address_is_poisoned(addr))
      raise(SIGSEGV);
    if (__asan_address_is_poisoned(addr + 7))
      raise(SIGSEGV);

    TRYRET addr;
  }
  CATCH {
    void *a = getPlayGround(addr)->out;
    CATCHRET a;
  }
  END_TRY;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
sanitizeStore(void *addr) {
  TRY {
    /* TODO: Just return this time */
    RESET_MAX_HOOKS;

    /* Incosistency between ASAN report */
    volatile uint64_t tmp = *(uint64_t *)addr;
    if (__asan_address_is_poisoned(addr))
      raise(SIGSEGV);
    if (__asan_address_is_poisoned(addr + 7))
      raise(SIGSEGV);

    TRYRET addr;
  }
  CATCH {
    void *a = getPlayGround(addr)->out;

    CATCHRET a;
  }
  END_TRY;
}

extern uint16_t lastAccessTaintMap;
extern void *lastAccessAddress;

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
DFSANLoadHook(uint16_t taintMap, void *addr) {
  lastAccessTaintMap = taintMap;
  lastAccessAddress = addr;
  return addr;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
DFSANStoreHook(uint16_t taintMap, void *addr) {
  lastAccessTaintMap = taintMap;
  lastAccessAddress = addr;
  return addr;
}

/******************************************************************************
 * Sanitize Call Functions
 ******************************************************************************/
 uint64_t (*sanitizeCall3Orig)(void *, void *, void *, void *, void *, void *,
                               void *, void *, void *, void *, void *, void *, 
                               void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) uint64_t
sanitizeCall3(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
              void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
              void *a13, void *a14, void *a15, void *a16) {
  
  TRY { sanitizeCall3Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH {}
  END_TRY;
  
}

void (*sanitizeCall7Orig)(void *, void *, void *, void *, void *, void *,
                          void *, void *, void *, void *, void *, void *, 
                          void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) void
sanitizeCall7(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
              void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
              void *a13, void *a14, void *a15, void *a16) {
  
  TRY { sanitizeCall7Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH {}
  END_TRY;
  
}

int (*sanitizeCall13_1Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) int
sanitizeCall13_1(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                 void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                 void *a13, void *a14, void *a15, void *a16) {
  
  TRY { TRYRET sanitizeCall13_1Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH { CATCHRET 0; }
  END_TRY;
  
}

int (*sanitizeCall13_8Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) int
sanitizeCall13_8(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                 void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                 void *a13, void *a14, void *a15, void *a16) {
  
  TRY { TRYRET sanitizeCall13_8Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH { CATCHRET 0; }
  END_TRY;
  
}

int (*sanitizeCall13_16Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) int
sanitizeCall13_16(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                  void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                  void *a13, void *a14, void *a15, void *a16) {
  
  TRY { TRYRET sanitizeCall13_16Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH { CATCHRET 0; }
  END_TRY;
  
}

int (*sanitizeCall13_32Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) int
sanitizeCall13_32(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                  void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                  void *a13, void *a14, void *a15, void *a16) {
  
  TRY { TRYRET sanitizeCall13_32Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH { CATCHRET 0; }
  END_TRY;
  
}

int (*sanitizeCall13_64Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"))) int
sanitizeCall13_64(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                  void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                  void *a13, void *a14, void *a15, void *a16) {
  
  TRY { TRYRET sanitizeCall13_64Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16); }
  CATCH { CATCHRET 0; }
  END_TRY;
  
}

void *(*sanitizeCall15Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
sanitizeCall15(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
               void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
               void *a13, void *a14, void *a15, void *a16) {
  
  TRY {
    void *result = sanitizeCall15Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);

    /* Heuristic */
    int tmp = *(int *)result;
    UNUSED_ARG(tmp);

    TRYRET result;
  }
  CATCH { CATCHRET NULL; }
  END_TRY;
  
}

void *(*sanitizeCall16Orig)(void *, void *, void *, void *, void *, void *,
                            void *, void *, void *, void *, void *, void *, 
                            void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
sanitizeCall16(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
               void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
               void *a13, void *a14, void *a15, void *a16) {
  
  TRY {
    void *result = sanitizeCall16Orig(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);

    /* Heuristic */
    int tmp = *(int *)result;
    UNUSED_ARG(tmp);

    TRYRET result;
  }
  CATCH { CATCHRET NULL; }
  END_TRY;
  
}

/**
 * @brief Check the indirect call is invoked from the link function.
 * 
 */
bool checkIndirectLink;

/**
 * @brief A pointer to list of indirect call functions.
 * 
 */
int (*IndirectLinks[256])(void *, void *, void *, void *, void *, void *,
                          void *, void *, void *, void *, void *, void *, 
                          void *, void *, void *, void *);

/**
 * @brief Input indices for the indirect call.
 */
int IndirectInputIndices[256];

/**
 * @brief Length indices for the indirect call.
 */
int IndirectLengthIndices[256];

/**
 * @brief A jump trampline for the indirect call.
 */
static uint64_t linkcounter;

static int indirectParamIndex;
static int indirectParamLengthIndex;
/**
 * @brief Check the validity of the parameter.
 * 
 * @param param The parameter to be checked.
 * @return true if the parameter is valid, false otherwise.
 * 
 * @note This function can be overriden by the user.
 */
__attribute__((no_sanitize("coverage"), no_sanitize("address"), weak)) bool
checkParamValid(void **param, int lengthIndex) {
  return true;
}

#define CHECK_PARAM(x)                                                         \
  if (!checkParamValid(&a##x, indirectParamLengthIndex))  {                    \
    return 0;                                                                  \
  }

#define CHECK_PARAM_1                                                          \
  if (indirectParamIndex == 0) {                                               \
    CHECK_PARAM(1);                                                            \
  }

#define CHECK_PARAM_2                                                          \
  if (indirectParamIndex == 1) {                                               \
    CHECK_PARAM(2);                                                            \
  }

#define CHECK_PARAM_3                                                          \
  if (indirectParamIndex == 2) {                                               \
    CHECK_PARAM(3);                                                            \
  }

#define CHECK_PARAM_4                                                          \
  if (indirectParamIndex == 3) {                                               \
    CHECK_PARAM(4);                                                            \
  }

#define CHECK_PARAM_5                                                          \
  if (indirectParamIndex == 4) {                                               \
    CHECK_PARAM(5);                                                            \
  }

#define CHECK_PARAM_6                                                          \
  if (indirectParamIndex == 5) {                                               \
    CHECK_PARAM(6);                                                            \
  }

#define CHECK_PARAM_7                                                          \
  if (indirectParamIndex == 6) {                                               \
    CHECK_PARAM(7);                                                            \
  }

#define CHECK_PARAM_8                                                          \
  if (indirectParamIndex == 7) {                                               \
    CHECK_PARAM(8);                                                            \
  }

#define CHECK_PARAMS                                                           \
  CHECK_PARAM_1;                                                               \
  CHECK_PARAM_2;                                                               \
  CHECK_PARAM_3;                                                               \
  CHECK_PARAM_4;                                                               \
  CHECK_PARAM_5;                                                               \
  CHECK_PARAM_6;                                                               \
  CHECK_PARAM_7;                                                               \
  CHECK_PARAM_8;

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static int
callIndirectLink(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                 void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                 void *a13, void *a14, void *a15, void *a16) {
#ifndef LOW_FUZZ

  uint64_t indirectLinkNum = 0;
  while (IndirectLinks[indirectLinkNum]){
    indirectLinkNum++;
  }

  if (indirectLinkNum == 0) {
    return 0;
  }

  uint64_t callIndex = linkcounter % indirectLinkNum;
  linkcounter++;

  indirectParamIndex = IndirectInputIndices[callIndex];
  indirectParamLengthIndex = IndirectLengthIndices[callIndex];

  // Check the validity of the parameters
  CHECK_PARAMS;

  // Check the maximum number of links per run
  if (linkcounterSingleRun++ > MAX_LINKS_PER_RUN) {
    return 0;
  }

  IndirectLinks[callIndex](a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
  return 0;

#else
  return 0;
#endif
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static bool
isIndirectLink(void *func) {
  for (int i = 0; i < 256; i++) {
    if (IndirectLinks[i] == func) {
      return true;
    }
  }
  return false;
}

/**
 * @brief Invalid indirect call threshold.
 * 
 */
#define ROUTE_INDIRECT_THRESHOLD 5

struct indirectFailCounter {
  uint64_t loc;
  uint64_t counter;
  struct indirectFailCounter *next;
};

struct indirectFailCounter *indirectFailCounterHead;

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static uint64_t
getIndirectFailCounter(void) {
  uint64_t current_loc = (uint64_t)__builtin_return_address(1);
  struct indirectFailCounter *current = indirectFailCounterHead;
  while (current) {
    if (current->loc == current_loc) {
      return current->counter;
    }
    current = current->next;
  }

  struct indirectFailCounter *new = (struct indirectFailCounter *)malloc(
      sizeof(struct indirectFailCounter));
  new->loc = current_loc;
  new->counter = 0;
  new->next = indirectFailCounterHead;
  indirectFailCounterHead = new;
  return 0;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
increaseIndirectFailCounter(void) {
  uint64_t current_loc = (uint64_t)__builtin_return_address(1);
  struct indirectFailCounter *current = indirectFailCounterHead;
  while (current) {
    if (current->loc == current_loc) {
      current->counter++;
    }
    current = current->next;
  }
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static void
resetIndirectFailCounter(void) {
  uint64_t current_loc = (uint64_t)__builtin_return_address(1);
  struct indirectFailCounter *current = indirectFailCounterHead;
  while (current) {
    if (current->loc == current_loc) {
      current->counter = 0;
    }
    current = current->next;
  }
}

volatile uint64_t unusetmp;

void (*sanitizeIndirectCall7Orig)(void *, void *, void *, void *, void *, void *,
                                  void *, void *, void *, void *, void *, void *, 
                                  void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
sanitizeIndirectCall7(void *a1, void *a2, void *a3, void *a4, void *a5,
                      void *a6, void *a7, void *a8, void *a9, void *a10,
                      void *a11, void *a12, void *a13, void *a14,
                      void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall7Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // Do we need to call uninstrumented function?
    // sanitizeIndirectCall7Orig(a1, a2, a3, a4, a5, a6, a7, a8);
    // TRYRET;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET;
  }
  END_TRY;
  
}

int (*sanitizeIndirectCallOrig)(void *, void *, void *, void *, void *, void *,
                                void *, void *, void *, void *, void *, void *, 
                                void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
sanitizeIndirectCall(void *a1, void *a2, void *a3, void *a4, void *a5, void *a6,
                     void *a7, void *a8, void *a9, void *a10, void *a11, void *a12,
                     void *a13, void *a14, void *a15, void *a16) {
  
  TRY { TRYRET 0; }
  CATCH { CATCHRET 0; }
  END_TRY;
  
}

int (*sanitizeIndirectCall13_1Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
sanitizeIndirectCall13_1(void *a1, void *a2, void *a3, void *a4, void *a5,
                         void *a6, void *a7, void *a8, void *a9, void *a10,
                         void *a11, void *a12, void *a13, void *a14,
                         void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall13_1Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // int result = sanitizeIndirectCall13_1Orig(a1, a2, a3, a4, a5, a6, a7, a8);
    // TRYRET result;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET 0;
  }
  END_TRY;
  
}

int (*sanitizeIndirectCall13_8Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
sanitizeIndirectCall13_8(void *a1, void *a2, void *a3, void *a4, void *a5,
                         void *a6, void *a7, void *a8, void *a9, void *a10,
                         void *a11, void *a12, void *a13, void *a14,
                         void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall13_8Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // int result = sanitizeIndirectCall13_8Orig(a1, a2, a3, a4, a5, a6, a7, a8);
    // TRYRET result;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET 0;
  }
  END_TRY;
  
}

int (*sanitizeIndirectCall13_16Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
sanitizeIndirectCall13_16(void *a1, void *a2, void *a3, void *a4, void *a5,
                          void *a6, void *a7, void *a8, void *a9, void *a10,
                          void *a11, void *a12, void *a13, void *a14,
                          void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall13_16Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // int result = sanitizeIndirectCall13_16Orig(a1, a2, a3, a4, a5, a6, a7, a8);
    // TRYRET result;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET 0;
  }
  END_TRY;
  
}

int (*sanitizeIndirectCall13_32Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
sanitizeIndirectCall13_32(void *a1, void *a2, void *a3, void *a4, void *a5,
                          void *a6, void *a7, void *a8, void *a9, void *a10,
                          void *a11, void *a12, void *a13, void *a14,
                          void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall13_32Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // int result = sanitizeIndirectCall13_32Orig(a1, a2, a3, a4, a5, a6, a7, a8);
    // TRYRET result;
  }
  CATCH {
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET 0;
  }
  END_TRY;
  
}

int (*sanitizeIndirectCall13_64Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
sanitizeIndirectCall13_64(void *a1, void *a2, void *a3, void *a4, void *a5,
                          void *a6, void *a7, void *a8, void *a9, void *a10,
                          void *a11, void *a12, void *a13, void *a14,
                          void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall13_64Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // int result = sanitizeIndirectCall13_64Orig(a1, a2, a3, a4, a5, a6, a7, a8);
    // TRYRET result;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET 0;
  }
  END_TRY;
  
}

void *(*sanitizeIndirectCall15Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
sanitizeIndirectCall15(void *a1, void *a2, void *a3, void *a4, void *a5,
                       void *a6, void *a7, void *a8, void *a9, void *a10,
                       void *a11, void *a12, void *a13, void *a14,
                       void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall15Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;
    // void *result = sanitizeIndirectCall15Orig(a1, a2, a3, a4, a5, a6, a7, a8);

    // /* Heuristic */
    // tmp = *(int *)result;
    // UNUSED_ARG(tmp);

    // TRYRET result;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET NULL;
  }
  END_TRY;
  
}

void *(*sanitizeIndirectCall16Orig)(void *, void *, void *, void *, void *, void *,
                                    void *, void *, void *, void *, void *, void *, 
                                    void *, void *, void *, void *);
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
sanitizeIndirectCall16(void *a1, void *a2, void *a3, void *a4, void *a5,
                       void *a6, void *a7, void *a8, void *a9, void *a10,
                       void *a11, void *a12, void *a13, void *a14,
                       void *a15, void *a16) {
  
  TRY {
    /* Heuristic */
    unusetmp = *(uint64_t *)sanitizeIndirectCall16Orig;
    UNUSED_ARG(unusetmp);

    checkIndirectLink = false;

    // void *result = sanitizeIndirectCall16Orig(a1, a2, a3, a4, a5, a6, a7, a8);

    // /* Heuristic */
    // tmp = *(int *)result;
    // UNUSED_ARG(tmp);

    // TRYRET result;
  }
  CATCH { 
    if (checkIndirectLink) {
      checkIndirectLink = false;
      callIndirectLink(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
    }
    CATCHRET NULL;
  }
  END_TRY;
  
}

/******************************************************************************
 * Sanitize Compare Functions
 ******************************************************************************/
__attribute__((no_sanitize("coverage"))) int sanitizeCmp13_1(int arg, int cmp) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(cmp);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      TRYRET cmp + 1;
    } else {
      TRYRET cmp - 1;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeCmp13_8(int arg, int cmp) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(cmp);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      TRYRET cmp + 1;
    } else {
      TRYRET cmp - 1;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeCmp13_16(int arg,
                                                              int cmp) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(cmp);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      TRYRET cmp + 1;
    } else {
      TRYRET cmp - 1;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeCmp13_32(int arg,
                                                              int cmp) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(cmp);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      TRYRET cmp + 1;
    } else {
      TRYRET cmp - 1;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeCmp13_64(int arg,
                                                              int cmp) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(cmp);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      TRYRET cmp + 1;
    } else {
      TRYRET cmp - 1;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) void *sanitizeCmp15(void *arg,
                                                             void *cmp) {
  TRY {
    RETURN_MAX_HOOKS(NULL);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      void *a = getPlayGround(arg)->out;
      TRYRET a;
    } else {
      void *a = getPlayGround(cmp)->out;
      TRYRET a;
    }
  }
  CATCH { CATCHRET NULL; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) uint64_t sanitizeCmp3(uint64_t arg,
                                                             uint64_t cmp) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(cmp);

    uint8_t state = getCmpStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg;
    } else if (state == 1) {
      TRYRET cmp;
    } else if (state == 2) {
      TRYRET cmp + 1;
    } else {
      TRYRET cmp - 1;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeSwitch13_1(int arg) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(0);

    uint16_t state = getSwitchStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg - 7;
    } else if (state == 1) {
      TRYRET arg - 6;
    } else if (state == 2) {
      TRYRET arg - 5;
    } else if (state == 3) {
      TRYRET arg - 4;
    } else if (state == 4) {
      TRYRET arg - 3;
    } else if (state == 5) {
      TRYRET arg - 2;
    } else if (state == 6) {
      TRYRET arg - 1;
    } else if (state == 7) {
      TRYRET arg;
    } else if (state == 8) {
      TRYRET arg + 1;
    } else if (state == 9) {
      TRYRET arg + 2;
    } else if (state == 10) {
      TRYRET arg + 3;
    } else if (state == 11) {
      TRYRET arg + 4;
    } else if (state == 12) {
      TRYRET arg + 5;
    } else if (state == 13) {
      TRYRET arg + 6;
    } else if (state == 14) {
      TRYRET arg + 7;
    } else {
      TRYRET arg + 8;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeSwitch13_8(int arg) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(0);

    uint16_t state = getSwitchStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg - 7;
    } else if (state == 1) {
      TRYRET arg - 6;
    } else if (state == 2) {
      TRYRET arg - 5;
    } else if (state == 3) {
      TRYRET arg - 4;
    } else if (state == 4) {
      TRYRET arg - 3;
    } else if (state == 5) {
      TRYRET arg - 2;
    } else if (state == 6) {
      TRYRET arg - 1;
    } else if (state == 7) {
      TRYRET arg;
    } else if (state == 8) {
      TRYRET arg + 1;
    } else if (state == 9) {
      TRYRET arg + 2;
    } else if (state == 10) {
      TRYRET arg + 3;
    } else if (state == 11) {
      TRYRET arg + 4;
    } else if (state == 12) {
      TRYRET arg + 5;
    } else if (state == 13) {
      TRYRET arg + 6;
    } else if (state == 14) {
      TRYRET arg + 7;
    } else {
      TRYRET arg + 8;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeSwitch13_16(int arg) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(0);

    uint16_t state = getSwitchStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg - 7;
    } else if (state == 1) {
      TRYRET arg - 6;
    } else if (state == 2) {
      TRYRET arg - 5;
    } else if (state == 3) {
      TRYRET arg - 4;
    } else if (state == 4) {
      TRYRET arg - 3;
    } else if (state == 5) {
      TRYRET arg - 2;
    } else if (state == 6) {
      TRYRET arg - 1;
    } else if (state == 7) {
      TRYRET arg;
    } else if (state == 8) {
      TRYRET arg + 1;
    } else if (state == 9) {
      TRYRET arg + 2;
    } else if (state == 10) {
      TRYRET arg + 3;
    } else if (state == 11) {
      TRYRET arg + 4;
    } else if (state == 12) {
      TRYRET arg + 5;
    } else if (state == 13) {
      TRYRET arg + 6;
    } else if (state == 14) {
      TRYRET arg + 7;
    } else {
      TRYRET arg + 8;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeSwitch13_32(int arg) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(0);

    uint16_t state = getSwitchStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg - 7;
    } else if (state == 1) {
      TRYRET arg - 6;
    } else if (state == 2) {
      TRYRET arg - 5;
    } else if (state == 3) {
      TRYRET arg - 4;
    } else if (state == 4) {
      TRYRET arg - 3;
    } else if (state == 5) {
      TRYRET arg - 2;
    } else if (state == 6) {
      TRYRET arg - 1;
    } else if (state == 7) {
      TRYRET arg;
    } else if (state == 8) {
      TRYRET arg + 1;
    } else if (state == 9) {
      TRYRET arg + 2;
    } else if (state == 10) {
      TRYRET arg + 3;
    } else if (state == 11) {
      TRYRET arg + 4;
    } else if (state == 12) {
      TRYRET arg + 5;
    } else if (state == 13) {
      TRYRET arg + 6;
    } else if (state == 14) {
      TRYRET arg + 7;
    } else {
      TRYRET arg + 8;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

__attribute__((no_sanitize("coverage"))) int sanitizeSwitch13_64(int arg) {
  TRY {
    RETURN_INT_RANDOM_MAX_HOOKS(0);

    uint16_t state = getSwitchStates((uint64_t)arg);
    if (state == 0) {
      TRYRET arg - 7;
    } else if (state == 1) {
      TRYRET arg - 6;
    } else if (state == 2) {
      TRYRET arg - 5;
    } else if (state == 3) {
      TRYRET arg - 4;
    } else if (state == 4) {
      TRYRET arg - 3;
    } else if (state == 5) {
      TRYRET arg - 2;
    } else if (state == 6) {
      TRYRET arg - 1;
    } else if (state == 7) {
      TRYRET arg;
    } else if (state == 8) {
      TRYRET arg + 1;
    } else if (state == 9) {
      TRYRET arg + 2;
    } else if (state == 10) {
      TRYRET arg + 3;
    } else if (state == 11) {
      TRYRET arg + 4;
    } else if (state == 12) {
      TRYRET arg + 5;
    } else if (state == 13) {
      TRYRET arg + 6;
    } else if (state == 14) {
      TRYRET arg + 7;
    } else {
      TRYRET arg + 8;
    }
  }
  CATCH { CATCHRET arg; }
  END_TRY;
}

/******************************************************************************
 * Function hooks
 ******************************************************************************/
#define NEVER_FREE_LIST_SIZE 10
void *never_free_list[NEVER_FREE_LIST_SIZE];
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
registerNeverFree(void *ptr) {
  for (int i = 0; i < NEVER_FREE_LIST_SIZE; i++) {
    if (never_free_list[i] == NULL) {
      never_free_list[i] = ptr;
      return;
    }
  }
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
clearNeverFree(void) {
  for (int i = 0; i < NEVER_FREE_LIST_SIZE; i++) {
    if (never_free_list[i] != NULL) {
      free(never_free_list[i]);
      never_free_list[i] = NULL;
    }
  }
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void
hook_free(void *ptr) {
  for (int i = 0; i < NEVER_FREE_LIST_SIZE; i++) {
    if (never_free_list[i] == ptr) {
      return;
    }
  }
  free(ptr);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
hook_malloc(uint64_t size) {
  if (size > 0x10000) return NULL;
  void *ptr = malloc(size);
  return ptr;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
hook_memcpy(void *dest, const void *src, size_t n) {
  if (n > 0x100000) {
    return dest;
  }
  return memcpy(dest, src, n);
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) void *
hook_memset(void *s, int c, size_t n) {
  if (n > 0x100000) {
    return s;
  }
  return memset(s, c, n);
}

/******************************************************************************
 * Ignore hook function
 ******************************************************************************/
void ignoreHook7(void) {
  TRY { RESET_MAX_HOOKS; }
  CATCH {}
  END_TRY;
}

void *ignoreHook15(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET NULL;
  }
  CATCH { CATCHRET NULL; }
  END_TRY;
}

int ignoreHook13_1(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH { CATCHRET 0; }
  END_TRY;
}

int ignoreHook13_2(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH {}
  END_TRY;
}

int ignoreHook13_4(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH {}
  END_TRY;
}

int ignoreHook13_8(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH { CATCHRET 0; }
  END_TRY;
}

int ignoreHook13_16(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH { CATCHRET 0; }
  END_TRY;
}

int ignoreHook13_32(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH { CATCHRET 0; }
  END_TRY;
}

int ignoreHook13_64(void) {
  TRY {
    RESET_MAX_HOOKS;
    TRYRET 0;
  }
  CATCH { CATCHRET 0; }
  END_TRY;
}

/******************************************************************************
 * Entry Function Lists (Used for cross fuzz)
 ******************************************************************************/
struct entryFunctionInfo {
  void (*func)(void *, void *, void *, void *, 
               void *, void *, void *, void *,
               void *, void *, void *, void *,
               void *, void *, void *, void *);
  int input_index;
  int length_index;
  int depth;
};

#define MAX_ENTRY_FUNCTION 64
struct entryFunctionInfo entryFunctionList[MAX_ENTRY_FUNCTION];

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
registerEntryFunction(void (*func)(void *, void *, void *, void *, 
                                   void *, void *, void *, void *,
                                   void *, void *, void *, void *,
                                   void *, void *, void *, void *),
                      int input_index, int length_index, int depth) {
  /* 0 is always default entry function */
  for (int i = 1; i < MAX_ENTRY_FUNCTION; i++) {
    if (entryFunctionList[i].func == func) {
      /* Update depth */
      if (entryFunctionList[i].depth < depth) {
        entryFunctionList[i].depth = depth;
      }
      return 1;
    }
    if (entryFunctionList[i].func == NULL) {
      entryFunctionList[i].func = func;
      entryFunctionList[i].input_index = input_index;
      entryFunctionList[i].length_index = length_index;
      entryFunctionList[i].depth = depth;
      printf("Regitser entry function %p\n", func);
      return 0;
    }
  }
  return 1;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
getEntryPointsNum(void) {
  int num = 0;
  for (int i = 0; i < MAX_ENTRY_FUNCTION; i++) {
    if (entryFunctionList[i].func) {
      num++;
    }
  }
  return num;
}

/* TODO: Change it in more efficient way */
/* This function is overriden by the user */
__attribute__((no_sanitize("coverage"), no_sanitize("address"), weak)) bool
checkCrossValid(void **input, int input_index, uint64_t *length, int length_index) {
  return true;
}

static uint64_t max_depth;
extern uint64_t current_test_depth;
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
crossEntryPoints(void *input, uint64_t length) {
  /* We used the last 1 byte of state bitmap to select entry functions */
  uint8_t state = state_buf[MAX_CMPS_PER_EXECUTION / 4 - 1];
  int input_index, length_index;

  current_test_depth = max_depth;

  int numEntryPoints = getEntryPointsNum();
  int target = state % (numEntryPoints + 1); // default + number of registered

  if (entryFunctionList[target].func) {
    input_index = entryFunctionList[target].input_index;
    length_index = entryFunctionList[target].length_index;
    void *a1 = input_index == 0 ? input : length_index == 0 ? (void *)length : NULL;
    void *a2 = input_index == 1 ? input : length_index == 1 ? (void *)length : NULL;
    void *a3 = input_index == 2 ? input : length_index == 2 ? (void *)length : NULL;
    void *a4 = input_index == 3 ? input : length_index == 3 ? (void *)length : NULL;
    void *a5 = input_index == 4 ? input : length_index == 4 ? (void *)length : NULL;
    void *a6 = input_index == 5 ? input : length_index == 5 ? (void *)length : NULL;
    void *a7 = input_index == 6 ? input : length_index == 6 ? (void *)length : NULL;
    void *a8 = input_index == 7 ? input : length_index == 7 ? (void *)length : NULL;
    void *a9 = input_index == 8 ? input : length_index == 8 ? (void *)length : NULL;
    void *a10 = input_index == 9 ? input : length_index == 9 ? (void *)length : NULL;
    void *a11 = input_index == 10 ? input : length_index == 10 ? (void *)length : NULL;
    void *a12 = input_index == 11 ? input : length_index == 11 ? (void *)length : NULL;
    void *a13 = input_index == 12 ? input : length_index == 12 ? (void *)length : NULL;
    void *a14 = input_index == 13 ? input : length_index == 13 ? (void *)length : NULL;
    void *a15 = input_index == 14 ? input : length_index == 14 ? (void *)length : NULL;
    void *a16 = input_index == 15 ? input : length_index == 15 ? (void *)length : NULL;
    if (checkCrossValid(&input, input_index, &length, length_index)) {
      current_test_depth = (uint64_t)entryFunctionList[target].depth;
      entryFunctionList[target].func(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12, a13, a14, a15, a16);
      return 1;
    }
    return 0;
  }
  return 0;
}

__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static int
getDepth(char *funcName) {
  /* ancestors file should be at the running directory */
  char *ancestorFile = "ancestors";
  int myDepth = -1;
  if (access(ancestorFile, F_OK) != -1) {
    FILE *fp = fopen(ancestorFile, "r");
    if (fp) {
      char line[1024];
      while (fgets(line, sizeof(line), fp)) {
        char *func = strtok(line, ",");
        if (func) {
          // if (strcmp(func, funcName) == 0) {
          if (strstr(funcName, func)) {
            /* Find depth */
            myDepth = atoi(strtok(NULL, ","));
            break;
          }
        }
      }
      fclose(fp);
    }
  }
  return myDepth;
}


__attribute__((no_sanitize("coverage"), no_sanitize("address"))) static int
getMaxIndex(char *nmfileName, char *funcName) {
  FILE *nmfp = fopen(nmfileName, "r");
  int maxIndex = -1;
  /* Remove last '_' */
  char origFuncName[1024];
  snprintf(origFuncName, sizeof(origFuncName), "%s", funcName);
  char *lastDash = strrchr(origFuncName, '_');
  if (lastDash) {
    *lastDash = '\0';
  }

  if (nmfp) {
    char nmLine[1024];
    while (fgets(nmLine, sizeof(nmLine), nmfp)) {
      char *funcAddr = strtok(nmLine, " ");
      char *funcType = strtok(NULL, " ");
      char *funcName = strtok(NULL, " ");
      if (funcName) {
        if (strstr(funcName, origFuncName)) {
          char *funcIndex = strrchr(funcName, '_');
          if (funcIndex) {
            int index = atoi(funcIndex + 1);
            if (index > maxIndex) {
              maxIndex = index;
            }
          }
        }
      }
    }
    fclose(nmfp);
  }
  return maxIndex;
}

#if 0
__attribute__((no_sanitize("coverage"), no_sanitize("address"))) int
LLVMFuzzerInitialize(int *argc, char ***argv) {
  char path[1024];
  /* What is my name? */
  /* Filename should looks like (project)-functionname-fuzz */
  /* If it starts with 'top', it is top fuzzer */
  char *filename = strrchr((*argv)[0], '/') ? strrchr((*argv)[0], '/') + 1 : (*argv)[0];
  if (strcmp(filename, "riot") == 0) {
    ssize_t len = readlink("/proc/self/exe", path, sizeof(path) - 1);

    if (len != -1) {
      path[len] = '\0';
      filename = strrchr(path, '/') ? strrchr(path, '/') + 1 : path;
    }
  }
  char tmpFileName[1024];
  char *funcName;
  snprintf(tmpFileName, sizeof(tmpFileName), "%s", filename);
  char *lastDash = strrchr(tmpFileName, '-');
  if (lastDash) {
    *lastDash = '\0';
  }
  funcName = strrchr(tmpFileName, '-') + 1;

  /* Get my depth */
  int myDepth = -1;
  myDepth = getDepth(funcName);
  if (myDepth < 0) {
    return 0;
  }

  max_depth = myDepth;
  if (strncmp(filename, "top", 3) == 0) {
    return 0;
  }
  
  /* Get the entire function map */
  char outputFile[1024];
  char nmCmd[2048];
  snprintf(outputFile, sizeof(outputFile), "/tmp/%s.map", filename);
  snprintf(nmCmd, sizeof(nmCmd), "nm %s > %s", (*argv)[0], outputFile);
  system(nmCmd);

  /* multi_entry list */
  char multiEntryFile[1024];
  snprintf(multiEntryFile, sizeof(multiEntryFile), "/tmp/%s_multi_entry.txt", funcName);
  FILE *multiEntryFp = fopen(multiEntryFile, "r");
  if (multiEntryFp) {
    char line[1024];
    while (fgets(line, sizeof(line), multiEntryFp)) {
      char *entryFuncName = line;
      char *funcIndex = strrchr(line, '_');
      int inputIndexInt = -1;
      int lengthIndexInt = -1;
      int maxIndex = getMaxIndex(outputFile, entryFuncName);

      /* Get Function input index and length index */
      if (funcIndex) {
        int index = atoi(funcIndex + 1);
        for (int i = 0; i < 8; i++) {
          if ((1 << i) > maxIndex) {
            break;
          }
          if ((index & (1 << i)) == 0) {
            if (inputIndexInt < 0) {
              inputIndexInt = i;
            } else {
              lengthIndexInt = i;
              break;
            }
          }
        }
      }
      if (inputIndexInt < 0) {
        continue;
      }

      /* Get address from entryFuncName */
      FILE *nmfp = fopen(outputFile, "r");
      if (nmfp) {
        char nmLine[1024];
        while (fgets(nmLine, sizeof(nmLine), nmfp)) {
          char *funcAddr = strtok(nmLine, " ");
          char *funcType = strtok(NULL, " ");
          char *funcName = strtok(NULL, " ");
          if (funcName) {
            if (strcmp(funcName, entryFuncName) == 0) {
              int funcDepth = getDepth(funcName);
              if (funcDepth <= 0 || funcDepth > myDepth) {
                continue;
              }

              void *func = (void *)strtoull(funcAddr, NULL, 16);
              if (registerEntryFunction(func, inputIndexInt, lengthIndexInt, funcDepth) == 0) {
                fprintf(stderr, "[+] Register %s at %p with input index %d and length index %d\n", entryFuncName, func, inputIndexInt, lengthIndexInt);
              }
              break;
            }
          }
        }
        fclose(nmfp);
      }
    }
    fclose(multiEntryFp);
  }
}

#endif