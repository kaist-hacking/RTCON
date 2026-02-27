#!/usr/bin/env python3

import os
import sys

func_dict = {
    0: "testRawData",
    1: "testGnrcPktSnip",
    2: "testOsMbuf"
}


def replace_test_func(choice):
    assert choice in func_dict

    source_dir = os.path.dirname(os.path.realpath(__file__))
    main_file = os.path.join(source_dir, "appfuzz", "main.c")

    # replace main file
    if os.path.exists(main_file):
        with open(main_file, "rt") as f:
            content = f.read()
        with open(main_file, "wt") as f:
            f.write(content.replace("testRawData(Data, Size);", f"{func_dict[choice]}(Data, Size);"))


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <choice>")
        sys.exit(1)
    choice = int(sys.argv[1])
    replace_test_func(choice)
