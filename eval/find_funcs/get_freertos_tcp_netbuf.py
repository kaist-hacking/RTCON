#!/usr/bin/env python3

import os
import re
import sys

from func_util import get_function_line

pattern = r"\b(?:[a-zA-Z_]\w*)\s+[a-zA-Z_]\w*\s*\([^)]*\)[\s\n]*\{"
param_pattern = r"\([^)]*\)"

def get_netbuf(directory):
    prototypes = set()
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                if "Test" in filepath:
                    continue
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                    matches = re.findall(pattern, content, re.DOTALL)
                    for match in matches:
                        if "NetworkBufferDescriptor_t*" not in match.replace(" ", ""):
                            continue
                        if "NetworkBufferDescriptor_t**" in match.replace(" ", ""):
                            continue
                        match = match[:-1]  # Remove the last {
                        match.strip()

                        params = re.search(param_pattern, match).group()
                        params_index = match.index(params)
                        function_name = match[:params_index].strip()
                        function_name = function_name.split(" ")[-1]

                        if "\n" in function_name:
                            continue

                        function_line = 20
                        try:
                            function_line = get_function_line(filepath, function_name)
                        except:
                            pass

                        if function_line < 20:
                            continue

                        for i, param in enumerate(params.split(",")):
                            if "NetworkBufferDescriptor_t*" in param.replace(" ", ""):
                                relpath = os.path.relpath(filepath, directory)
                                prototypes.add(f"{relpath},{function_name},{i},-1,1")

    return prototypes

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <directory>")
        sys.exit(1)

    protos = get_netbuf(sys.argv[1])
    protos = sorted(protos)
    for proto in protos:
        print(proto)
