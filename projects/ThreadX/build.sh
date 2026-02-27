#!/bin/bash

if [ "$#" -ne 4 ]; then
    echo "Usage: $0 <file_path> <function_name> <test_func_idx> <cross_fuzz>"
    exit 1
fi

FILE_PATH=$1
FUNC_NAME=$2
TEST_FUNC_IDX=$3
CROSS_FUZZ=$4

export TARGET_FUNC_NAME=${FUNC_NAME}

# replace test function ahead of compilation
echo "[INFO] Replacing test function with ${TEST_FUNC_IDX}"
python3 test_selector.py ${TEST_FUNC_IDX}

# Change to fuzz directory
cd fuzz

if [ ! -f "/configs/threadx-func-list-reduced.txt" ]; then
    echo "[INFO] Reducing the function list"
    python3 /source/cfg/config_reducer.py --dot tcg.dot --ll whole_project.ll --target /configs/threadx-full-func-list.txt --output /configs/threadx-func-list-reduced.txt
fi

echo "[INFO] Analyzing CFG for ${FUNC_NAME}"
python3 /source/cfg/analyze_cfg.py --dot tcg.dot --ll whole_project.ll --target /configs/threadx-func-list-reduced.txt --function ${FUNC_NAME} --file ${FILE_PATH}

# Some error in pass
sed -i '/\/\* Taint all arguments \*\//,+3d' /source/pass/Pass.cpp
cd /source && cmake --build build && cd $OLDPWD

# Build the target
echo "[INFO] Building fuzzing target"
if [ "$CROSS_FUZZ" = "true" ]; then
  python3 build_fuzzer.py
else
  python3 build_fuzzer.py ${FUNC_NAME}
fi

exec "/bin/bash"