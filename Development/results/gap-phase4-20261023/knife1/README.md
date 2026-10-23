# 第四阶段刀1：ViT V-fragment生产/消费成对布局

2026-10-23。head-major候选已通过数值与三档性能门，现已合入生产源码并在已验范围默认开启。phase3的QT32/TCHAIN/调度负候选均不带入；Small与AE资格没有扩大。root唯一执行GPU/RTC，最终双架构清单与默认兼容回执继续由root收口。

## 索引与容量

旧QKV producer part2中`acc[j][e]`对应：

- token=`first+gr*8+e`
- channel=`head*32+j*16+rc`

这恰好是attention同一个lane(`gr,rc`)在key=`first`时所需的V片段。新producer把同lane的`acc[0][0..7]`与`acc[1][0..7]`各自经原`byte_F`量化后打成16B，只写V尾部：

```
Vbase = 2*tokens*1024
address = Vbase + (head*(tokens/16)+first/16)*512 + lane*16 + j*8+e
lane = gr*16+rc
```

最终布局是 **head-major**：同一个head的key16 tile连续按512B排列。consumer每个key16/head/lane从同地址一次16B读回，固定head的下一key只前进512B，低8B给c0、高8B给c1。旧Q/K前`2*n*1024`字节完全不动；V仍占`n*1024`，整个QKV还是`3*n*1024`，没有新buffer或额外launch。

所有lane起点16B对齐，整个V区域是双射，没有重叠/洞。独立CPU三坐标字节模式穷尽400/640并核前后canary，见[mapping-review.py](mapping-review.py)、[结果](mapping-review.json)、[源审](source-review.md)。400/640的25/40个tile恰好整除5wave组，所以这两档没有inactive producer wave；原live guard仍保留在全部WG barrier之后，不冒称已实测非整5尾。

## 新导出与路由

Producer（`vit-stream-fast`）：`vit_stream_qkv_frag_bin_w5f8_vfrag`，ABI与旧核相同：in字节别名、weights、out bytes、tokens、reuse_gate。160线程，groups=`96*((n/16+4)/5)`，400/640分别480/768组。

Consumer（`deep_fast-packed-fast`）：`vit_attention_fused_{400,640}_bytein_bout_vfrag`，ABI与旧核相同，32线程，groups=`n*2`（800/1280），没有QT32。

实验候选原用`NR_VIT_VFRAG=1`进入；最终生产优先级为**非空NR诊断值 > 非空`DLSS5_HIP_VIT_VFRAG` > 内置1**。值`0`禁用、`1`请求启用；其它非空值保守禁用，空值回下一层。构造期预热producer＋400/640两个consumer，**三者齐全才可用**。资格在producer之前形成一个local bool，并用于两端；缺任一个export全部回旧布局/旧读取。旧导出保留。

保守范围：Original、FAST1、MP1、显式`DLSS5_VIT_ADAPTIVE=0`、非adaptive_active、非graph、三种processing shape（1600×960、1920×1088/1152）、n400/640、原byte-contract/bin_w5f8/bytein_bout路径。Small/FAST0/AE/其它尺寸或layout/skip/observer/dump保旧；若KS2实际可用也不切V-fragment。不会把新V数据交给旧consumer。

Run的producer新exactname已加入原160线程和groups重映射。旧Q/K归一化、softmax位映射、den half WMMA、每query AV key顺序、输出量化及reuse_gate早退均保持。析构日志`VIT_VFRAG requested/available/explicit_ae0/producers/consumers`；计数包含冷warm，不只计测量帧。

## 最终head-major CPU初筛（非正式COMGR）

同一候选module内旧/新导出对比：

|核|旧→新 VGPR|LDS/private|静态指令|VMEM load|
|---|---:|---|---:|---:|
|attention400/640|68→61|0/0不变|334→290|20→5|
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

## 上GPU前定版为用户指定head-major

最初实现误用了tile-major外层顺序；该草案已完成CPU/COMGR构建但**未执行GPU**，不列性能失败或另一个实测候选。root指出后，统一改为用户原式`(head*(tokens/16)+tile)*512+lane*16`。旧草案patch/源码身份/CPU表存`tile-major-draft/`，不测第二布局。

