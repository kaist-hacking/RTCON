#!/bin/sh

# Little hack to build the RIOT project

if [ -z "$RIOT_ROOT" ]; then
    echo "Please set RIOTBASE environment variable"
    exit 1
fi

patch -p1 -d $RIOT_ROOT < patch/init.c.pat
patch -p1 -d $RIOT_ROOT < patch/irq_cpu.c.pat
patch -p1 -d $RIOT_ROOT < patch/auto_init.c.pat
patch -p1 -d $RIOT_ROOT < patch/makefile.include.pat
patch -p1 -d $RIOT_ROOT < patch/native.inc.mk.pat


# patch -p1 -d $RIOT_ROOT < patch/nimble_rpble.c.pat
# patch -p1 -d $RIOT_ROOT < patch/nimble_autoconn.c.pat
