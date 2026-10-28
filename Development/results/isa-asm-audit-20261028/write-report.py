from pathlib import Path
import json,hashlib,subprocess,collections
P=Path(__file__).parent;R=P.parents[2];a=json.loads((P/'anatomy.json').read_text());live=json.loads((P/'liveness-summary.json').read_text());st=json.loads((P/'c32-inline-stage-map.json').read_text());oldaco=json.loads((R/'Development/results/gap-refresh-20261023/isa/aco-stats.json').read_text());aco={x['name']:x for x in oldaco};stats={r:json.loads((P/(r+'-statistics.json')).read_text()) for r in ['upwide','dectail','ffnb2']}
inputs={'production_git_base':'b5875bfa','module_manifest_sha256':hashlib.sha256((R/'hip/SHA256SUMS').read_bytes()).hexdigest(),'compiler':subprocess.check_output(['/home/lmxxf/work/llvm-build-23/bin/clang','--version'],text=True).splitlines()[0],'aco_reference':'/home/lmxxf/work/dlss5-aco-isa-20261023','aco_files':{f.name:hashlib.sha256(f.read_bytes()).hexdigest() for f in Path('/home/lmxxf/work/dlss5-aco-isa-20261023').glob('*.s')},'source_hashes':{str(f):hashlib.sha256(f.read_bytes()).hexdigest() for f in map(Path,['hip/wave_owned_mh.inc','hip/wave_owned_c32.inc','hip/deep_fast.hip','hip/vit_stream.inc','hip/build-modules.ps1','Development/HIP/hip_reference_network.h'])}}
(P/'inputs.json').write_text(json.dumps(inputs,indent=2));(P/'aco-resources.json').write_text(json.dumps({k:aco[k] for k in ['fswin32','fswin64','fswin128','fswin256','decups','ffwd3','attn','vitattn','gemmvnores','gemmvproj']},indent=2))
s='''# 9070 XT 活跃核 .s 汇编逐核解剖与 ACO 对账 — Phase 1

**结论：完成全量拆分、实际派发映射、汇编往返与真实 .s 修改的 GPU 验证；完成三项独立手术和全部 ABBA，三项均 STOP，生产保持 b5875bfa。** 这轮没有收下优化，但已把“242 VGPR 在哪里”“Up 为什么大”“少指令/高驻留是否就快”变成可复核的答案。

## 范围、身份与计数

当前40个gfx1201模块共1,976个AMDGPU导出，全部切为单核.s。新鲜六档Frame派发、observer/dump/profile关闭，实际覆盖56个物理模块/函数对。Original1088/1152每帧153次、900为157次；Small1080/900为83次、720为87次。冷准备阶段另有预热/模型切换，fresh-dispatches.json保留全部序列，稳态集合取最后三帧，不混作每帧派发。

二进制/编译器/ACO输入身份见inputs.json，实际活跃集合与各核opcode见active-summary.json。完整反汇编、源码副本、编译中间文件在work生成，按用户要求不入Git，完成后清理；脚本可重建。文中.s行号均指extract.py生成的单函数文件，1起算；DWARF源行另标probe.hip。统计严格用ELF函数symbol size截断，已经剔除函数间对齐NOP；不能把objdump直到下一个标签的整段都当函数。VOPD算一条编码指令，但执行两个运算；静态指令数不是动态执行次数、周期或stall时间。

本轮Gold是明确生产FAST1基线，不声称等于NV exact0.01。每候选七档42帧（两臂各3帧）0 bit-diff/invalid0；四档各3轮ABBA，240帧/slot、弃80、每arm960稳态样本、NET0/edges_only1，每slot独立进程且严格串行。三候选共144个性能slot。游戏检查、进程检查、D>=100GB、CreateNew gpu.lock、15秒看门狗全部沿用，命令与SHA锁在*-gold/perf.json，结果复算在*-statistics.json。

## 汇编闭环，含真实 .s 修改

canonical LLVM23配方（RowOpts、-real-true16）能从.hip生成带完整AMDHSA描述符与metadata的.hsaco.s。普通objdump反汇编是阅读材料，不能直接当完整可链接源；应使用编译器-S输出保留符号、标签、ABI、metadata。

```
clang -cc1as -triple amdgcn-amd-amdhsa -target-cpu gfx1201 \
  -target-feature -real-true16 -filetype obj input.s -o output.o
ld.lld --no-undefined -shared -plugin-opt=mcpu=gfx1201 output.o -o output.hsaco
```

默认集成汇编器的real-true16模式不匹配，最初拒绝半精度指令；-Xassembler形式也不支持。使用上面cc1as入口解决。C32往返64函数的全部机器码、资源和完整metadata逐项一致，整ELF SHA可以因容器布局改变，不能要求其相同。

进一步在c32_wave1_prefix_b8d_w16中把一条`v_lshlrev_b32_e32 dst,1,src`改为`v_add_nc_u32_e32 dst,src,src`，两者uint32模2^32结果同、都不写VCC/SCC、编码同长。仅该函数机器码变化，64核资源/完整ABI不变；重新组装的模块在真实9070上通过七档21帧loader/Gold。它是闭环证明，不是提速候选，也不进生产。见asm-patch-proof.json、roundtrip-abi-proof.json、asmproof-gold.json与verification.json。

## 靶子1：Up/decoder的大代码究竟在哪里

'''
s+='|核|指令|VALU|WMMA|wait（全部类型）|64位地址相关|LDS|VGPR|\n|---|---:|---:|---:|---:|---:|---:|---:|\n'
for n in ['decoder_project2x_h16w_byteout_w_bin_f8w16','c64_wave2_bi_bo','c64_wave2_up_lb_sh','c128_wave2_bi_bo','c128_wave2_up_lb_sh','c32_wave1_up_b8_w16']:
 r=a[n];c=r['counts'];s+='|'+n+'|'+ '|'.join(str(c[k]) for k in ['instructions','VALU','wmma','wait','addr64','LDS'])+'|'+str(r['vgpr'])+'|\n'
