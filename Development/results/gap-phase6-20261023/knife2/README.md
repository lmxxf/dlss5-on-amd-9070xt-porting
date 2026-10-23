# 第六阶段刀2：全部 STOP，生产未改

本轮最终只保留刀1已经采纳的三shape QK优化。刀2 A全开、A half-only与B均不采纳；不加shape条件，不再开变体。所有准备源码、正式模块、正确性和性能原始证据继续保留。

A全开：W16四档mean均退。纯NN虽mean −5.983µs，但p99 +35.709µs且三轮p99全退，STOP。静态转换仅208→192，绝非全核减半。

A half-only最后48槽：Small720 mean −5.874µs、pooled p99 −64.665µs；Small free1088 −7.860/−7.414µs；Small900 mean −0.459µs基本为零、p99 +22.444µs且两轮尾退，未过全范围验收，因此STOP。Original1088是未执行修改核的不变路径控制，其 −4.224µs属于对照读数，不能归为本优化收益。half-only正式码仅两个half ByteOutput核变化且等全A，其余91核（含W16/QK）等基线，见half-formal-review.json。

B：gfx1201不支持所提LDS ds_load_tr8_b64；本地assembler与正式COMGR支持探针见B-result.md及相关日志。未把gfx1250指令塞到gfx1201，也未重跑已有负账的producer LDS VT方案。

生产确认：deep_fast.hip、build-modules.ps1、hip_reference_network.h逐字等于刀1采纳后的冻结基线，见final-production-check.json。本代理未集成任何刀2源码或模块。性能结论采用root完成的正式批次；本地审计没有新增目标机操作。
