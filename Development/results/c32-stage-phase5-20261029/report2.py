from pathlib import Path
import json
p=Path(__file__).parent;a=json.loads((p/'2-statistics.json').read_text())['tiers'];k=json.loads((p/'2-c32-statistics.json').read_text())['profiles'];bits={'small1080':1,'small900':2,'small720':4,'smallwindow':8,'original1088':16};mask=0;rows=[];decisions={}
for n,b in bits.items():
 x=a[n];y=k[n];win=x['delta_ms']<0 and x['p99_delta_ms']<0 and all(z['delta_ms']<0 and z['p99_delta_ms']<0 for z in x['rounds'])and y['delta_stage_ms']<0 and all(z['delta_ms']<0 for z in y['rounds']);mask|=b if win else 0;decisions[n]={'accept':win,'bit':b,'frame_mean_delta_us':x['delta_ms']*1000,'frame_p99_delta_us':x['p99_delta_ms']*1000,'c32_stage_delta_us':y['delta_stage_ms']*1000};rows.append(f"|{n}|{x['baseline_ms']:.6f}|{x['candidate_ms']:.6f}|{x['delta_ms']*1000:+.3f}|{x['p99_delta_ms']*1000:+.3f}|{y['delta_stage_ms']*1000:+.3f}|{'ACCEPT'if win else'STOP'}|")
(p/'2-decision.json').write_text(json.dumps({'accepted_mask':mask,'profiles':decisions,'gate':'All three paired round mean and p99 improve; aggregate mean/p99 improve; device-clock C32 stage improves each round. No rerun for outcome selection.'},indent=2))
s='''# 刀2：Norm-hoist 五档扩围

七档42帧Gold，invalid=0，首尾输出SHA逐位全同。已有生产norm900机器码复用，无核重编：prefix/post的静态rsq均16→2，总指令1498→1457/1510→1469。只采用下表通过全部三轮mean/p99以及C32实时钟门禁的几何；原Original900路由保留。

|档位|基线ms|候选ms|mean Δµs|p99 Δµs|C32 Δµs|结论|
|---|---:|---:|---:|---:|---:|---|
'''+ '\n'.join(rows)+f'\n\n生产扩围mask={mask}。`NR_C32_NORM_EXPAND_MASK=0`显式回退所有新增范围。每档Frame12独立slot、240帧弃80、960稳态样本/臂；硬件时钟按完整dispatch序列轮转，各dispatch每slot两观测，单位100MHz、邻接stamp校正80ticks。\n\n小窗实际91dispatch，首次83断言失败日志保留；更新为91后重新完整计时。一次驱动短HIP event异常导致整轮重跑；诊断判据始终是单独的硬件实时钟，未用事件异常值计算单核时间。新版诊断host只取消其非计时用途的事件有效性断言，正常纯NN宿主仍严格检查事件。首次/重跑原始日志均入证据索引。静态rsq减少不直接等于显存事务或预测性能。\n'
(p/'knife2-README.md').write_text(s)
if mask:
 f=Path('Development/HIP/hip_reference_network.h');s=(p/'work/2-host/Development/HIP/hip_reference_network.h').read_text();s=s.replace('v?unsigned(std::strtoul(v,nullptr,0)):0u','v?unsigned(std::strtoul(v,nullptr,0)):'+str(mask)+'u');f.write_text(s)
with Path('Development/failed-experiments.md').open('a')as f:
 f.write('\n\n## 2026-10-29 C32 Phase5 刀2 未通过扩围档位\n\n'+', '.join(n for n,d in decisions.items()if not d['accept'])+' 未满足三轮mean/p99及单核稳定改善，保持旧路由；实际数值与每轮反例见results/c32-stage-phase5-20261029/knife2-README.md、2-statistics.json。只收mask='+str(mask)+'，无性能刷轮。\n')
print((p/"knife2-README.md").read_text())
