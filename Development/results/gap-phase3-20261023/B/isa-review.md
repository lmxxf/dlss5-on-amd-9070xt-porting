# B正式ISA安全审：可进受控GPU门，producer编译口径已纠正

## 先纠正编译基线

实际生产producer是`a6866439…9828`。虽然历史recipe/manifest写了max-ilp，但本轮fresh加max-ilp得到`fff2a211…e602`，91个旧函数中89个代码变、65个descriptor资源字段变，不能归为cuid或重定位。

原先标作“漏row opts”的empty候选`7d9c6beb23845ed4c93f3b16f28d8553da5097b9a576653bab8e88ec0f58678d`反而保持**生产91个旧函数全部机器码/资源字段相同**，只加2个chain exports。故采用empty；max-ilp组继续隔离，不上GPU。此前实现者按recipe推荐max-ilp的建议撤回。历史究竟是哪个构建步骤未应用opts，本审不凭字段猜定。

consumer候选`b249f6b9edcfb5005ef0fde51753f445131ae3f7e03958a4efd8f8f016a2db49`对原`00e39349…095ce`保持旧89函数代码/资源全同，只加consumer chain。对应`producer-noopts-identity.json`、`consumer-identity.json`。不使用`producer-identity.json`里max-ilp baseline相互一致来冒充对生产一致。

## 主测资源

|核|旧VGPR/SGPR|新VGPR/SGPR|LDS/private|
|---|---|---|---|
|FFN producer|95/13|96/16|0/0保持|
|projection consumer|100/17|101/21|1024/0保持|

`isa-resources.json`保数值。主测两个完整module均 **0条 `MSG_RTN_GET_REALTIME`**。trace consumer为123VGPR/26SGPR，不能混入性能。

## Producer发布顺序：PASS

实际empty producer新核：

`global_store_b8 ×32` → `s_or exec`恢复全部有效/无效wave控制 → `s_wait_storecnt 0` → 两组 `s_barrier_signal/s_barrier_wait` → `(lane&31)==0`与原有效tile谓词合取 → `global_atomic_add_u64 scope:SCOPE_DEV` → `s_wait_storecnt 0 / global_inv scope:SCOPE_DEV`。

最初invalid-first分支跳到exec恢复点，**不跳过两次WG barrier**；信号点继续用valid谓词屏蔽无效第二wave。所有lane的stores先确认完成才允许wave leader发布，atomic是64位device-scope RMW。每tile8贡献的映射由独立source审计证明，GPU counter门仍需跑。

## Consumer获取/失败：PASS

- leader poll为64位`global_load_b64 scope:SCOPE_DEV`，随后`wait_loadcnt0 + global_inv DEV`，64位比较target；有限计数递减并`s_sleep1`。
- exec重新汇合后`v_readfirstlane`广播ok；读取kernel argument的scalar loads不等于读取producer数据。
- 成功分支额外`wait_loadcnt0 + global_inv DEV`之后才出现skip的`global_load_u8`、weights的`global_load_b32`和contract输入载入。没有将数据load投机移到acquire之前。
- 失败分支直接构造常数0，32次F32 store和32次byte store覆盖本WG的16×64输出，末尾leader `global_atomic_or_b32`置sticky后退出。该分支没有contract/skip/weight load。精确索引/canary由独立timeout-zero GPU probe补验。

## Consumer trace时钟依赖：PASS

trace1反汇编见实际WMMA写v25，随后`flat_store_b32(...,v25)`（volatile结果落盘）→`s_wait_storecnt_dscnt0`→`s_sendmsg_rtn_b64 MSG_RTN_GET_REALTIME`→timestamp store。因此marker确实在一个真实WMMA结果完成之后，不能仅是入核或自旋时间。

最终empty trace已从root既有产物取回（未重复编译）：producer `c71af7e966854c2cf09e788bd8f06af380ac7a76dcfc55edae2822d65755c902`、consumer `e0d0d8e4adb125d17918f49d5956a201f4157aa744aea39b7f2bdb6e875ac458`。两者旧91/89函数机器码与资源仍全同生产；producer marker在stores确认＋两WG barrier＋valid/lane0门之后、atomic64之前。producer trace96VGPR/18SGPR/LDS0/private0。所有关键安全ISA项可签，GPU实际同步/Gold/overlap门仍必须跑。

fresh empty producer baseline整模块SHA已经复现为 `a6866439fa89e770d2de82ef683b3e61e31819f4cb3618d50999b2ff14159828`。最终路径是`B/accepted-modules`与`B/accepted-trace-modules`；`B/comgr`下的max-ilp producer不是最终选用项。完整身份补充见`producer-trace-identity.json`、`consumer-trace-identity.json`与`isa-resources.json`。
