FROM cgcc-base

ARG THREADX_ROOT
ARG TAINT_CONFIGURATION_FILE

ENV THREADX_ROOT=${THREADX_ROOT}
ENV TAINT_CONFIGURATION_FILE=${TAINT_CONFIGURATION_FILE}

# ThreadX
COPY projects/ThreadX /source/projects/ThreadX
WORKDIR /source/projects/ThreadX
RUN cd shared/lib && \
    git clone --depth=1 https://github.com/eclipse-threadx/threadx.git threadx
RUN cd shared/lib && \
    git clone --depth=1 https://github.com/zserge/jsmn.git jsmn/src
RUN cd shared/lib && \
    git clone --depth=1 --recursive https://github.com/eclipse-threadx/netxduo.git netxduo

# ThreadX fuzz
COPY skel /source/skel
COPY cfg /source/cfg
RUN cp /source/skel/fuzz.* fuzz/src/ && \
    cp /source/skel/hashmap.* fuzz/src/ && \
    cp /source/skel/hook.* fuzz/src/ && \
    ln -s /usr/bin/clang-18 /usr/bin/clang && \
    ln -s /SVF/llvm-16.0.0.obj/bin/clang /usr/bin/clang-16 && \
    ln -s /SVF/llvm-16.0.0.obj/bin/llvm-link /usr/bin/llvm-link

# Static analysis (Each fuzz target should be built with the same configuration)
RUN cd fuzz && \
    python3 /source/cfg/create_compile_commands.py threadx && \
    python3 /source/cfg/analyze_mta.py compile_commands.json

RUN chmod +x build.sh
ENTRYPOINT [ "./build.sh" ]