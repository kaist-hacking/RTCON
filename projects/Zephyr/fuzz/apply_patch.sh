#!/bin/bash

patch -p1 -d $ZEPHYR_ROOT/zephyr < patch/main.c.pat