# 第四阶段刀1独立源审：生产端V片段布局

2026-10-23，只CPU/源码审计，不GPU/RTC、不生产改动；刀2须等本刀闭环。

## 设计准入

当前真producer为`vit_stream_qkv_frag_bin_w5f8`（160线程/5wave），每wave处理16token×32通道的一head。其V part==2的`acc[j][e]`天然对应：token=`first+gr*8+e`，channel=`head*32+j*16+rc`，j0/1各8个e。

可将每lane的16个E4M3值直接作为16B片段，写到第三个V区内部的新地址：

`Vbase + (head*(tokens/16)+first/16)*512 + lane*16 + j*8+e`

其中`Vbase=2*tokens*1024`、`lane=gr*16+rc`。consumer在同`key/16,head,lane`一次读取16B，前8B作c0的V fragment、后8B作c1。前两段Q/K必须仍为原plane-major AoS，第三段容量仍tokens×1024，总allocation仍3×tokens×1024。

这与10-06 `vit-av-trload`不同：旧实验不改producer/内存布局，仅consumer用硬件transpose-load读取原AoS；完整数值过但p99退，STOP。本刀在producer一次重排，consumer普通合并b128读取，不重试旧TR builtin，也不减query/wave数。

## 必保不变量

- producer各acc值仍经过原`byte_F(acc[j][e])`，不改变FMA/归约/half/FP8/负零或饱和规则；只pack这些已选择的字节。
- Q/K的写地址、归一化和缩放逐字保持，part2才改V出口。
- live判断保持原所有WG barrier之后，不能因为V写法新增inactive wave提前return破坏5wave协作。
- 400/640分别25/40个16-token tile，恰好5/8个5wave组，**两档都没有inactive尾wave**；尾防线保留不能冒称这两档实际覆盖了非整5尾。
- consumer每query仍16token，score、exp、分母与AV的每K16顺序不动。16B载入同时产生两个V片段，但不能合并或调换两条AV累加链。
- producer/consumer新导出须在producer启动前成对决定：缺任一导出、FAST0、Small、unsupported geometry、其它attention路线（尤其_ks2）必须都回旧格式/旧函数。
- reuse_gate/AE同时传给两端；不能生产者跳过而消费者还读新格式。AE输出缓存仍由原外层策略处理，不改变其数学。
- 新producer名字要加入Run exactname，保持96×ceil(tokens/80)组、160线程；新attention维持旧n/16×32组、32线程。不能默认派发错误网格。

## 当前状态

最终按用户指定head-major已实现并重新穷尽验证，详下文；初始tile-major仅未上GPU草案，不能把旧CPU回执当最终验证。

## 实际patch源审：PASS

已重新读取最终head-major `candidate.patch`及隔离目录 `/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1/source`。源码身份在`source-review-identity.json`；CPU穷尽脚本`mapping-review.py`与结果`mapping-review.json`绑定同一patch SHA。

### producer

新`vit_stream_qkv_frag_bin_w5f8_vfrag`是旧bin_w5f8的独立导出。自动提取函数并归一函数名后，**最终store之前的整个函数代码逐字一致**：CH8 staged权重、64个K16累加、Q/K归约与scale、barrier、live判定和reuse_gate均未改。

仅最终出口分part：Q/K保原写法；V把原`byte_F(acc[j][e])`的16个字节放入零初始化的4×uint vector，`j*2+e/4`选word、`8*(e%4)`选byte，再memcpy16B。没有另做一次量化、没有把float转half、没有更改负零/溢出规则。

最终用户指定head-major真实V地址为`2*n*1024+(head*(n/16)+first/16)*512+lane*16`，与上方设计式等价。每lane store16B全对齐；最大地址严格为3*n*1024-1，没有扩大allocation。

### consumer

原`vit_attention_transposed_score_body`只新增默认false的VFragment模板位。true时一次16B memcpy到vpair，c0取word0/1，c1取word2/3；false保原AoS gathering。Q/K读取、score、bit-exp、denominator、每K16的AV顺序、最终inverse与byte_F全沿用原代码。两新export均调用同一原body，query仍16、wave仍32。

`static_assert(ByteInput)`防止把F32 QKV路线误当片段字节读取。该布局不借硬件transpose-load；不能将10-06 TR候选的结果当本轮性能。

### CPU穷尽结果