只变producer/consumer地址；Q/K、数学、容量、ABI、导出名、host预热/路由均不变。两个candidate HIP必须一起重新COMGR编译，旧draft新export名字相同，严禁只换一边。frame/pure header与二进制身份不变；独立pair probe的逆排已同步为head-major并重编新SHA。最终CPU mapping已绑定新源重新验，400/640三种坐标字节模式、Q/K保持、完整容量与canary全PASS；没有复用旧receipt。

最终head-major生成源码SHA：deep `6790d6975340dd40ceabf44dfc62e898aaa1e46f46e4ce27fa391433172f0bd3`，vit `1d9bc17635047b3d2fb0c617d8726c97dee156658e9bd70ec2279745f1cba116`。Host header确认逐字节未变，frame/pure无需重编；独立pair probe最终SHA `0c45ea5a545239577791b7cb6983a65807b823f4b7f56731f1c3fb31ce85956e`。两baseline生成源码和原归档仍同，不重新编译baseline。

## 最终head-major正式COMGR签审：PASS

fresh baseline整模块分别复现`00e39349…095ce`、`8401ed61…dd2ee`。最终候选：

- deep `68021d286fbfb09d24b38259f9b0ac8c177dec50f837f062047a67d0ce45d12c`：旧89个函数机器码/资源全同，仅新增400/640两个consumer。
- vit `2cfd9c5f4fb050a9bbbcdd861c2518454444f550701d08f57550c6198e76d320`：旧101个函数机器码/资源全同，仅新增producer。

两模块都来自`comgr-headmajor/`，不引用未测tile-major草案的ISA。正式consumer旧68VGPR/12SGPR，新61/16，LDS/private均0；producer旧76/22，新78/22，LDS16384/private0保持。

按正式back-edge识别的每key16循环体：

|每key16迭代|旧|head-major新|
|---|---:|---:|
|指令bundle|123|91|
|VALU（排除WMMA）|84|62|
|WMMA|5|5|
|K载入|2×b64|2×b64|
|V载入|16×u8|1×b128|
|wait_alu / wait_loadcnt|3 / 5|5 / 3|

新loop没有u8 load。400每层800wave×25key、640每层1280wave×40key，wave/grid与数学工作不变；逻辑V读取字节也没减，减少的是加载指令和组装工作、改变访问连续性。producer实际V出口是单条b128 store，但其packing/branch仍有成本；不能由consumer指令下降直接承诺Frame加速。

证据：[deep-headmajor-identity.json](deep-headmajor-identity.json)、[vit-headmajor-identity.json](vit-headmajor-identity.json)、[comgr-isa.json](comgr-isa.json)、[formal-loop.json](formal-loop.json)、[producer V写入片段](producer-vstore-excerpt.s.txt)。已向root签可执行pair-probe→三档Gold/ABBA→1088纯NN。AE仍保守退旧，尚未因reuse_gate存在就宣称支持AE。

## 性能过门后生产默认落地

root回报：pair producer逆排/attention AV/reuse gate/canary通过，54帧完整Gold通过；三档各3轮ABBA均值9/9改善，pooled p99均改善。Frame差值：900 **−0.015497ms**，1088 **−0.058899ms**，1152 **−0.051794ms**。900第三轮p99 **+4.693µs**原样保留，不称每轮尾延迟都好。上述为已测候选数据；不拿它扣1088纯NN差距。

生产三文件：`hip/deep_fast.hip`、`hip/vit_stream.inc`、`Development/HIP/hip_reference_network.h`。GPU两源与已测head-major生成文本逐字节同；只host默认值由实验opt-in变为内置1，并增加`DLSS5_HIP_VIT_VFRAG`关闭入口。配对资格完全保持，**AE为1/auto/未明确设0均退旧；Small/FAST0/MP2/3/graph也退旧**。开关在Network构造读取，修改后需重建实例/重启，不宣动态热切支持。

原实验host保留；生产默认新O2 host：

- `final-frame.exe` SHA `7a15bc6542f9a12fbf5ea26feee94f5277a98faa9790d5d895b0f02f9a9e77f4`
- `final-pure.exe` SHA `a686b0131d0c10c7528db113a5b06f3b875ea6081b245c81e651b3783a7d3faf`

