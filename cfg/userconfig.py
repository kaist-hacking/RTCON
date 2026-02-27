
# assert functions are used to get out of the current function frame
assert_func = [
    "_assert_failure",
    "__assert_fail",
    "abort",
]

# skip functions are just returned
skip_func = [
    # RIOT
    "ztimer_set",
    "_native_sleep",
    "thread_yield_higher",
    "mutex_lock_internal",
    "mutex_unlock_internal",
    "os_msys_get_pkthdr",       # Do not allocate memory
    "atomic_test_bit",
    # ThreadX
    "_tx_thread_sleep",
    "_tx_linux_debug_entry_insert",
    "_tx_thread_interrupt_disable",
    "_tx_thread_interrupt_restore",
    # Zephyr
    "posix_irq_handler_im_from_sw",
    "posix_swap",
    "k_work_cancel_delayable",
    "k_work_reschedule",
    "k_work_schedule",
    "k_work_add_arg_delayable",
    "k_work_flush",
    "net_buf_unref",
]

# Any functions that contains these keywords are skipped.
skip_keywords = [
    "mutex",
    "Mutex",
    "MUTEX",
    "sem_",
    "Semaphore",
]

# Functions that does not need to be sanitized.
# e.g., User implemented functions in the harness are not need to be sanitized.
ban_func = [
    "printf",
    "printk",
    "sprintf",
    "snprintf",
    "vsprintf",
    "vsnprintf",
    "vprintf",
    "vfprintf",
    "vsprintf",
    "vsnprintf",
    "vsscanf",
    "fprintf",
    "fscanf",
    "scanf",
    "sscanf",
    # RIOT
    "_pktbuf_alloc",
    "os_memblock_get",
    "lwip_netdev_init", # User implemented functions
    "sys_mbox_trypost_fromisr", # User implemented functions
    # FreeRTOS
    "uiTraceTimerGetValue", # User implemented functions
    "uiTraceTimerGetFrequency", # User implemented functions
    "vTraceTimerReset", # User implemented functions
    "prvSaveTraceFile", # User implemented functions
    "vAssertCalled", # User implemented functions
    "traceOnEnter", # User implemented functions
    "vLoggingPrintf", # User implemented functions
    "vApplicationMallocFailedHook", # User implemented functions
    "vApplicationIdleHook", # User implemented functions
    "vApplicationStackOverflowHook", # User implemented functions
    "vApplicationTickHook", # User implemented functions
    "vApplicationDaemonTaskStartupHook", # User implemented functions
    "ulApplicationGetNextSequenceNumber", # User implemented functions
    "uxRand", # User implemented functions
    "xApplicationGetRandomNumber", # User implemented functions
    "vApplicationIPNetworkEventHook_Multi", # User implemented functions
    "vApplicationIPNetworkEventHook", # User implemented functions
    "xPortGetMinimumEverFreeHeapSize", # User implemented functions
    "xApplicationDNSQueryHook_Multi", # User implemented functions
    "vApplicationPingReplyHook", # User implemented functions
    "pcApplicationHostnameHook", # User implemented functions
    "ulApplicationTimeHook", # User implemented functions
]

# Functions that contains these keywords are not sanitized.
ban_keywords = [
    # Zephyr
    "k_",
    "z_",
    "sys_",
    "atomic_",
    "arch_",
    "net_buf_id",
    "net_buf_append_bytes",
    "net_buf_slist",
    "net_buf_get",
    "net_buf_put",
    "net_buf_alloc",
    "net_buf_ref",
    # RIOT
    "_native_",
    "panic_",
    "thread_",
    "irq_",
    "panic_",
    "ztimer_",
    "sio_", # User implemented functions
    "ble_transport_to_ll_", # User implemented functions
    "hal_", # User implemented functions
    # ThreadX
    "_tx_",
    # "_nx_packet_",
    # "_nx_tcp_",
    # FreeRTOS
]

# Deprecated
top_functions = []

indirect_call_match = True

# User can hook functions.
# e.g., RTOSes use a huge memory pool to allocate memory which results in that
# ASAN cannot catch the memory errors as the memory pools are not poisoned. We 
# can hook the memory allocation functions to poison the memory pools.
# 
hook_functions = [
    "_nx_packet_allocate",
    "_nx_packet_release",
    # "_nx_udp_socket_receive",
]