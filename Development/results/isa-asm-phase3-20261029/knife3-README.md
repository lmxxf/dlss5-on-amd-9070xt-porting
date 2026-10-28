# 刀 3：serial_norm 平方缓存，STOP

原始汇编已复用平方值；C512 改前后平方乘法均为 64 条，没有 self-square FMA。显式缓存没有消除假设中的重复平方。C64 fast 全部 137 个函数、swin-persistent 全部函数的代码和资源逐项相同，未对无代码变化的版本重复跑 GPU。

C512 compact W16 从 2104 降至 2096 条，VGPR 188、SGPR 25、LDS 6144 B 不变，零 spill。差异主要是调度和 move：v_mov 23→14、dual_mov 8→16、wait_dscnt 45→44，而不是数学运算减少。具体分类、代码身份和平方计数见同目录 knife3-*.json。

独立 C512 候选七档 Gold 全部 0 bit-diff，invalid=0；四档各三轮四 slot ABBA（240 帧弃 80，960 稳态帧/arm）：

| 档位 | mean 差 µs | p99 差 µs |
|---|---:|---:|
| Original 1088 | +10.295 | -0.018 |
| Original 900 | +30.115 | +59.581 |
| Small 1080 | +13.538 | +13.602 |
| Small 900 | +1.672 | +21.553 |

全部均值退步；Original 900 三轮均值均退。STOP，生产源码和模块保持不变。C32 未发现同构重复平方，不作无依据改动。原始逐轮数据见 knife3C-statistics.json，命令、Gold 和 CSV 的证据保存在 knife3C/evidence，实验补丁仅作复现材料。
