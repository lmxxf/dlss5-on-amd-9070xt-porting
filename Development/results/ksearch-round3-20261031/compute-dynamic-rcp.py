#!/usr/bin/env python3
"""
Exact per-dispatch dynamic wave-level instruction and lane-level operation calculator
for R2-D (Softmax ones-WMMA denominator reciprocal 8 -> 1 hoist) and R3-A (C32 Softmax
CW_FMED3_CLAMP=1 v_med3_num_f32 clamp).

Notes:
- sp_recover256_w16 is launched with grid=1 as a timeout recovery guard and exits
  immediately when state[2]==0; its body is NOT executed in normal frames and is
  explicitly excluded from dynamic counts (0 dynamic iterations).
- For every active C32 (`c32_wave1_*`) and C64/C128/C256/SP (`c64/c128/c256_wave2_*`,
  `sp_run256_w16`) block, the Softmax query-tile loop (`for (uint qt = 0; qt < 4; qt++)`)
  is rolled (`_Pragma("clang loop unroll(disable)")`) and executes `qt = 4` iterations
  per wave per block.
- In each `qt` iteration, R2-D reduces `v_rcp_f32_e32` from 8 to 1 (saving 7 wave-level
  `v_rcp_f32_e32` instructions = 224 lane-level reciprocal operations per wave per `qt`).
- Note that `c32_wave1_*` and `c64/c128/c256_wave2_*` also contain 1 static `v_rcp_f32_e32`
  and 1 static `v_s_rcp_f32` outside Softmax (in Q/K normalization), which are unchanged.
"""
import json
from pathlib import Path

DECODER_SHIFTS = [0,3,1,2,0,3,1,2,0,3,1,2,0,3,1,2,1,2,0,3,1,2,0,3,1,2,0,3,1,2]
ENCODER_SHIFTS = [0,3,1,2,0,3,1,2]

def decoder_shift(block: int, small: bool) -> int:
    if not small:
        return DECODER_SHIFTS[block - 40]
    if block in (40, 48, 56, 62, 66):
        return 1
    if block in (47, 55, 61, 65, 69):
        return 2
    raise ValueError(f"Unexpected Small decoder block {block}")

def win_c32(w: int, h: int, shift: int) -> int:
    sx = 4 if (shift & 1) else 0
    sy = 4 if (shift & 2) else 0
    ww = w + 2 * sx
    hh = h + 2 * sy
    return (ww * hh) // 64

def win_mh(w: int, h: int, shift: int) -> int:
    sx = 4 if (shift & 1) else 0
    sy = 4 if (shift & 2) else 0
    ww = (w + sx + 7) & ~7
    hh = (h + sy + 7) & ~7
    return (ww * hh) // 64

