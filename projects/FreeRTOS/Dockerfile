FROM cgcc-base

ARG FREERTOS_ROOT
ARG TAINT_CONFIGURATION_FILE

ENV FREERTOS_ROOT=${FREERTOS_ROOT}
ENV TAINT_CONFIGURATION_FILE=${TAINT_CONFIGURATION_FILE}

COPY skel /source/skel
COPY projects/FreeRTOS /source/projects/FreeRTOS
COPY cfg /source/cfg

# Build FreeRTOS
WORKDIR /source/projects/FreeRTOS

RUN mkdir -p FreeRTOS_latest && cd FreeRTOS_latest && \
    git init && \
    git remote add origin https://github.com/FreeRTOS/FreeRTOS.git && \
    git fetch --depth=1 --recurse-submodules --no-tags origin 59f1c570cfef3eeea6a554a14b9ada0c970cafe5 && \
    git reset --hard 59f1c570cfef3eeea6a554a14b9ada0c970cafe5 && \
    git submodule update --init --recursive && \
    apt install -y libpcap-dev

# RUN git clone https://github.com/FreeRTOS/FreeRTOS.git --depth=1 --recurse-submodules FreeRTOS_latest && \
#     cd FreeRTOS_latest && \
#     git submodule update --init --recursive && \
#     apt install -y libpcap-dev

RUN cp /source/skel/crash.* FreeRTOS_Plus_Cellular_Interface_Fuzz && \
    cp /source/skel/hook.* FreeRTOS_Plus_Cellular_Interface_Fuzz && \
    cp /source/skel/hashmap.* FreeRTOS_Plus_Cellular_Interface_Fuzz && \
    cp /source/skel/fuzz.* FreeRTOS_Plus_Cellular_Interface_Fuzz && \
    cp /source/skel/crash.* FreeRTOS_Plus_Cellular_Modules_Fuzz && \
    cp /source/skel/hook.* FreeRTOS_Plus_Cellular_Modules_Fuzz && \
    cp /source/skel/hashmap.* FreeRTOS_Plus_Cellular_Modules_Fuzz && \
    cp /source/skel/fuzz.* FreeRTOS_Plus_Cellular_Modules_Fuzz && \
    cp /source/skel/crash.* Application_Protocols_Fuzz && \
    cp /source/skel/hook.* Application_Protocols_Fuzz && \
    cp /source/skel/hashmap.* Application_Protocols_Fuzz && \
    cp /source/skel/fuzz.* Application_Protocols_Fuzz && \
    cp /source/skel/crash.* FreeRTOS_Plus_TCP_Fuzz && \
    cp /source/skel/hook.* FreeRTOS_Plus_TCP_Fuzz && \
    cp /source/skel/hashmap.* FreeRTOS_Plus_TCP_Fuzz && \
    cp /source/skel/fuzz.* FreeRTOS_Plus_TCP_Fuzz && \
    ln -s /usr/bin/clang-18 /usr/bin/clang && \
    ln -s /usr/bin/clang++-18 /usr/bin/clang++ && \
    ln -s /SVF/llvm-16.0.0.obj/bin/clang /usr/bin/clang-16 && \
    ln -s /SVF/llvm-16.0.0.obj/bin/llvm-link /usr/bin/llvm-link

# Static analysis (Each fuzz target should be built with the same configuration)
RUN cd FreeRTOS_Plus_Cellular_Interface_Fuzz && \
    python3 /source/cfg/create_compile_commands.py freertos && \
    python3 /source/cfg/analyze_mta.py compile_commands.json
RUN cd FreeRTOS_Plus_Cellular_Modules_Fuzz && \
    python3 /source/cfg/create_compile_commands.py freertos && \
    python3 /source/cfg/analyze_mta.py compile_commands.json
RUN cd Application_Protocols_Fuzz && \
    python3 /source/cfg/create_compile_commands.py freertos && \
    python3 /source/cfg/analyze_mta.py compile_commands.json
RUN cd FreeRTOS_Plus_TCP_Fuzz && \
    python3 /source/cfg/create_compile_commands.py freertos && \
    python3 /source/cfg/analyze_mta.py compile_commands.json

RUN chmod +x build.sh
ENTRYPOINT [ "./build.sh" ]