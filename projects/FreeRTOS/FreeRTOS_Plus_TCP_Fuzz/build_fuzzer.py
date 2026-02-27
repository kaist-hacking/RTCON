#!/usr/bin/env python3

import threading
import subprocess
import os
import sys
import time
import shutil


PRINT_RED = "\033[91m"
PRINT_GREEN = "\033[92m"
PRINT_YELLOW = "\033[93m"
PRINT_RESET = "\033[0m"

FUNC_NAME = ""
BUILD_COMMAND = (
    "rm /tmp/%s_multi_entry.txt; FUNC_NAME=%s make -f Makefile"
)
BUILD_CLEAN_COMMAND = "FUNC_NAME=%s make -f Makefile clean"

FUZZER_CONFIGURATION_DIR = "config"

MAX_BUILD_ITERATIONS = 5

TARGET_FUNC_NAME = ""
TARGET_FUNC_INPUT_INDEX = ""
TARGET_FUNC_LENGTH_INDEX = ""

def get_nprocs():
    # FreeRTOS only supports 1 thread
    return 1


# Semaphore to limit the number of workers
sema = threading.Semaphore(get_nprocs())


TEST_FUNC_LIST = "/configs/freertos-tcp-func-list-reduced.txt"
def get_test_idx(func_name):
    with open(TEST_FUNC_LIST, "r") as f:
        for line in f.readlines():
            if f",{func_name}," in line:
                return line.split(",")[-1].strip()
    return None


def get_total_created_funcs(cff):
    if os.path.exists(cff):
        with open(cff, "r") as f:
            lines = f.readlines()
            return len(lines)
    return 0


