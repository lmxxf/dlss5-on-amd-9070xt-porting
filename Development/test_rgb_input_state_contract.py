"""Source control-flow regression: first ordinary Record must leave private buffers readable.
No D3D12 resource-state query exists; this check is not a GPU/debug-layer test.
"""
from pathlib import Path
import sys
p=Path(sys.argv[1]) if len(sys.argv)>1 else Path(__file__).parents[1]/"src/native_game_rgb_input.h"
s=p.read_text(); body=s.split(" void Record(",1)[1].split(" void RedirectOutput(",1)[0]
needle="transition(c,r,D3D12_RESOURCE_STATE_UNORDERED_ACCESS,D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE);"
line=next(x.strip() for x in body.splitlines() if needle in x)
assert line.startswith("for(auto*r:{tiles,color})"),"first non-replay Record incorrectly skips output UAV -> SRV"
assert "if(r&&!(external&&r==color))" in line,"external buffer must retain its separate COMMON path"
assert "if(replayable_recording||recorded)for" in body,"replay and subsequent frames require entry SRV -> UAV"
assert "replayable_recording?D3D12_RESOURCE_STATE_NON_PIXEL_SHADER_RESOURCE:D3D12_RESOURCE_STATE_UNORDERED_ACCESS" in s,"first ordinary buffers must start UAV"
assert body.index(needle)<body.index("recorded=true"),"read-state transition precedes first-record latch"
print("PASS first ordinary RGB-input output state, subsequent/replay entry and external COMMON contracts")
