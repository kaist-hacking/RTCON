diff --git a/pkg/nimble/autoconn/nimble_autoconn.c b/pkg/nimble/autoconn/nimble_autoconn.c
index 5598da9d50..81dea6ed79 100644
--- a/pkg/nimble/autoconn/nimble_autoconn.c
+++ b/pkg/nimble/autoconn/nimble_autoconn.c
@@ -130,7 +130,7 @@ static int _filter_uuid(const bluetil_ad_t *ad)
     return 0;
 }
 
-static void _on_scan_evt(uint8_t type, const ble_addr_t *addr,
+static void _on_scan_evt_autoconn(uint8_t type, const ble_addr_t *addr,
                          const nimble_scanner_info_t *info,
                          const uint8_t *ad_buf, size_t ad_len)
 {
@@ -358,7 +358,7 @@ int nimble_autoconn_update(const nimble_autoconn_params_t *params,
     }
 
     /* initialize scanner with default parameters */
-    nimble_scanner_init(&scan_params, _on_scan_evt);
+    nimble_scanner_init(&scan_params, _on_scan_evt_autoconn);
 
     /* we also need to apply the new connection parameters to all BLE
      * connections where we are in the MASTER role */
