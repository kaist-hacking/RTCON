diff --git a/makefiles/arch/native.inc.mk b/makefiles/arch/native.inc.mk
index 78cbdbb3..d56c983f 100644
--- a/makefiles/arch/native.inc.mk
+++ b/makefiles/arch/native.inc.mk
@@ -24,7 +24,7 @@ ifneq (,$(filter backtrace,$(USEMODULE)))
   $(warning module backtrace is used, do not omit frame pointers)
   CFLAGS_OPT ?= -Og -fno-omit-frame-pointer
 else
-  CFLAGS_OPT ?= -Og
+  CFLAGS_OPT ?=
 endif
 
 # default std set to gnu11 if not overwritten by user
