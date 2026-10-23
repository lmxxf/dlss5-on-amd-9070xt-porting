# a9b13dd4 ACO 机器码与当前 HIP：CPU 反汇编实证

2026-10-23。只做本地源码、SPIR-V 编译、机器码解析和官方资料核查；本报告未运行 GPU、未重编生产 HIP、未动游戏/驱动。

## 1. 身份终于锁到了机器码

- Mochi 源码固定 `a9b13dd4cdb5f4bf43db358492d72e6e5e2acd67`；完整源码在 `/home/lmxxf/work/dlss5-gap-source-20261023/DLSSNR-AMD-a9b13dd4cdb5f4bf43db358492d72e6e5e2acd67`。
- 本地 glslang **16.5.0**，按该版 `linux/shaders/rdna4/pipelines.json` 每一项的实际宏编译。**52 个网络 SPV 全部按上游 FNV64 算法命中对应的 52 个 `.nrp`**。这证明名称→SPV→所附 ACO 机器码的身份链，不只是按函数名字猜。
- 按该版 `windows/src/core/nr_pal_binary.hpp:107–139` 解析 `NRPAL001`，校验原 code checksum、实际长度；截取 `exec` 字节，包装为只用于反汇编的 gfx1201 ELF。**52 个记录全部成功反汇编，0 unknown instruction**。没有尝试加载这份人工 ELF。
- 反汇编器 `/home/lmxxf/work/llvm-build-23/bin/llvm-objdump`。完整重建 SPV、`.elf`、`.s` 在 `/home/lmxxf/work/dlss5-aco-isa-20261023/`。记录不是本轮重新用 ACO 编译的：其 Mesa commit/完整生成配方仍未独立恢复。
- HIP 来自 `/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201`，**40/40 模块逐 SHA 与当前仓 `hip/SHA256SUMS` 一致**。本轮解析其中八个热点模块的 842 个导出函数（包含备用导出，不能称 842 个实际派发）。用 ELF symbol size 截断函数，排除函数间对齐填充。
- 可复现脚本：[isa/analyze.py](isa/analyze.py)。原始表：[52核 ACO](isa/aco-stats.json)、[当前/候选 trace 命中的41个 HIP 导出](isa/hip-stats.json)（全842导出表留实验工作目录；未把旧 `deep:vit_gather` 纳入此热点目录）、[HIP 模块身份](isa/hip-module-identity.json)。脚本只读生产载荷，输出实验目录。

**同名 HIP 导出分布在多个模块且机器码不同。** 例如 `split_projection_frag_w16_bskip` 在 `deep_fast-packed-fast`、`vit-stream-fast`、`c512-m32-deep` 都存在；下表按生产源码选择模块，主进程提供的 `/tmp/dlss5-trace-current.log` 与 `trace-candidate.log` 已核对下表所列热点的模块别名。不能从备用模块挑一个比较好看的数。

## 2. 实际静态机器码表

口径：VALU 排除 WMMA，VOPD 双发射 bundle 计作一条指令；VOPD 列是其中双发射 bundle 数。VMEM 只统计 global/buffer/flat load/store，wait 为 `s_wait*`（不含 `s_delay_alu`）。这些是**代码体静态数量**，不是每帧动态次数、周期、DRAM 字节或瓶颈百分比。

|实现/核|VGPR|LDS B|WG线程|静态总指令|VALU|VOPD|WMMA|VMEM读/写|wait|
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
|ACO `fswin32`|192 配额|2048|32|8738|6169|1336|762|288/24|340|
|HIP `c32_wave1_chain_w16` (`c32-wave1-fast`)|133 使用|4096|32|982|652|148|54|33/2|64|
|ACO `ffwd3`|240 配额|21504|128|1780|1128|16|160|90/5|90|
|HIP `split_ffn_one_w2f8_w16_bin` (`c512-m32-deep`)|95 使用|0|64 上限|684|479|106|32|20/32|40|
|ACO `attn`|240 配额|9728|64|2849|1705|121|208|114/3|208|
|HIP `c512_qkv_attention_compact_w16` (`c512-m32-mh`)|188 使用|6144|64|2483|1367|245|404|168/2|136|
|ACO `vitattn`|96 配额|0|32|1626|1099|40|64|36/4|92|
|HIP `vit_attention_fused_640_bytein_bout` (`deep_fast-packed-fast`)|68 使用|0|32|320|216|38|5|20/2|15|
|ACO `gemmvnores`|192 配额|0|128|988|674|64|32|16/128|24|
|HIP 当前1088 contract (`vit-stream-fast`)|208 使用|4096|32|813|406|151|64|117/34|77|
|HIP 候选/1152 compact contract W16 (`vit-stream-fast`)|112 使用|4096|32|482|241|55|16|49/34|45|
|ACO `gemmvproj`|192 配额|0|128|1484|826|64|32|160/128|152|
|HIP 当前1088 projection (`vit-stream-fast`)|120 使用|0|32|534|303|94|16|56/32|56|
|HIP 候选/1152 compact projection W16 (`vit-stream-fast`)|112 使用|0|32|424|226|58|16|48/32|33|

