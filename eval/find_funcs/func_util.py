import sys
import os
import re

"""
Author: mat-eng, based on yuedeji c_func_name_extract project
Description: Extract list of all functions of C source code
Date: 28/07/2023
"""

import os
import os.path
import csv
import re
import sys
import openpyxl

###########################
# INPUT SCRIPT PARAMETERS #
###########################
# Path to folder with all files to analyse
folder_path = "src"

# Output file to be written, .txt and .xlsx supported format
result_file = "Functions_list.xlsx"

###########################

# Attribute that should be removed during line analysis
attribute_list = ["__attribute__((interrupt))"]

# File extensions to search for functions
valid_ext = [".c", ".cpp"]

# Keywords that should not be detected as a function
keyword_set = [
    "sizeof",
    "int",
    "char",
    "float",
    "double",
    "bool",
    "void",
    "short",
    "long",
    "signed",
    "struct",
    "uint8_t",
    "uint16_t",
    "uint32_t",
    "int8_t",
    "int16_t",
    "int32_t",
]

###########################

# Name: is_valid_name
# Description: Verify that function has a valid name
# input: name of function
# return: True or False
def is_valid_name(name):
    if re.match("[a-zA-Z_][a-zA-Z0-9_]*", name) is None:
        return False
    if name in keyword_set:
        return False
    return True

# Name: is_func
# Description: Check if line is a function
# input: Line of file
# return: Function name or none
def is_func(line):
    # Remove all leading and trailing whitespaces
    line = line.strip()

    # Remove all attribute keywords
    for i in range(0, len(attribute_list)):
        line = line.replace(attribute_list[i], "")

    # Rule 1: Line to short to be a function
    if len(line) < 2:
        return None

    # Rule 2: Line can not be a function if '=' is present
    if "=" in line:
        return None

    # Rule 3: Line should have at least '(' to be a function
    if "(" not in line:
        return None

    # Rule 4: Line can not be a function if it starts with '#' or '/'
    if line[0] == "#" or line[0] == "/":
        return None

    # Rule 5: Line can not be a function if it ends with ';'
    if line.endswith(";"):
        return None

    if line.startswith("static"):
        line = line[len("static") :]

    # Remove pointer character '*' and '&'
    line = re.sub("\*", " ", line)
    line = re.sub("&", " ", line)

    # Replace '(' as ' ('
    line = re.sub("\(", " \( ", line)
    line_split = line.split()

    if len(line_split) < 2:
        return None

    # Count round bracket '('
    bracket_num = 0
    for ch in line:
        if ch == "(":
            bracket_num += 1

    if bracket_num == 1:
        for index in range(len(line_split)):
            if "(" in line_split[index]:
                return line_split[index - 1]
    else:
        line = re.sub("\(", " ", line)
        line = re.sub("\)", " ", line)
        line_split = line.split()
        index = 0
        for one in line_split:
            if is_valid_name(one):
                index += 1
                if index == 2:
                    return one
        return None


# Name: get_line_type
# Description: Check if the line is a comment, macro or other
# input: Line of file
# return: Type of line
def get_line_type(line):
    line = line.strip()
    if line.startswith("/*"):
        return "comment_paragraph"
    elif line.startswith("//"):
        return "comment_line"
    elif line.startswith("#"):
        return "macro"
    return "other"

# Name: is_comment_end
# Description: Check if it is the end of comment block
# input: Line of file
# return: True or False
def is_comment_end(line):
    line = line.strip()
    if line.endswith("*/"):
        return True
    return False

# Name: func_name_extract
# Description: List all functions of file 
# input: File path
# return: List of functions
def func_name_extract(file_path):
    if not os.path.isfile(file_path):
        return

    file_list = []
    with open(file_path, "r") as fp:
        for line in fp.readlines():
            file_list.append(line)

    func_list = []

    i = -1
    while i < len(file_list) - 1:
        i += 1
        line = file_list[i]
        line_type = get_line_type(line)
        if line_type == "comment_line" or line_type == "macro":
            continue
        elif line_type == "comment_paragraph":
            while not is_comment_end(file_list[i]):
                i += 1
        else:
            func_name = is_func(line)
            if func_name is not None:
                start_line = i
                left_brack_num = 0
                define_num = 0
                while True:
                    line = (file_list[i]).strip()
                    if define_num > 0:
                        i += 1
                        continue

                    left_brack_num += line.count("{")
                    if "}" in line:
                        left_brack_num -= line.count("}")
                        if left_brack_num < 1:
                            if (
                                "};" in line
                            ):  # Function can not ends with '};', function found will be discarded
                                was_not_function = 1
                                break
                            else:
                                was_not_function = 0
                                break
                    i += 1
                end_line = i

                if was_not_function == 0:
                    file_name = file_path.replace(folder_path + "\\", "")
                    func_list.append(
                        [func_name, file_name, start_line + 1, end_line + 1]
                    )
    return func_list

