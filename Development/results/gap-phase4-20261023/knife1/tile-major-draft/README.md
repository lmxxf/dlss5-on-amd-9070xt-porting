# 第四阶段刀1：ViT V-fragment生产/消费成对布局

2026-10-23，隔离候选。基线仍phase2刀1；phase3的QT32/TCHAIN/调度负候选均不带入。只一个布局方案，不改wave数/数学。root唯一执行GPU/RTC，生产尚未改。

## 索引与容量

旧QKV producer part2中`acc[j][e]`对应：

- token=`first+gr*8+e`
- channel=`head*32+j*16+rc`

这恰好是attention同一个lane(`gr,rc`)在key=`first`时所需的V片段。新producer把同lane的`acc[0][0..7]`与`acc[1][0..7]`各自经原`byte_F`量化后打成16B，只写V尾部：

```
Vbase = 2*tokens*1024
address = Vbase + ((first/16)*32+head)*512 + lane*16 + j*8+e
lane = gr*16+rc
```

consumer每个key16/head/lane从同地址一次16B读回，低8B给c0、高8B给c1。旧Q/K前`2*n*1024`字节完全不动；V仍占`n*1024`，整个QKV还是`3*n*1024`，没有新buffer或额外launch。

所有lane起点16B对齐，整个V区域是双射，没有重叠/洞。独立CPU三坐标字节模式穷尽400/640并核前后canary，见[mapping-review.py](mapping-review.py)、[结果](mapping-review.json)、[源审](source-review.md)。400/640的25/40个tile恰好整除5wave组，所以这两档没有inactive producer wave；原live guard仍保留在全部WG barrier之后，不冒称已实测非整5尾。

## 新导出与路由

Producer（`vit-stream-fast`）：`vit_stream_qkv_frag_bin_w5f8_vfrag`，ABI与旧核相同：in字节别名、weights、out bytes、tokens、reuse_gate。160线程，groups=`96*((n/16+4)/5)`，400/640分别480/768组。

Consumer（`deep_fast-packed-fast`）：`vit_attention_fused_{400,640}_bytein_bout_vfrag`，ABI与旧核相同，32线程，groups=`n*2`（800/1280），没有QT32。

`NR_VIT_VFRAG=0`/未设走旧路，`1`请求新路。构造期预热producer＋400/640两个consumer，**三者齐全才可用**。资格在producer之前形成一个local bool，并用于两端；缺任一个export全部回旧布局/旧读取。旧导出保留。

保守范围：Original、FAST1、MP1、显式`DLSS5_VIT_ADAPTIVE=0`、非adaptive_active、非graph、三种processing shape（1600×960、1920×1088/1152）、n400/640、原byte-contract/bin_w5f8/bytein_bout路径。Small/FAST0/AE/其它尺寸或layout/skip/observer/dump保旧；若KS2实际可用也不切V-fragment。不会把新V数据交给旧consumer。

Run的producer新exactname已加入原160线程和groups重映射。旧Q/K归一化、softmax位映射、den half WMMA、每query AV key顺序、输出量化及reuse_gate早退均保持。析构日志`VIT_VFRAG requested/available/explicit_ae0/producers/consumers`；计数包含冷warm，不只计测量帧。

## CPU初筛（非正式COMGR）

同一候选module内旧/新导出对比：

|核|旧→新 VGPR|LDS/private|静态指令|VMEM load|
|---|---:|---|---:|---:|
|attention400/640|68→61|0/0不变|334→288|20→5|
|QKV bin_w5f8|76→78|16384/0不变|445→551|12→12|

新consumer实际发出 **1条global_load_b128替代每key的16条global_load_u8**，2条K b64与2条Q入口b64保持；新producer V分支实际1条global_store_b128。producer多出pack/branch成本，完整静态函数包含Q/K与V两个出口，不能只报consumer收益或把全函数store数当V支路动态数。没有新增WMMA。

`cpu-isa.json`保完整opcode和资源，`cpu-build.json`保编译命令。本地LLVM21只初筛；正式COMGR须以fresh基线wholeSHA复现后再审旧导出身份与实际b128。

## 产物与复现

工作根 `/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1/`：

- `{deep_fast-packed-fast,vit-stream-fast}.{baseline,candidate}.hip`，正式两模块**均empty extra opts**；[generated/](generated/)也已归档。
- 两baseline生成源码与现生产归档逐字节同，记录[build-identity.json](build-identity.json)。应fresh锁`00e39349…095ce`与`8401ed61…dd2ee`，不单凭recipe字段认定。
- `frame.exe` O2 SHA `8d2bb3291f80e142d55d8b9c61ba688f0ff77daf4038ef36928c79b93d665b99`。
- `pure.exe` O2 SHA `5971b9da4d7213a0c08463b641ce44489062bf55f0cbd580ddd805eb1dd1a62d`，复用phase2刀1pure.cpp/相同8参数受控输入协议；开关走环境，不改纯NN计时边界。
- [prepare.py](prepare.py)、[candidate.patch](candidate.patch)、[build-hosts.sh](build-hosts.sh)、[source-identity.json](source-identity.json)、[isa-check.py](isa-check.py)、[check-module.py](check-module.py)。

root后续同新host/同candidate模块以NR0/1对照，先独立producer布局逆排＋attention AV canary/reuse门，再三档Gold与各3ABBA，另刷新1088纯NN。独立pair probe由build_pure实现，不能拿它代替整Frame收益。
