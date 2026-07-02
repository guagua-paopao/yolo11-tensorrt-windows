cmake_minimum_required(VERSION 3.10)

project(yolo11 LANGUAGES CXX CUDA)

add_definitions(-DAPI_EXPORTS)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)

# =========================
# Windows 防止 min/max 宏污染
# =========================
add_definitions(-DNOMINMAX)
add_definitions(-DWIN32_LEAN_AND_MEAN)

# =========================
# CUDA
# =========================
set(CUDA_ROOT "D:/GPU13.3")
set(CMAKE_CUDA_COMPILER "${CUDA_ROOT}/bin/nvcc.exe")

set(CUDA_INCLUDE_DIR "${CUDA_ROOT}/include")
set(CUDA_LIB_DIR "${CUDA_ROOT}/lib/x64")

# RTX 4080 = sm_89
set(CMAKE_CUDA_ARCHITECTURES 89)

# =========================
# TensorRT 10
# =========================
set(TENSORRT_ROOT "D:/TensorRT-10.16.1.11")
set(TENSORRT_INCLUDE_DIR "${TENSORRT_ROOT}/include")
set(TENSORRT_LIB_DIR "${TENSORRT_ROOT}/lib")

message(STATUS "TensorRT include = ${TENSORRT_INCLUDE_DIR}")
message(STATUS "TensorRT lib     = ${TENSORRT_LIB_DIR}")

find_library(NVINFER_LIB
    NAMES nvinfer_10
    PATHS "${TENSORRT_LIB_DIR}"
    NO_DEFAULT_PATH
)

find_library(NVINFER_PLUGIN_LIB
    NAMES nvinfer_plugin_10
    PATHS "${TENSORRT_LIB_DIR}"
    NO_DEFAULT_PATH
)

find_library(CUDART_LIB
    NAMES cudart
    PATHS "${CUDA_LIB_DIR}"
    NO_DEFAULT_PATH
)

if(NOT EXISTS "${TENSORRT_INCLUDE_DIR}/NvInfer.h")
    message(FATAL_ERROR "Cannot find NvInfer.h in ${TENSORRT_INCLUDE_DIR}")
endif()

if(NOT NVINFER_LIB)
    message(FATAL_ERROR "Cannot find nvinfer_10.lib in ${TENSORRT_LIB_DIR}")
endif()

if(NOT NVINFER_PLUGIN_LIB)
    message(FATAL_ERROR "Cannot find nvinfer_plugin_10.lib in ${TENSORRT_LIB_DIR}")
endif()

if(NOT CUDART_LIB)
    message(FATAL_ERROR "Cannot find cudart.lib in ${CUDA_LIB_DIR}")
endif()

# =========================
# OpenCV
# =========================
set(OpenCV_DIR "D:/libs/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED)

message(STATUS "OpenCV_VERSION = ${OpenCV_VERSION}")
message(STATUS "OpenCV_INCLUDE_DIRS = ${OpenCV_INCLUDE_DIRS}")
message(STATUS "OpenCV_LIBS = ${OpenCV_LIBS}")

# =========================
# Project paths
# =========================
set(PROJECT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set(PROJECT_PLUGIN_DIR "${PROJECT_SOURCE_DIR}/plugin")

# =========================
# Source files
# =========================
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)

# =========================
# Plugin library
# =========================
add_library(myplugins SHARED
    ${PROJECT_SOURCE_DIR}/plugin/yololayer.cu
)

target_compile_definitions(myplugins PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(myplugins PRIVATE
    "${CUDA_INCLUDE_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${OpenCV_INCLUDE_DIRS}"
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(myplugins PRIVATE
    "${NVINFER_LIB}"
    "${NVINFER_PLUGIN_LIB}"
    "${CUDART_LIB}"
)

# =========================
# YOLO11 Detection only
# =========================
add_executable(yolo11_det
    ${PROJECT_SOURCE_DIR}/yolo11_det.cpp
    ${SRCS}
)

target_compile_definitions(yolo11_det PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(yolo11_det PRIVATE
    "${CUDA_INCLUDE_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${OpenCV_INCLUDE_DIRS}"
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(yolo11_det PRIVATE
    "${NVINFER_LIB}"
    "${NVINFER_PLUGIN_LIB}"
    "${CUDART_LIB}"
    myplugins
    ${OpenCV_LIBS}
)