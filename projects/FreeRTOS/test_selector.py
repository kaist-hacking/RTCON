#!/usr/bin/env python3

import os
import sys

func_dict = {
    "Application_Protocols_Fuzz": {
        0: "testRawData",
        1: "testMQTTPacket",
    },
    "FreeRTOS_Plus_Cellular_Interface_Fuzz": {
        0: "testRawData",
        1: "testStringData",
        2: "testCellularATCommandResponse",
    },
    "FreeRTOS_Plus_Cellular_Modules_Fuzz": {
        0: "testRawData",
    },
    "FreeRTOS_Plus_TCP_Fuzz": {
        0: "testRawData",
        1: "testNetworkBuffer",
    },
}

def replace_test_func(app_dir, choice):
    assert app_dir in func_dict
    assert choice in func_dict[app_dir]

    source_dir = os.path.dirname(os.path.realpath(__file__))
    main_file = os.path.join(source_dir, app_dir, "main.c")

    # replace main file
    if os.path.exists(main_file):
        with open(main_file, "rt") as f:
            content = f.read()
        with open(main_file, "wt") as f:
            f.write(content.replace("testRawData(Data, Size);", f"{func_dict[app_dir][choice]}(Data, Size);"))

    
if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <app_dir> <choice>")
        sys.exit(1)
    app_dir = sys.argv[1]
    choice = int(sys.argv[2])
    replace_test_func(app_dir, choice)