s+='''
**当前数字需先纠正：** 该decoder当前为3,612条，C32 up为1,354条，并非所有Up都3,000～5,400条。C64/128 up约4,500条确实成立。

C64正常bi_bo到up_lb_sh增加1,575条，其中VALU增加1,347条、WMMA仅增加16条。C128增加1,298条，其中VALU增加1,121条、WMMA只增4条。问题主体是Up坐标/边界、half/FP8转换、skip合并和打包，不是矩阵指令突然翻几倍。生产LowByte已经被后端合成b64，不存在该路径“每lane八条u8 global load”的前提。

定位很明确：C64 up的首个完整屏障前为.s行1～1869，普通bi_bo只到267；后面的FFN段分别1870～2526与268～921，几乎一样长。C128 up首段到1565，普通到249；后面FFN分别1566～2272与250～949。膨胀集中在Up前处理，不应全网盲关unroll。

decoder则同时包含1024→512、512→256及FullTile/边界专门化路径；整个函数的静态大小不能当成一条运行路径的执行量。尾部`it=0..1`把16通道skip/scale/融合/打包及地址生成复制，确实是可减少代码的落点。实验dectail只禁止这两次尾循环展开，字节输出3612→2648条，float输出2056→1511条；VGPR166→167（同分配档）、LDS2176不变、零spill，Gold全同，但性能不稳定，STOP。

ACO `decups.s`为261条、0 WMMA、1,516B执行代码；我们的decoder包含WMMA矩阵投影，不能直接以261比3612断言差十几倍。ACO对应投影/重排的完整边界需要组合对账。更不能用.s文本文件的23KB比较机器代码I-cache占用。没有本轮SQTT/SPM/缓存计数器证据，**不宣称已经证明L0 I-cache冲爆**。

## 靶子2：242 VGPR的物理活跃曲线

liveness.py按指令解析VGPR def/use，构建直接分支CFG并做固定点may-liveness，包含回边，不是“第一次出现到最后一次出现”的直线包络。曲线见[vgpr-liveness.svg](vgpr-liveness.svg)，峰值、屏障及回边寄存器集合见liveness-summary.json。它是lane-agnostic静态物理寄存器模型，不冒充运行时逐lane观测或源变量调试器。

|核|metadata /最高物理编号+1|静态live峰值|首次峰值.s行|屏障处最大live|\n|---|---:|---:|---|---:|\n'''
for n,v in live.items():s+=f"|{n}|{v['allocated_max_register']}|{v['peak_registers']}|{','.join(map(str,v['peak_lines'][:8]))}|{max(x['count'] for x in v['barrier_live'])}|\n"
s+='''
C128峰值行320～324、C256约409/412，都落在初始staging屏障之后、FFN写plane1屏障之前：**Expand→量化→Contract交错的FFN阶段**。不是Attention阶段，也不是242个寄存器全部跨屏障存活。C128 FFN结束屏障约949/968行时live只20/29；C256约1119/1129行时只17/20。

源码对应`contract[B][2]`与`expanded[B][T]`，B=4、T=2，各64个float累加器；再叠加预取输入片段、权重、量化临时值和仍存活的坐标/地址。C128约250～270行一次发出多组`ds_load_2addr_b64 v[147:210]`，与已有accumulator同时保活；峰值片段保存在liveness-summary.json。`contract`跨ht循环携带，`expanded`在内部kt链携带，它们最终落plane1后并未整体跨下一道barrier。仅手改metadata的242为192是错误做法，指令仍会引用高位VGPR。

直接实验ffnb2把C128/C256的B降为2，C64保4。C128242→180（编译器occupancy估计5→8），C256242→216（5→7），C128 Up238→180（6→8），LDS不变且零spill。C256未达到<=192，不冒称都到8。**四档平均耗时反而退20～39µs，驻留提升没有兑换成速度。** B减半使权重跨token复用变少、FFN批循环增加，是可见机制；没有计数器证据将全部退步唯一归因于某个stall。见occupancy-estimates.json与ffnb2-statistics.json。

SP需单独看：当前`sp_run256_w16`实际184VGPR，不是242；不能把普通wave2的压力曲线套到持久核，更不改其counter/barrier协议来凑驻留。

## 靶子3：C32逐阶段与fswin32

DWARF行表重编的64个C32、137个C64函数，机器码/资源全部同生产（debug-map-identity.json）。使用llvm-symbolizer内联调用链回溯到cw_body的调用点，避免把所有内联F/量化helper扔进未知桶。每阶段完整opcode、源行、汇编PC/行及例子见c32-inline-stage-map.json。它是语义归属，调度器可能把不同阶段指令交错，不把源阶段表当时间区间。

|核|输入|FFN含量化残差|QKV|Norm/查询准备|Score/Exp|Sum/prob|AV|OutProj|Post/Down|控制/未归属|\n|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|\n'''
for n,r in st['kernels'].items():
 d={k:sum(v.values()) for k,v in r['stages'].items()};vals=[d.get('input/staging',0),d.get('FFN',0)+d.get('FFN residual/quant',0),d.get('QKV',0),d.get('Norm/QKV storage/query setup',0),d.get('Score/Exp',0),d.get('Sum/prob',0),d.get('AV',0),d.get('OutProj/residual',0),d.get('Post/Down/tail',0),d.get('prologue/control/unattributed',0)];s+='|'+n+'|'+'|'.join(map(str,vals))+'|\n'
