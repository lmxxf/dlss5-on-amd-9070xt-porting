# 1x 纯 NN 差距刷新与 Mochizuki a9b13dd4 解剖

2026-10-23，Zero / 鳩指定的第一阶段。已完成当前生产1x与锁定旧Mochi的同批刷新、旧HIP回放对照、a9源码/ACO ISA解剖，并额外验证一个无需改内核的1088路由候选。**生产代码、已装游戏、驱动、正式发行包均未改；未推送。**

## 1. 结果先行

| 同批比较（各3轮4-slot ABBA） | A GPU ms/frame | B GPU ms/frame | A−B |
|---|---:|---:|---:|
| 当前生产1x N / 锁定旧Mochi M | 8.533050354 | 7.867127083 | **仍差0.665923271ms** |
| 10-06旧HIP O / 当前生产1x N | 8.923111219 | 8.554250771 | **已追回0.368860448ms** |
| 当前生产1x N / 本轮仅扩1088资格候选 C | 8.570213292 | 8.354502188 | **候选再省0.215711104ms（2.517%）** |

三行各有自己的交错对照，N的不同绝对值体现批次/热态差异；**不跨行相减，不把候选的8.3545减第一行M当已测剩差**。候选还没合生产；表中“当前”仍指未改资格的产品路径。10-06历史8.931545125 / 7.885259375 / 差1.046285750ms留作历史，不用它替代本次旧→新同批收益。

- 当前/M三轮差：0.661819 / 0.664817 / 0.671134ms。
- 旧H/当前H三轮追回：0.364450 / 0.369044 / 0.373087ms。
- 扩资格候选三轮省：0.208327 / 0.203934 / 0.234872ms；三轮p99分别降低0.208674 / 0.199873 / 0.273705ms，合并p99 **8.816156→8.570757ms**。
- O、N、C的完整1088 RGB32F都为同一个SHA `153ac018f5dd5744cff9157661c46c469d01db6018a97c93aec2b1e2e05647f1`；M重复输出为历史 `624d9d1a…a612`。所有H帧有限、首尾逐位一致；M全部槽SHA相同，另下载一份全图核finite/alpha，见 `mochi-raw-check.json`。
- 锁定旧M只有聚合GPU总时间，**没有它的每帧p99**。没有伪造跨M尾延迟结论。

原始数据与复算：[statistics.json](statistics.json)、[compact-statistics.json](compact-statistics.json)、[evidence/](evidence/)、[compact-evidence/](compact-evidence/)。

## 2. 最重要的新发现：1088没有吃齐10-14优化

实际生产 `Development/HIP/hip_reference_network.h:478,495` 的 `InitCompactContract` / `InitCompactProjection` 只允许 `W==1920 && H==1152`。这原本是紧凑权重验收的保守资格，后续ViT W16/字节链又依赖它生成的函数指针，导致1088同为640token却走旧尾部。

| 实际冷帧及后续两帧 trace | 当前N | 隔离候选C |
|---|---:|---:|
| compact Contract / Projection | 0 / 0 | 1 / 1 |
| Contract / Projection W16 | 0 / 0 | 1 / 1 |
| ViT进出字节边资格 | 0 | 1 |
| C512字节链资格 | 1 | 1 |
| 每帧实际dispatch | **161** | **153** |
| 完整RGB输出 | 153ac…47f1 | 153ac…47f1 |

因此原任务的“我们153 vs他123”在**当前1088生产路径**上不成立。153属于接通这些路径的形态，不能由默认宏值推实际命中。trace没有使用会关闭快路径的stage observer，没有逐核插event；独立诊断产物与普通计时产物分开，三个诊断帧全部核同Gold。

候选只有两个条件从 `H==1152` 改成 `(H==1152||H==1088)`，没有改任何HSACO、核数学或640-token布局。它接通compact Contract/Projection、ViT内部及进出byte边，继而让Up39 byte-input/F8W16路线可用。具体补丁、源码闭包、构建及资格依赖审计在 [benchmark/compact1088-review.md](benchmark/compact1088-review.md)。

本轮已证**1920×1088、Original1x、FAST1、固定受控输入**的正确性和收益。尚未把资格放宽合入生产，也未扩称900/Small/history/热切换或全部输入已验；第二阶段落地时沿现有产品兼容门验收。

## 3. 测量合同与复现

