#!/usr/bin/env python3

import os
import sys
import re

from func_util import get_function_line

RED   = "\033[1;31m"  
BLUE  = "\033[1;34m"
CYAN  = "\033[1;36m"
GREEN = "\033[0;32m"
RESET = "\033[0;0m"
BOLD    = "\033[;1m"
REVERSE = "\033[;7m"

pattern = r"\b(?:[a-zA-Z_]\w*)\s+[a-zA-Z_]\w*\s*\([^)]*\)\s*\{"
param_pattern = r"\([^)]*\)"

def parse_func_list(target):
    with open(os.path.join(os.path.dirname(os.path.dirname(__file__)), "..", "config", f"{target.lower()}-func-list.txt"), "rt") as f:
        content = f.read()

    lines = content.split("\n")
    try:
        lines = lines[lines.index('-'*50) + 1:]
    except ValueError:
        print(f"{target}-func-list.txt does not follow the file format")
        sys.exit(1)
    lines = [line for line in lines if not line.startswith("#")]
    tuples = [tuple(line.strip().replace(' ', '').split(',')) for line in lines if line.strip()]
    return tuples

def get_subsystem_pathname(subsystem):
    if subsystem == "App":
        return "Application-Protocols"
    elif subsystem == "Cell-intf":
        return "FreeRTOS-Cellular-Interface"
    elif subsystem == "Cell-mod":
        return "FreeRTOS-Cellular-Modules"
    elif subsystem == "TCP":
        return "FreeRTOS-Plus-TCP"

def prettify_param(param):
    return param.strip().replace(")", "").replace("(", "").replace("\n", "").replace("  ", "")

def get_func_def(directory, filepath, input_arg_idx, input_arg_len_idx, func_name):
    with open(os.path.join(directory, filepath), "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
        matches = re.findall(pattern, content, re.DOTALL)
        for match in matches:
            if func_name in match:
                match = match[:-1]  # Remove the last {
                match.strip()
                params = re.search(param_pattern, match).group().split(",")
                if input_arg_idx >= len(params):
                    return None, None
                elif input_arg_len_idx != -1 and input_arg_len_idx >= len(params):
                    return None, None
                
                if input_arg_len_idx == -1:
                    return prettify_param(params[input_arg_idx]), None
                else:
                    return prettify_param(params[input_arg_idx]), prettify_param(params[input_arg_len_idx])

if __name__ == "__main__":
    if len(sys.argv) < 3:
        print("Usage: python3 check_freertos.py <directory> <subsystem>")
        sys.exit(1)

    directory = sys.argv[1]
    subsystem = sys.argv[2]

    func_list = parse_func_list(f"freertos-{subsystem}")
    for func in func_list:
        filepath, func_name, input_arg_idx, input_arg_len_idx, _ = func
        input_arg_ty, input_arg_len_ty = get_func_def(
            directory, filepath, int(input_arg_idx), int(input_arg_len_idx), func_name)
        print(f"{RED}{func_name}{RESET}: {BLUE}{input_arg_ty}{RESET} ({input_arg_idx}) / {CYAN}{input_arg_len_ty}{RESET} ({input_arg_len_idx})")
