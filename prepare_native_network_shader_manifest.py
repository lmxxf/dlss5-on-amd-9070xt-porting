"""Snapshot the complete source shader closure for full-network deployment."""
from pathlib import Path
import argparse,hashlib,json
p=argparse.ArgumentParser();p.add_argument('--color-frame',action='store_true');args=p.parse_args()
source=Path(__file__).resolve().parent
names=['native_rgb_reflect.hlsl','native_c32_reframe.hlsl','native_c32_ds.hlsl',
 'native_c64_ds.hlsl','native_c64.hlsl','native_c64_shift.hlsl','preblock_input_mix.hlsl',
 'preblock_attention_core.hlsl','preblock_finish.hlsl','native_split.hlsl','native_split_window.hlsl',
 'native_vit_attention.hlsl','native_vit_gather.hlsl','native_vit_linear.hlsl','native_vit_qkv.hlsl',
 'native_half_square.hlsli','native_post70.hlsl','native_temporal_sample.hlsl','native_temporal_coordinates.hlsl']
if args.color_frame:names+=['native_game_rgb_input.hlsl','native_codec_encode.hlsl','native_codec_decode.hlsl','native_rgb_texture.hlsl']
out=source/('release/native-color-frame' if args.color_frame else 'release/native-network-shader-manifest');out.mkdir(exist_ok=True)
entries=[{'name':name,'sha256':hashlib.sha256((source/name).read_bytes()).hexdigest()} for name in names]
(out/'shader-manifest.json').write_text(json.dumps(entries,indent=2)+'\n')
print(out/'shader-manifest.json')