- 同一9070XT，驱动 `32.0.31007.2048`，没有切驱动/超频。
- 1920×1080有效区、1920×1088处理区、ViT640token；Original全71块，MP1、FAST1、Style1、seed0、AE0、historyoff。H保旧对账PDL1；trace中PDL能力有效但实际any-order派发为0，析构lifetime_pdl_calls=0；纯NN没有D3D Bridge SubmitPulse。
- 共同half-exact encoded gradient。M读有效RGBA32F，H读同有效图+反射补边RGBA32F；输入只上传一次，H每帧prefix生成noise、M缓存noise的差别保留。
- 每槽80 warm +160 timed，每个4-slot比较两臂各320帧，三轮各960稳态帧。第一组NMMN、第二组ONNO交错三轮；候选另做NCCN三轮。共36性能槽，未刷掉失败轮。
- H计时保持10-06 host：外层两个HIP event覆盖Enqueue，Synchronize后读elapsed；CPU chrono单列。M保持 `--chunk 1 --host-boundary --reuse`、每帧submit/fence。没有把APP/游戏帧时混入。
- 每槽只在计时区外读首尾；trace另跑三帧逐帧读回。普通benchmark的CURRENT_PROFILE显示默认选择，不替代trace实际派发。
- 当前生产源HEAD `c24e3a97`；当前80模块与10-17最终清单和仓库SUMS全同，实际使用gfx1201的40模块。新host从当前真实options工厂生成，明确禁Small；exe `27561d9b…40cd7f44`。构建与全部源SHA见 [benchmark/](benchmark/)。
- 旧HIP exe `1efec59d…e4fd`、旧模块清单逐SHA核同；M锁 `d1185d25141b1714d7837151b6fa782e6427568b`，exe `8ad3ac1c…d153`、model及68项相关SPV/plan资产起点核同，测试后旧树未变。
- 每次GPU阶段均先game-check、排GPU/RTC进程、实验盘≥100GB、CreateNew独占gpu.lock，15秒看门狗只停止自己的probe；最终释放锁。D盘起始约320.7GB。

远端实验完整raw：`D:\DLSSNR-Lab\gap-refresh-20261023`；本地隔离host构建：`/home/lmxxf/work/dlss5-gap-refresh-20261023`。仓库保CSV、命令、SHA、日志和脚本，大raw留实验目录。`run.ps1` / `compact1088.ps1` 拒绝覆盖已存在槽，复跑需新结果目录，不能直接覆盖原数据。

预检有一次输入身份错误被拦在GPU锁之前：10-11调研写的 `b3d114…` 实为**processing1152**，不是valid图。按10-06原始input.json纠正为valid=`17b2e09b…`、processing1088=`3ef42d34…`，原失败留 `preflight-hash-failure.log`。另本地Windows ZIP反斜线解包后规范路径才复算，未重测GPU。

## 4. a9b13dd4四类差异：哪些已经弄清

详细源码证据：[source-differences.md](source-differences.md)。机器码表、Waves/SIMD资源上界、官方资料和ABI：[isa-and-portability.md](isa-and-portability.md)。

### (a) 跨核：TCHAIN与dispatch减少要分开

TCHAIN让consumer按tile-counter等待已就绪输入，移除部分全局barrier，**不减少dispatch数量**。默认Windows kinds0x1、ACO0x1ff，实际还受核/offset/tile资格限制。QKV→全局attention仍需要完整依赖。

更少派发主要来自小通道完整持久stage（含DS-last/UPS-first），以及pack/gather组织。M持久stage的中间结果仍写global，不能说一派发就把所有层留寄存器。我方当前C256只包内层，C64/C128更碎；但现有ViT tile-ready已证明重叠且净慢，旧边界融合也有负账，不能简单照搬TCHAIN全网开。

### (b) 算术：med3不是尚未打开的秘密开关

两边都已有float多项式、FP8成对转换和单指令clamp。M一些softmax用预烘bias、half/packed位映射、不同分母归约；这些包含数学变化，不能从源码看着短就称无损。

ACO C32首级Horner用 `v_fma_mix_f32` 把两个精确half常量装进一个literal，另有ALT/PAT交换FMA乘数来帮助VOPD配对。我方第二级已经有8条dual_fmaak bundle，值得借的是**首级混精度指令和操作数生命期**，不是“给HIP加双发射”。M maxmin与H med3已经都是一条指令，未证明名称不同就有收益。

### (c) 供数：N-pair与W16的配对方向不同

M N-pair把相邻两个输出N16片配在一次16B加载里，一份A更新两个输出片；H W16把同一输出的两个相邻K16步配起来。两者都可整wave连续读512B，但累加器数量、A重复读取、生命周期不同。HIP可实现N-pair，需生产者/消费者成对换布局并保K累加次序。

