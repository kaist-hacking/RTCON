#!/usr/bin/env python3
import os
import sys
import config


root_dir = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))

#MAX_CMP_DEPTH = 1   # FreeRTOS
MAX_CMP_DEPTH = 3   # Zephyr

def parse_single_stack(f, spliter="====="):
    stack = {}
    line = f.readline().strip()
    while line and line != spliter:
        if "Bug: " in line:
            if ", " in line:
                # Confidence (float)
                stack["CONFIDENCE"] = float(line.split(", ")[1])
                stack["TYPE"] = line.split(", ")[0]
            else:
                stack["CONFIDENCE"] = 0.0
                stack["TYPE"] = line
        elif "STACK" not in stack:
            stack["STACK"] = [line]
        else:
            stack["STACK"].append(line)
        line = f.readline().strip()
    return stack


def duplicated_stack(stacks, stack):
    duplicated = False
    for e in stacks:
        if e["TYPE"] != stack["TYPE"]:
            continue
        if len(e["STACK"]) != len(stack["STACK"]):
            continue
        match = True
        for i, line in enumerate(e["STACK"]):
            if i >= MAX_CMP_DEPTH:
                break
            line_func_name = line.split(" ")[1]
            line_file_name = line.split(" ")[2]
            stack_func_name = stack["STACK"][i].split(" ")[1]
            stack_file_name = stack["STACK"][i].split(" ")[2]
            if "fuzzEntryFunction" in stack_func_name:
                break
            if (stack_func_name not in line_func_name) or (line_file_name != stack_file_name):
                match = False
                break
        if match:
            # Check Confidence
            new_confidence = max(stack["CONFIDENCE"], e["CONFIDENCE"])
            e["CONFIDNECE"] = new_confidence
            duplicated = True
            break
    return duplicated


# Bugs could be verified at other fuzzing sites.
def final_duplicated_stack(stacks, stack):
    duplicated = False
    for e in stacks:
        if e["TYPE"] != stack["TYPE"]:
            continue
        match = True
        for i, line in enumerate(e["STACK"]):
            if i >= MAX_CMP_DEPTH:
                break
            line_func_name = line.split(" ")[1]
            line_file_name = line.split(" ")[2]
            stack_func_name = stack["STACK"][i].split(" ")[1]
            stack_file_name = stack["STACK"][i].split(" ")[2]
            if "fuzzEntryFunction" in line_func_name:
                break
            if (line_func_name not in stack_func_name) or (line_file_name != stack_file_name):
                match = False
                break
        if match:
            duplicated = True
            break
    return duplicated


