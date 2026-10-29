from pathlib import Path
import json
p=Path(__file__).parent;a=json.loads((p/'3-statistics.json').read_text())['tiers'];k=json.loads((p/'3-c32-statistics.json').read_text())['profiles'];checks={}
for n,x in a.items():checks[n]={'mean_improves':x['delta_ms']<0,'mean_rounds_improve':sum(r['delta_ms']<0 for r in x['rounds']),'p99_support':x['p99_delta_ms']<=0 or sum(r['p99_delta_ms']<=0 for r in x['rounds'])>=2}
clock={n:{'delta_ms':x['delta_stage_ms'],'rounds_improve':sum(r['delta_ms']<0 for r in x['rounds'])}for n,x in k.items()};win=all(x['mean_improves']and x['p99_support']for x in checks.values())and all(x['delta_ms']<0 and x['rounds_improve']>=2 for x in clock.values());d={'accepted':win,'gate':'User-calibrated: pooled mean improves, with hardware stage confirming the direction; round mean counts are disclosed, not an extra veto; pooled p99 nonregressing OR majority p99 rounds support; hardware C32+pool stage confirms improvement. Do not label isolated tails as proven system interrupts without counters.','frame':checks,'device_clock':clock};(p/'3-decision.json').write_text(json.dumps(d,indent=2))
s='''# Step1：修复C32 finish→pool融合

起点4cb74f52。显式`cvt_pkrtz`恢复独立MH模块的真正FP16 RTZ边界，不再误用C32 fast内的恒等Hrtz；最终FP8写前按任务要求加入+0.f。组合修复一次通过七档42帧Gold，invalid=0、首尾SHA与原基线逐位同。此实验验证这组修复，不声称分别证明每一项单独必要，也不把源文件对负下溢零的注释当作硬件转换的完整证明。24种几何/shift组合覆盖/2×2采样坐标检查通过，实际独立pool调用valid_width/height为0，裁剪后有效像素恰好各写一次。main仍保留为decoder skip。

新核134VGPR、4096B LDS、private/spill=0。新增8条显式尾部RTZ（e64），通过mix指令精确展开到F32；3-rtz-excerpt.txt保留实际汇编。原先e32 RTZ属于其他阶段，不能混数。生产W16导出均≤160，基线少数非默认旧回退导出原已>160（最大177），逐个保留并核验未退化，不虚报全部导出都≤160。

|档位|基线ms|融合ms|mean Δµs|p99 Δµs|支持mean轮数|支持p99轮数|
|---|---:|---:|---:|---:|---:|---:|
'''
for n,x in a.items():s+=f"|{n}|{x['baseline_ms']:.6f}|{x['candidate_ms']:.6f}|{x['delta_ms']*1000:+.3f}|{x['p99_delta_ms']*1000:+.3f}|{sum(r['delta_ms']<0 for r in x['rounds'])}|{sum(r['p99_delta_ms']<=0 for r in x['rounds'])}|\n"
s+='\n硬件实时钟C32+pool阶段：\n'
for n,x in k.items():s+=f"- {n}: {x['baseline_stage_ms']:.6f}→{x['candidate_stage_ms']:.6f}ms（{x['delta_stage_ms']*1000:+.3f}µs）\n"
s+='\n结论：'+('ACCEPT，进入组合形态。'if win else'STOP，不把未获稳定净收益的融合混入组合基线；正确性修复与全量数据保留。')+'\n';(p/'step1-README.md').write_text(s);print(s)
