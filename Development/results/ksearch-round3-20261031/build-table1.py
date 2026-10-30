#!/usr/bin/env python3
"""
Generate `table1-isa-resources.json` for Round 3 with corrected RDNA4 (`gfx1201`)
hardware topology:
- 4 SIMD32/WGP (128 KiB LDS/WGP), 2 SIMD32/CU (64 KiB LDS/CU).
- Multi-wave WGs in WGP mode are bounded by 32 WGs/WGP (16 WGs/CU), while single-wave
  WGs (`1w/WG`) do NOT consume workgroup barrier slots and are bounded only by LDS
  capacity (or unbounded by LDS/WG when `LDS = 0`).
- Explicitly splits `v_rcp_f32_e32` and `v_s_rcp_f32`.
- Tracks SGPR and `v_cmp_gt_f32_e64` + `v_cndmask_b32_e64` reductions from R3-A
  (`CW_FMED3_CLAMP=1`).
"""
import json, subprocess, importlib.util
from pathlib import Path

p = Path(__file__).parent
sp = importlib.util.spec_from_file_location('compare', 'Development/tools/llvm-fork/compare-kernels.py')
m = importlib.util.module_from_spec(sp)
sp.loader.exec_module(m)

p8a = Path('Development/results/c32-stage-phase5b-20261029/payload/HIP/gfx1201')
pe7 = Path('Development/results/ksearch-step4-20261031/payload/HIP/gfx1201')
pr2 = Path('Development/results/ksearch-round2-20261031/payload/HIP/gfx1201')
pr3 = Path('Development/results/ksearch-round3-20261031/payload/HIP/gfx1201')

t2 = json.loads(Path('Development/results/ksearch-round2-20261031/table1-isa-resources.json').read_text())

cache = {}
def get_elf(path):
    if path not in cache:
        cache[path] = m.elf(path)
    return cache[path]

def disasm_stats(hsaco_path, sym_dict, kname):
    addr, size, _ = sym_dict[kname]
    out = subprocess.check_output([
        '/home/lmxxf/work/llvm-build-23/bin/llvm-objdump', '-d', '--triple=amdgcn-amd-amdhsa', '--mcpu=gfx1201',
        f'--start-address={addr}', f'--stop-address={addr+size}', str(hsaco_path)
    ], text=True)
    insns = 0
    v_rcp = 0
    vs_rcp = 0
    cmp_e64 = 0
    cnd_e64 = 0
    med3 = 0
    for line in out.splitlines():
        if '//' in line and ':' in line.split('//')[1]:
            parts = line.strip().split()
            if parts:
                op = parts[0]
                insns += 1
                if op.startswith('v_rcp_f32'): v_rcp += 1
                elif op.startswith('v_s_rcp_f32'): vs_rcp += 1
                elif op == 'v_cmp_gt_f32_e64': cmp_e64 += 1
                elif op == 'v_cndmask_b32_e64': cnd_e64 += 1
                elif op.startswith('v_med3_num_f32') or op.startswith('v_med3_f32'): med3 += 1
    return insns, v_rcp, vs_rcp, cmp_e64, cnd_e64, med3

def compute_lds_residency(lds_bytes: int, wgp_mode: int, waves_per_wg: int):
    simds = 4 if wgp_mode else 2
    total_lds = 131072 if wgp_mode else 65536
    unit = "WGP(128KiB, 4 SIMD32)" if wgp_mode else "CU(64KiB, 2 SIMD32)"
    if lds_bytes == 0 and waves_per_wg == 1:
        # Single-wave WG with 0 LDS: not limited by LDS or multi-wave WG barrier slots
        return None, f"{unit}: 0 LDS, 1w/WG (unbounded by LDS/barrier)"
    wgs_by_lds = total_lds // lds_bytes if lds_bytes > 0 else 64
    if waves_per_wg > 1:
        max_wgs = 32 if wgp_mode else 16
        wgs = min(wgs_by_lds, max_wgs)
    else:
        wgs = wgs_by_lds
    waves_per_simd = (wgs * waves_per_wg) / simds
    return waves_per_simd, f"{unit}: {wgs} WGs/{'WGP' if wgp_mode else 'CU'} = {waves_per_simd:.1f} waves/SIMD"

out_rows = []
for row in t2:
    mod = row['module']
    k = row['kernel']
    old_k = row['route_old_kernel']
    _, a8, sa8 = get_elf(p8a / mod)
    _, ae, sae = get_elf(pe7 / mod)
    _, ar2, sar2 = get_elf(pr2 / mod)
    _, ar3, sar3 = get_elf(pr3 / mod)

    i8, vr8, vsr8, _, _, _ = disasm_stats(p8a / mod, sa8, k)
    iro8, _, _, _, _, _ = disasm_stats(p8a / mod, sa8, old_k)
    ie, vre, vsre, _, _, _ = disasm_stats(pe7 / mod, sae, k)
    ir2, vrr2, vsrr2, cmp2, cnd2, med2 = disasm_stats(pr2 / mod, sar2, k)
    ir3, vrr3, vsrr3, cmp3, cnd3, med3 = disasm_stats(pr3 / mod, sar3, k)

    lds_waves, lds_desc = compute_lds_residency(row['lds'], row['wgp_mode'], row['waves_per_wg'])
    vgpr_waves = row['waves_simd_vgpr']
    eff_waves = vgpr_waves if lds_waves is None else min(float(vgpr_waves), float(lds_waves))

    new_row = {
        "module": mod,
        "compiler": row["compiler"],
        "kernel": k,
        "route_old_kernel": old_k,
        "vgpr_same_sym_8a": a8[k][".vgpr_count"],
        "vgpr_route_old_8a": a8[old_k][".vgpr_count"],
        "vgpr_e7": ae[k][".vgpr_count"],
        "vgpr_r2": ar2[k][".vgpr_count"],
        "vgpr_r3": ar3[k][".vgpr_count"],
        "sgpr_r2": ar2[k].get(".sgpr_count", 0),
        "sgpr_r3": ar3[k].get(".sgpr_count", 0),
        "lds": row["lds"],
        "wgp_mode": row["wgp_mode"],
        "waves_per_wg": row["waves_per_wg"],
        "waves_simd_vgpr": vgpr_waves,
        "waves_simd_lds": lds_waves,
        "waves_simd_eff": eff_waves,
        "lds_scope": lds_desc,
        "insns_same_sym_8a": i8,
        "insns_route_old_8a": iro8,
        "insns_e7": ie,
        "insns_r2": ir2,
        "insns_r3": ir3,
        "v_rcp_f32_e32_e7": vre,
        "v_rcp_f32_e32_r2": vrr2,
        "v_rcp_f32_e32_r3": vrr3,
        "v_s_rcp_f32_e7": vsre,
        "v_s_rcp_f32_r2": vsrr2,
        "v_s_rcp_f32_r3": vsrr3,
        "v_cmp_gt_f32_e64_r2": cmp2,
        "v_cmp_gt_f32_e64_r3": cmp3,
        "v_cndmask_b32_e64_r2": cnd2,
        "v_cndmask_b32_e64_r3": cnd3,
        "v_med3_num_f32_r2": med2,
        "v_med3_num_f32_r3": med3,
    }
    out_rows.append(new_row)

(p / "table1-isa-resources.json").write_text(json.dumps(out_rows, indent=2))
print("Wrote table1-isa-resources.json with", len(out_rows), "rows.")
