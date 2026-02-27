#!/usr/bin/env python3

import os
import re
import sys

from func_util import get_function_line

static_pattern = r"\bstatic\s+(?:[a-zA-Z_]\w*)\s+[a-zA-Z_]\w*\s*\([^)]*\)[\s\n]*\{"
pattern = r"\b(?:[a-zA-Z_]\w*)\s+[a-zA-Z_]\w*\s*\([^)]*\)[\s\n]*\{"
param_pattern = r"\([^)]*\)"

def get_net_buf(directory):
    prototypes = set()
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                if "subsys" not in filepath:
                    continue
                if "samples" in filepath:
                    continue
                if "tests" in filepath:
                    continue
                with open(filepath, "r") as f:
                    content = f.read()
                    matches = re.findall(pattern, content, re.DOTALL)
                    static_matches = re.findall(static_pattern, content, re.DOTALL)
                    for match in matches:
                        if "struct net_buf*" not in match.replace(" ", ""):
                            continue
                        if "struct net_buf**" in match.replace(" ", ""):
                            continue
                        match = match[:-1]  # Remove the last {
                        match.strip()

                        params = re.search(param_pattern, match).group()
                        params_index = match.index(params)
                        function_name = match[:params_index].strip()
                        function_name = function_name.split(" ")[-1]

                        if "\n" in function_name:
                            continue

                        if function_name.startswith("net_buf"):
                            continue

                        for i, param in enumerate(params.split(",")):
                            if "struct net_buf*" in param.replace(" ", ""):
                                relpath = os.path.relpath(filepath, directory)
                                prototypes.add(f"{relpath},{function_name},{i},-1,1")

    return prototypes

def get_simple_net_buf(directory):
    prototypes = set()
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                if "subsys" not in filepath:
                    continue
                if "samples" in filepath:
                    continue
                if "tests" in filepath:
                    continue
                with open(filepath, "r") as f:
                    content = f.read()
                    matches = re.findall(pattern, content, re.DOTALL)
                    static_matches = re.findall(static_pattern, content, re.DOTALL)
                    for match in matches:
                        if "structnet_buf_simple*" not in match.replace(" ", ""):
                            continue
                        if "structnet_buf_simple**" in match.replace(" ", ""):
                            continue
                        match = match[:-1]  # Remove the last {
                        match.strip()

                        params = re.search(param_pattern, match).group()
                        params_index = match.index(params)
                        function_name = match[:params_index].strip()
                        function_name = function_name.split(" ")[-1]

                        if "\n" in function_name:
                            continue

                        if function_name.startswith("net_buf"):
                            continue

                        for i, param in enumerate(params.split(",")):
                            if "structnet_buf_simple*" in param.replace(" ", ""):
                                relpath = os.path.relpath(filepath, directory)
                                prototypes.add(f"{relpath},{function_name},{i},-1,2")

    return prototypes

if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <directory>")
        sys.exit(1)

    protos = get_net_buf(sys.argv[1])
    protos = protos.union(get_simple_net_buf(sys.argv[1]))
    for proto in protos:
        print(proto)
