FROM ubuntu:22.04

RUN apt update && apt install -y wget && \
    echo 'deb http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main' >> /etc/apt/sources.list && \
    echo 'deb-src http://apt.llvm.org/jammy/ llvm-toolchain-jammy-18 main' >> /etc/apt/sources.list
RUN wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key | tee /etc/apt/trusted.gpg.d/apt.llvm.org.asc

# Install dependencies
RUN apt update && \
    DEBIAN_FRONTEND=noninteractive TZ=Etc/UTC apt install -y git cargo \
    g++ libz3-dev ninja-build python3-pip zlib1g-dev wget vim gdb gperf \
    ccache dfu-util device-tree-compiler python3-dev python3-pip \
    python3-setuptools python3-tk python3-wheel xz-utils file make gcc \
    gcc-multilib g++-multilib libsdl2-dev libmagic1 udev zip libz-dev \
    libssl-dev llvm-18 libboost-all-dev clang-18 liblld-18-dev \
    llvm-18-linker-tools bear
RUN pip3 install west pyelftools pydot tqdm

# CMake
RUN cd /tmp && wget https://github.com/Kitware/CMake/releases/download/v3.30.3/cmake-3.30.3.tar.gz && \
    tar -xf cmake-3.30.3.tar.gz && \
    cd cmake-3.30.3 && ./bootstrap && make -j$(nproc) && make install

# SVF
RUN cd / && git clone https://github.com/SVF-tools/SVF.git && \
    cd SVF && git checkout a68b29388634f6f2c88751ffcd3d500f10353930 && \
    chmod +x build.sh && ./build.sh
ENV PATH=/SVF/Release-build/bin:$PATH

WORKDIR /source

# Build pass
COPY pass /source/pass
RUN cmake -G Ninja -B build -S pass \
    -DLLVM_DIR=/usr/lib/llvm-18/lib/cmake/llvm \
    -DCMAKE_CXX_COMPILER=clang++-18 \
    -DCMAKE_C_COMPILER=clang-18 \
    -DASSERT_HOOK=on \
    -DIGNORE_HOOK=on
    # -DSIMPLE_DFSAN=on \
    # -DINDEPTH_TAINT=on
    # -DTAINT_DEBUG=on \
    # -DSANITIZE_DEBUG=on \
    # -DENTRY_DEBUG=on \
    # -DHOOK_DEBUG=on \
    # -DMEMORY_HOOK=on

RUN cmake --build build && chmod +x build/cgcc build/cov-cgcc

# Eval scripts
COPY eval/scripts/run_fuzz_single.py /scripts/
COPY eval/scripts/run_fuzz_cross.py /scripts/
COPY eval/scripts/run_cov.py /scripts/
COPY eval/scripts/view_eval.py /scripts/
COPY eval/config /configs

COPY eval/scripts/view_coverage.py /scripts/
COPY eval/native_binaries /native_binaries

ENV PATH=/source/build:$PATH