[production.patch](production.patch)、[production-identity.json](production-identity.json)、[build-final-hosts.sh](build-final-hosts.sh)保落地回执。`production-modules/`两份HIP供gfx1200/gfx1201同源编译，文本与已测gfx1201源一致；gfx1200由root持锁COMGR构建/核ELF，不宣真机性能。`hip/SHA256SUMS`已按root确认的四项更新，其余76行保持。本轮没有部署游戏、修改玩家配置或发行ZIP。

## 双架构完整80模块装配完成

本地最终目录：`/home/lmxxf/work/dlss5-gap-phase4-20261023/knife1/final-payload/HIP/`，gfx1200/gfx1201各40个HSACO。仅替换两架构的`deep_fast-packed-fast`和`vit-stream-fast`共4项，其余76个逐SHA保持10-17最终载荷。生产`hip/SHA256SUMS`仅对应4行改变；最终目录总清单与生产清单逐字节相同，架构子目录各有40项清单。

80/80文件内容SHA和实际ELF target再次核验通过，gfx1200 flags=0x48、gfx1201=0x4e（按LLVM ELF枚举与llvm-readobj核）；**gfx1200仅编译/结构核验，没有真机Gold/性能声明**。完整回执：[final-payload-identity.json](final-payload-identity.json)，装配脚本[assemble-payload.py](assemble-payload.py)，改前清单[sha256sums-before-vfrag.txt](sha256sums-before-vfrag.txt)。没有访问目标机、没有替换现装游戏或发行包。

## 实测与生产结题：采纳

独立GPU producer→consumer probe在400/640逐字节确认QK原样、head-major V逆排0diff、AV0diff、全部canary与reuse gate保持；已下载原raw再用独立CPU脚本复核。第一份probe只认物理.f32，实际权重由生产ReadWeights从逻辑.f32回落.f16，首次在CPU读取阶段拒绝；新loader probe保旧证据并使用同一生产解码，未改矩阵/布局/数学。tile-major先导未上GPU，只有用户指定head-major进入本轮。

三档三图样54帧Frame Gold同。3轮4-slot ABBA/档，240弃80/960稳态样本每arm：

|档位|baseline ms|V-fragment ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900|6.457943|6.442446|−0.015497|−0.050000|
|1088|8.853382|8.794483|−0.058899|−0.061764|
|1152|9.153676|9.101882|−0.051794|−0.071997|

九轮均值全改善，三档pooled p99改善；900 R3 p99+4.693µs保留，不称每轮尾全胜。容量保持是QKV/AV与工作张量层面的事实，不冒整进程驻留峰值测量。

生产默认/公开禁用/NR优先级、两端不对称缺导出、FAST0/MP2/AE1回退共13case×3帧通过（39帧）；未扩大Small或AE资格。默认开关DLSS5_HIP_VIT_VFRAG=1，0关闭，构造期读取需重启；NR仅诊断优先。当前新路范围Original/FAST1/MP1/显式AE0/三档，AE仍旧路。生产GPU源码与已测head-major完全同，final-frame/final-pure只改默认路由。

另用真实旧生产host+旧模块对新默认host+新模块，1088/640-token纯NN三轮ABBA（80warm+160timed，每arm960）：**8.341779→8.305514ms，−0.036265ms（36.265µs）**；各轮−33.755/−39.896/−35.145µs，pooled p99 8.503923→8.458402ms；各自首尾同153ac…47f1。此为纯NN GPU口径，与上表Frame不相加，也不直接扣上批M差当新实测剩差。统计pure-statistics.json。

生产三文件已合入；双架构80模块已装配，hip/SHA256SUMS只更新4项，其余76不变，80项SHA和实际ELF目标全核。gfx1200仅编译/结构检查，无真机声明。回执production-identity.json、final-modules4.json、final-payload-identity.json，完整模块在工作根knife1/final-payload/HIP。

已装游戏/配置/发行ZIP未改。所有GPU/RTC持锁守门，数据/独立读审保留。刀1闭环后开始Small资格刀2。
