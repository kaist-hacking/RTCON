/*
 * FreeRTOS V202212.00
 * Copyright (C) 2020 Amazon.com, Inc. or its affiliates. All Rights Reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 * https://www.FreeRTOS.org
 * https://github.com/FreeRTOS
 *
 */

/******************************************************************************
 * This project provides one demo application.  A TCP echo demo.
 * The mainSELECTED_APPLICATION setting is used to select between
 * the three
 *
 * If mainSELECTED_APPLICATION = ECHO_CLIENT_DEMO the tcp echo demo will be
 *built. This is implemented and described in main_networking.c
 *
 * This file implements the code that is not demo specific, including the
 * hardware setup and FreeRTOS hook functions.
 *
 *******************************************************************************
 * NOTE: Linux will not be running the FreeRTOS demo threads continuously, so
 * do not expect to get real time behaviour from the FreeRTOS Linux port, or
 * this demo application.  Also, the timing information in the FreeRTOS+Trace
 * logs have no meaningful units.  See the documentation page for the Linux
 * port for further information:
 * https://freertos.org/FreeRTOS-simulator-for-Linux.html
 *
 *******************************************************************************
 */

/* Standard includes. */
#include <errno.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

/* FreeRTOS kernel includes. */
#include "FreeRTOS.h"
#include "task.h"

#include <trcRecorder.h>

#include "crash.h"
#include "fuzz.h"

/* This demo uses heap_3.c (the libc provided malloc() and free()). */

/*-----------------------------------------------------------*/
static void traceOnEnter(void);

/*
 * Prototypes for the standard FreeRTOS application hook (callback) functions
 * implemented within this file.  See http://www.freertos.org/a00016.html .
 */
void vApplicationMallocFailedHook(void);
void vApplicationIdleHook(void);
void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName);
void vApplicationTickHook(void);

/*
 * Writes trace data to a disk file when the trace recording is stopped.
 * This function will simply overwrite any trace files that already exist.
 */
static void prvSaveTraceFile(void);

/*-----------------------------------------------------------*/

/* When configSUPPORT_STATIC_ALLOCATION is set to 1 the application writer can
 * use a callback function to optionally provide the memory required by the idle
 * and timer tasks.  This is the stack that will be used by the timer task.  It
 * is declared here, as a global, so it can be checked by a test that is
 * implemented in a different file. */
StackType_t uxTimerTaskStack[configTIMER_TASK_STACK_DEPTH];

/* Notes if the trace is running or not. */
#if (TRACE_ON_ENTER == 1)
static BaseType_t xTraceRunning = pdTRUE;
#else
static BaseType_t xTraceRunning = pdFALSE;
#endif

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


/*-----------------------------------------------------------*/

#ifdef FUZZ_COVERAGE
#include <pthread.h>
#include <stdio.h>
#include <sys/stat.h>

#define COVERAGE_INTERVAL (5 * 60)
#define COVERAGE_UPDATE_INTERVAL 60

#ifndef COVERAGE_DIR_PATH
#define COVERAGE_DIR_PATH "coverage_dir"
#endif

#define FLUSH_INTERVAL 30
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

__attribute__((no_sanitize("address"))) int
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

/*-----------------------------------------------------------*/

void vApplicationMallocFailedHook(void) {
  /* vApplicationMallocFailedHook() will only be called if
   * configUSE_MALLOC_FAILED_HOOK is set to 1 in FreeRTOSConfig.h.  It is a hook
   * function that will get called if a call to pvPortMalloc() fails.
   * pvPortMalloc() is called internally by the kernel whenever a task, queue,
   * timer or semaphore is created.  It is also called by various parts of the
   * demo application.  If heap_1.c, heap_2.c or heap_4.c is being used, then
   * the size of the    heap available to pvPortMalloc() is defined by
   * configTOTAL_HEAP_SIZE in FreeRTOSConfig.h, and the xPortGetFreeHeapSize()
   * API function can be used to query the size of free heap space that remains
   * (although it does not provide information on how the remaining heap might
   * be fragmented).  See http://www.freertos.org/a00111.html for more
   * information. */
  vAssertCalled(__FILE__, __LINE__);
}
/*-----------------------------------------------------------*/

