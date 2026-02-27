diff --git a/Makefile.include b/Makefile.include
index 771f7cfd49..d34a0f7445 100644
--- a/Makefile.include
+++ b/Makefile.include
@@ -481,7 +481,7 @@ endif
 # provide this macro
 TOOLCHAINS_SUPPORTED ?= gnu
 # Import all toolchain settings
-include $(RIOTMAKE)/toolchain/$(TOOLCHAIN).inc.mk
+include $(RIOTMAKE)/toolchain/$(TOOLCHAIN)$(TOOLCHAIN_SURFFIX).inc.mk
 
 # Other than on native, RWX segments in ROM are not actually RWX, as regular
 # store instructions won't write to flash.
 