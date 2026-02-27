#!/usr/bin/env python3
import os
import sys
import json
import time
import signal
import subprocess

BC_FILE_DIR = "./bc_files"


def build_whole_project_bc(compile_command_json):
    with open(compile_command_json, "r") as f:
        compile_commands = json.load(f)
        for compile_command in compile_commands:
            if compile_command["file"].endswith(".c"):
                directory = compile_command["directory"]
                actual_command = " ".join(compile_command["arguments"])
                if " -o " not in actual_command:
                    continue
                actual_obj = actual_command[actual_command.index(" -o ") + 4 :]
                actual_obj = actual_obj[: actual_obj.index(" ")]

                if not os.path.exists(BC_FILE_DIR):
                    os.makedirs(BC_FILE_DIR)

                bc_file = os.path.join(
                    BC_FILE_DIR, os.path.basename(actual_obj.replace("/", "_")) + ".bc"
                )
                bc_file = os.path.abspath(bc_file)
                if os.path.exists(bc_file):
                    continue

                new_command = actual_command.replace(actual_obj, bc_file)
                new_command = new_command.replace(" -o ", " -c -emit-llvm -o ")
                new_command += " 2>/dev/null"

                print(f"[+] Building {bc_file}")
                subprocess.run(new_command, shell=True, cwd=directory)

    if os.path.exists(BC_FILE_DIR):
        merge_command = f"llvm-link {BC_FILE_DIR}/*.bc -o whole_project.bc"
        os.system(merge_command)
        print(f"[+] Building whole_project.bc")


def build_whole_project_ll():
    command = f"llvm-dis-18 whole_project.bc -o whole_project.ll"
    os.system(command)
    print("[+] Building whole_project.ll")


def build_mta_tcg():
    print("[+] Start building TCG")
    command = f"mta whole_project.bc"
    p = subprocess.Popen(command, shell=True)

    # Check tcg.dot file generated
    while True:
        if os.path.exists("tcg.dot"):
            break
        if os.path.exists("ptacg.dot"):
            os.rename("ptacg.dot", "tcg.dot")
            break
        time.sleep(1)

    # kill the process
    time.sleep(2)
    os.killpg(os.getpgid(p.pid), signal.SIGTERM)
    print("[+] Building TCG finished")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <compile_command.json>")
        sys.exit(1)
    build_whole_project_bc(sys.argv[1])
    build_whole_project_ll()
    build_mta_tcg()
