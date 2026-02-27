#!/usr/bin/env python3

import os
import re
import sys

from func_util import get_function_line

pattern = r"\b(?:[a-zA-Z_]\w*)\s+[a-zA-Z_]\w*\s*\([^)]*\)[\s\n]*\{"
param_pattern = r"\([^)]*\)"


def get_data_len_pair(directory):
    prototypes = set()
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                if "nimble" not in filepath:
                    continue
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                    matches = re.findall(pattern, content, re.DOTALL)
                    for match in matches:
                        if "*data" not in match:
                            continue
                        if "int len" not in match:
                            continue
                        match = match[:-1]  # Remove the last {
                        match.strip()

                        params = re.search(param_pattern, match).group()
                        params_index = match.index(params)
                        function_name = match[:params_index].strip()
                        function_name = function_name.split(" ")[-1]

                        if "\n" in function_name:
                            function_name = function_name.split("\n")[-1]

                        # function_line = 50
                        # try:
                        #     function_line = get_function_line(filepath, function_name)
                        # except:
                        #     pass

                        # if function_line < 50:
                        #     continue

                        data_index = -1
                        len_index = -1
                        for i, param in enumerate(params.split(",")):
                            if "*data" in param:
                                data_index = i
                            if "int len" in param:
                                len_index = i
                        if data_index != -1 and len_index != -1:
                            relpath = os.path.relpath(filepath, directory)
                            prototypes.add(
                                f"{relpath},{function_name},{data_index},{len_index},0"
                            )

    return prototypes


def get_os_mbuf(directory):
    prototypes = set()
    for root, _, files in os.walk(directory):
        for file in files:
            if file.endswith(".c"):
                filepath = os.path.join(root, file)
                if "nimble" not in filepath:
                    continue
                with open(filepath, "r", encoding="utf-8", errors="ignore") as f:
                    content = f.read()
                    matches = re.findall(pattern, content, re.DOTALL)
                    for match in matches:
                        if "os_mbuf *" not in match:
                            continue
                        if "os_mbuf **" in match:
                            continue
                        match = match[:-1]  # Remove the last {
                        match.strip()

                        params = re.search(param_pattern, match).group()
                        params_index = match.index(params)
                        function_name = match[:params_index].strip()
                        function_name = function_name.split(" ")[-1]

                        if "\n" in function_name:
                            function_name = function_name.split("\n")[-1]

                        # function_line = 50
                        # try:
                        #     function_line = get_function_line(filepath, function_name)
                        # except:
                        #     pass

                        # if function_line < 50:
                        #     continue

                        for i, param in enumerate(params.split(",")):
                            if "os_mbuf *" in param:
                                relpath = os.path.relpath(filepath, directory)
                                prototypes.add(
                                    f"{relpath},{function_name},{i},-1,2"
                                )

    return prototypes


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print(f"Usage: {sys.argv[0]} <directory>")
        sys.exit(1)

    protos = get_data_len_pair(sys.argv[1])
    protos = protos.union(get_os_mbuf(sys.argv[1]))
    for proto in protos:
        print(proto)
