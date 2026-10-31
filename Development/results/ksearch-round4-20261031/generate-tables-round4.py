#!/usr/bin/env python3
"""
Programmatically generate GENERATED-TABLES-ROUND4.md from raw JSON/CSV artifacts.
Zero manual copying.
"""
import json
from pathlib import Path

p = Path(__file__).parent
load = lambda f: json.loads(f.read_text(encoding='utf-8-sig'))

t1 = load(p / 'table1-isa-resources.json')
dyn_rcp = load(p / 'dynamic-rcp-ledger.json')
r2_stats = load(p.parent / 'ksearch-round2-20261031/final-statistics.json')['tiers']
r3_stats = load(p.parent / 'ksearch-round3-20261031/final-statistics.json')['tiers']
r4a_stats = load(p / 'r4a-statistics.json')['tiers']
r4_stats = load(p / 'final-statistics.json')['tiers']
r3a_4arm = load(p / 'r3a_r2d_4arm-statistics.json')['summary']
pure_abba = load(p / 'final-pure-abba-statistics.json')['tiers']
nmmn = load(p / 'final-nmmn-statistics.json')['nmmn']
probes_r2 = load(p.parent / 'ksearch-round2-20261031/probes-2x2-statistics.json')['probes']
probes_r3 = load(p.parent / 'ksearch-round3-20261031/probes-sm-2x2-statistics.json')['summary']
probes_r4 = load(p / 'probes-sm-2x2-statistics.json')['summary']
ledger = load(p / 'final-stage-ledger.json')
ae_reg = load(p / 'ae_reuse_reg-summary.json')

lines = []

# Table 1A
lines.append("### 表 1A：修改核静态 ISA 指令净差、同符号 vs 实际路由基线与 RDNA4 (`4 SIMD32/WGP`, `2 SIMD32/CU`) 硬件驻留表（`table1-isa-resources.json`）")
lines.append("")
lines.append("| 所属模块 (`.hsaco`) | 核函数符号 | 编译器 | 同符号 VGPR (`8a9c` $\\to$ `b1af` $\\to$ `1005` $\\to$ **R4**) | 实际路由 VGPR (`8a9c` $\\to$ **R4**) | SGPR (`b1af` $\\to$ `1005` $\\to$ **R4**) | LDS (B) & 模式 | VGPR 上限 | LDS/WG 上限 | **有效驻留 $\\min$** | 同符号指令 (`8a9c` $\\to$ `b1af` $\\to$ `1005` $\\to$ **R4**) | 实际路由指令 (`8a9c` $\\to$ **R4** $\\Delta$) | `v_rcp_f32` (`e7b2` $\\to$ **R4**) | `v_cmp_gt_e64`+`cnd_e64` (`b1af` $\\to$ **R4**) | `v_lshrrev_b32`+`v_xor_b32` (`1005` $\\to$ **R4**) |")
lines.append("| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |")

