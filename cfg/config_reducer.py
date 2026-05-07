#!/usr/bin/env python3
import os
import re
import sys
import datetime
import argparse
import pydot


class ConfigReducer:
    def __init__(self, input_dot, input_ll, input_target, output_file):
        self.start_time = datetime.datetime.now()

        # Read from llvm ll
        self.DIFile = {}
        self.DISubprogram = {}
        self.define_func = {}

        self.analysis_targets = []
        self.indirect_links = []

        self.read_input_graph(input_dot)
        self.read_llvm_ll(input_ll)
        self.read_target_list(input_target)

        self.output_file = output_file

        self.scopes = {}

        self.scope_cache = {}
        self.implicit_scope_cache = {}

    def get_elapsed_time(self):
        elapsed_time = datetime.datetime.now() - self.start_time
        return elapsed_time

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

        print(f"[+] Read {input_ll} Done. - {self.get_elapsed_time()}")

    def read_target_list(self, input_target):
        print(f"[+] Read {input_target}")
        self.target_list = []
        self.target_index = {}
        self.target_type = {}

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
                        if target_file_name:
                            self.analysis_targets.append((target_func_name, target_file_name))

                        self.target_index[(target_file_name, target_func_name)] = (
                            line.split(",")[2],
                            line.split(",")[3],
                        )

                        self.target_type[(target_file_name, target_func_name)] = line.split(",")[4].strip()
                line = f.readline()

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

    def get_file_path_from_ll(self, func_name):
        for func_id, func_match in self.define_func.items():
            if func_name not in func_match:
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
            if func_name not in func_match:
                continue

            if "define internal" in func_match:
                return "internal"
            else:
                return "external"
        return None

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
            if explicit:
                if edge.get_attributes()["color"] == "black":
                    destination = edge.get_destination()
                    destination_node = self.get_func_node_by_nodename(destination)
                    children.append(destination_node)
            else:
                if edge.get_attributes()["color"] == "red":
                    destination = edge.get_destination()
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

    def reduce_thread_scope(self):
        child_scope = []
        for scope, children in self.scopes.items():
            # The function which is usually not a target
            func_name = scope[0]
            if (
                "init" in func_name
                or "encode" in func_name
                or "decode" in func_name
                or "write" in func_name
                or "_tx" in func_name
                or "send" in func_name
                or "unref" in func_name
                or "free" in func_name
                or "fill" in func_name
                or "put" in func_name
            ):
                # Remove the child as well
                for e in children:
                    if e not in child_scope:
                        child_scope.append(e)

                # if scope not in child_scope:
                #     child_scope.append(scope)
                #     continue

            # for k, v in self.scopes.items():
            #     if scope == k:
            #         continue

            #     if scope in v and scope not in child_scope:
            #         child_scope.append(scope)

        for e in child_scope:
            if e in self.scopes:
                print(f"[+] Remove {e} from scope")
                del self.scopes[e]

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

    def write_reduced_config(self):
        with open(self.output_file, "w") as f:
            f.write("Reduced Config\n")
            f.write("--------------------------------------------------\n")
            for scope in self.scopes:
                func_name = scope[0]
                file_name = scope[1]
                input_index = self.target_index[(file_name, func_name)][0]
                length_index = self.target_index[(file_name, func_name)][1]
                input_type = self.target_type[(file_name, func_name)]

                f.write(f"{file_name},{func_name.split('.')[0]},{input_index},{length_index},{input_type}\n")

    def analyze(self):
        print("========================================")
        print("[+] Forward analysis start")
        for target in self.analysis_targets:
            funcname = target[0]
            filename = target[1]
            self.explicit_analysis_single(funcname, filename)
        print(f"[+] Forward analysis end - elapsed time: {self.get_elapsed_time()} seconds")

        print("========================================")
        print("[+] Reduce config start")
        self.reduce_thread_scope()
        print(f"[+] Reduce config end - elapsed time: {self.get_elapsed_time()} seconds")
        after_reduce = len(self.scopes)

        print("========================================")
        print(f"[+] Total {after_reduce} functions are in the scope")
        self.write_reduced_config()


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Analyze the control flow graph for the target functions"
    )
    parser.add_argument('-d', '--dot', help="Input dot file", required=True)
    parser.add_argument('-l', '--ll', help="Input llvm ll file", required=True)
    parser.add_argument('-t', '--target', help="Target list", required=True)
    parser.add_argument('-o', '--output', help="Output file", required=True)

    args = parser.parse_args()

    cfg = ConfigReducer(args.dot, args.ll, args.target, args.output)
    cfg.analyze()
