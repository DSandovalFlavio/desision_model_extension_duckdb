cmake_minimum_required(VERSION 3.14)

# Extension "decide_amox": Jev-style decision models (OneJev1.6 0.8B / 4B / 9B) over llama.cpp, from SQL.
set(TARGET_NAME decide_amox)
set(EXTENSION_NAME ${TARGET_NAME}_extension)
set(LOADABLE_EXTENSION_NAME ${TARGET_NAME}_loadable_extension)

project(${TARGET_NAME})

set(CMAKE_CXX_STANDARD 17 CACHE STRING "C++ standard to enforce")
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---------------------------------------------------------------------------------------------------------------
# llama.cpp (pinned submodule third_party/llama.cpp @ b12521), linked statically into the extension.
# ---------------------------------------------------------------------------------------------------------------
option(DECIDE_WITH_CUDA "Build the llama.cpp CUDA backend (needs the CUDA Toolkit)" OFF)
option(DECIDE_NATIVE "Optimise llama.cpp for the CPU of the build machine (OFF = portable binary, used by CI)" OFF)

set(LLAMA_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/llama.cpp)
set(DECIDE_LLAMA_BUILD "b12521-6f311d7")

set(BUILD_SHARED_LIBS OFF)
set(LLAMA_BUILD_COMMON OFF)
set(LLAMA_BUILD_TESTS OFF)
set(LLAMA_BUILD_TOOLS OFF)
set(LLAMA_BUILD_EXAMPLES OFF)
set(LLAMA_BUILD_SERVER OFF)
set(LLAMA_BUILD_APP OFF)
set(LLAMA_OPENSSL OFF)
set(LLAMA_ALL_WARNINGS OFF)
set(LLAMA_TOOLS_INSTALL OFF)
set(LLAMA_TESTS_INSTALL OFF)
set(GGML_BACKEND_DL OFF)
set(GGML_NATIVE ${DECIDE_NATIVE})
set(GGML_CUDA ${DECIDE_WITH_CUDA})
set(GGML_CUDA_NCCL OFF)
if(NOT CMAKE_CUDA_ARCHITECTURES)
  # RTX 20xx/30xx/40xx laptops; override with -DCMAKE_CUDA_ARCHITECTURES=...
  set(CMAKE_CUDA_ARCHITECTURES "75;86;89")
endif()

set(CMAKE_POSITION_INDEPENDENT_CODE ON)
add_subdirectory(${LLAMA_DIR} ${CMAKE_CURRENT_BINARY_DIR}/llama.cpp EXCLUDE_FROM_ALL)

# ---------------------------------------------------------------------------------------------------------------
# Extension
# ---------------------------------------------------------------------------------------------------------------
include_directories(src/include ${LLAMA_DIR}/include ${LLAMA_DIR}/ggml/include)

set(EXTENSION_SOURCES src/decide_amox_extension.cpp src/decide_models.cpp src/decide_prompt.cpp src/decide_runtime.cpp src/decide_grammar.cpp src/decide_store.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

foreach(T ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
  target_link_libraries(${T} llama ggml)
  target_compile_definitions(${T} PRIVATE DECIDE_LLAMA_BUILD="${DECIDE_LLAMA_BUILD}")
endforeach()

# DuckDB exports every static extension target; the static llama.cpp libraries it links must be in the same set.
set(DECIDE_LLAMA_TARGETS llama ggml ggml-base ggml-cpu)
if(DECIDE_WITH_CUDA)
  list(APPEND DECIDE_LLAMA_TARGETS ggml-cuda)
endif()

install(
  TARGETS ${EXTENSION_NAME} ${DECIDE_LLAMA_TARGETS}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
