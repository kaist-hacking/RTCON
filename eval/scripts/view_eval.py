#!/usr/bin/env python3

import sys
import os
import threading
from http.server import HTTPServer, SimpleHTTPRequestHandler

LLVM_VERSION = 18
LLVM_PROFDATA = f"llvm-profdata-{LLVM_VERSION}"
LLVM_COV = f"llvm-cov-{LLVM_VERSION}"

SERVICE_PORT = 8080


class CovHTTPServer(HTTPServer):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

class RequestHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        directory = kwargs.pop('directory', None)
        super().__init__(*args, directory=directory, **kwargs)


def run_http_server(workdir):
    # Start the HTTP server
    root_dir = os.path.join(workdir, "total-cov/report")
    os.chdir(root_dir)
    server_address = ("", SERVICE_PORT)
    httpd = CovHTTPServer(server_address, RequestHandler, root_dir) # for some reason, does not work...
    thread = threading.Thread(target=httpd.serve_forever)
    thread.start()

    print(f"HTTP server started at port {SERVICE_PORT}")

def merge_profdata(workdir):
    command = [
        LLVM_PROFDATA, "merge", "-sparse",
        "-o", f"{workdir}/total-cov/total-cov.profdata",
        f"{workdir}/func-cov/*/coverage/*.profdata"
    ]
    os.system(' '.join(command))

def report_coverage(workdir):
    # for binary, just use the first one
    binary = ""
    for f in os.listdir(f"{workdir}/func-cov"):
        if f == "total-cov":
            continue
        binary_dir = os.path.join(workdir, "func-cov", f, "bin")
        cov_candidates = [f for f in os.listdir(binary_dir) if f.endswith("-cov")]
        if len(cov_candidates) == 0:
            continue
        binary = os.path.join(binary_dir, cov_candidates[0])
        break
    
    if binary == "":
        print("No -cov binary found")
        sys.exit(1)

    command = [
        LLVM_COV, "show",
        "-format=html",
        f"-output-dir={workdir}/total-cov/report",
        f"-instr-profile={workdir}/total-cov/total-cov.profdata",
        f"-compilation-dir=.",
        binary
    ]
    os.system(' '.join(command))

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} <workdir>")
        sys.exit(1)
    
    merge_profdata(sys.argv[1])
    report_coverage(sys.argv[1])
    run_http_server(sys.argv[1])
