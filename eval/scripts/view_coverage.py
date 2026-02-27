#!/usr/bin/env python3
import os
import sys
import csv
import argparse
import subprocess
from tqdm import tqdm


root_dir = "/"


def extract_id(filename):
    return int(os.path.basename(filename).split(".")[0])


def write_csv(all_funcs_coverage, out_dir):
    out_file = os.path.join(out_dir, "coverage.csv")
    with open(out_file, "w") as f:
        writer = csv.writer(f)
        writer.writerow(
            [
                "File",
                "Function",
                "Iteration",
                "Regions Covered",
                "Regions Total",
                "Functions Covered",
                "Functions Total",
                "Lines Covered",
                "Lines Total",
                "Branches Covered",
                "Branches Total",
            ]
        )

        for (file, func), reports in all_funcs_coverage.items():
            for i, report in enumerate(reports):
                with open(report, "r") as r:
                    for line in r:
                        if not line.startswith("TOTAL"):
                            continue
                        data = line.split()
                        data = [e.strip() for e in data]

                        regions = int(data[1])
                        regions_missed = int(data[2])
                        regions_cover = data[3]

                        functions = int(data[4])
                        functions_missed = int(data[5])
                        functions_cover = data[6]

                        lines = int(data[7])
                        lines_missed = int(data[8])
                        lines_cover = data[9]

                        branches = int(data[10])
                        branches_missed = int(data[11])
                        branches_cover = data[12]

                        writer.writerow(
                            [
                                file,
                                func,
                                i + 1,
                                regions - regions_missed,
                                regions,
                                functions - functions_missed,
                                functions,
                                lines - lines_missed,
                                lines,
                                branches - branches_missed,
                                branches,
                            ]
                        )
            writer.writerow([])


def merge_coverage(target, subsystem, all_funcs_coverage_profdata, out_dir):
    infer_interval = 9999
    infer_max_num = 0

    for (file, func), profdata in all_funcs_coverage_profdata.items():
        start_prof_id = 0
        for single in profdata:
            prof_id = extract_id(single)

            if start_prof_id == 0:
                start_prof_id = prof_id
                continue

            if prof_id - start_prof_id < infer_interval:
                infer_interval = prof_id - start_prof_id

            max_num = (prof_id - start_prof_id) // infer_interval + 1
            if max_num > infer_max_num:
                infer_max_num = max_num

    if infer_max_num == 0:
        # Only one profdata
        infer_max_num = 1
        infer_interval = 1

    print("[+] Infer interval: %d, Infer Max Num: %d" % (infer_interval, infer_max_num))

    coverage_at_point = {}
    for (file, func), profdata in all_funcs_coverage_profdata.items():
        start_prof_id = extract_id(profdata[0])
        profdata_dir = os.path.dirname(profdata[0])

        prev_prof_id = start_prof_id
        for i in range(infer_max_num):
            indexed_profid = start_prof_id + i * infer_interval
            indexed_profdata = os.path.join(profdata_dir, f"{indexed_profid}.profdata")
            if not os.path.exists(indexed_profdata):
                # Needed profdata is missing
                # Maybe due to the fuzzing stuck in the middle
                # Use the previous profdata
                indexed_profid = prev_prof_id
                indexed_profdata = os.path.join(profdata_dir, f"{prev_prof_id}.profdata")

            if i not in coverage_at_point:
                coverage_at_point[i] = []

            coverage_at_point[i].append(indexed_profdata)
            prev_prof_id = indexed_profid

    num_of_profdata = 0
    for i, profdatas in coverage_at_point.items():
        if num_of_profdata == 0:
            num_of_profdata = len(profdatas)
        if num_of_profdata != len(profdatas):
            print(f"[-] Different length at point {i}")
            return

    # Get native binary file
    target_os = target.split("-")[-1].lower()
    target_subsystem = subsystem.lower() if subsystem else None
    binary = os.path.join(
        root_dir,
        "native_binaries",
        (
            f"{target_os}-native"
            if not target_subsystem
            else f"{target_os}-{target_subsystem}-native"
        ),
    )
    if not os.path.exists(binary):
        print(f"[-] No native binary found at {binary}")
        sys.exit(1)

    # Pick binary from the outputs
    # binary = ""
    # for (file, func), profdata in all_funcs_coverage_profdata.items():
    #     profdata_dir = os.path.dirname(profdata[0])
    #     target_dir = os.path.dirname(os.path.dirname(profdata_dir))
    #     for f in os.listdir(os.path.join(target_dir, "bin")):
    #         if f.endswith("-fuzz"):
    #             binary = os.path.join(target_dir, "bin", f)
    #             break
    # if binary == "":
    #     print("[-] No binary found")
    #     return

    # Merge the coverage
    total_coverage_report = []
    pbar = tqdm(total=len(coverage_at_point), desc=f"Merging coverage into {out_dir}", bar_format="{l_bar}{bar}")
    for i, profdata in coverage_at_point.items():
        total_profraw = os.path.join(out_dir, f"{i}.profdata")
        if os.path.exists(total_profraw):
            os.remove(total_profraw)
        if i != 0:
            # Gradually mark the coverage for the case the fuzzer dies at some point
            profdata += coverage_at_point[i - 1]
        subprocess.run(["llvm-profdata-18", "merge", "-o", total_profraw] + profdata)

        with open(os.path.join(out_dir, f"{i}.report"), "w") as f:
            subprocess.run(["llvm-cov-18", "report", "-instr-profile", total_profraw, binary], stdout=f)

        total_coverage_report.append(os.path.join(out_dir, f"{i}.report"))

        pbar.update(1)

    pbar.close()

    return total_coverage_report


