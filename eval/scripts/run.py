#!/usr/bin/env python3

import os
import sys
import subprocess
import multiprocessing
import multiprocessing.pool
import logging

import func_list_manager

logging.basicConfig(level=logging.INFO)
root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

lock = multiprocessing.Lock()
cpu_counters = multiprocessing.Array('i', multiprocessing.cpu_count())

def alloc_cpuset(cpus):
    assert cpus <= multiprocessing.cpu_count()
    with lock:
        cpusets = []
        for i in range(multiprocessing.cpu_count()):
            if cpu_counters[i] == 0:
                cpusets.append(str(i))
                cpu_counters[i] = 1
            if len(cpusets) == cpus:
                break
    return ','.join(cpusets)

def free_cpuset(cpusets):
    with lock:
        for cpuset in cpusets.split(','):
            cpu_counters[int(cpuset)] = 0

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

def run(target, subsystem, args):
    docker_image_name = f"cgcc-{target.lower()}"
    workdir = config.get_workdir(target, subsystem)

    file_path = args[0]
    func_name = args[1]
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
        f"--name={container_name}",
        docker_image_name,
    ]
    sh_command = f"sh -c \"echo 'python3 /scripts/run_fuzz.py {config.TIME}' | ./build.sh {' '.join(args)}\"".split(' ')
    command += sh_command
    
    if subsystem:
        logging.info(f"Running fuzzing {target}-{subsystem}-{func_name} on cpuset {cpusets}")
    else:
        logging.info(f"Running fuzzing {target}-{func_name} on cpuset {cpusets}")
    

    log_stdout = open(os.path.join(output_dir, "stdout"), "wb")
    log_stderr = open(os.path.join(output_dir, "stderr"), "wb")
    process = subprocess.run(' '.join(command), stdout=log_stdout, stderr=log_stderr, shell=True)
    log_stdout.close()
    log_stderr.close()
    free_cpuset(cpusets)

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
        results = pool.imap_unordered(run_unpack, [(target, subsystem, args) for args in func_list])
        for result in results:
            file_path = result[0]
            func_name = result[1]
            if subsystem:
                logging.info(f"Finished fuzzing {file_path}, {target}-{subsystem}-{func_name}")
            else:
                logging.info(f"Finished fuzzing {file_path}, {target}-{func_name}")
