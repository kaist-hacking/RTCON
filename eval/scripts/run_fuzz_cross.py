#!/usr/bin/env python3
import os
import re
import sys
import time
import shutil
import signal
import subprocess
import threading
from tqdm import tqdm


killswitch = False

MAX_FILE_SIZE = 10 * 1024 * 1024  # 10MB


def get_max_depth(func_name):
    with open("ancestors", "r") as f:
        line = f.readline()
        while line:
            ancestor_name = line.split(",")[0].strip()
            ancestor_depth = line.split(",")[1].strip()
            if ancestor_name == func_name:
                return int(ancestor_depth)
            line = f.readline()
    return -1


def get_nprocs():
    p = subprocess.Popen(["nproc"], stdout=subprocess.PIPE)
    stdout, _ = p.communicate()
    return int(stdout.decode().strip())


# Semaphore to limit the number of workers
sema = threading.Semaphore(get_nprocs())

running_fuzzers = []

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


# Stack1 is haystack, stack2 is needle
def compare_crash_stack(stack1, stack2):
    if stack1 is None or stack2 is None:
        return False

    if len(stack2) == 0:
        return False

    for i in range(len(stack2)):
        if stack2[i] is None:
            return True

        if stack2[i][0] == "fuzzEntryFunction":
            return True

        if stack2[i][0] not in stack1[i][0]:
            return False
        if stack1[i][1] != stack2[i][1]:
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

                depth_start = report_data.index("depth: ")
                depth_end = report_data.index("\n", depth_start)
                depth = int(report_data[depth_start + len("depth: "):depth_end])

                crash_stacks = []
                # We will use top 3 crash stacks
                for i in range(MAX_DEDUP_CRASH_STACK):
                    crash_stacks.append(get_crash_stack(report_data, i))

                is_duplicated = False
                for _, (crash_depth, stack) in crash_dict.items():
                    if crash_depth != depth:
                        continue
                    if compare_crash_stack(stack, crash_stacks):
                        shutil.rmtree(indiv_crash)
                        is_duplicated = True
                        break
                
                if not is_duplicated:
                    crash_dict[indiv_crash] = (depth, crash_stacks)


