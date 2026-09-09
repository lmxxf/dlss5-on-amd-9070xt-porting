"""Static arithmetic guards and layout coverage, not a GPU equivalence proof."""
from pathlib import Path
import re

old = Path('native_vit_attention.hlsl').read_text()
new = Path('native_wave_vit_attention.hlsl').read_text()
for signature in ['float H', 'float F', 'uint key_index']:
    assert next(s for s in old.splitlines() if s.startswith(signature)) == next(
        s for s in new.splitlines() if s.startswith(signature))
assert new.index('inverse[t]=H(1.0/denominator)') < new.index('ex[i]=half(F(float(ex[i])))')
assert 'H(acc.Get(i)+p.Get(i))' in new
assert 'F(H(acc.Get(i)*inverse[c.x]))' in new
for tokens in [64, 128, 256, 640]:
    assert tokens % 64 == 0 and tokens * 16 <= 640 * 16
    coords = {(query + row, head * 32 + channel + col)
              for query in range(0, tokens, 16) for head in range(32)
              for channel in [0, 16] for row in range(16) for col in range(16)}
    assert len(coords) == tokens * 1024
    assert min(coords) == (0, 0) and max(coords) == (tokens - 1, 1023)
print('ViT arithmetic helpers, quantization order and output coverage passed')
