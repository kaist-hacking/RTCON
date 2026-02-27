FROM cgcc-base

ARG ZEPHYR_ROOT
ARG TAINT_CONFIGURATION_FILE

ENV ZEPHYR_ROOT=${ZEPHYR_ROOT}
ENV TAINT_CONFIGURATION_FILE=${TAINT_CONFIGURATION_FILE}

# Zephyr SDK
RUN cd /opt && wget https://github.com/zephyrproject-rtos/sdk-ng/releases/download/v0.16.8/zephyr-sdk-0.16.8_linux-x86_64.tar.xz
# COPY projects/Zephyr/zephyr-sdk-0.16.8_linux-x86_64.tar.xz /opt/zephyr-sdk-0.16.8_linux-x86_64.tar.xz
RUN cd /opt && tar -xf zephyr-sdk-0.16.8_linux-x86_64.tar.xz

# Zephyr
WORKDIR ${ZEPHYR_ROOT}/zephyr
RUN git init && \
    git remote add origin https://github.com/zephyrproject-rtos/zephyr && \
    git fetch --depth=1 --no-tags origin bdca41d0bf9863e83f09b9713b4c24d672d2cf8e && \
    git reset --hard bdca41d0bf9863e83f09b9713b4c24d672d2cf8e
RUN west init -l ${ZEPHYR_ROOT}/zephyr && \
    cd ${ZEPHYR_ROOT} && \
    west update -n -o=--depth=1
RUN pip3 install -r ${ZEPHYR_ROOT}/zephyr/scripts/requirements.txt

ENV ZEPHYR_TOOLCHAIN_VARIANT=llvm
RUN cd /opt/zephyr-sdk-0.16.8 && \
    ./setup.sh -t all -h

# Zephyr Fuzz
COPY skel /source/skel
COPY projects/Zephyr/fuzz /source/projects/Zephyr/fuzz
COPY cfg /source/cfg
RUN mv /source/projects/Zephyr/fuzz ${ZEPHYR_ROOT} && \
    cd ${ZEPHYR_ROOT}/fuzz/src && \
    cp /source/skel/crash.* . && \
    cp /source/skel/hook.* . && \
    cp /source/skel/hashmap.* . && \
    cp /source/skel/fuzz.* . && \
    ln -s /usr/bin/clang-18 /usr/bin/clang && \
    ln -s /usr/bin/llvm-link-18 /usr/bin/llvm-link

# Apply patch to build Zephyr
RUN cd ${ZEPHYR_ROOT}/fuzz/src && \
    ./apply_patch.sh

WORKDIR ${ZEPHYR_ROOT}/fuzz

# Static analysis (Each fuzz target should be built with the same configuration)
RUN python3 /source/cfg/create_compile_commands.py zephyr && \
    python3 /source/cfg/analyze_mta.py compile_commands.json

RUN chmod +x ./build.sh
ENTRYPOINT [ "./build.sh" ]