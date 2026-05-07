# RTCon

## Overview

`RTCon` is a tool that performs function-level fuzzing on RTOSes.

## ⚠️ To use `RTCon` for testing general libraries, switch to the `general` branch.
```
git fetch origin
git checkout -b general
```

Currently, it supports 4 RTOSes:

- `Zephyr`
- `RIOT`
- `FreeRTOS`
- `ThreadX`

## One-pass build script

This script builds the Docker image and compiles the fuzzer for the target function in a single step.

**Note1**: It takes a long time (60 minutes) to build the docker image and the fuzzer. Downloading LLVM and RTOS project can be unstable and takes a lot of time.

**Note2**: It might fail to build the docker image while downloading and installing the dependencies for the RTOSes.

```bash
./build.sh <Project> <File> <Function> <test_type> <Multi-level fuzzing enable>

# e.g., To fuzz ble_hs_hci_evt_le_ext_adv_rpt in ble_hs_hci_evt.c for RIOT
./build.sh riot /source/projects/RIOT/riot_latest/build/pkg/nimble/nimble/host/src/ble_hs_hci_evt.c ble_hs_hci_evt_le_ext_adv_rpt 0 true

# Zephyr takes much longer time to build
# e.g., To fuzz bt_hci_le_adv_ext_report in scan.c for Zephyr
# ./build.sh zephyr /source/projects/Zephyr/zephyr_latest/zephyr/subsys/bluetooth/host/scan.c bt_hci_le_adv_ext_report 1 true
```

## Run fuzzer

```bash
# Inside the docker container

# Option 1
# Run the naive fuzzer (Note: may produce extensive ASAN reports)
# /tmp/result will store the result of the fuzzer
# Ctrl-C to stop the fuzzer
mkdir corpus && ./host_bin/riot-ble_hs_hci_evt_le_ext_adv_rpt-fuzz corpus/ &>/tmp/result

# Option 2
# Without multi-level fuzzing
# Run the fuzzer with scripts with 30 seconds timeout
python3 /scripts/run_fuzz_single.py ble_hs_hci_evt_le_ext_adv_rpt 30

# Option 3
# With multi-level fuzzing
# Run the fuzzer with scripts with 300 seconds timeout
# Each target function will be fuzzed for an equal share of the time (e.g., 100 seconds each for 3 functions)
python3 /scripts/run_fuzz_cross.py 300

```

## Check the result

```bash
# You can find the crash result in `crash_dir/crash_<function_name>`
cat crash_dir/crash_ble_hs_hci_evt_le_ext_adv_rpt/high/ble_hs_hci_evt_le_ext_adv_rpt/0xea44c42839e0/report.txt

depth: 0
=================================================================
==4073==ERROR: AddressSanitizer: heap-buffer-overflow on address 0x5020000557c9 at pc 0x00000076f745 bp 0x0000011d2e50 sp 0x0000011d2e48
READ of size 1 at 0x5020000557c9 thread T0
    #0 0x76f744 in ble_hs_hci_evt_le_ext_adv_rpt /source/projects/RIOT/riot_latest/build_ble_hs_hci_evt_le_ext_adv_rpt/pkg/nimble/nimble/host/src/ble_hs_hci_evt.c:582:36
    #1 0x7819b3 in fuzzEntryFunction (/source/projects/RIOT/host_bin/topriot-ble_hs_hci_evt_le_ext_adv_rpt-fuzz+0x7819b3) (BuildId: f4eddf48d7483ecd66ec06c2b15840060a70142c)
```

```bash
# Multi-level fuzzing result
# You can find the result in `crash_dir`

cat crash_dir/high_confidence

Total crashes: 5
=====
Bug: heap-buffer-overflow, 1.0
#0 ble_hs_hci_evt_le_ext_adv_rpt /source/projects/RIOT/riot_latest/build/pkg/nimble/nimble/host/src/ble_hs_hci_evt.c:556:31
#1 fuzzEntryFunction (/source/projects/RIOT/host_bin/riot-ble_hs_hci_evt_le_ext_adv_rpt-fuzz+0x7819b3)
#2 testRawData /source/projects/RIOT/appfuzz/fuzz.c:50:3
=====
Bug: heap-buffer-overflow, 1.0
#0 ble_hs_hci_evt_le_ext_adv_rpt /source/projects/RIOT/riot_latest/build/pkg/nimble/nimble/host/src/ble_hs_hci_evt.c:566:28
#1 fuzzEntryFunction (/source/projects/RIOT/host_bin/riot-ble_hs_hci_evt_le_ext_adv_rpt-fuzz+0x7819b3)
#2 testRawData /source/projects/RIOT/appfuzz/fuzz.c:50:3

```

## Manual build docker image

We recommend using Docker to build the project.

You need to build the base image and the docker image for each RTOS.

**Note**: Building the docker image may take a long time. Downloading LLVM and RTOS project can be unstable and takes a lot of time.

```bash
# Build the base image
docker-compose build base
```

To build the docker image for each RTOS, run
```bash
# Build the docker image for Zephyr
docker-compose build zephyr

# Build the docker image for RIOT
docker-compose build riot

# Build the docker image for FreeRTOS
docker-compose build freertos TCP

# Build the docker image for ThreadX
docker-compose build threadx
```

**Note:** It might fail to build the docker image while downloading and installing the dependencies for the RTOSes.

### Build fuzzer

To distinguish the static function with the same name across different files, we need to specify the file name.

You can build the fuzzer by using the following command:

```bash
docker-compose run <Project> <File> <Function> <test_type> <Multi-level fuzzing enable>

# e.g., To fuzz bt_hci_le_adv_ext_report in scan.c for Zephyr
# test_type: 0 - raw, 1 - net_buf
docker-compose run zephyr /source/projects/Zephyr/zephyr_latest/zephyr/subsys/bluetooth/host/scan.c bt_hci_le_adv_ext_report 1 true
```

`RTCon` will generate the fuzzer for the target function.

### Target function list

The target functions tested by `RTCon` are listed in `eval/config/<RTOS>-func-list.txt`.

Each file must contain at least one entry specifying a target function with the following format:

```txt
# Format:
# <file>,<function>,<input_index>,<length_index>,<input_type>

# Example:
# To test the function at_parse_result in at.c, include the following line:
/source/projects/Zephyr/zephyr_latest/zephyr/subsys/bluetooth/host/classic/at.c,at_parse_result,1,-1,1
```

### Bulk evalutaion

We provide a script to run the fuzzing for all the target functions in the target function list file.

To use it, navigate to `eval/scripts` and run:

```bash
# Format:
# ./run_eval.py <Project> [Subsystem]

# Zephyr
./run_eval.py zephyr

# RIOT
./run_eval.py riot

# FreeRTOS
./run_eval.py freertos TCP

# ThreadX
./run_eval.py threadx
```

You can specify the detail configuration of evaluation in `eval/config/config.py`.

Currently, the maximum number of cores is set to 92 and the timeout is set to 24 hours.

Also, you can specify the multi-level fuzzing enable or disable.

```python
TIME = 24*60*60 # 24 hours
POOL_SIZE = 92

MEM_LIMIT = "4g"
CPU_LIMIT = 1

CPU_OFFSET = 0
MAX_ALLOC_PER_CPU = 1

CROSS_FUZZ = True
```

You can find the result in `out/` directory and to see the result, run:

```bash
python3 view_result.py <Project>

# Zephyr
python3 view_result.py zephyr
```
