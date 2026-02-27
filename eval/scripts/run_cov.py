#!/usr/bin/env python3

import os
import sys
import subprocess

LLVM_VERSION = 18
LLVM_PROFDATA = f"llvm-profdata-{LLVM_VERSION}"
LLVM_COV = f"llvm-cov-{LLVM_VERSION}"

COV_TIMEOUT = 3

if __name__ == "__main__":
    try:
        binary = os.path.join(os.getcwd(), [f for f in os.listdir() if f.endswith("-cov")][0])
    except IndexError:
        print("No -cov binary found")
        sys.exit(1)
    
    for file in os.listdir("./corpus"):
        subp = subprocess.Popen(
            [binary, os.path.join(os.getcwd(), "corpus", file)],
            env={"LLVM_PROFILE_FILE": os.path.join("./coverage", f"cov-{file}.profraw")},
            stdout=sys.stdout, stderr=sys.stderr
        )
        try:
            return_code = subp.wait(timeout=COV_TIMEOUT)
            print(f"Cov: {file}, Return code: {subp.wait()}")
        except subprocess.TimeoutExpired:
            print(f"Cov: {file}, Timeout")
            subp.kill()
    
    os.system(f"{LLVM_PROFDATA} merge -sparse ./coverage -o ./coverage/cov.profdata")
    # os.system("rm ./coverage/cov-*.profraw")
    llvm_cov_command = [
        LLVM_COV, "show",
        "-format=html",
        "-output-dir=./report",
        "-instr-profile=./coverage/cov.profdata",
        "-compilation-dir=.",
        binary
    ]
    os.system(' '.join(llvm_cov_command))