HIP 的 WG 列来自 metadata `max_flat_workgroup_size`，不是 GPU trace；ACO 为 `.nrp.wg`。ACO 配额是上游实际适配器使用的 `((rsrc1 & 63)+1)*8`，HIP 列为 metadata `.vgpr_count`；两者还要经过硬件分配粒度，不能把数值相减当空闲寄存器。

**不能得出“我们的指令更少，所以理应更快”。** ACO C32 的 `NR_EDGE_BODIES=1` 生成多段主体，HIP 有 `qt` 循环；ViT attention 的 query tile、循环展开、softmax 算术也不同。C512 QKV-attention 双方实际都是每wave32个query、每WG两个wave处理64点窗口；但HIP K32深流水展开与Mochi循环/edge-interior多路径不同，404 vs 208仍不能直接解释动态WMMA差。完整帧需按实际路径、循环次数、网格乘权。此表首先排除了“ACO 在每个热点都用更少 VGPR/更少静态 VALU”的简单归因。

### Waves/SIMD 能说到哪一步

[AMD ISA §3.3.2](https://docs.amd.com/v/u/en-US/rdna4-instruction-set-architecture)规定 1536-VGPR/SIMD 设备的 wave32 按24寄存器粒度分配。在该设备配置、常规静态分配下，**仅 VGPR 的驻留上界**为 `min(16, floor(1536 / round_up(vgpr,24)))`：

|对应核|ACO 仅VGPR上界|HIP 仅VGPR上界|
|---|---:|---:|
|C32主体|8|10|
|C512 FFN|6|16|
|C512 QKV-attention|6|8|
|ViT attention|16|16|
|ViT contract（当前1088 / 候选compact）|8|7 / 12|
|ViT projection（当前1088 / 候选compact）|8|12 / 12|

这些**不是实际 occupancy**：WG 的 SIMD 分布、CU/WGP mode、LDS、驻留工作组槽位及运行时资源竞争继续限制。52 份 ACO ISA 未见动态 `alloc_vgpr` 指令。不要重用旧历史中某个核“5 waves/SIMD”的推算来覆盖新版。正确下一步是用实际派发映射＋该驱动资源配置/trace取值。

## 3. C32 激活：能借的是操作数组织，不是“ACO会双发射而HIP不会”

锁版 `linux/shaders/rdna4/fswin_t.comp:2386–2445` 的 `NR_ACT_ALT=3`/`NR_ACT_PAT` 已在 Linux recipe 激活：相邻分量交换两次 FMA 的乘数顺序，让寄存器分配更容易满足 VOPD 目的寄存器偶奇约束。源码表达有价值，但我方当前机器码**已经**将第二级 Horner FMA 合成 VOPD。

- ACO 示例：首级是 `v_fma_mix_f32`，常量 literal **`0x3728ab28`** 打包两个可精确表示的 half 系数（−0.055908203125、+0.447265625）；输入 `abs(t)` 仍是 FP32，输出 FP32。二级是 `v_dual_fmaak_f32`，最后乘法也成对。
- HIP 当前 C32 chain：首级 `v_fma_f32 abs(t), SGPR系数, 0x3ee50000`；二级已经有 **8 条 `v_dual_fmaak_f32` bundle**，处理16个分量。全核还有148条 VOPD。
- ACO clamp 为 `v_maxmin_num_f32 -4,t,4`，HIP 为 `v_med3_num_f32 t,-4,4`。二者已都是单指令；有限域等价不代表 NaN/有符号零合同天然相同，也没有实测证明 maxmin 比 med3 更快。

可落地候选：先在孤立 HIP 激活批处理内试 **FMA_MIX 的两常量打包＋交替操作数布局**，锁住原舍入和有限域，看是否减少SGPR常量依赖/活跃范围、改善双发射或代码字节。若只换源表达而 ISA 不变就停；不能给它预记百分比。大块 inline asm 反而会挡住编译器跨块调度，先用 builtin/很短 asm。

证据：[ACO 片段](isa/aco-c32-activation-excerpt.s.txt)、[HIP 当前 C32 激活片段](isa/hip-c32-chain-activation.s.txt)。前者能直接看到 `s_wait_loadcnt 5/4` 之间有 WMMA：代码并没有要求每次矩阵计算前排空所有内存请求。

## 4. “AMD不能边算边搬/WMMA和VALU绝不重叠”必须拆开

1. **“RDNA4不能把global load和矩阵计算交错、隐藏内存延迟”是错误的泛化。** AMD 2026-10-16 [RDNA FP16 GEMM 官方文章](https://rocm.blogs.amd.com/software-tools-optimization/rdna-fp16-gemm/README.html)把“在WMMA间插入预取”作为一个独立优化阶段，实测包含9070XT。它同时强调缓冲深度、寄存器/LDS与驻留的取舍。FP16示例证明架构具有这条路径，不承诺本项目FP8上具体收益。
2. **“没有NVIDIA TMA、没有CDNA式 `global_load_lds`”不等于没有异步未完成访存。** RDNA4 ISA有VMEM计数/等待和矩阵 transpose load；不要把 `DS_DIRECT_LOAD` 混称为全局内存→LDS，它是LDS→VGPR广播。当前ISA目录及指令表未列 `global_load_lds`。
3. **“FP8 WMMA与VALU完全不重叠，甚至跨wave”在本次官方资料核查中没有找到足够直接的证明。** ISA §7.12.1列的是数据依赖和可能stall，不能推出独立指令/跨wave所有执行资源的互斥。反过来，看到交错ISA也不能证明物理管线重叠。必须把同SIMD、跨SIMD、同wave和跨wave区分；完整断言需受控微基准或明确架构吞吐文档。
4. 即使某种VALU/WMMA吞吐互斥成立，**Mochi和我们跑的是同一块9070XT**。它只能说明为什么应减少VALU、供数和等待，不能单独解释两实现之间的毫秒差。

本轮无此微基准，不把该说法当本轮已测硬件定律。仓内 `results/c64-block-fused-20260927/README.md` 将它记为对方的说法，`hip-roofline-20260930` 用作模型假设；这些引用本身不是独立的跨wave互斥微测。若追溯其原始微测，仍需核精度、依赖链、wave映射和时钟。

## 5. ACO代码能否放进HIP

[LLVM AMDGPU ABI](https://llvm.org/docs/AMDGPUUsage.html)区分 amdhsa、amdpal、mesa3d 三种装载环境；Windows HIP使用的HSA kernel descriptor/kernarg，与Vulkan descriptor-table/user SGPR不同。

Mochi锁版实现已经给出一个可运行方向：`nr_pipeline_binary.hpp:186–255` 经Vulkan获取PAL模板，`nr_pal_binary.hpp:229–300` 在单compute入口上拼接ACO代码，补SGPR适配stub并改resource metadata。`.nrp`还显式存set0、push、grid、inline参数位置、资源寄存器和workgroup大小。**它没有输出HIP可直接加载的.hsaco。**

|路线|是否可做|现在的建议|
|---|---|---|
|HIP源码/内建函数借鉴N-pair供数、边界组织、激活批处理|可直接实施实验|优先；保每个K段舍入、量化和Gold|
|HIP短inline asm固定单条FMA_MIX、转换、load/wait|可做；项目已有asm基础|明确操作数/early-clobber/内存依赖，先ISA后Gold再ABBA|
|HIP内用大块asm手写完整核心循环|技术上可做|要管理寄存器、EXEC、MODE、wait、hazard；容易失去跨块调度与可维护性，先选小热点|
|把ACO整核机器码复制进现有.hsaco|不能直接替换|需要HSA启动stub、descriptor构造、kernarg到user SGPR、workgroup ID、scratch/LDS/float-mode/元数据与ISA目标适配|
|独立Vulkan小子图复用ACO→PAL|上游有实例|比假装HIP编译开关诚实；引入interop/同步，子图收益需覆盖交接成本|
|将ACO本身接入HIP编译链|工程上另一个编译器项目|ACO吃NIR，不直接吃HIP/LLVM IR；本阶段不作追平首选|

最有价值的借用对象是 **已定位的寄存器生命周期、load/wait安排和字节布局**。本轮机器码表没有支持“整体改成ACO就自然赢”的结论，也没有支持“ACO不可用所以只能认输”。

## 6. 本轮值得先做与不值得重做

1. **先核1088是否实际启用了1152那批优化。** 当前 `hip_reference_network.h:478,495` 把compact ViT tail资格限定为1920×1152；`VitEntryExitByteOk`又依赖相应函数指针。原始1088对账会漏掉部分10-14的W16/字节链，不能假设优化全覆盖。实际trace已确认当前1088为161次派发、compact=0/EntryExit=0；只扩两条资格的候选为153次、compact=1/EntryExit=1，主进程3帧输出逐位一致。主进程3轮NCCN实测 **8.570213292 → 8.354502188 ms（−0.215711104 ms / −2.517%）**，每轮均值/p99均改善，pooled p99 **8.816156 → 8.5707569 ms**；12槽首尾保持旧153ac。它已由假设升为有证据的隔离候选，尚未合入生产。统计原表见主报告的 `compact-statistics.json`。
2. **N-pair QKV权重＋计算片顺序**：与我方K方向W16配对不同，可在现有布局族上单独试；先用ISA证明读取与活跃区间有改善，而非重复旧W16。
3. **激活首级FMA_MIX/operand pattern**：小而可控，ISA先筛。第二级VOPD、med3和短段FP8 MODE已有，避免重复宣功。
4. **从实际最热核挑load/wait预取窗口**：a9有具体partial waits与消费点，可借安排；不能按全核静态wait总数排序收益。
5. **不要依据这张静态表再做一轮全局VGPR封顶、全编译器替换或全部内核asm重写。** 最新HIP在多个热点的寄存器上界已经优于a9，差距要落到实际工作、依赖和运行路径上。
