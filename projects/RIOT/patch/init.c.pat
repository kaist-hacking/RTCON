diff --git a/core/lib/init.c b/core/lib/init.c
index e6e4c026..d7096135 100644
--- a/core/lib/init.c
+++ b/core/lib/init.c
@@ -43,11 +43,12 @@
     RIOT_VERSION ")"
 #endif
 
-extern int main(void);
+extern int main(int argc, char **argv);
 
 static char main_stack[THREAD_STACKSIZE_MAIN];
 static char idle_stack[THREAD_STACKSIZE_IDLE];
 
+extern char **_native_argv;
 static void *main_trampoline(void *arg)
 {
     (void)arg;
@@ -60,7 +61,9 @@ static void *main_trampoline(void *arg)
         LOG_INFO(CONFIG_BOOT_MSG_STRING "\n");
     }
 
-    int res = main();
+    int argc = 2;
+    char *argv[] = {"riot", _native_argv[1], NULL, NULL};
+    int res = main(argc, argv);
 
     if (IS_USED(MODULE_TEST_UTILS_MAIN_EXIT_CB)) {
         void test_utils_main_exit_cb(int res);
@@ -101,6 +104,13 @@ static void *idle_thread(void *arg)
     return NULL;
 }
 
+#include <sanitizer/asan_interface.h>
+
+void resetMemoryPoison(void)
+{
+    ASAN_UNPOISON_MEMORY_REGION(main_stack, sizeof(main_stack));
+}
+
 void kernel_init(void)
 {
     if (!IS_USED(MODULE_CORE_THREAD)) {