# Name: func_name_extract_folder
# Description: List all functions of all files
# input: Folder path
# return: List of functions
def func_name_extract_folder(work_folder):
    func_list_all = []
    parsed_file_count = 0
    ignored_file_count = 0
    for dirpath, dirnames, filenames in os.walk(work_folder):
        for name in filenames:
            ignored_file_count += 1
            for ext in valid_ext:
                if name.endswith(ext):
                    parsed_file_count += 1
                    file_path = os.path.join(dirpath, name)
                    func_list_all = func_list_all + func_name_extract(file_path)
    ignored_file_count = ignored_file_count - parsed_file_count

    print("---------- Analysis statistics ----------")
    print("Source files folder      :", work_folder)
    print("Number of files ignored  :", ignored_file_count)
    print("Number of files parsed   :", parsed_file_count)
    print("Number of functions found:", len(func_list_all))
    print("")
    print("Details:")

    # Everything below is just to print a nice table in the console
    longest_file_name = 0
    space = " "
    line = "-"
    for i in range(0, len(func_list_all)):
        if longest_file_name < len(func_list_all[i][1]):
            longest_file_name = len(func_list_all[i][1])

    print("+", (longest_file_name + 1) * line, "+", 19 * line, "+")
    print("| File", (longest_file_name - 4) * space, "| Number of functions |")
    print("+", (longest_file_name + 1) * line, "+", 19 * line, "+")

    i = 0
    functions_count = 0
    while i < len(func_list_all):
        file_name = func_list_all[i][1]
        i += 1
        if (i >= len(func_list_all)) or (
            (i < len(func_list_all)) and (file_name != func_list_all[i][1])
        ):
            xspace = longest_file_name - len(file_name)
            if (i - functions_count) > 99:
                print("|", file_name, xspace * space, "|", i - functions_count, 15 * space, "|")
            elif (i - functions_count) > 9:
                print("|", file_name, xspace * space, "|", i - functions_count, 16 * space, "|")
            else:
                print("|", file_name, xspace * space, "|", i - functions_count, 17 * space, "|")

            functions_count = i
    
    print("+", (longest_file_name + 1) * line, "+", 19 * line, "+")
    print("-----------------------------------------")

    return func_list_all

# Name: write_to_file
# Description: Write list of functions to text file
# input: Functions list, output text file
# return: -
def write_to_file(func_list, output_file):
    header = ["Name", "File", "First line", "Last line"]

    if output_file.endswith(".txt"):
        with open(output_file, "w") as out_file:
            csv_write = csv.writer(out_file, delimiter=",")
            csv_write.writerow(header)
            for one in func_list:
                csv_write.writerow(one)
        remove_empty_line(output_file)
        print("File", output_file, "created")

    elif output_file.endswith(".xlsx"):
        wb = openpyxl.Workbook()
        ws = wb.worksheets[0]
        with open(output_file, "w") as data:
            ws.append(header)
            for row in func_list:
                ws.append(row)
        wb.save(output_file)
        print("File", output_file, "created")

    else:
        print("Output file format not supported, please use .txt or .xlsx")

# Name: remove_empty_line
# Description: Remove empty line of text file
# input: Text file
# return: -
def remove_empty_line(file_in):
    with open(file_in, "r+") as fd:
        lines = fd.readlines()
        fd.seek(0)
        fd.writelines(line for line in lines if line.strip())
        fd.truncate()

def get_function_line(file_path, function_name):
    functions = func_name_extract(file_path)
    for func_info in functions:
        if func_info[0] == function_name:
            line = func_info[3] - func_info[2]

    return line
