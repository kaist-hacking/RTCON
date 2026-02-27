#!/bin/bash

if [ "$#" -ne 5 ]; then
    echo "Usage: $0 <application> <file_path> <function_name> <test_func_idx> <cross_fuzz>"
    echo "       application: [App, Cell-intf, Cell-mod, TCP]"
    exit 1
fi

APPLICATION=$1
FILE_PATH=$2
FUNC_NAME=$3
TEST_FUNC_IDX=$4
CROSS_FUZZ=$5

export TARGET_FUNC_NAME=${FUNC_NAME}

if [ "$APPLICATION" == "App" ]; then
    APP_DIR=Application_Protocols_Fuzz
elif [ "$APPLICATION" == "Cell-intf" ]; then
    APP_DIR=FreeRTOS_Plus_Cellular_Interface_Fuzz
elif [ "$APPLICATION" == "Cell-mod" ]; then
    APP_DIR=FreeRTOS_Plus_Cellular_Modules_Fuzz
elif [ "$APPLICATION" == "TCP" ]; then
    APP_DIR=FreeRTOS_Plus_TCP_Fuzz
else
    echo "Invalid application: $APPLICATION"
    exit 1
fi

# echo "[INFO] Replacing test function with ${TEST_FUNC_IDX}"
# python3 test_selector.py ${APP_DIR} ${TEST_FUNC_IDX}

# Change to application directory
cd ${APP_DIR}

if [ ! -f "/configs/freertos-${APPLICATION,,}-func-list-reduced.txt" ]; then
    echo "[INFO] Reducing the function list"
    python3 /source/cfg/config_reducer.py --dot tcg.dot --ll whole_project.ll --target /configs/freertos-${APPLICATION,,}-full-func-list.txt --output /configs/freertos-${APPLICATION,,}-func-list-reduced.txt
fi

echo "[INFO] Analyzing CFG for ${FUNC_NAME}"
python3 /source/cfg/analyze_cfg.py --dot tcg.dot --ll whole_project.ll --target /configs/freertos-${APPLICATION,,}-func-list-reduced.txt --function ${FUNC_NAME} --file ${FILE_PATH}

# Build the target
echo "[INFO] Building fuzzing target"
if [ "$CROSS_FUZZ" = "true" ]; then
  python3 build_fuzzer.py
else
  python3 build_fuzzer.py ${FUNC_NAME}
fi

exec "/bin/bash"