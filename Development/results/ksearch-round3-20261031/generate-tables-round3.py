#!/usr/bin/env python3
"""
Programmatically generate GENERATED-TABLES-ROUND3.md from raw JSON/CSV artifacts.
Zero manual copying.
"""
import json
from pathlib import Path

p = Path(__file__).parent
load = lambda f: json.loads(f.read_text(encoding='utf-8-sig'))

t1 = load(p / 'table1-isa-resources.json')
dyn_rcp = load(p / 'dynamic-rcp-ledger.json')
r1_stats = load(p.parent / 'ksearch-step4-20261031/final-statistics.json')['tiers']
r2_stats = load(p.parent / 'ksearch-round2-20261031/final-statistics.json')['tiers']
r3a_stats = load(p / 'r3a-statistics.json')['tiers']
r3_stats = load(p / 'final-statistics.json')['tiers']
s1080_4arm = load(p / 's1080_4arm-statistics.json')['summary']
pure_abba = load(p / 'final-pure-abba-statistics.json')['tiers']
nmmn = load(p / 'final-nmmn-statistics.json')['nmmn']
probes_r2 = load(p.parent / 'ksearch-round2-20261031/probes-2x2-statistics.json')['probes']
probes_sm = load(p / 'probes-sm-2x2-statistics.json')['summary']
ledger = load(p / 'final-stage-ledger.json')

lines = []

# Table 1A
lines.append("### 表 1A：修改核静态 ISA 指令净差、同符号 vs 实际路由基线与修正后 RDNA4 (`4 SIMD32/WGP`, `2 SIMD32/CU`) 硬件驻留表（回应审单 P1-1 & P2-5，`table1-isa-resources.json`）")
lines.append("")
lines.append("> **RDNA4 硬件拓扑修正声明**：严格按 AMD RDNA4 ISA §2.3（`4 SIMD32/WGP` 共享 `128 KiB LDS`、`2 SIMD32/CU` 共享 `64 KiB LDS`；WGP 多 wave 工作组 barrier 槽位上限 `32 WGs/WGP`，单 wave 工作组 `1w/WG` 不受 16/32 组 barrier 槽位限制）计算 `LDS 上限 (Waves/SIMD)` 与 `有效驻留 min(VGPR_waves, LDS_waves)`；并将 `v_rcp_f32_e32` 与 `v_s_rcp_f32` 显式分列，新增 R3-A (`CW_FMED3_CLAMP=1`) 的 SGPR 与 `v_cmp_gt_f32_e64 + v_cndmask_b32_e64` 消除列。")
lines.append("")
lines.append("| 所属模块 (`.hsaco`) | 核函数符号 | 编译器 | 同符号 VGPR (`8a9c` $\\to$ `e7b2` $\\to$ `b1af` $\\to$ **R3**) | 实际路由 VGPR (`8a9c` $\\to$ **R3**) | SGPR (`b1af` $\\to$ **R3**) | LDS (B) & 模式 | VGPR 上限 (Waves/SIMD) | LDS/WG 上限 (Waves/SIMD) | **有效驻留 $\\min$ (Waves/SIMD)** | 同符号指令 (`8a9c` $\\to$ `e7b2` $\\to$ `b1af` $\\to$ **R3**) | 实际路由指令 (`8a9c` $\\to$ **R3** $\\Delta$) | `v_rcp_f32_e32` (`e7b2` $\\to$ **R2/R3**) | `v_s_rcp_f32` (`e7b2` $\\to$ **R2/R3**) | `v_cmp_gt_e64` + `v_cndmask_e64` (`b1af` $\\to$ **R3**) |")
lines.append("| :--- | :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |")