def clean(cff, tuf):
    if os.path.exists(cff):
        os.remove(cff)
    if os.path.exists(tuf):
        os.remove(tuf)
    p = subprocess.Popen(
        BUILD_CLEAN_COMMAND % FUNC_NAME,
        shell=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    stdout, stderr = p.communicate()
    p.wait()


def build_single_fuzzer(func_name):
    taint_configuration_file = os.path.join(FUZZER_CONFIGURATION_DIR, f"{func_name}.yaml")
    taint_configuration_file_lock = f"/var/lock/{func_name}_taint_func_file.lock"
    created_funcs_file = f"/tmp/{func_name}_created_func_list.txt"
    created_funcs_lock = f"/var/lock/{func_name}_created_func_list.lock"
    taint_update_func_file = f"/tmp/{func_name}_taint_conf_updated"

    build_dir = os.path.abspath(f"./build_{func_name}")
    crash_dir = os.path.abspath(f"crash_dir/crash_{func_name}")
    coverage_dir = os.path.abspath(f"coverage_dir/coverage_{func_name}")

    test_idx = get_test_idx(func_name)
    if test_idx is None:
        print(f"{PRINT_RED}[!] Test index not found for {func_name}{PRINT_RESET}")
        return

    global FUNC_NAME
    FUNC_NAME = func_name

    fuzzer_env = os.environ.copy()
    fuzzer_env["TAINT_CONFIGURATION_FILE"] = os.path.abspath(taint_configuration_file)
    fuzzer_env["TAINT_CONFIGURATION_FILE_LOCK"] = taint_configuration_file_lock
    fuzzer_env["CREATE_FUNC_FILE"] = created_funcs_file
    fuzzer_env["CREATE_FUNC_FILE_LOCK"] = created_funcs_lock
    fuzzer_env["TAINT_UPDATE_FUNC_FILE"] = taint_update_func_file

    fuzzer_env["CRASH_DIR_PATH"] = crash_dir
    fuzzer_env["COVERAGE_DIR_PATH"] = coverage_dir
    fuzzer_env["TEST_IDX"] = test_idx

    fuzzer_env["TARGET_FUNC_NAME"] = TARGET_FUNC_NAME
    fuzzer_env["TARGET_FUNC_INPUT_INDEX"] = TARGET_FUNC_INPUT_INDEX
    fuzzer_env["TARGET_FUNC_LENGTH_INDEX"] = TARGET_FUNC_LENGTH_INDEX

    if TARGET_FUNC_NAME.lower() == func_name.lower():
        fuzzer_env["LOW_FUZZ"] = "1"

    # Create crash dir and coverage dir if not exists
    if not os.path.exists("crash_dir"):
        os.makedirs("crash_dir")
    if not os.path.exists(crash_dir):
        os.makedirs(crash_dir)
    if not os.path.exists("coverage_dir"):
        os.makedirs("coverage_dir")
    if not os.path.exists(coverage_dir):
        os.makedirs(coverage_dir)

    clean(created_funcs_file, taint_update_func_file)
    start_time = time.time()
    first_build = True
    build_iteration = 0

    print(f"{PRINT_GREEN}[+] Start building target {func_name}...{PRINT_RESET}")
    while True:
        build_iteration += 1
        try:
            p = subprocess.Popen(
                BUILD_COMMAND % (func_name, func_name),
                env=fuzzer_env,
                shell=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
            )
            stdout, stderr = p.communicate()
            p.wait()

            if p.returncode != 0:
                for line in stderr.decode("utf-8").split("\n")[-100:]:
                    print(line)
                print(stdout.decode("utf-8")[-500:])
                print(
                    f"{PRINT_RED}[!] Build failed {func_name}{PRINT_RESET}"
                )
                break

            elapsed_time = time.time() - start_time
            total_created_funcs = get_total_created_funcs(created_funcs_file)
            if first_build:
                first_build = False
                print(
                    f"{PRINT_YELLOW}[!] First build {func_name} finished. Total elapsed time: {elapsed_time:.2f} seconds. Total created functions: {total_created_funcs}{PRINT_RESET}"
                )
            else:
                if os.path.exists(taint_update_func_file):
                    print(
                        f"{PRINT_YELLOW}[!] Configuration updated. Rebuilding the target {func_name}. Total elapsed time: {elapsed_time:.2f} seconds. Total created functions: {total_created_funcs}{PRINT_RESET}"
                    )
                    with open(taint_update_func_file, "r") as f:
                        line = f.readline().strip()
                        while line:
                            print(f"func: {line}")
                            line = f.readline().strip()
                else:
                    print(
                        f"{PRINT_GREEN}[+] Target {func_name} built successfully. Total elapsed time: {elapsed_time:.2f} seconds. Total created functions: {total_created_funcs}{PRINT_RESET}"
                    )

                    # It is bind mounted to the host
                    if not os.path.exists("./host_bin"):
                        os.makedirs("./host_bin")
                    shutil.copy(f"{build_dir}/freertos-tcp-fuzz", f"./host_bin/freertos-tcp-{func_name}-fuzz")
                    break

            if build_iteration >= MAX_BUILD_ITERATIONS:
                # It is bind mounted to the host
                if not os.path.exists("./host_bin"):
                    os.makedirs("./host_bin")
                shutil.copy(f"{build_dir}/freertos-tcp-fuzz", f"./host_bin/freertos-tcp-{func_name}-fuzz")
                break

            clean(created_funcs_file, taint_update_func_file)
        except Exception as e:
            print(f"{PRINT_RED}[!] Error {func_name}: {e}{PRINT_RESET}")
            break

    elapsed_time = time.time() - start_time
    print(f"{PRINT_GREEN}[+] Finished building target {func_name}. Total elapsed time: {elapsed_time:.2f} seconds{PRINT_RESET}")

    # Release semaphore
    sema.release()


def build_fuzzers(single=None):
    if not os.path.exists(FUZZER_CONFIGURATION_DIR):
        print(f"{PRINT_RED}[!] No configuration directory found{PRINT_RESET}")
        return
    
    if os.environ.get("TARGET_FUNC_NAME") is None:
        print(f"{PRINT_RED}[!] Target function name not found{PRINT_RESET}")
        return
    
    # Create target information file
    input_index = ""
    length_index = ""
    target_func_name = os.environ.get("TARGET_FUNC_NAME")
    target_config_file = os.path.join(FUZZER_CONFIGURATION_DIR, f"{target_func_name}.yaml")
    if not os.path.exists(target_config_file):
        print(f"{PRINT_RED}[!] Target configuration file not found{PRINT_RESET}")
        return
    with open(target_config_file, "r") as f:
        target_config = f.readlines()
        for line in target_config:
            if "input_index: " in line:
                input_index = line.split(": ")[1].strip()
            if "length_index: " in line:
                length_index = line.split(": ")[1].strip()
    if input_index == "" or length_index == "":
        print(f"{PRINT_RED}[!] Input or length index not found in the target configuration file{PRINT_RESET}")
        return
    with open("target", "w") as f:
        f.write(f"{target_func_name},{input_index},{length_index}")

    global TARGET_FUNC_NAME
    global TARGET_FUNC_INPUT_INDEX
    global TARGET_FUNC_LENGTH_INDEX
    TARGET_FUNC_NAME = target_func_name
    TARGET_FUNC_INPUT_INDEX = input_index
    TARGET_FUNC_LENGTH_INDEX = length_index

    for config in os.listdir(FUZZER_CONFIGURATION_DIR):
        if not config.endswith(".yaml"):
            continue
        if single and single != os.path.basename(config)[: -len(".yaml")]:
            continue

        func_name = os.path.basename(config)[: -len(".yaml")]

        # Acquire semaphore
        sema.acquire()
        threading.Thread(target=build_single_fuzzer, args=(func_name,)).start()


if __name__ == "__main__":
    if len(sys.argv) == 1:
        build_fuzzers()
    elif len(sys.argv) == 2:
        build_fuzzers(sys.argv[1])
    else:
        print(f"Usage: {sys.argv[0]} [function_name(optional)]")
        sys.exit(1)