void vApplicationIdleHook(void) {
  /* vApplicationIdleHook() will only be called if configUSE_IDLE_HOOK is set
   * to 1 in FreeRTOSConfig.h.  It will be called on each iteration of the idle
   * task.  It is essential that code added to this hook function never attempts
   * to block in any way (for example, call xQueueReceive() with a block time
   * specified, or call vTaskDelay()).  If application tasks make use of the
   * vTaskDelete() API function to delete themselves then it is also important
   * that vApplicationIdleHook() is permitted to return to its calling function,
   * because it is the responsibility of the idle task to clean up memory
   * allocated by the kernel to any task that has since deleted itself. */

  usleep(15000);
  traceOnEnter();
}
/*-----------------------------------------------------------*/

void vApplicationStackOverflowHook(TaskHandle_t pxTask, char *pcTaskName) {
  (void)pcTaskName;
  (void)pxTask;

  /* Run time stack overflow checking is performed if
   * configCHECK_FOR_STACK_OVERFLOW is defined to 1 or 2.  This hook
   * function is called if a stack overflow is detected.  This function is
   * provided as an example only as stack overflow checking does not function
   * when running the FreeRTOS POSIX port. */
  vAssertCalled(__FILE__, __LINE__);
}
/*-----------------------------------------------------------*/

void vApplicationTickHook(void) {
  /* This function will be called by each tick interrupt if
   * configUSE_TICK_HOOK is set to 1 in FreeRTOSConfig.h.  User code can be
   * added here, but the tick hook is called from an interrupt context, so
   * code must not attempt to block, and only the interrupt safe FreeRTOS API
   * functions can be used (those that end in FromISR()). */
}

void traceOnEnter() {
  int xReturn;
  struct timeval tv = {0L, 0L};
  fd_set fds;

  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);

  xReturn = select(STDIN_FILENO + 1, &fds, NULL, NULL, &tv);

  if (xReturn > 0) {
    if (xTraceRunning == pdTRUE) {
      taskENTER_CRITICAL();
      { prvSaveTraceFile(); }
      taskEXIT_CRITICAL();
    }

    /* clear the buffer */
    char buffer[1];
    size_t xReadRet;
    xReadRet = read(STDIN_FILENO, &buffer, 1);
    (void)xReadRet;
  }
}

void vLoggingPrintf(const char *pcFormat, ...) {
  va_list arg;

  va_start(arg, pcFormat);
  vprintf(pcFormat, arg);
  va_end(arg);
}
/*-----------------------------------------------------------*/

void vApplicationDaemonTaskStartupHook(void) {
  /* This function will be called once only, when the daemon task starts to
   * execute (sometimes called the timer task). This is useful if the
   * application includes initialisation code that would benefit from executing
   * after the scheduler has been started. */
}
/*-----------------------------------------------------------*/

void vAssertCalled(const char *const pcFileName, unsigned long ulLine) {
  return;
}
/*-----------------------------------------------------------*/

static void prvSaveTraceFile(void) {
/* Tracing is not used when code coverage analysis is being performed. */
#if (projCOVERAGE_TEST != 1)
  {
    FILE *pxOutputFile;
    pxOutputFile = fopen("Trace.dump", "wb");

    if (pxOutputFile != NULL) {
      {
        extern TraceRingBuffer_t *RecorderDataPtr;
        xTraceDisable();
        fwrite(RecorderDataPtr, sizeof(TraceRingBuffer_t), 1, pxOutputFile);
        fclose(pxOutputFile);
        printf("\r\nTrace output saved to Trace.dump\r\n");
        xTraceEnable(TRC_START);
      }
    } else {
      printf("\r\nFailed to create trace dump file\r\n");
    }
  }
#endif /* if ( projCOVERAGE_TEST != 1 ) */
}
/*-----------------------------------------------------------*/

static uint32_t ulEntryTime = 0U;

void vTraceTimerReset(void) { ulEntryTime = xTaskGetTickCount(); }

uint32_t uiTraceTimerGetFrequency(void) { return configTICK_RATE_HZ; }

uint32_t uiTraceTimerGetValue(void) {
  return (xTaskGetTickCount() - ulEntryTime);
}

/*-----------------------------------------------------------*/
