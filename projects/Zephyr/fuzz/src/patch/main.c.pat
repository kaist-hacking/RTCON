diff --git a/boards/native/native_posix/main.c b/boards/native/native_posix/main.c
index f68f4dbf0e3..f12e43f593b 100644
--- a/boards/native/native_posix/main.c
+++ b/boards/native/native_posix/main.c
@@ -107,9 +107,14 @@ void posix_exec_for(uint64_t us)
  * Not used when building fuzz cases, as libfuzzer has its own main()
  * and calls the "OS" through a per-case fuzz test entry point.
  */
+int _native_argc;
+char **_native_argv;
 int main(int argc, char *argv[])
 {
-	posix_init(argc, argv);
+	_native_argc = argc;
+	_native_argv = argv;
+	char *mock_argv[] = {"zephyr", "--bt-dev=hci0", NULL};
+	posix_init(2, mock_argv);
 	while (true) {
 		hwm_one_event();
 	}
