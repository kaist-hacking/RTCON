set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(CMAKE_C_COMPILER    cgcc)
set(CMAKE_CXX_COMPILER  cgcc++)
#set(AS                  as)
#set(AR                  ar)
#set(OBJCOPY             objcopy)
#set(OBJDUMP             objdump)
#set(SIZE                size)

set(THREADX_ARCH "linux")
set(THREADX_TOOLCHAIN "gnu")

set(CMAKE_C_FLAGS "-g -O0 -fno-inline -fno-pie -no-pie" CACHE INTERNAL "c compiler flags")
set(CMAKE_CXX_FLAGS "-fno-rtti -fno-exceptions" CACHE INTERNAL "cxx compiler flags")
set(CMAKE_ASM_FLAGS "-x assembler-with-cpp" CACHE INTERNAL "asm compiler flags")
set(CMAKE_EXE_LINKER_FLAGS "${LD_FLAGS} -Wl,--gc-sections -fsanitize=address,fuzzer -fno-omit-frame-pointer -fsanitize-recover=address -fprofile-instr-generate -fcoverage-mapping -rdynamic -fno-pie -no-pie" CACHE INTERNAL "exe link flags")

SET(CMAKE_C_FLAGS_DEBUG "-Og -g -ggdb3" CACHE INTERNAL "c debug compiler flags")
SET(CMAKE_CXX_FLAGS_DEBUG "-Og -g -ggdb3" CACHE INTERNAL "cxx debug compiler flags")
SET(CMAKE_ASM_FLAGS_DEBUG "-g -ggdb3" CACHE INTERNAL "asm debug compiler flags")

SET(CMAKE_C_FLAGS_RELEASE "" CACHE INTERNAL "c release compiler flags")
SET(CMAKE_CXX_FLAGS_RELEASE "" CACHE INTERNAL "cxx release compiler flags")
SET(CMAKE_ASM_FLAGS_RELEASE "" CACHE INTERNAL "asm release compiler flags")

# this makes the test compiles use static library option so that we don't need to pre-set linker flags and scripts
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)
