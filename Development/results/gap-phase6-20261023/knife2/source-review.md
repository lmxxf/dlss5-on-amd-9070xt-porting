# 第六阶段刀2：独立 A / B

A 只替换 `decoder_project2x_h16w_wide_body` 的 ByteOutput 打包循环。每个 q 对应 r[4q..4q+3]，第一次 cvt 写低16位，第二次带旧值且 op_sel=true 写高16位，依次仍为四个原字节。r[] 之前的 total/skip/H/F/fp8_add0 表达式、边界检查、base、16B memcpy 均原样；无新 clamp、重排求和或缩窄中间浮点。16值由16次单元素转换变为8次成对转换，是否实际减 ISA 和整网收益仍由正式模块/Gold/ABBA验收。其他模板 ByteOutput=false 没改。基线包含已采纳 C512 pair；A 没改该源码及配方。generated 包括所有引用 deep_fast.hip 的7个配方，各自 baseline/A，收益不得与刀1混记。

B：本地 LLVM23 builtin 定义要求 gfx1250-insts/wavefrontsize32，DS selector 同样限定 isGFX1250Plus。完全相同 ds_load_tr8_b64 汇编在 gfx1201 拒绝（instruction not supported on this GPU），gfx1250 成功，见 tr8-support.json。此为 LDS TR；gfx1201 已验证的 global_load_tr_b64 是另一条指令。提供 tr8-support.hip 供正式 COMGR 仅编译，绝不将 gfx1250 代码放到 gfx1201 执行。该探针只检验支持，不作 lane 映射猜测或正确性证明；不支持则没有可运行 lane probe，也没有 B 性能候选。

历史负账：c512-compact-vt-20260930 在 producer 把 V 从 [token][channel] 散写为 [channel][token]，consumer 普通 b64；少64条 ds_load_u8，但增加32条 ds_store_b8。19组数值SAME，900平均基本零且轮间正负混杂，不采纳。该宏仍默认0，本轮未复跑。vit-av-trload-20261006 则是global TR读取旧自然AoS，数值过但p99退，亦非本轮LDS指令支持证据。没有新的producer写端机制时，不把旧VT重命名为新候选。

准备阶段仅本地CPU。生产源、宿主和模块均未更改。实际身份与7配方SHA见identity.json；A.patch仅一处打包语句。

最终提交的 scope 为 A/scoped-source：HIP_UP_WIDE_BYTE_PAIR 默认0，只有 deep_fast-packed-fast 配方显式1。其余6个引用同源的配方保持原打包分支。原 A/source 和 generated 是未限定配方草案，保留用于同码审计，不作为全7模块上线范围。正式材料为 A-scoped.patch、A-scoped-identity.json、generated-scoped。16→8 仅指一个16值 ByteOutput出口，不表示核内全部208条转换减半。

QK刀1采纳后已重新隔离：本轮正式 COMGR 输入改为 generated-qk-rebased，身份见 A-qk-rebased-identity.json，补丁 A-qk-rebased.patch。baseline/candidate 都保留400/640两条 qkfrag exports及其原实现；仅打包宏和指定配方开关不同。宿主复用限定三shape QK正式版本。旧 generated / generated-scoped 是历史准备证据，不得替换本轮模块。B正式COMGR拒绝见 B-result.md，B已排除。

最终一次收窄候选（A-half-only）：根据全A实测，W16四档mean退，而F16权重三档有所改善，因此宏开启时仅 `if constexpr(!W8)` 使用pair；W8（含W16）仍原scalar。无运行时分辨率条件。正式生成输入是 generated-qk-rebased/deep_fast-packed-fast.half-only.hip，SHA4540899a…bd6cc。待正式COMGR应验证仅两个half权重ByteOutput核变化且代码等全A，W16及其余91个核等基线；尚未把这个预期写成已验证。生产未改，不再准备其它变体。