for r in t1:
    mod_short = r["module"].removesuffix(".hsaco")
    k = r["kernel"]
    old_k = r["route_old_kernel"]
    vgpr_sym = f"`{r['vgpr_same_sym_8a']} -> {r['vgpr_r2']} -> {r['vgpr_r3']} ->` **`{r['vgpr_r4']}`**"
    if old_k != k:
        short_old = old_k.replace("split_projection_frag_", "").replace("mh_attention_project_frag_c512_", "")
        vgpr_route = f"`{r['vgpr_route_old_8a']}` (`{short_old}`) $\\to$ **`{r['vgpr_r4']}`**"
    else:
        vgpr_route = f"`{r['vgpr_route_old_8a']} ->` **`{r['vgpr_r4']}`**"
    if r["sgpr_r2"] != r["sgpr_r3"] or r["sgpr_r3"] != r["sgpr_r4"]:
        sgpr_str = f"`{r['sgpr_r2']} -> {r['sgpr_r3']} ->` **`{r['sgpr_r4']}`**"
    else:
        sgpr_str = f"`{r['sgpr_r4']}`"
    mode_str = f"{r['lds']:,} ({'WGP' if r['wgp_mode'] else 'CU'}, {r['waves_per_wg']}w/WG)"
    lds_lim = "不限 (0 LDS, 1w/WG)" if r["waves_simd_lds"] is None else f"{int(r['waves_simd_lds']) if r['waves_simd_lds'].is_integer() else r['waves_simd_lds']}"
    eff_lim = f"**{int(r['waves_simd_eff']) if float(r['waves_simd_eff']).is_integer() else r['waves_simd_eff']}**"
    insn_sym = f"`{r['insns_same_sym_8a']} -> {r['insns_r2']} -> {r['insns_r3']} ->` **`{r['insns_r4']}`**"
    delta_route = r["insns_r4"] - r["insns_route_old_8a"]
    insn_route = f"`{r['insns_route_old_8a']} ->` **`{r['insns_r4']} ({delta_route:+d})`**"
    v_rcp_str = f"`{r['v_rcp_f32_e32_e7']} ->` **`{r['v_rcp_f32_e32_r4']}`**" if r["v_rcp_f32_e32_e7"] != r["v_rcp_f32_e32_r4"] else f"`{r['v_rcp_f32_e32_r4']}`"
    cmp_cnd_str = f"`{r['v_cmp_gt_f32_e64_r2']}+{r['v_cndmask_b32_e64_r2']} ->` **`{r['v_cmp_gt_f32_e64_r4']}+{r['v_cndmask_b32_e64_r4']}`**" if (r["v_cmp_gt_f32_e64_r2"] or r["v_cndmask_b32_e64_r2"]) else "`0+0`"
    if r["v_lshrrev_b32_r3"] != r["v_lshrrev_b32_r4"] or r["v_xor_b32_r3"] != r["v_xor_b32_r4"]:
        lshr_xor_str = f"`{r['v_lshrrev_b32_r3']}+{r['v_xor_b32_r3']} ->` **`{r['v_lshrrev_b32_r4']}+{r['v_xor_b32_r4']}`**"
    else:
        lshr_xor_str = f"`{r['v_lshrrev_b32_r4']}+{r['v_xor_b32_r4']}`"
    lines.append(f"| `{mod_short}` | `{k}` | {r['compiler']} | {vgpr_sym} | {vgpr_route} | {sgpr_str} | {mode_str} | {r['waves_simd_vgpr']} | {lds_lim} | {eff_lim} | {insn_sym} | {insn_route} | {v_rcp_str} | {cmp_cnd_str} | {lshr_xor_str} |")