def view_crash(target, subsystem):
    if subsystem:
        fuzz_out_dir = os.path.join(root_dir, "out", target, subsystem)
    else:
        fuzz_out_dir = os.path.join(root_dir, "out", target)

    if not os.path.exists(fuzz_out_dir):
        print(f"{fuzz_out_dir} does not exist")
        return

    high_crash_list = []
    med_crash_list = []
    low_crash_list = []
    verifier_crash_list = []

    total_target_num = 0
    total_root_func_num = 0
    max_root_func_num = 0

    for fuzz_target in os.listdir(fuzz_out_dir):
        if not os.path.isdir(os.path.join(fuzz_out_dir, fuzz_target)):
            continue
        fuzz_target_loc = fuzz_target.replace(".", "/")
        fuzz_target_file = fuzz_target_loc[:fuzz_target_loc.rfind("/")] + ".c"
        fuzz_target_func = fuzz_target_loc[fuzz_target_loc.rfind("/")+1:]

        crash_dir = os.path.join(fuzz_out_dir, fuzz_target, "crash_dir")
        if not os.path.exists(crash_dir):
            continue

        host_bin = os.path.join(fuzz_out_dir, fuzz_target, "bin")
        if not os.path.exists(host_bin):
            continue

        current_root_func_num = 0
        for f in os.listdir(host_bin):
            if not f.startswith("top"):
                current_root_func_num += 1
        current_root_func_num -= 1 # remove the target function

        if current_root_func_num > 0:
            total_target_num += 1
            total_root_func_num += current_root_func_num

            if current_root_func_num > max_root_func_num:
                max_root_func_num = current_root_func_num

        for file in os.listdir(crash_dir):
            if file == "high_confidence":
                with open(os.path.join(crash_dir, file), "r") as f:
                    # Crash number
                    data = f.readline().strip()
                    # spliter
                    f.readline()
                    stack = parse_single_stack(f)
                    while len(stack) != 0:
                        if not duplicated_stack(high_crash_list, stack):
                            high_crash_list.append(stack)
                        stack = parse_single_stack(f)
            elif file == "med_confidence":
                with open(os.path.join(crash_dir, file), "r") as f:
                    # Crash number
                    data = f.readline().strip()
                    # spliter
                    f.readline()
                    stack = parse_single_stack(f)
                    while len(stack) != 0:
                        if not duplicated_stack(med_crash_list, stack):
                            med_crash_list.append(stack)
                        stack = parse_single_stack(f)
            elif file == "low_confidence":
                with open(os.path.join(crash_dir, file), "r") as f:
                    # Crash number
                    data = f.readline().strip()
                    # spliter
                    f.readline()
                    stack = parse_single_stack(f)
                    while len(stack) != 0:
                        if not duplicated_stack(low_crash_list, stack):
                            low_crash_list.append(stack)
                        stack = parse_single_stack(f)
            elif file == "verifier":
                with open(os.path.join(crash_dir, file), "r") as f:
                    # Crash number
                    data = f.readline().strip()
                    # spliter
                    f.readline()
                    stack = parse_single_stack(f)
                    while len(stack) != 0:
                        if not duplicated_stack(verifier_crash_list, stack):
                            verifier_crash_list.append(stack)
                        stack = parse_single_stack(f)

    # Deduplication
    final_unverifiable_crash_list = []
    for crash in med_crash_list:
        if duplicated_stack(high_crash_list, crash):
            continue
        if final_duplicated_stack(high_crash_list, crash):
            continue
        final_unverifiable_crash_list.append(crash)

    final_low_crash_list = []
    for crash in low_crash_list:
        if duplicated_stack(high_crash_list, crash) or duplicated_stack(med_crash_list, crash):
            continue
        if final_duplicated_stack(high_crash_list, crash) or final_duplicated_stack(med_crash_list, crash):
            continue
        final_low_crash_list.append(crash)

    # Separating the highest confidence crashes
    final_high_crash_list = []
    final_med_crash_list = []
    for crash in high_crash_list:
        if crash['CONFIDENCE'] < 1 and crash['CONFIDENCE'] > 0:
            final_med_crash_list.append(crash)
        else:
            final_high_crash_list.append(crash)
            
    print("===================================")
    print(f"Total crashes: {len(high_crash_list) + len(final_unverifiable_crash_list) + len(final_low_crash_list)}")
    print(f"High Confidence crashes: {len(final_high_crash_list)}")
    print(f"Med Confidence crashes: {len(final_med_crash_list)}")
    print(f"Low Confidence crashes: {len(final_low_crash_list)}")
    print(f"Unverifiable crashes: {len(final_unverifiable_crash_list)}")
    print("===================================")
    print(f"Verifier crashes: {len(verifier_crash_list)}")

    with open("/tmp/output.csv", "w") as f:
        f.write("High confidence crashes\n")
        for crash in final_high_crash_list:
            f.write(f"{crash['CONFIDENCE']},{crash['TYPE']}")
            for e in crash["STACK"]:
                f.write(",")
                f.write(f"{e}")
            f.write("\n")
        f.write("\n")
        f.write("Med confidence crashes\n")
        for crash in final_med_crash_list:
            f.write(f"{crash['CONFIDENCE']},{crash['TYPE']}")
            for e in crash["STACK"]:
                f.write(",")
                f.write(f"{e}")
            f.write("\n")
        f.write("\n")
        f.write("Low confidence crashes\n")
        for crash in final_low_crash_list:
            f.write(f"0,{crash['TYPE']}")
            for e in crash["STACK"]:
                f.write(",")
                f.write(f"{e}")
            f.write("\n")
        f.write("\n")
        f.write("Unverifiable crashes\n")
        for crash in final_unverifiable_crash_list:
            f.write(f"{crash['TYPE']}")
            for e in crash["STACK"]:
                f.write(",")
                f.write(f"{e}")
            f.write("\n")
        f.write("\n")
        f.write("Verifier crashes\n")
        for crash in verifier_crash_list:
            f.write(f"{crash['TYPE']}")
            for e in crash["STACK"]:
                f.write(",")
                f.write(f"{e}")
            f.write("\n")

    print("===================================")
    print(f"Total targets: {total_target_num}")
    print(f"Max root functions: {max_root_func_num}")
    print(f"root func num: {total_root_func_num}")
    print(f"Average root functions: {total_root_func_num / total_target_num}")


if __name__ == "__main__":
    target = sys.argv[1]
    subsystem = sys.argv[2] if len(sys.argv) == 3 else None
    view_crash(target, subsystem)

