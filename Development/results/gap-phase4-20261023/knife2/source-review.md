# 第四阶段刀2独立源审：Small compact资格

2026-10-23。只本地CPU/源码，不目标机工具、不GPU/RTC、不生产写入。本刀只扩大Small compact/W16/byte路径资格，**不扩大V-fragment到Small**。

## 1. 实际几何：请求1088不等于执行1088

`geometry-review.cpp`直接包含当前`native_network_geometry.h`执行真实`NativeResolveNetworkGeometry`，本地g++编译运行，输出保存在`geometry-review.log`：

| 输入/模式 | 有效尺寸 | processing | ViT |
|---|---|---|---|
| Small900 tier |1600×900|1600×1024|28×16=448|
| Small720 tier |1280×720|1344×768|24×12=288|
| Small1080 tier，1080_ROWS=1088 |1920×1080|**1920×1152**|32×20=640|
| Small free输入1920×1072 |1920×1072|**1920×1088**|32×20=640|

原因是Small tier从Original base只拿valid_width/height，再重新SmallProcessing；原base的processing1088被丢掉。Small(1920,1080)的y alignment为128而得到1152；Small(1920,1072)的y alignment为64而得到1088。free1072是合法正常入口，无需强塞FREE_PAD。

所以第三档验收必须用真实free输入1920×1072，并在日志核processing1088/token640；只设置ROWS1088却输入常规1080会测到旧1152资格，不能计新增范围通过。

## 2. 角色名与scale尾

实际Small manifest（`/home/lmxxf/work/dlss5-small-build/small/model-manifest.json`）逻辑11..18映射角色31..38。运行时仍用`Block(31..38,"contract/projection")`载荷名字，变化的是assets根目录到small/；不是把CompactKnown改成block11..18。

CPU实际检查16个Small contract/projection文件的元素数、物理f32/f16类型和1024个scale尾，全部形状正确、尾有限，指纹见`small-assets-review.json`。ReadWeights优先f32，不存在则将f16扩宽为float；CompactKnown接收规范.f32逻辑名，因此不受磁盘f16后缀影响。

- Contract矩阵4096×1024、尾1024 F32；compact矩阵4194304B，尾从float index4194304移到1048576，最终4198400B。
- Projection矩阵1024×1024、尾1024 F32；compact矩阵1048576B，尾从float index1048576移到262144，最终1052672B。
- 这些地址与token数无关。已存在known name/shape、selector、sparse、paired-export及legacy-cache保护不能删。
- 两模型weight map分离，compact pointer tag集合覆盖两套常驻allocation；不能为了热切清掉tag却保留compact pointer。

只证明元素数/尾范围，不冒全矩阵精度检查；运行时原ExactWeightFp8仍负责拒绝非E4M3矩阵，实际Gold负责网络数值。

## 3. 288/448不是把attention硬填到400/640

现有attention导出名400/640只是模板上限/选择；生产transposed-score body按传入的实际tokens循环K16，并按实际tokens做Q/K/V平面基址。Small448选择640导出但仍只读0..447，Small288选择400导出但仍只读0..287。

`tail-mapping-review.json`的CPU枚举：

| 实际n | bin_w5f8 WG×160 | 全QKV无效尾wave | attention WG×32 | key16步 |
|---:|---:|---:|---:|---:|
|288|384|192|576|18|
|448|576|192|896|28|
|640|768|0|1280|40|

288/448每个head/part最后5wave组各有2个inactive wave，这次与刀1的400/640不同，必须真覆盖。当前producer inactive wave仍参加所有staging/归约barrier，最后才`if(!live)return`；不得把return提到协作barrier前。穷尽确认全部实际QKV输出byte恰写一次，Gather的16-token内置换不越界；contract/projection的n个32线程WG覆盖完整n×1024。

VF生产默认仍有`small_active`拒绝：即使Small free1088正好n640、Original三shape之一，也必须VFRAG producers/consumers=0，不能借这刀启用新V布局。

## 4. byte入口/出口与Up39不是处处同一条路

Small900 deep52×32、Small720 deep44×24；宽度不同于W/32（50/42），所以当前`C512ByteStreamOk`会拒其C512跨块byte流。这不阻止独立ViT entry/exit byte：

