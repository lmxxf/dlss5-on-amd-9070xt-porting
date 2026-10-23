# 第四阶段刀2：Small compact contract/projection形状资格补齐（隔离候选）

基线冻结刀1已默认启用head-major V-fragment的生产源码/80模块。候选只改`hip_reference_network.h`两Init的形状判断，提取共同`CompactVitTailGeometryEligible()`，其它设备、FAST、MP、graph、experimental、sparse、selector、kernel资格不动；**V-fragment的Small拒绝也不动**。现已通过数值/兼容/性能门，已将同一helper合入生产；root唯一GPU，最终默认Gold和addon/runtime构建由root收口。

## 三种新增Small范围

直接编译调用实际`native_network_geometry.h`核验：[geometry-check.cpp](geometry-check.cpp)、[结果](geometry-check.json)。

|输入/选择|Small valid|实际processing|Deep|ViT / token|
|---|---|---|---|---|
|900档|1600×900|1600×1024|52×32|28×16 / 448|
|720档|1280×720|1344×768|44×24|24×12 / 288|
|free输入1920×1072|1920×1072|1920×1088|60×36|32×20 / 640|
|普通1080，ROWS=1088|1920×1080|**1920×1152**|60×36|32×20 / 640|

普通Small1080即使请求1088仍由SmallProcessing按valid重新生成1152，不能把flag当实际1088。free1072需要`FREE_RES=1`且真正输入1920×1072，专用host/输入见下。

旧1920×1152两模型资格保持；旧Original1920×1088、1600×960保持。新增仅Small的1600×1024、1344×768、1920×1088。独立源审恢复旧表达式/删除helper后整个header逐字节回到baseline；84个组合只增加上述三项，见[source-review.md](source-review.md)。

## 已检查的路径

- Small逻辑ViT11..18沿用兼容role31..38文件名，loader的`block31..38-contract/projection.f32`白名单、矩阵形状与scale尾检查可匹配；模型资产仍在独立small目录，未拿Original权重冒Small。
- n448选择`vit_attention_fused_640_bytein_bout`，实际loop仍按448；n288选择400导出、实际按288。完整16tile边界合法。QKV W5最后一组各有两个inactive wave，原barrier后live-return保持；不是400/640整5wave情形。
- 900/720的DeepWidth52/44不同于W/32=50/42，因此`C512ByteStreamOk`仍false；但ViT EntryExit可独立打开。Up39会新走**byte-input、F32-output** `_w_bin[_f8w16]`，不能只引用1152的`_byteout_w_bin`验收。
- EntryExit的producer/gather/ViT/decoder出口导出存在性检查保持，所有80模块复用刀1最终版本。
- compact函数和pointer-layout配对初始化仍在构造/producer wait之前；原profile预热保留。模型各自weight map/compact指针集合机制未变。
- 公共F9在新三范围一般因模型processing几何变化重建资源（9001024↔960、7201344↔1280、free1072的Small1088↔Original1152）；同Network双模型预热的桥接特例仅1152，原本已支持。本轮没有改裸Network同geometry手动SetModelMode的构造资格缓存，不宣所有非公开热态已验；实际public switch由root验证。

**不预称数学必同。** 当前W16 projection先把skip放total，再加零起点各K分段；旧非W16在第一段acc里含skip，FP32结合顺序不同。1152历史Gold不能代替Small448/288的新实际Gold。先跑实际逐值门，未过就不能用性能结果宣无损。

## 初始CPU候选交付（后续生产落地见末节）

工作根 `/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/`：

- `frame-baseline-standard.exe` SHA `7a15bc6542f9a12fbf5ea26feee94f5277a98faa9790d5d895b0f02f9a9e77f4`，与刀1final-frame整文件相同。
- `frame-candidate-standard.exe` SHA `39c5e035835df6d5516b0d634b2127b4533249e857d40c58e77186040617994e`。
- `frame-baseline-free1072.exe` SHA `e7fae90f76af4ef0d590028deb87ba484151ba38e27fbe0b189c6a3659bad00b`。
- `frame-candidate-free1072.exe` SHA `7ea7f8c37c858bcd0dd04809ce99195aa4a0acbeca853acabb152ee0bb090f10`。

standard沿用1296×720真实HDR fixture与原命令参数；free1072编译`BENCH_WIDTH=1920,BENCH_HEIGHT=1072`，其它O2配方相同。两臂均用刀1`final-payload/HIP/gfx1201`，清单SHA `3b6471c2…016e1`，没有新kernel编译。

`free1072-input.f16`为受控RGBA16F梯度，16465920B，SHA `d4a7b07b4de53a79589b73fec3d8131c06baf53b460341805ad1482e102ddda4`，**不是实拍**。生成脚本[make-free1072-input.py](make-free1072-input.py)。该尺寸Gold不冒普通1080测量；主要ABBA为Small900/720，Small1080作为不变对照Gold。

