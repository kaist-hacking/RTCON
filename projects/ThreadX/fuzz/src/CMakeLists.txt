
if (DEFINED NATIVE)
    set(SOURCES main_native.c)
endif()

if (DEFINED COV_FUZZ)
    set(SOURCES
        main_cov.c
        fuzz.c
        hook.c
        crash.c
        hashmap.c

        dummy.c
    )
endif()

if (DEFINED FUZZ)
    set(SOURCES
        main.c
        fuzz.c
        hook.c
        crash.c
        hashmap.c

        dummy.c
    )
endif()

if (DEFINED CRASH_DIR_PATH)
    add_compile_options( -DCRASH_DIR_PATH="$ENV{CRASH_DIR_PATH}" )
endif()
if (DEFINED ENV{LOW_FUZZ})
    add_compile_options( -DLOW_FUZZ=1 )
endif()
if (DEFINED ENV{COVERAGE_DIR_PATH})
    add_compile_options( -DFUZZ_COVERAGE )
    add_compile_options( -DCOVERAGE_DIR_PATH="$ENV{COVERAGE_DIR_PATH}")
endif()

add_executable(${PROJECT_NAME} ${SOURCES})

target_link_libraries(${PROJECT_NAME}
    azrtos::threadx
    azrtos::netxduo

    app_common
)