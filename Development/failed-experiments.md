# 未采纳与失败实验总账

整理基线：生产仓 `0431ba0e`（2026-10-12）。这是检索台账，不改变历史裁决，不运行新实验。

## 检索入口

- 时间段：[2026-09-26及以前](#through-0926)；[2026-09-27—29](#0927-0929)；[2026-09-30至今](#since-0930)；[覆盖与缺证](#coverage)。
- 主题可直接搜索：`C32`、`C64/C128`、`C256/SP`、`C512`、`ViT/QKV`、`DirectML/HLSL`、`compiler/LLVM/ACO`、`graph/PDL/pulse`、`interop/handoff/IO`、`history/temporal`。同名不同日期保留阶段区别，跨段的后续采纳以最新裁决为准。
- Δ默认候选减基线，正数慢；`900/1080/1440`是原报告档位，处理高度960/1088/1152/1472须看该条scope。µs与ms明确标单位；微核、纯NN、完整NR Frame和游戏FPS不能相加或互代。

## 读法与状态

- **性能负**：正确性通过，但有效对照未达到采纳门，或均值/尾延迟退步。数字只适用于原硬件、几何、调用范围。
- **收益不足**：观察略快/中性，但未过当时的幅度、两档或尾门；不等于已证明变慢。
- **正确性失败**：实际数值/布局/同步门失败，不能拿其时量作为性能收益。
- **被修复/替代**：记录失败候选及后续修正，不能据此判整条路线永久失败。
- **未验证/暂停**：只有源码/静态估算/无效试跑，或主动止于范围边界；不冒充严格性能负。
- **环境/编译/测量无效**：错误模块、参数、驱动编译、计时污染等；原证保留，不与有效结果混算。

每条统一记录：`日期｜实验标识（含别名）｜状态`；随后写改动、真实基线/范围、正确性与时量、裁决及重试新条件、原始证据路径/已有SHA。缺少数字或身份时明确写未记录，不补造。

## 覆盖与去重方法

本版按当前及归档 DevHistory、WorkingPlan、Development/results 与 Development/HIP/experiments逐项归并；同机制的阶段回执合并，数值与性能裁决分开。下列候选分组中有的含多个明确变体，因此分组数不是执行次数、失败次数或独立统计样本数。早期组没有原始单项时量时明确保留缺失，不用整批成功数字代填。覆盖依据与未核清边界见文末。

<a id="through-0926"></a>

## 2026-09-26 及以前

### 08-31～09-03｜PIX/live oracle/row-major与effective固定帧｜环境测量无效

结果/裁决：PIX五种捕获均失败；row-major encoder幅度到block21约1e-10；archive half误喂packedFP8导致0/0x7f饱和；23..30错view/grid.z=4及residual指针后修复。自建NVAPI跨ViT私有tile-sync两次WDDM锁死，不作35..38生产oracle。固定帧RGBcorr.945不等完整71层：NV入口及affine skip被严格审计撤销，随后原生逐层替代。

证据：`Development/history/DevHistory-full-20260923.md:136`。

### 09-05｜尾矩阵Agility实验/60fps/网格修复旧结论｜环境测量无效

结果/裁决：LinAlg C32FFN .569→.257ms但114240值差；AA关闭使FSR未创建、以及ReShade _has_commands=false没真正提交，让旧60fps/画面修复无效；回退c5a3a8a，停止追FPS转原生exact链。

证据：`Development/history/DevHistory-full-20260923.md:174`。

### 09-06｜double→F32→Half mix末舍入｜正确性失败/被修复或替代

结果/裁决：RGB512→ViT38首差DS0五值；单点看似成立的末舍入候选全幅更坏并撤回，后按WMMA对齐截断规则修复。不能用单点支持全幅舍入替换。

证据：`Development/history/DevHistory-full-20260923.md:183`。

### 09-06～09-07｜640token single-list、同阶段合并chunk、同列表拆dispatch｜正确性失败/被修复或替代

结果/裁决：首帧exact第二帧DEVICE_HUNG 0x887A0006/LKE141；同提交拆dispatch仍TDR；chunk独立提交+fence才过，整网singlelist禁用。数学exact不证明执行可重放。

证据：`Development/history/DevHistory-full-20260923.md:183`。

### 09-07｜时序坐标：整轴doubleFMA、leftFMA、Newton倒数、整数量化｜正确性失败/被修复或替代

结果/裁决：整轴double fma产生91159差；left权重FMA、倒数Newton精化、运动采样整数量化均无效/撤回。最终靠21bitUV、8bit纹理fraction/乘积量化、FMA顺序和TEX tie规则闭合；不要用“更高精度”替原算术。

证据：`Development/history/DevHistory-full-20260923.md:214`。

### 09-08～09-09｜C32一窗一wave/局部同步占用假说、多头QKV+attention融合｜性能负

结果/裁决：一窗一wave零组同步反而4.7ms；MH QKV+attention融合因每head重读输入更慢关闭。后来wave-owned重构是不同数据流，不能由这条禁止。

证据：`Development/history/DevHistory-full-20260923.md:238`。

### 09-10～09-11｜HLSL ViT fused FFN 16/32token｜性能负

结果/裁决：DLSS5_VIT_FUSED_FFN：expand+contract同LDS，逐位同；每块.35～.39ms对旧.08+.10，32token翻倍重用无改善。640token全16wave组并行度不足，省中间量不抵依赖延迟。

证据：`Development/history/DevHistory-full-20260923.md:272`。

### 09-11｜HLSL C32 two-pass softmax / 消896Bspill假说｜环境测量无效/性能负

结果/裁决：RGP占位DXBC hash缓存旧pipeline，认错p0087；实际运行p0006 scratch0。两遍softmax逐位同但VGPR112→118、指令7627，不采用；不是已测“消spill”。

证据：`Development/history/DevHistory-full-20260923.md:272`。

### 09-11｜HLSL LDS_SLIM / typed groupshared alias / 加4KB占用探针｜性能负/正确性失败

结果/裁决：29696→25600B，C32段16.52→16.24但整帧34.88vs34.87持平；加4KB超32K验证器拒；uint数组跨F16/E4M3 matrix用法PSO access violation，换正确typed数组解决该构建失败。

证据：`Development/history/DevHistory-full-20260923.md:272`。

### 09-11｜DX12 OVERLAP COMPUTE普通/HIGH队列｜性能负/被修复或替代

结果/裁决：第一版先交付再capture反馈自身输出成噪声（已修）；正确晚一帧路径菜单基线32.5～34.9→49.5～50ms，HIGH52ms；独立无游戏23.9vs24.1持平。只否定该卡该场景的图形/计算真并发实现，flag默认0。

证据：`Development/history/DevHistory-full-20260923.md:272`。

### 09-11～09-12｜时序黑块/Magpie垃圾motion、上传batch悬空、宿主UAV提示层｜正确性失败/被修复或替代

结果/裁决：motion暗部±5万像素致warp黑洞，64pxguard止血非修光流；NativeResidentBatch仅持源不持目标→TDR，补两端引用到flush解决；宿主纹理CreateUAV即D3D12CoreAV，改私有raw+CopyTextureRegion。均是已修工程缺陷，不列为永久不做优化。

证据：`Development/history/DevHistory-full-20260923.md:272`。

### 09-12｜HLSL C512 split FFWD FP8替F16｜性能负

结果/裁决：NATIVE_SPLIT_FFWD_FP8全链nohist/hist逐位，核藏起会initfail证实际加载。encoder min.852→.880/median1.494→1.502；隔离×100 min .118/.114vs.116/.114；整帧31.9～32.7重合。默认关/不发cso，换更高矩阵峰值未兑现。

证据：`Development/history/DevHistory-full-20260923.md:272`。

### 09-03｜post黑盒MLP与row-major1H｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：`run_original_post_dataset.cpp`把单tile oracle改为复用同一CUDA context/module、weights、texture与surface，只逐样本上传2048-byte main和512-byte skip。独立held-out同时否决无结构MLP：128样本时correlation约0.04，8,192 train＋1,024 held-out、2560→512→192模型也只有0.292，MAE 0.0913，甚至不如零残差baseline MAE 0.0725。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1276`。

### 09-03｜body黑盒MLP｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：批量runner新增`features`模式：保留body与48-value input/gain区，依次给两块out-conv的32个逻辑channel置unit weight，从R通道直接读回64×32 body feature。无结构body MLP再次被held-out否决：8,192 train＋1,024 held-out的2560→512→2048模型correlation仅0.377、MAE 0.401，几乎等于均值baseline 0.413。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1286`。

### 09-03｜portable-upsample+raw矩阵fullbody联合训练｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：尝试把portable upsample直接接raw-unpacked W1/W2/QKV/projection并做full-body联合训练，2,048个iid样本上held-out correlation只到0.254，明确否决。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1300`。

### 09-03｜按std拟合skip全局scalar｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：现存`stellar-block8/14/22-skip.fp8`尺寸恰好等于各级padded tensors，但直接E4M3解码std为54.8/78.4/46.7，仍是physical-scaled坐标而非canonical。该结果否决“按std拟合skip scalar”的捷径。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1382`。

### 09-03｜fullwidth256→576→256 E4M3解释｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：block49 controlled分支给出更可靠的body证据。双residual权重下，修正为bijective 16KiB input scatter后，15,872/16,384 output bytes与input exact，其余512为固定无效槽。将record矩阵误解成full-width E4M3 `256→576→256`反而降至0.532，因此该路线再次否决，临时代码已撤回。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1406`。

### 09-04｜DirectML mixed FP32A/FP16B→FP32｜API/类型创建失败，无可运行数值候选

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：驱动能力探针同时尝试直接省掉边界转换的混合类型GEMM：`FP32 A × FP16 B → FP32 output`在`CreateOperator`返回`E_INVALIDARG (0x80070057)`。正式接入必须使用GPU cast（HLSL或DirectML graph cast）→FP16 GEMM→GPU unpack＋`F()`。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1728`。

### 09-04｜DirectML C64完整FFN FP16总输出｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：完整DirectML block63对同版本即时旧oracle corr0.9999734214、MAE0.0129612、max0.5、76.5630% exact，全finite；故64档状态明确为性能通过、最终exact失败，不进入主线。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1860`。

### 09-04｜DirectML FFN Ksplit2/3｜正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：K-split实测否决该假设。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1862`。

### 09-04｜Attention只换DXC cs6.2｜性能负

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：最低风险的Attention DXC重编先做控制实验。故“只换DXC”路线否决，正式仍用旧DXBC； 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1906`。

### 09-04｜32640windows batchedDirectML QK/AV｜性能负/正确性失败

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：完整batched Attention推翻了上段乐观比较：4.316ms是**单层**两次GEMM，四层矩阵主体本身约17.265ms，已经接近旧四层Attention总计18.190ms，之前把单层与四层总量放在同一分母是口径错误。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1910`。

### 09-04｜front FP32预归一化｜性能负

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：旧Attention的重复norm随后做FP32预归一化候选：QKV pass按token计算一次Q/K范数，保存`Q×scale`、normalized K与原V，Attention只做dot+bias+softmax。路线否决并隔离为`d3d12_front_normalized_attention.cpp`，干净front再次恢复。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1914`。

### 09-04｜post70 DirectML裸FFN变完整边界｜性能负

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：连续两次front Attention候选失败后切换热点，重审block70。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:1918`。

### 09-05｜groupFFN全C32扩展｜性能负/部分采纳

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：group FFN实测：block70 FFN5.76844→4.59296ms有效，但front/decoder C32从8.62/8.32升到10.687/10.730ms，全网退回61.43072ms。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:2052`。

### 09-05｜共享窗口attention扩到SHIFTED｜性能负/部分采纳

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：block70 attention每个8×8窗口一个64线程组，将K/V及K范数放入groupshared，复用窗口内数据。单独替换block70时attention4.69968→2.704ms、全网54.66756ms。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:2058`。

### 09-05｜C32 batch4 FFN｜性能负

改动/基线：实际模型/窗口大小按该原记录；非当前HIP。

结果/裁决：新增c32_ffn_batched.hlsl，每64线程处理4token（16lane/token），共享输入与hidden。 教训：矩阵裸核不代完整边界；局部exact不代多层累计，原记录成功子路径不一起否定。

证据：`Development/history/porting-worklog.md:2074`。

### 09-04｜ViT QKV两pass normalize｜正确性失败

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：单block31逐byte exact且15.6→12.4ms；八层block38corr.993642/NRMSE11.37%，撤回恢复singlepass。

证据：`Development/history/README.md:77`。

### 09-04｜ViT Expand仿Contract共享64值块｜性能负

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：Contract58.5→6.5～7.4ms成功；同法Expand反慢撤回，不能外推共享tile收益。

证据：`Development/history/README.md:73`。

### 09-04｜block70 FFN→QKV单shader融合｜性能负

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：BLOCK70_FUSED_FFN_QKV逐byteR10同；融合37.974～38.903ms对分离32.161～36.230，否决，原文归因资源压力/spill。

证据：`Development/history/README.md:281`。

### 09-04｜DirectML32补shiftmask｜性能负/正确性失败

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：52.204ms候选加shiftmask升91.697ms且不改善主误差，未采用。

证据：`Development/history/README.md:285`。

### 09-08｜矩阵gate native-half转换｜正确性失败

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：独立8847360值2.156008ms且910515值差/max2/MAE.0020885；原exact矩阵1.748128ms，nativehalf不采用。

证据：`Development/history/CURRENT-STATE.md:423`。

### 09-08～09-09｜MAP_FEATURE矩阵残差/wave-prefix独立核｜性能负/正确性失败

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：无收益/PSNR下降，关；汇总未给可归属单项速度。

证据：`Development/history/CURRENT-STATE.md:211`。

### 09-08～09-09｜MH QKV归一化平方和MMA｜性能负

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：探针.103→.099ms，收益不足，define留关；不是后来整个fast-attention成功项。

证据：`Development/history/CURRENT-STATE.md:215`。

### 09-08～09-09｜finish位元F / exp整矩阵Store LDS / FFN映射LDS｜性能负

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：finish位元F0收益；exp表整块LDS两轮无收益回退；FFN映射LDS+barrier比wave广播慢.7ms@2.2M。

证据：`Development/history/CURRENT-STATE.md:227`。

### 09-08｜FAST_F half-away初版｜正确性失败/被修复或替代

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：half-away不同原HLSL round RNE；修RNE后exact但中性不采用。matrix-store本身后来默认，不能整组说未采用。

证据：`Development/history/CURRENT-STATE.md:281`。

### 09-05｜FP16feature/WaveSize/统一descriptorHeap/scorecacheMode1/projection全展开｜性能负/未采纳

改动/基线：原HLSL阶段该候选，不混当前HIP数学。

结果/裁决：压缩记录明确五项否决，未保单项数字；C32 batch4已独立记录，不再重复。

证据：`Development/history/DevHistory-full-20260923.md:174`。

### 2026-09-15｜HIP_C32_LDS_VECTOR两次重测｜收益不足未采纳（RTZ成功项不列负）

结果/裁决：新增--wall-profile：逐kernel执行前drain、执行后等待，用steady_clock累计调用墙钟和次数。 共享内存连续8个FP8字节以memcpy整组读入寄存器（HIP_C32_LDS_VECTOR可选），编译与四轮整网hash通过，但65.267→65.307ms无收益，保持关闭。benchmark-module-swap.ps1保存每轮日志并检查hash，失败立即停止。 先复查旧HIP_C32_LDS_VECTOR实验，在当前DWORD权重+direct8布局重新ABBA：35.465→35.422ms，输出一致但无明确收益，仍关闭。 同alias重测合并：2026-09-15：C32输入DWORD写入；LDS整组读取仍关闭。

证据：`Development/history/DevHistory-full-20260923.md:778`；`Development/history/DevHistory-full-20260923.md:979`。

### 2026-09-15 16:28起：完整HDR计时、Graph验证及四项后续优化｜性能负/未采纳

结果/裁决：读取用户试玩日志，已安装68c8…版最后几批滤镜耗时57.5/57.8ms，之前62.2ms；24帧每8帧reset：每图build3/replay18，最终SHA22C171FC…一致，50.748→50.8465ms无收益。Graph不默认启用，也未修复live异步问题。发现旧计时只Flush外层submission，async时未等Frame内部提交，HLSL异步0.488ms为无效CPU提交时间。

证据：`Development/history/DevHistory-full-20260923.md:818`。

### 2026-09-15 17:00：C32缓存及MH分块实验未采用｜性能负/未采纳

结果/裁决：完成8组正常900 ABBA，每组四进程各6次、排除cold，每方案10hot；没有发现值得替换默认的稳定加速，实验实现已从默认源码撤回；

证据：`Development/history/DevHistory-full-20260923.md:844`。

### 2026-09-15 17:57｜MH FFN无合作input初版｜收益不足，后合作输入版采纳

结果/裁决：实现C64/128/256的16-token/group融合FFN，分别128/256/512线程；初版45.2915→45.2645ms，无明确收益；后合作input版44.617ms获采纳，只封存无合作input初版。

证据：`Development/history/DevHistory-full-20260923.md:862`。

### 2026-09-15｜ViT16×16 Split-K｜性能负（四输出成功项不列负）

结果/裁决：在已提交C32优化基线上继续对照HLSL的ViT组织。COMGR与整网逐值通过，但16×16小块Split-K 41.4745→42.8955ms更慢，未默认。

证据：`Development/history/DevHistory-full-20260923.md:886`。

### 2026-09-15：修复异步颜色／运动描述符重绑竞态｜被修复或替代/保留负分支

结果/裁决：RebindSourceAfterCompletion原来只持有CPU锁，在deferred提交尚未完成时就原位重写SRV并释放旧resource；旧异步错误hash随调度变化，不能写死错误hash。

证据：`Development/history/DevHistory-full-20260923.md:915`。

### 2026-09-15：C32输出投影LDS整组读取撤回，刷新热点｜性能负/未采纳

结果/裁决：仅将输出投影a操作数从逐byte put_bits改成memcpy8，宏HIP_C32_OUTPUT_READ8控制。COMGR编译及正常900 ABBA4轮输出hash全部通过，但热中位34.0665→34.243ms略慢，已从生产源码撤回。

证据：`Development/history/DevHistory-full-20260923.md:989`。

### 2026-09-15：MH QKV输入预打包无收益，撤回｜性能负/未采纳

结果/裁决：新增独立pack4输入核和fast_dense<true,false,true>的QKV归一化入口，沿用原pack4转换，没有使用带额外clamp的ViT pack核。正常900 ABBA4轮各6次去cold，全通道33.891→34.2605ms更慢；仅C>=256时33.963→34.1615ms也更慢。生产源码已撤回。

证据：`Development/history/DevHistory-full-20260923.md:995`。

### 2026-09-15：刷新MH同输入层级对照，QKV单wave四输出实验撤回｜性能负/未采纳

结果/裁决：compare_layers.cpp增加mh-only过滤，并在所选融合FFN配置中启用identity-shift；正常900 ABBA4轮各6次去cold32.939→33.727ms更慢，四轮RGB7b959143…一致。未继续历史/HDR，已撤回生产改动。

证据：`Development/history/DevHistory-full-20260923.md:1044`。

### 2026-09-15：MH V转置与直接F-byte输出实验归档｜性能负/未采纳

结果/裁决：V转置候选在载入时将V写为[channel][token]、行跨度68，保留Q/K布局；正常900 ABBA4轮各6次去cold31.155→31.198ms，四轮RGB7b959143…一致，但无收益，已撤回。

证据：`Development/history/DevHistory-full-20260923.md:1068`。

### 2026-09-15：QKV分块权重合作装载实验撤回｜性能负/未采纳

结果/裁决：PackedMhWeight仅对QKV矩阵区域做TilePackedMatrix [N16][K32][K][N]纯byte重排，独立cache key和mh_qkv_normalize_fused_tiled入口； COMGR/gfx1201模块与runner编译通过，正常900 ABBA4轮各6次去cold31.129→31.4705ms更慢；源码已撤回。

证据：`Development/history/DevHistory-full-20260923.md:1082`。

### 2026-09-15：QKV ISA资源检查及共享暂存复用实验撤回｜性能负/未采纳

结果/裁决：读取当前mh-row-sum.hsaco.s：mh_qkv_normalize_fused使用VGPR46、SGPR14、固定LDS21760byte、private segment0，无scratch spill。正常900 ABBA4轮各6次去cold31.1555→31.2715ms，四轮RGB7b959143…一致，但无收益；

证据：`Development/history/DevHistory-full-20260923.md:1090`。

### 2026-09-15：QKV K64装载分块实验撤回｜性能负/未采纳

结果/裁决：在已通道特化的QKV上把K32分块扩大到K64：每次合作装载两倍A/B，LDS行stride9改17，四次K16 WMMA维持原累加顺序，全组barrier次数减半。 COMGR/gfx1201编译通过，正常900 ABBA4轮各6次去cold30.7635→30.9125ms略慢，四轮RGB7b959143…一致。编译后QKV LDS25856byte（旧21760）、VGPR69（旧45）、private0。

证据：`Development/history/DevHistory-full-20260923.md:1114`。

### 2026-09-15｜C512 split_ffn多wave gr索引错误｜正确性失败已修复并采纳

结果/裁决：新增split_ffn_fused：每组16token×64channel、4wave计算256维展开并将原Hrtz+激活+FP8结果留在LDS，再按原K16 FP16 WMMA顺序收缩，输出F(Hrtz)。 初版复用单wave gr()辅助函数导致多wave索引错误，首轮hash检查拦截；失败轮不计性能。修正gr索引后29.924→29.509/31.4555→30.8895ms并采纳，不判融合路线失败。

证据：`Development/history/DevHistory-full-20260923.md:1138`。

### 2026-09-15｜ViT QKV四输出分块｜性能负（两输出成功项不列负）

结果/裁决：先试单wave16×64四输出，正常900 ABBA4轮去cold29.0145→29.1985ms略慢，四轮RGB一致，不采用；

证据：`Development/history/DevHistory-full-20260923.md:1174`。

### 2026-09-15：C512输入/残差映射实验暂不采用｜性能负/未采纳

结果/裁决：新增split_mix_blocked_mapped及split_projection_blocked_mapped，前者矩阵输入、后者残差均按原shift_pack的workgrid→raster规则映射，越界0；无明确速度收益，未继续历史/HDR、未默认； 同alias重测合并：2026-09-16：C512直接映射输入实验不采用。

证据：`Development/history/DevHistory-full-20260923.md:1198`；`Development/history/DevHistory-full-20260923.md:1468`。

### 2026-09-15：QKV/FFN直接量化byte收益过小，归档｜性能负/未采纳

结果/裁决：实验dense_byte_F合并q8(F(x))，保留输入正负零归正零、带符号超范围/非有限饱和，其余直接执行FP8转换；连续负载EdgesOnly=1、40帧ABBA：基线26.127/26.129ms，候选26.053/26.045ms；

证据：`Development/history/DevHistory-full-20260923.md:1250`。

### 2026-09-15：prefix完全内联C32的实验暂不采用｜被修复或替代/保留负分支

结果/裁决：实验c32_prefix_inline在128线程前置C32内生成原PCG/历史/RGB特征，复用scratch执行前置投影；没有将残差错误替换成FP8值。未进一步全帧/reset验证，生产源码已恢复，不采用。并未出现预想的寄存器溢出，不能以寄存器压力作为无收益的已证原因。ABBA（benchmark_pinline.exe）：关17.242/17.122，开17.247/17.251，**+0.07，null**。 同alias重测合并：2026-09-17 00:21起（续）：prefix内联进block-0 C32核 null（+0.07）。 后续09-17 02:23在新基线上−0.27并采用；本条仅旧实现/旧基线未采纳。

证据：`Development/history/DevHistory-full-20260923.md:1276`；`Development/history/DevHistory-full-20260923.md:2135`。

### 2026-09-15：前置融合核硬件RTZ实验无明确收益｜性能负/未采纳

结果/裁决：测试用HIP_PREFIX_RTZ_ISA将prefix的有限值Hrtz替换为v_cvt_pkrtz_f16_f32+v_cvt_f32_f16，非有限输入仍先按原实现返回x； 同一连续负载40帧ABBA：基线25.392/25.389ms，候选25.370/25.383ms，四轮最终FEEA9EF3…匹配且首尾有限。

证据：`Development/history/DevHistory-full-20260923.md:1292`。

### 2026-09-15：C32成对输入量化无稳定收益，归档｜性能负/未采纳

结果/裁决：HIP_C32_PAIRED_INPUT候选把每四个输入值的4次单值FP8转换改为2次双值转换，仍先按原范围clamp、相同mapped/raw-chain/merge读取，DWORD写回布局不变。最终FEEA9EF3…一致，首尾有限，计时无稳定收益。

证据：`Development/history/DevHistory-full-20260923.md:1300`。

### 2026-09-15：MH投影通道特化收益过小，归档｜性能负/未采纳

结果/裁决：在同序标量残差基线上，分别对matrix FP8投影及crop投影按64/128/256/512通道传入编译期常量，保留各通道的matrix/scalar残差及post epilogue；收益约0.08ms，未进一步全帧/reset，不采用，生产源码已恢复。

证据：`Development/history/DevHistory-full-20260923.md:1354`。

### 2026-09-16：最新profile及保持64×64的QKV双输出wave实验｜性能负/未采纳

结果/裁决：profile-current.ps1更新为prefix-direct-input-release-modules及全部当前快速选项，reference runner新增--direct-prefix-input。未使用编译失败后的不完整基准。更慢，不采用，生产源码恢复。

证据：`Development/history/DevHistory-full-20260923.md:1392`。

### 2026-09-16：HIP head直接写共享输出实验暂不采用｜性能负/未采纳

结果/裁决：RunGraph实验允许借用最终输出buffer，EnqueueRaw用非owned Allocation包装rgb_output，head直接写入并省去末尾hipMemcpyAsync；收益约0.02–0.09ms，未进一步全帧/排队/reset，不默认，生产源码恢复。

证据：`Development/history/DevHistory-full-20260923.md:1400`。

### 2026-09-16：MH投影单wave四输出及wave打包均不采用｜性能负/未采纳

结果/裁决：新增实验projection_wave4<C>：普通C64/128/256投影每wave16×64、共用byte AV输入，保留三段标量残差、K16顺序、crop和post epilogue；32线程版本连续40帧ABBA：基线24.747/24.792ms，候选25.289/25.302ms，更慢。随后将4个独立wave放进128线程组、虚拟bid=group*4+wave，所有运算保持，ABBA基线24.818/24.731ms、候选25.388/25.400ms，也更慢。仍更慢，不采用，未扩大全帧/reset，源码恢复，游戏不变。 同alias重测合并：2026-09-16：仅C64启用单wave四输出投影仍慢。

证据：`Development/history/DevHistory-full-20260923.md:1408`；`Development/history/DevHistory-full-20260923.md:1628`。

### 2026-09-16：MH投影lane-major权重实验不采用｜性能负/未采纳

结果/裁决：在单wave四输出投影上，只重排C64/128/256 attention权重的projection区域（3*C*C），按[N16][K32][kt16][lane32][byte8]存放；仍比现有合作LDS投影慢，未进一步全帧/reset，不默认，生产源码恢复。

证据：`Development/history/DevHistory-full-20260923.md:1414`。

### 2026-09-16：MH AV双输出共用概率读取实验不采用｜性能负/未采纳

结果/裁决：在mh_attention_fused_fp8_out最后AV阶段，将两次16输出通道循环改为两组累加器共同遍历K16，复用概率矩阵操作数读取，各自K16累加顺序、输出F、布局及同步保持。 连续40帧ABBA：固定ViT融合基线22.792/22.812ms，候选22.862/22.831ms；

证据：`Development/history/DevHistory-full-20260923.md:1492`。

### 2026-09-16：MH crop投影K64分块实验不采用｜性能负/未采纳

结果/裁决：回查通道特化已做过，未重复。仅fast_dense的Crop实例由K32扩大到K64，合作装载两轮DWORD、LDS stride9→17，同步次数减半，四次K16 WMMA保持累加顺序；与旧Normalize K64实验不同，此实例不含归一化临时LDS。mh_attention_crop资源：LDS8704 bytes、VGPR43、SGPR34、private segment0、spill0。连续40帧ABBA：基线22.799/22.820ms，候选22.823/22.802ms，最终FEEA9EF3…匹配、首尾有限。

证据：`Development/history/DevHistory-full-20260923.md:1506`。

### 2026-09-16：C32 V结果直接FP8写出实验不采用｜性能负/未采纳

结果/裁决：在REGISTER_FFN路径单独处理QKV的V：两组16列结果保留寄存器，完成本wave全部FFN读取后直接写packed V，省去scratch.raw的写回与再读取；略慢，不采用，未扩大全帧/reset，生产源码恢复，游戏仍13d4dc10… ViT融合版。

证据：`Development/history/DevHistory-full-20260923.md:1524`。

### 2026-09-16：部署C32直接权重候选，局部同步实验暂不采用｜性能负/未采纳

结果/裁决：部署脚本确认游戏退出，将c32-global-ffn-release-modules全部24模块安装并逐hash通过，DLL沿用13d4dc10e6507eb05120b056a6710498e4922a53e699dd6878be2674614ae821。 随后仅在独立候选中将FFN展开后、contract→FFN、FFN→QKV的三处整组同步换为已有sync_owned_rows，保留合作输入载入及注意力跨wave读之前的整组同步。连续40帧ABBA：基线21.939/21.940ms、候选21.900/21.912ms，最终FEEA9EF3…一致、首尾有限。

证据：`Development/history/DevHistory-full-20260923.md:1540`。

### 2026-09-16：C32分半隐藏层计算实验不采用｜性能负/未采纳

结果/裁决：参照HLSL ffn_fused的两半结构，先初始化残差累加器，再每次展开64个hidden通道并立即收缩，hidden stride132→68、重复使用64通道暂存。明显更慢，不采用，未扩大全帧/reset，生产源码恢复。

证据：`Development/history/DevHistory-full-20260923.md:1548`。

### 2026-09-16：C32 raw-chain固定模式入口无收益｜性能负/未采纳

结果/裁决：核对生产C32 prev路径固定传mode3/raw1；没有稳定收益，未扩大全帧/reset，不采用，生产源码恢复。

证据：`Development/history/DevHistory-full-20260923.md:1556`。

### 2026-09-16：MH crop投影直接权重读取不采用｜性能负/未采纳

结果/裁决：保留原64×64输出tile、512线程、共享A输入和K32分块，仅Crop+packed实例取消B权重LDS写入，WMMA直接读取对应两DWORD。更慢，不采用，未扩大全帧/reset，生产源码恢复，游戏仍c32-global-ffn-release-modules+13d4dc10… DLL。

证据：`Development/history/DevHistory-full-20260923.md:1564`。

### 2026-09-16：C512 mix FP8指令探测不采用｜性能负/未采纳

结果/裁决：读取真实block23-ffwd.f32，mix262144/expand131072/contract131072个float逐值检查均可由有限E4M3精确表示。更慢，未扩大全帧/reset/所有权重检查，生产源码恢复，游戏不变。

证据：`Development/history/DevHistory-full-20260923.md:1572`。

### 2026-09-16：C512 mix预打包权重实验暂不采用｜被修复或替代/保留负分支

结果/裁决：新增独立split_mix_fp8_packed入口，保留float输入现场FP8转换，权重使用PackedDeepWeight(ffwd,262144)一次精确编码并缓存，读取两DWORD作为WMMA B。无稳定可见优势，不纳入生产，未扩大全帧/reset，源码恢复。 同alias重测合并：2026-09-16：C512 mix精确half权重预打包暂不采用。 后续09-16 21:50 mix half预打包−0.12并采用，不是永久负。

证据：`Development/history/DevHistory-full-20260923.md:1580`；`Development/history/DevHistory-full-20260923.md:1784`。

### 2026-09-16：C32 raw输入移除重复FP8往返实验暂不采用｜性能负/未采纳

结果/裁决：mapped_c32_input的Raw分支原先F(half)返回float，再由输入暂存的fp8重新编码。连续40帧ABBA基线21.924/21.949ms，候选21.914/21.878ms，最终FEEA9EF3…一致、首尾有限。

证据：`Development/history/DevHistory-full-20260923.md:1596`。

### 2026-09-16：C32输入两两FP8打包实验暂不采用｜性能负/未采纳

结果/裁决：输入DWORD打包由四次fp8(value,0)改为两次cvt_pk_fp8(value0,value1)，保留每值clamp、mapped读取与字节顺序；COMGR编译通过，连续40帧ABBA基线21.883/21.950ms、候选21.875/21.927ms，最终FEEA9EF3…一致、首尾有限。

证据：`Development/history/DevHistory-full-20260923.md:1602`。

### 2026-09-16：MH残差三段尺度预计算实验暂不采用｜性能负/未采纳

结果/裁决：给packed attention缓存增加独立-residual3 key，验证原长度并在末尾追加3*C个float。仅约0.04ms差异，未扩大全帧/reset/Graph验证，不采用，源码恢复，游戏不变。

证据：`Development/history/DevHistory-full-20260923.md:1636`。

### 2026-09-16：C128两头分批复用工作区更慢，撤回｜性能负/未采纳

结果/裁决：实验将四head同时处理改为两head×两批，256线程，共用2head ex/packed工作区，AV独立64×132byte跨批保存，每批末同步后复用工作区；更慢，不采用，未扩大全帧/reset，源码恢复，游戏仍ae66d4c8…+c128-attn-project-release-modules。明显更慢，未扩大全帧/reset，不采用，生产源码恢复。 同alias重测合并：2026-09-16：C128分批投影循环展开消除private segment但更慢。

证据：`Development/history/DevHistory-full-20260923.md:1678`；`Development/history/DevHistory-full-20260923.md:1686`。

### 2026-09-16：C512两头八批注意力投影融合更慢，撤回｜性能负/未采纳

结果/裁决：新增独立c512_attention_project候选，256线程、两head一批共八批，独立全AV64×516bytes跨批保存。更慢，不采用，未扩大全帧/reset，生产源码恢复，游戏仍57b8ab45…+c256-attn-project-release-modules。

证据：`Development/history/DevHistory-full-20260923.md:1732`。

### 2026-09-16：C512 FFN FP8预打包有速度收益但出现逐位反例，暂不采用｜被修复或替代/保留负分支

结果/裁决：实验新增PackedSplitFfnWeight缓存，分别精确打包ffwd展开/收缩区域262144/393216起各131072个元素，旧float缓存仍供mix使用。首轮因组偏移仍按float推进而golden失败，修正g*16384→g*4096后重新编译测量；初次失败不计性能。block46在生产默认跳过，但此反例使任意有限FP8输入逐位等价的主张不成立，不能仅凭真实帧fixture通过交付。反例保留在test_split_ffn_fp8.cpp（需应用experiments/split-ffn-fp8.patch后编译）。验证脚本validate-split-ffn-fp8*.ps1会因该反例失败，不能写成全部通过。 日志release/HIP/split-ffn-fp8-test.log（错误布局）、split-ffn-fp8-fixed-test.log（修正版ABBA）、split-ffn-fp8-validation.log（反例）。

证据：`Development/history/DevHistory-full-20260923.md:1740`。

### 2026-09-16：当前C256 FFN-QKV非tiled权重布局更慢，撤回｜性能负/未采纳

结果/裁决：新增C256非tiled g128_qkv及mapped入口，使用mh_ffn_qkv_body<256,false,…>，生产实验阈值256→512使仅C256从tiled回到连续DWORD权重，其他通道/分组收缩/QKV融合保持。明显更慢，未扩大全帧/reset，生产源码与guard恢复，游戏仍c8842686…+vit-qkv-compact-release-modules。

证据：`Development/history/DevHistory-full-20260923.md:1824`。

### 2026-09-16：C256 tiled权重DWORD读取+wave交换更慢，撤回｜性能负/未采纳

结果/裁决：保持现有tiled矩阵布局，只替换mh_ffn_qkv_body的Tiled展开/收缩B操作数读取：各lane读相邻4列的两DWORD，再用ds_bpermute在半wave内转置成原8字节片段，保持各lane列/K顺序及全部矩阵/舍入。更慢，未扩大全帧/reset，生产源码恢复，游戏仍c8842686…+vit-qkv-compact-release-modules。

证据：`Development/history/DevHistory-full-20260923.md:1832`。

### 2026-09-16：融合投影直接读取FP8精确float残差更慢，撤回｜性能负/未采纳

结果/裁决：核对当前FFN第三投影输出经F或q8_fused_round后decode，当前生产feature为精确FP8 float。明显更慢，未扩大全帧/reset，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。单独C256也更慢，未扩大全帧/reset，源码恢复，游戏保持c8842686…+ffn-qkv-round-byte-release-modules。 同alias重测合并：2026-09-16：C256单独直接读取精确FP8残差仍慢。

证据：`Development/history/DevHistory-full-20260923.md:1854`；`Development/history/DevHistory-full-20260923.md:1868`。

### 2026-09-16：C32展开相邻输出成对计算收益不明显，暂不采用｜性能负/未采纳

结果/裁决：当前直接读取FFN权重的C32展开阶段，每次计算相邻两片16列，复用同一K16输入片段，保持每片累加、激活/FP8舍入、隐藏层布局及后续收缩不变。连续40帧ABBA基线19.325/19.365ms，候选19.300/19.328ms，最终FEEA9EF3…一致、首尾有限。

证据：`Development/history/DevHistory-full-20260923.md:1876`。

### 2026-09-16：C512投影同时写byte供QKV使用无稳定收益｜性能负/未采纳

结果/裁决：独立split_projection_blocked_dual保留float残差输出，同时写额外FP8 byte缓冲；无稳定收益，未扩大全帧/reset，不采用，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。

证据：`Development/history/DevHistory-full-20260923.md:1884`。

### 2026-09-16：decoder上采样投影half权重无稳定收益，暂不采用｜被修复或替代/保留负分支

结果/裁决：新增@decoder-f16缓存，精确打包inputs*outputs矩阵并保留float残差尺度原偏移；无稳定收益，未扩大全帧/reset，不采用，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。 后续同日22:45上采样half预打包−0.07并采用。

证据：`Development/history/DevHistory-full-20260923.md:1900`。

### 2026-09-16：post-head每线程RGB三通道计算无收益，撤回｜性能负/未采纳

结果/裁决：新增hip_post_head_fast_half_rgb，每线程负责一像素，读取32个half特征同时更新RGB三个累加器，保持各通道j0–31顺序、Hrtz、base合并与clamp；无稳定收益，未扩大全帧/reset，源码恢复，游戏仍c8842686…+ffn-qkv-round-byte-release-modules。

证据：`Development/history/DevHistory-full-20260923.md:1908`。

### 2026-09-16：C512展开交换循环共用输入无稳定收益｜性能负/未采纳

结果/裁决：仅split_ffn_fused_fp8的F16展开阶段将j外层/k内层改为k外层/j内层，四个输出片段共用同一h8输入，保持各片K0/16/32/48累加顺序、half权重布局、激活与FP8收缩。候选HSACO SHA4DA96B4F…与基线73B5CBD3…不同，未见稳定帧时间收益，未扩大全帧/reset，不采用，源码恢复，游戏不变。

证据：`Development/history/DevHistory-full-20260923.md:1924`。

### 2026-09-16：MH残差流E4M3字节化——逐位一致但更慢0.1ms，不采用｜被修复或替代/保留负分支

结果/裁决：按HLSL fp8_stream设计（链首f32输入、链尾f32输出、链内块间字节）实现：mh_ffn_qkv_body加ByteIn/ByteFeature模板（ffn_input8解码后走原f32代码；未跑全帧/reset/history三道（不采用）。连续40帧ABBA关19.268/19.239、开19.302/19.330，最终FEEA9EF3…一致——约+0.06ms更慢。 同alias重测合并：2026-09-16 07:45：ViT全链字节流（contract→qkv→attention→project→下一块expand）逐位一致但无收益。 后续09-19修复decoder尾漏dispatch后完整MH字节流通过并采用；旧反例不能归罪于byte表示。

证据：`Development/history/DevHistory-full-20260923.md:1968`；`Development/history/DevHistory-full-20260923.md:1985`。

### 2026-09-16｜Graph复测与ViT expand M4负分支｜性能负（fragment后来成功）

结果/裁决：- **Graph**（DLSS5_HIP_GRAPH=1，现基线）：ABBA 19.159/19.162→19.135/19.211，null，与09-15一致。

证据：`Development/history/DevHistory-full-20260923.md:1976`。

### 2026-09-16 07:50：ViT half流+QKV四列片段：N2更慢，N4在噪声内｜性能负/未采纳

结果/裁决：读HLSL native_wave_vit_qkv.hlsl（900p时400 token不是64倍数，HLSL走BLOCK_M=1）：其fused QKV A片段用pack16的half直接WaveMatrix load、零转换、BLOCK_N=4；六轮ABBA（关/N2/N4/N4/N2/关）：关19.323/19.188，N2 19.455/19.525（+0.23更慢，出乎意料——去掉转换反而慢，未查根因），N4 19.230/19.180（−0.05，在本机±0.07噪声内），最终FEEA9EF3…全部一致。不采用。

证据：`Development/history/DevHistory-full-20260923.md:1990`。

### 2026-09-16 07:58：C64/C128 FFN-QKV核批量归一化（先算三段QKV再一次LDS归约）逐位一致但更慢｜性能负/未采纳

结果/裁决：读HIP mh_ffn_qkv_body与HLSL native_wave_qkv_normalize.hlsl：两边生产版都是每行32项顺序求和（HLSL的NATIVE_QKV_FAST2 MMA求和未编进生产cso），顺序决定逐位一致，不能改树形。ABBA关19.188/19.172、开19.314/19.275，最终FEEA9EF3…一致——**+0.11ms更慢**。今天四次"按结构推理该更快"的改动（ViT字节流、half流N2、N4、QKV批量归一）全部不快或更慢：这批核的瓶颈不在barrier数、转换指令数或输入格式上，下一步不该再凭结构直觉改，要拿RGP抓HLSL与HIP同一核的真实指令占用/等待类型对照。

证据：`Development/history/DevHistory-full-20260923.md:2002`。

### 2026-09-16 17:50：池化+投影融合核逐位一致但无收益；下采样家族真实差距不到0.2ms｜性能负/未采纳

结果/裁决：精确名匹配重复法（dup_prefix末尾`$`=精确）：mh_pool四次launch≈0，mh_pool_project_production五次+0.50。ABBA关19.25/19.267、开19.235/19.296，最终FEEA9EF3…一致，无收益。

证据：`Development/history/DevHistory-full-20260923.md:2031`。

### 2026-09-16 18:50：C512 QKV归一化wave级重写逐位一致但更慢；今日结论：这批核卡在权重的L2重读｜性能负/未采纳

结果/裁决：mh_qkv_normalize_wave_c512：128线程组把16 token的A按pack4打包进LDS一次，四个wave各算64列（两个头）K=512 FP8 MMA链，B直接读预打包[N][K]字节，行平方和按j=0..31顺序在wave私有LDS tile上做，输出q8(F(acc*inv))与fast_dense<Normalize>同式。ABBA关19.277/19.231、开19.418/19.386，最终FEEA9EF3…一致，**+0.15更慢**。 把今天六次null放一起看（ViT字节流/half流/N4、FFN-QKV批量归一、池化投影融合、C512 QKV wave级）：凡是"每16个token一个wave、B从全局重读"的设计都不比"64×64 tile经LDS共享A/B"的老核快，甚至更慢——权重每16 token重读一遍，L2流量是tile版的4倍（C512 QKV：78MB对20MB每次launch）。

证据：`Development/history/DevHistory-full-20260923.md:2039`。

### 2026-09-16 19:20：ViT expand M2与tile布局输入均无收益｜性能负/未采纳

结果/裁决：M2（vit_expand_blocked_fp8_frag_bytein_m2，86 VGPR无scratch）：关19.157/19.11、开19.148/19.189；

证据：`Development/history/DevHistory-full-20260923.md:2050`。

### 2026-09-16 19:50：ViT expand+contract融合核逐位一致但慢0.55ms：并行度不够｜性能负/未采纳

结果/裁决：ABBA关19.189/19.200、开19.763/19.711，最终FEEA9EF3…一致，**+0.55更慢**。 ViT一天的账：字节流/half/N4/M2/tile输入/融合FFN六刀全部逐位一致、全部无收益或更慢；

证据：`Development/history/DevHistory-full-20260923.md:2053`。

### 2026-09-16 21:25：C64 attention-project按HLSL attention_direct拆成16 wave：逐位一致，null｜性能负/未采纳

结果/裁决：隔离账：HIP FFN+proj+QKV融合核0.145对HLSL ffn+qkv 0.172（HIP赢），HIP attention-project 0.104（帧内0.112）对HLSL attention 0.058+projection 0.018（每块输0.035≈C64全部差距）。ABBA关18.802、开18.785/18.828，null。 HIP_C32_LDS_VECTOR=1在当前基线第三次复测：19.106/19.018对19.033/19.089，null，与闇09-10/09-13两次结论一致。

证据：`Development/history/DevHistory-full-20260923.md:2086`。

### 2026-09-16 21:05：C512 attention投影照同一形状重写（**首轮ABBA无效，见21:55更正**）｜被修复或替代/保留负分支

结果/裁决：mh_attention_project_frag_c512（wave=16 token×64列，A=av字节行主序8B/lane，B=frag投影权重，残差Hrtz(feature*scale)、post/crop同fast_dense<ByteInput,Crop>），PackedMhWeightQkvFrag同时把projection区做frag布局。ABBA关18.693/18.697、开18.725/18.700，null。ABBA关18.687/18.740、开18.716/18.704，null。同一bug也让mh_feature_byte首轮成了假null，真结果+0.26更慢（18.655/18.666对18.40）——字节feature再次否定。 同alias重测合并：2026-09-16 21:40：C512 split projection照QKV-frag形状（contract存tile、frag权重、无LDS）（**首轮ABBA无效，见21:55更正**）、2026-09-16 21:55：更正——投影两刀的"null"是测试脚本runner没换；真结果 attention投影frag −0.06、split投影tile化 −0.18、两者合计 −0.31。 首轮runner未换造成假null，21:55重测−0.06并采用。

证据：`Development/history/DevHistory-full-20260923.md:2095`；`Development/history/DevHistory-full-20260923.md:2099`；`Development/history/DevHistory-full-20260923.md:2102`。

### 2026-09-17 00:21起（续）：当前构建成本表（dup法）；C32不展开循环降寄存器 null（+0.64）｜性能负/未采纳

结果/裁决：**HIP dup成本表（test-dup-map.ps1，benchmark_b4tail.exe，基线17.55）**：C64 ffn 1.32+attn 0.90=2.22（HLSL 1.93，+0.29）；pool_project_fused用当前runner复测仍+0.16更慢（poolf-retest.log），真null。模块交替ABBA：基线17.572/17.581，候选18.208/18.235，**+0.64更慢**。

证据：`Development/history/DevHistory-full-20260923.md:2124`。

### 2026-09-17 00:21起（续）：C64/C128 FFN tiled权重 null（+0.38）｜性能负/未采纳

结果/裁决：HLSL生产全通道用tiled权重（DLSS5_FFN_TILED_WEIGHTS/TILED_WEIGHTS），HIP只有C256走tiled（tiled_ffn_min_c=256）且C256是HIP领先的通道。ABBA（benchmark_tsmall.exe）：关17.421/17.424，开17.786/17.822，**+0.38更慢**——tiled对小通道不利（每wave列数少、tile利用率低），闇当初的min_c=256成立。

证据：`Development/history/DevHistory-full-20260923.md:2128`。

### 2026-09-17 01:35起（续）：C64/C128 FFN+QKV融合核frag权重 null（+0.08）｜性能负/未采纳

结果/裁决：mh_ffn_qkv_body加Frag位（`frag_b`：expand/contract/project/QKV四处B片改为FragmentPackedMatrix连续8字节读），权重`PackedFusedMhWeightFrag`（三个FFN区域）与`PackedMhWeightQkvFragOnly`（attention只frag QKV行，投影区留行主序给attention核）；ABBA：关16.847/16.889，开16.932/16.955，**+0.08更慢**——小通道权重（C64 FFN合计约20KB）本就常驻L0/L1，跨步读不是瓶颈，换布局只添指令；

证据：`Development/history/DevHistory-full-20260923.md:2147`。

### 2026-09-17｜C32 no-unroll与局部同步负分支｜性能负（对角残差后来采纳）

结果/裁决：核瘦身后按"旧null要重测"复测三个宏（compile-c32-retest.ps1，基线c32h-modules，ABBA各4轮）： - `HIP_C32_NO_UNROLL=1`（VGPR 192→84）：15.778/15.806对16.779/16.795，**+1.0更慢**（09-16是+0.64）。decoder的skip scale提升（`HIP_DEC_HOIST_SCALE`）ABBA 15.807/15.816对15.804/15.841，null，宏留0。 对角权重残差后来采用；no-unroll/局部同步仍各有负结果。

证据：`Development/history/DevHistory-full-20260923.md:2172`。

### 2026-09-17 03:31起：Zero授权路线1（PSNR门）与路线2（launch间隙）；先做PSNR验收与C32相位消融｜性能负/未采纳

结果/裁决：- PSNR验收：validate-psnr.ps1（三道同样的检查但不锁hash，输出 X-p-full.f16 / X-p-reset.f16 / X-p-history.f32）+ tools/psnr-check.sh（scp回来与逐位golden ffnh2-full/reset/history-check比PSNR、最大绝对误差、逐位相同比例）。 - V转置布局（`HIP_C32_VT`：V在QKV阶段写成[col][key] 68字节行的独立LDS数组，AV的B片一次8字节读，字节不变）：ABBA（c32vt对ffnh2）15.551/15.604对15.565/15.549，**null**，哈希一致。 结果（对ffnh2）：`PK_ACT=1`精确配对 15.540/15.557对15.538/15.552，null，哈希一致；`PK_ACT=2` packed f16 15.554/15.547对15.643/15.622，**+0.08更慢**（多出的f32↔f16转换抵掉了pk指令省的一半VALU）。

证据：`Development/history/DevHistory-full-20260923.md:2196`。

### 2026-09-17 10:18–12:00 显存路线：先量，再试 VMM 稀疏映射（失败）｜正确性失败/未准入

结果/裁决：- **先量**。整个插件占用 = 建网络前后 free 之差 = **1.17 GB**（测试台 15958→14785，游戏内 14700→13523）。估算活字节 225 MiB（ViT expand/contract 16 份 256 MiB → 69）。三个坑：(1) 一个保留区里混用不同大小的块 → `hipMemSetAccess` InvalidValue；改成一律 64 KiB 块后权重 607→236 MiB，但三道 hash 全变。读 64 KiB 多块 → 设备挂死（sync 不返回）；读 2 MiB 多块 → hipErrorLaunchFailure；(3) 一个物理块分段映射（`hipMemMap` offset≠0 或部分尺寸）→ InvalidValue。

证据：`Development/history/DevHistory-full-20260923.md:2227`。

### 2026-09-17 22:50 C32/post/decoder ISA 統計無病灶；C64 按窗口合核（FFN+QKV+注意力+投影一核）= null｜性能负/未采纳

结果/裁决：- ISA 統計（`Development/tools/isa-kernel-stats.py`，讀 rtc_compile 的 .hsaco.s）：C32 mapped/chain/finish_main8/chain_finish 2976/3705/3239/4360 條、70 wmma、VGPR 207/199/196/185、佔用 6～8、無 scratch 無 movrel；佔用率之前已用 no-unroll 排除（+1.0 更慢）。非逐位的路（PK_ACT=2 已測 +0.08 更慢、放寬精度已封、跳塊有性能檔）不再看。 - 23:05 buffer_load 這刀的底數：COMGR 的 clang 21 接受 `__builtin_amdgcn_make_buffer_rsrc` + `__builtin_amdgcn_raw_buffer_load_b64`，探針（hip-backend\buf_probe.hip）出 `buffer_load_b32 v, v, s[4:7], null offen offset:4096`，32 位 voffset、立即數偏移可折。

证据：`Development/history/DevHistory-full-20260923.md:2292`。

### 2026-09-19 16:30～16:37：1080档基线与分块候选初筛｜性能负/未采纳

结果/裁决：开始前检查游戏/Magpie均退出，下次运行仍须现场核实。 候选结论：ViT M2无收益；M4 ABBA基线21.1305/21.1335对21.134/21.050，差异小且不稳定，暂不采用；

证据：`Development/history/DevHistory-full-20260923.md:2410`；`Development/results/1080-20260919`。

### 2026-09-19 16:41：C32概率配对转换与1080阶段消融｜性能负/未采纳

结果/裁决：先回读旧结果，no-unroll虽降低VGPR却慢0.64～1ms，局部同步与新lane staging组合还曾破坏逐位，故不再盲试占用率/删barrier。新C32概率配对FP8转换（独立patch，未改生产）COMGR通过、1080最终hash不变，ABBA基线21.1055/21.2255、候选21.109/21.1705ms，无稳定收益，暂不采用。

证据：`Development/history/DevHistory-full-20260923.md:2420`。

### 2026-09-19 17:33起：有界倒数扩到多头/ViT，收益很小，暂留候选｜性能负/未采纳

结果/裁决：多头注意力与C32指数范围相同，保留其两路partial sum，只替换最终倒数；ViT 1080基线20.8095/20.8385对20.8125/20.8025，差约0.017ms，与波动相当，暂不采用。

证据：`Development/history/DevHistory-full-20260923.md:2455`。

### 2026-09-19 17:49起：多头Q/K缓存与预取三种尝试均不采用｜性能负/未采纳

结果/裁决：基于已采用MH指数寄存器复用的mh-ex-production-modules，尝试复用ex共享空间暂存Q/K，全部分数先留寄存器、全组同步后再写ex，LDS大小不增加但多一道同步。1080 ABBA基线20.695/20.7205，候选20.7485/20.7565，约+0.045ms更慢。只缓存K、Q沿原路径：基线20.696/20.7405对20.7295/20.729，变化在噪声内，无收益。最后把Q/K提前读到固定索引寄存器数组（不加共享缓存/同步）：20.734/20.727对20.710/20.7435，同样无稳定收益。

证据：`Development/history/DevHistory-full-20260923.md:2469`。

### 2026-09-19 17:56起：多头概率DWORD布局与融合AV共读均无稳定收益｜性能负/未采纳

结果/裁决：概率按[row][key%16][key/16]存储，让每lane的pv整数一次写4byte，AV按新布局取原概率；1080 ABBA基线20.701/20.756，候选20.7615/20.7305，无收益。不采用。ABBA20.7545/20.788对20.7725/20.744，同样无稳定收益；

证据：`Development/history/DevHistory-full-20260923.md:2474`。

### 2026-09-19 18:16起：局部特征方案黄金通过，完整字节流扩验发现旧反例｜被修复或替代/保留负分支

结果/裁决：局部MH_FEATURE_BYTE+MH_PROJ_DIAG_FB方案补过900w/960共六道黄金；回测原有完整MH_BYTE_STREAM同样在此seed123输入上失败，说明此前默认输入/小单元检查未覆盖整链反例，不能归咎于新导出，也不能把局部特征方案牵连为失败。

证据：`Development/history/DevHistory-full-20260923.md:2490`。

### 2026-09-19 21:17｜FFN入口qfeature缓冲复用｜收益不足（直接byte成功项不列负）

结果/裁决：以已采用的ffn-direct-production-modules为基线，入口暂存借用稍后才使用的qfeature，expand读qfeature、激活写hidden，去掉防止覆盖输入的一道barrier，无额外LDS。无稳定收益，不采用；第一候选未采用；

证据：`Development/history/DevHistory-full-20260923.md:2571`。

### 2026-09-19 21:32｜C256 expand预读｜性能负（连续fragment成功项不列负）

结果/裁决：先看当前ffn-load-production ISA：C64 expand的20次global load全部在第一条MMA前发出，C128为34/40，已有编译器预读；输出hash一致但更慢，不采用； 进一步发现09-17的MH FFN fragment-layout失败实验只覆盖C64/C128。预读尝试不采用；

证据：`Development/history/DevHistory-full-20260923.md:2581`。

### 2026-09-19 21:57起：C32半精度buffer地址正确，收益不足以采用｜性能负/未采纳

结果/裁决：在c256-frag-production基线上，仅把RawMapped半精度输入（入口/残差）及HalfOutput写出换为raw_buffer_load/store_b16，保留坐标、边界、舍入和数组布局；初版照搬09-17仅编译过的buf_probe常量0x27000，720首组输出错误（未装到游戏）； 结论：写侧无收益；读侧900有约0.03ms的小变化，但1080接近零，整体收益受漂移影响、没有清楚的跨档优势，本轮不合入生产。数据c32buf-*，游戏保持用户刚验证的C256版本，README未发布项不添加未采用实验。

证据：`Development/history/DevHistory-full-20260923.md:2597`。

### 2026-09-20 01:36｜RE9后置首版GetBackBuffer AV｜接口失败后修复

结果/裁决：用户授权直接退出游戏并继续兼容，由助手处理退出/启动，无需反复请用户退出。失败停止处理并保留可能在途资源。菜单连续1200帧（约30fps菜单上限）无提交错误，截图正常。

证据：`Development/history/DevHistory-full-20260923.md:2662`；`Development/RE9/results/present-first-frame.json`。

### 2026-09-20 07:33｜撤回2K后置支持｜需求变更，非性能负

结果/裁决：用户否定先超分再缩小的后置流程，要求关闭游戏超分、最大1080P，超范围报错且不工作。撤回RE9_POST_1440构建开关，统一外部帧上限1920×1080，超过显示ERROR MAX 1920X1080并在创建/执行HIP前返回；准备验证2K拒绝、1080关闭超分可用，以及开启超分的错误提示。上条2K实测作为被撤回实验保留，后续专用包以本条契约为准。1080配置配无边框仍会产出桌面2K，因此配置脚本最终用WindowMode=Normal（两处）及NormalWindowResolution=(1920.000000,1080.000000)，不是无效的Windowed值。

证据：`Development/history/DevHistory-full-20260923.md:2688`。

### 2026-09-21 01:43起：C128融合核内QKV共用输入片段｜性能负/未采纳

结果/裁决：继续小步无损方向，查看C128 FFN/收缩/投影/QKV后，先试QKV阶段8个i2输入片段常驻寄存器供三次投影共用，少读qfeature；干净160帧ABBA900 13.179301→13.181824ms、1080 18.674586→18.675410ms，没有收益，不采用。不重复微小无收益轮次。

证据：`Development/history/DevHistory-full-20260923.md:2786`；`Development/results/c128-qkv-input-reuse-20260921`。

### 2026-09-21 15:42起：C128平衡tile接入完整FFN/QKV，量出收益缩水位置｜性能负/未采纳

结果/裁决：按用户继续深入要求，把M32×N32展开接到独立融合核：512线程覆盖32行，收缩/投影/QKV切回两个16行×8列wave组，原K累加/FP8舍入顺序不改，六个兼容入口用独立_m32符号。 补720控制抓到实验构建漏HIP_PREPACKED_WEIGHTS：旧host+新module及禁用M32都复现，属于通用fallback读错权重，不是新分工错误。脚本experiments/c128-fused-row-reuse，完整CSV/hash/构建错误交叉证据/报告results/c128-fused-row-reuse-20260921。

证据：`Development/history/DevHistory-full-20260923.md:2853`；`Development/results/c128-fused-row-reuse-20260921`。

### 2026-09-21 16:17起：C128全阶段权重重用，定位到驻留上限抵消｜性能负/未采纳

结果/裁决：按用户下一步要求做三种独立候选，均双架构编译。后两种已慢，不扩测/不采用。③LDS16384、VGPR94、编译器Occupancy16，实际4组/32wave，驻留改善仍更慢。

证据：`Development/history/DevHistory-full-20260923.md:2861`；`Development/results/c128-all-phase-reuse-20260921`。

### 2026-09-21 18:55｜C64 empty-tile扩展｜性能负（C256成功项不列负）

结果/裁决：基线为已含C128快路径的147421f，分别仅扩C64或C256。补千帧1080仍慢0.018243，没兑现整网收益，暂不采用，不把单核胜利硬推到主线。

证据：`Development/history/DevHistory-full-20260923.md:2875`；`Development/results/mh-empty-tile-20260921`。

### 2026-09-21 22:09起：补齐C32输入/尾部与真实ViT FFN台阶，分别存档｜性能负/未采纳

结果/裁决：按剩余三组计划，C32保留原导出并增加输入/尾部运行时1/2/4次探针，900/1080×prefix/首chain/post，共120个计时槽完整产出逐位同，12次首尾原始网络FP32同。拆开后的等价完整路径两档都更慢，不作为优化采用。

证据：`Development/history/DevHistory-full-20260923.md:2907`；`Development/results/c32-edge-cost-20260921`、`Development/results/network-cost-summary-20260921`、`Development/results/vit-ffn-ladder-20260921`。

### 2026-09-21 23:37起：同launch扩到八份，分离输入后交错收益大多消失｜性能负/未采纳

结果/裁决：按上一轮计划，11bc0ad基线上保持每份真实ViT展开，M1/M2×整份优先/交错×1/2/4/8份，独立输出。初版M1漏Frag=true被输出检查抓住，拒收留档；1080每份M1整份优先36.085→35.166μs，仅约2.5%；交错36.254→31.748，M2交错40.181→30.316。 补八份同核ABBA，仅将输入stride从0改为T×1024，输入数值相同、预先分配/上传相同缓冲，权重/输出/grid相同。M1整份34.905→35.007μs基本不变，交错31.613→34.788；M2整份33.953→34.002，交错30.549→36.567。但M2显存中位2505→2238MHz等状态一起变，不能全归为缓存等待。

证据：`Development/history/DevHistory-full-20260923.md:2945`；`Development/results/vit-work-scale-20260921`。

### 2026-09-22 00:04起：固定尺寸基线重测A连续片段布局，整网持平不采用｜性能负/未采纳

结果/裁决：用户继续缺口研究。900 12.742735→12.741888ms、1080 17.917179→17.915888，约1μs差远小于漂移，视持平不采用。

证据：`Development/history/DevHistory-full-20260923.md:2953`；`Development/results/vit-input-layout-20260922`。

### 2026-09-22 05:18：审阅网友PR #5，同帧桥接思路可用，但原样合并有回归｜性能负/未采纳

结果/裁决：用户提供PR5要求查看。head HIP入口因bridge五诊断字段被删而失败，codec独立include因CompileNativeShader声明丢失失败。异步入口需补失败状态和提交顺序契约。

证据：`Development/history/DevHistory-full-20260923.md:2969`。

### 2026-09-22 11:48–12:04：ViT平衡tile与A布局交叉对照，少逻辑加载不等于少取数代价｜性能负/未采纳

结果/裁决：回到算力缺口主线，按上轮计划试M32×N32：M640时四累加片段/wave数/有用矩阵工作不变，K64的global_load_b64从20→16、WMMA仍16；u1更慢，列优先略缓解。900档12.777513→12.786306ms、1080档17.943379→17.940298，未观察到稳定净收益，不采用。

证据：`Development/history/DevHistory-full-20260923.md:3030`；`Development/results/vit-balanced-tile-20260922`。

### 2026-09-22 12:23–12:40：ViT八累加片段交叉A/B重用、布局和展开，转向真实网络时序测量｜性能负/未采纳

结果/裁决：按上轮方向，16×128(A1/B8)与32×64(A2/B4)同八累加片段/有效矩阵量，M640同1280wave，M400尾部BM1。补u4→u2配对80槽：16×128 VGPR136→118/117却更慢；900/u4 12.790733→12.777526，u2 12.806486→12.802083，0～0.103%不足形成稳定结论，不采用。

证据：`Development/history/DevHistory-full-20260923.md:3038`；`Development/results/vit-eight-accumulators-20260922`。

### 2026-09-22 12:50起：子代理离线排查网友RE9无效果/超宽卡死反馈｜性能负/未采纳

结果/裁决：用户要求由子代理分析temp/bug20260922，减少主上下文占用。日志明确本帧PrepareFrame在codec D3D12 PSO创建处E_FAIL(0x80004005)，1280×544、960×544、1136×640均失败，不支持输入超限归因；

证据：`Development/history/DevHistory-full-20260923.md:3047`。

### 2026-09-22 13:47：补充验证RE9失败后的尺寸恢复边界｜正确性失败/未准入

结果/裁决：用户追加启动2560×1080、改回不恢复及“只要失败就再也起不来”。原因是先创建1080 bridge、codec晚检查失败仅清局部对象；未发现runtime持久化几何/失败标志；shader缓存无完整性校验、非原子写入且PSO失败不重编是可持续故障风险，尚无网友缓存实体或对照证实。

证据：`Development/history/DevHistory-full-20260923.md:3059`。

### 2026-09-22：修复RE9输入越界检查顺序与失败后恢复｜正确性失败/未准入

结果/裁决：在TheAutomatic原宿主的producer/HIP/consumer拆分设计上补边界适配：真实输入尺寸/格式在改network geometry与创建HIP前检查，超限明确返回原始SR；初始化异常同步后回滚新bridge+codec，未退休帧禁止覆盖，宿主INVALID_ARGUMENT不再触发session销毁。 9070空闲检查后独立GPU测试10组12提交帧：fresh、超限→有效、有效→超限→有效、合法1080输入注入PSO失败→720恢复、旧帧录制后拒绝覆盖；

证据：`Development/history/DevHistory-full-20260923.md:3065`。

### 2026-09-22 16:30：C32收缩后成对编码及保留half位元，无稳定收益｜性能负/未采纳

结果/裁决：按算力缺口主线，把收缩后16个结果的RTZ配对、FP8配对、两者合用拆成三候选。整网两档均稍慢约0.015～0.037ms，不采用。整网1080省0.011ms、900多0.017ms，无稳定收益。

证据：`Development/history/DevHistory-full-20260923.md:3091`；`Development/results/c32-pair-encode-20260922`。

### 2026-09-22 23:20：CU 模式按工作组大小逐核对照，无收益，此线关闭｜性能负/未采纳

结果/裁决：基线 fence-modules（当前生产）。整网 ABBA 每测试 8 槽，逐位同：1080 +0.008/+0.051/−0.017ms，900 +0.027/+0.050/+0.008ms，除 900 pair2 略差外槽间均交叠，频率同。结论：CU 模式收益是 C32 结构特有（4 wave LDS 密集交换 + 权重常驻 L0），不是工作组大小规律；

证据：`Development/history/DevHistory-full-20260923.md:3169`；`Development/results/fence-cu-size-20260922`。

### 2026-09-23 00:05：C32 注意力 ex/prob 寄存器化，逐位同但寄存器压力抵消收益，不采用｜性能负/未采纳

结果/裁决：分数 WMMA 与行和"乘 1 矩阵"WMMA 均对调操作数，ex/prob 全留寄存器，K/V 仍在 LDS；不采用，后续可试限驻留或按 key tile 流水。

证据：`Development/history/DevHistory-full-20260923.md:3187`；`Development/results/c32-register-attention-20260922`。

### 2026-09-23 01:35：字节链 + 向量化 staging（HIP_C32_BYTE_CHAIN），正式回归中｜被修复或替代/保留负分支

结果/裁决：由 phase-trace 的 staging 拆分出发。中途 r2 因 prepare 断言失败实为字节链重测（记录保留）。 单纯byte链无稳定收益；后加向量staging后采用，不混为整路线负。

证据：`Development/history/DevHistory-full-20260923.md:3197`；`Development/results/c32-byte-chain-20260923`。

### 2026-09-23 03:30：mh_fused 注意力核打点 → 三函数体 ex/prob 寄存器化（C512 HIP_MH_REGISTER_EX（C64..256成功不在此负项）），prod5 回归中｜性能负/未采纳

结果/裁决：mh-phase-trace：c256_attention_project 每 wave 约 50k 周期，两批注意力约 42%、残差/对角 21%、投影 17%、barrier 12%； 同法推到 C512 注意力核 mh_attention_fused_fp8_out（ex 8.4KB 删除、同步 3→1）：三份 ABBA 逐位同但 ±0.01ms 交叠，无收益——该核不受 LDS 驻留限制。不采用，results/mh-register-c512-20260923。

证据：`Development/history/DevHistory-full-20260923.md:3209`；`Development/results/mh-register-c512-20260923`。

### 2026-09-23 08:20：ViT 融合注意力寄存器化 + V 转置，逐位同但 ≤0.03ms，不采用｜性能负/未采纳

结果/裁决：25 个/wave 的 LDS 往返与 barrier 去除（r1）±0.02ms 交叠；核太小，不采用。

证据：`Development/history/DevHistory-full-20260923.md:3221`；`Development/results/vit-register-attention-20260923`。

### 2026-09-23 09:00｜split_projection_frag转置尾声｜性能负（请求规则纠正未救性能）

结果/裁决：每 lane 64 条标量写并成 3 条向量写、残差 32 标量读并成 8 条 b128，整网 ±0.02ms 偏差。

证据：`Development/history/DevHistory-full-20260923.md:3225`；`Development/results/deep-transposed-epilogue-20260923`。

### 2026-09-23 09:40：ffn_fused_c256 barrier 审查，BatchNorm=true 合并两对同步无收益｜性能负/未采纳

结果/裁决：8 个 barrier 中 4 个真依赖，QKV 归一化两 part 各一对可合并（函数体已有 BatchNorm 分支）。翻模板参数试：barrier 8→6，LDS 21.6→39KB、驻留 12→10，整网 ±0.02ms 交叠，逐位同，不采用。

证据：`Development/history/DevHistory-full-20260923.md:3229`；`Development/results/mhfast-batchnorm-20260923`。

### 2026-09-23 10:30：ffn_fused_c256 宽权重片段（打包器 + 四循环步长 32），逐位同，约 −0.03ms，未采用｜性能负/未采纳

结果/裁决：权重片段读 162→94（b128 68），两轮各三份 ABBA 剔除进程首槽冷启动后 1080 −0.03～−0.05、900 −0.01～−0.03ms。

证据：`Development/history/DevHistory-full-20260923.md:3233`；`Development/results/mhfast-wide-frag-20260923`。

### 2026-09-23 08:05：ffn_fused_c256 VGPR 封 96，逐位同，+0.06/+0.07ms，不采用｜性能负/未采纳

结果/裁决：waves_per_eu(16,16) 让两个 c256 frag 核降到 96 VGPR、各溢出 8～9 个寄存器，驻留 12→16 wave/SIMD 反而更慢，三份复现槽不交叠。

证据：`Development/history/DevHistory-full-20260923.md:3249`；`Development/results/mhfast-vgpr-cap-20260923`。

### 2026-09-23 09:50：注意力残差特征读提前，逐位同，+0.07～0.12ms，不采用｜性能负/未采纳

结果/裁决：寄存器版 12 wave/SIMD 已盖住残差读延迟，提前读反而和入口 staging 抢队列。旧（LDS 版）阶段账的份额不能直接沿用。

证据：`Development/history/DevHistory-full-20260923.md:3257`；`Development/results/mh-hoist-residual-20260923`。

### 2026-09-23 10:10：注意力投影输出转置，逐位同，+0.05～0.16ms，不采用｜性能负/未采纳

结果/裁决：原版输出循环是运行时滚动循环（静态 2 条全局写），转置后展开成 8 条写 + 地址算术，指令 +20%，c256 VGPR 118→138 掉一档驻留。

证据：`Development/history/DevHistory-full-20260923.md:3269`；`Development/results/mh-transposed-project-20260923`。

### 2026-09-23 10:10：1b wave 内 QKV 归一化（非逐位），转置之上再 −0.18/−0.14ms（1.1%）｜正确性失败/未准入

结果/裁决：part 0/1 每 wave 一个 head 32 列，平方和 lane 内 16 项 + 一次 bpermute，无 LDS 无 barrier。注意力投影输出转置反例：滚动循环别转置。

证据：`Development/history/DevHistory-full-20260923.md:3273`；`Development/results/mhfast-wave-norm-20260923`。

### 2026-09-16：重新检查HIP event计时可靠性｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：新增check_event_timing.cpp：HIP7 runtime实际报告70260201，同一stream固定vit_pack_input、1/8/64/256次重复各5轮，输入零/输出先填255，末尾检查全输出为零。两个网络脚本显式检测INVALID并返回失败，未将失败计时用于优化结论。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:1916`。

### 2026-09-21 19:57起：当前网络耗时账，C32约35%，排除逐核事件的测量膨胀｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：用户要求依据实测算力上限定位网络消耗。全234次dispatch逐核插begin/end事件虽时间单调、输出正确，却将约12.8→23.4ms、18.2→28.8ms，不能作真实分项账；初版稀疏复用事件及极短区间还偶发负值（cut0=-0.016980ms），拒收，不裁零。每区段左右前缀8轮ABBA求差，15段覆盖234调用。极小段的跨帧ABBA差值有负样本，保留误差，不能精排。核对C32实际运行HSACO与汇编源旁的binary哈希相同C381FCE5…：入口/链/post已有233/243/338个静态F32算术操作在VOPD里，还各有数百转换和LDS操作； 工具experiments/network-timeline，证据results/network-timeline-20260921（明确保留拒收数据及原因）。另可做真实ViT FFN尺寸的GEMM→完整算子台阶，别重做已小的噪声或盲提occupancy。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:2889`；`Development/results/network-timeline-20260921`。

### 2026-09-22 12:57–13:19：真实网络时序的ViT测量，明确短段HIP事件不能作依据｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：恢复算力缺口主线，比较原核与32×64/片段A/u2。加end同步后59/160非正，立即同步起止并逐帧读取仍61/160，保存原值拒收。 再放大标记首次pack→block38，5120区域事件有效。1080区间2.051730→2.051415ms、900档1.272712→1.268393；无标记整网17.869095→17.865163、12.796713→12.815408ms，仍无足够采用证据。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:3053`；`Development/results/vit-in-context-20260922`。

### 2026-09-22 00:17起：同读取量的计算密度探针，稳定1080从约150到299TF｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：按缺口主线，真实ViT展开A/B每K64仍20条global_load_b64，增加寄存器内独立WMMA为16/32/64/128条，最终网络输出保持原累加链。8倍实际更慢，不是游戏提速。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:2961`；`Development/results/vit-compute-density-20260922`。

### 2026-09-23 00:40：C32 核内时间戳，首份每 wave 阶段周期账｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：三热核共享体加 9 个 SHADER_CYCLES 打点 + barrier 内周期累加，lane 0 写 12 u64 到全局缓冲；结论：输入 staging（gather+FP8+字节写 LDS）是 C32 最大单项，注意力 LDS 本就小（解释寄存器化无收益）。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:3191`；`Development/results/c32-phase-trace-20260923`。

### 2026-09-19 18:43起：逐块定位到900档解码尾部漏派发，修复完成｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：隔离探针按原字节格式抓中间张量，额外同步前后最终hash一致，未掩盖反例。整链对比已确认字节流反例消失：900/1080不同输入与seed六组、720两组首尾全同； 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:2504`。

### 2026-09-20 22:30起：从AttExp选入三项有效改动到main｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：用户明确要求切main，只取精确流式attention、R3自适应复用、静止输入延长缓存/融合提交。main原已带建分支前KV均值实验084f8df，另用d29211a撤回；失败代码/全量报告留AttExp，将来按需回读，不能重复计算收益或把未测试C64/C128/C256写成失败。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:2722`；`Development/results/main-vit-reuse-20260920`。

### 2026-09-19 16:58起：C32注意力细分与指数值寄存器复用候选（约−0.19ms）｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：1080细分消融：基线21.1235/21.1335ms；数据并入Development/results/1080-20260919（regex-indexed为失败首版、regex为展开版、regex-validation为多档检查）； 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:2425`；`Development/results/1080-20260919`。

### 2026-09-19 15:37～15:55：Magpie测试包回归与内核文件加载报错｜环境测量无效/内部失败已修

改动/基线：对照协议或已知失败路径。

结果/裁决：用户回测优化效果不明显；网友日志 `C:\Users\lmxxf\Downloads\logs202009131113` 同样27次报该错误，但未记录安装路径，应先检查完整解压和英文路径，不能直接判同因。 教训：修复后的成功证据不倒灌成旧失败数据有效；无新失败优化不作为永久封禁。

证据：`Development/history/DevHistory-full-20260923.md:2389`。

### 2026-09-23 23:00～09-24 00:05：《卧龙 2》Alpha Demo 试装（网友反馈"用不了"）｜内部失败/被修复或替代

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：游戏在 9070 机 `C:\Program Files (x86)\Steam\steamapps\common\Wo Long 2 Wings of Ember Alpha Demo`（Katana 引擎，Streamline 2.9 接 DLSS，自带 FSR）。`[Inputs] EnableFfxInputs=false` 后钩子每帧触发，网络初始化成功（1580×888 → 900 档）。

证据：`Development/history/DevHistory-full-20261006.md:240`。

### 2026-09-24 00:30：C32 转置尾部（out 并宽）null｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：WorkingPlan 的"C32 产出端并宽"试完：FFN 收缩 + prefix + 投影全部转置，ffn8/scratch.ex/out/pre16 的窄写并宽。原因：残差初始化和缩放访问改成 lane=行后 LDS 读变差，投影缩放 2→16 读，chain/finish VGPR +15。

证据：`Development/history/DevHistory-full-20261006.md:252`；`Development/results/c32-transposed-tail-20260924`。

### 2026-09-24 01:00：探索 1 关闭——RDNA4 没有直达 LDS 的读取｜资源/硬件能力失败

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：318 账本里 staging 31% 想用 `global_load_lds`（显存直落共享内存）省掉寄存器往返和 LDS 写指令。

证据：`Development/history/DevHistory-full-20261006.md:256`。

### 2026-09-24 07:30～18:30：《赛博朋克 2077》2.31 试装——三层坑，最后一层是别名瞬态资源｜内部失败/被修复或替代

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：常规 OptiScaler 0.29 包装进 `bin\x64`（`deployments/cyberpunk-20260924/install.ps1`，覆盖游戏自带的 5 个 xess/ffx dll，备份 `_dlss5_backup`）。网友"用不了"逐层： 1. 和卧龙一样：游戏自带 FSR dll，OptiScaler 走 Detours 跳板 → `[Inputs] EnableFfxInputs=false` + 优先钩 upscaler dll 的 addon。

证据：`Development/history/DevHistory-full-20261006.md:280`。

### 2026-09-24 20:00～20:50：强度外推试看、Google Drive 链接、网友"统一宿主"补丁审查｜未验证暂停/代码审回归

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：- 用户要看"最高强度"：`DLSS5_STRENGTH` 上限放到 3（>1 沿 lerp 外推，纯诊断），剑星/2077 flags 临时写 2,1；

证据：`Development/history/DevHistory-full-20261006.md:321`。

### 2026-09-24 21:10～21:50：2077 三方对比（默认 / OP / Magpie 0.29）——红偏来自颜色转移，改成按 exe 查表 1,0｜内部失败/被修复或替代

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：用户同机位截四张（机位微差，逐像素对不上，看全局统计 + 等倍裁片； - 落地：`native_game_codec.h` `LegacyParameters` 里 `DLSS5_STRENGTH` 缺省或 `auto` → 按 exe 查表（Cyberpunk2077.exe → 1,0，其他 1,1），写 `event=strength` 到 oneshot 日志；

证据：`Development/history/DevHistory-full-20261006.md:328`。

### 2026-09-25 06:47～07:20：剑星切 DLSS 档位后 DLSS5 消失——钉死的 FSR 上下文｜内部失败/被修复或替代

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：用户装 prod8 后从"平衡"切"质量"/"DLAA"，效果没了，切回也没了（进程内永久失效）。日志：06:47:00 切 DLAA 时 OptiScaler 建了新 FSR 上下文（1000002）并释放旧的（1000001），我们的 pre-upscale 日志从那一刻起一行都没有——不是回归失败，是 `dispatch_core` 在 `DLSS5_FIT_INPUT` 分支里把 `fit_context` 钉在第一个上下文上，"直到它被销毁"，而销毁钩子只挂在 upscaler dll 的 `ffxDestroyContext` 上；(3) restart 预算只在失败（phase 5）时消耗，几何/队列变化无限次。

证据：`Development/history/DevHistory-full-20261006.md:354`。

### 2026-09-25 07:20～08:20：C512 链旗子（不采用）与 0.30 打包｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：用户定"这一轮做完就打包"。**不采用**，实验和结论归档 `results/pdl-c512-20260925`；坑：实验 host 打在生产头文件上，生产 ctor 只在 opt.pdl 时分配旗子缓冲，timeline 的 flags.txt 没写 PDL=1 → 对空指针算偏移 launch 失败。

证据：`Development/history/DevHistory-full-20261006.md:361`；`Development/results/pdl-c512-20260925`。

### 2026-09-25 22:21 起：自主跑完 post70 三种输入候选，均不采用｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：Zero 明确授权：DLSS5 优化的耗时编译、实验、回归、分析由 agent 自行执行，不再让用户手动跑命令。后两者无稳定收益，不采用。不是假 null，也不能因此宣布整个 staging 已到极限。 同时补读 ViT 旧 page/pitch/groups 记录：完整 4MiB 权重的 padding/xor 收益很小，随机 stride 重排和多 wave 合组更慢。

证据：`Development/history/DevHistory-full-20261006.md:381`；`Development/results/c32-post-input-20260925`。

### 2026-09-25 22:33 起：ViT 局部二维工作组编号，六候选无稳定收益｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：`HIP/experiments/vit-group-order`：真实展开/收缩各做 GM2/4/8，保持每组一 wave、原累加顺序和输出地址，只让相邻 token tile 接连访问同一列权重。同期同体异名对照单次约−0.011～+0.036ms，未获稳定收益，六个均不采用。

证据：`Development/history/DevHistory-full-20261006.md:392`；`Development/results/vit-group-order-20260925`。

### 2026-09-26 07:05～07:28：C512 一头一 wave（attention/projection-only）——逐位但 null，不采用｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：逐族账里 C512 是唯一没动的族，照 C64/C128 做法试一头一 wave。900/1080 各 3 组 200 帧 ABBA（对照 = prod8 + wave-owned 46 + PDL=1）全部槽首尾逐位、16 帧动态历史逐位、13 次/帧替换（含 identity 块）：**900 −0.017 ms、1080 −0.019 ms，噪声内，null**。

证据：`Development/history/DevHistory-full-20261006.md:544`；`Development/results/c512-wave-20260926`。

### 2026-09-26 09:00～10:10：生产基线 900 历史偶发不一致——PDL 1/0 共 350 次零复现，不调默认值｜环境测量无效/未复现

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：c512-ffn 回归里那次"基线第 8～11 帧不一致"：回归的 `extra-900-history-False` 本身就是离群那次（多数结果 frame8 `F9FABBE4…`），异常时帧时抖到 18～23ms（有外部 GPU 负载），第 8 帧整幅微差、热点为网络 x≈770～1340 的全高纵带，后续帧由历史带下去。

证据：`Development/history/DevHistory-full-20261006.md:555`；`Development/results/pdl-race-20260926`。

### 2026-09-26 10:35～10:45：剑星一直开着 ViT 自适应复用；修正"与 Daniel 打平"｜环境测量无效/未复现

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：核 flags 发现剑星早就是 `DLSS5_VIT_ADAPTIVE=1`（REUSE_HOTKEY=1，F8 切换；

证据：`Development/history/DevHistory-full-20261006.md:584`。

### 2026-09-26 10:50～12:30：ViT attention 四候选全 null；C32 prefix 分摊 1080 −0.03ms（宏并入、默认关）｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：`experiments/vit-attn-c32-input`（m32-sweep 派生，基线含 VIT_PROJ_N64），五候选均逐位 + 动态历史通过。ViT attention：m32 两 query tile 共用 K/V +0.08ms（并行度减半），V 转置写/8 字节读 −0.01，QK 操作数对调去 LDS 转置与 barrier ±0.03，二者叠加 ≈0——不卡访存量、V gather、同步链，卡在逐元素 exp/fp8 打包的 VALU 依赖延迟，逐位下无结构空间，不采用。

证据：`Development/history/DevHistory-full-20261006.md:597`；`Development/results/vit-attn-c32-input-20260926`。

### 2026-09-26 12:05～12:20：C32 finish/prefix 尾部合并遍历——null｜性能负/未采纳

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：`CW_TAIL_QUAD`（`hip/wave_owned_c32.inc`，默认 0）：2×2 块一遍遍历，4 次 LDS 读同时供 main 与 down（原 64+64 次），写地址改 32 位偏移；另设同源对照测噪声）：900/1080 均 −0.010ms（−0.10%/−0.07%），同源对照噪声带 −0.013～+0.024ms，**不采用**；

证据：`Development/history/DevHistory-full-20261006.md:615`；`Development/results/c32-tail-20260926`。

### 2026-09-26 18:30～18:45：HIP 导入的共享缓冲区永不归还——桥接层按档位池化｜内部失败/被修复或替代

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：受 3z 更新日志启发实测：裸探针按桥接顺序导入/映射/释放 40 次，显存与私有内存各漏 ~3 GB，整套缓冲区一字节不还；池化后第一轮三档后平台期（显存 ≈2530、私有 ≈310 MiB 不再涨）；

证据：`Development/history/DevHistory-full-20261006.md:623`；`Development/results/vram-leak-20260926`。

### 2026-09-26 19:50～20:30：RE9 runtime 可配置 + 接入 0.31 新核（A1）｜内部失败/被修复或替代

改动/基线：该归档scope；固定输入筛选不冒真实游戏收益。

结果/裁决：TheAutomatic/ouco 反馈 RE9 包无法配置开关。顺带修 PR #9 删 include 导致的 add-on 编译失败（`native_preblock_runtime.h` 补 `<cmath>`）；部署脚本安装→回滚验证。

证据：`Development/history/DevHistory-full-20261006.md:635`；`Development/results/re9-runtime-flags-20260926`。

### 2026-09-26｜QKV尾部LDS并b128｜收益不足未采纳

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：900/1080约仅−.04ms，未选；32tokenQKV与mix是采用项。

证据：`Development/results/c512-ffn-20260926/README.md`。

### 2026-09-26｜QKV48/64token与projection32token｜性能负 / QKV溢出

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：QKV48/64溢出更慢；projection32token +.01/+.02ms null。

证据：`Development/results/c512-ffn-20260926/README.md`。

### 2026-09-26｜C256 FFN M32 LINE_STORES0/1｜性能负

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：1080+.163/+.036ms；118/181VGPR、43/60KB LDS，无收益。

证据：`Development/results/m32-sweep-20260926/README.md`。

### 2026-09-26｜ViT QKV 32×64、32×32、16×64｜性能负 / 跨档不一致

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：900/1080分别+.170/+.136、+.098/+.179、+.068/−.051ms；第三两档不一致，均否。

证据：`Development/results/m32-sweep-20260926/README.md`。

### 2026-09-26｜ViT proj16×32、32×32、手写N64特化｜收益不足，被更优模板替代

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：16×32−.043/−.229，32×32−.016/−.167，均不如已选16×64；手写98VGPR772指令只有−.044/−.225，不如模板−.101/−.281，采用实测模板不是更少指令版。

证据：`Development/results/m32-sweep-20260926/README.md`。

### 2026-09-26｜decoder16×64、与ViT N64叠加｜性能负 / 跨档不一致

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：单decoder+.114/+.139ms；组合+.009/−.155ms，否；上采样散写尾部串行变重。

证据：`Development/results/m32-sweep-20260926/README.md`。

### 2026-09-21｜u1/u2/u8/u16、4/8wave合组、列优先、M2｜性能未采纳；900排名受DVFS污染

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：1080约36.4μs基线：54.4/41.0/37.7/37.5，4wave36.2、8wave38.4、列优先40.8、M2 40.3；未填缺口。90026～44μs/DVFS漂移的候选排名拒收。

证据：`Development/results/vit-gap-controls-20260921/README.md`。

### 2026-09-25｜双stream/event/GPU_MAX_HW_QUEUES｜无有效并发收益，后同流anyorder替代

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：两流不并发，事件110～180μs/对，HW_QUEUES4无效；同流anyorder后来有效，不能否定它。

证据：`Development/results/pdl-chain-20260925/README.md`。

### 2026-09-25｜PDL protocol ordered、仅FFN、仅attention、group旗｜性能负；后每wave旗版本采纳

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：普通PDL协议+.25～.29ms；NO_WAIT+.10、wait+.15～.19；任意序只FFN/只attention各+.10，两端group旗≈0。最终每wave旗版本才成功。

证据：`Development/results/pdl-chain-20260925/README.md`。

### 2026-09-25｜旗子环形跨尺寸复用/空指针提前return｜同步正确性失败，已修复

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：混用不同尺寸tile计数导致死锁；helper空指针提前返回任意序三跑三次LaunchFailure，撤掉两跑过；后改按kind/channel/lattice计数+保张量引用，已修，不能照抄旧helper。

证据：`Development/results/pdl-chain-20260925/README.md`。

### 2026-09-25｜spin readsteadycounter计时｜计时实现无效，已更换时基

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：消息全GPU串行，200μs等待两次poll到期；改SHADER_CYCLES才有效，旧消息拥塞曲线不可作并发性能。

证据：`Development/results/pdl-chain-20260925/README.md`。

### 2026-09-22｜固定总wave多wave合组/编号｜性能负

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：合组更慢；不是减少dispatch数，也不是证明物理硬件调度必按组编号；独立report保各配置数据，未生产采用。

证据：`Development/results/wmma-groups-gap-20260922/README.md`。

### 2026-09-22｜page/span64/cache counter外推｜测量归因无效，不作生产候选

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：特殊span64不是完整4MiB真实权重案例；不同捕获一致性未通过的指标不拿来定量归因/挑数，未生产采用。

证据：`Development/results/wmma-page-gap-20260922/README.md`。

### 2026-09-22｜完整权重padding/xor与随机stride重排｜padding/xor收益不足；随机stride性能负

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：完整4MiB padding/xor仅小收益，随机stride更慢；不可用特殊小span微核外推全网。

证据：`Development/results/wmma-pitch-gap-20260922/README.md`。

### 2026-09-22｜额外WMMA/asm控制｜诊断扰动 / 有意额外工作，非优化收益

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：8倍额外算术虽TF上升但实际更慢；原asm控制零额外MMA即36→50～53μs扰调度，不能归因原核，不作优化。

证据：`Development/results/vit-compute-density-20260922/README.md`。

### 2026-09-21｜同launch1/2/4/8副本交错/M2｜首布局失败已修；分离输入性能负

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：初版M1漏Frag输出错拒收；修后共输入交错表面快，分离输入M1 31.613→34.788μs、M2 30.549→36.567，收益大多消失；非游戏提速。

证据：`Development/results/vit-work-scale-20260921/README.md`。

### 2026-09-22｜连续短event测真实ViT｜计时无效，已替代

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：非单调/负事件拒收，不能裁0/把短event当原生阶段账；后wall/sync多前缀ABBA替代。

证据：`Development/results/vit-in-context-20260922/README.md`。

### 2026-09-22｜片段布局R1/R2/R4重复上下文｜正常R1收益不足，重复放大不外推

改动/基线：独立报告中的真实输入微核/整网范围，按报告区分。

结果/裁决：额外工作放大优势，正常R1约仅.01～.03ms且受漂移，仍不采用；重复收益不外推正常8次调用。

证据：`Development/results/vit-repeat-context-20260922/README.md`。

### 2026-09-08：512融合Wave FFWD无收益，定位入口head低并行投影｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- native_wave_split_ffwd保持512混合→F→八组64/256/64 gate/收缩，各K32 H；暖708.7535321、末5帧707.392882ms，不优于706.43771ms，未采用。

证据：`Development/history/porting-worklog.md:2986`。

### 2026-09-08：F16累加和首层拆分实验未通过采用标准｜正确性失败

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- matrix_half_accumulate使用F16 accumulator MultiplyAccumulate代替每K32软件H。session24540 exit0，全8847360值finite，但438687不同、max2/MAE0.00118217093/RMSE0.0163191，0.697196ms相对0.732240ms收益小，不采纳。最终范围0.10000008..0.8999999、均值0.4998785，对参考MAE0.0313366/max0.1968994/RMSE0.0396041，接近原输入而不是已验神经结果。

证据：`Development/history/porting-worklog.md:3002`。

### 2026-09-07：C64矩阵15帧数值通过但算子回退，不推广｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- C64开放显式MATRIX_C64开关，shader泛型CHANNELS64；

证据：`Development/history/porting-worklog.md:3103`。

### 2026-09-07：矩阵C128整网数值通过，性能存在非局部回退｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 三matrix shader以MATRIX_CHANNELS参数化（默认256），host权重块数/K块数/输入步长/dispatch随channels变；C128 encoder9_14仅68.21856→71.92095、tail57_61仅55.63836→59.04131ms，主要增量在未改的ViT stage3每层+14～22ms。 - 下一步检查显存预算、资源驻留和更长暖机，矩阵QKV原始缓存会增加常驻内存但目前没有证据证明已发生显存溢出。

证据：`Development/history/porting-worklog.md:3125`。

### 2026-09-07：C32位元量化无收益，保持关闭｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- preblock_attention_core保留LegacyF，以严格DLSS5_TEST_FAST_C32_FP8宏启用已有NativeFastFp8，非finite回旧路径；暖轮1488.7480575ms，慢于1469.7617425ms有效基线，未采纳。

证据：`Development/history/porting-worklog.md:3197`。

### 2026-09-07：普通shader替代路径——ViT矩阵驻留无收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 自动goal不是换驱动许可，因此不安装驱动；

证据：`Development/history/porting-worklog.md:3203`。

### 2026-09-08：共享概率暂存实验，整体无收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 新增NATIVE_SHARED_PROB实验分支，仅-SharedProb显式编译开启；局部有改善但整网不优于586.273701ms，故不纳入有效配置、不宣称提速。

证据：`Development/history/porting-worklog.md:6114`。

### 2026-09-08：C64收缩预载实验，无整网收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 新增NATIVE_PRELOAD_CONTRACT分支，原每K32读512 half和两次barrier改为一次读取完整16×256 half，然后矩阵依原K32顺序读取共享区。C64共享容量1KiB→8KiB，不增整图buffer，H/F与权重不变。

证据：`Development/history/porting-worklog.md:6120`。

### 2026-09-08：矩阵版C32权重驻留复测，无可靠收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 早期驻留实验发生在不同标量实现，本次在当前multihead-av/post-detail有效矩阵基线复测既有RESIDENT_C32_WEIGHTS=1。没有可靠整网收益，不能把不同轮次591→587当确定提速，继续默认关闭。

证据：`Development/history/porting-worklog.md:6140`。

### 2026-09-08：C32 Wave浮点中间矩阵行padding候选｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- NATIVE_PAD_WAVE_C32_SCORES=1把复用scores区的32列行跨度改33，64列行跨度改65；

证据：`Development/history/porting-worklog.md:6146`。

### 2026-09-08：Wave C32注意力位元FP8复测，性能倒退｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 既有NativeFastFp8此前只在更早标量路径测试；

证据：`Development/history/porting-worklog.md:6153`。

### 2026-09-08：C32硬件half转换候选，数值未对齐｜正确性失败

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- NATIVE_C32_HARDWARE_HALF显式分支仅将C32 attention H替换为f16tof32(f32tof16(v))，NativeHalfSquarePair及half_add_preserving_midpoint补偿仍保留，舍入位置不改。 - DXC通过，PID35852首帧正确性检查失败并exit1； - 证据release/native-network70-c32-hardware-half/（日志与失败输出，ignored）；

证据：`Development/history/porting-worklog.md:6159`。

### 2026-09-08：硬件half中点修正，数值闭环但性能倒退｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 新增native_half_corrected.hlsli：依已测RTZ结果找相邻half、中点比较和奇偶位修正，处理符号、溢出与非有限值；比约586ms软件H基线慢，明确不采用、默认关闭。

证据：`Development/history/porting-worklog.md:6173`。

### 2026-09-08：软件half分支提示实验，无收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- NATIVE_C32_BRANCH_HALF=1只给原H的非有限值/次正规值分支加[branch]，算术逐字不变；

证据：`Development/history/porting-worklog.md:6180`。

### 2026-09-08：八Wave C32候选被管线创建拒绝｜环境测量无效/被修复

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- 将四Wave shader参数化：默认128线程/4Wave不变；该错误原因未定位，不能从单个复杂shader拒绝推出GPU普遍不支持256线程。

证据：`Development/history/porting-worklog.md:6193`。

### 2026-09-08：C32末端残差/写回全组并行，无收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- NATIVE_PARALLEL_C32_OUTPUT把64线程串行32通道写回改为全组按2048元素分工，scores/输入/skip系数映射不变，half_add_preserving_midpoint、H与RAW_OUTPUT/F原样； - 暖568.236776ms、末5帧566.920302ms，不优于567.012028ms基线；明确不采用-ParallelOutput。

证据：`Development/history/porting-worklog.md:6222`。

### 2026-09-08：多头Q/K逐通道归一化并行，无收益｜性能负

改动/基线：该HLSL阶段已验有效基线，不混HIP后续。

结果/裁决：- NATIVE_PARALLEL_MULTIHEAD_NORM仅预计算QKV＋Wave AV，64查询线程保留原平方和树/rsqrt/H，保存128 float倒数，raw Q/K以已有half共享区暂存；

证据：`Development/history/porting-worklog.md:6254`。

## 合并时必须保留的状态纠正

- prefix内联：09-15/17早null，09-17新基线−.27ms后采纳；不能永久弃。
- C512 mix half与decoder half预打包：早无稳定收益，09-16晚重测采纳。
- C512投影两假null：runner未换，换对后attention−.06/split−.18并采纳。
- 完整MH/ViT byte流旧反例：900Up少派4groups/3072float未写；修复后MH完整流采纳，旧gold也需更新；ViT其他布局仍需各自门。
- C32/MH矩阵八wavePSO拒绝：驱动回退导致4wave也拒，恢复后通过；不要归为shader资源限制。
- HIP_MH_REGISTER_EX C64..256成功不抵消C512 ex/prob寄存器化null；两类必须分别记。
- 09-23 wave内QKV norm求和重结合：虽−.18/−.14ms但非逐位，prod6不含；58dB/1.4%像素>1/255只是候选画质量级，未获该轮准入。
- 09-26 prefix split初默认关但随后与vecinput进入生产，不应保留为“未采纳”。

早期大型取证工作稿在 `/home/lmxxf/work/dlss5-failed-experiments-survey/`；本文已归并候选与原仓证据入口，不将930条关键词命中当失败项数。


### 2026-09-21｜C32 post减LDS提高占用率｜性能负（目录复核补证）

精确merged input4352B改残差处重读，48 frozen/motion帧byte0；VGPR158→155、LDS19712→15360、occupancy6→8、scratch0。160帧弃32，900档13.221539→13.308988（+.087449ms），1080档18.661582→18.785602（+.124020），拒绝。不能以更高occupancy单独推赢，必须为重复global/ALU工作计成本。证据：[实际资源与ABBA](results/c32-post-lds-20260921/report.md)。

### 2026-09-21｜network-fixed-shapes 未收五项｜两项性能负 / 三项收益不足未复测

基线2f4c8fa、真实默认234calls，900/1080各40warm100measure四槽、全部64槽raw0。C32 pool固定维度Δ−.011730/+.013590ms；ViT attention固定token+.004230/−.015055；QKV固定token+.005240/−.055245，两档不一致不收。C512 attention−.015355/−.016115、shift/pool−.009145/−.026705ms小正仅研究、未纳当轮组合，不判物理变慢。ViTexpand/projection/decoder三项后来组合正式通过，不能把全尺寸常量化判失败。重开需同当前实际dynamicsite与新稳态门，不能把旧形状适配泛化。证据：[完整八项表及采用范围](results/network-fixed-shapes-20260921/README.md)。

<a id="0927-0929"></a>

## 2026-09-27—29

口径：下表“900/1080”沿原报告固定几何，通常是完整 NativeGameFrame wall，不是游戏FPS或纯HIP；显式micro/ISA/CPU项例外。gfx1201=9070XT实测，gfx1200通常只编译；0.5%是当时门槛，09-30已改变，不能把旧门槛当现行。所有毫秒Δ默认候选减基线，正数慢。大多数旧报告无p99，未据均值虚构尾延迟。已修成功路线仅保失败版本历史，不把当前生产路线标成弃用。

### 2026-09-27｜FP8入口全程MODE.FP16_OVFL / HIP_FP8_SAT_MODE｜正确性失败

- 改动：入口置OVFL，替每值clamp；含保留clamp控制。
- 实际范围/结果：0.32 C32；900/1080语料同，但720-motion改变。OVFL连带把f16 RNE溢出从Inf改65504；±Inf/NaN FP8也不等clamp。指令少8–11%不能过数学门。
- 裁决/重试条件：只能隔离短FP8段并证明恢复和有限域；后续分段MODE已采用，不能将整条MODE路线列弃。
- 证据：`Development/results/fp8-sat-mode-20260927/README.md`。

### 2026-09-27｜gfx12 FP8转换指令clamp修饰位｜编译/ISA拒绝，无GPU数值结论

- 改动：尝试 v_cvt_pk_fp8_f32 ... clamp。
- 实际范围/结果：gfx12汇编器invalid operand；没有可运行候选/时间。
- 裁决/重试条件：只有明确新ISA/工具链支持证据才重开。
- 证据：`Development/results/fp8-sat-mode-20260927/README.md`。

### 2026-09-27｜DF_PACK8 / deep与ViT逐字节pack成对化｜性能负

- 改动：deep_fast/vit-wide/c512-m32-deep八处pack循环成对cvt。
- 实际范围/结果：0.32同配方；7×12逐位；900档10.74→10.80、1080档15.00→15.08ms，ISA仅少0.5–1.7%，不收。
- 裁决/重试条件：须改变实际活跃生产者/消费者数据接口；不能再改大量生产死分支。
- 证据：`Development/results/pack8-20260927/README.md`；`Development/results/ovfl-census-20260927/README.md`。

### 2026-09-27｜MF PACK8 / multihead_fast_padded两处pack｜未验证暂停

- 改动：推广相同成对pack。
- 实际范围/结果：两处均属生产关闭分支，撤回；未测收益。
- 裁决/重试条件：先证实目标函数/分支实际调用。
- 证据：`Development/results/pack8-20260927/README.md`。

### 2026-09-27｜MH转置布局直接复用累加器｜未验证暂停

- 改动：拟[feature][token]交换WMMA操作数省LDS。
- 实际范围/结果：源审发现expand/V已如此；剩余plane往返为跨头交换。未实现未计时，不能称负性能。
- 裁决/重试条件：必须发现新跨头供数组织，不重复已有转置。
- 证据：`Development/results/transpose-persist-20260927/README.md`。

### 2026-09-27→09-29｜C256持久化初期≤0.1ms估算｜被修复替代

- 改动：原拟跨层原子领window，因收益估算暂停。
- 实际范围/结果：09-27仅成本判断，没有实现/负计时；09-29真就绪队列+GPUrecover已采用（900约−1.9%、1080−0.6%）。旧“持久化更慢”混同单层融合，已纠正。
- 裁决/重试条件：历史暂停不阻新依赖队列；仅旧CPU同步版本负账另列。
- 证据：`Development/results/transpose-persist-20260927/README.md`；`Development/results/swin-persistent-20260929/README.md`；`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-27｜分段OVFL O/P最初非有限域拒绝｜被修复替代

- 改动：每片段FP8转换前后开关MODE，W2_PACK8=4。
- 实际范围/结果：最初语料逐位、约−0.5%，因非有限字节NaN暂缓；后续census+恢复审+游戏验后W2_PACK8=6进配方。不是最终弃用。
- 裁决/重试条件：保有限域/模式恢复合同；不能推广入口全程MODE。
- 证据：`Development/results/fmed3-ovfl-20260927/README.md`；`Development/results/ovfl-census-20260927/README.md`；`Development/results/c64-hand-asm-20260927/README.md`。

### 2026-09-27｜成对FP8解包 v_cvt_pk_f32_fp8｜正确性失败

- 改动：一次得到两个byte解码值，asm与builtin两路。
- 实际范围/结果：gfx1201 word0xc4054038实际返回(byte0,byte0)，op_sel返回(byte2,byte2)；单值解码正常。无整网收益。
- 裁决/重试条件：需新驱动/ISA探针证明双值真实不同且正确。
- 证据：`Development/results/ovfl-census-20260927/README.md`。

### 2026-09-27｜v_pk_f16替RTZ往返｜未验证暂停

- 改动：packed-half运算替f32算术+Hrtz。
- 实际范围/结果：默认按MODE RNE与生产RTZ不同，未做；不能当逐位小优化。
- 裁决/重试条件：新局部舍入模式与packed权重合同独立证明。
- 证据：`Development/results/ovfl-census-20260927/README.md`。

### 2026-09-27｜C64手改FFN汇编 FMA+删deadmov｜收益不足

- 改动：COMGR汇编往返后修改隐层乘积add0和死mov。
- 实际范围/结果：P基线；往返逐位、每window少192VALU，手改ABBA噪声内；后改源码显式W2_Q8_SETF，最终采用W2_PACK8=6。
- 裁决/重试条件：重试应是源码可重编且真实新瓶颈，不部署旧手改文件。
- 证据：`Development/results/c64-hand-asm-20260927/README.md`。

### 2026-09-27｜移动+0到s_setreg段外等待LLVM自动FMA｜收益不足

- 改动：mul紧邻add0希望自动收缩。
- 实际范围/结果：实ISA add0反186→191、FMA不变；源码显式fmaf替代成功，不是整条FMA弃用。
- 裁决/重试条件：编译器有真实combine补丁才重开自动路线。
- 证据：`Development/results/c64-hand-asm-20260927/README.md`。

### 2026-09-27｜PDL累计ticket回绕与4MiB泄漏｜被修复替代

- 改动：旧uint32累积目标直接比较；析构未放旗子。
- 实际范围/结果：源码有限反例：旧大计数满足回绕新小目标。drain+clear+同步修后隔离上限64压力900/1080实际70/64次reset逐位；旧缺agent-acquire/任意序前进保证仍单列边界。
- 裁决/重试条件：不把短会话异常无证归因回绕；以后改协议保代际/生命周期。
- 证据：`Development/results/pdl-audit-20260927/README.md`；`Development/results/re9-runtime-leak-20260927/README.md`。

### 2026-09-27｜关PDL作为整Frame替代｜性能负

- 改动：PDL0完整Frame对PDL1。
- 实际范围/结果：固定re9-runtime-flags旧模块，7用例84帧同；两批900+0.056/+0.066、1080+0.079/+0.079ms，未改默认。
- 裁决/重试条件：只能以新的同步合同/真实生产调用数重评，不据安全审自动宣性能更好。
- 证据：`Development/results/pdl-audit-20260927/README.md`。

### 2026-09-27｜RE9复用stream抑制重建显存｜收益不足

- 改动：临时stream复用与每次创建销毁对照。
- 实际范围/结果：24次显存轨迹一致，不收；大RGB tracked AddRef和4MiBPDL泄漏真正修复，120阶段末18阶段平台。
- 裁决/重试条件：有具体stream分配成本/泄漏证据才重开。
- 证据：`Development/results/re9-runtime-leak-20260927/README.md`。

### 2026-09-27｜RE9 Submitted只认构造queue｜被修复替代

- 改动：游戏重建交换链后改执行queue，旧retire不跟随。
- 实际范围/结果：actual prior job not yet submitted永不退休；改按实际执行queue退役并重建、8次评估watchdog。8次帧生成切换/窗口切换正常；新queue重建路径当时未强制触发。
- 裁决/重试条件：属宿主正确性修，不是性能路线全弃。
- 证据：`Development/results/onimusha-presr-20260927/README.md`。

### 2026-09-27｜C32 B / CW_FMED3_CLAMP全局｜收益不足

- 改动：全局clamp显式med3。
- 实际范围/结果：0.34C32基线；84帧逐位，两档两轮接近噪声留关；后只改activation的CW_ACT_FMED3不同scope且组合采用。
- 裁决/重试条件：新范围精准切口才重开，勿重复全局宏。
- 证据：`Development/results/c32-aco-20260927/README.md`；`Development/results/aco-lineup-20260928/README.md`。

### 2026-09-27｜C32 MODE intrinsic B空段｜被修复替代

- 改动：setreg intrinsic包转换。
- 实际范围/结果：84帧逐位和提速仍拒：ISA38个开关相邻空段、转换移出。删除源路径，短inline-asm固定4次转换M替代并采用。
- 裁决/重试条件：必须实际ISA转换在开关之间、无half收窄、恢复MODE，不以RGB同代证。
- 证据：`Development/results/c32-round2-20260927/README.md`。

### 2026-09-27→09-28｜激活q/p两FMA收缩为旧逐位恒等式｜正确性失败

- 改动：合q或p或两者，声称不变FP8。
- 实际范围/结果：2,097,153合法网格CPU反例3/66/68，含0x94→0x95非零码。09-28后用户批准float-FMA新数学基线，是独立有损/近似变更，不能洗旧逐位失败。
- 裁决/重试条件：若改数学须显式批准新gold/质量参考；exact候选不能放宽。
- 证据：`Development/results/c32-round2-20260927/README.md`；`Development/results/aco-lineup-20260928/README.md`；`Development/results/float-fma-20260928/README.md`。

### 2026-09-27｜C32 post P / CW_POST_HEAD_VEC｜性能负

- 改动：每8通道成组读half/权重，原累加序不变。
- 实际范围/结果：round2基线；7组84帧同，900+0.0053/−0.0131，1080+0.0338/+0.0228ms。LDS原本已向量化，不收。
- 裁决/重试条件：真正新布局/跨像素供数，别重写已合宽的相同读取。
- 证据：`Development/results/c32-round3-20260927/README.md`。

### 2026-09-27｜C32 post Q / CW_POST_FULL_TILE｜收益不足

- 改动：输出头整窗判断代逐像素bounds。
- 实际范围/结果：84帧同；900−0.0045/−0.0079、1080−0.0045/−0.0048ms，不收。finish同类F已采用，不能连F也列失败。
- 裁决/重试条件：需新的更大尾部地址成本证据。
- 证据：`Development/results/c32-round3-20260927/README.md`。

### 2026-09-27｜ViT V4 / halfQKV unroll2｜收益不足

- 改动：在已采纳stream V3上强制QKV8倍→2倍展开，VGPR96→48。
- 实际范围/结果：84EXACT+84AE同；900−0.0102/−0.0153、1080−0.1547/−0.1419ms对旧stream0，反比V3少赚约.02ms1080，不取。
- 裁决/重试条件：更低寄存器不能单独推快，须新供数而非扫展开数。
- 证据：`Development/results/vit-bytestream-20260927/README.md`。

### 2026-09-27｜C512 H / mix_hout→ffn_hin HALF_EDGE｜性能负

- 改动：精确F(Hrtz)半存半读，保mix主读取和projection残差。
- 实际范围/结果：ViTstream0/第三刀；168帧同AE同，900+0.0145/+0.0083、1080+0.0021/+0.0055ms。mix尾部额外转换，不收。
- 裁决/重试条件：上游实际packed输入/主循环供数新组织，不能只缩mixed临时。
- 证据：`Development/results/c512-mix-20260927/README.md`。

### 2026-09-27｜MH T / W2_FULL_WINDOW｜性能负

- 改动：投影完整窗口免逐像素bounds。
- 实际范围/结果：0.35第五刀；EXACT/AE逐位，900−0.0073/−0.0145、1080+0.0122/+0.0032ms，不收。
- 裁决/重试条件：新代码体积/编译组织而非重复同双路径。
- 证据：`Development/results/mh-round1-20260927/README.md`。

### 2026-09-27｜MH D / W2_DIRECT_FEATURE｜收益不足

- 改动：C256attention直读原feature省plane0，LDS32→16KiB/VGPR169→144。
- 实际范围/结果：逐位，900+0.0311/−0.0129、1080−0.0117/−0.0274ms，不稳定不取。
- 裁决/重试条件：新load/capture结构证据；不从静态LDS减半推收益。
- 证据：`Development/results/mh-round1-20260927/README.md`。

### 2026-09-27｜P runtime-zero-pad / C512_ZERO_PAD_900｜性能负

- 改动：现场确认32×512输入±0且权重有限才省mix；U显式标量化。
- 实际范围/结果：各168帧同/AE同；900+0.2144/+0.1607；1080−0.0096/−0.0187ms。目标900慢约1.7–2.3%，不收。
- 裁决/重试条件：若生产者直接提供精确有效tile信息且无需现场扫描归约才是新条件；未删attention paddingkey。
- 证据：`Development/results/tier900-20260927/README.md`。

### 2026-09-27｜U readfirstlane-zero-pad / C512_ZERO_PAD_900｜性能负

- 改动：现场确认32×512输入±0且权重有限才省mix；U显式标量化。
- 实际范围/结果：各168帧同/AE同；900+0.1724/+0.1579；1080−0.0193/−0.0218ms。目标900慢约1.7–2.3%，不收。
- 裁决/重试条件：若生产者直接提供精确有效tile信息且无需现场扫描归约才是新条件；未删attention paddingkey。
- 证据：`Development/results/tier900-20260927/README.md`。

### 2026-09-27→09-28｜C512 round1 R｜收益不足

- 改动：mix RTZ+FP8配对
- 实际范围/结果：第五刀基线；各168帧同/AE同；VGPR96→97驻留64→56；900−.0701/−.0197、1080−.0086/−.0100ms。四候选都无稳定≥0.5%不收，R合两批约.48%不可挑第一批。
- 裁决/重试条件：须换主循环/packed生产者/归一化调度；不重跑相同删VALU/拆组/LDS形态。
- 证据：`Development/results/c512-round1-20260927/README.md`。

### 2026-09-27→09-28｜C512 round1 Q｜收益不足

- 改动：QKV去q8(F)往返
- 实际范围/结果：第五刀基线；各168帧同/AE同；资源不变；900+.0062/−.0068、1080−.0083/−.0144ms。四候选都无稳定≥0.5%不收，R合两批约.48%不可挑第一批。
- 裁决/重试条件：须换主循环/packed生产者/归一化调度；不重跑相同删VALU/拆组/LDS形态。
- 证据：`Development/results/c512-round1-20260927/README.md`。

### 2026-09-27→09-28｜C512 round1 N｜性能负

- 改动：mix64→32输出通道/grid翻倍
- 实际范围/结果：第五刀基线；各168帧同/AE同；VGPR62但等工作量VALU3018>1871/VMEM448>320；900+.0230/−.0015、1080+.0363/+.0458ms。四候选都无稳定≥0.5%不收，R合两批约.48%不可挑第一批。
- 裁决/重试条件：须换主循环/packed生产者/归一化调度；不重跑相同删VALU/拆组/LDS形态。
- 证据：`Development/results/c512-round1-20260927/README.md`。

### 2026-09-27→09-28｜C512 round1 L｜收益不足

- 改动：QKV raw/byteout分时复用LDS
- 实际范围/结果：第五刀基线；各168帧同/AE同；初版alias DS重读修mode2；VGPR155→151/LDS5456→4420；900−.0105/−.0079、1080−.0132/+.0198ms。四候选都无稳定≥0.5%不收，R合两批约.48%不可挑第一批。
- 裁决/重试条件：须换主循环/packed生产者/归一化调度；不重跑相同删VALU/拆组/LDS形态。
- 证据：`Development/results/c512-round1-20260927/README.md`。

### 2026-09-27→09-28｜C512 round1游戏占卡污染｜环境测量无效

- 改动：1080 ledger及R-r1中途游戏启动。
- 实际范围/结果：33号远征基线12.5→21ms，另鬼武者中断；原批隔离，退出游戏后两批重测以上四项。不是数学失败。
- 裁决/重试条件：确认实际Shipping/RE9/game+benchmark无进程干扰，逐slot检查。
- 证据：`Development/results/c512-round1-20260927/README.md`。

### 2026-09-28→09-30｜mochi022 I｜被修复替代（旧门槛不足后组合采纳）

- 改动：去C32mapped/post已知half输入再收窄/拓宽
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.0026/−.0164；1080−.0014/−.0235ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 后续现状：09-30按新验收规则重测组合C，三轮900−.026/−.029/−.030、1080−.016/−.028/−.016ms且p99不差，已采纳；见`Development/results/small-wins-retest-20260930/README.md`。本条仅保存09-28旧不足账，不表示现役仍关。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28→09-30｜mochi022 P｜被修复替代（旧门槛不足后组合采纳）

- 改动：prefix保原RTZhalf位模式shuffle
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.0053/−.0005；1080−.0161/−.0174ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 后续现状：09-30按新验收规则重测组合C，三轮900−.026/−.029/−.030、1080−.016/−.028/−.016ms且p99不差，已采纳；见`Development/results/small-wins-retest-20260930/README.md`。本条仅保存09-28旧不足账，不表示现役仍关。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28→09-30｜mochi022 O｜被修复替代（旧门槛不足后组合采纳）

- 改动：mix/ViTcontract加4KiB空LDS限制驻留
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900−.0122/−.0130；1080−.0068/−.0062ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 后续现状：09-30按新验收规则重测组合C，三轮900−.026/−.029/−.030、1080−.016/−.028/−.016ms且p99不差，已采纳；见`Development/results/small-wins-retest-20260930/README.md`。本条仅保存09-28旧不足账，不表示现役仍关。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28→09-30｜mochi022 C=I+P+O｜被修复替代（旧门槛不足后组合采纳）

- 改动：三项组合
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900−.13/−.29%；1080−.22/−.14%；百分比，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 后续现状：09-30按新验收规则重测组合C，三轮900−.026/−.029/−.030、1080−.016/−.028/−.016ms且p99不差，已采纳；见`Development/results/small-wins-retest-20260930/README.md`。本条仅保存09-28旧不足账，不表示现役仍关。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜mochi022 S｜性能负

- 改动：C512projection→QKV tile计数短链
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.0404/+.0321；1080+.0488/+.0490ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜mochi022 V0｜性能负

- 改动：ViTcontract→QKV agent-acquire轮询
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.4592/+.4594；1080+.3862/+.3781ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜mochi022 F｜性能负

- 改动：ViT短链现行relaxed对照+hidden强持
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.0101/+.0174；1080+.0142/+.0276ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜mochi022 G｜性能负

- 改动：C512mix/QKV组间权重优先编号
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.0932/+.0741；1080+.1101/+.0995ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜mochi022 H｜性能负

- 改动：mix四wave128线程同64列权重
- 实际范围/结果：第五刀7fcad1e；同数值ninegroups共1512候选帧逐位/756AE同，gfx1201；900+.0426/+.0331；1080+.0402/+.0447ms，无稳定达标，不收。V0仅先导、F修后另完整门，不当可部署V0。
- 裁决/重试条件：同shape/同数学的新供数或内存协议条件才重开；不以新名字重复同路线。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜ViT短链F首版hidden.reset别名｜被修复替代

- 改动：串行池生命周期直接移用并发链。
- 实际范围/结果：720-motion反例；producer仍读hidden但host已释放，best-fit池供norm复用。强持hidden+无别名断言后完整EXACT/AE和回绕修好，性能仍负另列F。
- 裁决/重试条件：并发变更必须延长所有未完成读取者的owner，不能只改fence。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜上游NR_Q32_DIRECT/STAGE直接FP8删half｜正确性失败

- 改动：把half中间舍入视为可删冗余。
- 实际范围/结果：x=1.062500119/0x3f880001直接FP8=39，经half=38；上游48.6dB不构成本方exact证明。非正式GPU候选。
- 裁决/重试条件：显式新数学/质量选项才可研究，不允许逐位恒等式名义搬。
- 证据：`Development/results/mochizuki-022-20260928/README.md`。

### 2026-09-28｜ACO-lineup half直入 M｜收益不足

- 改动：softmax先成对half打包再fma_mix
- 实际范围/结果：C32VALU3781→3853，C64 5068→5336；仅EXACT84同，未AE/ABBA，ISA门停。
- 裁决/重试条件：需要真实减少转换且无新增拼接/零规范化开销的不同实现。
- 证据：`Development/results/aco-lineup-20260928/README.md`。

### 2026-09-28｜ACO-lineup half直入 N｜收益不足

- 改动：单个_Float16直送inlineasm省M拼接
- 实际范围/结果：chain3813/mapped4260仍高于基线3781/4232；未回归，ISA门停。
- 裁决/重试条件：需要真实减少转换且无新增拼接/零规范化开销的不同实现。
- 证据：`Development/results/aco-lineup-20260928/README.md`。

### 2026-09-28｜W2_BOUNDED_RCP单独R｜被修复替代

- 改动：保两侧求和与两Newton修正，仅缩小已知[1/256,624]除数范围。
- 实际范围/结果：逐位；900−.459/−.288%、1080−.345/−.372%单独不足门；与仅activation med3组合C两档−.65～.82%已采用。
- 裁决/重试条件：不把单项不足写成整个boundedRCP弃用；组合需实测不能相加。
- 证据：`Development/results/aco-lineup-20260928/README.md`。

### 2026-09-28｜朴素恢复half激活H｜性能负

- 改动：输入half、两halfFMA+half乘法，矩阵仍float累加。
- 实际范围/结果：FMA-vs-NV隔离；7×12全有限但输出全变，AE未测；900+.8411/+.8389ms约9%，1080+1.25/+1.23ms约9.8–9.9%，不生产。
- 裁决/重试条件：packed-half优化/完整exact新合同才可重开，不能称该H已恢复全NV。
- 证据：`Development/results/fma-vs-nvidia-20260928/README.md`。

### 2026-09-28｜FMA-vs-NV旧oracle shift错误与错架构目录｜环境测量无效

- 改动：候选postshift3对oracle0；另首轮选旧架构子目录。
- 实际范围/结果：撤回0.145%/0.517%误差改善；wrongarch整轮隔离。float-FMA最终统一shift3、新gold并获批准采用，不能用旧误差表宣传。
- 裁决/重试条件：同输入/shift/71blocks/seed/模块路径SHA再比较；局部ISA与有效重跑计时未撤回。
- 证据：`Development/results/fma-vs-nvidia-20260928/README.md`；`Development/results/float-fma-20260928/README.md`。

### 2026-09-28｜W2_PACK_NOZERO P｜收益不足

- 改动：借低高半覆盖删除目的reg清零，同时合互斥打包段。
- 实际范围/结果：floatFMA/currentDIRECT_IO3；168帧逐位/84AE同；重测900+.00642/−.00958、1080−.01019/−.01468ms，不达.5%。
- 裁决/重试条件：新packed生命周期/供数形态；不能只再删同一mov。
- 证据：`Development/results/daniel-kernels-20260928/README.md`。

### 2026-09-28｜W2_PACK_NOZERO首批与zero-copy工具并发｜环境测量无效

- 改动：P初轮与benchmark-zc运行区间重叠，guard未涵盖该进程。
- 实际范围/结果：16旧P槽全隔离不用；hash数值证据保。扩guard后有效重测仍不足。
- 裁决/重试条件：含所有benchmark前缀进程检查和唯一设备窗。
- 证据：`Development/results/daniel-kernels-20260928/README.md`。

### 2026-09-28｜C32 CW_WEIGHT_CACHE Q｜收益不足

- 改动：QKV权重qt4缓存；保持索引和MMA序。
- 实际范围/结果：各12帧1080-motion逐位；少36/12/48global_load_b64，但VGPR增2～24。Q/R近噪声，C最好900约−.19%；新IO下C900+.02324/+.01834、1080−.00567/+.01099ms，未做完整AE/不收。
- 裁决/重试条件：需避免寄存器活跃期代价的新加载组织，不重复qt4缓存。
- 证据：`Development/results/daniel-kernels-20260928/README.md`。

### 2026-09-28｜C32 CW_WEIGHT_CACHE R｜收益不足

- 改动：projection权重qt4缓存；保持索引和MMA序。
- 实际范围/结果：各12帧1080-motion逐位；少36/12/48global_load_b64，但VGPR增2～24。Q/R近噪声，C最好900约−.19%；新IO下C900+.02324/+.01834、1080−.00567/+.01099ms，未做完整AE/不收。
- 裁决/重试条件：需避免寄存器活跃期代价的新加载组织，不重复qt4缓存。
- 证据：`Development/results/daniel-kernels-20260928/README.md`。

### 2026-09-28｜C32 CW_WEIGHT_CACHE C｜收益不足

- 改动：两者组合CW_WEIGHT_CACHE3；保持索引和MMA序。
- 实际范围/结果：各12帧1080-motion逐位；少36/12/48global_load_b64，但VGPR增2～24。Q/R近噪声，C最好900约−.19%；新IO下C900+.02324/+.01834、1080−.00567/+.01099ms，未做完整AE/不收。
- 裁决/重试条件：需避免寄存器活跃期代价的新加载组织，不重复qt4缓存。
- 证据：`Development/results/daniel-kernels-20260928/README.md`。

### 2026-09-28｜C256融合 O｜收益不足

- 改动：重测旧C256整块
- 实际范围/结果：floatFMA1152/shift3；当前1080−.136ms但900+.176ms；非spill六变体各12motion帧同，最终仅B/qtbatch2在1080采用，900保持分体。
- 裁决/重试条件：新供数/生命周期与真正降低峰值；不从少global交界推必快。
- 证据：`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-28｜C256融合 L｜收益不足

- 改动：Q片段放LDS缩短生命周期
- 实际范围/结果：floatFMA1152/shift3；不降寄存器峰值，未优于B；非spill六变体各12motion帧同，最终仅B/qtbatch2在1080采用，900保持分体。
- 裁决/重试条件：新供数/生命周期与真正降低峰值；不从少global交界推必快。
- 证据：`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-28｜C256融合 BL｜收益不足

- 改动：B两qt权重复用+Q的LDS
- 实际范围/结果：floatFMA1152/shift3；1080−.223/900+.138ms，与B−.221/+.130近，未有额外收益；非spill六变体各12motion帧同，最终仅B/qtbatch2在1080采用，900保持分体。
- 裁决/重试条件：新供数/生命周期与真正降低峰值；不从少global交界推必快。
- 证据：`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-28｜C256融合 F1/FL｜收益不足

- 改动：四qt单hidden及另Q LDS
- 实际范围/结果：floatFMA1152/shift3；1080−.177/−.182ms，均不如B；非spill六变体各12motion帧同，最终仅B/qtbatch2在1080采用，900保持分体。
- 裁决/重试条件：新供数/生命周期与真正降低峰值；不从少global交界推必快。
- 证据：`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-28｜C256融合 F四qt双hidden｜未验证暂停

- 改动：四qt/hidden_tiles2
- 实际范围/结果：floatFMA1152/shift3；编译spill，未计时；非spill六变体各12motion帧同，最终仅B/qtbatch2在1080采用，900保持分体。
- 裁决/重试条件：新供数/生命周期与真正降低峰值；不从少global交界推必快。
- 证据：`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-28｜C256 B在900通用启用｜性能负

- 改动：把两qt融合好处推广900。
- 实际范围/结果：短筛+.12965ms；最终host只1152启用，900正式控制−.004800/+.003724ms持平，不曾生产启900。
- 裁决/重试条件：900真实不同任务粒度/调度条件才重开。
- 证据：`Development/results/c256-fusion-20260928/README.md`。

### 2026-09-28｜C512-fusion轮 D｜收益不足

- 改动：_t8删无人消费float出口
- 实际范围/结果：当前floatFMA/直写IO；900−.00654、1080−.00251ms，未采用。QKV+attention融合F及C64/C128Sboth已另采用，不混为本条失败。
- 裁决/重试条件：新不减有效wave/不延长串行链的组织；仅逻辑读写下降不足。
- 证据：`Development/results/c512-fusion-20260928/README.md`。

### 2026-09-28｜C512-fusion轮 M｜性能负

- 改动：FFN16→32token共享expand/contract权重
- 实际范围/结果：当前floatFMA/直写IO；VGPR113→216、LDS4160→8320，慢.10～.15ms，未采用。QKV+attention融合F及C64/C128Sboth已另采用，不混为本条失败。
- 裁决/重试条件：新不减有效wave/不延长串行链的组织；仅逻辑读写下降不足。
- 证据：`Development/results/c512-fusion-20260928/README.md`。

### 2026-09-28｜C512-fusion轮 MD｜性能负

- 改动：M加D
- 实际范围/结果：当前floatFMA/直写IO；仍慢.10～.15ms，未采用。QKV+attention融合F及C64/C128Sboth已另采用，不混为本条失败。
- 裁决/重试条件：新不减有效wave/不延长串行链的组织；仅逻辑读写下降不足。
- 证据：`Development/results/c512-fusion-20260928/README.md`。

### 2026-09-28｜C512-fusion轮 W16｜性能负

- 改动：900C256每head两wave/两plane32KiB
- 实际范围/结果：当前floatFMA/直写IO；额外2barrier，VGPR200～208，900-motion12帧同；8.9411875→9.06292857ms(+.121741/+1.36%)，未采用。QKV+attention融合F及C64/C128Sboth已另采用，不混为本条失败。
- 裁决/重试条件：新不减有效wave/不延长串行链的组织；仅逻辑读写下降不足。
- 证据：`Development/results/c512-fusion-20260928/README.md`。

### 2026-09-28｜fusion-round3 D｜性能负

- 改动：C64/C128尾块+Down融合
- 实际范围/结果：C512fusion当前基线；1080-motion12帧同，900/1080短筛+.03465/+.04790ms，均不取。
- 裁决/重试条件：需要减少冗余编码/store/引用期的新数据流，而非只减派发数。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-28｜fusion-round3 P｜性能负

- 改动：ViTprojection顺带写下一blockbyte
- 实际范围/结果：C512fusion当前基线；1080-motion12帧同，900/1080短筛+.0669/+.0053ms，均不取。
- 裁决/重试条件：需要减少冗余编码/store/引用期的新数据流，而非只减派发数。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-28｜fusion-round3 G｜性能负

- 改动：P再合入口Gather+pack
- 实际范围/结果：C512fusion当前基线；1080-motion12帧同，900/1080短筛+.0628/+.0176ms，均不取。
- 裁决/重试条件：需要减少冗余编码/store/引用期的新数据流，而非只减派发数。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-28｜fusion-round3 Q｜性能负

- 改动：复用首次FP8编码缩短byte引用
- 实际范围/结果：C512fusion当前基线；1080-motion12帧同，900/1080短筛+.0231/+.0106ms，均不取。
- 裁决/重试条件：需要减少冗余编码/store/引用期的新数据流，而非只减派发数。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-28｜fusion-round3 R｜性能负

- 改动：Q后续入口融合对照
- 实际范围/结果：C512fusion当前基线；1080-motion12帧同，900/1080短筛+.0209/+.0061ms，均不取。
- 裁决/重试条件：需要减少冗余编码/store/引用期的新数据流，而非只减派发数。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-28｜Up U1负零位图｜被修复替代

- 改动：先存正零+额外位图恢复FP8负零。
- 实际范围/结果：核实原canonical只7f→ff、不清80；U2直接保byte替代位图，SGPR降27/26。最终U2+T两档约−2%已采纳。
- 裁决/重试条件：同字节直接存即可，不重试额外位图。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-28｜C32跨qt权重共享/FFN追加｜未验证暂停

- 改动：拟多个qt同时驻留共用权重。
- 实际范围/结果：源码审需重构输入→FFN→QKV流水，违背09-26rollhidden/window成功减少spill的方向；本轮无新实现/无时间。ViT M2/M4等旧负账由早期分工覆盖。
- 裁决/重试条件：新ISA峰值/布局证据且避免长活跃期，不能再全展开。
- 证据：`Development/results/fusion-round3-20260928/README.md`。

### 2026-09-29｜deep-layers R｜性能负

- 改动：单wave寄存器FFN去hiddenLDS
- 实际范围/结果：0.36基线，各12帧逐位；900+.199551/1080+.264842ms，不扩完整七组AE，不取。compact C/P本轮另成功。
- 裁决/重试条件：不减wave/不增供数开销的新布局；无LDS/低VGPR不是收益证据。
- 证据：`Development/results/deep-layers-20260929/README.md`。

### 2026-09-29｜deep-layers RF｜性能负

- 改动：R前接mix同wave供数
- 实际范围/结果：0.36基线，各12帧逐位；+.140592/+.298937ms，不扩完整七组AE，不取。compact C/P本轮另成功。
- 裁决/重试条件：不减wave/不增供数开销的新布局；无LDS/低VGPR不是收益证据。
- 证据：`Development/results/deep-layers-20260929/README.md`。

### 2026-09-29｜deep-layers V｜性能负

- 改动：ViTexpand直接读float内核打包删pack
- 实际范围/结果：0.36基线，各12帧逐位；+.147357/+.337045ms，不扩完整七组AE，不取。compact C/P本轮另成功。
- 裁决/重试条件：不减wave/不增供数开销的新布局；无LDS/低VGPR不是收益证据。
- 证据：`Development/results/deep-layers-20260929/README.md`。

### 2026-09-29｜C512 RF8展开FP8｜正确性失败

- 改动：16份8,388,608权重可无损编码，拟展开换FP8MMA。
- 实际范围/结果：旧block46/pattern2实际10个float输出元素位差（非10bit）；F16控制/仅contract通过。本轮只重核证据，RF8未构建/未运行。
- 裁决/重试条件：证明累加序/片内舍入一致或显式新数学质量合同。
- 证据：`Development/results/deep-layers-20260929/README.md`。

### 2026-09-29｜compact离线收益未体现在剑星2K读数｜收益不足

- 改动：已采纳compact核离线约−.24ms1080，现场验证。
- 实际范围/结果：07:53同EXACT静止54fps/frameavg18.56～18.60ms，对旧18.55～18.72持平；不据此撤回离线bit0与ABBA，也不宣游戏已赚。
- 裁决/重试条件：同场景真实路径/重叠与计时范围核实，非再刷离线均值。
- 证据：`Development/results/deep-layers-20260929/README.md`。

### 2026-09-29｜kernel-map合成scale全压±1/64｜环境测量无效

- 改动：尾区gain亦合成小数，42浅层输出全零。
- 实际范围/结果：整批169项作废，保结构零/scale1后重跑323有效比较项。独立核中位数和也不是Frame时长。
- 裁决/重试条件：fixture必须finite/guard/nonzero与真实参数尺度。
- 证据：`Development/results/kernel-map-20260929/README.md`。

### 2026-09-29｜kernel-map P projection未测｜被修复替代

- 改动：08:48仅准备投影M32，不能算第三刀。
- 实际范围/结果：后续同日独立C512_PROJ_M32完整gate并测慢，见下一条。
- 裁决/重试条件：已由真实后续负账取代，不保“未测”现行状态。
- 证据：`Development/results/kernel-map-20260929/README.md`；`Development/results/c512-proj-share-20260929/README.md`。

### 2026-09-29｜LLVM20 COMGR2整模块替换｜性能负

- 改动：同30生产拼接源码/宏，两架构60模块替换当前COMGR3内部LLVM21。
- 实际范围/结果：基线590b9320a5be90e40268759c6203c01fde121e68；Frame DIRECT_IO3/PDL1/full71两轮ABBA1000弃200；900+3.339/+3.057%，1080+2.987/+2.866%，约+.26～.33ms。版本SHA 33ab2c2f7838239f1e2e5c06432bbb8d887e8cb2。不改配方不部署。
- 裁决/重试条件：新的明确backend修复+原失败用例及同工具链控制；不能挑1080通过子集当完整数值门。
- 证据：`Development/results/compiler-versions-20260929/README.md`；`Development/results/llvm-fork-20260929/README.md`。

### 2026-09-29｜公开LLVM21整模块替换｜性能负

- 改动：同30生产拼接源码/宏，两架构60模块替换当前COMGR3内部LLVM21。
- 实际范围/结果：基线590b9320a5be90e40268759c6203c01fde121e68；Frame DIRECT_IO3/PDL1/full71两轮ABBA1000弃200；900+.153/−.150%持平，1080+.506/+.427%慢。版本SHA 6d585d872fbd3c594da7a3c09ac9b22eef4167f6。不改配方不部署。
- 裁决/重试条件：新的明确backend修复+原失败用例及同工具链控制；不能挑1080通过子集当完整数值门。
- 证据：`Development/results/compiler-versions-20260929/README.md`；`Development/results/llvm-fork-20260929/README.md`。

### 2026-09-29｜公开LLVM22整模块替换｜正确性失败

- 改动：同30生产拼接源码/宏，两架构60模块替换当前COMGR3内部LLVM21。
- 实际范围/结果：基线590b9320a5be90e40268759c6203c01fde121e68；Frame DIRECT_IO3/PDL1/full71两轮ABBA1000弃200；96/168帧不同，AE40/84行变，静态900候选12帧12hash；样本max.468262/RMSE.042658，无NaN也非signedzero；未计时。版本SHA f58b06dce1f9c15707c5f808fd002e18c2accf7e。不改配方不部署。
- 裁决/重试条件：新的明确backend修复+原失败用例及同工具链控制；不能挑1080通过子集当完整数值门。
- 证据：`Development/results/compiler-versions-20260929/README.md`；`Development/results/llvm-fork-20260929/README.md`。

### 2026-09-29｜ROCm7.1.1自编LLVM20后备｜未验证暂停

- 改动：原准备自编20。
- 实际范围/结果：收到驱动COMGR2后取消自编，不是构建失败；真实L20负结果来自COMGR2，不混身份。
- 裁决/重试条件：驱动链不可用才需备用构建，无性能证据。
- 证据：`Development/results/compiler-versions-20260929/README.md`。

### 2026-09-29｜LLVM21 VOPD lookahead4｜收益不足

- 改动：GCNCreateVOPD小范围独立指令前移，原hazard重新生成。
- 实际范围/结果：fork269832faf25b；168帧+AE逐位；对驱动900−.148/−.355%、1080+.026/+.151%，未任档稳定≥.5%；对公共21小正不代生产收益。32长槽32000帧仅首尾检查。
- 裁决/重试条件：新的RA/供数/调度切口；不把后置配对收益递减称整个编译器无潜力。
- 证据：`Development/results/llvm-patch1-20260929/README.md`。

### 2026-09-29｜VOPD lookahead8静态探针｜未验证暂停

- 改动：仅C32/C64两个gfx1201模块8条前瞻。
- 实际范围/结果：C32多2～4对多只净省0～3条；C64/C128部分长2～4条，C256attn不变。未GPU/计时。
- 裁决/重试条件：有明确新静态优势再做，不扫参数找噪声。
- 证据：`Development/results/llvm-patch1-20260929/README.md`。

### 2026-09-29｜自编LLVM六类patch候选除VOPD外｜未验证暂停

- 改动：med3/NaN、范围rcp、精确half往返、mul+0、wait/MODE依赖五类调查。
- 实际范围/结果：仅定位pass/IEEE边界；本轮无补丁/速度，既有手写原语仍在，不能称编译器自动消除。
- 裁决/重试条件：最小IR/MIR合法域与反例、真实编译产物再实验。
- 证据：`Development/results/llvm-fork-20260929/README.md`。

### 2026-09-29｜C256persistent每stage CPU synchronize/readback｜性能负

- 改动：init/run后每段CPU等结果决定恢复。
- 实际范围/结果：900+.485/1080+.685ms；异步GPU条件recover替代后正式两档正并已采纳，持久化路线非整体弃。
- 裁决/重试条件：只GPU条件恢复/不增加CPU关键路径的新组织。
- 证据：`Development/results/swin-persistent-20260929/README.md`。

### 2026-09-29｜小通道SP C128 encoder｜收益不足

- 改动：现C256两段常开基线，只增加该stage persistent init/run/recover。
- 实际范围/结果：同exe matched两轮900+.291/+.363%；1080−.195/−.100%。四stage共672常规+384压力候选帧gold0，AE528行同；C1284→3省1派发，C642→3增1派发，均不取。
- 裁决/重试条件：需不同层数组合/队列开销或真正新调度收益，不从C256推广推必赚。
- 证据：`Development/results/swin-persistent-c128-c64-20260929/README.md`。

### 2026-09-29｜小通道SP C128 decoder｜收益不足

- 改动：现C256两段常开基线，只增加该stage persistent init/run/recover。
- 实际范围/结果：同exe matched两轮900+.167/+.025%；1080−.119/+.005%。四stage共672常规+384压力候选帧gold0，AE528行同；C1284→3省1派发，C642→3增1派发，均不取。
- 裁决/重试条件：需不同层数组合/队列开销或真正新调度收益，不从C256推广推必赚。
- 证据：`Development/results/swin-persistent-c128-c64-20260929/README.md`。

### 2026-09-29｜小通道SP C64 encoder｜性能负

- 改动：现C256两段常开基线，只增加该stage persistent init/run/recover。
- 实际范围/结果：同exe matched两轮900+.457/+.354%；1080−.004/−.115%。四stage共672常规+384压力候选帧gold0，AE528行同；C1284→3省1派发，C642→3增1派发，均不取。
- 裁决/重试条件：需不同层数组合/队列开销或真正新调度收益，不从C256推广推必赚。
- 证据：`Development/results/swin-persistent-c128-c64-20260929/README.md`。

### 2026-09-29｜小通道SP C64 decoder｜性能负

- 改动：现C256两段常开基线，只增加该stage persistent init/run/recover。
- 实际范围/结果：同exe matched两轮900+.388/+.295%；1080+.155/+.123%。四stage共672常规+384压力候选帧gold0，AE528行同；C1284→3省1派发，C642→3增1派发，均不取。
- 裁决/重试条件：需不同层数组合/队列开销或真正新调度收益，不从C256推广推必赚。
- 证据：`Development/results/swin-persistent-c128-c64-20260929/README.md`。

### 2026-09-29｜small-SP先对生产host比较的诊断开销污染｜环境测量无效

- 改动：诊断host额外读SP变量，直接对现役host差。
- 实际范围/结果：小差值不可靠，补两轮同exe仅stage开关matched；最终四负结果用matched，不挑最优−.127%。
- 裁决/重试条件：公平samehost/共同检查成本，旧先导保原证。
- 证据：`Development/results/swin-persistent-c128-c64-20260929/README.md`。

### 2026-09-29｜ViT attention head-order｜性能负

- 改动：只改head排列
- 实际范围/结果：当日现C256 SP/后score转置基线；隔离输出bytecheck后micro，多数持平/变慢；123jobs原TIME保，非完整Frame候选。
- 裁决/重试条件：新V读取/加载组织且不改half分母数学，不重复相同分组预取。
- 证据：`Development/results/vit-attention-20260929/README.md`。

### 2026-09-29｜ViT attention wave4｜性能负

- 改动：四wave同WG
- 实际范围/结果：当日现C256 SP/后score转置基线；隔离输出bytecheck后micro，未优于单wave；123jobs原TIME保，非完整Frame候选。
- 裁决/重试条件：新V读取/加载组织且不改half分母数学，不重复相同分组预取。
- 证据：`Development/results/vit-attention-20260929/README.md`。

### 2026-09-29｜ViT attention prefetch4｜性能负

- 改动：四keytile预取
- 实际范围/结果：当日现C256 SP/后score转置基线；隔离输出bytecheck后micro，未优于单wave；123jobs原TIME保，非完整Frame候选。
- 裁决/重试条件：新V读取/加载组织且不改half分母数学，不重复相同分组预取。
- 证据：`Development/results/vit-attention-20260929/README.md`。

### 2026-09-29｜ViT attention wave4+prefetch4｜性能负

- 改动：分组+预取
- 实际范围/结果：当日现C256 SP/后score转置基线；隔离输出bytecheck后micro，640约72–73µs；叠native转换仍约55.6µs，劣于单wave37.885µs；123jobs原TIME保，非完整Frame候选。
- 裁决/重试条件：新V读取/加载组织且不改half分母数学，不重复相同分组预取。
- 证据：`Development/results/vit-attention-20260929/README.md`。

### 2026-09-29｜ViT V=+1去V取数诊断｜未验证暂停

- 改动：夹具常量V代真实V地址/pack/load。
- 实际范围/结果：640省约6–10µs仅该夹具逐位，不可当通用优化/纯DRAM耗时。
- 裁决/重试条件：真正保任意V内容的读取组织才能成为候选。
- 证据：`Development/results/vit-attention-20260929/README.md`。

### 2026-09-29｜C512最终projection M32 / C512_PROJ_M32｜性能负

- 改动：同grid后半返回，每wave两个16token共权重，累加链翻倍。
- 实际范围/结果：216帧逐位/AE同，VGPR80→97、SGPR28→38无spill；两轮900+ .156/.149ms(1.93/1.85%)，1080+.060/.072ms(.54/.66%)；不收。
- 裁决/重试条件：不减wave数/不加长串行链的新供数，旧两tileM32形态不重复。
- 证据：`Development/results/c512-proj-share-20260929/README.md`。

### 2026-09-29｜ViT QKV hoist｜性能负

- 改动：scale提前读一次
- 实际范围/结果：原half数学，jobbench逐字节同/7×128微测，40032.6～33.2 vsbase32.1～32.2µs；64050.3～50.5 vs48.7～48.8（数字项单位µs，末项仅原文描述）；不收。
- 裁决/重试条件：真实新读取/寄存器组织，不扫同块编号/展开数组。
- 证据：`Development/results/vit-qkv-20260929/README.md`。

### 2026-09-29｜ViT QKV order1｜性能负

- 改动：part/token/head编号重排1
- 实际范围/结果：原half数学，jobbench逐字节同/7×128微测，40034.7～34.9；64051.6～51.7（数字项单位µs，末项仅原文描述）；不收。
- 裁决/重试条件：真实新读取/寄存器组织，不扫同块编号/展开数组。
- 证据：`Development/results/vit-qkv-20260929/README.md`。

### 2026-09-29｜ViT QKV order2｜性能负

- 改动：重排2
- 实际范围/结果：原half数学，jobbench逐字节同/7×128微测，40035.5～35.9；64052.5～52.8（数字项单位µs，末项仅原文描述）；不收。
- 裁决/重试条件：真实新读取/寄存器组织，不扫同块编号/展开数组。
- 证据：`Development/results/vit-qkv-20260929/README.md`。

### 2026-09-29｜ViT QKV order3｜性能负

- 改动：重排3
- 实际范围/结果：原half数学，jobbench逐字节同/7×128微测，40033.4～38.3；64052.5～52.6（数字项单位µs，末项仅原文描述）；不收。
- 裁决/重试条件：真实新读取/寄存器组织，不扫同块编号/展开数组。
- 证据：`Development/results/vit-qkv-20260929/README.md`。

### 2026-09-29｜ViT QKV W5数组/CH4/CH16｜性能负

- 改动：相同五wave共享权重不同声明/通道chunk
- 实际范围/结果：原half数学，jobbench逐字节同/7×128微测，均比4registerCH8更慢，数组丢大半收益，无独立整网精确数不造（数字项单位µs，末项仅原文描述）；不收。
- 裁决/重试条件：真实新读取/寄存器组织，不扫同块编号/展开数组。
- 证据：`Development/results/vit-qkv-20260929/README.md`。

### 2026-09-29｜ViT QKV W5初期因冻结host不装｜被修复替代

- 改动：新160线程/5wave导出需host探测与新grid。
- 实际范围/结果：原1080−.74/−.81%已数值216帧同，但暂未部署；21:08批准新host后再216帧同/900−1.39%、1080−1.07%，实际已装。不能保持“未采用”旧标题。
- 裁决/重试条件：本项无重试；保ABI和缺导出fallback。
- 证据：`Development/results/vit-qkv-20260929/README.md`。

### 2026-09-29｜MAKE_RESIDENT_EVERY60→0去周期尖峰｜收益不足

- 改动：完整Frame离线A/B/A/B每轮1200弃200。
- 实际范围/结果：60均值12.055/12.175ms，0为12.128/12.229；p99 60=12.279/12.423、0=12.392/12.891。60倍数未见周期尖峰，唯一28.84ms在0/帧857。未关生产默认。
- 裁决/重试条件：真实游戏资源OS降级/驻留成本证据才重开，离线tracked集合小不排所有游戏成本。
- 证据：`Development/results/resident-spike-20260929/README.md`。

## 扫描覆盖与待核

- 完整日期历史主段：`Development/history/DevHistory-full-20261006.md` 的2026-09-27 03:00起至09-29 21:28（含成功条目用于纠正旧暂停状态），以及当前`Development/DevHistory.md`对应索引。
- 目录枚举：下列全部日期results目录；对README及负关键词所在上下文、表格逐项核对，核心summary/原始日志路径亦据README交叉。未逐份反汇编/解压全部二进制，未运行新实验。

  - `Development/results/aco-isa-20260927/`

  - `Development/results/aco-lineup-20260928/`

  - `Development/results/c256-fusion-20260928/`

  - `Development/results/c32-aco-20260927/`

  - `Development/results/c32-round2-20260927/`

  - `Development/results/c32-round3-20260927/`

  - `Development/results/c512-fusion-20260928/`

  - `Development/results/c512-mix-20260927/`

  - `Development/results/c512-proj-share-20260929/`

  - `Development/results/c512-round1-20260927/`

  - `Development/results/c64-block-fused-20260927/`

  - `Development/results/c64-hand-asm-20260927/`

  - `Development/results/compiler-versions-20260929/`

  - `Development/results/daniel-050-20260928/`

  - `Development/results/daniel-051-20260929/`

  - `Development/results/daniel-kernels-20260928/`

  - `Development/results/daniel-nonnative-20260929/`

  - `Development/results/deep-layers-20260929/`

  - `Development/results/float-fma-20260928/`

  - `Development/results/fma-vs-nvidia-20260928/`

  - `Development/results/fmed3-ovfl-20260927/`

  - `Development/results/fp8-sat-mode-20260927/`

  - `Development/results/frame-breakdown-20260928/`

  - `Development/results/frame-stats-20260928/`

  - `Development/results/fusion-round3-20260928/`

  - `Development/results/kernel-map-20260929/`

  - `Development/results/llvm-fork-20260929/`

  - `Development/results/llvm-patch1-20260929/`

  - `Development/results/mh-round1-20260927/`

  - `Development/results/mochizuki-022-20260928/`

  - `Development/results/onimusha-presr-20260927/`

  - `Development/results/ovfl-census-20260927/`

  - `Development/results/pack8-20260927/`

  - `Development/results/pdl-audit-20260927/`

  - `Development/results/re9-runtime-leak-20260927/`

  - `Development/results/resident-spike-20260929/`

  - `Development/results/swin-persistent-20260929/`

  - `Development/results/swin-persistent-c128-c64-20260929/`

  - `Development/results/tier900-20260927/`

  - `Development/results/transpose-persist-20260927/`

  - `Development/results/vit-attention-20260929/`

  - `Development/results/vit-bytestream-20260927/`

  - `Development/results/vit-c512-aco-20260927/`

  - `Development/results/vit-qkv-20260929/`

  - `Development/results/zero-copy-io-20260928/`


- 额外源：`Development/results/fusion-round3-20260928/ffn-audit.md`、`aco-lineup-20260928/README.md`、`llvm-fork-20260929/patch-candidates.md`、`compiler-versions-20260929/isa-notes.md`、`daniel-kernels-20260928/activation-candidates.md`，以及协调者`all-negative-md-index.tsv`日期子集。
- 归并而非新增：09-22 ViT M2/M4/balancedtile、09-26C32unroll spill/C256旧整块负账属于早期负责人；本段保09-28重开及后续解决引用，最终稿应互链去重。09-30 handoff/poll/1088/小件属后期负责人。
- 证据限制/疑似遗漏：ViT attention单项head-order/wave4/prefetch4微测的全部123job参数组合在`micro.json`和raw压缩日志，本文按独立机制及组合归并，不能声称逐个job均有FrameABBA；QKV W5数组/CH4/CH16同理。自编LLVM其余五类仅调查无实现，不列实际性能失败。纯shader无输出gold的已编/估算方案明确暂停。


### 被引用README完整SHA256（供整合绑定）


- `Development/results/aco-lineup-20260928/README.md`：`341e4c53ac8e1ed41aed7e6fd102baa74fe23795caf2fb7c857d2096f72180fe`

- `Development/results/c256-fusion-20260928/README.md`：`1871cc03835fbcff7f2d09ac2f54df689c14a112859f250b0f4519935a7511b8`

- `Development/results/c32-aco-20260927/README.md`：`ca24451a1d99300167e310930deb5cce55d4c23811d50d63318eba01727d9d8a`

- `Development/results/c32-round2-20260927/README.md`：`88c9bdb409cba97ca03b5a6c853c6b78aa4d531d4a19a0b90530309ea39ea396`

- `Development/results/c32-round3-20260927/README.md`：`96722a57d367d8c48d82364cfa6e83e33d14dc799f3d51809de4655cfe1812d8`

- `Development/results/c512-fusion-20260928/README.md`：`6c653f24d4f0897a1d187ad3b758525bab921b52cddb2c4698a52f1d10c60916`

- `Development/results/c512-mix-20260927/README.md`：`33d37513a7a8cb41e3a57e8fa77eca747fb0c6a72a6299e4ad3ca8726f66dceb`

- `Development/results/c512-proj-share-20260929/README.md`：`d285f02a933c23f7bb71dcb4ae9de340d64ef51acaa948b67a0767c180a46706`

- `Development/results/c512-round1-20260927/README.md`：`d59eca88cc208a690c8ef44ec790366b243072979a6c4d82259eea8c7677f6ba`

- `Development/results/c64-hand-asm-20260927/README.md`：`a0d265fcc2df4e0d8537a13b60a2dfb45aa6ba296a65d53d4613614d8f523d94`

- `Development/results/compiler-versions-20260929/README.md`：`4ee43fe836ed2c2e75ef5e9229c71cebf34cb315d14c3d05f31506c762bdcdb0`

- `Development/results/daniel-kernels-20260928/README.md`：`38529708acc7a7854d7833b03facfb27c0f1833ef6b0943cca40c0496b0db56f`

- `Development/results/deep-layers-20260929/README.md`：`4cfa8de0f423bdd8339a4937f667ebec07c0311447b9ec640a6c40497572fa51`

- `Development/results/float-fma-20260928/README.md`：`5e67fb55cc8c9ab7566f91b1ec41b2100bb8c378b90303457b167e2302000079`

- `Development/results/fma-vs-nvidia-20260928/README.md`：`e356c05257389dc22d9dcc929f86f0aa6adb169f69a837630822951080ee00ba`

- `Development/results/fmed3-ovfl-20260927/README.md`：`adf7b22bed19dd75cf18cb9e0d99ac2e4271ac3bb63f7b63ba19f1f2c8dee914`

- `Development/results/fp8-sat-mode-20260927/README.md`：`b6dc5001588acbf77d41a08ec96c8e2602bac09bed22bf923ef96f2b75e7e7db`

- `Development/results/fusion-round3-20260928/README.md`：`dac7a1f4f3c719fbf806d30153ccfa6e7a5b31a942753c42142665b40921bd56`

- `Development/results/kernel-map-20260929/README.md`：`71adbda551e4a7a7f82344ee336a70828cd930009c8f4becfac90d458b25ea47`

- `Development/results/llvm-fork-20260929/README.md`：`b630811c74a9e056dde8e89cdd978268302fd584b0b7e0247f26bf2909141e3f`

- `Development/results/llvm-patch1-20260929/README.md`：`acb4087e5bedf1109cc607448521de8d7e85353a5b3da5cb83df071216814eda`

- `Development/results/mh-round1-20260927/README.md`：`f5b6c492a0f63467d9449e17a580c194f2c83949aa09f73bbbbf19d48b35e7db`

- `Development/results/mochizuki-022-20260928/README.md`：`028194f04cc036aa01f11686e8286e58584038cfef5e182e36f14e473a35d566`

- `Development/results/onimusha-presr-20260927/README.md`：`e75e66e98cc0de75254fd824092641a5ce2cf78a59d2daae4cff903affdf0e1a`

- `Development/results/ovfl-census-20260927/README.md`：`e8cb9d5726f2100f1883ccee8dbe56b1779ac2944038a56f8463ac55a7210fa1`

- `Development/results/pack8-20260927/README.md`：`8948be35815b8851bf7e57415fecbab18545a4883fdc02dabab075a4c5b23650`

- `Development/results/pdl-audit-20260927/README.md`：`164766353e5f807755044103c6f75b9339d816711579a4617d4b1598e21fc36b`

- `Development/results/re9-runtime-leak-20260927/README.md`：`08d2e20acfe3e4b3a679bbcdc20ecaa3628bf9bbb793d1a7d00f1d3cfc495f62`

- `Development/results/resident-spike-20260929/README.md`：`c10b4793d3e8038c367f0db6fda4b8d063ff606bbfbf31a9ad0210dbec2fef53`

- `Development/results/swin-persistent-20260929/README.md`：`3382700c74c435bb53220b52bd9761ff4d9b5c3f83708f650513dbd9754ca293`

- `Development/results/swin-persistent-c128-c64-20260929/README.md`：`82870e1ff9271ce662b7dbdf4b87fceaadba98e7c806c9a77169ad97df24a270`

- `Development/results/tier900-20260927/README.md`：`0236c637078af85a030f740b25c58be3f25586972dfb34a82c913f41921a2b1e`

- `Development/results/transpose-persist-20260927/README.md`：`c06ac61d256c9154aa297ff05d6446f3809ea3bf232e3b5516cfb01a38e0b7f5`

- `Development/results/vit-attention-20260929/README.md`：`b389538667d8f4979841f60722f2330dbc187d58803c1963cf281fee2791f541`

- `Development/results/vit-bytestream-20260927/README.md`：`dcc16fc81e83faa595da3c3a29f7ee5fef907686beb2c13a4c78e57880d2d143`

- `Development/results/vit-qkv-20260929/README.md`：`1a4aaf21033181830b4b251fe7abfecd3f7c84fd462296118c36580d3ee94b8b`


<a id="since-0930"></a>

## 2026-09-30 至今及跨版本专题

### 2026-09-30｜small-cuts：D 对角残差、PK_ACT、ViT QKV 范数｜部分后来采纳 / 性能无收益 / 未实现

D=`CW_DIAG_ONLY` 跳过全零K16半块，19 SAME，900约−.02ms、1080−.02～−.03ms，初因不足0.5%暂缓；09-30 08:01获准进入生产，**不是永久负账**。`HIP_FFN_PK_ACT` 降855→831 VALU，DK与D差在噪声内、单项测不出收益，不收。QKV改32项求和/范数片段无法保持原求和/half舍入合同，未作同数学候选。不要把D的旧门槛重用到当前，也不要由PK指令减少推收益。证据：[small-cuts](results/small-cuts-20260930/README.md)。

### 2026-09-30｜small-wins-retest：Q 与 I/P/O｜性能负；旧 I/P/O 被重新采纳

现役a80db313+31模块，19 SAME。Q为`CW_POST_FULL_TILE` post内部窗口快路（finish F已另采纳）：900首轮−.002ms，1080首轮+.0015ms，量级约.005ms，与09-27一致，仍不收；I/P/O原因不到0.5%淘汰，经新门组合C三轮900−.026/−.029/−.030、1080−.016/−.028/−.016ms、p99不差后采纳。须分清旧门槛淘汰与真实负；Q需新机制而非合包改名。证据：[重测及逐候选表](results/small-wins-retest-20260930/README.md)。

### 2026-09-30｜deep-tail V：VIT_QKV_TAIL_VEC｜性能负

QKV w5最终量化字节经本wave空闲norm LDS转置，16×b8→b128，AoS/数学不变、19 SAME；不增加LDS容量或全WG barrier。三轮900−.013/+.007/−.006，1080+.014/−.007/+.008ms，合并1080+.005ms；不收。C512同批S宽尾已采纳，不能归为“所有宽写失败”。重试须有不同供数机制；10-06 butterfly是另一机制且另负。证据：[deep-tail](results/deep-tail-20260930/README.md)、[routing纠正](results/mochi-driver-cache-20261006/qkv-store-routing-correction.md)。

### 2026-09-30｜deep-tail2 E/Q：expand尾宽写、C256 LINE_STORES/PAD_ZERO｜性能负

两者19 SAME。E经LDS转置hidden字节：900三轮+.011/+.012/+.012、1080+.005/−.004/−.006ms，900p99 7.918→7.931、1080 10.826→10.833。Q尾整行写+填充零8B写：900+.010/+.002/+.002、1080+.011/−.007/−.001；1080p99 10.816→10.828。两者均止；Q原9条b8在填充冷路，主输出早已b64，不能继续按“热逐字节出口”估收益。证据：[deep-tail2](results/deep-tail2-20260930/README.md)。

### 2026-09-30 / 10-03｜C512_COMPACT_VT｜性能负

V在LDS改[channel][token]，64条ds_load_u8消除但新增32条b8写，19 SAME。首三轮900−.0040/+.0001/+.0036、1080+.0023/−.0015/−.0142ms，900合并近零，不收；10-03复审900−.0086/−.0050/−.0121，1080+.0061/+.0026/−.0018ms，900p99 7.293→7.342，仍不收。须新依赖/资源证据，不能只重复少LDS指令论证。证据：[VT](results/c512-compact-vt-20260930/README.md)、[终局复审](results/pending-review-20261003/README.md)。

### 2026-09-30｜W16 推广 C64/C128｜性能负

`W2_FFN_W16_SMALL`/`HIP_SMALL_FFN_W16`，两K16权重片段一次16B读，19 SAME及导出/回退门通过。C64三轮900+.005/−.010/+.003、1080−.012/+.001/+.000ms；C128900−.016/+.004/−.003、1080−.010/−.009/−.001。900不全正，停止；C256 W16同期已采纳，不能以此否定其成功。小权重已缓存是解释而非独立因果测量；新形状/资源证据才重开。证据：[W16小通道](results/w16-c64-c128-20260930/README.md)。

### 2026-09-30｜F-sweep V/M/W｜性能负

删冗余F、往返及范围内倒数，穷举与19 SAME。V(vit-stream byte_F)900+.003/+.004/+.004、1080−.014/−.014/−.008；M(mh q8)900−.009/−.010/−.022、1080+.009/+.007/+.015；W(wave2/SP up字节)900−.004/+.013/+.007、1080+.004/+.004/+.008ms。均不开宏；D deep_fast同批成功已收。值域等价≠跨模块性能等价。证据：[F-sweep](results/f-sweep-20260930/README.md)。

### 2026-09-30｜fill-cu：SPLIT_N、MIX_PIPE、decoder skip预读｜性能负 / 噪声

同K序核级SAME：mix/attn-project每wave32列N-split慢1～3µs；mix软件流水模式1+2.9µs、模式2±1µs噪声；decoder skip预读−1.5/+2/+1.2/−2.5µs，不全正。均不收；同批`HIP_C512_HOIST_RES`成功，不能把残差外提都归负。重新试须证明实际关键路径变化，不从“填不满CU”假设出发。证据：[fill-cu原表](results/fill-cu-20260930/README.md)。

### 2026-09-30｜hoist-loads 的 SP 推广｜性能负

条件读外提已在C64/C128及C32 up成功；同宏移SP，19 SAME，900−.008/−.020/+.006、1080−.010/−.002/−.006ms，900有慢轮，SP宏保持关。不可把两个普通模块合并收益外推到SP。证据：[hoist-loads](results/hoist-loads-20260930/README.md)。

### 2026-09-30｜gap-fusion G：gather折叠｜性能负

两个ViT gather分别并head Down尾/decoder39头，省2派发、同置换、19 SAME；900+.004/+.011/+.005、1080+.004/−.006/−.004ms。打散访存抵消派发节省，宏关。重试必须不同地址/供数机制，不只说减少派发。证据：[gap-fusion](results/gap-fusion-20260930/README.md)。

### 2026-09-30—10-03｜gap-fusion T：C512_T8_NO_F32｜未采纳，后来宏失效

当时t8 FFN删无读者f32 contract副本：19 SAME，900六轮−.021～−.036ms；1080一轮+.008ms，两批合并p99均+.003ms，不收。10-03现役`split_ffn_one`替换旧t8后宏无作用对象，复审不再把它当有效待合包刀。新代码若无该写出不得重测空宏。证据：[gap-fusion](results/gap-fusion-20260930/README.md)、[pending-review](results/pending-review-20261003/README.md)。

### 2026-09-30｜C512_MIX_LDS_G：FFN按W5共权｜性能负

每8WMMA共享B，经LDS共权；同核数学/原导出保持。900单核慢1～5%，1080快1～6%但批间漂，止于原型，不写宿主/整网。权重在L2、A仍f32供数，省B请求不证明瓶颈改变；不宣整网负数字。新供数/类型须先新证，不直接照搬ViT W5成功。证据：[c512-ffn-lds](results/c512-ffn-lds-20260930/README.md)，基准现装8e84f7c0。

### 2026-09-30｜Infinity Cache / MRU scratch arena｜性能负；“没有池”假设错误

生产已有生命周期best-fit池。候选优先最近空闲buffer，18 SAME，900整网约+.01ms、1080持平，不收。C32单dispatch135/194MB不能塞64MB MALL，不能由峰值带宽估.3～.8ms收益；原数据录于字节化/PAD16之前，不伪最新shape。新候选需真实复用/冲突证据。证据：[infinity-cache](results/infinity-cache-20260930/README.md)。

### 2026-09-30—10-01｜handoff-poll / handoff-gpu / same-queue｜探针及整帧负；研究边界

D3D→HIP WriteBufferImmediate+StreamWaitValue32探针省.054ms、后测约.08ms，未达当时.1ms门；完整NR回放未兑现（约.01～.04ms更慢），不收。HIP→D3D分片draw自旋比fence慢.04～.17ms，7.4ms活双poll gap7.61～7.68 vs单入poll7.50～7.51ms，64MiB .273 vs .084ms，尾更差；不做全帧/长跑。same-queue对照同D3D第二队列也有~.20～.28ms往返，同队列~.015ms，不能把队列同步当API固定税或由此证明另Vulkan队列必快。CPU/API缺共享HIP游戏queue路线属未实现，不算实测所有后端失败。证据：[poll](results/handoff-poll-20260930/README.md)、[GPU](results/handoff-gpu-20260930/README.md)、[same-queue](results/same-queue-20261001/README.md)。

### 2026-10-01｜C512跨块队列 P / 八无依赖streams｜有意错输出的上限诊断负

编码23—30全部读同输入、8stream并发，**不是正确模型候选**。三轮900+.21～.25、1080+.31～.33ms，p99更差，暂停P及同形A建议。功耗/L2解释未分别测，不把该多stream探针当所有带依赖队列的严格上界；10-06审计已纠正此过度结论。重开需不同已证依赖/调度机制，不能只降低stream数盲扫。证据：[xblock](results/c512-xblock-queue-20261001/README.md)、[边界纠正](results/mochizuki-gap-audit-20261006/README.md)。

### 2026-10-01｜W2_DOWN_FUSED / resample-fold｜性能负

C64/C128池化折链尾，19 SAME，省2派发/一次重读却三轮900+.05～.08、1080约+.12ms。尾部串行、LDS/稀疏访存为推断；C32/C256同尾方式没有GPU试，不伪四族都测。新的window-local/regcache供数须区别原raw-reread尾，10-06后续另列。证据：[resample-fold](results/resample-fold-20261001/README.md)。

### 2026-10-01｜SP_ENDS 编码两端并C256队列｜新基线性能负

逐位+超时恢复/票号压力通过。旧基线有正，换新C512FFN基线1080三轮均慢且p99退，止、不装；解码49/55同机制未扩。保留两基线不挑旧正值。C256内六层SP已成功，不是持久化整体失败。证据：[c256-gap的两基线表](results/c256-gap-20261001/README.md)。

### 2026-10-01｜C512 B8 / PROJ_FB8 v1/v2/v3｜早测宿主不对称已修；最终无稳定收益

投影残差fp8：早v1三轮900+.036/+.009/+.012、1080+.146/+.140/+.149ms，v2~+.04/+.20；后发现基线与候选不同宿主，不能归数值布局。修为同Pfb8宿主后v1 1080−.013/−.003、900+.011/−.010，噪声内；v3宽16B读写19 SAME，900−.007/−.007/−.000、1080+.001/−.011/+.002，不收。不得继续引用早+.2ms当纯B8因果，也不能称修后采纳。证据：[mochi-gap §6](results/mochizuki-gap-20261001/README.md)、[pipeline §2](results/c512-qkv-pipeline-20261001/README.md)。

### 2026-10-01｜C512 QKV_PIPE及范数微改｜无收益 / 未实现

QKV预取等核级/19门无合格整网收益（各表在原README），norm/softmax消融仅约3µs但改变输出/下游，不能据此当优化上界；串行norm改变求和顺序不做逐位候选。同期FUSEQKV/U4/DEEP4+SCHED1后来采纳，不把深流水整个否定。重试须新实际关键路径证据。证据：[mochi-gap §10—11](results/mochizuki-gap-20261001/README.md)、[pipeline](results/c512-qkv-pipeline-20261001/README.md)。

### 2026-10-01｜C512_FFN_PIPE｜性能负 / 溢出

mix→FFN手排预取，19 SAME，900+.077/+.065/+.061、1080+.167/+.145/+.176ms，明显慢，宏关；不能由“深流水比对手浅”再重命名重做。FFN_F8W/FFN_ONE/W2是后来不同候选且已收。证据：[pipeline后段](results/c512-qkv-pipeline-20261001/README.md)。

### 2026-10-01｜C512 FW2早尾门｜后来复审采纳

FFN双wave最初19 SAME、1080合并p99+16µs而暂缓；补三轮与旧六合后+5µs曾仍未收，后来依据当时现装基线最终采纳。不能拿中途表当FW2永久失败；具体基线/裁决演进见原§10/11/12，不拼成一次三轮。证据：[mochizuki-gap](results/mochizuki-gap-20261001/README.md)。

### 2026-10-01｜C512_PROJ_DEEP d4、PROJ_WN4 w2/w1｜性能负 / scratch

d4环流水19 SAME，900+.221/+.245/+.249、1080+.223/+.225/+.229ms，p99各退约.24/.21；VGPR255、42～62scratch，多种sched_barrier/基址写法仍溢出。w2 900+.013/+.011/−.004、1080+.018/+.006/−.008；w1 1080+.024/+.033/+.027ms，均止。新寄存器/依赖实证才重开，不能从wave数或少读量预估赢。证据：[gap-map-evening](results/gap-map-evening-20261001/README.md)。

### 2026-10-01｜W2_UP_DIRECT、W2_SKIP_BYTE、HIDDEN_TILES/QKV_FUSE｜未采纳

UP_DIRECT以高分辨率token作B列、去shuffle，19 SAME，两档三轮全慢，p99 7.642→7.670/10.430→10.474。SKIP_BYTE双写half池化/byte Up skip逐位但持平偏慢，不收；HIDDEN_TILES/QKV_FUSE在当前小通道形状亦无合格收益，具体候选/表见§6而非泛指所有融合。同期W2_UP_VEC成功，不能否定Up全部组织。证据：[c128-c64-inchain](results/c128-c64-inchain-20261001/README.md)。

### 2026-10-01｜CW_UP_LOW_BYTES｜性能负；首launch名单缺失已修

块65尾byte→C32Up，19 SAME，900+.004/−.036/−.026、1080+.021/−.018/−.003ms，1080合并近零、不收。首launch719是宿主漏_lb线程表，修后才有有效性能，不是数学失败。不同于已收C64/C128 Up LB。证据：[swin-body-gap §5.1](results/swin-body-gap-20261001/README.md)。

### 2026-10-01—03｜decoder NT、DEC_F8W及bitexact-pm组合｜性能负

NT4三轮全慢，NT2 900一轮+.0014ms，不收（逐位）；DEC_F8W正确性通过，单测与合包均没有稳定收益，10-03终局900+.0028/+.0040/+.0041、1080−.0032/−.0843/+.0270ms，900p997.554→7.562。bitexact-pm只收DW，不收F或DWF。不要把首fast-tier旧shader配IO_FUSE的35dB当F8W数学坏，那是错误decode绑定，已校正。证据：[tail-c32-gap](results/tail-c32-gap-20261001/README.md)、[pipeline §11](results/c512-qkv-pipeline-20261001/README.md)、[bitexact-pm](results/bitexact-pm-20261001/README.md)、[终局](results/pending-review-20261003/README.md)。

### 2026-10-01—03｜IO_FUSE / input-slim｜性能尾负；错误shader批无效

decode读f32网络buffer、保f32→half舍入跳neural pass，19 SAME；六轮平均900−.004～−.020、1080−.012～−.035ms，900p99+.020ms，不收。合包再现900p99约+.06；10-03终局1080一轮+.0193、900p997.484→7.494，停止。旧codec E5BB3862不认buffer导致35dB，修0C3CAABD后7×12 SAME，只否定错误shader配方不否定数学。新的带宽/texture路径证据才再议。证据：[input-slim](results/input-slim-20261001/README.md)、[bitexact-pm](results/bitexact-pm-20261001/README.md)、[pending-review](results/pending-review-20261003/README.md)。

### 2026-10-01｜ViT ATTN_M32 / TM2/TM4 / W5T2/H2/BIG｜性能负

各19 SAME；M32两16-query链复用K/V，900+.201/+.213/+.213、1080+.145/+.144/+.147ms。TM4 900+.100/+.112/+.125、1080+.090/+.084/+.086；TM2 900+.105/+.102/+.100、1080+.122/+.119/+.100ms；W5T2/H2/BIG均不收，BIG最接近仍约+.030/+.065ms。A/A clean约−.001/+.003。减少独立wave/供数形状损失已试，不再把“两query共K/V”当未试。证据：[vit-1080-gap全部表](results/vit-1080-gap-20261001/README.md)。

### 2026-10-01｜fast-tier KS / MS / C32 PK｜有意有损仍性能负

KS两wave拆key+LDS合并：900+.009/+.004/+.020、1080+.044/+.052/+.042ms，不进。MS为SP FAST3实际替换，900+.0084/+.0065/+.0172（合并+.0107）、1080合并约+.0004且尾退，非空宏未试。C32 PK f16成对激活相对CF慢约.22/.32ms，原pkrtz/拆高half往返抵消；不因有损即快。CF/MF与后续PF已采纳不列失败。证据：[fast-tier表/§4](results/fast-tier-20261001/README.md)、[MS触发核实](results/sp-fast-coverage-20261006/README.md)。

### 2026-10-01｜rebuild-baseline / KSPLIT宿主额外探测｜回归已修；A/A门需区别

HEAD宿主对现装e22d15a2六轮900+.026/+.014/+.010、1080+.009/+.003/+.011ms，p99均退；定位每块未命中HasFn未缓存与后缀检查，编译关未收KS入口/负探测缓存后系统慢消失。修后同代码12轮符号混、A/A也+.010/+.007，不能把未达“轮轮快”称代码仍性能坏。后可复现重装已采纳，旧坏宿主证保留。证据：[rebuild-baseline](results/rebuild-baseline-20261001/README.md)、完整历史后续基线裁定。

### 2026-10-02｜ideas-yi：FFN_PROJ_FUSE、ADDR_SKEW、C32 builtinRTZ｜两负一后续范围采纳

8wave C512 FFN+projection静态融合19 SAME，900+.101/+.086/+.091、1080+.207/+.145/+.107ms，p99退；跨块同形未做。256B/4KB/64KB张量/权重地址错位全在AA±.05ms噪声，无冲突信号，停。builtinRTZ全域等价/19 SAME，但900−.050/+.037/−.010、p997.360→7.433；仅1080 route后来经范围验证获准采纳，不永久否定builtin。证据：[ideas-yi §1—4](results/ideas-yami-ikaruga-20261002/README.md)。

### 2026-10-02｜PDL强开1080分体路径｜性能负

原1080整块融合本来PDLcalls0；硬绕为split+PDL，runtime三轮1080+.076/+.077/+.049ms，900本路径未变中性，止于此不跑无必要19。不能由requested1/effective1推断实际PDLlaunch。证据：[pdl-1080](results/pdl-1080-20261002/README.md)。

### 2026-10-02｜NET timing always-on / event修法 v2/v3｜常开及伪修性能负；后时序修复采纳

原GPU时间接口无值问题：always-on六轮900+.043～+.051、1080+.013～+.102ms，改懒启。试v2 EventSync收割与v3 end前普通4Bmemset+fence判完成都未修可靠性且v3 E六轮+.11～+.16ms，撤回。按调用顺序endRecord后立即非阻塞Query才真正解决塌陷，诊断开销+.06～+.15ms按功能采纳，不能当优化快。错误负/近零event值不作性能数字。证据：[net-timing §后续](results/net-timing-20261002/README.md)、[d3d-hip-stall无效首扫](results/d3d-hip-stall-20261004/README.md)。

### 2026-10-03｜FAST_ViT/C512 P3 half残差｜性能负；PF其它分量已采纳

C512 attention投影f16残差逐位，P3两轮均慢，停止；散2B写/残差读占比解释有限，不推所有byte路径。ViT bit0/1与FFN bit2/3单项弱但PF整体三轮900−.0891、1080−.1000ms且画质门通过已收，不能把其单项噪声归为永久负。证据：[fast-vit-c512](results/fast-vit-c512-20261003/README.md)。

### 2026-10-03—06｜多遍外推/预测/跳块及真游戏首帧｜质量取舍与集成失败，非同算法优化

一阶x+k(f(x)−x)最好k约2，y1→y3仅26.5～28.4dB、外推29.0～31.1、真y2→y3 31.2～34.9，替代不了三遍；最多低频对比选项。后prediction两遍预测第三是另算法/用户选项，不覆盖旧外推。MP3后续跳ViT+C512省13～14%但对全三遍约34～35dB，效果也被拿掉，不是便宜同三遍。鬼武者预测档01:07黑屏/弹窗、Prepare成功后pendingEnqueue，原文未取，回滚后恢复；lazyModule/fence死锁仅待证假说，不判网络数学坏。证据：[multi-pass-extrap](results/multi-pass-extrap-20261003/README.md)、[skip](results/multi-pass-skip-20261003/README.md)、[predict及failure](results/multi-pass-predict-20261004/README.md)。

### 2026-10-04｜C32-small / ViT960 / C512 LUT｜小筛性能负

C32固定1440三轮−.00961/+.00226/−.02536ms，不入19。ViT960 FAST15常量化有慢轮，不整网/游戏；原actualattention对拍通过。C512激活65536half查表含非有限全byte同、实际FFN两档0diff；1080隔离FFN一快一慢一平止，900漂移不能挽救。均非正式整Frame/p99结论；重新试需新硬件/资源机制。证据：[C32](results/c32-small-20261004/README.md)、[ViT960](results/vit960-20261004/README.md)、[LUT](results/c512-activation-lut-20261004/README.md)。

### 2026-10-04｜C512 direct-pack单项1/2｜单项拒，组合3后来采纳

单项混合/输出pack有慢轮，拒；组合3九轮孤立均快，再整Frame正式三档900/1080/1440均值−.023/−.042/−.103ms、p99好后采纳。不能把单项不收写成direct-pack整线失败，也不能乘16块微测收益。证据：[direct-pack](results/c512-direct-pack-20261004/README.md)、[whole](results/c512-direct-whole-20261004/README.md)。

### 2026-10-05｜vit-contract960 T32W2｜静态资源止，无GPU数值/性能

两个16token wave共享64列权重的大tile，原208VGPR/0private/4KiB→256VGPR/171spill/688Bprivate/32KiB LDS，直接停，不能称实测慢或不逐位。gfx1201产物SHA5dedd36053a82b989cc65271f193819095c7b807007549097cbb130d2b857084。新的no-spill机制才可议，禁止同组参数扫。证据：[metadata](results/vit-contract960-20261005/resources.json)、[配方](HIP/experiments/vit-contract960/prepare.py)。

### 2026-10-05｜原生1440 / final-direct 的旧参考与工具门失败｜已修/仍不知旧异常根因

native1440首AE对旧R有720-motion四帧不同，旧R A/A通过；同HEAD同命令新baseline AE/CSV/roll全通过，但旧R根因未定位，原失败保留。final-direct首次TimingOnly误用于逐帧19导致MissingFrames，换正确runner通过；graph+PDL互斥baseline拒，候选graph明回旧copy。均不可当已收优化数学失败或拿未跑graph性能外推。证据：[native1440](results/native-1440-optimization-20261004/README.md)、[final-direct](results/final-output-direct-20261005/README.md)。

### 2026-10-06｜C32 norm-transpose T｜真实布局正确性失败；静态性能判撤回

初-S漏实际internalize/辅助target曾误报VGPR169→193及占用恶化，撤回。实际FAST1 baseline ELF f46b8af2…与stock全同，T LDS4096→4608、实际occupancy16→14，不单凭指令多判慢。首4288 fixture1 raw/square同但sum/inv/FP8各512diff，止无perf。独立矩阵定位原QKV已tokenlane，最初轴理解倒置；布局证明同错前提撤回。后原布局H范数外提另成功，**T失败不是norm hoist全线失败**。证据：[T完整纠正](results/c32-norm-transpose-20261006/README.md)。

### 2026-10-06｜C32 direct-feature FFN少量化阶梯｜有损研究暂停 / 收益不足

24VALU少但改变原typedhalf合同，整71与旧raw约51.144dB、99.9991%组件bit变、MAE.00202784（非NVgold）。首NN ABBA GPU−.014088ms(~.159%)，wall−.013644，墙钟漂移−.02643ms更大，解释性实验STOP，不APP/不合生产。残差capture首次抓错时刻后已修，不拿错reference判数学。新真实PAL/精度收益证据才重开。证据：[direct-feature及capture修正](results/c32-direct-feature-20261006/README.md)。

### 2026-10-06｜C32 H norm-hoist 1152；H900旧严格p99门｜范围负；900后来采纳

原布局rsq(sum0)每lane一次，unit/prefix/whole0。1152实际完整HDR Frame首筛均值−.067694ms、p99+73.99µs，止，不能套900采纳。900正式3轮均值−.044929/−.049248/−.045163，R3p99+0.14µs曾严格FAIL；实际wall整数1µs截断/quantile稀疏审后裁为未分辨尾退，范围限定FAST1/1600×960/MP1/nongraph等，经完整兼容后采纳H900，**不保留“900已失败”假终局**。证据：[APP](results/c32-norm-hoist-framework-20261006/README.md)、[统计审](results/c32-norm-hoist-stat-audit-20261006/README.md)、[范围兼容](results/c32-norm-hoist-compat-20261006/README.md)。

### 2026-10-06｜ViT B/C16、B2 halfclamp、C2 keyperm、M32×halfmath｜有损负 / 不稳定未采纳

同full71 encoded1088/640/FAST1比较：B half-score+map均值+.007611ms、p99−.015198；C再half64den+.036425/+ .028335，relativeOLD52.20/52.63dB非NV准确度。B2实际halfclamp码gold0，但GPU−.006105/p99+.054664、未形成可靠收益。C2 keyperm GPU−.004771、p99−.068848，控制漂移更大，不正式。M32×half组合61386均值+.149293、p99+.129314，wall+.153728/+.180980，两个B都慢、STOP；修其grid减空WG79430仅−.003263、漂移+.0149，不翻旧负。每条不同数学/布局，不把M32旧负笼统当交互未做或反过来声称交互成功。证据：[stair](results/vit-math-stair-20261006/README.md)、[halfclamp](results/vit-score-halfclamp-20261006/README.md)、[keyperm](results/vit-den-keyperm-20261006/README.md)、[halfmix](results/vit-m32-halfmix-20261006/README.md)、[grid](results/vit-m32-grid-20261006/README.md)。

### 2026-10-06｜ViT V TR-load｜性能尾负

actualPAL机制映射后原math硬件bytegold及whole0；24057 NN1088 warm80/measure160首ABBA，GPU−.035052ms但p99+.032038，wall−.010447但p99+.023850ms，止不APP/不合。实际VTR早于QK、71/72静态指令后才AV wait，不可改名“提前V加载/藏在QK下”重做。证据：[TR-load](results/vit-av-trload-20261006/README.md)、[实际调度审](results/mochi-driver-cache-20261006/tr-load-schedule-negative.md)。

### 2026-10-06｜QKV packed-butterfly最终AoS宽写｜性能负

不走旧LDS、固定word butterfly 16DS+8v_perm等，完整quantizedbytes/数学同。unit/whole0；14169纯NN小正−.01493ms低于控制+.01942漂移。实际APP72086 1152旧9.6365→新9.6918125ms（+.0553125），p99+ .23983ms，两个B均慢，STOP。最初动态源寄存器索引假设不成立、修成真实16DS成本已保。不是未试的“仅store宽写”，不可再建议同lane映射。证据：[butterfly](results/qkv-store-butterfly-20261006/README.md)、[sourceword纠正](results/mochi-driver-cache-20261006/qkv-store-routing-correction.md)。

### 2026-10-06｜ViT physical column producer+TR consumer配对｜首错误模块批无效；正确FAST1后仍未采纳

首59652用了normal producer而currentconsumer FAST1，身份不匹配，原失败保留，不当有效数学负。更正实际877e→03de producer/11a25→38da consumer，全两输出gold0；80832同71/1088每槽1928实际call，GPU−.005841ms低于+.040064控制漂移，wall+.005788/p99+.004340，STOP。旧9/23 V-only直接产column及9/26V-only消费另早负，不混本全Q/K/V配对。需真正新供数，不说“改正模块后还没测”。证据：[column完整演进](results/vit-column-layout-20261006/README.md)、[actual-fast-fixed receipt](results/vit-column-layout-20261006/actual-fast-fixed/receipt.json)、[配对失败审](results/mochi-driver-cache-20261006/paired-column-layout-failure-review.md)。

### 2026-10-06｜C512 den 8→1冗余删除｜实际APP尾负

typed unit/whole0、189VGPR/6KiBLDS/0spill不变。纯NN首筛GPU−.014400ms，wallp99+1.1µs，未采纳；实际APP77060 1152旧9.68605→新9.68575625ms（仅−.294µs，小于14.4µs控制漂移），p99+54.17µs，STOP。实际uniformscale早已只s_load一次，没有再次hoist余地，不再以重复denominator为新方向。证据：[typed basis](results/c512-den-typed-basis-20261006/README.md)、[APP负及标量审](results/c512-den-app-preparation-20261006/README.md)。

### 2026-10-06｜prefix seed0 noise cache｜性能负

同GPU Box–Muller/half4 cached、seed/context/曝光/reset守门，两个FAST及seed0/1/ffffffff unit/prefix/main/down gold0；增加900档12.288/108816.712MB每帧读。1088 GPU8.914243→8.898674(−.015569ms)，900档6.655314→6.662606(+.007292)，900即止，不1152/正式。旧零噪声消融不是cache实测；不能拿对手计时外noise说必快。证据：[prefix-noise-cache](results/prefix-noise-cache-20261006/README.md)。

### 2026-10-06｜Up48→Body48局部融合｜性能未达，STOP

真实half Up→ordinaryBody48→SP49，保Up RNE与bodyFAST3区别，whole0/count161→160。92402首NN ABBA GPU−.003447ms小于+.022009控制漂移，wall+.003681/p99+.11964，STOP不APP/调参。旧raw-reread Down尾不能直接否定此不同供数，但此本候选已有负门。证据：[UpBody](results/up48-body48-local-20261006/README.md)、[typed审](results/up48-body48-audit-20261006/README.md)。

### 2026-10-06｜Body22→Down local / regcache / noescape｜三个候选分别止

local保存half池LDS41216、whole0/161→160：79646 GPU−.014568低于+.032915漂移、GPUp99+.063217、wallp99+.07612，止。regcache共享池降32768LDS但private192B/lane（spill metadata0不代表没scratch），静态止无GPU。noescape修lambda逃逸，VGPR242/private0/32KiB、whole0，但70217 GPU均值约−.000006ms、p99+.009905，wall+.0112/p99+.03254，整线STOP。资源修好不等性能被救；需新组织证据不是再扫closure/opts。证据：[local](results/body22-down-local-20261006/README.md)、[regcache](results/body22-down-regcache-20261006/README.md)、[noescape](results/body22-down-regcache-noescape-20261006/README.md)、[独立资源审](results/body22-regcache-review-20261006/README.md)。

### 2026-10-06｜小请求晚一批复用 pool-coldage｜性能负

≤8MiB请求排除recent logicalbatch用过buffer（非物理完成精确last-use），PDLkeep/graph旧路。实际2560×1472/960/full71 FAST1 MP1完整Frame，raw0且reject34560实际命中；首ABBA15.680819→15.742244(+.061425ms)，两B均慢，停止不扫阈值/布局。证据：[pool-coldage](results/pool-coldage-20261006/README.md)。

### 2026-10-06｜StreamQuery / matchedCPUdelay / paired-vs-untimed pulse｜性能负；单timed后续采纳

同纯NN1088/640：C512起点StreamQuery GPU+.168719/wall+.079494ms，busy1µs GPU+.012646/wall+.011366；未复制空eventpair收益，不认为Record合同必flush。完整APP timed-vs-untimed T/U900mean−.008069但p99+.00658、1152+.009092/p99+.04424。paired正式900三轮快，1152R3p99+.08934ms，停止pair；后来单timed有效生产路径另获准，**不把整个提交marker判失败**。证据：[pacing](results/submission-pacing-20261006/README.md)、[paired](results/framework-submit-pair-20261006/README.md)。

### 2026-10-06｜single-event伪on / fallback / 真on｜前两测量无效，真单event后来采纳

旧O1/NET1 safe900实际Record0/fallback，正式wall+.020946/p99+.10405不是有效pulse性能负。O2/NET0 APP40018有Create但Record0，−.01776亦不归pulse。后orderedDown边界真acceptedRecord=N及lease门通过，900/1152有效门进入正式并采纳。不能用startup active1/allocated判断执行；新组合必须真实accepted计数。证据：[single-event完整纠正](results/framework-single-event-20261006/README.md)、[生产closure](results/pulse-production-20261006/README.md)。

### 2026-10-06～10-14｜hipGraph各代：旧Graph、samework replay、Auto产品替换（ReplayOff 161与ReplayAuto 162）｜旧无效/被修正/产品替换全部STOP

旧“所有graph无收益”撤回：固定161实际Fn、唯一DAG/原同工作，pure同scope3轮GPU−.080～−.096ms、APP pulseoff也3轮−.065～−.074ms，gold0，**这些成功不是失败**。APP70977首capture allocationpool未warm guard拒，后boundedwarm/capture修好，不计数学或性能负。实际产品EagerAuto vsReplayOff单门：Auto9.597319→Replay9.604294(+.006975ms)、p99−.0451、控制mean漂移+.013213，未过收益门，STOP不替换。2026-10-14补完 `ReplayAuto`（161 kernel + 1 timed EventRecord = 162node强DAG，`position=24` `C256Down -> EventRecord -> C512 firstFFN`）：修ROCm 7.0.2 CLR默认 `hipEventRecord(flags=0)` 捕获期不入图及 `hipEventRecordWithFlags(1)` 栈指针未初始化bug（改用 `hipStreamGetCaptureInfo_v2` + `hipGraphAddEventRecordNode` + `hipStreamUpdateCaptureDependencies`），12帧HDR序列2 gold 0-diff全过；3轮4-slot ABBA（480帧/arm）`EagerAuto` mean 9.541323ms → `ReplayAuto` 9.524071ms（`Δmean=−17.25µs`，处于12～53µs漂移带内），p99 9.732100 → 9.736100ms（`Δp99=+4.00µs`），且R3 mean（`+0.91µs`）与p99（`+28.64µs`）双退步，判定 **STOP**。旧opt.graph与实验固定图不是同一路径；至此hipGraph各代在RX 9070 XT上全线结题，生产维持EagerAuto。证据：[samework](results/graph0-perf-20261006/README.md)、[APP先失败](results/graph0-app-20261006/README.md)、[APP成功](results/graph0-app-perf-20261006/README.md)、[产品STOP](results/graph0-product-perf-20261006/README.md)、[ReplayAuto最小要求](results/graph0-product-baseline-review-20261006/replay-auto-minimal-review.md)、[ReplayAuto结题STOP](results/graph0-replay-auto-20261014/README.md)。

### 2026-09-29—10-02｜COMGR/LLVM版本及调度扫｜分行采纳；其余性能负/编译失败/正确性修复

公开LLVM21独立链ABI+168候选/168原gold0，但未ABBA，不是直接替换成功；LLVM20/COMGR2较慢、22初不逐位（细节早期片段），不能泛说23都坏。10-01调度静态拒：bottomup/topdown/itminreg指令+10～20%、nounrollpart private160B、某max-ilp/topdown spills；WPE1/2/4全20热核代码不变，6/8只少数小核。trackers vitcontract VGPR降却链内+161/+194µs；c64 iterative-ilp900近零。CS2正确且六轮均值快但900p99 7.437→7.492；CS3/CS4均有慢轮/尾退，不收。关VOPD C32+.143/+.225ms，关delay+.068/+.117，非免费优化；LLVM23 iterative-ilp多模块clang崩，CS6 c64 maxocc900−.013/+.020/+.009、p99+ .069ms，止。**c32/c64 LLVM23及c512deep max-ilp成功行继续采纳**，不归负。证据：[compiler-sweep各轮表](results/compiler-sweep-20261001/README.md)、[公开21独立链](results/llvm-fork-20260929/README.md)、[版本完整身份](results/compiler-versions-20260929/README.md)。

### 2026-10-02｜LLVM22/23 vit-stream/mh裸barrier错｜后来数学修复，但性能不收

初把FMA直方图当收缩根因，ffp-contract on/off与原同、不修；真正LDS裸barrier缺wait_dscnt，新编译器不再白送等待。WG_FENCE补后22/23都19 SAME，修后vit/mh没有稳定收益。22mh900+.003/+.021/−.015、1080+.018/−.005/−.021，1080p99退；不整套换编译器。real-true16默认导致inlinehalf operands编不过，需要明确−real-true16，是工具前提不是算法负。换新LLVM须先barrier_scan，旧“22全部不逐位”已失效。证据：[llvm23-vit根因](results/llvm23-vit-20261002/README.md)、[sweep §7—8误归因纠正](results/compiler-sweep-20261001/README.md)。

### 2026-10-06｜mochi受控scoreF32反事实 / ACO归因｜研究无强效，不是HIP候选性能门

67182同旧M d1185d2只换score数学SPV，GPU7.845119→7.854481(+.0093625ms)，control漂移+.0252125更大，STOP不能解释1.046ms差；无每帧p99不造。2026-10-11 ACO最新a9b13dd Windows作者表~17.7～20.9%耗时下降同时切Linuxshader/TCHAIN0x1ff/Npair/plan，非纯编译器A/B；PAL.text重封装不是HIP .hsaco可换后端开关，**未做本机ACO新优化实验，不能列实测失败**。证据：[score-only](results/mochi-vit-score-control-20261006/README.md)、[ACO机制与版本/性能边界](results/mochi-aco-survey-20261011/README.md)、[旧资产非ACO锁账](results/mochi-old-lock-20261006/README.md)。

### 2026-10-05—08｜history/temporal原型｜旧HALF4合同被证伪修正；运动质量仍未验证

早history f16RNE/write域依据不足，原CUBIN HALF4 gold证明应RTZ/内部blend域，后修；不能拿旧序列通过当原API数学。F64参考gate成本7204.779→7.081、1080档9.146→14.179ms只是诊断开销，不推FPS。真实闪烁改善/多人运动等缺实玩gold；MP2/3不实现时序时应合同拒绝，旧剑星MP2拒绝无NR输出不等MP1算法坏。后MODE3/currentMP1真实NR运行与F9guard已成功，旧失败单独保留。证据：[history-contract](results/history-contract-20261006)、[sequence旧修正](results/temporal-sequence-20261005/README.md)、[trial](results/history-trial-041a-20261006/README.md)、[当前live审](results/mode3-stellar-live-audit-20261011/README.md)。

### 2026-10-09｜legacy nativeD3D12 fourstage/full71/APP｜局部成功但非currentHIP，性能无提升

Agility721/SM6.10旧HLSL完整71数值自重复0，但相currentHIP PSNR51.382dB，数学/noise不同，不能称同算法port成功。freshpilot native23.388310GPU/23.574450wall vsHIP9.171576/9.472550ms（非ABBA，10warm20）；原生无提速。fourstage只4stage同gold，一pairGPU.57816→.52192但host.659→.788，不能外推wholelist；legacyAPP29430冷首帧通过，不是性能采纳。旧prefix错误root/noise/1088tailguard等先失败后fix按回执分开。四stage/APP是main仅JSON回执、无顶层README，见[四stage回执](results/nr-dx12-fourstage-20261009/receipt.json)与[APP回执](results/nr-dx12-legacy-app-20261009/app-receipt.json)，不造README链接。证据：[legacy总报告](results/nr-native-dx12-pure-20261009/README.md)、current独立分支下面一项。

### 2026-10-10—11｜current71原生D3D12迁移｜最终数值成功，性能约33×负；中间失败已修

独立worktree `/home/lmxxf/work/dlss5-hip-to-dx12-current`，分支`hip-to-dx12-current`、结题`b834a93f`，**非main合入**。60925受控1152/full71/161真实HIP launches→native、185/185端点+typedfinite+final26542080B SHA1a1f…bddf全0，17首写poison223637504B及SParena NaN通过。两HNNH10warm20：canonicalHIP9.2119955/native303.937879msGPU；production-selectorHIP9.272851/native301.099307（PDL1/1/0、非真实PDLdispatch benchmark），约32.5～33×慢。globalUAV fragment staging/int64精确适配为实现成本，不能归API固有差。39PSO冷建累计411551.813ms；328强持buffer allocInfo2005139456B非residentpeak，不伪HIPVRAM比较。STOP不追速度、不部署。

细失败归并：①C256/FFN mad实际非融合→signed64exactpoly2Mgrid0/fullbody0；②Up halfpack RTZ/RNE差→显式RNE后全族0；③C32/QKV squareHalf双舍入→exact48product→half单舍入，原185183PASS/2QKVFAIL后64856双unit0及60925全1850；④projection负小量化−0时机317byte差→只修F入口判零后0。以上是被修复数学候选，**不是整网迁移失败**。

驱动/宿主门：directHead及C32Up PSO amdxc64 AV，无NN数学结论；globalhalf桥候选60a443成功PSO+Up双gold，旧LDS候选也AV保负；SP recover嵌套/扁loop/独bodyPSO创建60s阶段卡，后来复用已成runPSO+GPU条件indirect恢复，真实893+fault门0。87902未record前累计90s只超cold预算；隔离1003同PSO27.279s成功，不说永久hang。5580已39PSO但stage1 nodes原R71全0（pre-first复用allocation非后CPUupload），独立重建28576B/893nodes/1926edge SHA5ddd…与已过fixture双核后T44/T159绑定修复。cachedblob954B误套tensorword对齐、Agility导出漏、WG128被硬256、getter把group误判up等caller失败均修保原证，不当kernel负。

原证路径：该worktree `Development/HIP/experiments/hip-to-dx12-current-20261010/{README.md,RESULTS.md,PROGRESS.md,host/hip_warm/results/abba-20261011/}`；小结果`Development/results/current-hip-dx12-multihead-20261010/`。真实大资产在 `/home/lmxxf/work/dlss5-dx12-artifacts/` 与 `/home/lmxxf/work/dlss5-dx12-recovery/`，主台账只引用branch/commit和绝对路径，不大合并。重新提速须更换global桥/资源组织且保math，不启动整套再移植来重复证负。

### 2026-10-12｜ViT contract→QKV tile-ready｜真实重叠、正确但性能负

0431ba0e实验结题；sameAoS/math/640，16列producer release/acqrel、consumer真实anyorder/acquire，全contract655360/QKV1966080 doublegold0/finite0/status0。firstWMMA完成marker36/157WG提前、ordered0，矩阵重叠已证。traceoff公平31969四slot10warm20，两边同D2H，reset计入candidate窗：GPU70.043→95.770µs（+36.7%）、wall106.475→141.603µs，两个B均慢于两个A；n40/arm不以p99宣普遍尾。原72359异步节奏不对称批、旧clockbuiltin编译fail、.643ms诊断值均留。不进入整帧/不扫变体、不部署。证据：[实验主报告](HIP/experiments/vit-tile-ready/README.md)、[公平CSV/独立统计](HIP/experiments/vit-tile-ready/measurement/results-31969/statistics.json)。

### 2026-09-30—10-06｜未实施方案 A/F/V、稀疏事件及计数器归因｜未验证/撤回推断

C512块内attention→projection队列A、mix→FFN→proj F、ViT八块全局栅栏V设计阶段估值不是收益；F后来8wave融合已负，A不由八stream探针严格证伪，V未实现不能写实测负。稀疏stage事件让全NN负扰动约1.8/1.3%，不能均摊成族份额；SPM busy/stall共分母/可重叠、SQTT缺完整phase/loss bounds不能当核因果。保研究结论，不以counter名或估值建议重开。证据：[设计](results/c512-vit-reorg-design-20260930/README.md)、[hardware scope审](results/hw-window-comparability-20261006/README.md)、[SPM](results/mochi-spm-20261006/README.md)、[当前DevHistory §12](DevHistory.md)。

### 2026-10-07—08｜lowfreq/mode1 准备与资产门｜环境/接口失败后修复

无顶层README的真实回执：lowfreq-runtime CPU59781及absent-byte-array缺陷修后68326 smoke过；mode1-assets27511 ExecutionPolicy阻拷、old.dll LoadLibrary在D3D/HIP前失败，failfast/path/hash修后73476通过。不能将这几项写成MODE1/2算法或性能失败；当前游戏观感/时序证据另scope，四module文件SHA差不等机器码数学差。证据：[lowfreq原receipt](results/lowfreq-runtime-20261007/receipt.json)、[mode1资产receipt](results/mode1-assets-20261008/receipt.json)。


### 2026-10-11｜MODE3 单遍高低频残差增强｜功能有效，用户未采纳收益/成本取舍

同一真实旧菜单、一次NN加相同decoder，原encoded修正MAE .01868645→.02713170（幅度+45.2%），共同显示映射RGB变化约4.51/255；局部脸发更暗/边缘反差增强，没有新细节或画质更好证据。当前MP1真实NR成功，不是旧MP2拒绝；用户本次报告约54fps、恢复MODE0后约58fps上下，**用户观察，非同场景ABBA，不能归出精确MODE3开销**。166fb5da已按用户要求关闭，强度1保留，不动DLL/modules。功能实现正确不等使用价值，缺真实运动/显示器质量评价不包装成功提质。证据：[客观对照](results/mode3-objective-20261011/README.md)、[恢复回执](results/stellar-mode0-restore-20261011/receipt.json)、本会话2026-10-11 18:57用户反馈。

### 2026-10-04｜启发式肤色保护｜功能通过，用户观感未满足、保持off

SKIN_PROTECT只多遍：原输入肤色启发式mask混y1/yN，不是原图/语义人物分割；技术门通过、默认0。用户02:16反馈保护后整体效果几乎看不出来、剑星真3约27fps，随后双游戏skin0；不能把真3相对预测3的成本归mask约.34ms。暖色背景误选、彩光/阴影漏以及face/neck一致性没有语义保障，未默认启用。重开应有新的整人/皮肤真实游戏评估，而非调颜色阈值刷单帧。证据：[skin-protect及用户关闭](results/skin-protect-20261004/README.md)。

### 2026-10-11｜YuNet脸检测demo与人物模型调研｜单帧成功，未选型/未集成，不是模型实验失败

旧1296×720菜单实际CPU单次检1脸(0.92676)，冷41.74ms/含准备约91.22ms，仅Linux aarch64/OpenCV，不能外推WindowsAMD低耗。真人权重对正常游玩小脸/转头/遮挡/多人未验证，用户小脸漏检、只face保护脖色差诉求属于下一评估问题，不虚构此demo已在该集成漏检。模型SHA8f2383e4…，人物/解析/RVM/SAM三路是只读调研，未下载/AMD推理性能门，不记负模型胜负。证据：[YuNet实测](results/face-detection-probe-20261011/README.md)、[人物候选与边界](results/character-mask-survey-20261011/README.md)。

<a id="coverage"></a>

## 证据索引与未核清项目

### 本次覆盖与来源身份

- 基线 `0431ba0e`：逐读当前 `DevHistory.md`、`WorkingPlan.md`，并回到两次压缩前全文 [2026-09-23归档](history/DevHistory-full-20260923.md)、[2026-10-06归档](history/DevHistory-full-20261006.md)，补读早期 [porting-worklog](history/porting-worklog.md)、[CURRENT-STATE](history/CURRENT-STATE.md) 等原生路线资料。历史全文并未删除或改写。
- 目录级独立盘点：**328个 results 子目录、266个 HIP/experiments 子目录**，另有results顶层3文件、experiments顶层69文件；文件不混叫目录。早期73个README、09-27—29的45个results入口及后期逐候选报告/receipt/metadata/CSV交叉核验；200个负关键词MD只是搜索候选，不是200个已判失败实验。
- [目录索引](results/failed-experiments-survey-20261012/directory-index.tsv) 保全部594个目录、是否在本文直接出现、独立扫描类别及证据入口。`document_reference`仅词面可检索性，**不是证明该目录每个脚本执行过或每个子文件已逐字阅读**；纯源码目录追同机制results/历史，不从脚本存在推运行成功/失败。新建本台账的辅助目录不算原328。
- [历史来源SHA清单](results/failed-experiments-survey-20261012/source-sha256.json) 锁全文版本。各候选已有产物SHA沿原receipt，不为排版重算或覆盖原数据。仓外 current-DX12 明确 branch/commit/绝对路径；未将独立分支大合并进main。
- 独立复核：早期作者检查200组标题/状态并修正成功分支误挂负标签；中期102组name全部存在；最后目录复核额外补C32 post-LDS、固定shape未收分支，以及无README的四stage/APP/mode1/lowfreq回执。Present回执相对RE9语境已修为 `Development/RE9/results/present-first-frame.json`。

### 有记录但不另造失败项的来源

- 成功部署/打包/配置/接口功能：package/release/config-layers/current-install/strength/issue4/PR12/bridge-stages；有实际失败后修复的只在对应条目记录，不把预期negative测试的“拒绝”当真实坏帧。product-fmt-reload 旧格式回归成功、新兜底格式未全硬件验证是支持范围，不是优化负。
- 成功优化的原报告仍保：composite-quant、c256-W16、C32 align/tail-vec、C512 NOF/RCP、shift-pack、final-direct、pool64-byte、SP1440、outside-net、已采纳的CF/MF/PF、H900及single pulse。本文只收其真实失败变体/旧裁决翻案，不用成功表填失败项。
- 分析地图与刻意破坏输出的消融：kernel-map/roofline/clock/SPM/hardwarewindow/c128-norm-cost/mhfast-tail-ablate、竞品静态对照不是有效优化candidate。比如C128空barrier重测只有+.0558/+ .1389µs，不证明真实依赖barrier免费；tail消融改输出不能当逐位候选或严格收益上界。已经纠正的计数器/几何/提交口径保原证，不增加一项不存在的实测优化。
- mode3/人物mask/YuNet分别是用户取舍、技术demo和调研，不将“未部署”自动等同模型失败；正文明确列未采纳/未验证边界。

### 仍缺原证或不能进一步裁定的项目

1. 早期压缩摘要中若干09-04/05候选没有保单项均值/尾/完整SHA，本文只列原“否决/未采用”及worklog行号，**数字未知**；不能给其补一份现代公平ABBA。
2. 旧实验逐帧大raw部分已按当时cleanup规则删除，CSV/摘要/hash仍在；原D盘路径是否现在可读本次未连接核验，不把历史路径当已恢复raw。仓外DX12数据明确保留位置，本次未读取约GB原tensor或重跑。
3. 早driver版本、gfx1200多为编译、游戏变频/温控与单帧质量没有跨硬件/跨版本因果保证。部分性能略正但当时门槛不足的项目只有“不采纳”，不证明普遍物理慢；缺真实生产调用计数的旧候选不套当前计数补证。
4. 未逐一解压所有ZIP、反汇编全部未活跃导出或重建已删tmp。目录索引中的source-only/analysis项作为可追溯边界；若以后找到**新的实际候选结果**，应追加其身份/裁决，而不是把本次搜索词命中或未命中充作胜负。

本次只整理证据，无GPU实验、编译、游戏操作、部署或push；未删任何实验/原始历史。台账的目的是先检索旧候选和真实失败条件，避免换名重复，也避免用旧失败阻断已有新证据。


### 2026-10-12 — C32 score upper clamp / NoHi 七层资格路由

- **目标/范围：** 证明实际FP8/Half量化链下upper不可达，仅七个精确权重SHA启用孪生_nohi；其余三层/未知保持stock。FAST1/1920×1152/MP1/graph0，原Auto2/PDL请求1、实际calls0。不是全局删clamp。
- **正确性：** 84738全部65536Half×32位置语义0；51888 mapped完整gold0；72895三帧raw0/finite0与21:9路由；25355四槽八首尾gold0、44模块一致。证明与候选正确，不属于数学失败。
- **性能/裁决：** 320帧弃80×ABBA，480/arm wall9.6336771→9.6284083ms（−5.269µs/−0.05469%），p99−17.790µs；control漂移+47.546µs，candidate漂移+19.875µs。收益小于漂移，**未证稳定收益、停止/不采纳**，不追加刷轮或变体。
- **重试条件/证据：** 只有可指向新的实际编译/热路径代价且有独立稳定测量前提才重开；不能将删掉32cmp等价速度改善。持久分支bound-and-compact，`Development/HIP/experiments/c32-score-nohi/` 和 `bound-and-compact/run_a_frame_timing.ps1`；原小CSV及SHA收录该实验results，build原始目录 `/home/lmxxf/work/dlss5-c32-score-nohi-build/frame-abba/`。

### 2026-10-12 — CompactC512Body fused unused mixed/contract New｜资源负，不采纳

- **改动/scope：** 仅将两个n×512 F32临时Tensor New移至原mix+FFN fallback；kernel/权重/数学/Run调用不变，当前Bcompact与NeverHistory已生效。逻辑8.44MiB不是实际池节省。
- **证据：** 26032完整Frame两role各3帧，实际48C512calls/fallback0、0900gold/repeat/finite/Auto3/modulesame均PASS；source正式O2与私有FrameO2通过。
- **结果/裁决：** 池290571128→292742008B，反增2170880B（约2.07MiB），均27tensor；weights423438120/gather5242880不变，driverfree只+65536B。best-fit复用重排使逻辑删除未带来净资源收益，STOP/撤回，不做性能ABBA，不称数学失败。
- **不要重试/索引：** 除非有独立新池生命周期证据，不能换名再删同New或将64KiB计数粒度当胜。实验源/正式O2/source-diff在 `HIP/experiments/c512-unused-tensors/`，原Frame资源数据在 `HIP/experiments/resource-followups/measurement/`；主终态/source身份 `HIP/experiments/resource-followups/results/stage2-build/`，branch resource-followups。

### 2026-10-12 — Auto2 host QPC timing｜有限测量，零扰动/精细GPU归因门未成立

- **范围：** 99fc33ba私有宿主11个host QPC点，无新增GPU命令/同步、无绕过Auto guard。95980数值门和31627 A/A完整43模块/0900/Auto320 reject0 PDL0全部通过，不是数学或路径失败。
- **干扰：** 320弃80×四槽、480/arm，off9.6242125→on9.647700ms(+23.4875µs)，p99+4.750µs；控制漂移28.725µs，两B均高于两A。正差无法与时变漂移分离，不能称零开销或精细排程不变；保留有限host粗定位用途。
- **裁决/边界：** 不追加A/A或GPU标记工具；完成动作9.188ms不是可删除CPU开销，GPU内部stage/空档无可辨证据。重评需实际可见HIP队列/校时可靠/保持生产路径的trace证据，不能仅再平均几轮或把CPU等待当GPU空泡。证据 `HIP/experiments/auto-timeline/`，原handle31627 exit0/LOCK_RELEASED。

### 2026-10-12 — ViT QKV V-only末rendezvous｜数值通过，筛选漂移/收益未证

- 改动：仅part2/V跳过末post-K WG rendezvous；K-loop、Q/K LDS完成/norm同步、inactivewave退出及math全部保，actualda86资源同、旧88Fn原字节同。8218编译/ISA与94277完整640tokens/1,966,080B双old双new gold/finite/canary门通过。
- 唯一662786 QKV-only A-B-B-A10warm20：GPU113.300/36.850/32.250/34.295µs，pooled73.7975→34.550µs但control漂移−79.005µs；wall159.600/90.400/88.350/86.450µs。原首A不剔除，不能归因pooled改善，亦不证明候选慢；STOP不整Frame/不重跑/不追加warm。
- 证据 `HIP/experiments/two-kernel-trials/v-tail/`、measurement原CSV；以后需新的可靠测量/关键路径证据，不能因依赖原理成立就扩刷。本轮不是VTR/column/软件流水重试，产品源未改。

### 2026-10-12 — Prefix main TH_STORE_NT｜正确，完整Frame收益未证/未采纳

- **改动/范围：** f46 prefix arg4 main八8B store TH0→NT，down两store/scope/waits/math/资源保持；侧车761984仅newexport，currentjoint43/既有资源优化/真实Auto均保。18221完整双输出gold0，79532实际候选3calls全Frame0900/finite0。68401是私有style默认资格误拒，candidate未执行，修host后成功，不归类算法失败。
- **实际测量：** 唯一89008完整Frame A-B-B-A各320弃80、480/arm：mean9.656520833→9.655660417ms（−0.8604µs），p99 9.88847→9.86799ms，max9.947→9.926ms；control漂移+26.408µs/B+31.129µs。槽均值9.643316667/9.640095833/9.671225/9.669725ms，后B略慢对应A。
- **裁决/重评条件：** 收益远低漂移，STOP未证可靠收益，非数学失败/非证明变慢；不单凭p99改善采纳，不换TH/刷轮。TH不是保证绕L2；重评须新的真实关键路径/缓存行为证据，而非重复同短ABBA。产品源未改。证据 `HIP/experiments/two-kernel-trials/prefix-th/` 与 `measurement/`，原handle89008 exit0/LOCK_RELEASED，初次误拒68401原证保留。

### 2026-10-14 — ViT `compactw` contract / projection W16（16B `global_load_b128`）｜静态 VGPR 膨胀掉 occupancy，不采纳

- **改动/范围：** 在 C512 W16 成功后，排查 ViT 8 块（`31..38`）中仍使用 `FragmentPackedMatrix`（8B `global_load_b64`）的两个紧凑权重核：`vit_stream_contract_frag_bout_compactw` 与 `vit_stream_project_n64_bb_compactw`（`vit-stream.hsaco` / `vit-stream-fast.hsaco`），尝试按 32-k 步 `_Pragma("unroll 4")` 改为 16B `global_load_b128`（`W16=true`）。（注：ViT 最大的 `vit_stream_qkv_f8w_bin` 在 0.42 已是 `PackedVitQkvCompactFp8` W16 16B 加载。）
- **编译器/ISA 审计结论：** 用 `compile-modules.py --only vit-stream vit-stream-fast --targets gfx1201 --target-feature=-real-true16` 实编核查：
  1. `vit_stream_contract_frag_bout_compactw`：VGPR 从 `211` 暴涨至 **`251`**（贴死 256 VGPR 硬件上限，且带 4096B occupancy-cap LDS）。
  2. `vit_stream_project_n64_bb_compactw`：VGPR 从 `161` 暴涨至 **`220`**，直接跨过 `170` VGPR 阈值，使每 SIMD wave 驻留数从 **3 waves/SIMD 跌至 2 waves/SIMD（occupancy -33%）**。
- **裁决/重评条件：** 停止/不合入，避免把掉 occupancy 的 ViT 改动混入已验证的 C512 W16（VGPR 81～187，occupancy 不降）。除非未来能在不跨 170 VGPR 阈值（如 `unroll 1` 或寄存器复用重排）的前提下保持 3 waves/SIMD，否则不再对 ViT contract/projection 套用 W16 unroll。证据：`Development/results/c512-w16-20261014/README.md`。

### 2026-10-14 — C256 `SpStage` 首尾层（`15, 22, 48, 55`）并入与 `sp_init`/`sp_recover` 合并审计｜合同不兼容且单层 `c64-wave2-fast` 更快，不采纳

- **范围：** 源码与历史回执全量审计为什么 Encoder C256（`blocks 15..22`）只将 `16..21` 放进 `SpStage(16,6)`、Decoder C256（`blocks 48..55`）只将 `49..54` 放进 `SpStage(49,6)`，以及能否并入首尾层或省掉 `sp_init`/`sp_recover`。
- **源码/历史铁证（四重阻断）：**
  1. **Encoder 首尾层（`15, 22`）数据格式硬不兼容**：`block 15` 输入来自 `Down(14)`（`mh_pool_project_group_c128_hin`），格式为 **FP32（`ByteIn=false`，4B/通道）**，而 `sp_run256_w16` 硬编码 `ByteIn=true`（1B E4M3）；`block 22` 为 stage tail（`raw=true, post=3, ByteOut=false`），必须输出 **FP32 `Hrtz(v)`（`33.75 MiB`）** 同时喂给 `skips[3]`（供 Decoder `Up(48)` 按 FP32 64B `b128` 读取）和 `Down(22)`（按 4B 步长读低 16 位 half）。
  2. **Decoder 首尾层（`48, 55`）在 1080p 下数值合同不同（`W2_FAST_NUM=3` vs `0`）**：`swin_persistent_network.h:59` 中 `SpFastTierMask()` 默认为 `1`（仅 720p 用 `swin-persistent-fast.hsaco`；1080p 因 `sp-fast-coverage-20261006` 实测慢 `+0.025 ms` 且改数值而保持 `swin-persistent.hsaco` 即 `W2_FAST_NUM=0`）。而独立 `Body(48)` 和 `Body(55)` 调的是 `c64-wave2-fast.hsaco`（`W2_FAST_NUM=3`）。把 `48` 或 `55` 并入 `SpStage` 会将其算术从 `W2_FAST_NUM=3` 变成 `W2_FAST_NUM=0`，直接破坏 0.42 Gold（`0900E489...`）。
  3. **`swin-persistent.hsaco` 单层内核配方显著弱于 `c64-wave2-fast.hsaco`**：`c64-wave2-fast.hsaco` 由 **LLVM 23** 编译且开启 **`W2_QKV_FUSE=3`（FFN+QKV 融合）、`W2_FFN_QT_BATCH=4`、`W2_HOIST_LOADS=15`**；而 `swin-persistent.hsaco` 因持久外层循环 VGPR 压力只能用 **COMGR** 编译且 **`W2_QKV_FUSE=0`、`W2_FFN_QT_BATCH=2`、`W2_HOIST_LOADS=0`**（此前尝试在 `swin-persistent` 开 `W2_QKV_FUSE`/`W2_HOIST_LOADS`/LLVM23 均因 VGPR `184→222` 退步 `+0.006..+0.27 ms` 被拒，见 `DevHistory-full-20261006.md:1277,1290,1353`），且每扩 1 层还需在 `plan.outputs` 多常驻 `2.21 MB` 非别名恢复缓冲（两层 `+4.42 MB`）。
  4. **`sp_init_pair` 与 `sp_recover256_w16` 不可删改**：`sp_init_pair` 早在 `swin_persistent.inc:27` 实现并实测负（`-0.001 / +0.023 / +0.003 ms`，`DevHistory-full-20261006.md:1378`），且 `CheckPulseCapabilities()`（`hip_reference_network.h:1006`）明确规定若模块导出 `sp_init_pair` 则直接禁用 `submit_pulse=auto`；`sp_recover256_w16` 是替代每次 `+0.485..+0.685 ms` CPU `hipStreamSynchronize` 的 `<1.5 µs` 1-WG 异步超时检查核（正常情况 `if(!sp_load(p.state+2)) return;` 立即返回）。
- **裁决：** 保持 `SpStage(16,6)` 与 `SpStage(49,6)` 现行边界及 3-launch 结构不动，不再重开 C256 首尾层并入或 `sp_init_pair`。证据：`Development/results/c512-w16-20261014/README.md`、`Development/results/sp-fast-coverage-20261006/README.md`、`Development/results/swin-persistent-20260929/README.md`。


### 2026-10-17｜候选2：ViT双写入端gather折叠｜Gold通过，性能负

区别旧读取端gin：Down30写ViT顺序、Block38写raster顺序，Up39保持连续读取。1920×1152 Gold0900逐位同；以候选1(mode3)为共同基线，3轮ABBA mean +0.513/+1.659/+14.902µs，pooled +5.691µs；p99各+14.445/−37.833/+7.254µs，pooled−3.487µs。不收，保两个gather和旧生产代码，不刷轮。详results/vit-write-gather-20261017。

### 2026-10-17｜候选4：C32 pool四列合波与W16｜Gold通过，性能负

Down4每wave算64全列、W16权重、共享字节输入，保Hrtz(0+sum)并合并输出量化。Gold0900逐位同，候选1+3作共同基线，3轮ABBA mean +6.697/+4.832/+5.109µs，pooled+5.546µs；p99各−7.051/−9.417/−12.486µs、pooled−4.099µs。均值负，STOP不收，保旧四wave/行主序核。详results/pool32-wave-w16-20261017。

### 2026-10-17｜候选5：融合Up56/62/66 W16 FP8｜部分性能负，完整Gold失败

56/62实际命中新核、Gold0900同；3轮ABBA mean −0.670/−2.868/+14.202µs，pooled+3.555µs，p99三轮全退/pooled+20.987µs，STOP。独立派发记录发现66仍c32_wave1_up_b8：配方CW_UP_LOW_BYTES0缺原字节low导出，原计划输入假设不符。补通65字节输出→已编译66新export后，full Gold首帧2,768,436/3,732,480 half值异于0900，finite/repeat通过但数值门失败，未跑后续ABBA。原因未隔离，不泛归FP8/WMMA本身。完整停止，不入生产；代码/原失败结果/派发日志见results/upbody-f8-w16-20261017及HIP/experiments/five-bitexact-20261017/candidates.patch。

### 2026-10-23｜刀2 C32激活首级FMA_MIX+ALT｜完整Frame无净收益 STOP

- 原float Horner首级改v_fma_mix_f32，一SGPR打包两个精确half常量，变量/结果仍F32；ALT与MIX两模块.text完全同，仅测MIX。直接packed literal被LLVM MC拒后改SGPR，失败保留。
- c32-wave1-fast及norm900热点代码少64–72B、1–2条scalar，VOPD/VALU/寄存器/LDS/WMMA/wait不变。54帧三档三图样Gold0。
- 刀1后完整Frame三档各3轮ABBA（240弃80/960样本每arm）：900 mean−7.787µs但R2退；1088 mean+10.902µs/p99+31.698µs；1152 mean−0.474µs近零。STOP，不合入，不追加同.text的ALT测试。
- 新条件须是实际不同机器码/常量承载/调度改进，不能改名重做此SGPR-MIX。证据results/gap-phase2-20261023/knife2。

### 2026-10-23｜刀3 C512 projection N-pair｜Gold通过、目标1088无收益 STOP

- 独立N-pair权重键+两新export，将K32配对改N32配对，保每输出K16顺序/skip-first/末量化；旧路已A复用4片，不声称A读取减半。正式COMGR旧89Fn机器码/资源全同，新增两Fn VGPR100→80，LDS1024/spill0不变。
- 54三图样+6普通F32skip+6实际缺导出回退，共66帧Gold同。3档各3轮完整FrameABBA（240弃80/960每arm），同host+同candidate模块仅NR0/1；active3856calls/slot（含1准备帧）核实。
- 900mean+0.909µs近零且不稳定；1088三轮都退，pooledmean+6.152µs/p99+21.655µs；1152mean−11.935µs，但R2p99+36.063µs且部分漂移较大。整体STOP，保留1152正值不抹，不合生产/不扫其它展开度。
- 需新shape/实际动态供数或调度证据才重开；此结果不否定所有N-pair。证据results/gap-phase2-20261023/knife3。

### 2026-10-23｜Phase3 A 当前FAST1精确QT32共享K/V｜明确复验仍负 STOP

- 用户明确要求复验；旧10-01 M32已19 SAME且性能负，不改写为“仅有损”。当前候选保FAST1 score/den/AV顺序，真实半grid，400尾Two=false删除整条第二链；正式COMGR旧89Fn同，新98VGPR/0LDS/private0。
- 源/ISA证明K/V逻辑请求400−48%/640−50%，有用WMMA同，非DRAM计数；60帧Gold含实际缺导出回退同。
- 三档各3轮FrameABBA：900mean+12.039µs/p99+62.208µs，1088+97.277/+89.184µs、1152+90.150/+100.465µs；后两档每轮mean/p99均退。STOP，不合生产/不扫同类展开。
- 后续重开须用户新授权或明确新数学/供数/硬件条件，不能把低VGPR与重复KV当未试。证据results/gap-phase3-20261023/A。

### 2026-10-23｜Phase3 B C512 FFN→Projection 64bit tile-chain｜真重叠但整网负 STOP

- 两wave FFN发布8贡献/tile，consumer bounded acquire后原projection；64bit累计target不逐帧reset。超时完整零写+sticky、最终drain拒整批，非产品回退；16槽hold/FrameFlush后clear成本计候选，NR0不加假hold。
- 实际生产producer是empty-opts；max-ilp会改旧89函数，已隔离未上GPU。fresh emptybaseline整SHA同生产，最终新旧stock91/89Fn同。首次afterwait缺池被保护拒，修候选冷预热有界稳定，保失败不刷性能。
- 84主Gold/诊断帧同，独立timeout-zero canary及CPU复核PASS；强制完整Frame失败status1/exit1正常拒批；carry32/64极限关闭过。
- trace三档16边全部真首WMMA重叠，ordered0；完整Frame各3轮ABBA仍900+86.410µs、1088+124.866µs、1152+52.815µs，九轮mean/p99全退。STOP，不合生产，不扫同步变体。成本未单独分摊，不否定所有TCHAIN。证据results/gap-phase3-20261023/B。

### 2026-10-23｜Phase3 C C512 QKV最后等待前移独立WMMA｜微小不稳定 STOP

- 31处wait0均有真实RAW，未盲改阈值；仅W16最后两K16步用compiler sched barrier把7条独立WMMA移到wait0前。正式COMGR仅目标Fn代码变、其它112同，资源/大小/全opcode计数同。
- 54帧Gold同。原r3期间root准备后续对账文件有目标CPU/I/O干扰，在steady统计前声明排除，保原轮并补单个quiet r3；不按结果挑选。
- 有效三档3轮FrameABBA：900mean+4.076µs/p99+10.880µs；1088mean−4.563µs但轮次不稳、R1p99+40.309µs；1152mean+0.788µs。STOP，不合生产、不扫尾步/深度。
- 证据results/gap-phase3-20261023/C；仅此窗口无稳定收益，不扩称无任何load/wait机会。

### 2026-10-23｜C512 serial_norm permlanex16及与pair打包组合｜STOP

同四段累加顺序、ds_bpermute16→0、permlanex16=16、wait_dscnt45→31，Gold和独立GPU/CPU lane/FP8探针全通过。仅perm总指令2483→2542、VALU1367→1438，1088三ABBA均值+4.869µs，2轮回退；combined三档mean有小收益，但pure1088 pooledp99+184.606µs，多槽尾部不是单个max。保留全部样本，不将组合尾退直接归因perm。最终只采纳pair-only（cvt144→72）；norm/w2未动。证据results/gap-phase5-20261023/knife1，静态wait减少不等整网收益。

### 2026-10-23｜QK-fragment全档推广与C512 LDS TR｜限定/停止

QK配对数学/同步/Gold通过，七档ABBA未全正。仅Orig900/Small900/Small1080按pooledmean/p99门采纳；Orig1088 Frame近零不稳且pure pooled尾+10.364µs，Orig1152尾+18.668µs、Smallfree1088尾+41.4µs，Small720mean+3.704µs，四档保持V-only。所有分轮反例在gap-phase6-20261023/knife1。不是静态b128或61→60VGPR就能推整网大幅提速。

C512 ds_load_tr8：本地LLVM23限定gfx1250，gfx1201汇编拒；正式COMGR该拼法未声明，提示ds_read_tr8后再次编译明确requires gfx950-insts。均CPU预期拒绝，没有GPUlaunch。旧C512_COMPACT_VT已有09-30/10-03负账，本次无新写端机制不复刷；不否定未来不同LDS组织。证据gap-phase6-20261023/knife2/B-result.md。

### 2026-10-24｜Up wide全开成对打包｜W16路径STOP

ByteOutput出口每4值4次单输入换2次pair，正式W16核总指令3612→3446，但全核cvt208→192，不是104；其余转换未删，目标VGPR166不变。63候选Gold+42AE+6半权重回退通过、真实route可核。七档84slot中四个W16档位pooled mean均退：Original900+0.910µs、1088+11.397µs、1152+2.946µs、Small1080+9.677µs。fullA pure1088 mean−5.983µs但p99+35.709µs且三轮p99都退，不能采纳全开。FP16权重Up48三个Small档有改善，另独立half-only编译期!W8候选验证；它恢复W16和其余91核基线码，不把fullA收益冒作新版本收益。证据gap-phase6-20261023/knife2/A-statistics.json、A-pure-statistics.json。

### 2026-10-24｜Up ByteOutput仅FP16权重pair｜STOP

最后!W8候选仅2halfFn变且等fullA，余91Fn全等基线，21freshGold通过。4profile各3ABBA：Small720−5.874µs/p99−64.665，free1088−7.860/p99−7.414，但Small900mean−0.459µs近零、p99+22.444µs（R2/R3尾+35.843/+44.571），未过受影响全范围门，停止，不加分辨率条件、不再变体。Original1088控制不执行改核，其−4.224µs读数仅作为噪声控制，不宣优化收益。生产无刀2变更。证据gap-phase6-20261023/knife2/half-statistics.json。

## 2026-10-28 backfeed 刀1 Small720 C256融合 STOP

Small31禁PDL/SP的新条件重测：87→83 dispatch且Gold 0diff，但三轮ABBA pooled mean +0.405µs、p99 +15.896µs，R3 mean +29.607µs。保留旧门控，不将该档混入下一刀基线。详 results/backfeed-20261028/knife1/tier-statistics.json。900与896×512窗口另有正收益，不能据此泛化720。

## 2026-10-28 backfeed 刀2 STOP

contract/project尾部LDS转置均消除32条b8，但VGPR112→113，不满足资源门，未上GPU。qkv_vfrag原地norm LDS转置资源不增、Gold六档全同，三轮ABBA却无稳定收益：Orig1088 p99+8.805µs、1152 mean+5.726µs/p99+13.560µs、Small720 mean+0.192µs；_qkfrag三档未执行改核不算收益。全部回退，详results/backfeed-20261028/knife2。

## 2026-10-28 backfeed 刀3 STOP

W2_FFN_QT_SMALL_MASK当前已3，补W2_FFN_W16_SMALL=3新增C64/C128 W16导出。正式LLVM23两baseline整SHA同、旧函数同、新函数0spill、七档Gold逐位同。但Small900三轮mean全退（pooled+8.466µs/p99+15.439µs）；Original900+2.263µs/p99+14.066µs；Small1080轻微均值收益但三轮p99全退；Original1088近零且不稳。整刀STOP，生产配方保持。详results/backfeed-20261028/knife3。

## 2026-10-28 ISA Phase1：upwide/dectail/ffnb2 STOP

三候选隔离在b5875bfa基线上，均七档Gold0diff/invalid0、各四档3×4 ABBA（240弃80）。upwide原LowByte已是b64，改显式打包解码不能收稳定收益；dectail缩静态代码3612→2648，Small1080三轮mean全退，其他档不稳；ffnb2 C128242→180VGPR、C256242→216、LDS不增/零spill，四档mean三轮全退（pooled+20.487/+19.600/+39.429/+22.612µs）。全部STOP不合生产，不继续刷轮/换名扫参。物理VGPR/CFGliveness、汇编差异、原统计与证据身份见results/isa-asm-audit-20261028。

## 2026-10-29 ISA Phase2 刀1 C512 V-fragment STOP

WMMA V操作数对调+天然片段LDS，64ds_load_u8消除、VGPR188→187、0spill、七档Gold同。初次仅Original1088稳定−4.978µs，资格隔离后最终真实产品before/after3ABBA反而mean+12.284µs（三轮全退）/p99+17.925µs，STOP不合生产。保所有中间正/负结果，不刷轮。详results/isa-asm-phase2-20261028/knife1-README.md。

## 2026-10-29 ISA Phase2 刀2 ViT正交转置 STOP

contract/project零额外LDS、u8/b8已清零，但分别113/115VGPR，超112硬门；一次短pack生命周期变体仍相同，资源门STOP未上GPU。expand77VGPR/0LDS/0spill、490→407条、32b8→4b64，七档Gold同，但四档3ABBA不稳定：Orig1088 mean+5.527µs/p99+32.674µs（3mean全退），Small900+2.474µs（3mean全退），Small1080 pooled虽正R2反例明显。全部STOP，不进生产。详results/isa-asm-phase2-20261028/knife2-README.md。

## 2026-10-29 ISA Phase2 刀3 pool/split正交转置 STOP

pool经打包后字掩码修正，6个out8核32b8→4b64，C64 VGPR90→89，其余88不变、LDS不增/0spill。split两核改Transposed+成对转换，VGPR100不变、LDS1024→0、u8及F32散写消除，旧_nof32保持代码/资源同。分别七档Gold同、各四档3ABBA完成。pool组合四档无稳定净收益；split Orig1088 mean+8.345µs/p99+12.989µs（3mean全退），Small1080三轮p99全退。均STOP，不叠加、不把opcode下降或某一pooled好值当产品收益。详results/isa-asm-phase2-20261028/README.md。

## 2026-10-29 ISA Phase3 刀1 STOP

C512 W16 Stage2成对打包1A：_bin684→646条、95VGPR不变，非byte119→112，0spill/Gold七档同；四档ABBA无稳定净收益（Orig1088+17.239µs、Small900+7.476µs）。跨阶段/跨h2预取完整版本资源跨档，受限1B只非byte-in启用，Gold同但Orig1088+16.878/Orig900+11.089µs均三mean全退。额外Hrtz位掩码原型资源失败且误送Gold被首帧SHA不一致拒绝，不采纳不算通过。详results/isa-asm-phase3-20261029/knife1-README.md。

## 2026-10-29 ISA Phase3 刀2 STOP

B4保持、按两tile折叠expanded的普通C256各CPU实现仍spill（10/139/6），资源门STOP未上GPU。独立非W16提前Contract P与SP B2不变折叠S均七档Gold同、4档3ABBA。P无稳定全档净收益；S run184→153VGPR/0spill但Orig1088 mean+58.954µs（三轮全退）/p99+89.374µs，STOP，不以权重复用保持或VGPR下降推导性能。详results/isa-asm-phase3-20261029/knife2-README.md。

## 2026-10-29 ISA Phase3 刀3 STOP

serial_norm 显式平方缓存：原编译器已复用，C64 fast 137函数及SP全部代码/资源同；C512平方数仍64，compact W162104→2096条/188VGPR/0spill仅调度搬运改变。七档Gold同，四档3ABBA均值全部退步（Orig1088+10.295、Orig900+30.115、Small1080+13.538、Small900+1.672µs），STOP保生产。详results/isa-asm-phase3-20261029/knife3-README.md。


## 2026-10-29 ACO hotspot Phase4 Step3：C128深预取/最小ISA移动均STOP

新版ACO同机基线差约46%，有效设备实时钟确定Original C128 bi_bo约0.819ms/帧为一级热点。3A保持B4全复用，Contract前读至Expand前，仅目标函数改、242→248VGPR/同硬件分配桶/0spill；七档Gold同，Original单核三轮微正−0.521µs/次，但整网1088 mean+2.481µs/p99+15.682µs，Small尾部不稳，STOP。3B严格CFG/等待/EXEC/异步写检查后只把一条FW b64提前跨两条地址VALU，目标外全部字节不变、七档Gold同；单核不稳，Original900三轮mean全退，+4.544µs/p99+15.587µs，STOP。分别完成两档单核与四档3ABBA，不叠加、不刷轮、不改生产80/SHA。详results/aco-hotspot-phase4-20261029/step3-README.md。


## 2026-10-29 C32 Phase5 刀1 STOP

OutProj/scales跨4qt预读1A：147/148VGPR、4096LDS/0spill/最大驻留同、7Gold同、C32族Orig−30.45µs/Small−13.33µs；全网mean虽改善，Small900 p99+14.965µs，Orig1088 R3尾反例，STOP。双qt批处理1B保持K序共享权重；Prefix/Up超160资源拒绝，一次Prefix生命周期改写仍超，最终mask31热核151–156/0spill/7Gold同；4档mean各三轮全退，STOP。不把负候选混下刀、不加路由救微收益。详results/c32-stage-phase5-20261029/knife1-README.md。


## 2026-10-29 C32 Phase5 刀2 未通过扩围档位

small900, small720, smallwindow, original1088 未满足三轮mean/p99及单核稳定改善，保持旧路由；实际数值与每轮反例见results/c32-stage-phase5-20261029/knife2-README.md、2-statistics.json。只收mask=1，无性能刷轮。


## 2026-10-29 C32 Phase5 刀3 Gold STOP

实际数据流要求融合Block4 finish_dcrop→pool而非Block1 mapped。新核134VGPR/4096LDS/0spill，但首个Original1152 Gold SHA不符，立即STOP，未跑性能、未混入下刀基线；未定位中间张量前不猜WMMA/裁剪/舍入根因。详results/c32-stage-phase5-20261029/knife3-README.md。


## 2026-10-29 C32 Phase5 刀4 STOP

Post寄存器尾部h2成对交换145VGPR/0spill/七档Gold同，静态1510→1488，尾部LDS改为8条ds_bpermute；两档单核与四档3ABBA无稳定全网净收益，STOP保刀2。原两qt177/单qtF32 161VGPR版本资源STOP未上GPU。详results/c32-stage-phase5-20261029/knife4-README.md。


## 2026-10-29 C32 Phase5 刀2 最终集成撤销

资格Small1080三轮mean/p99均赢，但独立最终集成R1 mean−25.651µs/p99+65.243µs，稳定性反例成立；不按资格批美化最终结论，撤销mask1，恢复ba057a5d原生产头文件。原资格、最终反例均保留，之后原基线收口不是刷轮救候选。详results/c32-stage-phase5-20261029/2-final-revocation.json。


## 2026-10-30～31 K-Search Phase 2～4 未采纳分支与作用域排除账单

1. **分支 K3（`c32_wave1_post_b8_w16` 跨 `qt` 外提 `scales[64]`）｜性能负 / STOP**
   - 改动与基线：基于 `8a9c60fd`，在 `hip/wave_owned_c32.inc` 中将 `Post && CW_VEC_INPUT` 下的 `scales` 读取外提到首个 `qt=0..3` 循环前（`148 -> 149 VGPR`，`0 spill`，静态指令 `1455 -> 1454`）。
   - 正确性与时量：七档 42 帧 Gold `0 bit-diff`（`invalid=0`）；硬件实时钟 `original1088` `c32_wave1_post_b8_w16` `+0.960 µs`（`small1080` `-2.820 µs`）；四档 3 轮 Frame ABBA：`original1088` `+3.580 µs`、`original960` `+15.555 µs (p99 +253.601 µs)`、`small1080` `-2.000 µs`、`small900` `+0.012 µs`。
   - 裁决与根因：`scales[64]` 仅 256B（2 条 cache line），`qt=1..3` 原本 100% 命中 L0 cache；外提拆散了 `qt` 循环内的 `s_clause` 合并加载并跨整个 FFN+QKV 循环额外保活 16 个 VGPR，判定 STOP。证据：`Development/results/ksearch-k2-k3-20261030/README.md`。

2. **分支 K4（`sp_run256_w16` 开启 `W2_QKV_FUSE` 与 `W2_HOIST_LOADS=3`）｜性能负 / STOP**
   - 改动与基线：基于 `8a9c60fd`，在 `swin-persistent-fast.hsaco`（COMGR）中测试 `W2_QKV_FUSE=12/4`（`kt` 展开，触发 `256 VGPR + 11~17 spill`）、`k4`（`W2_QKV_FUSE=28, W2_HOIST_LOADS=3`，禁用内层 `kt` 展开以保 `253 VGPR, 0 spill`，指令 `5246 -> 3923`）以及 `k4c`（`W2_QKV_FUSE=0, W2_HOIST_LOADS=3`，`184 VGPR, 0 spill`，指令 `5246 -> 5150`）。
   - 正确性与时量：`k4` 与 `k4c` 七档 42 帧 Gold 均 `0 bit-diff`；3 轮 ABBA 硬件实时钟探针（`original1088`）显示 `k4` 的 `sp_run256_w16`（2 次调用）从 `829.880 µs` 退化至 `1086.293 µs`（`+256.413 µs`，0/3 轮，整网 `+214.120 µs`），`k4c` 的 `sp_run256_w16` `-0.033 µs`（1/3 轮，整网 `+7.563 µs`）。
   - 裁决与根因：禁用 `kt` 展开虽消除 spill，但打断了跨 `kt` 迭代的 `s_clause` 全局加载预取深度且 253 VGPR 挤压了延迟隐藏空间；单开 `W2_HOIST_LOADS=3` 完全中性，判定 STOP。证据：`Development/results/ksearch-step3-20261030/README.md`。

3. **作用域排除 1：`small1080`（`1920×1152` Small）开启 `SubmitPulse`（`k6b` vs `k6c3`）｜性能负 / 排除出 `PulseRejectMask`**
   - 改动与实测：在 `k6b` 中对 `Small` 全档开启 `SubmitPulse`，`small900` 3/3 轮净快 `-72.361 µs (-1.76%, p99 -117.421 µs)`，但 `small1080` 3 轮 mean `+9.485 µs (+0.18%, 0/3 轮)`。
   - 裁决与根因：`small1080` 全网仅 `5.36 ms` 且不走 `sp_run256` 持久核，中途 `hipEventRecord` 拆包在小步幅 1080p Small 下产生轻微拆包开销；因此生产 `PulseRejectMask` 精确放行 `small900`、`small720`、`smallwindow`，保持 `small1080` 不拆包。证据：`Development/results/ksearch-step4-20261031/README.md`。

4. **作用域排除 2：`1920×1152`（`original1152` / `small1080`）开启 `w16_fb8` 及 `w16_fb8` 提前 `ffn8.reset()` 同址复用｜性能负 & 正确性边界**
   - 改动与实测：在未开启 `SubmitPulse` 时，`original1088` 开启 `w16_fb8` 因整帧单批提交与 L2 驱逐相位变化慢 `+229 ~ +243 µs`（开启 `SubmitPulse` 后逆转为净快 `-25 ~ -47 µs`）；而在 `small1080`（`1920×1152`，`SubmitPulse=0`）上开启 `w16_fb8` 仍慢 `+24.97 ~ +54.01 µs`，`original1152` 上开启 `w16_fb8` 无额外净收益。此外，在 `w16_fb8` 路径下若提前调用 `ffn8.reset()` 使 `result` 与 `ffn8` 同址复用，由于 `ffn8`（`[16×32]` 瓦片）与 `result`（`[token×512]` 行主序）跨列分块工作组地址交错，在 `pure.exe` 241 帧连续纯 NN 压测中会偶发跨工作组覆写竞态。
   - 裁决：`C512W16Fb8ShapeEligible()` 精确限定为 `small_active?(W==1600&&H==1024):((W==1600&&H==960)||(W==1920&&H==1088))`（并加上 `fast_numeric && multi_pass==1 && !experimental_temporal && !observer && dump_dir.empty() && AE=0` 完整安全门禁），排除 `1920×1152`；且保持 `if(!fb8&&!w16_fb8)ffn8.reset();` 使 `w16_fb8` 下 `ffn8` 与 `result` 严格异址。证据：`Development/results/ksearch-step3-20261030/README.md`、`Development/results/ksearch-step4-20261031/README.md`、`Development/results/ksearch-round2-20261031/README.md`。

5. **分支 R2-A（`vit_stream_qkv_frag_bin_w5f8_*` 局部改写 `r2a1` / `r2a2`）｜性能负 / STOP**
   - 改动与基线：基于 `e7b234e7`，在 `hip/vit_stream.inc` 中测试 `r2a1`（Q/K 平方和归约循环显式 `unroll 8`）与 `r2a2`（W5 权重加载指针预计算外提），VGPR 保持 `112`、`0 spill`。
   - 正确性与时量：七档 42 帧 Gold `0 bit-diff`；3 轮 ABBA 100MHz 硬件实时钟探针（`original1088 / small1080`）显示 `vit_qkv` 在 `r2a1` 下为 `+4.453 µs (0/3) / +0.700 µs (1/3)`，在 `r2a2` 下为 `+12.213 µs (0/3) / +3.433 µs (0/3)`。
   - 裁决与根因：局部循环展开与指针预计算破坏了 COMGR 原本围绕 6 次 LDS 屏障的指令调度窗口，未改善 LDS 转发等待，判定 STOP。证据：`Development/results/ksearch-round2-20261031/README.md`。

6. **分支 R2-B（`c128_wave2_bi_bo/ho` 压入 `<= 240 VGPR` 跨寄存器分配桶，`r2b`）与 R3-C（零循环开销压入 `238 VGPR`，`r3c`）｜性能中性偏负 / STOP（修正 RDNA4 硬件拓扑后闭环）**
   - 改动与基线：在 `hip/wave_owned_mh.inc` 的 `c128_wave2` 中，Round 2 的 `r2b`（基于 `e7b234e7`）通过在 `ht` 循环头重算 `ffn_tid` 并对 `upw` 指针做 `__builtin_amdgcn_readfirstlane` 标量化将 `c128_wave2_bi_bo/ho` 从 `242 VGPR` 压至 `240 VGPR`（`0 spill`）；Round 3 的 `r3c`（基于 `b1af6240`）不在 `ht` 循环内添加任何重算指令，仅在循环外对 `plane0/plane1` 基址与 `upw` 指针做标量化，将 `c128_wave2_bi_bo/ho` 从 `242 VGPR` 压至 `238 VGPR`（静态指令 `3275 -> 3274`，`0 spill`）。
   - 正确性与时量：`r2b` 与 `r3c` 七档 42 帧 Gold 均 `0 bit-diff`；3 轮 ABBA 100MHz 硬件实时钟探针显示：`r2b` 的 `c128_wave2` 在 `original1088 / small1080` 上为 `+0.637 µs (0/3) / +0.987 µs (0/3)`；`r3c` 的 `c128_wave2` 在 `original1088` 上为 `-3.047 µs`（`910.323 -> 907.277 µs`，3/3 轮改善），在 `small1080` 上为 `+1.780 µs`（`263.330 -> 265.110 µs`，0/3 轮），全网实时钟总和 `-0.873 µs / +3.740 µs` 近中性。
   - 裁决与根因（修正 Round 2 错误拓扑证明）：撤回 Round 2 中将 RDNA4 误作 8 SIMD/WGP 得出“16KiB LDS 锁死 4 waves/SIMD”的错误解释——按 AMD RDNA4 ISA §2.3（`4 SIMD32/WGP`，`128 KiB LDS/WGP`），`c128_wave2`（4 waves/WG，16 KiB LDS）的 LDS 上限实为 `8 WGs/WGP × 4 / 4 = 8 waves/SIMD`，故 VGPR 从 `242`（5 waves/SIMD）压入 `238~240`（6 waves/SIMD）确实将理论有效驻留从 `min(5,8)=5` 提升至 `min(6,8)=6 waves/SIMD`。但 `r3c` 在零额外循环指令下的实测证明，`c128_wave2` 在 5 waves/SIMD（20 waves/WGP）下已使 L0/L2 与 WMMA 管线饱和，升至 6 waves/SIMD 在 `small1080` 较浅网格下反而轻微增加 L1 缓存争用，未达跨档晋升门槛，判定 STOP。证据：`Development/results/ksearch-round2-20261031/README.md`、`Development/results/ksearch-round3-20261031/README.md`。

7. **分支 R3-B（`vit_stream_qkv_frag_bin_w5f8_*` 取消 W5 5-wave 16KB LDS 权重共享，改单 wave W16 直接读取，`r3b`）｜性能暴退 / STOP**
   - 改动与基线：基于 `b1af6240`，顺着 ViT QKV `P0 = +33.09 µs` 探针线索，将 `vit_stream_qkv_frag_bin_w5f8_*` 改为 `launch_bounds(32)` 单 wave 直接从全局内存读取 W16 权重（彻底消除 16KB LDS 与 6 次跨 wave barrier，`108 VGPR, 0 spill`）。
   - 正确性与时量：七档 42 帧 Gold `0 bit-diff`；3 轮 ABBA 100MHz 硬件实时钟探针（`original1088 / small1080`）显示 `vit_qkv` 从 `209.097 / 212.183 µs` 暴退至 `574.673 / 549.830 µs`（**`+365.577 µs / +337.647 µs`**，0/3 轮，整网退步 `+370.957 / +323.750 µs`）。
   - 裁决与根因：原 W5 方案虽然付出 16KB LDS 与 6 次 barrier 同步代价，但让 5 个 token 块共享同一次全局权重加载；改为单 wave 直接读取后，全局 QKV 权重读取流量放大 5 倍，严重打爆 L2/VRAM 带宽，判定 STOP（ViT QKV 局部尝试 `3/3` 满额剪枝）。证据：`Development/results/ksearch-round3-20261031/README.md`。

8. **Round 4 成分消融：`R3-A + K5` 新基线下单独禁用 R2-D 的子集变体（`A0_NoR2D`、`A1_R2D_C32`、`A2_R2D_MH` vs `A3_R2D_Both`）｜不如完整 `R2D_Both + R4-A` 组合**
   - 改动与基线：回应 Round 3 审单 P2-5，在 `1005424b`（已固定开启 `R3-A` `CW_FMED3_CLAMP=1` + `K5`）上，同批交错测试 `A0_NoR2D`、`A1_R2D_C32`、`A2_R2D_MH`、`A3_R2D_Both` 四臂 3 轮 8-slot 镜像帧实验（`small1080` 与 `original1088`）。
   - 正确性与时量：全部 `0 bit-diff`；在 `small1080` 上，当 `R3-A` 消除了 C32 Softmax 31 对 `cmp_gt_e64 + cndmask_e64` 与 14~25 个 SGPR 阻塞后，相对 `A0_NoR2D`（`5.430046 ms` / p99 `5.767799 ms`），`A1_R2D_C32` 快 `-17.57 µs`、`A2_R2D_MH` 快 `-4.89 µs`，而完整 `A3_R2D_Both` 最快（`5.407441 ms`，净快 `-22.61 µs`，p99 `-34.52 µs`）；在 `original1088` 上四臂均落在 `8.596 ~ 8.625 ms` 的窄带内，且叠加 `R4-A` 后全网七档进一步实现 `original1088` `-464.79 µs`、`small1080` `-136.19 µs` 21/21 轮全胜。
   - 裁决：不拆分按 shape 关闭 R2-D 子集，全网统一保留 `R2D_Both + R3-A + R4-A` 完整生产组合。证据：`Development/results/ksearch-round4-20261031/r3a_r2d_4arm-statistics.json`、`Development/results/ksearch-round4-20261031/README.md`。
