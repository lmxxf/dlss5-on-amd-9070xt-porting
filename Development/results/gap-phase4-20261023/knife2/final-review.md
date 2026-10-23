# 第四阶段刀2独立终审：支持采纳Small compact资格扩展

本地独立复算36性能槽，每槽240帧丢前80，每档每arm960样本；并重新核72帧有效成对Gold、24帧真实路由trace和108帧public兼容。未目标机操作/GPU重测。详 `final-review.json`。

|实际Small处理档|tokens|baseline→candidate ms|均值差|pooled p99差|
|---|---:|---:|---:|---:|
|1344×768（720 tier）|288|3.061927396→3.007793958|**−54.133 µs**|−137.213 µs|
|1600×1024（900 tier）|448|4.350585729→4.286139896|**−64.446 µs**|−68.212 µs|
|1920×1088（free输入1072）|640|5.587079792→5.341975000|**−245.105 µs**|−229.516 µs|

九轮均值与p99全部改善，逐轮/合并数字与statistics吻合。以上是Small完整Frame成本，不混Original1088或纯NN口径。

## 数值与真实执行

按gold-corrected清单24个有效任务计72帧，包含Small原1080参考和三个新增shape、三图样、双arm；各帧finite/repeat，配对hash一致。首个额外baseline历史任务不重复计入。

独立重读八个trace的三帧完整派发序号与实际profile，无UNMAPPED，真实Small31块、288/448/640 tokens正确；候选并非回落1x。

- Small900/720各 **95→87** 派发，vit_pack_input **8→0**。
- Smallfree1088 **91→83**，pack **8→0**。
- 原Small1080 **83→83**、pack0，保持旧已支持路径。
- 新档compact Contract/Projection和W16/entry-exit实际生效；900/720的Up39 F32-output/byte-input特殊路径由真实callee记录覆盖。
- 三新增档权重分配容量各省**125829120B=120MiB**；它是对象容量，不冒显存驻留峰值。
- Small trace每帧VFRAG producer/consumer增量都0，本刀未偷偷启用Small V-fragment。

24trace帧与对应基线raw同；大Frame/trace raw以已生成hash回执核对，未重复下载。

## public兼容

再次本地执行已校正的compat-analyze：7对/14任务/108帧全部通过。三档模式0.5→1→2→3→0.5是真正Frame几何资源重建，DIRECT输出重取、SuppressFps、每帧跨arm与末回Small一致；三档AE1实际reuse与decision CSV配对一致；MODE1free1072真实Small head/blend与暖历史通过。

MODE1实际[0,0,1,0,1,1]保留：首冷帧864/831ms触发已有250ms idle规则，第二帧额外冷，2/4/5暖且双方完全同。没有为凑理想序列重跑GPU；原断言说明与具体接受条件独立存档。MODE1是static guide fallback，不宣真实MV/depth重投影。

## 签审边界

候选收益、数值和public兼容支持采纳。集中geometry helper不改变设备数学或现有VFRAG资格，生产最终应与已测candidate头字节一致；正式产物构建/最终默认Gold另锁身份。没有新HSACO、Small架构变化或第三刀。本审不宣所有动态输入的全域等价，也不扩大gfx1200真机证据。
