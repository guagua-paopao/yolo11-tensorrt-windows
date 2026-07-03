# YOLO11 TensorRT Windows 部署说明

本项目基于 `tensorrtx/yolo11` 进行 Windows 平台适配，用于在 **Visual Studio 2019 + CUDA + TensorRT + OpenCV** 环境下完成 YOLO11 目标检测模型的 C++ 部署、推理和后续 SDK 接口封装。

项目流程：

```text
yolo11n.pt
        ↓
gen_wts.py
        ↓
yolo11n.wts
        ↓
yolo11_det.exe -s
        ↓
yolo11n.engine
        ↓
yolo11_det.exe -d
        ↓
生成带检测框的结果图片
```

---

## 1. 项目说明

当前重点验证的是：

```text
YOLO11 Detection
```

分类、分割、姿态估计、OBB 等模块属于原项目支持范围，但本次 Windows 适配主要围绕目标检测模块 `yolo11_det` 展开。

---

## 2. 测试环境

| 项目 | 配置 |
|---|---|
| 操作系统 | Windows |
| IDE | Visual Studio 2019 |
| 编译方式 | CMake + MSVC x64 |
| CUDA | `D:\GPU13.3` |
| cuDNN | `D:\cuDNN9\bin\13.3\x64` |
| TensorRT | `D:\TensorRT-10.16.1.11` |
| OpenCV | `D:\libs\opencv\build` |
| GPU | NVIDIA RTX 4080 |
| CUDA 架构 | `sm_89` |
| Python | Python 3.12.3 |
| Python 依赖 | `torch`、`ultralytics` |

建议系统环境变量 `Path` 包含：

```text
D:\GPU13.3\bin
D:\cuDNN9\bin\13.3\x64
D:\TensorRT-10.16.1.11\bin
D:\libs\opencv\build\x64\vc16\bin
```
---

## 3. 主要修改内容

本项目主要修改：

```text
CMakeLists.txt
include/utils.h
```

可选新增 SDK 接口文件：

```text
sdk/include/yolo11_detector_api.h
sdk/src/yolo11_detector_api.cpp
sdk/demo/demo_yolo11_sdk.cpp
```

---

## 4. CMakeLists.txt 修改说明

原项目 CMake 文件在当前 Windows 环境下不能直接使用，因此修改了以下内容：

```text
1. 指定 CUDA 路径
2. 指定 TensorRT 10 路径
3. 指定 OpenCV 路径
4. 链接 nvinfer_10.lib / nvinfer_plugin_10.lib
5. 设置 RTX 4080 对应的 CUDA 架构 sm_89
6. 只编译 yolo11_det 检测目标
7. 编译 myplugins 插件库
8. 添加 NOMINMAX / WIN32_LEAN_AND_MEAN，避免 Windows 宏污染
```

核心路径示例：

```cmake
set(CUDA_ROOT "D:/GPU13.3")
set(CMAKE_CUDA_COMPILER "${CUDA_ROOT}/bin/nvcc.exe")
set(CMAKE_CUDA_ARCHITECTURES 89)

set(TENSORRT_ROOT "D:/TensorRT-10.16.1.11")
set(TENSORRT_INCLUDE_DIR "${TENSORRT_ROOT}/include")
set(TENSORRT_LIB_DIR "${TENSORRT_ROOT}/lib")

set(OpenCV_DIR "D:/libs/opencv/build/x64/vc16/lib")
find_package(OpenCV REQUIRED)
```

如果在其他电脑上部署，需要根据本机安装路径修改这些变量。

---

## 5. utils.h 修改说明

原项目使用：

```cpp
#include <dirent.h>
```

该头文件在 Linux 下可用，但 Windows / MSVC 默认不支持，会报：

```text
fatal error C1083: 无法打开包括文件: “dirent.h”: No such file or directory
```

因此本项目将目录遍历改成跨平台实现：

```text
Windows：FindFirstFileA / FindNextFileA / FindClose
Linux：opendir / readdir / closedir
```

同时补充 OpenCV 头文件，解决：

```text
cv::resize 不是 cv 的成员
cv::INTER_LINEAR 不是 cv 的成员
cv::imread 不是 cv 的成员
```

并加入：

```cpp
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
```

用于避免 `windows.h` 中的 `min/max` 宏污染 OpenCV 或标准库。

---

## 6. 推荐目录结构

