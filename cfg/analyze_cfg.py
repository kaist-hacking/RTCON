#!/usr/bin/env python3
import os
import re
import sys
import datetime
import argparse
import pydot

import userconfig

class CFGWriter:
    CONFIG_DIRECTORY = "config"

    @staticmethod
    def write_config(thread, scope, target_indices, indirect_links, func_depth):
        if not os.path.exists(CFGWriter.CONFIG_DIRECTORY):
            os.makedirs(CFGWriter.CONFIG_DIRECTORY)

        entry_func_name = thread[0]
        entry_file_name = thread[1]

        input_index = target_indices[(entry_file_name, entry_func_name)][0]
        length_index = target_indices[(entry_file_name, entry_func_name)][1]

        assert_func = []
        skip_func = []
        ban_func = []

        func_scope = []
        file_scope = []

        # Append assert functions
        for func_name in userconfig.assert_func:
            assert_func.append(func_name)

        with open(
            os.path.join(CFGWriter.CONFIG_DIRECTORY, f"{entry_func_name.split('.')[0]}.yaml"), "w"
        ) as f:
            for e in scope:
                func_name = e[0].split(".")[0]
                file_name = e[1]

                if (file_name, func_name) in target_indices:
                    func_input_index = target_indices[(file_name, func_name)][0]
                    func_length_index = target_indices[(file_name, func_name)][1]
                else:
                    func_input_index = -1
                    func_length_index = -1

                if not func_name or not file_name:
                    continue

                if f"{func_name},{func_input_index},{func_length_index}" not in func_scope:
                    func_scope.append(f"{func_name},{func_input_index},{func_length_index}")
                if file_name not in file_scope:
                    file_scope.append(file_name)

                if func_name in userconfig.assert_func:
                    continue
                if func_name in userconfig.skip_func:
                    skip_func.append(func_name)
                    continue
                if func_name in userconfig.ban_func:
                    ban_func.append(func_name)
                    continue

                if any(k in func_name for k in userconfig.skip_func):
                    if func_name not in skip_func:
                        skip_func.append(func_name)
                    continue

                if any(func_name.startswith(k) for k in userconfig.ban_keywords):
                    if func_name not in ban_func:
                        ban_func.append(func_name)
                    continue
            
            config = "\n"

            if len(assert_func) > 0:
                config += "assert:\n"
            else:
                config += "assert: []\n"
            for func in assert_func:
                config += f"  - {func}\n"
            config += "\n"

            if len(skip_func) > 0:
                config += "skip:\n"
            else:
                config += "skip: []\n"
            for func in skip_func:
                config += f"  - {func}\n"
            config += "\n"

            if len(ban_func) > 0:
                config += "ban:\n"
            else:
                config += "ban: []\n"
            for func in ban_func:
                config += f"  - {func}\n"
            config += "\n"

            if len(userconfig.hook_functions) > 0:
                config += "hook:\n"
            else:
                config += "hook: []\n"
            for func in userconfig.hook_functions:
                config += f"  - {func}\n"
            config += "\n"

            # Taint functions are analyzed later
            config += "taint: []\n"
            config += "\n"

            config += "entry:\n"
            config += f"  name: {entry_func_name.split('.')[0]}\n"
            config += f"  input_index: {input_index}\n"
            config += f"  length_index: {length_index}\n"
            config += "\n"

            if len(func_scope) > 0:
                config += "scope:\n"
                for func in func_scope:
                    config += f"  - {func}\n"
            else:
                config += "scope: []\n"
            config += "\n"

            if len(file_scope) > 0:
                config += "file_scope:\n"
                for file in file_scope:
                    config += f"  - {file}\n"
            else:
                config += "file_scope: []\n"
            config += "\n"

            if len(indirect_links) > 0:
                config += "indirect_links:\n"
                for link in indirect_links:
                    parent_func_name = link[0][0]
                    parent_file_name = link[0][1]
                    child_func_name = link[1][0]
                    child_file_name = link[1][1]
                    linkage = link[2]
                    argnum = link[3]

                    # child_func input index and length index
                    if (child_file_name, child_func_name) in target_indices:
                        child_input_index = target_indices[(child_file_name, child_func_name)][0]
                        child_length_index = target_indices[(child_file_name, child_func_name)][1]
                    else:
                        child_input_index = -1
                        child_length_index = -1

                    config += f"  - {parent_func_name},{parent_file_name},{child_func_name},{child_file_name},{linkage},{argnum},{child_input_index},{child_length_index}\n"
            else:
                config += "indirect_links: []\n"
            config += "\n"

            if len(func_depth) > 0:
                config += "multientry:\n"
                for func, depth in func_depth.items():
                    config += f"  - {func},{depth}\n"
            else:
                config += "multientry: []\n"

            f.write(config)


