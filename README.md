# YOLO11 TensorRT Windows

YOLO11 detection deployment on Windows with TensorRT 10, Visual Studio 2019, CUDA, and OpenCV.

## Environment

Tested environment:

* Windows
* Visual Studio 2019
* CUDA 13.3: `D:\GPU13.3`
* cuDNN 9: `D:\cuDNN9\bin\13.3\x64`
* TensorRT 10.16.1.11: `D:\TensorRT-10.16.1.11`
* OpenCV 4.12.0: `D:\libs\opencv\build`
* Python 3.12.3
* Ultralytics YOLO11
* GPU: RTX 4080, CUDA architecture `sm_89`

Make sure the following directories are added to system `Path`:

```text
D:\GPU13.3\bin
D:\cuDNN9\bin\13.3\x64
D:\TensorRT-10.16.1.11\bin
D:\libs\opencv\build\x64\vc16\bin
```

## Supported

* [x] YOLO11 detection
* [x] FP32 / FP16 / INT8 support depends on project configuration
* [x] C++ TensorRT inference
* [x] Windows + Visual Studio 2019 build

## Config

Main configs are in:

```text
include/config.h
```

For custom models, set class number correctly:

```cpp
const static int kNumClass = 80;
```

For example:

```cpp
const static int kNumClass = 1;
```

After modifying `config.h`, rebuild `yolo11_det` and regenerate the TensorRT engine.

## Build

Open the project folder with Visual Studio 2019:

```text
File → Open → Folder
```

Open:

```text
D:\project\guagua-paopao\tensorrtx\yolo11
```

If CMake cache already exists, delete:

```text
out/
```

Then build the CMake target:

```text
yolo11_det
```

After successful build, the executable is usually generated in:

```text
out\build\x64-Debug
```

Required output files:

```text
yolo11_det.exe
myplugins.dll
```

## Generate .wts

Enter the `yolo11` directory:

```bat
cd /d D:\project\guagua-paopao\tensorrtx\yolo11
```

For official YOLO11n:

```bat
python gen_wts.py -w yolo11n.pt -o yolo11n.wts -t detect
```

For custom model:

```bat
python gen_wts.py -w best.pt -o best.wts -t detect
```

If `C3k2` error occurs, update Ultralytics:

```bat
python -m pip install -U ultralytics
```

## Build TensorRT Engine

Copy `.wts` to the executable directory:

```bat
copy /Y yolo11n.wts out\build\x64-Debug\
```

Enter the executable directory:

```bat
cd /d D:\project\guagua-paopao\tensorrtx\yolo11\out\build\x64-Debug
```

Serialize engine:

```bat
yolo11_det.exe -s yolo11n.wts yolo11n.engine n
```

For custom model trained from YOLO11n:

```bat
yolo11_det.exe -s best.wts best.engine n
```

Model scale argument:

| Model | Argument |
|---|---|
| YOLO11n | `n` |
| YOLO11s | `s` |
| YOLO11m | `m` |
| YOLO11l | `l` |
| YOLO11x | `x` |

## Run Inference

Prepare images in:

```text
D:\project\guagua-paopao\tensorrtx\yolo11\images
```

Run inference:

```bat
yolo11_det.exe -d yolo11n.engine D:/project/guagua-paopao/tensorrtx/yolo11/images c
```

For custom model:

```bat
yolo11_det.exe -d best.engine D:/project/guagua-paopao/tensorrtx/yolo11/images c
```

Results are saved in the executable directory.

## Custom Model Notes

For custom models:

1. Modify `include/config.h`.
2. Set `kNumClass` to your class count.
3. Rebuild `yolo11_det`.
4. Regenerate `.engine`.
5. Run inference again.

Do not use an old engine after changing `config.h`.

## Git Ignore

Do not upload generated files to GitHub:

```text
out/
build/
.vs/
*.exe
*.dll
*.lib
*.pdb
*.obj
*.pt
*.wts
*.engine
*.onnx
```

## Minimal Commands

```bat
cd /d D:\project\guagua-paopao\tensorrtx\yolo11

python gen_wts.py -w yolo11n.pt -o yolo11n.wts -t detect

copy /Y yolo11n.wts out\build\x64-Debug\

cd /d D:\project\guagua-paopao\tensorrtx\yolo11\out\build\x64-Debug

yolo11_det.exe -s yolo11n.wts yolo11n.engine n

yolo11_det.exe -d yolo11n.engine D:/project/guagua-paopao/tensorrtx/yolo11/images c
```

## Acknowledgement

Modified from `tensorrtx/yolo11`.