s+='''
当前prefix/post/chain/finish的窄LDS load/store数量均0。prefix只有1条ds_bpermute；post/chain/finish为0。主要LDS已是b128（prefix 4store+18load，post4+6，chain2+2，finish4+18），没有可凭名字直接批量替换的b32/u16垃圾。

prefix 1498条中有144条v_fma_mix、112条FP8转换、92条s_delay_alu、65条s_wait_alu；这些不全是VMEM阻塞。FFN、Norm和Score/Exp有真实舍入/依赖合同。64位地址相关指令prefix41、post39、chain24、finish24；涉及映射/边界/尾部多条输出，先看源调用点和基址不变量，不能把v_add_co都当冗余。

ACO fswin32当前8,738条静态指令、762 WMMA、1,336 VOPD，执行代码69,480B；我们的这些入口每个约1,000～1,500条，融合边界、展开程度、循环和每WG工作量不同。不能拿单个prefix对整个fswin32、也不能凭静态指令少就说我们更快。两边都已有VOPD/成对FP8/有界exp的实现；本轮未发现可无条件删除的C32重复mov/等待。

## 靶子4：C512与ViT

C512 compact：2,104条、188VGPR、6KB LDS，1,025条VALU；wait总137 = loadcnt58+dscnt45+ALU31+kmcnt3。不是137条都在排空VMEM。其.s行157/160/167已经用loadcnt0x13/0x9/0x7做部分等待。64条ds_load_u8集中在V片段重排，约1461行起地址偏移4096/4128/4160，**stride32，不能直接换成同址b128读取**，要改producer/consumer配对布局才成立。176条med3、72条成对FP8转换、65条cndmask、132条delay_alu是明确的标量/量化组织成本；它们并非全可删。

对照ACO attn为2,849条、240VGPR、208 WMMA；我们188VGPR反而更低。不同tile/融合/循环边界不直接比较404与208个静态WMMA来推FLOPs浪费。

ViT 640 vfrag仅290条静态指令、61VGPR，qkfrag282条/60VGPR，都0 LDS；它们有动态key-tile循环，不能拿290比ACO vitattn1626条就判胜。ACO更大展开与不同数据布局/供数需要按动态工作对齐。contract当前480条/112VGPR，project424条/112VGPR；尾部散写前一轮已测过LDS转置，VGPR门/性能门未过，本轮不换名重刷旧方案。

|族/入口|本方静态指令|本方VGPR|ACO入口静态指令|ACO VGPR|\n|---|---:|---:|---:|---:|\n'''
for n,k in [('c128_wave2_bi_bo','fswin128'),('c256_wave2_bi_bo_w16','fswin256'),('split_ffn_one_w2f8_w16_bin','ffwd3'),('c512_qkv_attention_compact_w16','attn'),('vit_attention_fused_640_bytein_bout_vfrag','vitattn'),('vit_stream_contract_frag_bstream_bout_compactw_w16','gemmvnores'),('vit_stream_project_n64_bb_bout_compactw_w16','gemmvproj')]:s+=f"|{n}|{a[n]['counts']['instructions']}|{a[n]['vgpr']}|{aco[k]['instructions']}|{aco[k]['vgpr_alloc']}|\n"
s+='''
这些是按机制族并排的结构表，不是相同工作量的计时对照。ACO的确大量使用非零wait流水，但LLVM这边也已有阶梯等待；热核的一条wait必须结合未完成读的目的寄存器和CFG判断。ACO的精确行号/局部片段在aco-excerpts.json，本方对应行号在hot-excerpts.json与anatomy.json；不伪造ACO缺少调试信息的源阶段边界。本轮线性扫描曾把qkfrag行453与541的DS wait判成重复，检查分支汇合后否决，未发射不安全删wait补丁。

## 三项实际手术与最终裁决

修改前后汇编差异、资源见candidate-isa-diffs.json（关键摘录）及各*-metadata.json，源改动见upwide.patch/dectail.patch/ffnb2.patch。全部在隔离副本编译，不混进下一项基线。

|候选|档位|Δ均值 µs|Δp99 µs|裁决|\n|---|---|---:|---:|---|\n'''
for role,table in stats.items():
 for n,v in table['tiers'].items():s+=f"|{role}|{n}|{v['delta_ms']*1000:+.3f}|{v['p99_delta_ms']*1000:+.3f}|STOP|\n"
