# Phase6刀1正确性独立复核（不含性能结论）

仅本地CPU读取，无目标I/O或新增GPU。126帧七档三图样Gold（21对）逐帧finite/repeat/hash同；84帧七档AE（7对）逐帧hash及完整CSV同，实际状态均[0,1,1,1,1,1]覆盖compute/reuse，candidate QK producer/consumer成对真实route成立。详independent-correctness-review.json。

四token pair原始QKV/AV已另独立回读：288/400/448/640 Q/K按consumer坐标逆排与phase5 Vfrag基线同，V未变、AV同、canary/FP8有限及GPU reuse-gate哨兵门通过。不能把这些数值门当性能。

正式候选deep SHAba1a29db…ea354、vit SHAbf1dd632…22836，已对本地实际文件重算；old deep91、vit102个函数机器码及descriptor资源均同，仅新增2attention+1producer export。基线对应phase5正式两模块，未用旧AoS V。

对正式producer反汇编进一步核PC：norm读后的wait_dscnt0与两WMMA在首byte overwrite前；16次LDS写之后wait→ds_load_2addr_b64→wait→global_store_b128实际存在。CPU地址证明说明五wave raw slice互不重叠且落16KiB内；inactive wave在原WG barrier后return，之后只wave同步。相关源码/地址/PC证据见source-review.md、mapping-and-ownership-proof.json、producer-sync-isa-check.json。

新连续512B/16B地址仅证明请求组织，不证明缓存miss或DRAM流量倍数。消费者61→60VGPR不据此宣驻留跨档。整Frame/纯NN仍待root的独立性能批次，当前不提前签收益或采纳。
