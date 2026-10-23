# 刀2独立数值/asm源审：C32首级FMA_MIX与ALT

2026-10-23。只读审计，无GPU/RTC，无生产修改。候选来自 `mochi_isa` 的隔离树 `/home/lmxxf/work/dlss5-gap-phase2-20261023/knife2/{mix,mix-alt}/hip/wave_owned_c32.inc`；补丁与编译/ISA结果由该任务归档。审计只覆盖刀2，不启动刀3。

## 结论

**运算设计在当前FAST1有限域可保持原结果；必须保留原clamp、F32变量、F32结果、末乘原v和原FP8量化。** 第一版packed literal被LLVM MC拒绝，原失败保留；最终版已改为一个32位SGPR承载同样位串，编译成功，下面源审与metadata复核均通过。

这不是把激活改成half，也不是换hard-swish。首级仍是一次F32 fused multiply-add，只将两个本来就精确可表为half的常量在指令输入端扩宽。ALT仅交换第二级FMA的两个乘数，不重排第三个加数或后续乘法。

## 1. 位级操作数映射

原首级：`q = fmaf(abs(g), -0.055908203125f, 0.447265625f)`。

两个常量经Python `struct.pack('<e')`/unpack独立核对：

| 系数 | half位串 | 扩宽后的F32位串 | 是否精确等于原F32常量 |
|---|---|---|---|
| -0.055908203125 | `0xab28` | `0xbd650000` | 是 |
| 0.447265625 | `0x3728` | `0x3ee50000` | 是 |
| 第二级加数0.89453125（本刀不改） | `0x3b28` | `0x3f650000` | 是 |

承载word应为 **`0x3728ab28`**。首级指令的三个源依次为承载word、`abs(g)`、承载word：

- `op_sel_hi:[1,0,1]`：源0/2按half解释，源1仍按完整F32解释。
- `op_sel:[0,0,1]`：源0取word低16位，源2取高16位；源1的这个位对F32无效。
- `_f32`结果写完整32位，不使用`mixlo_f16`/`mixhi_f16`，没有中间half舍入。

