#!/usr/bin/env python3

import os
import sys
import logging
import subprocess

import config

root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

def view_cov(target, subsystem):
    docker_image_name = f"cgcc-{target.lower()}"
    workdir = config.get_workdir(target, subsystem)
    
    if subsystem:
        coverage_dir = os.path.join(root_dir, "out", target, subsystem)
    else:
        coverage_dir = os.path.join(root_dir, "out", target)
    output_dir = os.path.join(coverage_dir, "total-cov")

    os.makedirs(output_dir, exist_ok=True)

    # Run
    container_name = f"{target.lower()}-{subsystem.lower()}-test" if subsystem \
        else f"{target.lower()}-test"
    if subsystem:
        host_port = config.cov_port_dict[target][subsystem]
    else:
        host_port = config.cov_port_dict[target]
    
    command = [
        "docker", "run", "--rm",
        "--entrypoint=/usr/bin/python3",
        f"--volume={output_dir}:{workdir}/total-cov",
        f"--volume={coverage_dir}:{workdir}/func-cov",
        f"--name={container_name}",
        f"-p={host_port}:8080",
        docker_image_name,
    ]
    sh_command = f"/scripts/view_eval.py {workdir}".split(' ')
    command += sh_command

    process = subprocess.Popen(' '.join(command), stdout=subprocess.sys.stdout, stderr=subprocess.sys.stderr, shell=True)
    process.wait()
    if subsystem:
        logging.info(f"Finishing total coverage server for {target}-{subsystem}")
    else:
        logging.info(f"Finishing total coverage server for {target}")

if __name__ == "__main__":
    config.check_usage()
    
    target = sys.argv[1]
    subsystem = sys.argv[2] if len(sys.argv) == 3 else None
    view_cov(target, subsystem)
