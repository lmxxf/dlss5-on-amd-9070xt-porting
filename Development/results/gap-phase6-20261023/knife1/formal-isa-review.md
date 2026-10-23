# 正式 COMGR ISA：QK head-major

基线两模块 whole SHA 与第五阶段生产匹配：deep `68021d28…45d12c`、vit `2cfd9c5f…76d320`。候选 deep 旧 91 个、vit 旧 102 个函数的代码与资源描述均保持；只新增两 attention 和一 producer。详细证据见 `*-exports.json`、`comgr-isa.json`。

|实际路径|VGPR|LDS|private|字节|静态VALU|静态wait|
|---|---:|---:|---:|---:|---:|---:|
|V-only producer|78|16384|0|3624|251|71|
|QK producer|78|16384|0|3480|237|74|
|V-only attention|61|0|0|1956|191|17|
|QK attention|60|0|0|1876|193|15|

400/640 attention 两 export 机器码相同。VGPR 61→60 不据此宣称 occupancy 跨档；运行性能由实测判断。

QK consumer：Q entry `global_load_b128` PC 0x1D0D4；loop K/V 各一次 b128，PC 0x1D1B0/0x1D1D0。0x1D1DC wait_loadcnt1 后两条 K16 WMMA，0x1D3E0 wait_loadcnt0 后两条 AV WMMA。循环终点 0x1D3F4 返回 +0x174，Q 不在循环中。没有 byte global load。相比 V-only 的 Q 两次 b64、每 key K 两次 b64+V一次b128，逻辑请求字节相同、指令/地址组织不同；不能宣称 DRAM bytes 下降。各 token 的动态预算见 dynamic-request-budget.json，含每 loop 五条 WMMA，非静态总数冒充动态收益。

Producer 同步审：最后 norm LDS load 0x9AD2C，wait_dscnt0 0x9ADAC，之后两 norm WMMA 0x9ADC0/0x9ADE4。inactive return 分支 0x9ADF0 位于已有 WG barriers 之后。QK 首次 LDS overwrite 0x9B02C，16条store末尾0x9B1AC，随后wait_dscnt0 0x9B1B4再ds_load_2addr_b64 0x9B1B8，等待0x9B484后global_store_b128 0x9B488。前一次wave同步不一定有独立硬件opcode，但优化IR保留release/wave/acquire且实际norm读、消费全在覆盖之前。后续转置不引入WG barrier，inactive wave不参与本wave同步。

新旧数学没有变更。正式ISA安全依赖和布局加载宽度满足设计；GPU逐字数值、canary/reuse与整网性能由 root 独占执行。未修改生产。
