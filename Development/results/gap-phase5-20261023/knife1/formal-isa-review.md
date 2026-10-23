# C512 paired packing / serial norm 正式 ISA 审计

正式 COMGR 空 RTC_EXTRA_OPTS 基线 whole SHA `9afa918bc6252ab1beee5bcd15ed6ab838c612cc8368d814b48e28b67a7079b4` 与生产一致。下表是 `c512_qkv_attention_compact_w16` 静态指令数量，不是动态执行时间。

|版本|总指令|VALU|wait|FP8 cvt|ds_bpermute|permlanex16|dscnt|
|---|---:|---:|---:|---:|---:|---:|---:|
|baseline|2483|1367|136|144|16|0|45|
|pair|2104|1025|137|72|16|0|45|
|perm|2542|1438|122|144|0|16|31|
|combined|2175|1084|121|72|0|16|31|

四者 VGPR 188、LDS 6144、private segment 0。Pair 将 `v_perm_b32` 和 `v_or3_b32` 分别从 52 降至 16；剩余字节装配包含 V 片段，不能称完全消除。正式 perm opcode 是 `v_permlanex16_b32`，没有 `_var`。降低 LDS 等待数量同时增加 VALU，最终取舍依赖实测。

三候选均保留全部 113 exports；仅 compact 和 compact_w16 两函数代码改变，其余 111 个代码及资源字段不变（见各 `*-exports-identity.json`）。目标的 RSRC1/RSRC2、segment/kernarg 均保持；RSRC3 的 INST_PREF_SIZE 随代码长度变化，不能声称描述符逐字不变（见 resource-differences.json）。

Pair 的 QKV 路径保留 `contract(off)` 下独立乘法、`+0.f`、med3，然后成对转换；其他概率/AV 路径不添加 +0，原 F 操作保留。转换低/高 half 使用 false/true；不清除 FP8 下溢产生的负零。只把两次重复 scalar FP8 conversion 改为一次 pair conversion。

未采纳的 perm 使用六参 builtin `(old, src0, selLo, selHi, fi, bc)`，selector `0x76543210/0xfedcba98` 交换相邻半 wave，再选择原值/交换值以保留 lower→upper→lower→upper 的四段串行前缀；并未改成树形求和。性能拆分后它不进入生产。W2 现有 pack8 已经成对转换；W2 serial norm 可独立研究，但本刀未改、未测。

生产最终仅 pair：`production.patch`、`final-recipe.json`。正式 generated SHA `6ba9ab981282f7b1f0c51d080210c4457144d4ced9b5fd2cce8950c3677569ab`，与 root 已启动的 preview 输入逐字相同；gfx1201 已核实整段 .text/.rodata/.note 与实测 pair 逐字相同，113 个函数代码与资源均同；whole SHA 差异仅符号/字符串/hash section。gfx1200 只作编译/ELF核验。完整 80 模块包逐 SHA 通过，生产清单仅更新两项、其余 78 项保持（final-payload-identity.json）。性能与 Gold 以主 README/统计 JSON 为准，分轮 p99 反例保留。