s+='''
- upwide：一次8B LowByte读取再解码，测试解码/寄存器组织；生产本已合并b64，结果C64活跃Up4526→4563条、C1284586→4588，没挖出稳定收益。C64 Up_lb_sh仍197VGPR、C128仍238，无spill；一个非当前活跃的非half-skip备用export200→202，不扩围。
- dectail：实测少近千条静态指令，资源同分配档/无spill，Small1080三轮均值全退，其他档不稳，STOP。
- ffnb2：真实压低VGPR并提高编译器驻留估计，但四档均值全退，STOP。减少寄存器或指令数不等于减少整帧耗时。

本轮没有重测Mochizuki整网速度、没有做新的硬件stall归因，也没有宣称已追平其最新版本。得到的是三个机制明确的负结果、完整汇编操作能力和可复核的下一步地图。值得进一步隔离的是C512 V片段的stride32 LDS读取与FFN的供数/复用折中，而不是再次泛化删wait或机械追192VGPR。

## 复现与卫生

extract.py / active-map.py / anatomy.py / liveness.py / inline-stage-map.py生成分析；canonical compile-modules.py配合prepare-candidates.py、prepare-ffnb2.py生成候选；roundtrip.sh与asm-patch-proof.py打通汇编闭环；run-jobs.ps1执行远端锁定作业；verify-results.py独立核验Gold/CSV；完整证据索引为evidence-index.json。基线模块来源为backfeed-20261028/final-payload/HIP，与hip/SHA256SUMS逐项绑定。大反汇编和编译临时文件清理，原CSV/日志留忽略Git的各case evidence目录，Git只存报告、关键摘录、统计/身份回执和脚本。

生产源码/模块/玩家配置/已发布ZIP均未改，无部署、无push。清理与锁释放回执见cleanup.json。WorkingPlan/DevHistory/failed-experiments同步本轮结论。
'''
(P/'README.md').write_text(s)
