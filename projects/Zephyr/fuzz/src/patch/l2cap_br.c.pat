diff --git a/subsys/bluetooth/host/classic/l2cap_br.c b/subsys/bluetooth/host/classic/l2cap_br.c
index 2174aa1c..c1c326e7 100644
--- a/subsys/bluetooth/host/classic/l2cap_br.c
+++ b/subsys/bluetooth/host/classic/l2cap_br.c
@@ -860,10 +860,12 @@ static void l2cap_br_conn_req(struct bt_l2cap_br *l2cap, uint8_t ident,
 	 * channel. If no free channels available for PSM server reply with
 	 * proper result and quit since chan pointer is uninitialized then.
 	 */
+	/*
 	if (server->accept(conn, server, &chan) < 0) {
 		result = BT_L2CAP_BR_ERR_NO_RESOURCES;
 		goto no_chan;
 	}
+	*/
 
 	br_chan = BR_CHAN(chan);
 	br_chan->required_sec_level = server->sec_level;
