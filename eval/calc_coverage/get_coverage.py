#!/usr/bin/env python3
import os
import sys


def parse_statistic(element):
    element = element.strip()
    coverage = element.split(" ")[1]

    coverage_percent = element.split(" ")[0].split("%")[0]
    coverage_percent = float(coverage_percent) if coverage_percent != "-" else 0.0

    total_line = int(coverage[1:-1].split("/")[1])
    visit_line = int(coverage[1:-1].split("/")[0])

    return coverage_percent, visit_line, total_line


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python3 get_coverage.py <file>")
        sys.exit(1)
    file_name = sys.argv[1]
    if not os.path.exists(file_name):
        print(f"Error: {file_name} does not exist")
        sys.exit(1)

    total_covs = []
    with open(file_name, "r") as f:
        content = f.read()
        lines = content.split("\n")
        for line in lines:
            columns = line.split("\t")
            file_name = columns[0]
            func_cov = parse_statistic(columns[1])
            line_cov = parse_statistic(columns[2])
            region_cov = parse_statistic(columns[3])
            branch_cov = parse_statistic(columns[4])

            total_covs.append((file_name, func_cov, line_cov, region_cov, branch_cov))

    # Calculate average line coverage
    total_files = len(total_covs)
    total_line_coverage = 0
    for _, _, line_cov, _, _ in total_covs:
        total_line_coverage += line_cov[0]

    average_line_coverage = total_line_coverage / total_files
    print(f"Average Line coverage: {average_line_coverage:.2f}%")

    # Calulate overall line coverage
    total_lines = 0
    visited_lines = 0
    for _, func_cov, line_cov, _, _ in total_covs:
        visited_lines += line_cov[1]
        total_lines += line_cov[2]

    line_coverage = (visited_lines / total_lines) * 100
    print(f"Overall Line coverage: {line_coverage:.2f}% ({visited_lines} / {total_lines})")