ban_func_keywords = [
    # Zephyr (Not include netbuf)
    "k_",
    "z_",
    "sys_",
    "atomic_",
    "arch_",
]
def dedup_cross_crashes():
    crash_dir = os.path.join(os.getcwd(), "crash_dir")
    host_bin_dir = os.path.join(os.getcwd(), "host_bin")
    config_dir = os.path.join(os.getcwd(), "config")

    if not os.path.exists(host_bin_dir) or not os.path.exists(config_dir):
        print("[-] host_bin or config not found")
        return

    # Target file is created at build time
    if not os.path.exists("target"):
        print("[-] target not found")
        return
    with open("target", "r") as f:
        target_func_name = f.readline().strip().split(",")[0]

    cross_fuzz = False
    for binary in os.listdir(host_bin_dir):
        if target_func_name not in binary:
            cross_fuzz = True

    low_level_crash = {}
    high_level_crash = {}
    for func_crash in os.listdir(crash_dir):
        if not os.path.isdir(os.path.join(crash_dir, func_crash)):
            continue

        if target_func_name in func_crash:
            crash = low_level_crash
        else:
            crash = high_level_crash

        func_name = func_crash[len("crash_"):]
        if func_name not in crash:
            crash[func_name] = []

        crash_funcs_dir = os.path.join(crash_dir, func_crash, "high")
        if not os.path.exists(crash_funcs_dir):
            continue

        for crash_func in os.listdir(crash_funcs_dir):
            crash_func_dir = os.path.join(crash_funcs_dir, crash_func)
            for single_crash in os.listdir(crash_func_dir):
                # Here single_crash is the hash value
                address = single_crash
                report = os.path.join(crash_func_dir, single_crash, "report.txt")

                report_data = ""
                with open(report, "r") as f:
                    report_data = f.read()

                # Skip heap-use-after-free
                if "heap-use-after-free" in report_data:
                    continue

                # Summary
                summary_start = report_data.index("SUMMARY: AddressSanitizer:")
                summary_end = report_data.index("\n", summary_start)
                summary = report_data[summary_start:summary_end]
                bug = summary.split(" ")[2]

                # Depth
                depth_start = report_data.index("depth: ")
                depth_end = report_data.index("\n", depth_start)
                depth = int(report_data[depth_start + len("depth: "):depth_end])

                crash_stack = []
                for i in range(MAX_DEDUP_CRASH_STACK):
                    crash_stack.append(get_crash_stack(report_data, i))

                # Check if the crash is from ban function
                crash_from_ban_func = False
                for ban_func_keyword in ban_func_keywords:
                    if crash_stack[0][0].startswith(ban_func_keyword):
                        crash_from_ban_func = True
                        break
                if crash_from_ban_func:
                    continue

                crash[func_name].append((bug, crash_stack, depth))

    assert len(low_level_crash) == 1

    high_confidence_crash = []
    med_confidence_crash = []
    low_confidence_crash = []
    verifier_crash = []

    # There is only one function in low_level_crash
    low_level_crash_stacks = list(low_level_crash.values())[0]
    intersected_high_level_crash = []
    for low_level_crash_bug, low_level_crash_stack, low_level_crash_depth in low_level_crash_stacks:
        crash_intersected = False
        for high_level_func_name, high_level_crash_stacks in high_level_crash.items():
            max_high_level_func_depth = get_max_depth(high_level_func_name)

            for high_level_crash_bug, high_level_crash_stack, high_level_crash_depth in high_level_crash_stacks:
                if compare_crash_stack(high_level_crash_stack, low_level_crash_stack):
                    # normalized_depth
                    norm_depth = high_level_crash_depth / float(max_high_level_func_depth)

                    # Intersected crash
                    if (high_level_crash_depth, high_level_crash_stack) not in intersected_high_level_crash:
                        intersected_high_level_crash.append((high_level_crash_depth, high_level_crash_stack))

                    # Record it as high confidence crash
                    if (low_level_crash_bug, low_level_crash_stack, norm_depth) not in high_confidence_crash:
                        high_confidence_crash.append((low_level_crash_bug, low_level_crash_stack, norm_depth))
                    crash_intersected = True
                else:
                    # Crash is not intersected
                    # Record it as verifier crash
                    if len(verifier_crash) == 0:
                        verifier_crash.append((high_level_crash_bug, high_level_crash_stack))
                    else:
                        duplicated = False
                        for _, c in verifier_crash:
                            if compare_crash_stack(c, high_level_crash_stack):
                                duplicated = True
                                break
                        if not duplicated:
                            verifier_crash.append((high_level_crash_bug, high_level_crash_stack))

        if not crash_intersected:
            low_confidence_crash.append((low_level_crash_bug, low_level_crash_stack, 0.0))
    
    # Remove intersected high level crashes in verifier
    # new_verifier_crash = []
    # for bug, stack in verifier_crash:
    #     if stack not in intersected_high_level_crash:
    #         new_verifier_crash.append((bug, stack))
    # verifier_crash = new_verifier_crash

    # If there is no high level func, it is medium confidence crash
    if len(high_level_crash) == 0:
        med_confidence_crash = low_level_crash_stacks
        low_confidence_crash = []

    # Save the crashes with high confidence
    high_confidence_file = os.path.join(crash_dir, "high_confidence")
    with open(high_confidence_file, "w") as f:
        f.write(f"Total crashes: {len(high_confidence_crash)}\n")
        f.write("=====\n")
        for crash_bug, crash_stack, confidence in high_confidence_crash:
            f.write(f"Bug: {crash_bug}, {confidence}\n")
            for i, (func_name, loc) in enumerate(crash_stack):
                f.write(f"#{i} {func_name} {loc}\n")
            f.write("=====\n")

    med_confidence_file = os.path.join(crash_dir, "med_confidence")
    with open(med_confidence_file, "w") as f:
        f.write(f"Total crashes: {len(med_confidence_crash)}\n")
        f.write("=====\n")
        for crash_bug, crash_stack, confidence in med_confidence_crash:
            f.write(f"Bug: {crash_bug}, {confidence}\n")
            for i, (func_name, loc) in enumerate(crash_stack):
                f.write(f"#{i} {func_name} {loc}\n")
            f.write("=====\n")
    
    low_confidence_file = os.path.join(crash_dir, "low_confidence")
    with open(low_confidence_file, "w") as f:
        f.write(f"Total crashes: {len(low_confidence_crash)}\n")
        f.write("=====\n")
        for crash_bug, crash_stack, confidence in low_confidence_crash:
            f.write(f"Bug: {crash_bug}, {confidence}\n")
            for i, (func_name, loc) in enumerate(crash_stack):
                f.write(f"#{i} {func_name} {loc}\n")
            f.write("=====\n")

    verifier_file = os.path.join(crash_dir, "verifier")
    with open(verifier_file, "w") as f:
        f.write(f"Total crashes: {len(verifier_crash)}\n")
        f.write("=====\n")
        for crash_bug, crash_stack in verifier_crash:
            f.write(f"Bug: {crash_bug}\n")
            for i, (func_name, loc) in enumerate(crash_stack):
                f.write(f"#{i} {func_name} {loc}\n")
            f.write("=====\n")

    print("===================================")
    print(f"High confidence crashes: {len(high_confidence_crash)}")
    print(f"Medium confidence crashes: {len(med_confidence_crash)}")
    print(f"Low confidence crashes: {len(low_confidence_crash)}")
    print(f"Verifier crashes: {len(verifier_crash)}")