- Small head对900将26×16补成28×16，对720将22×12补成24×12，原Down的有效区域与零填充保持。
- compact bstream/bout两端齐备后可直接head out8→Gather byte→ViT→inverse Gather byte。
- Up39于是可能走 **byte-input但F32-output** 的`decoder_project2x_h16w_w_bin[_f8w16]`，不是1152常见的byteout路径。缺相应导出时entry/exit应整体回旧，不能把字节输入交F32 reader。
- Small free1088 deep60×36，与W/32×H/32=60×34也不同，同样需核C512byte资格与真实Up39路由，不能因width1920就套Original1088。

必须通过实际callee/Gold检查这些分支，不能仅报“compact flag已开”。

## 5. F9、AE、MODE1边界

公共NativeGameFrame在model/geometry变化时先Flush，再重建resource/network。三个新增Small scope切Original通常都会改变尺寸：9001024↔960，7201344↔1280，free1072 Small1088↔Original1152。应测这些真实F9重建回环，而非假造同geometry热切。

两Init仍是constructor-only；原始Network手动`SetModelMode`同geometry时不会重新算资格，属于既有scope冻结行为。本刀不扩大该低层入口保证。1152 Small↔Original同尺寸是既有支持，保持原双模型预热与head/blend reset，不另改。

AE开启时compact权重/W16仍可能生效，但`VitEntryExitByteOk`及interbyte会因AE拒绝，使用F32跨块入口/出口；因此AE短Gold不能由AE0性能测试代替。固定idle reset间隔，比较实际decision/output，避免宿主停顿改变AE而误报数学差异。

MODE1使用Small自己的history head/blend0x3A23；本刀不改变post/history参数。应有连续history/reset和F9回Small的短Gold，确认不同输入下W16 projection结果保持。`opt.experimental_temporal`仍旧拒绝，不与Native MODE1混为同一开关。

Projection W16与非W16的skip加入位置在源码上不同（旧审计已指出）；不能用640历史样本证明所有Small288/448都必然逐位相同。本轮必须先实际Gold，失败就定位/停止，不把结构意图当数值证明。

## 当前状态

实际几何、资产角色/尾、动态token尾与byte边界已独立审查。等待最终集中geometry helper patch核对，不能将上述准入说明冒最终候选已过GPU。

## 最终候选helper源审：PASS，可进Gold

实际patch为集中`CompactVitTailGeometryEligible()`，两Init仅替换原shape表达式。隔离header SHA `09cb03d5343a78abffa2add3ee7e00545689047ea8dd3f95872465513172d734`，baseline `a0a7190d1beda15681982d3b2f60318bc0f15ebc22af304050ff504279538df3`。

独立将candidate中helper移除、两处调用还原为旧表达式，**整个header与baseline逐字相同**。因此新改动只有：

- 保1920×1152的Original/Small旧资格。
- 保Original1920×1088、1600×960旧资格。
- 新增Small1600×1024、1344×768、1920×1088。

提取真实helper编译运行`scope-review.cpp`，84个model/width/height组合验证新增集合恰为这三项，未扩大Original其它尺寸。实际生产deep_fast/vit_stream源码SHA仍与复用模块身份一致；不重编GPU模块，不改变数学实现。

VF的Small拒绝、设备gfx1201/9070XT、selector、FAST、MP1、graph/experimental/sparse、byte-edge、split-K/fused等门和缺export回退都未改。现有同Network低层模式资格冻结边界也未偷偷修成另一个任务。

源审身份在`source-review-identity.json`；几何与资产检查分别见`geometry-review.log`、`small-assets-review.json`，288/448/640动态尾映射见`tail-mapping-review.json`。最初验证脚本曾假设host源码快照含hip目录而读取失败；改为核真实生产hip源码SHA与复用identity，未改任何生产或GPU实现。

建议实际日志至少确认：requested/effective=0.5、31blocks、processing与tokens、8组compact contract/projection实际命中、VF Small calls0；900/720/free1088的entry-exit/gather/Up39实际callee。每种新shape先对自身baseline Gold；free1072不能套标准1080旧SHA。AE和MODE1短回归按各自实际协议，公共F9几何重建回环，不扩低层同geometry承诺。

当前结论是资格/布局/边界源审通过，**不是预先宣布W16 projection在新Small输入上必然逐位相同**。后者以本轮GPU Gold与完整Frame结果裁决；审计者未执行GPU/RTC或目标机操作。
