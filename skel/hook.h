#include <stdlib.h>

#define RED "\033[0;31m"
#define RESET "\033[0m"

#define MAX_HOOKS_PER_EXECUTION 3000
#define MAX_CMPS_PER_EXECUTION 512

#define PLAYGROUND_SIZE 4096

#define UNUSED_ARG(x)

/***********************************************************************
 *  The following macros are used to restore from a crash.
 ***********************************************************************/
struct sigaction sa_new;
#define TRY                                                                    \
  do {                                                                         \
    sa_new.sa_sigaction = signal_handler;                                      \
    sigemptyset(&sa_new.sa_mask);                                              \
    sa_new.sa_flags = SA_SIGINFO;                                              \
    sigaction(SIGSEGV, &sa_new, &sa_old[sigjmp_depth]);                        \
                                                                               \
    hookCounter++;                                                             \
    if (sigjmp_depth >= MAX_SIGJMP_DEPTH) {                                    \
      printf(RED "Max sigjmp depth reached" RESET "\n");                       \
      raise(SIGUSR1);                                                          \
    }                                                                          \
    if (sigsetjmp(jump_buffer[sigjmp_depth++], 1) == 0)

#define TRYRET                                                                 \
  if (sigjmp_depth > 0) {                                                      \
    sigjmp_depth--;                                                            \
  }                                                                            \
  sigaction(SIGSEGV, &sa_old[sigjmp_depth], NULL);                             \
  return

#define CATCH else

#define CATCHRET                                                               \
  if (sigjmp_depth > 0) {                                                      \
    sigjmp_depth--;                                                            \
  }                                                                            \
  sigaction(SIGSEGV, &sa_old[sigjmp_depth], NULL);                             \
  return

#define END_TRY                                                                \
  if (sigjmp_depth > 0) {                                                      \
    sigjmp_depth--;                                                            \
  }                                                                            \
  sigaction(SIGSEGV, &sa_old[sigjmp_depth], NULL);                             \
  }                                                                            \
  while (0)

/***********************************************************************
 *  Max hook checker
 *
 *  The following macros are used to implement the max hook check mechanism.
 ***********************************************************************/
#define RETURN_INT_RANDOM_MAX_HOOKS(x)                                         \
  do {                                                                         \
    if (hookCounter >= MAX_HOOKS_PER_EXECUTION) {                              \
      raise(SIGUSR1);                                                          \
    }                                                                          \
  } while (0)

#define RETURN_MAX_HOOKS(x)                                                    \
  do {                                                                         \
    if (hookCounter >= MAX_HOOKS_PER_EXECUTION) {                              \
      raise(SIGUSR1);                                                          \
    }                                                                          \
  } while (0)

#define RESET_MAX_HOOKS                                                        \
  do {                                                                         \
    if (hookCounter >= MAX_HOOKS_PER_EXECUTION) {                              \
      raise(SIGUSR1);                                                          \
    }                                                                          \
  } while (0)

/***********************************************************************
 *  LLVM Type ID
 ***********************************************************************/
enum TypeID {
  // PrimitiveTypes
  HalfTyID = 0,  ///< 16-bit floating point type
  BFloatTyID,    ///< 16-bit floating point type (7-bit significand)
  FloatTyID,     ///< 32-bit floating point type
  DoubleTyID,    ///< 64-bit floating point type
  X86_FP80TyID,  ///< 80-bit floating point type (X87)
  FP128TyID,     ///< 128-bit floating point type (112-bit significand)
  PPC_FP128TyID, ///< 128-bit floating point type (two 64-bits, PowerPC)
  VoidTyID,      ///< type with no size
  LabelTyID,     ///< Labels
  MetadataTyID,  ///< Metadata
  X86_MMXTyID,   ///< MMX vectors (64 bits, X86 specific)  (10)
  X86_AMXTyID,   ///< AMX vectors (8192 bits, X86 specific)
  TokenTyID,     ///< Tokens

  // Derived types... see DerivedTypes.h file.
  IntegerTyID,        ///< Arbitrary bit width integers    (13)
  FunctionTyID,       ///< Functions
  PointerTyID,        ///< Pointers
  StructTyID,         ///< Structures                       (16)
  ArrayTyID,          ///< Arrays                           (17)
  FixedVectorTyID,    ///< Fixed width SIMD vector type
  ScalableVectorTyID, ///< Scalable SIMD vector type
  TypedPointerTyID,   ///< Typed pointer used by some GPU targets
  TargetExtTyID,      ///< Target extension type
};

void resetHookCounter(void);
