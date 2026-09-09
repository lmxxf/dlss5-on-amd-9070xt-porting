"""Inventory codec evidence in the installed reference addon, not its live mode."""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument('addon', type=Path)
parser.add_argument('report', type=Path)
args = parser.parse_args()
data = args.addon.read_bytes()
markers = {
    'codec_constants': b'cbuffer CodecConstants : register(b0)',
    'hdr_mode_field': b'uint HdrMode;',
    'srgb_encode': b'float3 SrgbEncode(float3 color)',
    'srgb_decode': b'float3 SrgbDecode(float3 color)',
    'tone_transfer': b'float3 UpgradeToneMap(float3 original, float3 proxy, float3 neural)',
    'hdr_conditional_ui': b'Codec: FP16 working surface; HDR soft-clip/sRGB only when NGX marks HDR.',
    'ngx_create_flags_key': b'DLSS.Feature.Create.Flags',
}
offsets = {key: data.find(value) for key, value in markers.items()}
assert all(offset >= 0 for offset in offsets.values()), offsets
report = dict(addon_sha256=hashlib.sha256(data).hexdigest(),
              marker_file_offsets=offsets, live_hdr_mode=None,
              game_color_contract_verified=False,
              scope='embedded strings prove available codec logic, not which branch ran; shader tail contains fragmented strings and is not reconstructed')
args.report.parent.mkdir(parents=True, exist_ok=True)
args.report.write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps(report, indent=2))