[candidate.patch](candidate.patch)、[prepare.py](prepare.py)、[build-hosts.sh](build-hosts.sh)、[source-identity.json](source-identity.json)、[build-identity.json](build-identity.json)保来源。root的public model switch/AE/MODE1兼容由build_pure编排；standard letterbox不满足MODE1全viewport，MODE1应使用free1072真正full viewport，DIRECT_TEST需DIRECT_IO=3。当前尚未有本候选GPU数值/性能结果。

## 独立三帧路径/RAW trace已准备

已另建baseline/candidate trace host，覆盖Small900/720/free1088/1080，真实callee按launch句柄解析，不设置observer；hard-check实际Small31及token数，逐帧记录compact/pack/Up39/weights/pool与V-fragment增量。候选可直接与baseline三份RGB32F逐位核对。见[trace-readme.md](trace-readme.md)。四measurement host及80模块逐SHA未改；尚无本工具GPU结果。

## 实机结果：采纳

四profile三图样72帧Frame Gold同；bare Small真实Fn trace24帧RGB32F也逐位同。900/720实际tokens448/288、attention640/400；各95→87派发，8pack→0。free1088实际640tokens，91→83；Small1080原83保持。Up39实际命中byte-input/F32-output路线。新三档活动weights容量各减125829120B=120MiB，pool容量未由此宣称变小，不当总VRAM驻留峰值。

public兼容14jobs/108帧通过：三档Small→1→2→3→Small真实geometry重建、fresh DirectOutput、回环；AE真实复用CSV一致；MODE1free1072静态fallback路径使用真实历史。首冷帧864/831ms超过原250ms idle规则，双方第二帧再次reset，实际use_history为0,0,1,0,1,1；与显式frame3reset及逐帧SHA一致，没改生产策略、没刷GPU数据。

完整Frame每档3轮4-slot ABBA，240弃80、960稳态样本/arm：

|Small档|baseline ms|candidate ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900 / 1600×1024|4.350586|4.286140|−0.064446|−0.068212|
|720 / 1344×768|3.061927|3.007794|−0.054133|−0.137213|
|free1072 / 1920×1088|5.587080|5.341975|−0.245105|−0.229516|

九轮mean/p99全改善，采纳集中geometry helper与两个Init调用。GPU模块完全复用刀1，不把V-fragment扩到Small。原始CSV、trace/真实callee/活跃容量、所有修正与独立复核在本目录。

初始化记录边界：只有默认Small1080且无auxiliary row会预热Original，因此VFRAG析构计数有8不代表Small启用；free1088没有这次预热。初始两个runner门误判已按真实模型日志修正，原baseline子进程均exit0，free baseline经CPU补验，不重跑GPU凑数。MODE1最初过严序列断言亦保留并按原源码规则修正；原数据未变。

## 刀2通过后正式合入生产

root与独立复核确认：72帧Frame Gold、24帧bare trace F32、108帧public兼容均成对保持；三种新增geometry各3轮ABBA，9轮mean/p99全部改善：

|processing|baseline ms/frame|candidate ms/frame|差值|
|---|---:|---:|---:|
|1600×1024 Small900|4.350585729|4.286139896|−64.446µs|
|1344×768 Small720|3.061927396|3.007793958|−54.133µs|
|1920×1088 Small free1072|5.587079792|5.341975000|−245.105µs|

三个新增shape各减少**120MiB active weight分配容量**，不是整进程驻留峰值。trace实际900/720由95→87派发、free1088由91→83，8次vit_pack_input→0，Up39真实`_w_bin_f8w16`；1152保持。MODE1初冷超过250ms时按既有policy两边seq001011，已核合法，没有改policy掩盖。

已只将helper及两处Init条件合入`Development/HIP/hip_reference_network.h`，生产整头SHA **`09cb03d5343a78abffa2add3ee7e00545689047ea8dd3f95872465513172d734`**，与已测candidate整文件相同。V-fragment的Small/AE资格未扩，80模块和清单`3b6471c2…016e1`原样。

最终O2产物在`/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/final-host/`：

- `final-frame-standard.exe` SHA `39c5e035835df6d5516b0d634b2127b4533249e857d40c58e77186040617994e`
- `final-frame-free1072.exe` SHA `7ea7f8c37c858bcd0dd04809ce99195aa4a0acbeca853acabb152ee0bb090f10`
- `final-pure.exe` SHA `1121e2b3671fbe2a69a0ec1056fa57ed5edcfb46dbc62f3718f046086016173f`，仍使用强制Original1x的pure协议。

**两个最终Frame exe也与已测候选整文件逐字节同**，不仅源码相同。[production-identity.json](production-identity.json)、[production.patch](production.patch)、[build-final-hosts.sh](build-final-hosts.sh)保完整回执。root继续最终默认Small4shape/Original3shape Gold；builder按冻结源构建addon/runtime。本代理没有运行GPU、改游戏或发行包。
