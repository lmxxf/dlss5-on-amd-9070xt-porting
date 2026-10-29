from pathlib import Path
import json
p=Path(__file__).parent;r=p.parents[2];decisions={};text='''# Step 3：C128 深预取与最小 ISA 手术

两个候选分别对照同一生产起点，未互相叠加。均只改变c64-wave2-fast模块中的c128_wave2_bi_bo；所有其余136个导出代码不变。两者各七档Gold42帧、invalid=0、0 bit-diff；各完成两档单核实时钟ABBA及四档Frame ABBA。

3A：保持B4/T2、全部权重复用及kt/ht/tile/ci/qi数学顺序，把四个Contract i2权重片段从激活后提前至Expand前。实际ASM Contract首次读取从函数约568行提前到270行。242→248VGPR，26SGPR/16KiB LDS/private0/spill0；实际.kd资源寄存器完全相同，分配桶没有变化，因此不声称做出了高/低驻留因果对照。原本HIP_KERNEL_WPE=0，没有显式waves限制可移除。源码与正式LLVM23配方、ISA摘要和资源编码见3a-*.json/3a-source.patch。

3B：在完整canonical .s中把global_load_b64 v[213:214],v67,s[10:11] offset:256提前跨v_add3/v_or3两条独立地址VALU；原hard clause计数14→13。无重命名、无新增VGPR/LDS、0spill。CFG3289节点，may-pending-write含所有直接分支与回边，固定点3轮；新活跃区间无目的/地址寄存器读写冲突，任一前驱未完成global/LDS/scalar写不冲突，无EXEC/分支/屏障跨越。全部内存issue顺序、wait计数以及s_delay_alu指向的指令ID保持。原位置下一条VMEM保留隐式VA_VDST完成边界（LLVM23 InsertDelayAlu::instructionWaitsForVALU、FLAT指令flag为依据），不靠未证实的ALU延迟间隔猜安全。参见3b-safety-proof.json、3b-asm.patch、prepare3b.py。

重组装真实使用 clang -cc1as -target-feature -real-true16 + ld.lld。完整重新链接的wrapper有46个非目标函数字节变化，未上GPU、不冒称它们已归因。先核目标函数roundtrip逐字节同生产、大小同，再把该函数候选字节注入未改的原ELF：所有目标函数外字节（含metadata/描述符）保持，136其他函数逐位同。这是实际送测3B模块，3b-isolated-module.json/3b-resource-comparison.json记录身份。

## 单核实时钟结果

使用Step2有效设备实时钟方法，Original/Small每臂分别120/24个目标核观测（按原图调用位置等权），三轮四slot ABBA；邻接stamp开销同法校正。以下为每次调用差值，负值更快。

| 候选 | 模型 | baseline µs/次 | candidate µs/次 | 差 µs/次 | 各轮核族总差 µs/帧 |
|---|---|---:|---:|---:|---|
'''
for role in ['3a','3b']:
 k=json.loads((p/(role+'-kernel-statistics.json')).read_text());net=json.loads((p/(role+'-statistics.json')).read_text());kp=k['profiles'];kernel_ok=all(v['delta_per_call_ms']<0 and all(z['delta_family_ms']<0 for z in v['rounds'])for v in kp.values());net_ok=all(v['delta_ms']<0 and v['p99_delta_ms']<0 and all(z['delta_ms']<0 and z['p99_delta_ms']<0 for z in v['rounds'])for v in net['tiers'].values());decisions[role]={'decision':'ACCEPT'if kernel_ok and net_ok else'STOP','kernel_stable':kernel_ok,'network_stable':net_ok,'gold_profiles':7,'gold_frames':42,'production_modified':False}
 for name,v in kp.items():text+=f"| {role} | {name} | {v['baseline_per_call_ms']*1000:.6f} | {v['candidate_per_call_ms']*1000:.6f} | {v['delta_per_call_ms']*1000:+.6f} | "+', '.join(f"{z['delta_family_ms']*1000:+.3f}"for z in v['rounds'])+' |\n'
 (p/(role+'-decision.json')).write_text(json.dumps(decisions[role],indent=2))
text+='\n## 整网 Frame 稳态结果\n\n每档3轮4-slot ABBA，240帧弃80、每臂960稳态帧，NET0/edges_only1。每slot独立进程，严格串行；表中µs是candidate−baseline。\n\n| 候选 | 档位 | mean差 µs | p99差 µs | 各轮mean差 µs | 各轮p99差 µs |\n|---|---|---:|---:|---|---|\n'
for role in ['3a','3b']:
 m=json.loads((p/(role+'-statistics.json')).read_text())
 for name,v in m['tiers'].items():text+=f"| {role} | {name} | {v['delta_ms']*1000:+.3f} | {v['p99_delta_ms']*1000:+.3f} | "+', '.join(f"{z['delta_ms']*1000:+.3f}"for z in v['rounds'])+' | '+', '.join(f"{z['p99_delta_ms']*1000:+.3f}"for z in v['rounds'])+' |\n'
text+='''
## 裁定

3A：Original单核每次−0.521µs（约−0.636%），三轮方向一致，说明源码预取并非完全无效。但Small单核不稳定，Original1088整网mean+2.481µs/p99+15.682µs；其他档位有尾部反例。STOP，不把单核微正兑换成不存在的稳定产品收益，不另加几何路由刷轮。

3B：Original/Small单核均值分别+0.085/+0.328µs，方向不稳定。Original900整网三轮均值全退，pooled+4.544µs/p99+15.587µs；Small1080尾部+22.862µs。STOP。Original1088 pooled约零，不选第三轮的好值覆盖前两轮反例。

没有获胜改动，不触发双架构80模块替换。生产代码及hip/SHA256SUMS保持起点，候选模块均隔离，清理临时大文件后独立提交。该结论只否决这两个具体原型，不证明源码优化或汇编后处理已到上限。
'''
assert all(x['decision']=='STOP'for x in decisions.values()),'winner requires full production integration before finalizing';(p/'step3-README.md').write_text(text);(p/'step3-decisions.json').write_text(json.dumps(decisions,indent=2))
entry='''\n\n## 2026-10-29 ACO hotspot Phase4 Step3：C128深预取/最小ISA移动均STOP

新版ACO同机基线差约46%，有效设备实时钟确定Original C128 bi_bo约0.819ms/帧为一级热点。3A保持B4全复用，Contract前读至Expand前，仅目标函数改、242→248VGPR/同硬件分配桶/0spill；七档Gold同，Original单核三轮微正−0.521µs/次，但整网1088 mean+2.481µs/p99+15.682µs，Small尾部不稳，STOP。3B严格CFG/等待/EXEC/异步写检查后只把一条FW b64提前跨两条地址VALU，目标外全部字节不变、七档Gold同；单核不稳，Original900三轮mean全退，+4.544µs/p99+15.587µs，STOP。分别完成两档单核与四档3ABBA，不叠加、不刷轮、不改生产80/SHA。详results/aco-hotspot-phase4-20261029/step3-README.md。\n'''
with(r/'Development/failed-experiments.md').open('a')as f:f.write(entry)
