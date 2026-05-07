cmake_minimum_required(VERSION 3.22)

include(FetchContent)
# Too slow
# FetchContent_Declare(
#   yaml-cpp
#   GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
#   GIT_TAG master
# )
# FetchContent_GetProperties(yaml-cpp)
# if(NOT yaml-cpp_POPULATED)
#   message(STATUS "Fetching yaml-cpp...")
#   FetchContent_Populate(yaml-cpp)
#   add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
# endif()

# FetchContent_Declare(
#  ryml
#  GIT_REPOSITORY https://github.com/biojppm/rapidyaml.git
#  GIT_TAG v0.7.1
#  GIT_SHALLOW FALSE  # ensure submodules are checked out
# )
# FetchContent_MakeAvailable(ryml)


project(cgcc)

# Fix for missing zstd target in LLVM 18 on some systems
set(ZSTD_LIB "/usr/lib/x86_64-linux-gnu/libzstd.so.1")
if(EXISTS "${ZSTD_LIB}")
  message(STATUS "Manually setting zstd target to ${ZSTD_LIB}")
  if(NOT TARGET zstd::libzstd_shared)
    add_library(zstd::libzstd_shared SHARED IMPORTED GLOBAL)
    set_target_properties(zstd::libzstd_shared PROPERTIES IMPORTED_LOCATION "${ZSTD_LIB}")
  endif()
endif()

find_package(LLVM REQUIRED CONFIG)

include_directories(${LLVM_INCLUDE_DIRS})
add_definitions(${LLVM_DEFINITIONS})

if(${TAINT_DEBUG})
  add_compile_options(-DTAINT_DEBUG)
endif()

if(${SANITIZE_DEBUG})
  add_compile_options(-DSANITIZE_DEBUG)
endif()

if(${ENTRY_DEBUG})
  add_compile_options(-DENTRY_DEBUG)
endif()

if(${HOOK_DEBUG})
  add_compile_options(-DHOOK_DEBUG)
endif()

if(${SIMPLE_DFSAN_DEBUG})
  add_compile_options(-DSIMPLE_DFSAN_DEBUG)
endif()

if(${SIMPLE_DFSAN})
  add_compile_options(-DSIMPLE_DFSAN)
endif()

if(${ASSERT_HOOK})
  add_compile_options(-DASSERT_HOOK)
endif()

if(${IGNORE_HOOK})
  add_compile_options(-DIGNORE_HOOK)
endif()

if(${INDEPTH_TAINT})
  add_compile_options(-DINDEPTH_TAINT)
endif()

if(${STRATEGY1})
  add_compile_options(-DSTRATEGY1)
endif()

if(${MEMORY_HOOK})
  add_compile_options(-DMEMCPY_HOOK)
endif()

if(${MALLOC_HOOK})
  add_compile_options(-DMALLOC_HOOK)
endif()

# Below hooks make system instable
# add_compile_options(-DFREE_HOOK)
# add_compile_options(-DMALLOC_HOOK)

add_compile_options(-g)

add_library(cgcc MODULE Pass.cpp)

file(GLOB SKEL_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/skel/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/skel/*.cpp"
)

add_library(skel STATIC ${SKEL_SRCS})
target_compile_options(skel PRIVATE
    -Wno-incompatible-pointer-types-discards-qualifiers
    -Wno-format
    -Wno-return-type
    -fPIC
)

add_executable(readfunc readFunc.cpp)
llvm_map_components_to_libnames(LLVM_LIBS
  Core
  Support
  IRReader
  BitReader
)

target_link_libraries(readfunc PRIVATE ${LLVM_LIBS})

# target_link_libraries(cgcc PUBLIC yaml-cpp::yaml-cpp)
# target_link_libraries(cgcc PUBLIC ryml::ryml)

set_target_properties(cgcc PROPERTIES OUTPUT_NAME "cgcc")
find_program(CLANG_BINARY "clang-18")

set(CLANG_LOAD_PASS "-fpass-plugin=")

configure_file("cgcc.in" "cgcc" @ONLY)
configure_file("cov-cgcc.in" "cov-cgcc" @ONLY)
