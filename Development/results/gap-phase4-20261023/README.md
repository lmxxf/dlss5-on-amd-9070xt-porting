# 第四阶段：V-fragment配对布局与Small资格

2026-10-23，两刀按顺序验证与落地。

## 刀1已采纳

head-major V片段写/读，QK不变；独立GPU逆排/AV/canary/reuse及三档Gold通过。Frame900/1088/1152分别省15.497/58.899/51.794µs，9轮均值改善；900R3 p99+4.693µs保留。1088纯NN真实旧生产→新默认同批追回36.265µs。

生产默认已开，保Original FAST1 MP1显式AE0三档，Small/AE等退旧；双架构80模块SHA/实际ELF目标核齐，gfx1200不冒真机。详 [knife1](knife1/README.md)。

## 刀2已采纳

Small900=1600×1024/448，720=1344×768/288；真实processing1088由free1920×1072输入得到，常规Small1080_ROWS1088仍实际1152。

72帧Frame Gold、24帧内部RGB32F trace、108帧public模式/AE/history兼容通过。新三shape每帧8次pack清零，活动权重容量各省120MiB，Small1080不变。三档各3轮4-slot ABBA，九轮mean/p99均改善：

|Small处理尺寸|原Frame ms|新Frame ms|节省µs|
|---|---:|---:|---:|
|1600×1024|4.350586|4.286140|64.446|
|1344×768|3.061927|3.007794|54.133|
|1920×1088（free1072输入）|5.587080|5.341975|245.105|

生产资格helper与已测候选逐字相同，最终重编Frame两个exe也逐字相同。Small不启用刀1 V-fragment，80模块沿用刀1。详 [knife2](knife2/README.md)。

## 证据与边界

全部性能为同批ABBA；刀1纯NN 8.341779→8.305514ms，不将收益跨批从第三阶段Mochi差距相减。两刀各三档Frame共18轮均值改善；刀1 900第三轮p99+4.693µs保留，其余尾部结论见各表。120MiB指活动权重分配容量，不当总VRAM峰值。

所有GPU/RTC持锁并遵守game-check、≥100GB和15秒看门狗。各目录含原CSV、源码、ISA、模块/host身份、失败及修正原始回执。gfx1201已真机验证，gfx1200仅编译/ELF核验。游戏安装与已发布ZIP未变。

最终生产默认复验：Original三档＋Small四档，7jobs/21帧Gold全部0-diff，见knife2/final-default-review.json。正式addon/runtime及80模块完整payload已构建，见[构建回执](final-build/README.md)。GPU队列结束、锁释放。
