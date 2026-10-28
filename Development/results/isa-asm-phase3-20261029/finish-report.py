from pathlib import Path
import json
p=Path(__file__).parent;r=p.parents[2];stats=json.loads((p/'final-statistics.json').read_text());gap=json.loads((p/'gap-statistics.json').read_text());build=json.loads((p/'full-rebuild-verification.json').read_text());text='\n\n### 最终七档稳态 Frame（ms）\n\n| 档位 | 重建版 mean | p99 | 对照 mean | 差 µs |\n|---|---:|---:|---:|---:|\n'
for k in ['small1080','small900','small720','smallwindow','original1152','original1088','original960']:
 v=stats['tiers'][k];assert v['samples_per_arm']==960;text+=f"| {k} | {v['candidate_ms']:.6f} | {v['candidate_p99']:.6f} | {v['baseline_ms']:.6f} | {v['delta_ms']*1000:+.3f} |\n"
text+='\n代码与资源相同的重建自对照，差值用于观察测量漂移，不作为优化收益。84 个独立性能进程、每档/arm 960 个稳态帧；14 个 Gold 进程共42帧。Gold 硬哈希与每 slot 首尾哈希均由 runner 核验。\n'
text+=f"\n### 1088 纯 NN 同批 NMMN\n\nHIP **{gap['hip_ms']:.6f} ms**，锁版 Mochizuki d1185d2 **{gap['mochi_ms']:.6f} ms**；差 **{gap['gap_ms']:.6f} ms（{gap['relative_percent']:+.2f}%）**。\n\n| 轮 | HIP ms | Mochizuki ms | 差 ms |\n|---|---:|---:|---:|\n"
for v in gap['rounds']:text+=f"| {v['round']} | {v['hip_ms']:.6f} | {v['mochi_ms']:.6f} | {v['gap_ms']:+.6f} |\n"
text+='\n三轮 NMMN、每进程80 warm+160 measured、每 arm960帧，Original1x/1920×1088/有效1080/640token/71块/style1/history关。HIP 用 GPU event，Mochizuki 用其 GPU total/160；M 没有逐帧CSV，不能提供其p99。两端使用各自锁定、半精度一致输入夹具与输出硬哈希；跨实现不要求原始输出逐位一致。宿主/提交、噪声生成与执行图边界差异仍存在，此账本是该既定纯NN测试边界下的差值。\n\n旧 gap-phase3 的 0.478631198 ms 仅保留作历史账本；跨批环境和生产版本不同，不能把新旧差值归因到本轮三刀。当前只测得延迟差，不据静态WMMA指令数、VGPR或全网平均吞吐反推峰值算力缺口。下一步应对实际热点做有计时归因的独立实验，尤其检验内存流水与驻留的取舍；保持权重复用、减少指令并不自动更快。\n'
text+=f"\n### 重建与交付身份\n\n全部80模块已实际重编（12个LLVM23、68个COMGR，外加gfx1200 C512配方纠正一次），80个ELF架构均核验；全部函数代码及资源与锁定生产相同。容器SHA完全相同的有{sum(x['matches_production']for x in build['modules'])}个，其余仅容器身份变化，已刷新hip/SHA256SUMS。gfx1200 c512-m32-deep实际代码对应max-ilp，gfx1201对应空选项；初次空选项gfx1200版本隔离未上GPU，纠正及函数比对见full-code-comparison.json。没有新增优化内核。\n\n正式O2 addon/runtime/frame及pure宿主已构建，SHA见final-build/hosts.sha256；运行时烟测见smoke/evidence。完整交付位于本目录payload（不进Git），分析脚本、关键ISA计数和统计进Git，大体积反汇编和编译中间文件清理。原始CSV/log/命令证据位于各case/evidence（不进Git），evidence-index.json可核验身份。锁版Mochizuki文件树及native模型资产前后完整哈希核对，未改玩家游戏文件和0.45 ZIP。\n"
with(p/'README.md').open('a')as f:f.write(text)
entry=f"\n\n### 2026-10-29 ISA Phase3 四刀闭环\n\n1f780a34（成对打包/预取）、cb5a59d2（保持B复用折叠/预取）、98ba6985（serial_norm缓存）均按四档3ABBA或资源门STOP，生产数学和核代码未采用新改动。全部80双架构模块实际重编、ELF/函数代码/资源复核，更新容器SHA；正式addon/runtime/frame O2构建、七档Gold0bit-diff/invalid0、七档3ABBA及runtime烟测完成。最新1088纯NN三轮NMMN：HIP {gap['hip_ms']:.6f}ms、锁版Mochizuki d1185d2 {gap['mochi_ms']:.6f}ms，差{gap['gap_ms']:.6f}ms（{gap['relative_percent']:+.2f}%）；旧0.478631198ms为跨批历史，不能归因本轮。完整逐轮/负结果/配方身份见results/isa-asm-phase3-20261029/README.md。临时大文件清理、保留隔离payload，不部署、不改ZIP、不push。\n"
for n in ['WorkingPlan.md','DevHistory.md']:
 with(r/'Development'/n).open('a')as f:f.write(entry)
