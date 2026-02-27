#!/bin/sh

# Little hack to build the Zephyr project
patch -p1 -d $ZEPHYR_ROOT/zephyr < patch/main.c.pat
patch -p1 -d $ZEPHYR_ROOT/zephyr < patch/l2cap_br.c.pat
