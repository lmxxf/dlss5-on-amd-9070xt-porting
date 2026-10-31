#!/usr/bin/env python3
"""
Analytical dispatch & wave/lane dynamic instruction model for:
- R2-D: Softmax ones-WMMA denominator reciprocal 8 -> 1 hoist (`v_rcp_f32_e32`)
- R3-A: C32 `CW_FMED3_CLAMP=1` (`v_med3_num_f32` replacing 31 `v_cmp_gt_f32_e64` + 31 `v_cndmask_b32_e64` in Softmax)
- R4-A: Softmax paired exponent bit-map merge (`__builtin_amdgcn_perm(bits(af1), bits(af0), 0x06050201u) & 0x7fe07fe0u`,
  saving 32 `v_lshrrev_b32` + 16 `v_xor_b32` = 48 VALU instructions per wave per qt iteration in C32/C64/C128/C256/SP).
  C512 compact attention and ViT are not included in this ledger.
  R4-A fields count eliminated lshr/xor instructions, not a census of all replacement/opcode deltas or total executed VALU.

Model Scope & Audit Notes (Round 5 corrections included):
- Kernel labels describe logical attention bodies, not a complete observed dispatch census. In original960,
  the four C256 boundary layers use c256_attn_wave{,_bo} attention-only exports; their shared W2 Softmax
  body still has eight waves per window and four qt iterations, so the counted arithmetic is unchanged.
1. Analytical Model vs Hardware Performance Counter:
   - This script is an analytical launch-grid and loop-iteration model derived from `hip_reference_network.h` and
     `production_options.generated.h` (verified against runtime `SP_PLAN`, `C32_POOL_FUSED`, and `C512_W16_SUMMARY` logs),
     NOT a hardware performance counter readout.
2. Corrected Block 70 (`c32_wave1_post_b8_w16`) `post_shift = 3`:
   - `production_options.generated.h` sets `o.post_shift = 3` (`sx = 4, sy = 4`), and `hip_reference_network.h:1110` launches
     `windows = (W + 2*sx) * (H + 2*sy) / 64`.
   - For `original1088` (`1920x1088`), Block 70 launches `1928 * 1096 / 64 = 33,017` windows (not `32,640`).
   - For `original1152` / `small1080` (`1920x1152`), Block 70 launches `1928 * 1160 / 64 = 34,945` windows (not `34,560`).
3. Launch Grid vs Active Body Waves & Boundary Predication:
   - `sp_recover256_w16` is launched with `grid = 1` as an abort recovery guard and exits at line 115 (`if (!sp_load(p.state+2)) return;`);
     its body waves are `0`.
   - For `sp_run256_w16`, the launch grid is `total_tasks` workgroups (SpStage passes plan.total), where each workgroup pops exactly 1 task
     from the queue of `total_tasks` (`846` tasks for `1920x1088` across 6 layers: `144*4 + 135*2 = 846`, matching runtime `SP_PLAN tasks=846`).
     Thus the number of executed layer-window bodies is exactly `total_tasks = 846` (`846 * 8 = 6,768` body waves per SP stage).
   - Within every active C32 (`c32_wave1_*`) and C64/C128/C256/SP (`c64/c128/c256_wave2_*`, `sp_run256_w16`) window body, `win < windows`
     holds for 100% of launched workgroups, and the Softmax `for (uint qt = 0; qt < 4; qt++)` loop executes unconditionally for all 4 tiles
     without boundary early-exit (boundary predication only guards global input loads and output stores).
4. Static `v_rcp_f32_e32` vs `v_s_rcp_f32` Outside Softmax:
   - In `c32_wave1_*` and `c64/c128/c256_wave2_*`, Q/K normalization uses `__builtin_amdgcn_rsqf` (`v_rsq_f32`), while coordinate division /
     auxiliary setup outside Softmax accounts for the remaining static `v_rcp_f32_e32` / `v_s_rcp_f32`.
"""
import json
from pathlib import Path