测试以每个原AoS位置的低/中/高3个坐标字节分别作为已量化payload，覆盖所有位置，避免8bit周期数据掩盖错误。穷尽生产者bid/wv/lane/j/e写入，再独立按旧consumer逻辑坐标比对新片段读取；同时核Q/K原样、每byte一次、所有16B地址对齐与前后32B canary。

| tokens | producer WG×threads | attention WG×threads | 总QKV bytes | Vbase / Vbytes |
|---:|---:|---:|---:|---:|
|400|480×160|800×32|1228800|819200 / 409600|
|640|768×160|1280×32|1966080|1310720 / 655360|

最终head-major重新执行三坐标pass全部通过，Q/K区和canary不变、全部V byte与原消费值一致。两档无inactive wave；live判断在原barrier之后保持，尚不声称其他token数实机可用。CPU测试验证字节排列，不模拟GPU量化算术。

## 成对路由与fallback审查

- 构造`InitVitVFragment`只在请求时预查/缓存producer及400/640两个consumer导出，三者齐才available；不在external wait之后首次探模块。
- `VitVFragmentEligible(n)`在producer Run前形成**本次调用的local bool**；producer与consumer都使用同一bool，不能中途只改一端。
- 实际要求Original、FAST1、MP1、指定三shape、n精确400/640、vit_stream==3、contract byte edge、实际fused FP8 fragment QKV与attention、无graph/skip/dup/observer/dump、非旧vit_byte_stream/half_stream。
- AE要求环境显式`DLSS5_VIT_ADAPTIVE=0`且`adaptive_active=false`。AE未声明或开启均回旧pair；两新核仍保原reuse_gate入口。这是本轮保守范围，不冒新增AE快路径支持。
- 若原选择会走`_ks2`，新布局拒绝；FAST0/Small/MP2/不支持尺寸及缺任一新导出也整个回旧格式。
- Run已将新producer加入精确名字规则，module vit_stream、threads160、groups96×ceil(n/80)；新attention的默认deep-fast 32线程及2n groups与旧完全一致，没有QT32残留。
- norm容量仍`New(n*768)`个float容量单位，即3*n*1024字节；权重、buffer数和输出AV格式不变。不是“少分配一半”或新量化方案。

本轮缺export测试应是合法模块移去一个新consumer或新producer，证明available=false、两端新calls均0并保持旧Gold；损坏整个模块不算安全fallback。

## GPU前后应保留的有限证据

1. 记录actual `vit_stream_qkv_frag_bin_w5f8_vfrag`和对应attention新callee，正常full71每帧两端各8次，不能仅报requested1。
2. 实际ISA必须出现目标V store/load的b128；memcpy源码不等于编译器没有scalarize。统计应区分Q/K路径与V路径，不拿含三part的全静态总数当V动态数量。
3. 若捕获中间QKV，整体SHA故意不同：Q/K直接byte0，V逆排列后byte0；完整RGB才对原Gold。
4. 400/640完整有限、重复/动输入Gold及两新导出缺失的成对回退；计时包含producer打包与consumer收益，不能只量consumer微核。
5. 没有query/wave减少，没有额外数学松动；旧TR负结果保留。最终性能由同批完整Frame裁决。

源审与CPU映射通过，不代GPU数值/性能。此审计未调用GPU/RTC，未修改生产，未启动刀2。


## 用户指定head-major修正与重新验收

初稿是tile-major `(tile*32+head)*512`；root在GPU前指出用户原式为head-major `(head*(tokens/16)+tile)*512`，两端已统一修正，**本轮只执行后者，不扫描两种布局**。旧草案从未GPU，旧CPU身份保在`tile-major-draft/`。

最终producer和consumer的实际offset逐行复核，并重新运行`mapping-review.py`全部400/640、三坐标byte pass、区间/对齐/canary检查；没有沿用旧CPU结果。`mapping-review.json`与`source-review-identity.json`已绑定最终同一patch SHA。

固定head时key每前进16，最终片段base增加512B；旧草案则增加16384B。每wave内lane仍连续16B、Q/K与总capacity均不变。这个访问顺序差异是此次修正的实质，不能因二者单次都coalesced就当同一个布局。

同步通知build_pure：中间V逆排列必须采用head-major；最终RGB Gold要求不变。此前所有“mapping PASS”若未标最终head-major都仅属于未测草案。
