diff --git a/sys/auto_init/auto_init.c b/sys/auto_init/auto_init.c
index 8f2212c336..b90209c2c1 100644
--- a/sys/auto_init/auto_init.c
+++ b/sys/auto_init/auto_init.c
@@ -205,9 +205,9 @@ AUTO_INIT(esp_ble_nimble_init,
           AUTO_INIT_PRIO_MOD_ESP_BLE_NIMBLE);
 #endif
 #if IS_USED(MODULE_NIMBLE)
-extern void nimble_riot_init(void);
-AUTO_INIT(nimble_riot_init,
-          AUTO_INIT_PRIO_MOD_NIMBLE);
+// extern void nimble_riot_init(void);
+// AUTO_INIT(nimble_riot_init,
+//           AUTO_INIT_PRIO_MOD_NIMBLE);
 #endif
 #if IS_USED(MODULE_AUTO_INIT_LORAMAC)
 extern void auto_init_loramac(void);
