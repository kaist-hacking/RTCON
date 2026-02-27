diff --git a/cpu/native/irq_cpu.c b/cpu/native/irq_cpu.c
index c5e404115a..9e457e9ab9 100644
--- a/cpu/native/irq_cpu.c
+++ b/cpu/native/irq_cpu.c
@@ -356,6 +356,7 @@ void native_isr_entry(int sig, siginfo_t *info, void *context)
     ((ucontext_t *)context)->uc_mcontext.arm_pc = (unsigned int)&_native_sig_leave_tramp;
 #else /* Linux/x86 */
   #ifdef __x86_64__
+    exit(0);
     _native_saved_eip = ((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP];
     ((ucontext_t *)context)->uc_mcontext.gregs[REG_RIP] = (uintptr_t)&_native_sig_leave_tramp;
   #else
@@ -515,6 +516,16 @@ void native_interrupt_init(void)
         err(EXIT_FAILURE, "native_interrupt_init: sigaction");
     }
 
+    if (sigdelset(&_native_sig_set, SIGSEGV) == -1) {
+        err(EXIT_FAILURE, "native_interrupt_init: sigdelset");
+    }
+    if (sigdelset(&_native_sig_set_dint, SIGSEGV) == -1) {
+        err(EXIT_FAILURE, "native_interrupt_init: sigdelset");
+    }
+    if (sigaction(SIGSEGV, &sa, NULL)) {
+        err(EXIT_FAILURE, "native_interrupt_init: sigaction");
+    }
+
     if (getcontext(&native_isr_context) == -1) {
         err(EXIT_FAILURE, "native_interrupt_init: getcontext");
     }