for r in t1:
    mod_short = r["module"].removesuffix(".hsaco")
    k = r["kernel"]
    old_k = r["route_old_kernel"]
    vgpr_sym = f"`{r['vgpr_same_sym_8a']} -> {r['vgpr_e7']} -> {r['vgpr_r2']} ->` **`{r['vgpr_r3']}`**"
    if old_k != k:
        short_old = old_k.replace("split_projection_frag_", "").replace("mh_attention_project_frag_c512_", "")
        vgpr_route = f"`{r['vgpr_route_old_8a']}` (`{short_old}`) $\\to$ **`{r['vgpr_r3']}`**"
    else:
        vgpr_route = f"`{r['vgpr_route_old_8a']} ->` **`{r['vgpr_r3']}`**"
    sgpr_str = f"`{r['sgpr_r2']} ->` **`{r['sgpr_r3']}`**" if r["sgpr_r2"] != r["sgpr_r3"] else f"`{r['sgpr_r3']}`"
    mode_str = f"{r['lds']:,} ({'WGP' if r['wgp_mode'] else 'CU'}, {r['waves_per_wg']}w/WG)"
    lds_lim = "不限 (0 LDS, 1w/WG)" if r["waves_simd_lds"] is None else f"{int(r['waves_simd_lds']) if r['waves_simd_lds'].is_integer() else r['waves_simd_lds']}"
    eff_lim = f"**{int(r['waves_simd_eff']) if float(r['waves_simd_eff']).is_integer() else r['waves_simd_eff']}**"
    insn_sym = f"`{r['insns_same_sym_8a']} -> {r['insns_e7']} -> {r['insns_r2']} ->` **`{r['insns_r3']}`**"
    delta_route = r["insns_r3"] - r["insns_route_old_8a"]
    insn_route = f"`{r['insns_route_old_8a']} ->` **`{r['insns_r3']} ({delta_route:+d})`**"
    v_rcp_str = f"`{r['v_rcp_f32_e32_e7']} ->` **`{r['v_rcp_f32_e32_r3']}`**" if r["v_rcp_f32_e32_e7"] != r["v_rcp_f32_e32_r3"] else f"`{r['v_rcp_f32_e32_r3']}`"
    vs_rcp_str = f"`{r['v_s_rcp_f32_r3']}`"
    cmp_cnd_str = f"`{r['v_cmp_gt_f32_e64_r2']}+{r['v_cndmask_b32_e64_r2']} ->` **`{r['v_cmp_gt_f32_e64_r3']}+{r['v_cndmask_b32_e64_r3']}`**" if (r["v_cmp_gt_f32_e64_r2"] or r["v_cndmask_b32_e64_r2"]) else "`0+0`"
    lines.append(f"| `{mod_short}` | `{k}` | {r['compiler']} | {vgpr_sym} | {vgpr_route} | {sgpr_str} | {mode_str} | {r['waves_simd_vgpr']} | {lds_lim} | {eff_lim} | {insn_sym} | {insn_route} | {v_rcp_str} | {vs_rcp_str} | {cmp_cnd_str} |")