def generate_coverage(target, subsystem, out_dir):
    if subsystem:
        fuzz_out_dir = os.path.join(root_dir, "out", target, subsystem)
    else:
        fuzz_out_dir = os.path.join(root_dir, "out", target)

    if not os.path.exists(fuzz_out_dir):
        print(f"{fuzz_out_dir} does not exist")
        return

    if not os.path.exists(out_dir):
        os.makedirs(out_dir)

    all_funcs_coverage_report = {}
    all_funcs_coverage_profdata = {}

    for fuzz_target in os.listdir(fuzz_out_dir):
        if not os.path.isdir(os.path.join(fuzz_out_dir, fuzz_target)):
            continue
        fuzz_target_loc = fuzz_target.replace(".", "/")
        fuzz_target_file = fuzz_target_loc[:fuzz_target_loc.rfind("/")] + ".c"
        fuzz_target_func = fuzz_target_loc[fuzz_target_loc.rfind("/")+1:]

        coverage_dir = os.path.join(fuzz_out_dir, fuzz_target, "coverage_dir")
        if not os.path.exists(coverage_dir):
            continue

        coverage_total_dir = os.path.join(coverage_dir, "total")
        if not os.path.exists(coverage_total_dir):
            continue

        coverage_total_report = []
        coverage_total_profdata = []
        for report in os.listdir(coverage_total_dir):
            if report.endswith(".report"):
                coverage_total_report.append(os.path.join(coverage_total_dir, report))
            elif report.endswith(".profdata"):
                coverage_total_profdata.append(os.path.join(coverage_total_dir, report))
        
        if len(coverage_total_profdata) == 0:
            continue

        # Sort the report
        coverage_total_report.sort(key=extract_id)
        coverage_total_profdata.sort(key=extract_id)

        all_funcs_coverage_report[(fuzz_target_file, fuzz_target_func)] = coverage_total_report
        all_funcs_coverage_profdata[(fuzz_target_file, fuzz_target_func)] = coverage_total_profdata

    merged_coverage_report = merge_coverage(target, subsystem, all_funcs_coverage_profdata, out_dir)

    all_funcs_coverage_report[("total", "total")] = merged_coverage_report

    write_csv(all_funcs_coverage_report, out_dir)
    print(f"[+] Output written to {out_dir}")


if __name__ == "__main__":

    argparser = argparse.ArgumentParser(description="View coverage report")
    argparser.add_argument("target", help="Target name")
    argparser.add_argument("subsystem", nargs="?", help="Subsystem name")
    argparser.add_argument("--output", help="Output directory", default="/tmp/output")
    args = argparser.parse_args()

    target = args.target
    subsystem = args.subsystem

    if target not in [
        "RIOT",
        "Zephyr",
        "FreeRTOS",
        "ThreadX",
        "FuzzSlice-RIOT",
        "FuzzSlice-Zephyr",
        "FuzzSlice-FreeRTOS",
        "FuzzSlice-ThreadX",
        "Manual-RIOT",
        "Manual-FreeRTOS",
        "Manual-ThreadX",
        "Manual-Zephyr",
        "Ablation-RIOT",
        "Ablation-FreeRTOS",
        "Ablation-Zephyr",
        "Ablation-ThreadX",
        "FuzzGen-RIOT",
        "FuzzGen-FreeRTOS",
        "FuzzGen-Zephyr",
        "FuzzGen-ThreadX",
    ]:
        print(f"Invalid target: {target}")
        sys.exit(1)

    if subsystem and subsystem not in ["App", "Cell-intf", "Cell-mod", "TCP"]:
        print(f"Invalid subsystem: {subsystem}")
        sys.exit(1)
    if subsystem and (target != "FreeRTOS" and target != "FuzzSlice-FreeRTOS" and target != "Manual-FreeRTOS" and target != "Ablation-FreeRTOS"):
        print(f"Subsystem is only available for FreeRTOS")
        sys.exit(1)

    generate_coverage(target, subsystem, args.output)