def build_schedule(W: int, H: int, small: bool):
    rows = []
    # 1. Block 0: Prefix C32 (W/2, H/2, shift=0)
    w0, h0 = W // 2, H // 2
    rows.append({
        "stage": "C32", "blocks": "0", "kernel": "c32_wave1_prefix_b8d_w16",
        "w": w0, "h": h0, "shift": "0", "dispatches": 1, "layers": 1,
        "wgs": win_c32(w0, h0, 0), "waves_per_wg": 1, "qt": 4,
        "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
    })
    # 2. Encoder C32: Blocks 1..4 (Small: 1, 4)
    enc_c32_blocks = [1, 4] if small else [1, 2, 3, 4]
    for b in enc_c32_blocks:
        sh = (0 if b == 1 else 3) if small else ENCODER_SHIFTS[b - 1]
        k = "c32_wave1_mapped_b8_w16" if b == 1 else ("c32_wave1_finish_dcrop_pool_b8d_w16" if b == 4 else "c32_wave1_chain_w16")
        rows.append({
            "stage": "C32", "blocks": str(b), "kernel": k,
            "w": w0, "h": h0, "shift": str(sh), "dispatches": 1, "layers": 1,
            "wgs": win_c32(w0, h0, sh), "waves_per_wg": 1, "qt": 4,
            "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
        })
    # 3. Encoder C64 / C128 / C256
    starts = [5, 9, 15]
    counts = [2, 2, 2] if small else [4, 6, 8]
    channels = [64, 128, 256]
    for g in range(3):
        w, h, c = W // (4 << g), H // (4 << g), channels[g]
        w_per_wg = c // 32
        for j in range(counts[g]):
            b = (starts[g] if j == 0 else (8 if g == 0 else 14 if g == 1 else 22)) if small else (starts[g] + j)
            sh = ENCODER_SHIFTS[j]
            if not small and c == 256 and j == 1:
                # Persistent Swin run covering blocks 16..21 (6 layers in 1 dispatch)
                sp_wgs = sum(win_mh(w, h, ENCODER_SHIFTS[jj]) for jj in range(1, counts[g] - 1))
                rows.append({
                    "stage": "C256-SP", "blocks": "16..21", "kernel": "sp_run256_w16",
                    "w": w, "h": h, "shift": "3,1,2,0,3,1", "dispatches": 1, "layers": 6,
                    "wgs": sp_wgs, "waves_per_wg": w_per_wg, "qt": 4,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                rows.append({
                    "stage": "C256-SP", "blocks": "16..21 (guard)", "kernel": "sp_recover256_w16 (inactive guard)",
                    "w": w, "h": h, "shift": "-", "dispatches": 1, "layers": 0,
                    "wgs": 0, "waves_per_wg": w_per_wg, "qt": 0,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                continue
            if not small and c == 256 and 1 < j < counts[g] - 1:
                continue
            half_tail = (j + 1 == counts[g]) and g < 2
            byte_out = (j + 1 < counts[g])
            sfx = "_bi" + ("_bo" if byte_out else "") + ("_ho" if half_tail else "") + ("_w16" if c == 256 else "")
            rows.append({
                "stage": f"C{c}", "blocks": str(b), "kernel": f"c{c}_wave2{sfx}",
                "w": w, "h": h, "shift": str(sh), "dispatches": 1, "layers": 1,
                "wgs": win_mh(w, h, sh), "waves_per_wg": w_per_wg, "qt": 4,
                "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
            })
    # 4. Decoder C256 / C128 / C64
    ups = [48, 56, 62]
    cs = [256, 128, 64]
    ends = [55, 61, 65]
    for g in range(3):
        ow, oh, c = W // (16 >> g), H // (16 >> g), cs[g]
        w_per_wg = c // 32
        dec_blocks = [ups[g], ends[g]] if small else list(range(ups[g], ends[g] + 1))
        for b in dec_blocks:
            sh = decoder_shift(b, small)
            if not small and c == 256 and b == ups[g] + 1:
                sp_wgs = sum(win_mh(ow, oh, decoder_shift(bb, False)) for bb in range(ups[g] + 1, ends[g]))
                rows.append({
                    "stage": "C256-SP", "blocks": "49..54", "kernel": "sp_run256_w16",
                    "w": ow, "h": oh, "shift": "3,1,2,0,3,1", "dispatches": 1, "layers": 6,
                    "wgs": sp_wgs, "waves_per_wg": w_per_wg, "qt": 4,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                rows.append({
                    "stage": "C256-SP", "blocks": "49..54 (guard)", "kernel": "sp_recover256_w16 (inactive guard)",
                    "w": ow, "h": oh, "shift": "-", "dispatches": 1, "layers": 0,
                    "wgs": 0, "waves_per_wg": w_per_wg, "qt": 0,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                continue
            if not small and c == 256 and ups[g] + 1 < b < ends[g]:
                continue
            if b == ups[g] and g > 0:
                k = f"c{c}_wave2_up_lb_sh"
            else:
                k = f"c{c}_wave2_bi_bo" + ("_w16" if c == 256 else "")
            rows.append({
                "stage": f"C{c}", "blocks": str(b), "kernel": k,
                "w": ow, "h": oh, "shift": str(sh), "dispatches": 1, "layers": 1,
                "wgs": win_mh(ow, oh, sh), "waves_per_wg": w_per_wg, "qt": 4,
                "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
            })
    # 5. Decoder C32: Blocks 66..69 (Small: 66, 69)
    dec_c32_blocks = [66, 69] if small else [66, 67, 68, 69]
    for b in dec_c32_blocks:
        sh = decoder_shift(b, small)
        k = "c32_wave1_up_b8_w16" if b == 66 else ("c32_wave1_finish_b8_w16" if b == 69 else "c32_wave1_chain_w16")
        rows.append({
            "stage": "C32", "blocks": str(b), "kernel": k,
            "w": w0, "h": h0, "shift": str(sh), "dispatches": 1, "layers": 1,
            "wgs": win_c32(w0, h0, sh), "waves_per_wg": 1, "qt": 4,
            "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
        })
    # 6. Post 70: C32 (W, H, shift=0)
    rows.append({
        "stage": "C32", "blocks": "70", "kernel": "c32_wave1_post_b8_w16",
        "w": W, "h": H, "shift": "0", "dispatches": 1, "layers": 1,
        "wgs": win_c32(W, H, 0), "waves_per_wg": 1, "qt": 4,
        "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
    })

    for r in rows:
        r["dynamic_waves"] = r["wgs"] * r["waves_per_wg"]
        r["dynamic_qt_iters"] = r["dynamic_waves"] * r["qt"]
        r["wave_rcp_saved"] = r["dynamic_qt_iters"] * 7
        r["lane_rcp_saved"] = r["wave_rcp_saved"] * 32
        # For C32 kernels, R3-A replaces 31 v_cmp_gt_f32_e64 + 31 v_cndmask_b32_e64 per qt iteration
        is_c32 = r["stage"] == "C32"
        r["wave_cmp_cnd_saved_r3a"] = r["dynamic_qt_iters"] * 62 if is_c32 else 0
    return rows

def main():
    p = Path(__file__).parent
    profiles = {
        "original1088": (1920, 1088, False),
        "original1152": (1920, 1152, False),
        "original960":  (1600, 960, False),
        "small1080":    (1920, 1152, True),
        "small900":     (1600, 1024, True),
        "small720":     (1344, 768, True),
        "smallwindow":  (896, 512, True),
    }
    summary = {}
    for name, (W, H, small) in profiles.items():
        rows = build_schedule(W, H, small)
        c32_waves = sum(r["dynamic_waves"] for r in rows if r["stage"] == "C32")
        mh_waves = sum(r["dynamic_waves"] for r in rows if r["stage"] != "C32")
        tot_waves = c32_waves + mh_waves
        tot_qt = sum(r["dynamic_qt_iters"] for r in rows)
        wave_rcp_saved = sum(r["wave_rcp_saved"] for r in rows)
        lane_rcp_saved = sum(r["lane_rcp_saved"] for r in rows)
        wave_r3a_saved = sum(r["wave_cmp_cnd_saved_r3a"] for r in rows)
        summary[name] = {
            "processing_resolution": f"{W}x{H}",
            "model": "Small" if small else "Original",
            "c32_dynamic_waves": c32_waves,
            "mh_sp_dynamic_waves": mh_waves,
            "total_dynamic_waves": tot_waves,
            "total_dynamic_qt_iters": tot_qt,
            "r2d_wave_v_rcp_f32_saved": wave_rcp_saved,
            "r2d_lane_rcp_ops_saved": lane_rcp_saved,
            "r3a_c32_wave_cmp_cnd_saved": wave_r3a_saved,
            "r3a_c32_lane_cmp_cnd_saved": wave_r3a_saved * 32,
            "schedule": rows if name in ("original1088", "small1080") else None
        }
    (p / "dynamic-rcp-ledger.json").write_text(json.dumps(summary, indent=2))
    for k, v in summary.items():
        print(f"{k:14s} ({v['processing_resolution']:9s}, {v['model']:8s}): waves={v['total_dynamic_waves']:7,d} | qt_iters={v['total_dynamic_qt_iters']:7,d} | R2-D wave v_rcp_f32 saved={v['r2d_wave_v_rcp_f32_saved']:9,d} (lane ops={v['r2d_lane_rcp_ops_saved']:11,d}) | R3-A C32 wave cmp+cnd saved={v['r3a_c32_wave_cmp_cnd_saved']:10,d}")

if __name__ == "__main__":
    main()
