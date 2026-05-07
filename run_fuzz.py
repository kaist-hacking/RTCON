#!/usr/bin/env python3

import os
import sys
import time
import re
import shutil
import subprocess
from tqdm import tqdm

killswitch = False

MAX_FILE_SIZE = 10 * 1024 * 1024  # 10MB

MAX_DEDUP_CRASH_STACK = 3

def get_crash_stack(report, index):
    start = f"#{index} "
    target_line_start = report.find(start)
    if target_line_start == -1:
        return None
    
    target_line_end = report.find("\n", target_line_start)
    if target_line_end == -1:
        return None

    target_line = report[target_line_start:target_line_end]

    # Remove replicate info
    target_line_token = target_line.split(" ")
    target_address = target_line_token[1]
    # target_line_token[2] is "in"
    target_func = target_line_token[3]
    target_loc = target_line_token[4]
    
    if target_func.startswith("replicate_"):
        target_func = target_func.replace("replicate_", "")
        target_func = target_func[:target_func.rfind("_")]
    
    # Replace build_xxx to build
    target_loc = re.sub(r'build_[^/]+', 'build', target_loc)

    return (target_func, target_loc)


def compare_crash_stack(stack1, stack2):
    if stack1 is None or stack2 is None:
        return False

    if len(stack1) != len(stack2):
        return False

    if len(stack1) < MAX_DEDUP_CRASH_STACK:
        return False

    for i in range(MAX_DEDUP_CRASH_STACK):
        if stack1[i] != stack2[i]:
            return False

    return True


def dedup_crashes(crash_dict, crash_dir):
    # early return
    if not os.path.exists(crash_dir):
        return
    if len(crash_dict) == len(os.listdir(crash_dir)):
        return

    # check high crashes
    high_crash = os.path.join(crash_dir, "high")
    if os.path.exists(high_crash):
        for func in os.listdir(high_crash):
            func_crash_dir = os.path.join(high_crash, func)
            for crash in os.listdir(func_crash_dir):
                indiv_crash = os.path.join(func_crash_dir, crash)
                if indiv_crash in crash_dict:
                    continue
                report = os.path.join(indiv_crash, "report.txt")

                # Unknown error
                if not os.path.exists(report):
                    shutil.rmtree(indiv_crash)
                    continue
                
                report_data = ""
                with open(report, "r") as f:
                    report_data = f.read()
                
                crash_stacks = []
                # We will use top 3 crash stacks
                for i in range(MAX_DEDUP_CRASH_STACK):
                    crash_stacks.append(get_crash_stack(report_data, i))

                is_duplicated = False
                for _, stack in crash_dict.items():
                    if compare_crash_stack(crash_stacks, stack):
                        shutil.rmtree(indiv_crash)
                        is_duplicated = True
                        break
                
                if not is_duplicated:
                    crash_dict[indiv_crash] = crash_stacks


def remove_asan_results():
    for root, _, files in os.walk(os.getcwd()):
        for file in files:
            if file.startswith("crash-"):
                os.remove(os.path.join(root, file))


