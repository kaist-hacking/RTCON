#!/usr/bin/env python3

import os
import sys
import re

from func_util import get_function_line

pattern = r"\b(?:[a-zA-Z_]\w*)\s+[a-zA-Z_]\w*\s*\([^)]*\)[\s\n]*\{"
param_pattern = r"\([^)]*\)"

def get_subsystem_pathname(subsystem):
    if subsystem == "App":
        return "Application-Protocols"
    elif subsystem == "Cell-intf":
        return "FreeRTOS-Cellular-Interface"
    elif subsystem == "Cell-mod":
        return "FreeRTOS-Cellular-Modules"
    elif subsystem == "TCP":
        return "FreeRTOS-Plus-TCP"

def get_func_defs(directory, subsystem):
    func_defs = []
    subsystem_pathname = get_subsystem_pathname(subsystem)
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                if subsystem_pathname not in filepath:
                    continue
                if "/source/" not in filepath:
                    continue
                if "/3rdparty/" in filepath:
                    continue
                if "/test/" in filepath:
                    continue
                if "/portable/" in filepath:
                    continue
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                    matches = re.findall(pattern, content, re.DOTALL)
                    for match in matches:
                        # manual filters
                        if "else if" in match:
                            continue
                        if "for(" in match:
                            continue
                        if "for (" in match:
                            continue
                        if "( void )" in match:
                            continue
                        match = match[:-1]  # Remove the last {
                        match.strip()
                        relpath = os.path.relpath(filepath, directory)
                        func_defs.append((relpath, match.replace(" "*4, "").replace("\n", "")))

    return func_defs

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: get_freertos_raw.py <directory> <subsystem>")
        print("Subsystem: App, Cell-intf, Cell-mod, TCP")
        sys.exit(1)

    directory = sys.argv[1]
    subsystem = sys.argv[2]
    func_defs = get_func_defs(directory, subsystem)
    
    for func_def in func_defs:
        print(f"{func_def[0]}: {func_def[1]}")
    # with open(f"freertos_prompt.txt", "r") as f:
    #     prompt = f.read()
    
<<<<<<< Updated upstream
    # file_split_no = 0
    # for i in range(0, len(func_defs), 100):
    #     with open(f"freertos_{subsystem}_prompt{file_split_no}.txt", "w") as f:
    #         f.write(prompt)
    #         for j in range(i, min(i+100, len(func_defs))):
    #             f.write(f"{func_defs[j][0]}: {func_defs[j][1]}\n")
    #     file_split_no += 1
=======
    file_split_no = 0
    for i in range(0, len(func_defs), 50):
        with open(f"freertos_{subsystem}_prompt{file_split_no}.txt", "w") as f:
            f.write(prompt)
            for j in range(i, min(i+50, len(func_defs))):
                f.write(f"{func_defs[j][0]}: {func_defs[j][1]}\n")
        file_split_no += 1
>>>>>>> Stashed changes
