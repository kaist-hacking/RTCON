diff --git a/pkg/nimble/rpble/nimble_rpble.c b/pkg/nimble/rpble/nimble_rpble.c
index da56a56e6e..672f4e5fdd 100644
--- a/pkg/nimble/rpble/nimble_rpble.c
+++ b/pkg/nimble/rpble/nimble_rpble.c
@@ -110,7 +110,7 @@ static void _children_accept(void)
     assert(res == 0);
 }
 
-static void _on_scan_evt(uint8_t type, const ble_addr_t *addr,
+static void _on_scan_evt_rpble(uint8_t type, const ble_addr_t *addr,
                          const nimble_scanner_info_t *info,
                          const uint8_t *ad, size_t ad_len)
 {
@@ -339,7 +339,7 @@ int nimble_rpble_param_update(const nimble_rpble_cfg_t *cfg)
 #else
     scan_params.flags |= NIMBLE_SCANNER_PHY_1M;
 #endif
-    nimble_scanner_init(&scan_params, _on_scan_evt);
+    nimble_scanner_init(&scan_params, _on_scan_evt_rpble);
 
     /* start to look for parents */
     if (_current_parent == PARENT_NONE) {