def merge_total_coverage():
    coverage_dir = os.path.join(os.getcwd(), "coverage_dir")
    if not os.path.exists(coverage_dir):
        return

    prof_id_file_list = {}
    for lev_coverage in os.listdir(coverage_dir):
        if not os.path.isdir(os.path.join(coverage_dir, lev_coverage)):
            continue
        for cov_profraw in os.listdir(os.path.join(coverage_dir, lev_coverage)):
            if not cov_profraw.endswith(".profraw"):
                continue
            prof_id = cov_profraw[:cov_profraw.rfind(".profraw")]
            if prof_id not in prof_id_file_list:
                prof_id_file_list[prof_id] = []
            prof_id_file_list[prof_id].append(os.path.join(coverage_dir, lev_coverage, cov_profraw))
    
    if len(prof_id_file_list) == 0:
        return

    if not os.path.exists(os.path.join(coverage_dir, "total")):
        os.makedirs(os.path.join(coverage_dir, "total"))

    pbar = tqdm(total=len(prof_id_file_list), desc="Merging coverage", bar_format="{l_bar}{bar}")
    for prof_id, prof_files in prof_id_file_list.items():
        total_profraw = os.path.join(coverage_dir, "total", f"{prof_id}.profdata")
        if os.path.exists(total_profraw):
            os.remove(total_profraw)
        subprocess.run(["llvm-profdata-18", "merge", "-o", total_profraw] + prof_files)

        # Get any binary file
        binary = ""
        for binary in os.listdir(os.path.join(os.getcwd(), "host_bin")):
            if binary.endswith("-fuzz"):
                binary = os.path.join(os.getcwd(), "host_bin", binary)
                break

        with open(os.path.join(coverage_dir, "total", f"{prof_id}.report"), "w") as f:
            subprocess.run(["llvm-cov-18", "report", "-instr-profile", total_profraw, binary], stdout=f)
        pbar.update(1)
    pbar.close()


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <func_name> <time in seconds>")
        sys.exit(1)

    func_name = sys.argv[1]
    run_time = int(sys.argv[2])

    fuzz_binary = None
    for binary in os.listdir("host_bin"):
        if func_name in binary:
            fuzz_binary = os.path.join("host_bin", binary)
            break

    if fuzz_binary is None:
        print("Binary not found")
        sys.exit(1)

    if not os.path.exists(fuzz_binary):
        print("Binary not found")
        sys.exit(1)

    binary_info = os.path.basename(fuzz_binary).split("-")
    if len(binary_info) < 2:
        print("Invalid binary name")
        sys.exit(1)

    func = binary_info[0]

    if not os.path.exists("./crash_dir"):
        os.makedirs("./crash_dir")

    start_time = time.time()

    crash_dict = {}
    crash_dir = os.path.join(os.getcwd(), "crash_dir", f"crash_{func}")

    if not os.path.exists("./corpus"):
        os.makedirs("./corpus")

    if not os.path.exists(f"./corpus/corpus_{func}"):
        os.makedirs(f"./corpus/corpus_{func}")

    if not os.path.exists("./fuzz_stdout"):
        os.makedirs("./fuzz_stdout")
    if not os.path.exists("./fuzz_stderr"):
        os.makedirs("./fuzz_stderr")

    binary = os.path.join(os.getcwd(), fuzz_binary)
    fuzz_stdout = os.path.join(os.getcwd(), "fuzz_stdout", f"fuzz_stdout_{func}")
    fuzz_stderr = os.path.join(os.getcwd(), "fuzz_stderr", f"fuzz_stderr_{func}")

    pbar = tqdm(total=run_time, desc=f"Running {fuzz_binary}", bar_format="{l_bar}{bar} [ left: {remaining}s ]")
    with open(fuzz_stdout, "wb") as out_file, open(fuzz_stderr, "wb") as err_file:
        subp = subprocess.Popen(
            [binary, f"./corpus/corpus_{func}"],
            # stdout=out_file,
            stdout=out_file,
            stderr=err_file,
            bufsize=0,
        )

        retcode = None
        for _ in range(int((run_time - (time.time() - start_time)) / 20)):
            retcode = subp.poll()
            if retcode is not None:
                break

            time.sleep(20)
            pbar.update(int(time.time() - start_time) - pbar.n)

        if retcode is None:
            subp.kill()

    time.sleep(1)
    dedup_crashes(crash_dict, crash_dir)
    remove_asan_results()
    while not killswitch and time.time() - start_time < run_time:
        # TODO: Optimize this part
        if os.path.getsize(fuzz_stderr) > MAX_FILE_SIZE:
            with open(fuzz_stderr, "rb+") as f:
                f.truncate(MAX_FILE_SIZE)
        if os.path.getsize(fuzz_stdout) > MAX_FILE_SIZE:
            with open(fuzz_stdout, "rb+") as f:
                f.truncate(MAX_FILE_SIZE)

        with open(fuzz_stdout, "ab+") as out_file, open(fuzz_stderr, "ab+") as err_file:
            subp = subprocess.Popen(
                [binary, f"./corpus/corpus_{func}"],
                # stdout=out_file,
                stdout=out_file,
                stderr=err_file,
                bufsize=0,
            )

            retcode = None
            for _ in range(int((run_time - (time.time() - start_time)) / 20)):
                retcode = subp.poll()
                if retcode is not None:
                    break
                time.sleep(20)
                pbar.update(int(time.time() - start_time) - pbar.n)

            if retcode is None:
                # Program is still running but time is up
                subp.kill()

        time.sleep(1)
        dedup_crashes(crash_dict, crash_dir)
        remove_asan_results()
    
    pbar.update(run_time - pbar.n)
    pbar.close()

    merge_total_coverage()
