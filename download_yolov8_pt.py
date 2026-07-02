from pathlib import Path
import os
from ultralytics import YOLO


# =========================
# 1. 选择你要下载的模型
# =========================
models = [
    #"yolo11n.pt",
    "yolo11n-obb.pt"
]


# =========================
# 2. 设置保存目录
# =========================
save_dir = Path("weights")
save_dir.mkdir(exist_ok=True)

# 切换到 weights 文件夹，这样模型会下载到这里
os.chdir(save_dir)


# =========================
# 3. 自动下载 .pt 模型
# =========================
for model_name in models:
    print(f"正在下载或加载：{model_name}")

    # 如果本地没有该模型，Ultralytics 会自动下载
    model = YOLO(model_name)

    file_path = Path(model_name).resolve()

    if file_path.exists():
        print(f"下载完成：{file_path}")
    else:
        print(f"模型已加载，但未在当前目录找到文件：{model_name}")

print("全部完成！")