```text
yolo11
├── CMakeLists.txt
├── README.md
├── README_EN.md
├── gen_wts.py
├── yolo11_det.cpp
├── include/
├── src/
├── plugin/
├── sdk/                     # 可选：C++ SDK 接口
│   ├── include/
│   ├── src/
│   └── demo/
└── images/                  # 可选：测试图片

---

## 7. VS2019 编译步骤

### 7.1 打开项目

在 VS2019 中选择：

```text
文件 → 打开 → 文件夹
```

打开：

```text
D:\project\guagua-paopao\tensorrtx\yolo11
```

或你的实际项目路径。

### 7.2 删除旧缓存

如果修改过 `CMakeLists.txt`，建议删除：

```text
yolo11/out
```

然后重新打开 VS2019，让 CMake 重新配置。

### 7.3 编译

切换到 CMake 目标视图，右键：

```text
yolo11_det
```

选择：

```text
重新生成 yolo11_det
```

编译成功后，通常在：

```text
yolo11/out/build/x64-Debug
```

生成：

```text
yolo11_det.exe
myplugins.dll
```

---

## 8. .pt 转 .wts

进入项目目录：

```bat
cd /d D:\project\guagua-paopao\tensorrtx\yolo11
```

官方模型：

```bat
python gen_wts.py -w yolo11n.pt -o yolo11n.wts -t detect
```

自定义模型：

```bat
python gen_wts.py -w best.pt -o best.wts -t detect
```

如果出现：

```text
AttributeError: Can't get attribute 'C3k2'
```

说明 `ultralytics` 版本过旧，执行：

```bat
python -m pip install -U ultralytics
```

---

## 9. 生成 TensorRT Engine

复制 `.wts` 到 exe 目录：

```bat
copy /Y yolo11n.wts out\build\x64-Debug\
```

进入 exe 目录：

```bat
cd /d D:\project\guagua-paopao\tensorrtx\yolo11\out\build\x64-Debug
```

生成 engine：

```bat
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

模型规模参数：

| 模型 | 参数 |
|---|---|
| yolo11n | `n` |
| yolo11s | `s` |
| yolo11m | `m` |
| yolo11l | `l` |
| yolo11x | `x` |

自定义模型如果基于 `yolo11n` 训练：

```bat
yolo11_det.exe -s best.wts best.engine n
```

---

## 10. 图片推理

准备图片目录：

```text
D:\project\guagua-paopao\tensorrtx\yolo11\images
```

运行推理：

```bat
yolo11_det.exe -d yolo11n.engine D:/project/guagua-paopao/tensorrtx/yolo11/images c
```

自定义模型：

```bat
yolo11_det.exe -d best.engine D:/project/guagua-paopao/tensorrtx/yolo11/images c
```

说明：

```text
-d：加载 engine 进行推理
c：CPU 后处理
```

生成带检测框的结果图片，即说明部署成功。

---

## 11. 自定义模型注意事项

如果使用官方 COCO 模型：

```cpp
const static int kNumClass = 80;
```

如果使用自定义模型，需要修改：

```text
include/config.h
```

例如 1 类：

```cpp
const static int kNumClass = 1;
```

3 类：

```cpp
const static int kNumClass = 3;
```

修改 `config.h` 后必须：

```text
1. 重新编译 yolo11_det
2. 重新生成 engine
3. 再运行推理
```

不能继续使用旧 engine。

---

## 12. Debug 与 Release

当前项目已在：

```text
x64-Debug
```

下跑通。

正式测速或部署时建议切换到：

```text
x64-Release
```

然后重新编译，并使用 Release 目录下的 `yolo11_det.exe` 重新生成 engine。

---

## 13. C++ 接口设计建议

如果需要把该部署封装为接口，建议采用三层结构：

```text
第一层：TensorRT 推理核心层
负责 engine 加载、显存申请、预处理、推理、后处理、资源释放。

第二层：C++ SDK 接口层
提供 Detector 类，如 init()、detect()、detectFile()、drawDetections()。

第三层：DLL / C ABI 导出层
提供 C 风格接口，便于 C#、Python、LabVIEW、Qt 上位机等调用。
```

推荐调用形式：

```cpp
yolo11sdk::Detector detector;

yolo11sdk::DetectorConfig config;
config.engine_path = "models/best.engine";
config.labels_path = "models/labels.txt";
config.gpu_id = 0;
config.use_gpu_postprocess = false;

detector.init(config);

cv::Mat image = cv::imread("test.jpg");
auto results = detector.detect(image);
```

---

## 14. 常见问题

### 找不到 NvInfer.h

```text
fatal error C1083: 无法打开包括文件: “NvInfer.h”
```

检查：

```text
D:\TensorRT-10.16.1.11\include\NvInfer.h
```

并确认 CMake 中 TensorRT 路径正确。

### 找不到 opencv2/opencv.hpp

检查 OpenCV 路径，并确认 CMake 中包含：

```cmake
find_package(OpenCV REQUIRED)
"${OpenCV_INCLUDE_DIRS}"
```

### 找不到 dirent.h

Windows 下不支持原始 `dirent.h`，应使用本项目修改后的 `utils.h`。

### C2589 或 min/max 宏污染

通常是 `windows.h` 的 `min/max` 宏导致，需要加入：

```cpp
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
```

### C4819 警告

`C4819` 通常是编码警告，一般不影响编译。优先处理 `error` 和 `fatal error`。

---

## 15. 最小运行命令

```bat
cd /d D:\project\guagua-paopao\tensorrtx\yolo11
python gen_wts.py -w yolo11n.pt -o yolo11n.wts -t detect

copy /Y yolo11n.wts out\build\x64-Debug\

cd /d D:\project\guagua-paopao\tensorrtx\yolo11\out\build\x64-Debug
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
yolo11_det.exe -d yolo11n.engine D:/project/guagua-paopao/tensorrtx/yolo11/images c
```

---

## 16. 成功标志

```text
1. yolo11_det.exe 编译成功
2. myplugins.dll 生成成功
3. .wts 生成成功
4. .engine 生成成功
5. 推理后生成带检测框的结果图片
```
