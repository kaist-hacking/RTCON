#!/usr/bin/env python3
import os
import re
import sys
import json
import shutil
import subprocess


ZEPHYR_BUILD_COMMAND = "ccache -C && bear -- west build -p always -d build_native -b native_posix/native/64 -- -DNATIVE=on -DCONF_FILE=prj.native.conf ."
RIOT_BUILD_COMMAND = "make -f Makefile clean && bear -- make -f Makefile 2>/dev/null"
THREADX_BUILD_COMMAND = "cmake -DNATIVE=on . && bear -- make VERBOSE=1"
FREERTOS_BUILD_COMMAND = "make -f Makefile.native clean && bear -- make -f Makefile.native"


def try_build_project(project):
    cwd = os.getcwd()
    if project == "zephyr":
        build_command = ZEPHYR_BUILD_COMMAND
    elif project == "riot":
        build_command = RIOT_BUILD_COMMAND
        cwd = os.path.join(cwd, "appnative")
    elif project == "threadx":
        build_command = THREADX_BUILD_COMMAND
    elif project == "freertos":
        build_command = FREERTOS_BUILD_COMMAND
    else:
        raise ValueError(f"Unknown project: {project}")

    p = subprocess.run(build_command, shell=True, cwd=cwd)


def post_processing_project(project):
    if project == "zephyr":
        processed_compile_commands = []
        with open("compile_commands.json", "r") as f:
            compile_commands = json.load(f)
            for compile_command in compile_commands:
                if compile_command["file"].endswith("/init.c"):
                    continue
                processed_compile_commands.append(compile_command)
        with open("compile_commands.json", "w") as f:
            json.dump(processed_compile_commands, f, indent=4)
    elif project == "riot":
        processed_compile_commands = []
        native_dir = os.path.join(os.getcwd(), "appnative")
        compile_commands_file = os.path.join(native_dir, "compile_commands.json")
        with open(compile_commands_file, "r") as f:
            compile_commands = json.load(f)
            for compile_command in compile_commands:
                processed_args = []
                for arg in compile_command["arguments"]:
                    # Replace (xxx) to 0
                    arg = re.sub(r"\(.*?\)", "0", arg)
                    processed_args.append(arg)
                compile_command["arguments"] = processed_args

                if compile_command["file"].endswith("/minimal_linkable.c"):
                    continue
                processed_compile_commands.append(compile_command)
        with open(compile_commands_file, "w") as f:
            json.dump(processed_compile_commands, f, indent=4)
        shutil.copy(compile_commands_file, "compile_commands.json")


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <project>")
        sys.exit(1)

    try_build_project(sys.argv[1])
    post_processing_project(sys.argv[1])
