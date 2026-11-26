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
# No OpenMP: on Windows it would make the extension depend on VCOMP140.DLL (Visual C++ redistributable), which the
# release zips cannot assume. ggml then uses its own thread pool.
set(GGML_OPENMP OFF)
if(WIN32 AND DECIDE_WITH_CUDA)
  # Link the CUDA runtime statically (cudart_static; cuBLAS stays a DLL, Windows has no static cuBLAS). nvcc emits
  # static initializers that call the CUDA runtime to register the kernels while the extension DLL is being loaded,
  # so cudart cannot be a delay-loaded DLL. On MSVC, GGML_STATIC changes nothing else.
  set(GGML_STATIC ON)
endif()
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

set(EXTENSION_SOURCES src/decide_amox_extension.cpp src/decide_models.cpp src/decide_prompt.cpp src/decide_runtime.cpp src/decide_grammar.cpp src/decide_store.cpp src/decide_dll.cpp)

build_static_extension(${TARGET_NAME} ${EXTENSION_SOURCES})
build_loadable_extension(${TARGET_NAME} " " ${EXTENSION_SOURCES})

foreach(T ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
  target_link_libraries(${T} llama ggml)
  target_compile_definitions(${T} PRIVATE DECIDE_LLAMA_BUILD="${DECIDE_LLAMA_BUILD}")
endforeach()

# Windows + CUDA: DuckDB loads extensions with LoadLibraryW, and Windows does not look for an extension's dependent
# DLLs next to the extension file. The cuBLAS DLLs (only called at inference time) and nvcuda.dll (driver) are therefore
# delay-loaded, and the extension preloads them from its own folder (then the normal search: the .exe folder,
# System32, PATH) before ggml touches CUDA, failing with a clear message when one is missing. The CUDA runtime is
# linked statically (GGML_STATIC above). See src/decide_dll.cpp.
set(DECIDE_CUDA_DLLS "")
if(WIN32 AND DECIDE_WITH_CUDA)
  find_package(CUDAToolkit REQUIRED)
  set(DECIDE_CUDA_DLLS "cublasLt64_${CUDAToolkit_VERSION_MAJOR}.dll;cublas64_${CUDAToolkit_VERSION_MAJOR}.dll")
  set(DECIDE_DELAYLOAD nvcuda.dll ${DECIDE_CUDA_DLLS})
  foreach(DLL ${DECIDE_DELAYLOAD})
    target_link_options(${LOADABLE_EXTENSION_NAME} PRIVATE "/DELAYLOAD:${DLL}")
  endforeach()
  target_link_libraries(${LOADABLE_EXTENSION_NAME} delayimp)
  message(STATUS "decide_amox: delay-loading ${DECIDE_DELAYLOAD}")
endif()
# Passed to C++ as a comma-separated list (a ';' would split the compile definition).
string(REPLACE ";" "," DECIDE_CUDA_DLLS_CSV "${DECIDE_CUDA_DLLS}")
foreach(T ${EXTENSION_NAME} ${LOADABLE_EXTENSION_NAME})
  target_compile_definitions(${T} PRIVATE DECIDE_CUDA_DLLS="${DECIDE_CUDA_DLLS_CSV}")
endforeach()

# DuckDB exports every static extension target; the static llama.cpp libraries it links must be in the same set.
# Backends depend on the platform (CUDA if requested, Metal and BLAS on macOS), so export whichever exist.
set(DECIDE_LLAMA_TARGETS llama ggml ggml-base)
foreach(T ggml-cpu ggml-blas ggml-metal ggml-cuda)
  if(TARGET ${T})
    list(APPEND DECIDE_LLAMA_TARGETS ${T})
  endif()
endforeach()

install(
  TARGETS ${EXTENSION_NAME} ${DECIDE_LLAMA_TARGETS}
  EXPORT "${DUCKDB_EXPORT_SET}"
  LIBRARY DESTINATION "${INSTALL_LIB_DIR}"
  ARCHIVE DESTINATION "${INSTALL_LIB_DIR}")
