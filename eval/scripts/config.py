import os
import sys
import logging

def usage():
    logging.warning(f"Usage1: {sys.argv[0]} <target>")
    logging.warning(f"Usage2: {sys.argv[0]} <target> <subsystem>")
    logging.warning(f"Example1: {sys.argv[0]} RIOT")
    logging.warning(f"Example2: {sys.argv[0]} FreeRTOS App")
    logging.warning("Targets: RIOT, Zephyr, FreeRTOS, ThreadX")
    logging.warning("         FuzzSlice-RIOT, FuzzSlice-Zephyr, FuzzSlice-FreeRTOS, FuzzSlice-ThreadX")
    logging.warning("         Manual-RIOT, Manual-FreeRTOS, Manual-ThreadX, Manual-Zephyr")
    logging.warning("         Ablation-RIOT, Ablation-FreeRTOS, Ablation-Zephyr, Ablation-ThreadX")
    logging.warning("         FuzzGen-RIOT, FuzzGen-Zephyr, FuzzGen-FreeRTOS, FuzzGen-ThreadX")
    logging.warning(f"Subsystems (FreeRTOS): App, Cell-intf, Cell-mod, TCP")
    sys.exit(1)

def check_usage():
    if len(sys.argv) == 3:
        if (
            (sys.argv[1] != "FreeRTOS")
            and (sys.argv[1] != "FuzzSlice-FreeRTOS")
            and (sys.argv[1] != "Manual-FreeRTOS")
            and (sys.argv[1] != "Ablation-FreeRTOS")
            and (sys.argv[1] != "FuzzGen-FreeRTOS")
        ):
            usage()
        if sys.argv[2] not in ["App", "Cell-intf", "Cell-mod", "TCP"]:
            usage()
    elif len(sys.argv) !=2:
        usage()

    if sys.argv[1] not in [
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
        "FuzzGen-Zephyr",
        "FuzzGen-FreeRTOS",
        "FuzzGen-ThreadX",
    ]:
        usage()

def get_workdir(target, subsystem):
    base_workdir = "/source/projects/"
    fuzzslice_workdir = "/StaticSlicer/test_lib/"
    manual_workdir = "/source/projects/"
    ablation_workdir = "/source/projects/"
    fuzzgen_workdir = "/source/projects/"
    if target == "FreeRTOS":
        if subsystem == "App":
            return os.path.join(base_workdir, target, "Application_Protocols_Fuzz")
        elif subsystem == "Cell-intf":
            return os.path.join(base_workdir, target, "FreeRTOS_Plus_Cellular_Interface_Fuzz")
        elif subsystem == "Cell-mod":
            return os.path.join(base_workdir, target, "FreeRTOS_Plus_Cellular_Modules_Fuzz")
        elif subsystem == "TCP":
            return os.path.join(base_workdir, target, "FreeRTOS_Plus_TCP_Fuzz")
        else:
            assert False
    elif target == "Zephyr":
        return os.path.join(base_workdir, target, "zephyr_latest", "fuzz")
    elif target == "RIOT":
        return os.path.join(base_workdir, target)
    elif target == "ThreadX":
        return os.path.join(base_workdir, target, "fuzz")
    ########################################
    # FuzzSlice
    elif target == "FuzzSlice-FreeRTOS":
        return os.path.join(fuzzslice_workdir, "freertos", "build")
    elif target == "FuzzSlice-Zephyr":
        return os.path.join(fuzzslice_workdir, "zephyr", "fuzz")
    elif target == "FuzzSlice-RIOT":
        return "/riot_fuzzslice"
    elif target == "FuzzSlice-ThreadX":
        return os.path.join(fuzzslice_workdir, "threadx", "build")
    ########################################
    # Manual
    elif target == "Manual-FreeRTOS":
        if subsystem == "App":
            return os.path.join(manual_workdir, "FreeRTOS", "Application_Protocols_Fuzz")
        elif subsystem == "Cell-intf":
            return os.path.join(manual_workdir, "FreeRTOS", "FreeRTOS_Plus_Cellular_Interface_Fuzz")
        elif subsystem == "Cell-mod":
            return os.path.join(manual_workdir, "FreeRTOS", "FreeRTOS_Plus_Cellular_Modules_Fuzz")
        elif subsystem == "TCP":
            return os.path.join(manual_workdir, "FreeRTOS", "FreeRTOS_Plus_TCP_Fuzz")
        else:
            assert False
    elif target == "Manual-Zephyr":
        return os.path.join(manual_workdir, "Zephyr", "zephyr_latest", "fuzz")
    elif target == "Manual-RIOT":
        return os.path.join(manual_workdir, "RIOT")
    elif target == "Manual-ThreadX":
        return os.path.join(manual_workdir, "ThreadX", "fuzz")
    ########################################
    # Ablation
    elif target == "Ablation-FreeRTOS":
        if subsystem == "App":
            return os.path.join(ablation_workdir, "FreeRTOS", "Application_Protocols_Fuzz")
        elif subsystem == "Cell-intf":
            return os.path.join(ablation_workdir, "FreeRTOS", "FreeRTOS_Plus_Cellular_Interface_Fuzz")
        elif subsystem == "Cell-mod":
            return os.path.join(ablation_workdir, "FreeRTOS", "FreeRTOS_Plus_Cellular_Modules_Fuzz")
        elif subsystem == "TCP":
            return os.path.join(ablation_workdir, "FreeRTOS", "FreeRTOS_Plus_TCP_Fuzz")
        else:
            assert False
    elif target == "Ablation-Zephyr":
        return os.path.join(ablation_workdir, "Zephyr", "zephyr_latest", "fuzz")
    elif target == "Ablation-RIOT":
        return os.path.join(ablation_workdir, "RIOT")
    elif target == "Ablation-ThreadX":
        return os.path.join(ablation_workdir, "ThreadX", "fuzz")
    ########################################
    # FuzzGen
    elif target == "FuzzGen-FreeRTOS":
        if subsystem == "App":
            return os.path.join(fuzzgen_workdir, "FreeRTOS", "Application_Protocols_Fuzz")
        elif subsystem == "TCP":
            return os.path.join(fuzzgen_workdir, "FreeRTOS", "FreeRTOS_Plus_TCP_Fuzz")
        else:
            assert False
    elif target == "FuzzGen-Zephyr":
        return os.path.join(fuzzgen_workdir, "Zephyr", "zephyr_latest", "fuzz")
    elif target == "FuzzGen-RIOT":
        return os.path.join(fuzzgen_workdir, "RIOT")
    elif target == "FuzzGen-ThreadX":
        return os.path.join(fuzzgen_workdir, "ThreadX", "fuzz", "build")
    else:
        assert False

# TIME = 24*60*60 # 24 hours
TIME = 24*60*60 # 24 hours
POOL_SIZE = 92

MEM_LIMIT = "4g"
CPU_LIMIT = 1

CPU_OFFSET = 0
MAX_ALLOC_PER_CPU = 1

CROSS_FUZZ = False

cov_port_dict = {
    "FreeRTOS": {
        "App": 50000,
        "Cell-intf": 50001,
        "Cell-mod": 50002,
        "TCP": 50003,
    },
    "Manual-FreeRTOS": {
        "App": 50100,
        "Cell-intf": 50101,
        "Cell-mod": 50102,
        "TCP": 50103,
    },
    "RIOT": 51000,
    "Manual-RIOT": 51100,
    "ThreadX": 52000,
    "Manual-ThreadX": 52100,
    "Zephyr": 53000,
    "Manual-Zephyr": 53100,
}