lines.append("")
lines.append("### 表 1B：基于实际启动网格与循环计数的解析推导模型汇总表（回应审单 P2-2，修正 `post_shift=3` 并加入 R4-A，由 `compute-dynamic-rcp.py` 生成，`dynamic-rcp-ledger.json`）")
lines.append("")
lines.append("> **解析模型边界说明（回应 P2-2）**：① 本表为基于 `hip_reference_network.h` 与 `production_options.generated.h` 实际启动网格与循环计数的**解析推导模型**（非硬件性能计数器直接读数）；② 已将 Block 70 (`c32_wave1_post_b8_w16`) 修正为 `post_shift = 3`（`sx=4, sy=4` $\\implies$ `ww=W+8, hh=H+8`，如 `original1088` 为 `1928×1096/64 = 33,017` 窗口，`original1152`/`small1080` 为 `1928×1160/64 = 34,945` 窗口）；③ `sp_recover256_w16` 启动网格 `grid=1` 并在入口立即退出（`body_wgs = 0`）；`sp_run256_w16` 持久网格 `launched_wgs = 256` 从任务队列领取 `body_wgs = 846` 个窗口任务（与运行时 `SP_PLAN tasks=846` 日志完全一致）；④ 每次 `qt` 迭代中，R2-D 省 `7` 条 `v_rcp_f32_e32`，R3-A 在 C32 核省 `62` 条 `cmp_gt_e64 + cndmask_e64`，R4-A 在 C32/C64/C128/C256/SP 核省 `8` 条 `v_lshrrev_b32` + `4` 条 `v_xor_b32`（每 wave 每 block 省 `48` 条 VALU 指令）。")
lines.append("")
lines.append("| 档位 | 处理分辨率 | 模型 | C32 活跃窗口 Waves/帧 | C64/128/256/SP 活跃窗口 Waves/帧 | 全网活跃窗口 Waves/帧 | 全网 `qt` 迭代/帧 (`×4`) | **R2-D 省 Wave 级 `v_rcp_f32` (`×7`)** | **R3-A 省 C32 Wave 级 `cmp+cnd_e64` (`×62`)** | **R4-A 省 C32/MH/SP Wave 级 `lshr+xor` (`×12`)** |")
lines.append("| :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
for prof in ["original1152", "original1088", "original960", "small1080", "small900", "small720", "smallwindow"]:
    d = dyn_rcp[prof]
    lines.append(f"| **`{prof}`** | `{d['processing_resolution']}` | {d['model']} | {d['c32_dynamic_waves']:,} | {d['mh_sp_dynamic_waves']:,} | {d['total_dynamic_waves']:,} | {d['total_dynamic_qt_iters']:,} | **{d['r2d_wave_v_rcp_f32_saved']:,}** | **{d['r3a_c32_wave_cmp_cnd_saved']:,}** | **{d['r4a_c32_mh_wave_valu_saved']:,}** |")

def fmt_abba_table(title, stats_dict, order=None):
    lines.append("")
    lines.append(f"### {title}")
    lines.append("")
    lines.append("| 档位 | 基线 A mean (ms) | 候选 B mean (ms) | **$\\Delta$mean (µs)** | **$\\Delta$mean (%)** | 基线 A p99 (ms) | 候选 B p99 (ms) | **$\\Delta$p99 (µs)** | 每轮 $\\Delta$mean (µs) (R1 / R2 / R3) | 每轮 $\\Delta$p99 (µs) (R1 / R2 / R3) | mean / p99 赢轮 |")
    lines.append("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- | :--- | :---: |")
    keys = order if order else sorted(stats_dict.keys(), key=lambda k: stats_dict[k]["delta_ms"])
    for k in keys:
        x = stats_dict[k]
        r_means = " / ".join(f"{r['delta_ms']*1000:+.3f}" for r in x["rounds"])
        r_p99s = " / ".join(f"{r['p99_delta_ms']*1000:+.3f}" for r in x["rounds"])
        mw = sum(r["delta_ms"] < 0 for r in x["rounds"])
        pw = sum(r["p99_delta_ms"] <= 0 for r in x["rounds"])
        lines.append(f"| **`{k}`** | {x['baseline_ms']:.6f} | **{x['candidate_ms']:.6f}** | **{x['delta_ms']*1000:+.3f}** | **{x['delta_percent']:+.2f}%** | {x['baseline_p99']:.6f} | **{x['candidate_p99']:.6f}** | **{x['p99_delta_ms']*1000:+.3f}** | `{r_means}` | `{r_p99s}` | **{mw}/3**、**{pw}/3** |")

fmt_abba_table("表 2.3A：Round 4 正式生产载荷 (`1005424b + R4-A`) vs Phase 5B 基线 `8a9c60fd` 七档 3 轮 4-slot Frame ABBA 机器生成表（`ksearch-round4-20261031/final-statistics.json`）", r4_stats)
fmt_abba_table("表 2.3B：Round 4 单刀 `R4-A` (`v_perm_b32(..., 0x06050201u) & 0x7fe07fe0u`) vs Round 3 基线 `1005424b` 四档 3 轮 4-slot Frame ABBA 机器生成表（`ksearch-round4-20261031/r4a-statistics.json`）", r4a_stats, ["original1088", "original960", "small1080", "small900"])
fmt_abba_table("表 2.3C：Round 3 生产载荷 (`1005424b`) vs `8a9c60fd` 七档 3 轮 4-slot Frame ABBA 历史对照表（`ksearch-round3-20261031/final-statistics.json`）", r3_stats)

lines.append("")
lines.append("### 表 2.3D：`R3-A + K5` 固定新基线上的 R2-D 四臂同批交错成分消融表（回应审单 P2-5，`r3a_r2d_4arm-statistics.json`）")
lines.append("")
lines.append("| 档位 | 消融实验臂 (`R3-A + K5` 固定) | mean (ms) | 相对 `A0_NoR2D` $\\Delta$mean (µs) | 相对 `A3_R2D_Both` $\\Delta$mean (µs) | pooled p99 (ms) | 相对 `A0_NoR2D` $\\Delta$p99 (µs) | 每轮相对 `A0_NoR2D` $\\Delta$mean (µs) (R1 / R2 / R3) |")
lines.append("| :--- | :--- | ---: | ---: | ---: | ---: | ---: | :--- |")
for prof in ["small1080", "original1088"]:
    for arm_key, arm_data in r3a_4arm[prof].items():
        r_str = " / ".join(f"{r['delta_vs_A0_us']:+.2f}" for r in arm_data["rounds"])
        lines.append(f"| **`{prof}`** | **`{arm_key}`** | {arm_data['mean_ms']:.6f} | **{arm_data['delta_vs_A0_us']:+.2f}** | **{arm_data['delta_vs_A3_us']:+.2f}** | {arm_data['p99_ms']:.6f} | **{arm_data['p99_delta_vs_A0_us']:+.2f}** | `{r_str}` |")

lines.append("")
lines.append("### 表 2.4A：同批交错 `8a9c60fd`（Phase 5B 旧 HIP）vs Round 4 最新生产 HIP 三档 3 轮 4-slot ABBA `pure.exe` 纯 NN 测速（`final-pure-abba-statistics.json`）")
lines.append("")
lines.append("| 模型档位 | 同批旧基线 `8a9c60fd` HIP NN (ms) | **同批 Round 4 新生产 HIP NN (ms)** | **同批净 $\\Delta$mean (µs)** | **同批 $\\Delta$mean (%)** | 旧基线 p99 (ms) | 新生产 p99 (ms) | **同批 $\\Delta$p99 (µs)** | 每轮 $\\Delta$mean (µs) (R1 / R2 / R3) | mean / p99 赢轮 |")
lines.append("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- | :---: |")
for k in ["original1088", "small1080", "small900"]:
    x = pure_abba[k]
    r_str = " / ".join(f"{r['delta_us']:+.3f}" for r in x["rounds"])
    mw = sum(r["delta_ms"] < 0 for r in x["rounds"])
    pw = sum(r["p99_delta_us"] <= 0 for r in x["rounds"])
    lines.append(f"| **`{k}`** | {x['baseline_ms']:.6f} | **{x['candidate_ms']:.6f}** | **{x['delta_us']:+.3f}** | **{x['delta_percent']:+.2f}%** | {x['baseline_p99']:.6f} | **{x['candidate_p99']:.6f}** | **{x['p99_delta_us']:+.3f}** | `{r_str}` | **{mw}/3**、**{pw}/3** |")

lines.append("")
lines.append("### 表 2.4B：Round 4 最新生产 HIP vs 新版 `v0.0.8 ACO` 三档 3 轮 NMMN 纯 NN 对账（`final-nmmn-statistics.json`）")
lines.append("")
lines.append("| 模型档位 | **Round 4 生产 HIP NN (ms)** | **同批 `v0.0.8 ACO` NN (ms)** | **同批纯 NN 差值 (ms)** | **HIP 较 ACO 慢 (%)** | 每轮 HIP / ACO (ms) (R1 / R2 / R3) |")
lines.append("| :--- | ---: | ---: | ---: | ---: | :--- |")
for k in ["original1088", "small1080", "small900"]:
    x = nmmn[k]
    r_str = ", ".join(f"`{r['hip_ms']:.4f}/{r['aco_ms']:.4f}`" for r in x["rounds"])
    lines.append(f"| **`{k}`** | **{x['hip_ms']:.6f}** | **{x['aco_ms']:.6f}** | **+{x['gap_ms']:.6f}** | **+{x['hip_slower_percent']:.2f}%** | {r_str} |")

lines.append("")
lines.append("### 表 3.0：敏感度探针总表（含 `1005424b` 刷新后 C32 Softmax 窄 `2×2` 探针、`b1af6240` 窄探针与 `e7b234e7` 粗探针，已统一按《355 期》修正交互项符号 `I = (Base - P0) + (Base - P1) - (Base - P2)`，回应审单 P2-1 & P2-5）")
lines.append("")
lines.append("| 探针批次与基线 | 探针家族 (`P0` / `P1` 定义) | 档位 | `Base` (ms) | `P0` (ms) | `P1` (ms) | `P2` (ms) | **`Base - P0` (µs)** | **`Base - P1` (µs)** | **`Base - P2` (µs)** | 交互项 `Interaction` (µs) |")
lines.append("| :--- | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
for prof in ["original1088", "small1080", "small900"]:
    x = probes_r4[prof]
    a = x["arms"]
    lines.append(f"| **Round 4 (`1005424b` 含 R3-A 基线)** | **C32 Softmax 窄拆分** (`P0`=剩余指数位移+概率pack8旁路, `P1`=4×ones-WMMA+倒数旁路) | `{prof}` | {a['Base']['gpu_mean_ms']:.6f} | {a['P0']['gpu_mean_ms']:.6f} | {a['P1']['gpu_mean_ms']:.6f} | {a['P2']['gpu_mean_ms']:.6f} | **{x['Base_minus_P0_us']:+.2f}** | **{x['Base_minus_P1_us']:+.2f}** | **{x['Base_minus_P2_us']:+.2f}** | **{x['Interaction_us']:+.3f}** |")
for prof in ["original1088", "small1080", "small900"]:
    x = probes_r3[prof]
    a = x["arms"]
    lines.append(f"| Round 3 (`b1af6240` 未含 R3-A 基线) | C32 Softmax 窄拆分 (`P0`=指数映射/clamp+概率pack8旁路, `P1`=4×ones-WMMA+倒数旁路) | `{prof}` | {a['Base']['gpu_mean_ms']:.6f} | {a['P0']['gpu_mean_ms']:.6f} | {a['P1']['gpu_mean_ms']:.6f} | {a['P2']['gpu_mean_ms']:.6f} | **{x['Base_minus_P0_us']:+.2f}** | **{x['Base_minus_P1_us']:+.2f}** | **{x['Base_minus_P2_us']:+.2f}** | **{x['Interaction_us']:+.3f}** |")
for fam, label in [("c32", "C32 粗探针 (`P0`=噪声旁路, `P1`=整段Softmax辅助/分母旁路)"), ("qkv", "ViT QKV 探针 (`P0`=W5 LDS转发旁路, `P1`=尾部归约旁路)")]:
    for prof in ["original1088", "small1080", "small900"]:
        x = probes_r2[fam][prof]
        a = x["arms"]
        s = x["sensitivity_us"]
        lines.append(f"| Round 2 (`e7b234e7` 基线) | {label} | `{prof}` | {a['Base']['gpu_ms']:.6f} | {a['P0']['gpu_ms']:.6f} | {a['P1']['gpu_ms']:.6f} | {a['P2']['gpu_ms']:.6f} | **{s['Base_minus_P0_us']:+.2f}** | **{s['Base_minus_P1_us']:+.2f}** | **{s['Base_minus_P2_us']:+.2f}** | {s['interaction_us']:+.3f} |")

lines.append("")
lines.append("### 表 3.2：Round 4 正式生产载荷 100MHz 硬件实时钟全网 152/82-dispatch 分解与 `v0.0.8 ACO` 对账表（`final-stage-ledger.json`）")
lines.append("")
lines.append("| 阶段家族 | `Orig1088` 旧 `8a9c` RTC (µs) | `Orig1088` **R4 生产 RTC (µs)** | `Orig1088` **累计 RTC $\\Delta$ (µs)** | `Orig1088` ACO Step2 (µs) | `Orig1088` 阶段差 `R4 - ACO` (µs) | `Small1080` 旧 `8a9c` RTC (µs) | `Small1080` **R4 生产 RTC (µs)** | `Small1080` **累计 RTC $\\Delta$ (µs)** | `Small1080` ACO Step2 (µs) | `Small1080` 阶段差 `R4 - ACO` (µs) |")
lines.append("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")

fam_labels = [
    ("1_c32_family", "**1. C32 家族** (`prefix`+`chain`+`finish_pool`+`up`+`mapped`+`finish`+`post`)"),
    ("2_c256_family", "**2. C256 家族** (`sp_init`+`sp_run256`+`sp_recover256`+`c256_wave2`+`up48`+`pool256`)"),
    ("3_c64_c128_family", "**3. C64 + C128 家族** (`c64/c128_wave2`+`up56/62`+`pool64/128`)"),
    ("4_c512_family", "**4. C512 家族** (`split_ffn_one`+`split_proj_nof32`+`qkv_attn_compact`+`attn_proj_fb8`+`pool512`)"),
    ("5_vit_family", "**5. ViT 家族** (`expand`+`contract`+`qkv`+`attn`+`project_n64`+`gather_b8`)"),
]
lo = ledger["original1088"]
ls = ledger["small1080"]
for fkey, flabel in fam_labels:
    fo = lo["families"][fkey]
    fs = ls["families"][fkey]
    lines.append(f"| {flabel} | {fo['hip_8a9c60fd_rtc_us']:.2f} | **{fo['hip_round4_rtc_us']:.2f}** | **{fo['hip_rtc_delta_us']:+.2f}** | {fo['aco_step2_query_us']:.2f} | {fo['stage_diff_round4_minus_aco_us']:+.2f} | {fs['hip_8a9c60fd_rtc_us']:.2f} | **{fs['hip_round4_rtc_us']:.2f}** | **{fs['hip_rtc_delta_us']:+.2f}** | {fs['aco_step2_query_us']:.2f} | {fs['stage_diff_round4_minus_aco_us']:+.2f} |")

so = lo["sum_of_stages_us"]
ss = ls["sum_of_stages_us"]
lines.append(f"| **阶段重构总和 (`SUM_OF_STAGES`)** | {so['hip_8a9c60fd_rtc_sum_us']:.2f} | **{so['hip_round4_rtc_sum_us']:.2f}** | **{so['hip_rtc_sum_delta_us']:+.2f}** | {so['aco_step2_sum_us']:.2f} | **{so['stage_sum_diff_us']:+.2f}** | {ss['hip_8a9c60fd_rtc_sum_us']:.2f} | **{ss['hip_round4_rtc_sum_us']:.2f}** | **{ss['hip_rtc_sum_delta_us']:+.2f}** | {ss['aco_step2_sum_us']:.2f} | **{ss['stage_sum_diff_us']:+.2f}** |")

wo = lo["whole_network_nmmn_event_us"]
ws = ls["whole_network_nmmn_event_us"]
lines.append(f"| **全网 NMMN 整帧计时 (`WHOLE_NET_NMMN`)** | — | **{wo['hip_round4_nmmn_us']:.2f}** | — | {wo['aco_round4_nmmn_us']:.2f} | **{wo['nmmn_gap_us']:+.2f}** | — | **{ws['hip_round4_nmmn_us']:.2f}** | — | {ws['aco_round4_nmmn_us']:.2f} | **{ws['nmmn_gap_us']:+.2f}** |")

ro = lo["unexplained_remainder_and_clock_domain_diff_us"]
rs = ls["unexplained_remainder_and_clock_domain_diff_us"]
lines.append(f"| **未解释余数 / 计时域差异 (`NMMN - SUM_OF_STAGES`)** | — | **{ro['hip_nmmn_minus_rtc_sum_us']:+.2f}** | — | {ro['aco_nmmn_minus_step2_sum_us']:+.2f} | **{ro['total_gap_minus_stage_sum_diff_us']:+.2f}** | — | **{rs['hip_nmmn_minus_rtc_sum_us']:+.2f}** | — | {rs['aco_nmmn_minus_step2_sum_us']:+.2f} | **{rs['total_gap_minus_stage_sum_diff_us']:+.2f}** |")

(p / "GENERATED-TABLES-ROUND4.md").write_text("\n".join(lines) + "\n")
print("Wrote GENERATED-TABLES-ROUND4.md")
