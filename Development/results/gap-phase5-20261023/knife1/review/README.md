# Phase5刀1独立预审：C512成对FP8与精确跨半wave广播

仅本地CPU/源码审查，无SSH/GPU/RTC，未修改生产。这里是实现前与微探针准备意见，不替正式ISA/Gold/整Frame结果。

## 历史查重

已检索failed-experiments、results和experiments的文档/设备源码。未找到当前活跃C512 compact三个出口成对pack＋保持原串行norm、改permlanex16广播这一具体组合的既有GPU结论。不要据此笼统说“以前没试过pack”：

- failed-experiments:1444及pack8-20260927：C32/W2 PACK8已成功采用；DF_PACK8在旧deep/ViT八处推广为900+0.06/1080+0.08ms，不收；MF两处是未调用分支，撤回没测。
- failed-experiments:620：9/16 C512 QKV wave级重写逐位但+0.15ms，改的是分块/供数及LDS组织，不等同当前固定kernel的出口pack。
- failed-experiments:2403：10/01 QKV_PIPE/norm微改没有合格整网收益；改变求和顺序的norm不做逐位候选。旧消融约3µs会改变输出/下游，不能当今天优化上界。
- failed-experiments:2469及c512-direct-pack-20261004：FFN的mix/contract directpack单项拒、组合3后来采纳。这是split FFN，不是本次attention compact的QKV/prob/AV三个scalar出口。
- norm-contract-20261006和c512-den-app-preparation-20261006已确认当前serial norm的四次跨half prefix，拒过没有依据的active-lane缩减/scale重复load猜测；没有完成本次permlanex16替换实验。

W2的w2_pack8当前本就成对转换，不要再次把它写成新工作。C512新候选先限定实际活跃c512_qkv_attention_compact_w16，不向别的族自动扩散。

## Serial norm精确性

c5c_serial_norm和w2_serial_norm都按32通道固定顺序执行四段8项f32平方/累加，并依次广播lower、upper、lower、upper prefix。两个half都执行算术，部分结果最后被丢弃；不能改成两half各reduce再相加，不能改变乘加融合语义。

候选swap opposite half同rc、再按当前half选择原值或swap值，能够实现同一prefix广播。`norm-symbolic-proof.json`对32lane四阶段逐项符号追踪全同，最终项序精确为lo_lower[0..7]→lo_upper[0..7]→hi_lower[0..7]→hi_upper[0..7]。它证明选择/累加顺序，不冒GPU intrinsic实测。

本地官方LLVM定义：BuiltinsAMDGPU.def的permlanex16为4个uint32＋两个立即bool；IntrinsicsAMDGPU.td将参数定义为old/src0/selLo/selHi/fi/bound_control，Convergent/NoMem。`(bits(v),bits(v),0x76543210,0xfedcba98,false,false)`签名正确；identity nibble选择器按rc选择opposite半wave。必须用独立GPU lane probe验证实际值，不靠函数名猜。

当前c5c norm的part<2是uniform，前面的坐标分支已合流，入口提前return也为WG-uniform；因此full EXEC前提在本scope成立。不能把这个等价结论搬到任意部分EXEC场景；old参数对inactive/边界情形的行为须保留。

## FP8、signed zero与NaN

三处语义必须分开保留：

1. **QKV NOF1**：现c5c_q8mz有contract(off)，先独立a*b，再+0，再fmed3(-448,448)，最后取FP8 byte。精确−0被+0变为+0，但负的非零微值在FP8舍入成−0应保留，不能事后清所有0x80。
2. **概率/AV NOF1**：现c5c_fp8是med3(x,-448,448)直接转，无+0。成对helper不能为了共用QKV代码给这里加+0或改NaN排序。
3. **NOF0**：保现F链，不可借NOF1证明顺手删掉它。

具体反例：a=−2^-149、b=0.5时，单独f32乘法先舍为−0，再+0得到+0；合成fma(a,b,+0)可直接舍为−0，FP8字节0x00/0x80不同。新QKV成对helper必须保contract(off)，别让跨inline/统一w2 helper悄悄恢复融合。

旧c512-av-f-20260930对全部2^32 float位型做过GPU等价proof，包含NaN/Inf/次正规；当时clamp/med3的NaN映射是当前合同的一部分。成对化最稳是每值保完全相同预处理，仅把两个scalar转换放进同一cvt_pk。cvt不自带安全饱和，不得删±448 clamp、动全局MODE.FP16_OVFL或假设所有输入有限。

新probe应包括±0、±最小f32次正规、±Inf、多种正负NaN payload、±448附近、E4M3舍入中点、负微值下溢，以及a*b underflow。预期值以旧函数实际byte为裁判，而不是C++fmin/fmax模型猜硬件NaN语义。

## 待GPU门

正式编译必须核实际cvt/ds_bpermute/permlanex16、wait和资源变化；少静态指令不等收益。旧/新norm逐lane32bit比较与三个pack出口逐byte比较先过，再全Frame。不要把本CPU符号证明或历史float穷举替代新builtin/配对输出选择位的本轮验证。
