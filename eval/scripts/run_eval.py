#!/usr/bin/env python3

import os
import sys
import subprocess
import multiprocessing
import multiprocessing.pool
import logging

import func_list_manager
import run_cov_web
import config

logging.basicConfig(level=logging.INFO)
root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

lock = multiprocessing.Lock()
cpu_counters = multiprocessing.Array('i', multiprocessing.cpu_count())


def alloc_cpuset(cpus):
    assert cpus <= multiprocessing.cpu_count()
    with lock:
        cpusets = []
        for i in range(config.CPU_OFFSET, multiprocessing.cpu_count()):
            if cpu_counters[i] < config.MAX_ALLOC_PER_CPU:
                cpusets.append(str(i))
                cpu_counters[i] += 1
            if len(cpusets) == cpus:
                break
    return ','.join(cpusets)

def free_cpuset(cpusets):
    with lock:
        for cpuset in cpusets.split(','):
            cpu_counters[int(cpuset)] -= 1

def get_output_dir(target, subsystem, file_path, func_name):
    if subsystem:
        output_dir = os.path.join(root_dir, "out", target, subsystem, 
            file_path.replace(".c", "").replace('/', '.') + '.' + func_name)
    else:
        output_dir = os.path.join(root_dir, "out", target, 
            file_path.replace(".c", "").replace('/', '.') + '.' + func_name)
    return output_dir

def get_container_name(target, subsystem, file_path, func_name):
    if subsystem:
        return f"{target.lower()}-{subsystem.lower()}-{file_path.replace('/', '.')}.{func_name}"
    else:
        return f"{target.lower()}-{file_path.replace('/', '.')}.{func_name}"


def run_fuzz_fuzzgen(target, subsystem, fuzzer_name):
    docker_image_name = f"cgcc-{target.lower()}"
    workdir = config.get_workdir(target, subsystem)
    output_dir = os.path.join(root_dir, "out", target, fuzzer_name)

    # if fuzzer is duplicate, warn and skip
    if os.path.exists(output_dir):
        logging.warning(f"Output directory already exists: {output_dir}")
        return "FuzzGen", fuzzer_name

    os.makedirs(output_dir)


    # Run
    container_name = get_container_name(target, subsystem, fuzzer_name, "")
    cpusets = alloc_cpuset(config.CPU_LIMIT)
    command = [
        "docker", "run", "--rm",
        f"--memory={config.MEM_LIMIT}", f"--cpuset-cpus={cpusets}",
        "--entrypoint=",
        f"--volume={output_dir}/crash_dir:{workdir}/crash_dir",
        f"--volume={output_dir}/corpus:{workdir}/corpus",
        f"--volume={output_dir}/bin:{workdir}/host_bin",
        f"--volume={output_dir}/coverage_dir:{workdir}/coverage_dir",
        f"--volume={output_dir}/report:{workdir}/report",
        f"--volume={output_dir}/config:{workdir}/config",
        f"--volume={output_dir}/fuzz_stdout:{workdir}/fuzz_stdout",
        f"--volume={output_dir}/fuzz_stderr:{workdir}/fuzz_stderr",
        f"--name={container_name}",
        docker_image_name,
    ]

    sh_command = [
        "sh", "-c",
        f"echo 'python3 /scripts/run_fuzz_single.py {fuzzer_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {subsystem} {fuzzer_name}" if subsystem else
        f"echo 'python3 /scripts/run_fuzz_single.py {fuzzer_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {fuzzer_name}"
    ]

    command += sh_command
    
    if subsystem:
        logging.info(f"Running fuzzing {target}-{subsystem}-{fuzzer_name} on cpuset {cpusets}")
    else:
        logging.info(f"Running fuzzing {target}-{fuzzer_name} on cpuset {cpusets}")

    log_stdout = open(os.path.join(output_dir, "stdout"), "wb")
    process = subprocess.run(command, stdout=log_stdout, stderr=subprocess.PIPE)
    log_stdout.close()
    free_cpuset(cpusets)

    return "FuzzGen", fuzzer_name