def remove_asan_results():
    for root, _, files in os.walk(os.getcwd()):
        for file in files:
            if file.startswith("crash-"):
                os.remove(os.path.join(root, file))


def run_single_fuzzer(fuzz_binary, run_time, handled_fuzzer):
    binary_info = os.path.basename(fuzz_binary).split("-")
    # if len(binary_info) != 3 and len(binary_info) != 4:
    if len(binary_info) < 3:
        print("Invalid binary name")
        sys.exit(1)

    project = binary_info[0]
    if project == "freertos":
        system = binary_info[1]
        func = binary_info[2]
    else:
        func = binary_info[1]

    start_time = time.time()

    if not os.path.exists("./crash_dir"):
        os.makedirs("./crash_dir")

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

    pbar = tqdm(total=run_time, desc=f"Running {fuzz_binary}", bar_format="{l_bar}{bar} [ left: {remaining}s ]", position=handled_fuzzer - 1)
    with open(fuzz_stdout, "wb") as out_file, open(fuzz_stderr, "wb") as err_file:
        subp = subprocess.Popen(
            [binary, f"./corpus/corpus_{func}"],
            # stdout=out_file,
            stdout=subprocess.DEVNULL,
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

        with open(fuzz_stdout, "ab") as out_file, open(fuzz_stderr, "ab") as err_file:
            subp = subprocess.Popen(
                [binary, f"./corpus/corpus_{func}"],
                # stdout=out_file,
                stdout=subprocess.DEVNULL,
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

    running_fuzzers.remove(threading.current_thread())

    # Release semaphore
    sema.release()


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
        subprocess.run(["llvm-profdata-18", "merge", "-sparse", "-o", total_profraw] + prof_files)

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
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <time in seconds>")
        sys.exit(1)

    run_time = int(sys.argv[1])

    if not os.path.exists("host_bin"):
        print("host_bin not found")
        sys.exit(1)

    # Count total number of fuzzers
    total_fuzzer = 0
    for fuzzer in os.listdir("host_bin"):
        if not fuzzer.endswith("-fuzz"):
            continue
        total_fuzzer += 1

    run_time_per_fuzzer = run_time // total_fuzzer

    handled_fuzzer = 0
    for fuzzer in os.listdir("host_bin"):
        if not fuzzer.endswith("-fuzz"):
            continue

        # Acquire semaphore
        sema.acquire()
        handled_fuzzer += 1
        fuzz_binary = os.path.join("host_bin", fuzzer)

        print(f"[{handled_fuzzer}/{total_fuzzer}] Running {fuzz_binary}")
        t = threading.Thread(
            target=run_single_fuzzer,
            args=(
                fuzz_binary,
                run_time_per_fuzzer,
                handled_fuzzer,
            ),
        )
        running_fuzzers.append(t)
        t.start()

        time.sleep(1)

    # Wait for all fuzzers to finish
    while len(running_fuzzers) > 0:
        time.sleep(5)

    dedup_cross_crashes()

    # Merge coverage
    merge_total_coverage()
