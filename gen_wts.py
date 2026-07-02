import sys  # noqa: F401
import argparse
import os
import struct
import torch


def parse_args():
    parser = argparse.ArgumentParser(description='Convert .pt file to .wts')

    parser.add_argument(
        '-w', '--weights',
        required=True,
        help='Input weights (.pt) file path (required)'
    )

    parser.add_argument(
        '-o', '--output',
        help='Output (.wts) file path (optional)'
    )

    parser.add_argument(
        '-t', '--type',
        type=str,
        default='detect',
        choices=['detect', 'cls', 'seg', 'pose', 'obb'],
        help='model type: detect / cls / seg / pose / obb'
    )

    args = parser.parse_args()

    if not os.path.isfile(args.weights):
        raise SystemExit(f'Invalid input file: {args.weights}')

    if not args.output:
        args.output = os.path.splitext(args.weights)[0] + '.wts'
    elif os.path.isdir(args.output):
        args.output = os.path.join(
            args.output,
            os.path.splitext(os.path.basename(args.weights))[0] + '.wts'
        )

    return args.weights, args.output, args.type


pt_file, wts_file, m_type = parse_args()

print(f'Generating .wts for {m_type} model')
print(f'Loading {pt_file}')

device = 'cpu'

model = torch.load(
    pt_file,
    map_location=device,
    weights_only=False
)['model'].float()

# detect / seg / pose / obb 都属于 YOLO head 类型
# 这里需要删除 anchors，否则 C++ 侧读取权重时可能和模型构建结构不匹配
if m_type in ['detect', 'seg', 'pose', 'obb']:
    if hasattr(model.model[-1], 'anchors'):
        anchor_grid = model.model[-1].anchors * model.model[-1].stride[..., None, None]
        delattr(model.model[-1], 'anchors')

model.to(device).eval()

with open(wts_file, 'w') as f:
    f.write('{}\n'.format(len(model.state_dict().keys())))

    for k, v in model.state_dict().items():
        vr = v.reshape(-1).cpu().numpy()
        f.write('{} {} '.format(k, len(vr)))

        for vv in vr:
            f.write(' ')
            f.write(struct.pack('>f', float(vv)).hex())

        f.write('\n')

print(f'Done. WTS saved to: {wts_file}')