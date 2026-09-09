"""Track bias coordinates through original CUBIN's first softmax partial sums.

This is symbolic provenance, not an instruction emulator or fitted formula.
Only supported operations preserve provenance; unknown writes invalidate it.
"""
import json
import re
import subprocess
import numpy as np

mapping = np.load('release/preblock-attention-layout/bias-layout.npz')
sass = subprocess.check_output(['/usr/local/cuda/bin/cuobjdump', '--dump-sass', '--function',
    'cc_tinlayout_fused_pre_block_swin_1h_32_1_ds_fp8', '/tmp/dlssnr-cubins/dlssnr-00.cubin'], text=True)
registers = {}
empty = [(None, None)] * 32
for line in sass.splitlines():
    match = re.search(r'/\*([0-9a-f]+)\*/\s+(\S+)\s+(.*?)\s*;', line)
    if not match:
        continue
    address, op, args = int(match[1], 16), match[2], match[3]
    if address < 0x69c0 or address > 0x81a0:
        continue
    operands = [part.strip() for part in args.split(',')]
    if not re.fullmatch(r'R\d+', operands[0]):
        continue
    dest = int(operands[0][1:])
    def read(name):
        return registers.get(int(name.split('.')[0][1:]), empty) if re.match(r'R\d+', name) else empty
    result = empty
    if op.startswith('LDG.E.128') and 'R160.64+' in args:
        offset = int(re.search(r'R160.64\+(0x[0-9a-f]+)', args)[1], 16)
        for reg in range(4):
            values = []
            for lane in range(32):
                slots = [(offset-0x3060)//2 + lane*8 + reg*2 + half for half in range(2)]
                values.append(tuple([int(mapping['query'][slot]), int(mapping['key'][slot])] for slot in slots))
            registers[dest+reg] = values
        continue
    if op.startswith('QMMA'):
        # Bias is the C operand; the QK product changes values, not coordinates.
        source = operands[3]
        values = [read(source), read('R'+str(int(source[1:])+1))] if source != 'RZ' else [empty, empty]
        registers[dest], registers[dest+1] = values
        continue
    if op == 'HFMA2' and 'R0.' in operands[2]:
        result = read(operands[1])
    elif op == 'HMNMX2':
        result = read(operands[1])
    elif op == 'LEA' and '0x7ff88000' in args:
        result = read(operands[1])
    elif op == 'HADD2':
        left, right = read(operands[1]), read(operands[2])
        result = [tuple(['+', a, b] if a is not None and b is not None else None for a, b in zip(x, y)) for x, y in zip(left, right)]
    registers[dest] = result

def select(dest, yes, no, bit, invert=False):
    left, right = registers[yes], registers[no]
    registers[dest] = [left[lane] if bool(lane & bit) != invert else right[lane] for lane in range(32)]

# 0x81d0..0x8360: transpose the 8x4 warp layout into four partial sums/query.
select(106,103,104,1);select(107,105,102,1)
select(103,104,103,1);select(102,102,105,1)
select(111,106,107,2,True);select(107,107,106,2,True)
select(106,103,102,2,True);select(110,102,103,2,True)
for reg, xor in [(111,0),(106,1),(107,2),(110,3)]:
    old = registers[reg]
    registers[reg] = [old[((lane%8)*4+lane//8)^xor] for lane in range(32)]
select(132,111,106,8,True);select(133,106,111,8,True)
select(111,107,110,8,True);select(106,110,107,8,True)
select(138,132,111,16,True);select(107,133,106,16,True)
select(132,111,132,16,True);select(106,106,133,16,True)
trees=[]
for lane in range(32):
    partial=[registers[reg][lane] for reg in (138,107,132,106)]
    totals=[]
    for half in range(2):
        total=partial[0][half]
        for pair in partial[1:]:total=['+',total,pair[half]]
        totals.append(total)
    trees.append(['+',*totals])
def leaves(tree):
    if tree[0]=='+':return leaves(tree[1])+leaves(tree[2])
    assert tree is not None
    return [tree]
for tree in trees:
    coords=leaves(tree)
    assert len(coords)==64 and len({q for q,k in coords})==1
    assert sorted(k for q,k in coords)==list(range(64))
print(json.dumps({'lane0_tree':trees[0], 'queries':[leaves(tree)[0][0] for tree in trees],
                  'lane0_key_order':[k for q,k in leaves(trees[0])]},indent=2))