DECODER_SHIFTS = [0,3,1,2,0,3,1,2,0,3,1,2,0,3,1,2,1,2,0,3,1,2,0,3,1,2,0,3,1,2]
ENCODER_SHIFTS = [0,3,1,2,0,3,1,2]
POST_SHIFT = 3  # production_options.generated.h: o.post_shift = 3 (sx=4, sy=4)

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
        "launched_wgs": win_c32(w0, h0, 0), "body_wgs": win_c32(w0, h0, 0), "waves_per_wg": 1, "qt": 4,
        "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
    })
    # 2. Encoder C32: Blocks 1..4 (Small: 1, 4)
    enc_c32_blocks = [1, 4] if small else [1, 2, 3, 4]
    for b in enc_c32_blocks:
        sh = (0 if b == 1 else 3) if small else ENCODER_SHIFTS[b - 1]
        k = "c32_wave1_mapped_b8_w16" if b == 1 else ("c32_wave1_finish_dcrop_pool_b8d_w16" if b == 4 else "c32_wave1_chain_w16")
        wgs = win_c32(w0, h0, sh)
        rows.append({
            "stage": "C32", "blocks": str(b), "kernel": k,
            "w": w0, "h": h0, "shift": str(sh), "dispatches": 1, "layers": 1,
            "launched_wgs": wgs, "body_wgs": wgs, "waves_per_wg": 1, "qt": 4,
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
                sp_tasks = sum(win_mh(w, h, ENCODER_SHIFTS[jj]) for jj in range(1, counts[g] - 1))
                rows.append({
                    "stage": "C256-SP", "blocks": "16..21", "kernel": "sp_run256_w16",
                    "w": w, "h": h, "shift": "3,1,2,0,3,1", "dispatches": 1, "layers": 6,
                    "launched_wgs": sp_tasks, "body_wgs": sp_tasks, "waves_per_wg": w_per_wg, "qt": 4,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                rows.append({
                    "stage": "C256-SP", "blocks": "16..21 (guard)", "kernel": "sp_recover256_w16 (inactive guard)",
                    "w": w, "h": h, "shift": "-", "dispatches": 1, "layers": 0,
                    "launched_wgs": 1, "body_wgs": 0, "waves_per_wg": w_per_wg, "qt": 0,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                continue
            if not small and c == 256 and 1 < j < counts[g] - 1:
                continue
            half_tail = (j + 1 == counts[g]) and g < 2
            byte_out = (j + 1 < counts[g])
            sfx = "_bi" + ("_bo" if byte_out else "") + ("_ho" if half_tail else "") + ("_w16" if c == 256 else "")
            wgs = win_mh(w, h, sh)
            rows.append({
                "stage": f"C{c}", "blocks": str(b), "kernel": f"c{c}_wave2{sfx}",
                "w": w, "h": h, "shift": str(sh), "dispatches": 1, "layers": 1,
                "launched_wgs": wgs, "body_wgs": wgs, "waves_per_wg": w_per_wg, "qt": 4,
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
                sp_tasks = sum(win_mh(ow, oh, decoder_shift(bb, False)) for bb in range(ups[g] + 1, ends[g]))
                rows.append({
                    "stage": "C256-SP", "blocks": "49..54", "kernel": "sp_run256_w16",
                    "w": ow, "h": oh, "shift": "3,1,2,0,3,1", "dispatches": 1, "layers": 6,
                    "launched_wgs": sp_tasks, "body_wgs": sp_tasks, "waves_per_wg": w_per_wg, "qt": 4,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                rows.append({
                    "stage": "C256-SP", "blocks": "49..54 (guard)", "kernel": "sp_recover256_w16 (inactive guard)",
                    "w": ow, "h": oh, "shift": "-", "dispatches": 1, "layers": 0,
                    "launched_wgs": 1, "body_wgs": 0, "waves_per_wg": w_per_wg, "qt": 0,
                    "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 0
                })
                continue
            if not small and c == 256 and ups[g] + 1 < b < ends[g]:
                continue
            if b == ups[g] and g > 0:
                k = f"c{c}_wave2_up_lb_sh"
            else:
                k = f"c{c}_wave2_bi_bo" + ("_w16" if c == 256 else "")
            wgs = win_mh(ow, oh, sh)
            rows.append({
                "stage": f"C{c}", "blocks": str(b), "kernel": k,
                "w": ow, "h": oh, "shift": str(sh), "dispatches": 1, "layers": 1,
                "launched_wgs": wgs, "body_wgs": wgs, "waves_per_wg": w_per_wg, "qt": 4,
                "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
            })
    # 5. Decoder C32: Blocks 66..69 (Small: 66, 69)
    dec_c32_blocks = [66, 69] if small else [66, 67, 68, 69]
    for b in dec_c32_blocks:
        sh = decoder_shift(b, small)
        k = "c32_wave1_up_b8_w16" if b == 66 else ("c32_wave1_finish_b8_w16" if b == 69 else "c32_wave1_chain_w16")
        wgs = win_c32(w0, h0, sh)
        rows.append({
            "stage": "C32", "blocks": str(b), "kernel": k,
            "w": w0, "h": h0, "shift": str(sh), "dispatches": 1, "layers": 1,
            "launched_wgs": wgs, "body_wgs": wgs, "waves_per_wg": 1, "qt": 4,
            "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
        })
    # 6. Post 70: C32 (W, H, shift=POST_SHIFT=3 -> sx=4, sy=4, ww=W+8, hh=H+8)
    wgs_post = win_c32(W, H, POST_SHIFT)
    rows.append({
        "stage": "C32", "blocks": "70", "kernel": "c32_wave1_post_b8_w16",
        "w": W, "h": H, "shift": str(POST_SHIFT), "dispatches": 1, "layers": 1,
        "launched_wgs": wgs_post, "body_wgs": wgs_post, "waves_per_wg": 1, "qt": 4,
        "static_v_rcp_f32_e32_before": 8, "static_v_rcp_f32_e32_after": 1, "static_v_s_rcp_f32": 1
    })

    for r in rows:
        r["wgs"] = r["body_wgs"]
        r["dynamic_waves"] = r["body_wgs"] * r["waves_per_wg"]
        r["dynamic_qt_iters"] = r["dynamic_waves"] * r["qt"]
        r["wave_rcp_saved"] = r["dynamic_qt_iters"] * 7
        r["lane_rcp_saved"] = r["wave_rcp_saved"] * 32
        is_c32 = r["stage"] == "C32"
        r["wave_cmp_cnd_saved_r3a"] = r["dynamic_qt_iters"] * 62 if is_c32 else 0
        # Round5 correction: kt/key has four unrolled groups inside rolled qt. Save 48 per wave per qt, 192 per wave per four-qt body.
        r["wave_valu_saved_r4a"] = r["dynamic_qt_iters"] * 48
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
        if name == "original1088":
            # Runtime cross-check assertions (SP_PLAN tasks=846, post70 shift=3 -> 33,017 windows)
            sp_rows = [r for r in rows if r["kernel"] == "sp_run256_w16"]
            assert len(sp_rows) == 2 and all(r["body_wgs"] == 846 for r in sp_rows)
            post_row = next(r for r in rows if r["blocks"] == "70")
            assert post_row["shift"] == "3" and post_row["body_wgs"] == 33017
        c32_waves = sum(r["dynamic_waves"] for r in rows if r["stage"] == "C32")
        mh_waves = sum(r["dynamic_waves"] for r in rows if r["stage"] != "C32")
        tot_waves = c32_waves + mh_waves
        tot_qt = sum(r["dynamic_qt_iters"] for r in rows)
        wave_rcp_saved = sum(r["wave_rcp_saved"] for r in rows)
        lane_rcp_saved = sum(r["lane_rcp_saved"] for r in rows)
        wave_r3a_saved = sum(r["wave_cmp_cnd_saved_r3a"] for r in rows)
        wave_r4a_saved = sum(r["wave_valu_saved_r4a"] for r in rows)
        summary[name] = {
            "model_kind": "analytical_launch_and_loop_model",
            "post_shift": POST_SHIFT,
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
            "r4a_c32_mh_wave_valu_saved": wave_r4a_saved,
            "r4a_c32_mh_lane_valu_saved": wave_r4a_saved * 32,
            "schedule": rows if name in ("original1088", "small1080") else None
        }
    (p / "dynamic-rcp-ledger.json").write_text(json.dumps(summary, indent=2))
    for k, v in summary.items():
        print(f"{k:14s} ({v['processing_resolution']:9s}, {v['model']:8s}): waves={v['total_dynamic_waves']:7,d} | qt={v['total_dynamic_qt_iters']:7,d} | R2-D rcp={v['r2d_wave_v_rcp_f32_saved']:9,d} | R3-A cmp+cnd={v['r3a_c32_wave_cmp_cnd_saved']:10,d} | R4-A lshr+xor={v['r4a_c32_mh_wave_valu_saved']:9,d}")

if __name__ == "__main__":
    main()
