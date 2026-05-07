#!/usr/bin/env python3
import os
import re
import sys
import json
import shutil
import subprocess


def try_build_project(project):
    cwd = os.getcwd()
    prj_dir = os.path.join(cwd, project)
    if not os.path.exists(prj_dir):
        print(f"Project {project} does not exist")
        return

    pre_processing_project(project)

    # Pre-build
    if project == "libtiff":
        cmake_command = "cmake -B native_build -S . -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug"
        p = subprocess.run(cmake_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run cmake for {project}")
            return
    elif project == "tcpreplay":
        autogen_command = "./autogen.sh"
        p = subprocess.run(autogen_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run autogen for {project}")
            return
        configure_command = "CC=clang-16 CXX=clang++-16 ./configure --disable-local-libopts"
        p = subprocess.run(configure_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run configure for {project}")
            return
    elif project == "libming":
        autogen_command = "./autogen.sh"
        p = subprocess.run(autogen_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run autogen for {project}")
            return
        configure_command = "CC=clang-16 CXX=clang++-16 CFLAGS='-fcommon -g -O0' ./configure --enable-static --disable-shared --disable-freetype"
        p = subprocess.run(configure_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run configure for {project}")
            return

    # Build
    if project == "libtiff":
        build_command = "bear -- cmake --build native_build"
        p = subprocess.run(build_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run bear for {project}")
            return
    elif project == "ffjpeg":
        build_command = "bear -- make"
        p = subprocess.run(build_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run bear for {project}")
            return
    elif project == "tcpreplay":
        build_command = "bear -- make"
        p = subprocess.run(build_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run bear for {project}")
            return
    elif project == "libming":
        build_command = "bear -- make"
        p = subprocess.run(build_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run bear for {project}")
            return
    elif project == "ngiflib":
        build_command = "CC=clang-16 CXX=clang++-16 CFLAGS='-fcommon -g -O0' bear -- make"
        p = subprocess.run(build_command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run bear for {project}")
            return

    post_processing_project(project)

def pre_processing_project(project):
    cwd = os.getcwd()
    prj_dir = os.path.join(cwd, project)
    if project == "libtiff":
        command = "sed -i -E '/add_subdirectory\((tools|test|contrib)\)/s/^/#/' CMakeLists.txt"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

def post_processing_project(project):
    cwd = os.getcwd()
    prj_dir = os.path.join(cwd, project)
    
    if project == "libtiff":
        processed_compile_commands = []
        with open("libtiff/compile_commands.json", "r") as f:
            compile_commands = json.load(f)
        for command in compile_commands:
            if command["directory"].endswith("tools"):
                continue
            if command["directory"].endswith("test"):
                continue
            if "contrib/" in command["directory"]:
                continue
            if command["file"].endswith("dummy.c"):
                continue
            processed_compile_commands.append(command)
        with open("libtiff/compile_commands.json", "w") as f:
            json.dump(processed_compile_commands, f)

        # Change the CMakeLists.txt
        with open("libtiff/CMakeLists.txt", "a+") as f:
            f.write("add_subdirectory(skel)\n")

        # Remove executable target
        command = "sed -i '/^add_executable(mkg3states /d' libtiff/CMakeLists.txt"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return
        command = "sed -i '/^target_sources(mkg3states /d' libtiff/CMakeLists.txt"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return
        command = "sed -i '/^target_link_libraries(mkg3states /d' libtiff/CMakeLists.txt"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return
        command = "sed -i '/^add_custom_target(faxtable/,/WORKING_DIRECTORY.*)/d' libtiff/CMakeLists.txt"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return
    elif project == "ffjpeg":
        processed_compile_commands = []
        with open("ffjpeg/compile_commands.json", "r") as f:
            compile_commands = json.load(f)
        for command in compile_commands:
            if command["file"].endswith("ffjpeg.c"):
                continue
            processed_compile_commands.append(command)
        with open("ffjpeg/compile_commands.json", "w") as f:
            json.dump(processed_compile_commands, f)

        command = "cp /source/Makefiles/ffjpeg_Makefile src/Makefile"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run cp for {project}")
            return
    elif project == "tcpreplay":
        ban_list = [
            "tcpcapinfo-",
            "tcpliveplay-",
            "tcpreplay-",
            "tcpreplay_edit-",
            "tcprewrite-",
            "tcpbridge-",
        ]
        processed_compile_commands = []
        with open("tcpreplay/compile_commands.json", "r") as f:
            compile_commands = json.load(f)
        for command in compile_commands:
            add = True
            for ban_file in ban_list:
                if ban_file in command["output"]:
                    add = False
                    break
            if add:
                processed_compile_commands.append(command)
        with open("tcpreplay/compile_commands.json", "w") as f:
            json.dump(processed_compile_commands, f)

        # Clean
        command = "make clean"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run make clean for {project}")
            return

        # Remove main
        command = "sed -i '78,197d' src/tcpprep.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return
        command = "sed -i '63,172d' src/tcprewrite.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

        # Remove ASAN at strlcpy
        command = "sed -i '26a\
__attribute__((no_sanitize(\"coverage\"), no_sanitize(\"address\"))) ' lib/strlcpy.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

        # Bugs detecting NULL pointer dereference
        # CVE-2023-27784
        # CVE-2023-27785
        # CVE-2023-27786
        # CVE-2023-27787
        # Do not remove default SIGSEGV handler
        # command = "sed -i '29d' src/crash.h"
        # p = subprocess.run(command, shell=True, cwd=prj_dir)
        # if p.returncode != 0:
        #     print(f"Failed to run sed for {project}")
        #     return

        command = "cp /source/Makefiles/tcpreplay_Makefile src/Makefile"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run cp for {project}")
            return
    elif project == "libming":
        processed_compile_commands = []
        with open("libming/compile_commands.json", "r") as f:
            compile_commands = json.load(f)
        for command in compile_commands:
            if "util/" in command["file"] and "makeswf_util" not in command["file"]:
                continue
            processed_compile_commands.append(command)
        with open("libming/compile_commands.json", "w") as f:
            json.dump(processed_compile_commands, f)

        # Remove main
        command = "sed -i '45,128d' src/actioncompiler/main.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

        # detect memory leak
        command = "sed -i 's/detect_leaks=0/detect_leaks=1/' src/fuzz.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

        command = "cp /source/Makefiles/libming_Makefile src/Makefile"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run cp for {project}")
            return
    elif project == "ngiflib":
        # Remove main
        command = "sed -i '84,195d' main.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

        command = "cp /source/Makefiles/ngiflib_Makefile ./Makefile"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run sed for {project}")
            return

        command = "cp /source/skel/* ./"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run cp for {project}")
            return

        command = "cp /source/harness/ngiflib_main.c ./main.c"
        p = subprocess.run(command, shell=True, cwd=prj_dir)
        if p.returncode != 0:
            print(f"Failed to run cp for {project}")
            return
        
        
if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 create_compile_commands.py <project_name>")
        sys.exit(1)

    try_build_project(sys.argv[1])
