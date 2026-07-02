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
# Third-party libraries from vcpkg
# 注意：
# 配置 CMake 时必须带：
# -DCMAKE_TOOLCHAIN_FILE=D:/vcpkg/scripts/buildsystems/vcpkg.cmake
# =========================
find_package(nlohmann_json CONFIG REQUIRED)
find_package(yaml-cpp CONFIG REQUIRED)
find_package(Crow CONFIG REQUIRED)

# Redis client: hiredis
# vcpkg install hiredis:x64-windows
find_package(hiredis CONFIG REQUIRED)
set(YOLO11_HIREDIS_TARGET hiredis::hiredis)

# =========================
# Project paths
# =========================
set(PROJECT_INCLUDE_DIR "${PROJECT_SOURCE_DIR}/include")
set(PROJECT_PLUGIN_DIR "${PROJECT_SOURCE_DIR}/plugin")
set(PROJECT_API_DIR "${PROJECT_SOURCE_DIR}/api")
set(PROJECT_EXAMPLES_DIR "${PROJECT_SOURCE_DIR}/examples")

# =========================
# Source files
# 注意：
# 这里只收集 src 下的公共源码。
# 不要把 yolo11_det.cpp、yolo11_obb.cpp 这种带 main 的文件放进 SRCS。
# 也不要把 src/server/*.cpp 放进 SRCS。
# =========================
file(GLOB_RECURSE SRCS
    ${PROJECT_SOURCE_DIR}/src/*.cpp
    ${PROJECT_SOURCE_DIR}/src/*.cu
)

# 关键：排除 server 源码，避免被 yolo11_det / yolo11_obb / yolo11_runtime 重复编译
list(FILTER SRCS EXCLUDE REGEX ".*src[/\\\\]server[/\\\\].*")

# =========================
# Helper function
# 编译后把 myplugins.dll 复制到目标 exe 所在目录
# =========================
function(copy_myplugins_to_target target_name)
    add_custom_command(TARGET ${target_name} POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            $<TARGET_FILE:myplugins>
            $<TARGET_FILE_DIR:${target_name}>
        COMMENT "Copy myplugins.dll to $<TARGET_FILE_DIR:${target_name}>"
    )
endfunction()

# =========================
# Helper function
# 给 demo / exe 配置公共 include、compile definitions、link libraries
# =========================
function(config_yolo11_target target_name)
    target_compile_definitions(${target_name} PRIVATE
        NOMINMAX
        WIN32_LEAN_AND_MEAN
    )

    target_include_directories(${target_name} PRIVATE
        "${CUDA_INCLUDE_DIR}"
        "${TENSORRT_INCLUDE_DIR}"
        "${OpenCV_INCLUDE_DIRS}"
        "${PROJECT_INCLUDE_DIR}"
        "${PROJECT_PLUGIN_DIR}"
    )

    target_link_libraries(${target_name} PRIVATE
        "${NVINFER_LIB}"
        "${NVINFER_PLUGIN_LIB}"
        "${CUDART_LIB}"
        myplugins
        ${OpenCV_LIBS}
    )

    add_dependencies(${target_name} myplugins)
    copy_myplugins_to_target(${target_name})
endfunction()

# =========================
# Plugin library
# =========================
add_library(myplugins SHARED
    ${PROJECT_PLUGIN_DIR}/yololayer.cu
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
# YOLO11 Detection original executable
# 原始命令行版本：yolo11_det.exe
# =========================
add_executable(yolo11_det
    ${PROJECT_SOURCE_DIR}/yolo11_det.cpp
    ${SRCS}
)

config_yolo11_target(yolo11_det)

# =========================
# YOLO11 OBB original executable
# 原始命令行版本：yolo11_obb.exe
# =========================
add_executable(yolo11_obb
    ${PROJECT_SOURCE_DIR}/yolo11_obb.cpp
    ${SRCS}
)

config_yolo11_target(yolo11_obb)

# =========================
# YOLO11 Runtime API
# 给外部 C++ 调用的静态库
# =========================
add_library(yolo11_runtime STATIC
    ${PROJECT_API_DIR}/yolo11_detector_api.cpp
    ${PROJECT_API_DIR}/yolo11_obb_api.cpp
    ${SRCS}
)

target_compile_definitions(yolo11_runtime PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(yolo11_runtime PUBLIC
    "${CUDA_INCLUDE_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${OpenCV_INCLUDE_DIRS}"
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(yolo11_runtime PUBLIC
    "${NVINFER_LIB}"
    "${NVINFER_PLUGIN_LIB}"
    "${CUDART_LIB}"
    myplugins
    ${OpenCV_LIBS}
)

add_dependencies(yolo11_runtime myplugins)

# =========================
# Demo Image
# Detection 新接口图片测试程序：demo_image.exe
# =========================
add_executable(demo_image
    ${PROJECT_EXAMPLES_DIR}/demo_image.cpp
)

target_compile_definitions(demo_image PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(demo_image PRIVATE
    "${CUDA_INCLUDE_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${OpenCV_INCLUDE_DIRS}"
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(demo_image PRIVATE
    yolo11_runtime
    myplugins
)

add_dependencies(demo_image myplugins)
copy_myplugins_to_target(demo_image)

# =========================
# Demo Video
# Detection 新接口视频测试程序：demo_video.exe
# =========================
add_executable(demo_video
    ${PROJECT_EXAMPLES_DIR}/demo_video.cpp
)

target_compile_definitions(demo_video PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(demo_video PRIVATE
    "${CUDA_INCLUDE_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${OpenCV_INCLUDE_DIRS}"
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(demo_video PRIVATE
    yolo11_runtime
    myplugins
)

add_dependencies(demo_video myplugins)
copy_myplugins_to_target(demo_video)

# =========================
# Demo OBB Image
# OBB 新接口图片测试程序：demo_obb_image.exe
# =========================
add_executable(demo_obb_image
    ${PROJECT_EXAMPLES_DIR}/demo_obb_image.cpp
)

target_compile_definitions(demo_obb_image PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

target_include_directories(demo_obb_image PRIVATE
    "${CUDA_INCLUDE_DIR}"
    "${TENSORRT_INCLUDE_DIR}"
    "${OpenCV_INCLUDE_DIRS}"
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(demo_obb_image PRIVATE
    yolo11_runtime
    myplugins
)

add_dependencies(demo_obb_image myplugins)
copy_myplugins_to_target(demo_obb_image)

# ============================================================
# Phase 4: YOLO11 pure C++ HTTP server + Redis Stream worker pool
# 纯 C++ HTTP 服务 + Redis Stream 多 Worker 推理池
# ============================================================
add_executable(yolo11_server
    ${PROJECT_SOURCE_DIR}/src/server/main_server.cpp
    ${PROJECT_SOURCE_DIR}/src/server/app_config.cpp
    ${PROJECT_SOURCE_DIR}/src/server/image_codec.cpp
    ${PROJECT_SOURCE_DIR}/src/server/result_serializer.cpp
    ${PROJECT_SOURCE_DIR}/src/server/redis_task_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/server/inference_worker.cpp
    ${PROJECT_SOURCE_DIR}/src/server/inference_service.cpp
    ${PROJECT_SOURCE_DIR}/src/server/http_controller.cpp
)

target_compile_definitions(yolo11_server PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(yolo11_server PRIVATE /utf-8)
endif()

target_include_directories(yolo11_server PRIVATE
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(yolo11_server PRIVATE
    yolo11_runtime
    myplugins

    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    Crow::Crow
    ${YOLO11_HIREDIS_TARGET}
)

# hiredis on Windows needs Winsock
if(WIN32)
    target_link_libraries(yolo11_server PRIVATE ws2_32)
endif()

add_dependencies(yolo11_server myplugins)
copy_myplugins_to_target(yolo11_server)
# ============================================================
# Phase 7: YOLO11 Redis Stream worker process
# Standalone Consumer process for production deployment
# ============================================================
add_executable(yolo11_worker
    ${PROJECT_SOURCE_DIR}/src/server/main_worker.cpp
    ${PROJECT_SOURCE_DIR}/src/server/app_config.cpp
    ${PROJECT_SOURCE_DIR}/src/server/image_codec.cpp
    ${PROJECT_SOURCE_DIR}/src/server/result_serializer.cpp
    ${PROJECT_SOURCE_DIR}/src/server/redis_task_queue.cpp
    ${PROJECT_SOURCE_DIR}/src/server/inference_worker.cpp
    ${PROJECT_SOURCE_DIR}/src/server/inference_service.cpp
)

target_compile_definitions(yolo11_worker PRIVATE
    NOMINMAX
    WIN32_LEAN_AND_MEAN
)

if(MSVC)
    target_compile_options(yolo11_worker PRIVATE /utf-8)
endif()

target_include_directories(yolo11_worker PRIVATE
    "${PROJECT_INCLUDE_DIR}"
    "${PROJECT_PLUGIN_DIR}"
)

target_link_libraries(yolo11_worker PRIVATE
    yolo11_runtime
    myplugins

    nlohmann_json::nlohmann_json
    yaml-cpp::yaml-cpp
    ${YOLO11_HIREDIS_TARGET}
)

if(WIN32)
    target_link_libraries(yolo11_worker PRIVATE ws2_32)
endif()

add_dependencies(yolo11_worker myplugins)
copy_myplugins_to_target(yolo11_worker)