AMD RDNA4 §16.10（PDF页406）的伪代码先按这些位选择输入并将half扩成F32，然后做FMA写F32；LLVM modifier文档同样明确`op_sel_hi=1`选择16位输入。这与普通packed VOP3P的“高半操作”命名不同，不能误解。[AMD RDNA4 ISA](https://docs.amd.com/api/khub/documents/uQpkEvk3pv~kfAb2x~j4uw/content) · [LLVM modifier语法](https://llvm.org/docs/AMDGPUModifierSyntax.html#vop3p-mad-mix-fma-mix-modifiers)

ACO反汇编中两个源显示`0xab28`，但指令末word为`3728AB28`；**不能将显示的短literal照抄为`0x0000ab28`**，否则源2高half为零，公式错。以实际编码和回读为准。

## 2. 有限域数值检查

候选沿用`g=cw_act_gate(v)`，所以有限v得到g∈[-4,4]；首级的两个系数扩宽精确，无新增舍入。

`q`的数学区间为[0.2236328125,0.447265625]，始终是正常、正、有限数；不会在首级生成Inf、NaN、次正规或零。相同生产round/denorm mode下，单FMA的精确乘加与最终F32舍入保持。

- **g不能转half。** g来自F32累加器，必须保持源1的`op_sel_hi=0`，`v(g)`传参也不能先构造half。
- **abs不能移动到原v或最终乘积。** 只对clamp后的g取绝对值用于首级；第二级使用有符号g。
- **末乘必须保原v。** `v*p`不能改成`g*p`，因为|v|>4时模型仍用原v乘饱和后的门函数。
- 原clamp不变；指令不得加`clamp`修饰符（它会把FMA输出夹到另一范围）。
- 对±0，首级q均为正0.447265625，第二级p均为正0.89453125；末乘原v保±0。随后原FP8量化对零的处理不动。
- 对F32极小次正规g，在生产RNE下首级微小负修正不足半个q的ULP，q仍舍入为同一常量；不过最终验收仍应比较old/new kernel的`float_mode`，不靠猜测去扩大到其它round mode。

`mix-alt`只让奇数e采用 `fmaf(q,g,0.89453125f)`，偶数保`fmaf(g,q,...)`。在上述有限输入域，乘法交换不改变精确积、融合加数或舍入；精确抵消到零的边界也没有引入新减法。它能否增加VOPD配对是代码生成问题，不是这条数值等价的结果。

## 3. NaN、Inf与异常作用域

本刀不应对外承诺任意NaN payload逐位等价。我们保持FAST1生产有限输入合同，完整网络finite/Gold仍由实际GPU结果裁决。

原`cw_act_gate`使用原fmed3/ternary规则；候选没有改NaN过滤、饱和范围或MODE。若原v为Inf，g仍被原clamp约束，但末乘仍可能Inf/NaN；不能靠新helper声称异常被修好。若要覆盖非有限输入，应分别记录分类、符号和payload，而不是把分类相同写成bit0。

**不可变更**：全局打开FP16_OVFL、把原round mode改掉、把原half分支合入本候选、删除FP8饱和，都不是本刀范围。

## 4. Inline asm约束审查

设计为单条 `v_fma_mix_f32`，结果`"=v"(q)`、变量`"v"(g)`、常量承载实际使用`"s"(0x3728ab28u)`。

- 单指令所有源先读再写，结果寄存器与g合法重叠；不需要`earlyclobber`。若以后改多指令串，在写q后又读g，就须重新审。
- 使用固定输入/显式输出即可；没有内存访问或副作用，不需要`memory` clobber。强加它会限制调度，不能借“安全”偷改性能路线。
- 不必`volatile`；结果被后级使用，不会被当死代码删除。允许编译器移动无副作用计算，但它不能合并掉显式asm内部FMA。
- 不修改SCC、EXEC、MODE等隐含状态；没有缺失相应clobber的问题。`abs`是源修饰，不是额外写标志。
- 同一32位常量源用两次应保持同一SGPR值；需在最终ISA看`0x3728ab28`装载和source0/source2实际寄存器，不能只看源文件字符串。
- 直接literal被MC拒绝的原日志应保留；改SGPR后不能假称已生成ACO相同literal编码。两者指令长度/SGPR压力不同，性能须实际量。

## 5. 编译与GPU门应检查什么

本候选改`wave_owned_c32.inc`的两处float激活分支（W16==1与其它布局）；`CW_FAST_NUM&4`的packed-half分支不变。若只重编`c32-wave1-fast`和`c32-wave1-fast-norm900`，FAST0模块字节不变；若最终全配方重编，必须另确认其它同源模块的实际变化范围。

ISA最小核对：

1. 首级实际为`v_fma_mix_f32`，源1F32，常量选择正确，无`mixlo`替代，无新增转换。
2. 原clamp与末乘原v还在；FP8 pack/MODE短段未改。
3. old/new `float_mode`、wave/WG、LDS/scratch一致或逐项说明；尤其不能为FMA_MIX改浮点模式。
4. 指令总数、SGPR/VGPR分配、dual-FMA/mul数和wait变化按同一导出比较；不拿M整段展开核与H循环核作静态比值。
5. mix与mix-alt两种若产出同ISA，就留一个，不能刷不同源码名找收益。

数值门应在原完整Frame输入之外包含足够暴露selector错误的边界：g=0、±4、clamp邻域、正负非half格点、不同e奇偶；显式比较首级q与最终FP8字节更容易定位。GPU短门由主队列执行，我没有执行。

通过后以刀1已收基线作900/1080实际完整Frame Gold与同批性能。资源数变好不代替ABBA；有限域首级证明不代替完整网络callees/hash核对。

## 最终候选状态

**最终MIX源审通过，可进GPU数值门；尚不等于性能采纳。**

我独立读取了更新后的`mix.patch`、`mix-alt.patch`、`identity.json`、`isa-comparison.json`和实际`.hsaco.s`：

- SGPR版确实生成`s_mov_b32 sN,0x3728ab28`，FMA_MIX同时从该sN取低/高half；g源为原VGPR全F32，abs修饰正确。
- 两个模块各64个导出，全部RSRC1/RSRC2/properties与baseline相同，浮点模式没有变化。
- MIX与MIX-ALT完整`.text`和descriptor逐字节相同，ALT没有新机器码收益，保单一MIX候选。
- 15个普通fast导出、20个norm900导出的RSRC3变化均为减16，其余位不变。LLVM23 `llvm/include/llvm/Support/AMDHSAKernelDescriptor.h:224`定义gfx12 `INST_PREF_SIZE`在bit4、宽8；`llvm/lib/Target/AMDGPU/MCTargetDesc/AMDGPUMCExpr.cpp:234–245`由代码长度/cacheline向上取整。因此这是代码缩短导致预取长度少1个cacheline，不是浮点模式或资源分配变化。
- 代表`c32_wave1_chain_w16`：普通fast code7256→7188B、982→981条；VGPR133/SGPR42/LDS4096/spill0不变，VOPD148/wait64/WMMA54不变。norm900对应6972→6904B、941→940条，资源不变。主要是编码缩短和极少指令调整，不能宣称大幅VALU减少。

最终SHA256：

| 模块 | MIX产物 |
|---|---|
| c32-wave1-fast.hsaco | `a329415246db60aca4fed4c037a4520df13603e975abe5dbac9a4b1e41fbe52c` |
| c32-wave1-fast-norm900.hsaco | `911033ae102b38297129dca2fadf019f28f6fb18a204d9b0d0bde11a96b4fead` |

第一版packed-literal仅是失败前置，不可执行/不可采纳；本报告没有调用GPU或RTC。
