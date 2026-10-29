from pathlib import Path
import json,shutil
p=Path(__file__).parent;a=json.loads((p/'4-statistics.json').read_text())['tiers'];k=json.loads((p/'4-c32-statistics.json').read_text())['profiles'];win=all(x['delta_ms']<0 and x['p99_delta_ms']<0 and all(z['delta_ms']<0 and z['p99_delta_ms']<0 for z in x['rounds'])for x in a.values())and all(x['delta_stage_ms']<0 and all(z['delta_ms']<0 for z in x['rounds'])for x in k.values());rows=[]
for n,x in a.items():rows.append(f"|{n}|{x['baseline_ms']:.6f}|{x['candidate_ms']:.6f}|{x['delta_ms']*1000:+.3f}|{x['p99_delta_ms']*1000:+.3f}|")
s='''# 刀4：Post寄存器RGB尾部

保持c0..31同序RGB累加，PostTap/非空logit保留旧路径。首版保留相邻两qt达到177VGPR；单qt-f32交换161VGPR，两者资源门STOP未上GPU。最终单qt-h2成对交换：145VGPR（原131）、4096B LDS、private/spill=0，全部active W16导出≤160。8条静态ds_bpermute交换对侧半wave的半精度通道，避免先F32展开再交换的额外寄存器。每qt由g0写16像素，g1参与全EXEC交换。

Post_b8静态指令1510→1488；ds_store_b128 4→2、ds_load_b128 6→2、ds_bpermute 0→8。计数是静态编码，不能忽略loop次数和active lane：旧RGB尾部32lane×2轮，新RGB发生在4轮qt且仅半wave写出，因此减少LDS不保证总吞吐增加。资源≤160是本轮资格线，不从VGPR或LDS容量直接编造真实active occupancy。

七档42帧Gold逐位全同、invalid=0。两档硬件实时钟+四档3×ABBA（每slot独立进程240帧弃80，960稳态样本/臂）如下：

|档位|基线ms|候选ms|mean Δµs|p99 Δµs|
|---|---:|---:|---:|---:|
'''+ '\n'.join(rows)+'\n\n'
for n,x in k.items():
 post={name:d for name,d in x['kernels'].items()if'post_b8_w16'in name};s+=f"{n} C32阶段Δ {x['delta_stage_ms']*1000:+.3f}µs；Post单核：`{json.dumps(post,ensure_ascii=False)}`。\n\n"
s+='结论：'+('ACCEPT，拷入生产并由最终80模块双架构重编收口。'if win else'STOP，恢复刀2基线，不用减少22条静态指令代替实测收益，不追加档位救援或刷轮。')+'\n';(p/'knife4-README.md').write_text(s);(p/'4-decision.json').write_text(json.dumps({'accepted':win,'gate':'All four frame mean/p99 and all rounds improve, C32 device-clock stage improves each round','profiles':a},indent=2))
if win:
 for n in ['wave_owned_c32.inc','build-modules.ps1']:shutil.copyfile(p/'work/4-source'/n,Path('hip')/n)
else:
 with Path('Development/failed-experiments.md').open('a')as f:f.write('\n\n## 2026-10-29 C32 Phase5 刀4 STOP\n\nPost寄存器尾部h2成对交换145VGPR/0spill/七档Gold同，静态1510→1488，尾部LDS改为8条ds_bpermute；两档单核与四档3ABBA无稳定全网净收益，STOP保刀2。原两qt177/单qtF32 161VGPR版本资源STOP未上GPU。详results/c32-stage-phase5-20261029/knife4-README.md。\n')
print(s)
