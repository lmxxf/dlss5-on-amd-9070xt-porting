"""Guard against replacing scalar E4M3 constants when parameterizing geometry."""
from pathlib import Path
import re
for name in ('native_head_pool.hlsl','native_wave_head_project.hlsl'):
    source=Path(name).read_text()
    body=re.search(r'float F\(float v\)\{([^\n]+)\}',source).group(1)
    assert 'MATRIX_CHANNELS' not in body and 'width' not in body and 'height' not in body,name
    assert 'round(a*512)/512' in body and '448' in body,name
print('head quantization independent of channel count: pass')
