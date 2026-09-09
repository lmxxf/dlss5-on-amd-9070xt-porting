"""Check disjoint group ownership and unchanged quantizers (GPU test separate)."""
from pathlib import Path

old=Path('native_wave_split_ffwd.hlsl').read_text()
new=Path('native_wave_split_ffwd_parallel.hlsl').read_text()
for signature in ['float H', 'float F']:
    assert next(x for x in old.splitlines() if x.startswith(signature)) == next(
        x for x in new.splitlines() if x.startswith(signature))
rows=[g*64+b*16+c for g in range(8) for b in range(4) for c in range(16)]
assert rows==list(range(512))
expand=[262144+g*16384+r*64+k for g in range(8) for r in range(256) for k in range(64)]
contract=[393216+g*16384+r*256+k for g in range(8) for r in range(64) for k in range(256)]
assert expand==list(range(262144,393216))
assert contract==list(range(393216,524288))
assert 'A::Load(mixed,k*32,80,' in new
assert '((group*64+block*16)*512+k*32)*2' in new
print('8 disjoint FFWD groups cover all outputs/weights; quantizers unchanged')
