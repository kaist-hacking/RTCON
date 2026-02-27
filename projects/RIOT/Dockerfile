FROM cgcc-base

ARG RIOT_ROOT
ARG TAINT_CONFIGURATION_FILE
ARG LWIP_FUZZ
ARG LORAWAN_FUZZ
ARG NIMBLE_FUZZ

ENV RIOT_ROOT=${RIOT_ROOT}
ENV TAINT_CONFIGURATION_FILE=${TAINT_CONFIGURATION_FILE}
ENV LWIP_FUZZ=${LWIP_FUZZ}
ENV LORAWAN_FUZZ=${LORAWAN_FUZZ}
ENV NIMBLE_FUZZ=${NIMBLE_FUZZ}

# RIOT
WORKDIR /source/projects/RIOT/riot_latest
RUN git init && \
    git remote add origin https://github.com/RIOT-OS/RIOT.git && \
    git fetch --depth=1 --no-tags origin 687a30af3362594b53bdfb7050d02a5b05a5f97c && \
    git reset --hard 687a30af3362594b53bdfb7050d02a5b05a5f97c

COPY skel /source/skel
COPY projects/RIOT /source/projects/RIOT
COPY cfg /source/cfg

# RIOT Fuzz
WORKDIR /source/projects/RIOT
RUN cp /source/skel/crash.* appfuzz && \
    cp /source/skel/hashmap.* appfuzz && \
    cp /source/skel/fuzz.* appfuzz && \
    cp /source/skel/hook.* appfuzz && \
    cp makefiles/*.inc.mk ${RIOT_ROOT}/makefiles/toolchain/ && \
    ./apply_patch.sh && \
    ln -s /usr/bin/clang-18 /usr/bin/clang && \
    ln -s /usr/bin/clang++-18 /usr/bin/clang++ && \
    ln -s /SVF/llvm-16.0.0.obj/bin/clang /usr/bin/clang-16 && \
    ln -s /SVF/llvm-16.0.0.obj/bin/llvm-link /usr/bin/llvm-link

# Static analysis (Each fuzz target should be built with the same configuration)
RUN python3 /source/cfg/create_compile_commands.py riot && \
    python3 /source/cfg/analyze_mta.py compile_commands.json

RUN chmod +x build.sh
ENTRYPOINT ["./build.sh"]