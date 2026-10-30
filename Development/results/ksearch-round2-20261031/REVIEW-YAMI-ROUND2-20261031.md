# Round 2 独立审查：R2-D收益成立，仍须进入Round 3

审查基线b1af6240；先读生产diff、原始CSV/JSON及ELF，再读评估报告。未运行GPU、未部署、未修改生产或原证据。

## P1-1：驻留表存在硬件拓扑错误，R2-B的“LDS锁死4 waves/SIMD”证明不成立

README第29行明确使用“128KiB/WGP / 8 SIMD，64KiB/CU / 4 SIMD”；table1-isa-resources.json也使用该分母。RDNA4实际上是4 SIMD32/WGP、2 SIMD32/CU。AMD ISA §2.3（PDF印刷页11）明确说明WGP的四个SIMD；本机LLVM源码AMDGPUBaseInfo.cpp:getEUsPerCU也返回CU模式2、WGP模式4。

依据：[AMD RDNA4 ISA §2.3](https://docs.amd.com/api/khub/documents/uQpkEvk3pv~kfAb2x~j4uw/content)。该节还说明WGP多wave工作组上限32，单wave工作组不占这个barrier资源额度，不能一律硬截为16组。

沿用报告自己的128KiB/WGP、16KiB/WG、4 waves/WG：

    128/16 = 8 WGs/WGP
    8 × 4 / 4 = 8 waves/SIMD（LDS上限），不是4。

因此其自报VGPR上限5→6并没有被该LDS上限抹掉。r2b测慢的事实成立，“因为LDS锁死所以所有只降VGPR路线无效”的根因解释必须撤回。不能由此反推r2b必快；调度、Grid、指令开销仍须实测。

同样：C64的16组×2/4=8，C32 CU模式16组×1/2=8；LDS=0单wave核不能靠LDS栏硬限为2。修全表，使用实际WG尺寸/ELF模式/硬件分配粒度并核官方工具或驱动结果，区分理论容量与实测活跃驻留。

## P1-2：最终晋升依旧未过Small1080尾延迟门，不能只强调7/7 mean负

从final/evidence逐槽out.csv独立复算与JSON一致：Small1080 mean -4.384688µs，pooled p99 +50.575µs；第二轮mean +16.133438µs、p99 +188.649µs。报告有列数字，但结论以“7/7 mean全负”宣布完整闭环，final-decision.json accepted=true没有解决这项门禁。

K5隔离-15.896458µs、pooled p99 -26.979µs成立；它回答K5单项，不等于最终K5+R2-D组合通过。R2-D组合相对e7b2的Small1080均值+0.128750µs；MH单项该档+9.394µs/p99+134.230µs。不能拿Original赢补这档。

Round3保全部旧批，使用最终模块做Small1080的K5固定、R2-D C32/MH四臂隔离，预先固定轮数/顺序/裁决，不刷到赢。未得到无尾回退证据前保持该scope未获自动晋升，或由Zero明确接受折衷。Original960最终R3 p99+87.304µs、Original1088 pure pooled p99+7.226µs也应继续明确保留。

## P1-3：当前基线探针实际仍是e7b2，且softmax父分支在晋升后消失了

probes-setup.ps1把ksearch-step4的HIP复制为Base，故表3.0是R2-D之前的e7b2敏感度，不是b1af6240的新基线敏感度。两组数值独立从timing.csv复算一致；这组探针确实推动了有用的R2-D，但不能据此宣布晋升后的机制已经充分搜索。

C32 P1同时删除指数映射、分母WMMA、倒数/乘法与概率打包，且只保留部分bias读取/score位模式作为替代概率；破坏下游与编译排程，差值不是这些子段的可相加预算。它支持优先查这片路径，不是“整网第一热点”的完整排序，也不能把噪声探针的小差当成噪声真实成本的严格上界。

将“C32 softmax剩余机制”明确列为Open、n=0。在b1af6240上刷新粗探针，再以指数位映射/概率量化为A、分母归约为B做更窄的2×2拆分（保留QK和AV及真实依赖，记录死代码消除与资源变化）。再选择保持原位映射/舍入的成对转换、常量运算合并或分母组织候选。不要把664µs减去22µs当剩余收益。

## P2-4：R2-D数值前提有合理依据，但全配置验证尚不能被42帧静态Gold替代

代码改动是每lane复用同列ones-WMMA结果的倒数，没有复用到其它lane/query。C32四次WMMA顺序不变；MH仍先分别累加两个side再相加，未改求和顺序。这一不变式成立时属于等价改写。所引10-06 C512 typed-basis记录最终确有非对称样本/原语bit0证据，不应误读前面失败段为最终失败。

当前生产变更同时编入normal/fast和SP等模块，但本轮final Gold是七档每臂3帧固定输入。它不等于FAST0、动态history、AE复用、F9/MP重建和缺导出组合都已测。可补针对新不变式的非对称lane/query原语，覆盖实际C32串行四项与MH两side布局；再补实际受影响的配置回归，不需无目的重跑全部历史。

K2门禁确已收窄模型/shape、fast_numeric、MP与AE，但`!opt.experimental_temporal`不等于“所有temporal/history关闭”：源码未查询本帧hist是否存在。报告写`!temporal`过宽。应明确只排除该实验路径，或补MODE1/history实测/显式门控；此处是范围不符，未发现已证数值错误。

## P2-5：表1的rcp列混入另一种指令，457万动态计数需附推导

本次直接对实际HSACO按ELF符号地址/size反汇编抽查：c32_wave1_chain_w16、c128_wave2_bi_bo各有1条v_rcp_f32_e32；另有1条v_s_rcp_f32。因此表列“v_rcp_f32 9→2”实际合并了两类指令，Softmax减少7条这一方向没错，但请分列或改名。sp_run256_w16实际1条v_rcp_f32，与表一致。

“动态457万”没有在报告中给出逐dispatch的Grid×waves/WG×qt×实际层数汇总；SP recover通常不执行body，不能按静态核存在算进来。该量应注明wave级指令次数还是lane级运算次数、Original哪个处理尺寸，并附可运行计算脚本；不能称硬件动态计数器读数。

## 已通过与仍开放的部分

- 80个payload/HIP实物逐SHA匹配hip/SHA256SUMS；两架构ELF flags分别0x48/0x4e。抽查上述三个实际核指令与C128 metadata（242VGPR/16KiB/wave32），未重做全部1976导出的完整ISA审计。
- 独立复算final七档84slot、r2d_both四档48slot、K5隔离12slot；R2-D Original1088/960增量-22.802/-22.904µs且三轮mean/p99均赢成立。
- 独立复算final-pure-abba36slot：对8a9c60fd，Orig1088 -9.018542µs/p99+7.2255µs；Small1080 -5.893344µs/p99-9.3335µs；Small900 -8.315938µs/p99-10.1088µs。命令记录的旧/新host和模块路径确实不同，已不再跨批偷减。
- 两组探针全部六个profile四臂从timing.csv复算，与probes-2x2-statistics一致。
- 家族账现在明确引用旧ACO Step2和新HIP RTC并把余数标未解释，比Round1正确；但依然是跨日期/计时域诊断，不能据其阶段差精确分摊当前NMMN缺口。

## Round3交接顺序

1. 修硬件拓扑计算与Small1080最终组合门；R2-B具体负实现继续STOP，撤销父机制的LDS不可能证明。
2. 当前b1af6240上刷新C32 softmax探针并拆指数/量化与分母两个子机制；该父分支保持Open。
3. 推进已经Open的ViT QKV单wave W16直接读取候选，先核实际重复权重请求、VGPR、Gold，再Frame；P0约30µs是排序线索而非兑现承诺。
4. C128重新按正确资源上限分析：r2b重算地址的额外指令可能抵消驻留收益；下一候选须在保持B4/T2下减少live range且避免每ht追加开销，与旧负实现明确不同。

结论：承认R2-D的已测收益，拒绝“八项已全部闭环/全档晋升已通过”。报告已经自己列出Open节点，无需等审稿人授权才承认搜索未结束；但本次审查不自动抢主攻端GPU队列。
