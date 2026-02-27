import os
import sys


def parse_func_list(target):
    if target.lower().startswith("fuzzslice"):
        func_file_name = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            "config",
            "fuzzslice",
            f"{target.lower()}-func-list.txt",
        )
    elif target.lower().startswith("manual"):
        func_file_name = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            "config",
            "manual",
            f"{target.lower()}-func-list.txt",
        )
    elif target.lower().startswith("ablation"):
        func_file_name = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            "config",
            "ablation",
            f"{target.lower()}-func-list.txt",
        )
    elif target.lower().startswith("fuzzgen"):
        func_file_name = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            "config",
            "fuzzgen",
            f"{target.lower()}-fuzzer-list.txt",
        )
    else:
        func_file_name = os.path.join(
            os.path.dirname(os.path.dirname(__file__)),
            "config",
            f"{target.lower()}-func-list.txt",
        )

    with open(func_file_name, "rt") as f:
        content = f.read()

    lines = content.split("\n")
    try:
        lines = lines[lines.index("-" * 50) + 1 :]
    except ValueError:
        print(f"{target}-func-list.txt does not follow the file format")
        sys.exit(1)
    lines = [line for line in lines if not line.startswith("#")]
    tuples = [
        tuple(line.strip().replace(" ", "").split(","))
        for line in lines
        if line.strip()
    ]
    return tuples
