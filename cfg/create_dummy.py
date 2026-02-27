import sys
import os
import re
import subprocess


def extract_parameters(function_signature):
    # Regular expression to match the parameter list within parentheses
    pattern = re.compile(r"\(\s*([^)]*)\s*\)")

    # Search for the pattern in the function signature
    matches = pattern.findall(function_signature)

    for match in matches:
        if " " not in match:
            continue
        return match

    return None


def extract_parameter_names(function_param):
    params_with_type = function_param.split(",")
    params_without_type = [e.strip().split(" ")[-1] for e in params_with_type]
    params_result = [e.replace("*", "") for e in params_without_type]

    return ",".join(params_result)


def append_function_definition(file_path, function_definition):
    # Read the existing content of the file
    with open(file_path, "r", encoding="utf-8", errors="ignore") as file:
        content = file.read()

    # Append the new function definition at the end of the file
    new_content = content + "\n\n" + function_definition + "\n"

    # Write the updated content back to the file
    with open(file_path, "w", encoding="utf-8") as file:
        file.write(new_content)

    print(f"Function definition appended to file: {file_path}")


def find_function_prototype(file_path, function_name):
    # Regular expression to match function prototypes in C
    # This pattern assumes function prototypes are on a single line.
    prototype_pattern = re.compile(
        # rf"\b(?:[\w\s\*\(\)\[\]]+)\s+{function_name}\s*\([^)]*\)\s*(?:;|{{)"
        rf"\b{function_name}\s*\([^)]*\)\s*{{"
    )
    found_prototype = None
    with open(file_path, "r", encoding="utf-8", errors="ignore") as f:
        content = f.read()
        matches = prototype_pattern.findall(content)
        if matches:
            found_prototype = (file_path, matches)

    if found_prototype is None:
        print(f"Could not find prototypes for {function_name}")
        return None
    
    return found_prototype


def create_dummy_caller(directory, file, function_name):
    if not os.path.exists(directory):
        print(f"Directory does not exist: {directory}")
        return None

    if not file.endswith(".c"):
        print(f"File is not a C file: {file}")
        return None

    file_path = os.path.join(directory, file)

    if not os.path.exists(file_path):
        print(f"File does not exist: {file_path}")
        return None
    
    found_prototype = find_function_prototype(file_path, function_name)
    if not found_prototype:
        return

    prototype = found_prototype[1]
    function_param = extract_parameters(prototype[0])
    if not function_param:
        print(f"Could not parse pararmeters for {function_name}")
        return None

    function_param_name = extract_parameter_names(function_param)

    dummy_def = f"void dummy_caller0({function_param}){{ {function_name}({function_param_name}); }}"

    append_function_definition(file_path, dummy_def)

    # for j in range(i + 1, 5):
    #     dummy_def = f"void dummy_caller{j}(){{}}"
    #     append_function_definition(file_path, dummy_def)


if __name__ == "__main__":
    if len(sys.argv) != 4:
        print(f"Usage: {sys.argv[0]} <proj_dir> <file_path> <symbol>")
        sys.exit(1)

    create_dummy_caller(sys.argv[1], sys.argv[2], sys.argv[3])

    # Run Build
    command = "make -f Makefile.native -j"
    process = subprocess.Popen(
        command, shell=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL
    )
    process.wait()