M ViT attention每wave两个16-query片共享K/V；H当前每wave16query，softmax/归约也不同。历史M32和half-den多次负账不能清零；下一步必须以真实供数重复或资源变化提出新的具体实现。

### (d) 编译器：已锁到真实发布机器码，不能再笼统怪LLVM

按a9锁版recipe重编52个网络SPV，**52/52准确命中发布.nrp**；提取ACO代码反汇编0 unknown，HIP40模块与生产SHA核同。没有重新生成ACO代码，Mesa完整生成配方仍未恢复。

ACO并不普遍寄存器更少：C32配额192、C512 attention240、ViT attention96；所选HIP分别使用133、188、68（allocation与used不同，详表）。静态VALU/WMMA数量受循环展开、edge路径、tile组织影响，不能直接当动态每帧工作或周期。

当前1088的ViT Contract为208 VGPR、813条静态指令；扩资格后的compact W16是112、482。这个变化已有本轮完整帧收益支撑，但0.216ms是**整组路由改变**，不能全部归给寄存器一项。

## 5. 硬件说法与ACO复用边界

AMD官方RDNA GEMM文档明确使用global-load预取穿插WMMA隐藏延迟；“AMD不能边算边搬”过宽。没有TMA/CDNA式global_load_lds不等于没有未完成访存。完整的“FP8 WMMA与VALU甚至跨wave绝不重叠”本轮未找到足够直接的官方证据；交错ISA也不能反证物理管线互斥，需要单独受控微测。无论如何，双方使用同一块9070XT，硬件限制本身不是两者差额的因果解释。

HIP能用builtin/短inline asm固定FMA_MIX、量化或load/wait安排；完整asm循环也可做，但要自己管理EXEC/MODE/hazard和寄存器。**ACO .nrp不能直接塞进.hsaco**：Vulkan/PAL user SGPR和descriptor ABI不同于HSA kernarg/kernel descriptor。若借整核，需启动stub、参数/descriptor布局、scratch/LDS/float mode/元数据适配。M目前走的是ACO→PAL→Vulkan导入。优先移植可解释的小段ISA和布局，不先开另一个编译器工程。

## 6. 第二阶段候选排序

收益除第1项均未实测，不给虚构毫秒预测；排序综合覆盖范围、可移植性与负账。

|顺序|候选|预期收益依据|HIP可移植性 / 验收|
|---|---|---|---|
|1|1088 compact资格放宽|**已测−0.216ms**，161→153，Gold0|最高；仅两处host门，产品兼容回归后再合入|
|2|C32首级FMA_MIX打包常量＋ALT/PAT|高频激活，现有具体ISA差异；增量未知|高；builtin/短asm，先ISA再完整Gold/ABBA；不重复已有二级VOPD|
|3|C512 QKV/投影N-pair与片段循环|减少A重复供数的机会，覆盖16块；未知|中；新布局成对缓存键，保累加顺序，先一个热点|
|4|实际热点load/wait与地址计算|对方有partial waits/具体复用证据；未知|中；按PC/循环消费点做，不全网扫参数|
|5|单条新C512 TCHAIN边|可能减少全局依赖空洞，覆盖面大但同步成本高|中低；限定新W16/byte边，release/acquire、回绕/超时/饿死全验；与已负ViT pair区分|
|6|C64/C128含DS/UPS完整stage|真正减少派发，但实现量与数学兼容成本高|低；先解决当前LLVM23与旧SP recipe差异，不重跑原负候选|
|条件研究|ViT32query+tile供数/分母组织|有复用差异，已有多次负账|需新ISA证据再立项；数学变化单独PSNR/画质验收|
|后置|ACO整核ABI桥或独立Vulkan子图|作者组合模式有优势，未分离编译器份额|工程量大；交接成本和运行时兼容必须单测|

## 7. 尚不能宣布的结论

- a9 Windows ACO作者表 **7.21→5.70ms** 是另一版本/组合路径的发布数据；本轮没有在9070加载该新版运行，不能说当前只差0.666ms就等于追平最新ACO。实测对手仍是锁版d1185d2，a9是源码+ISA研究对象。
- 已知道路由资格的0.216ms可回收，也知道其余几类真实差异；**尚未把剩余差额逐项做因果分摊**。要完成分摊需新版M同版本计划/数学/输入锁定后的消融，以及H逐候选同批测试。
- gfx1200仅已有模块编译/身份，不是本轮实机；本轮不替换任何游戏文件、不发布新版本。

独立复算及trace覆盖审计：[benchmark/final-review.md](benchmark/final-review.md)。本轮原始CSV重新复算通过；GPU最终锁释放。