def run_fuzz(target, subsystem, args, cross_fuzz):
    docker_image_name = f"cgcc-{target.lower()}"
    workdir = config.get_workdir(target, subsystem)

    if len(args) == 1:
        # Fuzzgen
        return run_fuzz_fuzzgen(target, subsystem, args[0])

    file_path = args[0]
    func_name = args[1]
    input_index = args[2]
    length_index = args[3]
    test_index = args[4]

    output_dir = get_output_dir(target, subsystem, file_path, func_name)
    
    # if function is duplicate, warn and skip
    if os.path.exists(output_dir):
        logging.warning(f"Output directory already exists: {output_dir}")
        return file_path, func_name

    os.makedirs(output_dir)

    # Run
    container_name = get_container_name(target, subsystem, file_path, func_name)
    cpusets = alloc_cpuset(config.CPU_LIMIT)
    command = [
        "docker", "run", "--rm",
        f"--memory={config.MEM_LIMIT}", f"--cpuset-cpus={cpusets}",
        "--entrypoint=",
        f"--volume={output_dir}/crash_dir:{workdir}/crash_dir",
        f"--volume={output_dir}/corpus:{workdir}/corpus",
        f"--volume={output_dir}/bin:{workdir}/host_bin",
        f"--volume={output_dir}/coverage_dir:{workdir}/coverage_dir",
        f"--volume={output_dir}/report:{workdir}/report",
        f"--volume={output_dir}/config:{workdir}/config",
        f"--volume={output_dir}/fuzz_stdout:{workdir}/fuzz_stdout",
        f"--volume={output_dir}/fuzz_stderr:{workdir}/fuzz_stderr",
        f"--name={container_name}",
        docker_image_name,
    ]

    if target.lower().startswith("fuzzslice"):
        sh_command = [
            "sh", "-c",
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {subsystem} {file_path} {func_name} {test_index}" if subsystem else
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {file_path} {func_name} {test_index}"
        ]
    elif target.lower().startswith("ablation"):
        sh_command = [
            "sh", "-c",
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {subsystem} {file_path} {func_name} {test_index}" if subsystem else
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {file_path} {func_name} {test_index}"
        ]
    elif target.lower().startswith("manual"):
        sh_command = [
            "sh", "-c",
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {subsystem} {file_path} {func_name} {test_index}" if subsystem else
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {file_path} {func_name} {test_index}"
        ]
    elif cross_fuzz:
        sh_command = [
            "sh", "-c",
            f"echo 'python3 /scripts/run_fuzz_cross.py {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {subsystem} {file_path} {func_name} {test_index} true" if subsystem else
            f"echo 'python3 /scripts/run_fuzz_cross.py {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {file_path} {func_name} {test_index} true"
        ]
    else:
        sh_command = [
            "sh", "-c",
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {subsystem} {file_path} {func_name} {test_index} false" if subsystem else
            f"echo 'python3 /scripts/run_fuzz_single.py {func_name} {config.TIME}; python3 /scripts/run_cov.py' | ./build.sh {file_path} {func_name} {test_index} false"
        ]
    command += sh_command

    if subsystem:
        logging.info(f"Running fuzzing {target}-{subsystem}-{func_name} on cpuset {cpusets}")
    else:
        logging.info(f"Running fuzzing {target}-{func_name} on cpuset {cpusets}")

    log_stdout = open(os.path.join(output_dir, "stdout"), "wb")
    # log_stderr = open(os.path.join(output_dir, "stderr"), "wb")
    # process = subprocess.run(' '.join(command), stdout=log_stdout, stderr=log_stderr, shell=True)
    process = subprocess.run(command, stdout=log_stdout, stderr=subprocess.PIPE)
    log_stdout.close()
    # log_stderr.close()
    free_cpuset(cpusets)

    return file_path, func_name

def run(target, subsystem, args, cross_fuzz):
    file_path, func_name = run_fuzz(target, subsystem, args, cross_fuzz)
    return file_path, func_name

def run_unpack(args):
    return run(*args)

if __name__ == "__main__":
    config.check_usage()
    
    target = sys.argv[1]
    subsystem = sys.argv[2] if len(sys.argv) == 3 else None
    if subsystem:
        func_list = func_list_manager.parse_func_list(f"{target}-{subsystem}")
    else:
        func_list = func_list_manager.parse_func_list(target)
    
    # Safety net: check if the output directory already exists
    if subsystem:
        output_dir = os.path.join(root_dir, "out", target, subsystem)
    else:
        output_dir = os.path.join(root_dir, "out", target)
    if os.path.exists(output_dir):
        logging.error(f"Output directory already exists: {output_dir}")
        sys.exit(1)

    # Run the evaluation script for each function
    with multiprocessing.pool.ThreadPool(config.POOL_SIZE) as pool:
        results = pool.imap_unordered(run_unpack, [(target, subsystem, args, config.CROSS_FUZZ) for args in func_list])
        for result in results:
            file_path = result[0]
            func_name = result[1]
            if subsystem:
                logging.info(f"Finished fuzzing {file_path}, {target}-{subsystem}-{func_name}")
            else:
                logging.info(f"Finished fuzzing {file_path}, {target}-{func_name}")

    run_cov_web.view_cov(target, subsystem)