lines.append("")
lines.append("### 表 1B：全网逐档位动态 Softmax `v_rcp_f32_e32`（R2-D）与 C32 `v_cmp_gt_f32_e64 + v_cndmask_b32_e64`（R3-A）指令/操作数精确推导汇总表（回应审单 P2-5，由 `compute-dynamic-rcp.py` 生成，`dynamic-rcp-ledger.json`）")
lines.append("")
lines.append("> **推导规则说明**：① `sp_recover256_w16` 在正常帧中仅作为 `grid=1` 的超时守卫启动并在入口立即退出，动态执行次数严格计为 `0`；② 每个活跃的 C32 核与 C64/C128/C256/SP 核内，`for(uint qt=0; qt<4; qt++)` 循环均设为 `unroll(disable)`，每条 wave 在每个 block 内动态执行 `qt = 4` 次 Softmax 迭代；③ 每次 `qt` 迭代中，R2-D 将 `v_rcp_f32_e32` 从 `8` 条减至 `1` 条（每 wave 每次 `qt` 省 `7` 条 wave 级指令 = `224` 次 lane 级超越运算）；R3-A 在 C32 核每次 `qt` 迭代中消除 `31` 条 `v_cmp_gt_f32_e64` + `31` 条 `v_cndmask_b32_e64`（共 `62` 条 wave 级 VALU 指令）。")
lines.append("")
lines.append("| 档位 | 处理分辨率 | 模型 | C32 动态 Waves/帧 | C64/128/256/SP 动态 Waves/帧 | 全网总动态 Waves/帧 | 全网总 `qt` 迭代/帧 (`Waves × 4`) | **R2-D 消除 Wave 级 `v_rcp_f32_e32` / 帧 (`×7`)** | **R2-D 消除 Lane 级倒数操作 / 帧 (`×224`)** | **R3-A 消除 C32 Wave 级 `cmp+cnd_e64` / 帧 (`×62`)** |")
lines.append("| :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
for prof in ["original1152", "original1088", "original960", "small1080", "small900", "small720", "smallwindow"]:
    d = dyn_rcp[prof]
    lines.append(f"| **`{prof}`** | `{d['processing_resolution']}` | {d['model']} | {d['c32_dynamic_waves']:,} | {d['mh_sp_dynamic_waves']:,} | {d['total_dynamic_waves']:,} | {d['total_dynamic_qt_iters']:,} | **{d['r2d_wave_v_rcp_f32_saved']:,}** | **{d['r2d_lane_rcp_ops_saved']:,}** | **{d['r3a_c32_wave_cmp_cnd_saved']:,}** |")

lines.append("")
lines.append("#### 附表 1B-Detail：`original1088` (`1920×1088`) 逐阶段 / 逐核动态 Wave 与 `v_rcp_f32_e32` 推导明细表")
lines.append("")
lines.append("| 阶段 | Block 范围 | 核函数符号 | 阶段宽高 (`w×h`) | Shift | Dispatches | 覆盖层数 | 总 WGs (`windows`) | Waves/WG | 动态 Waves | 动态 `qt` 迭代 (`×4`) | 静态 `v_rcp_f32_e32` (`前->后`) | 静态 `v_s_rcp_f32` | **消除 Wave 级 `v_rcp_f32_e32`** | **消除 Lane 级倒数 (`×32`)** |")
lines.append("| :--- | :---: | :--- | :---: | :---: | ---: | ---: | ---: | ---: | ---: | ---: | :---: | :---: | ---: | ---: |")
for r in dyn_rcp["original1088"]["schedule"]:
    lines.append(f"| {r['stage']} | `{r['blocks']}` | `{r['kernel']}` | `{r['w']}×{r['h']}` | `{r['shift']}` | {r['dispatches']} | {r['layers']} | {r['wgs']:,} | {r['waves_per_wg']} | {r['dynamic_waves']:,} | {r['dynamic_qt_iters']:,} | `{r['static_v_rcp_f32_e32_before']} -> {r['static_v_rcp_f32_e32_after']}` | `{r['static_v_s_rcp_f32']}` | **{r['wave_rcp_saved']:,}** | **{r['lane_rcp_saved']:,}** |")

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

fmt_abba_table("表 2.3A：Round 3 正式生产载荷 (`K6 + K2-Scope + K5 + R2-D + R3-A`) vs Phase 5B 基线 `8a9c60fd` 七档 3 轮 4-slot Frame ABBA 机器生成表（`ksearch-round3-20261031/final-statistics.json`）", r3_stats)
fmt_abba_table("表 2.3B：Round 3 单刀 `R3-A` (`CW_FMED3_CLAMP=1`) vs Round 2 基线 `b1af6240` 四档 3 轮 4-slot Frame ABBA 机器生成表（`ksearch-round3-20261031/r3a-statistics.json`）", r3a_stats, ["small1080", "original1088", "original960", "small900"])
fmt_abba_table("表 2.3C：Round 2 生产载荷 (`b1af6240`) vs `8a9c60fd` 七档 3 轮 4-slot Frame ABBA 历史对照表（`ksearch-round2-20261031/final-statistics.json`）", r2_stats)

lines.append("")
lines.append("### 表 2.3D：`small1080` 上固定 K5 的 R2-D 四臂同批交错隔离对照表（回应审单 P1-2，`s1080_4arm-statistics.json`）")
lines.append("")
lines.append("| 隔离实验臂 (均固定开启 K5) | `small1080` mean (ms) | 相对 `Base_K5` $\\Delta$mean (µs) | `small1080` pooled p99 (ms) | 相对 `Base_K5` $\\Delta$p99 (µs) | 每轮 $\\Delta$mean (µs) (R1 / R2 / R3) |")
lines.append("| :--- | ---: | ---: | ---: | ---: | :--- |")
for arm_key, arm_data in s1080_4arm.items():
    r_str = " / ".join(f"{r['delta_us']:+.3f}" for r in arm_data["rounds"])
    lines.append(f"| **`{arm_key}`** | {arm_data['mean_ms']:.6f} | **{arm_data['delta_vs_Base_K5_us']:+.3f}** | {arm_data['p99_ms']:.6f} | **{arm_data['p99_delta_vs_Base_K5_us']:+.3f}** | `{r_str}` |")

lines.append("")
lines.append("### 表 2.4A：同批交错 `8a9c60fd`（Phase 5B 旧 HIP）vs Round 3 最新生产 HIP 三档 3 轮 4-slot ABBA `pure.exe` 纯 NN 测速（`final-pure-abba-statistics.json`）")
lines.append("")
lines.append("| 模型档位 | 同批旧基线 `8a9c60fd` HIP NN (ms) | **同批 Round 3 新生产 HIP NN (ms)** | **同批净 $\\Delta$mean (µs)** | **同批 $\\Delta$mean (%)** | 旧基线 p99 (ms) | 新生产 p99 (ms) | **同批 $\\Delta$p99 (µs)** | 每轮 $\\Delta$mean (µs) (R1 / R2 / R3) | mean / p99 赢轮 |")
lines.append("| :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | :--- | :---: |")
for k in ["original1088", "small1080", "small900"]:
    x = pure_abba[k]
    r_str = " / ".join(f"{r['delta_us']:+.3f}" for r in x["rounds"])
    mw = sum(r["delta_ms"] < 0 for r in x["rounds"])
    pw = sum(r["p99_delta_us"] <= 0 for r in x["rounds"])
    lines.append(f"| **`{k}`** | {x['baseline_ms']:.6f} | **{x['candidate_ms']:.6f}** | **{x['delta_us']:+.3f}** | **{x['delta_percent']:+.2f}%** | {x['baseline_p99']:.6f} | **{x['candidate_p99']:.6f}** | **{x['p99_delta_us']:+.3f}** | `{r_str}` | **{mw}/3**、**{pw}/3** |")

lines.append("")
lines.append("### 表 2.4B：Round 3 最新生产 HIP vs 新版 `v0.0.8 ACO` 三档 3 轮 NMMN 纯 NN 对账（`final-nmmn-statistics.json`）")
lines.append("")
lines.append("| 模型档位 | **Round 3 生产 HIP NN (ms)** | **同批 `v0.0.8 ACO` NN (ms)** | **同批纯 NN 差值 (ms)** | **HIP 较 ACO 慢 (%)** | 每轮 HIP / ACO (ms) (R1 / R2 / R3) |")
lines.append("| :--- | ---: | ---: | ---: | ---: | :--- |")
for k in ["original1088", "small1080", "small900"]:
    x = nmmn[k]
    r_str = ", ".join(f"`{r['hip_ms']:.4f}/{r['aco_ms']:.4f}`" for r in x["rounds"])
    lines.append(f"| **`{k}`** | **{x['hip_ms']:.6f}** | **{x['aco_ms']:.6f}** | **+{x['gap_ms']:.6f}** | **+{x['hip_slower_percent']:.2f}%** | {r_str} |")

lines.append("")
lines.append("### 表 3.0：敏感度探针总表（含 `e7b234e7` 粗探针与 `b1af6240` 上 C32 Softmax 窄 `2×2` 子机制拆分探针，回应审单 P1-3，`probes-sm-2x2-statistics.json`）")
lines.append("")
lines.append("| 探针批次与基线 | 探针家族 (`P0` / `P1` 定义) | 档位 | `Base` (ms) | `P0` (ms) | `P1` (ms) | `P2` (ms) | **`Base - P0` (µs)** | **`Base - P1` (µs)** | **`Base - P2` (µs)** | 交互项 `Interaction` (µs) |")
lines.append("| :--- | :--- | :--- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |")
for prof in ["original1088", "small1080", "small900"]:
    x = probes_sm[prof]
    a = x["arms"]
    lines.append(f"| **Round 3 (`b1af6240` 新基线)** | **C32 Softmax 窄拆分** (`P0`=指数映射/clamp+概率pack8旁路, `P1`=4×ones-WMMA+倒数旁路) | `{prof}` | {a['Base']['gpu_mean_ms']:.6f} | {a['P0']['gpu_mean_ms']:.6f} | {a['P1']['gpu_mean_ms']:.6f} | {a['P2']['gpu_mean_ms']:.6f} | **{x['Base_minus_P0_us']:+.2f}** | **{x['Base_minus_P1_us']:+.2f}** | **{x['Base_minus_P2_us']:+.2f}** | {x['Interaction_us']:+.2f} |")
for fam, label in [("c32", "C32 粗探针 (`P0`=噪声旁路, `P1`=整段Softmax辅助/分母旁路)"), ("qkv", "ViT QKV 探针 (`P0`=W5 LDS转发旁路, `P1`=尾部归约旁路)")]:
    for prof in ["original1088", "small1080", "small900"]:
        x = probes_r2[fam][prof]
        a = x["arms"]
        s = x["sensitivity_us"]
        lines.append(f"| Round 2 (`e7b234e7` 基线) | {label} | `{prof}` | {a['Base']['gpu_ms']:.6f} | {a['P0']['gpu_ms']:.6f} | {a['P1']['gpu_ms']:.6f} | {a['P2']['gpu_ms']:.6f} | **{s['Base_minus_P0_us']:+.2f}** | **{s['Base_minus_P1_us']:+.2f}** | **{s['Base_minus_P2_us']:+.2f}** | {s['interaction_us']:+.2f} |")

lines.append("")
lines.append("### 表 3.2：Round 3 正式生产载荷 100MHz 硬件实时钟全网 152/82-dispatch 分解与 `v0.0.8 ACO` 对账表（`final-stage-ledger.json`）")
lines.append("")
lines.append("| 阶段家族 | `Orig1088` 旧 `8a9c` RTC (µs) | `Orig1088` **R3 生产 RTC (µs)** | `Orig1088` **本轮 RTC $\\Delta$ (µs)** | `Orig1088` ACO Step2 (µs) | `Orig1088` 阶段差 `R3 - ACO` (µs) | `Small1080` 旧 `8a9c` RTC (µs) | `Small1080` **R3 生产 RTC (µs)** | `Small1080` **本轮 RTC $\\Delta$ (µs)** | `Small1080` ACO Step2 (µs) | `Small1080` 阶段差 `R3 - ACO` (µs) |")
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
    lines.append(f"| {flabel} | {fo['hip_8a9c60fd_rtc_us']:.2f} | **{fo['hip_round3_rtc_us']:.2f}** | **{fo['hip_rtc_delta_us']:+.2f}** | {fo['aco_step2_query_us']:.2f} | {fo['stage_diff_round3_minus_aco_us']:+.2f} | {fs['hip_8a9c60fd_rtc_us']:.2f} | **{fs['hip_round3_rtc_us']:.2f}** | **{fs['hip_rtc_delta_us']:+.2f}** | {fs['aco_step2_query_us']:.2f} | {fs['stage_diff_round3_minus_aco_us']:+.2f} |")

so = lo["sum_of_stages_us"]
ss = ls["sum_of_stages_us"]
lines.append(f"| **阶段重构总和 (`SUM_OF_STAGES`)** | {so['hip_8a9c60fd_rtc_sum_us']:.2f} | **{so['hip_round3_rtc_sum_us']:.2f}** | **{so['hip_rtc_sum_delta_us']:+.2f}** | {so['aco_step2_sum_us']:.2f} | **{so['stage_sum_diff_us']:+.2f}** | {ss['hip_8a9c60fd_rtc_sum_us']:.2f} | **{ss['hip_round3_rtc_sum_us']:.2f}** | **{ss['hip_rtc_sum_delta_us']:+.2f}** | {ss['aco_step2_sum_us']:.2f} | **{ss['stage_sum_diff_us']:+.2f}** |")

wo = lo["whole_network_nmmn_event_us"]
ws = ls["whole_network_nmmn_event_us"]
lines.append(f"| **全网 NMMN 整帧计时 (`WHOLE_NET_NMMN`)** | — | **{wo['hip_round3_nmmn_us']:.2f}** | — | {wo['aco_round3_nmmn_us']:.2f} | **{wo['nmmn_gap_us']:+.2f}** | — | **{ws['hip_round3_nmmn_us']:.2f}** | — | {ws['aco_round3_nmmn_us']:.2f} | **{ws['nmmn_gap_us']:+.2f}** |")

ro = lo["unexplained_remainder_and_clock_domain_diff_us"]
rs = ls["unexplained_remainder_and_clock_domain_diff_us"]
lines.append(f"| **未解释余数 / 计时域差异 (`NMMN - SUM_OF_STAGES`)** | — | **{ro['hip_nmmn_minus_rtc_sum_us']:+.2f}** | — | {ro['aco_nmmn_minus_step2_sum_us']:+.2f} | **{ro['total_gap_minus_stage_sum_diff_us']:+.2f}** | — | **{rs['hip_nmmn_minus_rtc_sum_us']:+.2f}** | — | {rs['aco_nmmn_minus_step2_sum_us']:+.2f} | **{rs['total_gap_minus_stage_sum_diff_us']:+.2f}** |")

(p / "GENERATED-TABLES-ROUND3.md").write_text("\n".join(lines) + "\n")
print("Wrote GENERATED-TABLES-ROUND3.md")
