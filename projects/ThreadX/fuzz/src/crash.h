#include <setjmp.h>
#include <signal.h>
#include <stdbool.h>

struct sanitizedFunction {
  char *name;
  struct sanitizedFunction *next;
};

struct crash {
  void *addr;
  struct crash *next;
};

void registerSanitizedScope(char *name);
bool isSanitizedScope(char *name);

/***********************************************************************
 *  The following macros are used to restore from a crash.
 ***********************************************************************/
extern void __asan_set_error_report_callback(void (*callback)(const char *));

#define MAIN_TRY                                                               \
  do {                                                                         \
    struct sigaction sa_new;                                                   \
    sa_new.sa_sigaction = ground_signal_handler;                               \
    sigemptyset(&sa_new.sa_mask);                                              \
    sa_new.sa_flags = SA_SIGINFO;                                              \
    sigaction(SIGSEGV, &sa_new, NULL);                                         \
    sigaction(SIGUSR1, &sa_new, NULL);                                         \
    sigaction(SIGABRT, &sa_new, NULL);                                         \
    sigaction(SIGFPE, &sa_new, NULL);                                          \
    sigaction(SIGILL, &sa_new, NULL);                                          \
    sigaction(SIGBUS, &sa_new, NULL);                                          \
    sigaction(SIGTRAP, &sa_new, NULL);                                         \
    sigaction(SIGALRM, &sa_new, NULL);                                         \
    sigaction(SIGSYS, &sa_new, NULL);                                          \
    sigaction(SIGXCPU, &sa_new, NULL);                                         \
    sigaction(SIGXFSZ, &sa_new, NULL);                                         \
    sigaction(SIGPIPE, &sa_new, NULL);                                         \
    sigaction(SIGTERM, &sa_new, NULL);                                         \
    sigaction(SIGQUIT, &sa_new, NULL);                                         \
    sigaction(SIGKILL, &sa_new, NULL);                                         \
    sigaction(SIGSTOP, &sa_new, NULL);                                         \
    resetSanitizeCallTaintScoreStack();                                        \
    if (sigsetjmp(main_jump_buffer, 2) == 0)

#define MAIN_CATCH else

#define MAIN_END_TRY                                                           \
  }                                                                            \
  while (0)


/*-----------------------------------------------------------*/
void register_asan_report_cb(void);

void hookASANMalloc(void);
void unhookASANMalloc(void);

void resetSanitizeCallTaintScoreStack(void);
void pushSanitizeCallTaintScoreStack(void);
void popSanitizeCallTaintScoreStack(void);

int analyzeCrash(const char *);