class CFG:
    def __init__(self, input_dot, input_ll, input_target, target_func, target_file):
        self.start_time = datetime.datetime.now()

        # Read from llvm ll
        self.DIFile = {}
        self.DISubprogram = {}
        self.define_func = {}

        self.analysis_targets = []
        self.indirect_links = []

        self.target_func = target_func
        self.target_file = target_file

        self.debug_info = []

        self.read_input_graph(input_dot)
        self.read_llvm_ll(input_ll)
        self.read_target_list(input_target)

        self.threads = {}
        self.threads_scope = {}

        self.scopes = {}

        self.scope_cache = {}
        self.implicit_scope_cache = {}

        self.func_depth = {}

        self.func_def = {}
        self.func_params_type = {}
        self.func_ll = {}

    def get_elapsed_time(self):
        elapsed_time = datetime.datetime.now() - self.start_time
        return elapsed_time

    def check_top_functions(self, func_name, file_name):
        if func_name in userconfig.top_functions:
            return True
        return False

    def read_input_graph(self, input_dot):
        print(f"[+] Read {input_dot}")
        self.graph = pydot.graph_from_dot_file(input_dot)[0]
        print(f"[+] Read {input_dot} Done. - {self.get_elapsed_time()}")

    def read_llvm_ll(self, input_ll):
        print(f"[+] Read {input_ll}")
        with open(input_ll, "r") as f:
            self.ll = f.readlines()

        func_pattern = re.compile(r"define\s+.*?\s*!dbg\s+!(\d+)")
        subprogram_pattern = re.compile(
            r"!(\d+)\s+=\s+.*?!DISubprogram\(.*file: !(\d+)"
        )
        file_pattern = re.compile(
            r'!(\d+) = !DIFile\(filename: "(.*)", directory: "(.*)"\)'
        )

        for line in self.ll:
            file_match = file_pattern.search(line)
            subprogram_match = subprogram_pattern.search(line)
            func_match = func_pattern.search(line)
            if file_match:
                file_id = file_match.group(1)
                file_name = file_match.group(2)
                file_path = file_match.group(3)
                if file_name and '"' in file_name:
                    file_name = file_name[: file_name.index('"')]
                if file_path and '"' in file_path:
                    file_path = file_path[: file_path.index('"')]
                self.DIFile[file_id] = (file_name, file_path)
            elif subprogram_match:
                subprogram_id = subprogram_match.group(1)
                subprogram_file_id = subprogram_match.group(2)
                self.DISubprogram[subprogram_id] = subprogram_file_id
            elif func_match:
                func_def = func_match.group(0)
                func_id = func_match.group(1)
                self.define_func[func_id] = func_def

        # Parse the debug info
        self.debug_info = []
        debug_info = False
        for line in self.ll:
            if "!0 " in line:
                debug_info = True
            if debug_info:
                if not line.startswith("!"):
                    debug_info = False
                    break
                self.debug_info.append(line)

        print(f"[+] Read {input_ll} Done. - {self.get_elapsed_time()}")

    def find_function_ll(self, func_name):
        if func_name in self.func_ll:
            return self.func_def[func_name], self.func_params_type[func_name], self.func_ll[func_name]

        function_ll_pattern = re.compile(r"^define.*@([\w$.]+)\(([\w %,.]*)\).*{")
        start_func = False
        function_params_types = []
        function_ll = []
        function_def = None
        params = None
        for line in self.ll:
            if start_func:
                if line.strip() == "}":
                    start_func = False
                    break
                function_ll.append(line)
            else:
                match = function_ll_pattern.search(line)
                # Function definition
                if match and match.group(1) == func_name:
                    params = match.group(2)
                    function_def = line
                    start_func = True
        if params:
            # Function parameters
            for param in params.split(","):
                param_value_pattern = re.compile(r"%(\d+)")
                param_match = param_value_pattern.search(param)
                if param_match:
                    for param_value in param_match.groups():
                        param_type = self.find_type_ll(function_ll,param_value)
                        function_params_types.append(param_type)
            
        self.func_def[func_name] = function_def
        self.func_params_type[func_name] = function_params_types
        self.func_ll[func_name] = function_ll
        return function_def,function_params_types,function_ll

    def find_indirect_call_ll(self, ll):
        indirect_call_pattern = re.compile(r"call.*%([\w$.]+)\(([\w %,.]*)\)")
        params_type = []
        for line in ll:
            match = indirect_call_pattern.search(line)
            if match:
                params = match.group(2)
                if not params:
                    return line, []
                params = params.split(",")
                for param in params:
                    param_value_pattern = re.compile(r"%(\d+)")
                    match = param_value_pattern.search(param)
                    if match:
                        for param_value in match.groups():
                            param_type = self.find_type_ll(ll, param_value)
                            params_type.append(param_type)
                return line, params_type
        return None, []

    def find_type_ll(self, ll, value):
        # getelementptr
        # getelementptr_pattern = re.compile(rf"getelementptr.*%([\w.]+).*%{value},")
        # for line in ll:
        #     match = getelementptr_pattern.search(line)
        #     if match:
        #         return match.group(1)

        # get meta data ptr
        metadata_id = None
        type_id = None
        basetype_id = None
        for line in ll:
            if f"metadata ptr %{value}," in line:
                # There is a metadata
                pattern = re.compile(r"metadata !([\d]+),")
                match = pattern.search(line)
                if match:
                    metadata_id = match.group(1)
                    break
        # Function parameters could be stored in alloca
        if not metadata_id:
            new_value = None
            for line in ll:
                if f"%{value} = load ptr" in line:
                    new_value = line.split(',')[1].split('%')[-1]
                    if new_value:
                        break
                if f"store ptr %{value}," in line:
                    new_value = line.split(',')[1].split('%')[-1]
                    if new_value:
                        break

            if new_value:
                for line in ll:
                    if f"metadata ptr %{new_value}," in line:
                        pattern = re.compile(r"metadata !([\d]+),")
                        match = pattern.search(line)
                        if match:
                            metadata_id = match.group(1)
                            break

        if metadata_id:
            for line in self.debug_info:
                type_pattern = re.compile(rf"!{metadata_id}.*type: !([\d]+)")
                type_match = type_pattern.search(line)
                if type_match:
                    type_id = type_match.group(1)
                    break
        if type_id:
            while True:
                found = True
                for line in self.debug_info:
                    type_pattern = re.compile(rf"!{type_id} =.*baseType: !([\d]+)")
                    type_match = type_pattern.search(line)
                    if type_match:
                        if "DerivedType" in line:
                            type_id = type_match.group(1)
                            found = False
                        else:
                            basetype_id = type_match.group(1)
                        break
                    elif f"!{type_id} =" in line and "DICompositeType" in line:
                        # There is no baseType in DICompositeType
                        basetype_id = type_id
                        break
                if found:
                    # 1. Recursive search
                    # 2. Cannot find
                    break

        if basetype_id:
            for line in self.debug_info:
                composite_type_pattern = re.compile(rf"!{basetype_id}.*DICompositeType.*name: \"([\w]+)\"")
                composite_type_match = composite_type_pattern.search(line)
                if composite_type_match:
                    return composite_type_match.group(1)

        return None

    def read_target_list(self, input_target):
        print(f"[+] Read {input_target}")
        self.target_list = []
        self.target_index = {}

        self.fuzz_target_node = self.get_func_node_by_funcname(self.target_func, self.target_file)
        if not self.fuzz_target_node:
            print(f"Failed to get the node for {self.target_func}")
            sys.exit(1)
        fuzz_target_name = self.get_func_name(self.fuzz_target_node)

        with open(input_target, "r") as f:
            line = f.readline()
            while "-----" not in line:
                line = f.readline()
            # consume separator
            line = f.readline()

            while line:
                if len(line) > 0 and line[0] == '#':
                    line = f.readline()
                    continue
                if len(line.split(",")) < 5:
                    line = f.readline()
                    continue
                # filename, funcname, input_index, length_index, input_type
                if (line.split(",")[0], line.split(",")[1]) not in self.target_list:
                    self.target_list.append((line.split(",")[0], line.split(",")[1]))
                    target_node = self.get_func_node_by_funcname(
                        line.split(",")[1], line.split(",")[0]
                    )
                    if target_node:
                        target_func_name = self.get_func_name(target_node)
                        target_file_name = self.get_file_path_from_ll(target_func_name)
                        if target_file_name and target_func_name == fuzz_target_name:
                            self.analysis_targets.append((target_func_name, target_file_name))

                        self.target_index[(target_file_name, target_func_name)] = (
                            line.split(",")[2],
                            line.split(",")[3],
                        )
                line = f.readline()

        assert len(self.analysis_targets) > 0, "No target function found in the graph"
        print(f"[+] Read {input_target} Done. - {self.get_elapsed_time()}")

    def in_target_list(self, func_name, file_name):
        if not func_name or not file_name:
            return False
        for target_filename, target_funcname in self.target_list:
            if target_funcname != func_name:
                continue
            if file_name.endswith(target_filename):
                return True
        return False

    def get_root_threads(self, func_name, file_name):
        root_threads = []
        for root, elem in self.threads.items():
            if root[0] == self.target_func:
                continue
            if (func_name, file_name) in elem:
                root_threads.append(root)
        return root_threads

    def is_thread_root(self, func_name, file_name):
        if (func_name, file_name) in self.threads:
            return True
        return False

    def merge_thread(self, dst, src):
        self.threads[dst] = list(set(self.threads[dst]) | set(self.threads[src]))

    def del_thread(self, root):
        del self.threads[root]

    def get_file_path_from_ll(self, func_name):
        for func_id, func_match in self.define_func.items():
            if '@' + func_name not in func_match:
                continue

            if func_id not in self.DISubprogram:
                return None, None
            subprogram_file_id = self.DISubprogram[func_id]

            if subprogram_file_id not in self.DIFile:
                return None, None
            file_name, file_path = self.DIFile[subprogram_file_id]

            return os.path.join(file_path, file_name)

    def get_func_linkage_type_from_ll(self, func_name):
        for func_id, func_match in self.define_func.items():
            if '@' + func_name not in func_match:
                continue

            if "define internal" in func_match:
                return "internal"
            else:
                return "external"
        return None

    def get_func_argnum_from_ll(self, func_name):
        for func_id, func_match in self.define_func.items():
            if '@' + func_name not in func_match:
                continue

            argnum = 0
            for line in func_match.split("\n"):
                if "define" in line:
                    argnum = len(line.split(","))
                    break
            return argnum
        return 0

    def get_func_node_by_funcname(self, func_name, file_name):
        node_candidates = []
        for node in self.graph.get_node_list():
            shape = node.get_attributes()["shape"]
            if shape != "box":
                continue
            label = node.get_attributes()["label"]
            if f"fun: {func_name}" in label:
                node_candidates.append(node)

        target_node = None
        for node in node_candidates:
            node_func_name = self.get_func_name(node)
            node_file_name = self.get_file_path_from_ll(node_func_name)
            if node_func_name and node_func_name.split('.')[0] != func_name.split('.')[0]:
                continue
            if node_file_name and node_file_name.endswith(file_name):
                target_node = node

        return target_node

    def get_func_node_by_nodename(self, node_name):
        target_node = None
        for node in self.graph.get_node_list():
            if node.get_name() == node_name:
                target_node = node

        return target_node

    def get_func_name(self, node):
        label = node.get_attributes()["label"]
        if "fun: " not in label:
            return ""
        return label[label.index("fun: ") + 5 : label.index("\}")]

    def merge_scope(self, scope1, scope2):
        return list(set(scope1) | set(scope2))

    def get_parents(self, node, explicit=True):
        parents = []
        child_func_name = self.get_func_name(node)
        child_func_def, child_func_params_type, child_func_ll = self.find_function_ll(child_func_name)
        
        for edge in self.graph.get_edge_list():
            if edge.get_destination() != node.get_name():
                continue
            if explicit:
                if edge.get_attributes()["color"] == "black":
                    source = edge.get_source()
                    if ":" in source:
                        source = source.split(":")[0]
                    source_node = self.get_func_node_by_nodename(source)
                    parents.append(source_node)
            else:
                if edge.get_attributes()["color"] == "red":
                    source = edge.get_source()
                    if ":" in source:
                        source = source.split(":")[0]
                    source_node = self.get_func_node_by_nodename(source)

                    if userconfig.indirect_call_match:
                        # We need to check the prototype of the indirect call
                        parent_func_name = self.get_func_name(source_node)
                        parent_func_def, _, parent_func_ll = self.find_function_ll(parent_func_name)
                        
                        # Check if the indirect call is valid
                        indirect_call_line, indirect_call_params_type = self.find_indirect_call_ll(parent_func_ll)
                        if indirect_call_line:
                            if len(indirect_call_params_type) != len(child_func_params_type):
                                print(f"[-] Invalid indirect call: {parent_func_name} -> {child_func_name}")
                                print(f"[-]     Parent params: {indirect_call_params_type}")
                                print(f"[-]     Child params: {child_func_params_type}")
                                continue
                            valid = True
                            for i in range(len(indirect_call_params_type)):
                                if indirect_call_params_type[i] != child_func_params_type[i]:
                                    print(f"[-] Invalid indirect call: {parent_func_name} -> {child_func_name}")
                                    print(f"[-]     Parent params: {indirect_call_params_type}")
                                    print(f"[-]     Child params: {child_func_params_type}")
                                    valid = False
                                    break
                            if valid:
                                print(f"[+] Valid indirect call: {parent_func_name} -> {child_func_name}")
                                print(f"[+]     Parent params: {indirect_call_params_type}")
                                print(f"[+]     Child params: {child_func_params_type}")
                                parents.append(source_node)
                    else:
                        # We do not check the prototype of the indirect call
                        parents.append(source_node)
        return parents

    def get_children(self, node, explicit=True):
        children = []
        for edge in self.graph.get_edge_list():
            source = edge.get_source()
            if ":" in source:
                source = source.split(":")[0]
            if source != node.get_name():
                continue

            destination = edge.get_destination()

            func_name = self.get_func_name(self.get_func_node_by_nodename(destination))
            file_name = self.get_file_path_from_ll(destination)
            # Skip top functions
            if self.check_top_functions(func_name, file_name):
                continue

            if explicit:
                if edge.get_attributes()["color"] == "black":
                    destination_node = self.get_func_node_by_nodename(destination)
                    children.append(destination_node)
            else:
                if edge.get_attributes()["color"] == "red":
                    destination_node = self.get_func_node_by_nodename(destination)
                    children.append(destination_node)
        return children

    def forward_analysis(self, node, analyzed):
        current_node_scope = []
        current_node_func = self.get_func_name(node)
        current_node_file = self.get_file_path_from_ll(current_node_func)

        # Prevent recursion
        if (current_node_func, current_node_file) in analyzed:
            return []

        current_node_scope.append((current_node_func, current_node_file))

        implicit_children = self.get_children(node, explicit=False)
        for child in implicit_children:
            child_node_func = self.get_func_name(child)
            child_node_file = self.get_file_path_from_ll(child_node_func)

            if (
                (current_node_func, current_node_file),
                (child_node_func, child_node_file),
            ) not in self.indirect_links:
                self.indirect_links.append(
                    (
                        (current_node_func, current_node_file),
                        (child_node_func, child_node_file),
                    )
                )
            if (child_node_func, child_node_file) in self.analysis_targets:
                current_node_scope.append((child_node_func, child_node_file))

            # Is Forward analysis of the implicit call necessary?

        children = self.get_children(node, explicit=True)
        for child in children:
            child_node_func = self.get_func_name(child)
            child_node_file = self.get_file_path_from_ll(child_node_func)

            if (child_node_func, child_node_file) in self.scope_cache:
                child_node_scope = self.scope_cache[(child_node_func, child_node_file)]
            else:
                analyzed.append((current_node_func, current_node_file))
                child_node_scope = self.forward_analysis(child, analyzed)

            current_node_scope = self.merge_scope(current_node_scope, child_node_scope)
            analyzed = list(set(analyzed) | set(child_node_scope))

        self.scope_cache[(current_node_func, current_node_file)] = current_node_scope
        return current_node_scope

    def backward_analysis(self, node, depth=0):
        node_func_name = self.get_func_name(node)
        node_file_name = self.get_file_path_from_ll(node_func_name)

        self.func_depth[node_func_name] = depth

        # It is needed for multi entry fuzzing
        with open("ancestors", "a") as f:
            f.write(f"{node_func_name},{depth}\n")

        # If user specifies the top functions, do not traverse the call graph
        if self.check_top_functions(node_func_name, node_file_name):
            if (node_func_name, node_file_name) not in self.analysis_targets:
                self.analysis_targets.append((node_func_name, node_file_name))
            return

        # Direct call
        parents = self.get_parents(node, explicit=True)
        for parent in parents:
            parent_func_name = self.get_func_name(parent)
            parent_file_name = self.get_file_path_from_ll(parent_func_name)
            if not self.in_target_list(parent_func_name, parent_file_name):
                continue

            if (parent_func_name, parent_file_name) not in self.analysis_targets:
                print(f"[*] Add parent {parent_func_name} to analysis target")
                self.analysis_targets.append((parent_func_name, parent_file_name))
                self.backward_analysis(parent, depth = depth + 1)

        # Indirect call
        parents = self.get_parents(node, explicit=False)
        for parent in parents:
            parent_func_name = self.get_func_name(parent)
            parent_file_name = self.get_file_path_from_ll(parent_func_name)
            if not self.in_target_list(parent_func_name, parent_file_name):
                continue

            if (parent_func_name, parent_file_name) not in self.analysis_targets:
                print(f"[*] Add parent {parent_func_name} to analysis target")
                self.analysis_targets.append((parent_func_name, parent_file_name))
                self.indirect_links.append(
                    (
                        (parent_func_name, parent_file_name),
                        (node_func_name, node_file_name),
                    )
                )
                self.backward_analysis(parent, depth = depth + 1)

    def thread_analysis(self):
        fuzz_target_name = self.get_func_name(self.fuzz_target_node)
        for current_node, current_scope in self.scopes.items():
            # current_scopes are the explicitly callable functions
            current_node_func = current_node[0]
            current_node_file = current_node[1]

            roots = self.get_root_threads(current_node_func, current_node_file)

            # Entire function
            # print(f"[+] Create thread for {current_node_func}")
            # self.threads[current_node] = [current_node]

            # original
            if len(roots) == 0:
               print(f"[+] Create thread for {current_node_func}")
               self.threads[current_node] = [current_node]

            for scope in current_scope:
                if not scope[0] or not scope[1]:
                    continue

                if (scope[0], scope[1]) in self.implicit_scope_cache:
                    implicits = self.implicit_scope_cache[(scope[0], scope[1])]
                    # There couldn't be a existing thread
                    if len(roots) > 0:
                        for root in roots:
                            self.threads[root] = list(set(self.threads[root]) | set(implicits))
                    else:
                        self.threads[current_node] = list(set(self.threads[current_node]) | set(implicits))
                else:
                    self.implicit_scope_cache[(scope[0], scope[1])] = []

                    scope_node = self.get_func_node_by_funcname(scope[0], scope[1])
                    if not scope_node:
                        continue
                    implicits = self.get_children(scope_node, explicit=False)
                    for implicit in implicits:
                        implicit_func_name = self.get_func_name(implicit)
                        implicit_file_name = self.get_file_path_from_ll(implicit_func_name)
                        if not self.in_target_list(implicit_func_name, implicit_file_name):
                            continue

                        if implicit_func_name == current_node_func:
                            continue

                        self.implicit_scope_cache[(scope[0], scope[1])].append(
                            (implicit_func_name, implicit_file_name)
                        )

                        if (
                            (scope[0], scope[1]),
                            (implicit_func_name, implicit_file_name),
                        ) not in self.indirect_links:
                            self.indirect_links.append(
                                (
                                    (scope[0], scope[1]),
                                    (implicit_func_name, implicit_file_name),
                                )
                            )

                        # There could be a existing thread
                        if self.is_thread_root(implicit_func_name, implicit_file_name):
                            print(f"[*] Merge thread for {implicit_func_name}")
                            if len(roots) > 0:
                                for root in roots:
                                    self.merge_thread(root, (implicit_func_name, implicit_file_name))
                            else:
                                self.merge_thread(current_node, (implicit_func_name, implicit_file_name))

                            if len(roots) > 0 and (implicit_func_name, implicit_file_name) in roots:
                                roots.remove((implicit_func_name, implicit_file_name))

                            if implicit_func_name == fuzz_target_name:
                                print("[*] Do not remove fuzz target thread")
                            else:
                                # Entire function
                                self.del_thread((implicit_func_name, implicit_file_name))
                        else:
                            if len(roots) > 0:
                                for root in roots:
                                    if (implicit_func_name, implicit_file_name) not in self.threads[root]:
                                        self.threads[root].append((implicit_func_name, implicit_file_name))
                            else:
                                if current_node in self.threads:
                                    if (implicit_func_name, implicit_file_name) not in self.threads[current_node]:
                                        self.threads[current_node].append((implicit_func_name, implicit_file_name))
                                else:
                                    # It could be removed during the merge
                                    self.threads[current_node] = [current_node]
                                    self.threads[current_node].append((implicit_func_name, implicit_file_name))

    def thread_scope_analysis(self):
        for thread, children in self.threads.items():
            # Add the thread itself
            if thread not in self.scopes:
                continue
            self.threads_scope[thread] = self.scopes[thread]

            for child in children:
                if child not in self.scopes:
                    continue
                child_scope = self.scopes[child]
                for e in child_scope:
                    if e not in self.threads_scope[thread]:
                        self.threads_scope[thread].append(e)

    def reduce_thread_scope(self):
        fuzz_target_name = self.get_func_name(self.fuzz_target_node)

        child_scope = []
        for scope in self.threads_scope.keys():
            for k, v in self.threads_scope.items():
                if scope == k:
                    continue
                if k[0] == fuzz_target_name:
                    continue
                if (
                    scope in v
                    and scope not in child_scope
                    and scope[0] != fuzz_target_name
                ):
                    print(f"[*] Reduce scope for {scope}")
                    child_scope.append(scope)

        # Original
        for e in child_scope:
            del self.threads_scope[e]

        # We leave them for medium confidence calculation
        # with open("intermediate_funcs", "w") as f:
        #     for e in child_scope:
        #         #del self.threads_scope[e]
        #         f.write(f"{e[0]},{e[1]}")
        #         f.write("\n")

    def record_func_depth(self):
        with open("funcs_depth", "w") as f:
            for scope in self.threads_scope.keys():
                f.write(f"{scope[0]},{self.func_depth[scope[0]]}")
                f.write("\n")


    def explicit_analysis_single(self, func_name, file_name):
        analysis_node = self.get_func_node_by_funcname(func_name, file_name)
        if not analysis_node:
            return

        analysis_func_name = self.get_func_name(analysis_node)
        analysis_file_name = self.get_file_path_from_ll(analysis_func_name)

        print(f"[+] Analyzing {analysis_func_name} in {analysis_file_name}")
        if (analysis_func_name, analysis_file_name) not in self.scopes:
            self.scopes[(analysis_func_name, analysis_file_name)] = (
                self.forward_analysis(analysis_node, [])
            )

    def analyze(self):
        self.backward_analysis(self.fuzz_target_node)
        print("========================================")
        print("[+] Forward analysis start")
        for target in self.analysis_targets:
            funcname = target[0]
            filename = target[1]
            self.explicit_analysis_single(funcname, filename)
        print(f"[+] Forward analysis end - elapsed time: {self.get_elapsed_time()} seconds")

        print("========================================")
        print("[+] Thread analysis start")
        self.thread_analysis()
        self.thread_scope_analysis()
        self.reduce_thread_scope()
        print(f"[+] Thread analysis end - elapsed time: {self.get_elapsed_time()} seconds")

        print("========================================")
        for thread, scope in self.threads_scope.items():
            indirect_links = []
            for link in self.indirect_links:
                parent = link[0]
                child = link[1]
                if parent in scope and child in scope:
                    linkage = self.get_func_linkage_type_from_ll(child[0])
                    argnum = self.get_func_argnum_from_ll(child[0])
                    indirect_links.append((parent, child, linkage, argnum))
            CFGWriter.write_config(
                thread, scope, self.target_index, indirect_links, self.func_depth
            )


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analyze the control flow graph for the target functions"
    )
    parser.add_argument('-d', '--dot', help="Input dot file", required=True)
    parser.add_argument('-l', '--ll', help="Input llvm ll file", required=True)
    parser.add_argument('-t', '--target', help="Target list", required=True)
    parser.add_argument('--function', help="Target function", required=True)
    parser.add_argument('--file', help="Target file", required=True)

    args = parser.parse_args()

    cfg = CFG(args.dot, args.ll, args.target, args.function, args.file)
    cfg.analyze()
