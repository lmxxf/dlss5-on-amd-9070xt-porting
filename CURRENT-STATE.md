# 2026-09-07 收工现场：正确画面慢速展示

最新C32收尾按通道合并访存：COALESCED_FINISH显式开启，保留旧入口默认路径、H/F和池化求和顺序。15帧最终exact，暖661.67148ms、末5帧662.32926ms；preblock_detail_stage2由9.30013到1.14322ms，首个c32_probe_stage2由2.69509到0.24597ms。其他阶段存在波动，不把全部整网差额归因此改。无新增GPU缓冲，2D dispatch覆盖回归通过。证据release/native-network70-coalesced-finish；run_coalesced_finish_network.ps1继承上一有效配置。游戏DLL未改，10fps未达到。

最新raw下采样池化＋Wave投影15帧exact，暖688.03063ms、末5帧685.43865ms。WAVE_HEAD先把ViT入口head16.74132→0.04410ms（含池化）；WAVE_DOWNSAMPLE再覆盖C64/128/256 raw路径，非raw的ds4保持旧实现。矩阵无损half本地驻留、池化借共享packed区；valid矩形补零不变。证据release/native-network70-wave-head与-wave-downsample。参数化初版误改FP8常数已修正并加静态回归，失败日志保留；游戏未改，10fps未达到。

512融合Wave FFWD候选15帧exact但暖708.75353ms，不优于706.43771ms，保持WAVE_SPLIT_FFWD=0。新增首Split分段计时确认FFWD3.48644ms、QKV0.23348/attention0.45686ms；ViT入口head单独16.74132ms，下一步优先下采样投影。证据release/native-network70-wave-split-ffwd与-split-profile。encoder23_head现在只剩bridge间隔，需加encoder_head/encoder23_30_body和首层split*，不能误报标签变小为提速。

首層拆分已修复并通过15帧exact，暖706.43771ms、末5帧706.45990ms。小型双路探针定位混合prefix已错误（全为微小正数），将signed-int32×2^exponent直接RNE到half的等价整数公式代替拆分路径double转换后恢复；202905 CPU随机/边界案例bit0，GPU小探针前馈/最终两轮bit0，整网15帧bit0。确切编译器内部机制未定位，不宣称已证明驱动bug。证据release/native-network70-split-preblock[-fixed]；SPLIT_PREBLOCK_FFN显式开启、无新全图缓冲，游戏未改，10fps未达到。

最新两个实验未采纳：直接F16矩阵累加0.697196ms但438687/8847360值不同（max2），收益小，保留exact方案。首层prefix→Wave FFN拆分（SPLIT_PREBLOCK_FFN）首帧6635518/6635520最终值不同，输出范围0.1..0.9、接近原输入，MAE0.03134，不能当硬件小误差。默认关闭，数据release/native-network70-split-preblock/；下一步单独核对prefix与FFN接点，当前有效基线仍约746ms，游戏安装未改。

最新C32完整Wave注意力（QKV/QK/AV/projection）15帧exact，暖745.959355ms、末5帧744.382106ms。AV模式先将exp/prob转置存入已闲置Q/K half区（不改softmax求和），V存FP8格点half，后续两K32 H；投影仍保留最后half_add_preserving_midpoint。AV单独约752.19737ms，额外投影收益小。初版F32 B矩阵Cast到F16在初始化存取违例，已移除；直接half V方案恢复，故障证据保留。见release/native-network70-wave-c32-av和-c32-full-attn。游戏未改，10fps未达到。

最新普通C32 Wave FFN＋初始化无损打包本地权重：15帧exact，暖均762.82839ms、末5帧756.13036ms。首个C32 FFN9.30389→1.90317ms；每组重复转权重版约867ms无收益。WAVE_C32_FFN=1及WAVE_C32_FFN_LOCAL=1启用，仅raw_features，RGB/noise前缀未改。证据release/native-network70-c32-ffn-local/。新增C32首层细分计时，encoder1_4标签现不含该首层全部区间，比较须加c32_probe*，不能误报标签缩短为加速。游戏未改，10fps未达到。

最新512分裂层矩阵QKV＋Wave评分15帧exact，暖均862.25461ms、末5帧855.93912ms；encoder23_head=58.36901ms（旧109.29753）。MATRIX_SPLIT_ATTENTION显式启用并要求图级workspace，QKV无损half权重GPU本地、临时量共用，FFWD/投影不改。local14.711GB低于15.397GB预算。证据release/native-network70-matrix-split/。下一大项preblock/C32前馈；安装游戏未改，10fps未达到。

最新ViT Wave收缩/投影15帧exact，暖均965.47653ms、末5帧964.39536ms。WAVE_VIT_REDUCE显式开启，K4096/1024两CSO，保持四分区累计、初始残差H、每K32 H，矩阵无损half驻留GPU、skip系数原FP32位元。local14.686GB低于15.387GB预算。证据release/native-network70-wave-vit-reduce/。下一大项encoder23_head约109ms/preblock约103ms；游戏未改，10fps未达到。

最新ViT Wave展开＋本地half权重15帧exact，暖均1042.86360ms、末5帧1037.73500ms。保留65536输出/逐块提交，权重原值无损half，组内K32软件H。WAVE_VIT_EXPAND与RESIDENT_WAVE_VIT_EXPAND显式启用；先仅Wave为1133.27541ms，再本地权重降到1042.86360ms。local14.598GB在15.387GB预算内，证据release/native-network70-wave-vit-local/。下一步ViT收缩/投影，游戏未改，10fps未达到。

最新C32 Wave评分及QKV两方案均15帧exact：仅scores暖1183.24196ms，scores+QKV暖1170.98909ms、末5帧1171.23997ms。共享32KiB阶段复用不增全尺寸scratch，QKV权重转half前检查无损。WAVE_C32_QKV依赖WAVE_C32_SCORES，专用CSO；证据release/native-network70-wave-c32[-qkv]/。相对前版1.187秒改善有限，下一步转ViT/512与preblock大项；游戏未改，10fps未达到。

最新C64/C128/C256全Wave路径15帧exact，暖均1187.29283ms、末5帧1188.06103ms。WAVE_C64显式依赖MATRIX_C64，共享区使用C64容量；encoder5_8=37.62461、tail63_65=27.28023ms。local14.531GB低于15.396GB预算。证据release/native-network70-wave-c64/profile-validation.json。此前C64 Thread矩阵回退不适用于本Wave结果；游戏仍未更新，10fps未达到。

最新C128/C256 Wave整网15帧exact，暖均1223.90318ms、末5帧1220.70869ms。C128 encoder9_14=30.22797ms、tail57_61=24.68307ms；local14.469GB在15.381GB预算内。WAVE_C128显式开关、_c128 Wave CSO分开命名，C64仍旧分块。证据release/native-network70-wave-c128/profile-validation.json；游戏未更新，10fps未达到。

最新C256 Wave展开/收缩/QK评分已接整网15帧exact：DLSS5_TEST_WAVE_C256=1需MATRIX_C256，C128矩阵/共享workspace保持，C64旧分块。暖均1311.13692ms、末5帧1304.05339ms，tail49_55=24.51406ms。local14.469GB低于15.381GB预算。证据release/native-network70-wave-c256/profile-validation.json。游戏安装尚未更新，10fps未达到。

最新Wave Q×K评分接C256核心：归一化后的FP8格点Q/K用half共享，wave合作16×32乘32×16写scores，后续softmax近似/归约/V加权原样。五轮最终字节一致，attention2.71318→0.96467ms，完整核心5.17005→3.42863ms。证据release/matrix-c256-wave-scores/result.log。仅显式wave_scores/C256，默认关闭，未接整网/游戏。

最新Wave收缩接C256核心：每组16像素×16输出，K1024按K32加载组内half小块，保留每步H与最终F；无新增全尺寸输入打包。对照上一版Wave展开/旧收缩，五轮最终字节一致，contract1.70686→0.65209ms，完整核心5.89964→4.88883ms。证据release/matrix-c256-wave-contract/result.log。use_wave_contract显式开启、依赖Wave展开，默认关闭；未接整网/游戏，下一大项attention约2.44ms。

最新Wave展开已接C256 GPU核心链：同轮baseline为Thread矩阵展开＋矩阵QKV，候选只换Wave展开；五轮最终2211840值原版/两路字节一致。展开1.24613→0.34431ms，打包约0.194ms均计入，完整核心6.97843→6.11890ms。证据release/matrix-c256-wave/result.log。use_wave显式参数、仅packed C256，默认关闭；下一步Wave收缩（当前约1.7ms），尚未整网/游戏启用。

2026-09-08新方向：Wave scope矩阵×矩阵展开探针16×32乘32×16，保留每K32软件H，完整8847360值对exact激活参考零差异，0.732240ms。此前Thread scope完整K256累加1.713773ms且172676值不同，无实质收益不采用。证据release/matrix-real-probe/timing-wide-wave.txt。下一步把Wave算子接GPU链，尚不含运行时packing、不宣称整网速度。

C64矩阵实验15帧exact但变慢：暖均1441.31863ms、末5帧1437.48887ms；相比C128/C256共享基线，encoder5_8+42.09、tail63_65+31.96、body62+10.18ms，回退主要就在C64。local14.531GB低于15.397GB预算，不是上轮超预算情形。DLSS5_TEST_MATRIX_C64默认关闭，不采纳到游戏；仅开启时扩workspace至488×296×64，否则维持C128大小。证据release/native-network70-matrix-c64/。

图级共享矩阵工作区已通过15帧exact：DLSS5_TEST_SHARED_MATRIX_WORKSPACE=1，按248×152×128元素容量，共用packed/qkv临时量，编码器/解码器显式传入，跨层输出/skip不共享。本地usage15721615360→14468567040，节省1253048320字节，末预算15373082624；同C128+C256配置暖轮1527.07107→1354.10840ms、末5帧1344.92482ms。证据release/native-network70-shared-workspace/。C64矩阵尚未开放，扩大范围前需扩大workspace容量；游戏安装未更新。

显存诊断已确认：C128+C256矩阵整网15帧全部exact；初始化后local用量15049674752，运行后15721615360字节，最后预算15395364848，最高超预算326250512字节；nonlocal819978240。证据release/native-network70-memory-c128/。这说明预算压力，不等同已测到具体换页耗时。下一步优先共用顺序层的matrix_input/qkv_raw工作缓冲，保持独立图/设备隔离和barrier状态，不继续单纯增加每层常驻scratch。

C128矩阵路径整网五帧exact，但本轮暖均1515.8034ms，未优于C256-only的1347.77308ms。主要额外耗时发生在未改ViT attention（各+14～22ms），帧总时间1561→1553→1533→1502→1476ms仍下降，因此需查显存预算/暖机稳定性，不能直接判定C128算法慢了168ms。DLSS5_TEST_MATRIX_C128默认关闭，不部署；证据release/native-network70-matrix-c128/profile-validation.json。

最新硬件矩阵C256整网五帧已通过：DLSS5_TEST_MATRIX_C256=1经NativeC64Shift启用打包展开/QKV，含各层移位、raw输出、history off/on/reset，GPU实际最终结果与原版逐字节一致。暖轮1347.77308ms，encoder15_22=57.23774ms。证据release/native-network70-matrix-c256/profile-validation.json。此运行使用Agility721/experimental与预览驱动；旧1.470秒环境不同，不能将全部差额归给算子。游戏安装未改，10fps未达到，后续扩通道需沿该运行环境对照。

最新C256矩阵QKV核心：显式use_matrix_qkv依赖packed matrix expand，FFN结果在GPU再打包一次、LinAlg计算原Q/K/V后送回原normalize/attention/projection。block52五轮最终原版/旧核逐字节一致，完整核心13.80241→7.33708ms；QKV pack0.01224、matrix0.34057、剩余attention2.51536ms，对照旧attention8.86283ms。证据release/matrix-c256-qkv/result.log。尚未整网/游戏推广，下一步完整C256各层回归。

最新矩阵C256 GPU打包路径成功：每帧FP32→half打包0.19383ms＋矩阵展开1.10214ms=1.29597ms，同轮旧分块展开2.32118ms；完整核心13.88444→12.74870ms，五轮最终原版/旧核逐字节一致。无CPU逐帧打包。use_matrix/pack_input均显式开启才生效；packed CSO独立名native_matrix_expand_packed.cso，另需native_matrix_pack.cso。证据release/matrix-c256-packed/。还未接整网/游戏，下一重点是耗时更大的QKV/attention投影，不能将局部收益当10fps。

矩阵展开已接NativeC64真实GPU特征→收缩→attention→projection，block52五轮最终2211840值与原版oracle和旧分块baseline一致。仅显式use_matrix参数启用（C256/split限定），游戏未启用。性能反而差：展开6.15166ms vs已优化baseline2.14514ms；预打包half探针不能代表直接FP32输入接口成本。证据release/matrix-c256-integrated/result.log。下一步测GPU一次打包供各输出块重用，不能推广此慢版。

最新矩阵展开含gate/F：完整8847360输出矩阵/标量逐字节一致，CPU gate参考也different0。矩阵1.748128ms、标量探针2.384248ms；原生half转换变体2.156008ms且910515值不同、max2、MAE0.0020885349，不采用。仍未包含运行时输入packing及完整网络，下一步接GPU算子接口。证据release/matrix-real-probe/timing-activated.txt。

最新硬件矩阵完整尺寸线性展开：block52全部8640×1024输出，K256每K32 H；矩阵0.801680ms、标量探针2.170559ms（各10次、无独立暖机），8847360实际float结果finite且cmp逐字节一致。证据release/matrix-real-probe/timing-full.txt。尚不含运行时输入packing、gate/收缩、整网，不是对最优旧FFN的端到端速度比较。下一步接真实算子接口与激活。

硬件矩阵初步独立计时：同row0真实样本，MATRIX_PATH=1/2分别只执行矩阵/标量，十次dispatch均值0.056760/0.100652ms，实际32768-byte输出逐字节一致（b68a03ca…25d4）。没有独立暖机，不是对已优化分块整层的比较，更不能外推网络FPS。证据release/matrix-real-probe/timing-row0.txt。下一步完整像素/全通道独立算子与最佳旧核对照。

最新硬件矩阵真数据探针：block52 C256展开的全部1024行×256个选定像素、完整K256，8次K32 dot后各H，矩阵接口对GPU标量参考262144结果零差异。原输入/权重转F16无损；还未覆盖全部8640像素、gate/收缩或整网，未计时。证据release/matrix-real-probe/result.log及各row*.json。下一步真实独立算子性能测试，不必预先放弃exact。

## 22:21用户已批准新路线；22:24驱动安装与首个数值探针成功

用户明确允许预览驱动、保留exact裁判链、快速版仅允许硬件算术差异（权重/结构/输入依赖不动），先测矩阵是否能exact。此前“等待批准/blocked”记录仅为历史，不再是当前障碍。

20260907-approved安装退出0、厂商日志完成，AMD当前32.0.31007.2048，Intel不变，启动时间仍8/31，无重启。安装记录独立于9/5，回退127文件保留；一次性SYSTEM任务已确认Ready/result0后移除。开启进程实验特性后SM6.10、LinAlg tier0x10；不加--experimental的能力探针仍会显示tier0，不能误判驱动失败。

旧smoke8192值一致；新matrix_rounding_probe8192值亦一致（32项dot、带符号E4M3可表示输入/权重、F32矩阵输出加残差后H）。这只证明该探针可对齐，不证明全部累加情形或真实层。下一步真权重/真实输入验证，再接快速路径。精确链及游戏DLL未改。证据release/driver-install-20260907-approved/。

## 自动优化停在用户决策点

10fps未完成，有效整网基线约1.470秒。矩阵接口路线需要重新安装预览驱动，已多轮等待真人确认；期间两个普通shader替代实验均无收益。最后复查驱动仍32.0.31041.1004、无游戏/测试进程、工作区干净。暂将goal标记blocked，避免自动重复消耗额度；不表示理论上普通shader再无优化空间。待用户批准换驱动，或指定继续当前驱动路线后恢复。未自动安装或重启。

本页优先于 README 中旧里程碑。不宣称完整移植目标已经完成。

## 20:14之后：用户恢复性能工作

C32注意力位元FP8量化候选DLSS5_TEST_FAST_C32_FP8=1五帧exact，但1488.7480575ms慢于1469.7617425ms基线，保持关闭。证据release/native-network70-fast-c32/profile-validation.json。未换驱动、未改游戏安装版。

等待换驱动确认期间继续普通shader实验：DLSS5_TEST_RESIDENT_VIT_LINEAR=1将非decoder线性矩阵一次上传DEFAULT。五帧exact但暖轮1563.7835275ms，慢于1469.7617425ms基线，默认关闭、不推广。证据release/native-network70-resident-vit/profile-validation.json。未安装驱动，游戏安装版不变。

重要环境变化：重新检查Windows，AMD驱动当前32.0.31041.1004（不是9/5曾装的32.0.31007.2048预览驱动）。私有Agility721探针当前SM最高6.9、LinAlg tier0；旧matrix_smoke与新matrix_rounding_probe均PSO E_INVALIDARG。尚未改驱动，不能假设矩阵核心接口仍可用。新32项舍入探针只完成编译，未执行数值验证；需用户确认是否再次切预览驱动，普通SM5主线仍可继续。

最新逐提交计时（DLSS5_TEST_SUBMISSION_TIMING=1）五帧exact：每帧513次提交，暖轮GPU区间合计1390.92069ms，CPU录制至完成等待合计1469.05275ms，差78.13206ms（非纯CPU时间，含排队/同步等）。计算本身占主要成本，不能靠合并提交达到100ms。证据release/native-network70-submit-profile/；默认关闭诊断，未改提交策略，安装版不变。

ViT expand塊内共享实验DLSS5_TEST_TILED_VIT_EXPAND=1保留原65536输出提交边界，五轮exact但全网1482.349745ms，未优于1469.7617425ms基线；默认关闭、不部署。证据release/native-network70-tiled-expand/profile-validation.json。下一步需区分ViT单dispatch GPU计算与跨提交等待，不能继续把stage总计时当纯算子时间。

ViT同阶段合并chunk提交实验失败：session43310/PID41232 exit1，HRESULT2289696774=0x887a0006 DEVICE_HUNG，无完整帧。实验代码已撤回，仍用逐chunk等待，不能重试整网/整阶段大列表。独立AMD原codec三scale执行成功，GPU已可用。失败证据release/native-network70-batch-vit/；有效性能基线仍1.470秒，下一步只改算子（如ViT expand分块），保留提交边界。

最新C64/128/256/512注意力共享行padding通过完整五帧字节对照，1512.6764325→1469.7617425ms。显式DLSS5_TEST_PAD_MULTIHEAD_LDS=1启用，证据release/native-network70-pad-multi/profile-validation.json，安装DLL未改。

最新C32共享存储行跨度32→33实验五轮exact，暖轮1590.0457→1512.6764325ms。DLSS5_TEST_PAD_C32_LDS=1显式启用，证据release/native-network70-pad-c32/profile-validation.json；游戏安装版未改。

解码尾段细分已完成，release/native-network70-tail-profile/profile-validation.json五帧exact。tail49_55约104.12ms、tail67_69约76.54ms、tail57_61约63.01ms、tail63_65约42.60ms；三段上采样projection合计约3.55ms，不应优先改它。新增profile将原decoder_stage12拆成十段，该旧标签现在仅表示最后时间戳间隔，比较尾段须求tail*合计，不能拿近零旧标签宣称加速。

最新C32注意力输入量化复用整网五轮exact：1678.7057975→1590.0457ms，证据release/native-network70-cache-c32/profile-validation.json。DLSS5_TEST_CACHE_C32_INPUT=1开启，未启用收益不明的RESIDENT_C32_WEIGHTS；默认关闭，游戏安装版未改。

额外C32系数驻留实验DLSS5_TEST_RESIDENT_C32_WEIGHTS=1五轮exact，但全网1678.7057975→1665.2109175ms（不足1%），未确认超出波动，保持默认关闭、不作为确定加速推广。证据release/native-network70-resident-c32/profile-validation.json，后续性能基线仍以resident-noise约1.679秒为准。

最新192MiB noise表改为初始化一次上传DEFAULT/GPU本地只读缓冲，DLSS5_TEST_RESIDENT_NOISE=1显式启用，五轮最终字节一致。暖轮1720.227495→1678.7057975ms，preblock193.22972→144.14321ms。证据release/native-network70-resident-noise/profile-validation.json；无逐帧复制，安装版仍未更新。

最新普通C32共享FFN整网五帧通过，暖轮1856.0057175→1720.227495ms；显式DLSS5_TEST_SHARED_C32=1启用，只对raw_features路径生效，RGB噪声输入算法未改。证据release/native-network70-shared-c32/profile-validation.json。游戏安装版仍未更新，当前所有完整测试均已退出。

最新QKV分块已通过完整五帧原版字节对照，暖轮2173.7783275→1856.0057175ms，证据release/native-network70-tiled-qkv/profile-validation.json。DLSS5_TEST_TILED_QKV=1显式启用；默认关闭，游戏安装版未改。注意ViT stage2是QKV，stage3才是attention，先前关于stage2为attention的方向已纠正。

最新完整整网共享FFWD已通过（session2674/PID1808 exit0）：五轮最终原版逐字节一致，暖轮2439.2734975→2173.7783275ms；encoder23_head降至106.53807ms。证据release/native-network70-shared-ffwd/profile-validation.json。游戏DLL未更新、游戏仍退出。接下来可检查ViT attention单线程ex[640]/quantized[640]暂存开销，尚未实施该优化。

后续目标更新为实际神经画面超过10fps。新增默认关闭DLSS5_TEST_SPLIT_FFWD=1共享前馈候选，小夹具五轮四阶段/输入切换通过，前馈暖均34.16767→0.65429ms，整核心36.07349→4.27147ms；注意力段1.47757→3.12796ms有回退。仅16×8样本，完整尺寸/整网尚待验证，不能外推游戏FPS。日志release/split-shared-ffwd/result.log与release/split-projection-tiled/profile.log。

用户要求暂以现有正确性为基线，继续优化。首个候选将NativeSplit的两段C512投影切成共享分块，默认关闭，仅DLSS5_TEST_SPLIT_PROJECTION=1启用。现成四阶段输入切换五轮通过，完整off/on/reset五轮最终字节也一致；暖轮2.710→2.439秒，约10%收益，证据release/native-network70-split-projection/profile-validation.json。代码已保留，尚未推广到游戏DLL。

为避免GPU计时争用，21632已正常Alt+F4退出，完整测试PID21952已exit0；当前游戏不在运行，已安装DLL与开关文件不变。下文“最后检查PID21632”描述封存时历史状态，不是现时进程。继续优化时以此段及worklog最新条目为准。

## 当前能做到什么

- AMD完整0～70层、1080p输入、1152处理高度、原版shift3，五轮history off/on/reset最终输出与独立原版逐字节一致。证据：`release/native-network70-tiled/profile-validation.json`。全网暖轮约2.71秒，不是实时。
- 旧真实游戏单帧PID34096/request1已独立原版重放验证一致，证据：`release/native-live-neural-34096/`。
- 当前连续展示DLL每帧重设history=0，游戏内连续处理/回写已运行；用户20:09确认“这个是对的，虽然只有2fps，主人公有一点黑”。这是用户观感确认，不替代所有新帧的数值核验。
- 43156正面像及21632展示首帧已读回，finite、alpha保持、RGB有变化；这些新帧独立原版整网对照尚未完成。连续历史反馈、时序画质、最终开关对照仍待完成。不能用每帧reset冒充完整temporal。

## Windows现场（amd9070）

最后检查PID21632仍Responding，持续render_complete；不要假定下次PID仍相同。

- 游戏：`C:\Program Files (x86)\Steam\steamapps\common\StellarBlade\SB\Binaries\Win64`。
- 自动载入DLL：`native-submission-order.addon64`，实际为神经渲染验证版，名称带observer的老日志字段不代表只读。
- 当前SHA256：`b6e42d355396a88a3f4181556240497a9514dfb9827e8aeee517b5aa322c0903`。
- 资产：`D:\DLSSNR-Lab\native-game-tiled-assets`；噪声表：`D:\DLSSNR-Lab\matrix-probe\native-runtime-rgb512\functions.f32`。当前不是自包含发行包，不能只复制DLL到另一台机器。
- 开关文件：`D:\DLSSNR-Lab\continuous-reset-preview.txt`。存在即自动慢速反复处理；初始化本次约195秒，每次处理约4秒，屏幕FPS计数不等于神经吞吐。
- 暂停展示：把上述开关文件改名为`.disabled`，当前在途帧完成后停止自动请求；还原文件名即可继续。无须杀进程或改存档。
- 回退到手动单帧：先正常退出游戏，备份当前DLL，再用同目录`native-submission-order.addon64.before-continuous-reset`替换DLL，同时禁用上述开关。该备份SHA为`61e731057b39233b8e9c55cdd6d08791c6a7196d99d6cf44e2b812f18552b1cb`。不要在游戏运行中替换DLL。
- 旧错误渲染器`dlss5-1080p-runtime.addon64.before-order-probe`仍禁用，不要恢复成自动加载后缀。
- 正常启动Steam游戏即可自动加载；现有任务`DLSS5GameLaunch`也可启动。初始化后自动处理，不需要再发请求文件。
- 日志：`D:\DLSSNR-Lab\logs\native-game-oneshot.txt`。确认当前PID的ready、render_begin、render_complete；render_failed不是成功。连续模式只保存request1前后原始帧，避免无限落盘。

## 本地封存与Git边界

`release/`已由仓库`.gitignore`中的`/release/`忽略。没有删除旧证据、权重或远端备份；没有把权重、图片、DLL提交到Git。

封存目录：`release/checkpoints/2026-09-07-correct-game-preview/`：

- `native-game-reset-preview.addon64`：当前安装版本，SHA见上。
- `native-game-neural-tiled-explicit.addon64`：手动单帧版，SHA见上。
- `native-network70-tiled.exe`：已通过整网测试的程序，SHA `cf9de2707d6ce5ea4699b41f55a9b15bb2b7739970cdf07058b57b30f591e319`。
- `run_nvidia_ui.uncommitted.ps1`：此前未提交的5090聚焦实验原样保留，未认定为正式修复。SHA `f267268583383d81b7e1a38ccb3568cc33d41098293bf29cb90b65a967f08698`。根目录同名脚本已恢复HEAD内容；需要续该实验时对比这份封存，不要以为改动丢失。

其余大文件保留原位，不复制多份GiB权重：

- `release/native-live-tiled-43156/`：正面像before/after及预览，原始线性数据预览不是最终swapchain截图。
- `release/native-live-continuous-21632/`：连续展示首帧、日志快照和被终端遮挡的桌面截图；该截图不能作为完整画质证明。
- `release/native-network70-tiled/`与`release/native-network70-profile/`：候选与基线输出、计时、验证报告。
- `release/native-color-frame/`、`release/native-temporal-valid1080/`等原版参考及权重保持原目录。
- 远端`D:\DLSSNR-Lab\live-reference-43156-1`已生成该帧原版encode；尚未完成其余原版重放。

## 恢复工作

1. 先看本页与porting-worklog最新段，再查Git状态、实际游戏PID和当前DLL散列，不重启仍存活的测试。
2. 用户20:14重新授权性能优化，暂沿用现有正确性基线；仍保留回归，不以牺牲神经输出换FPS。新帧独立核验与历史反馈未完成项保留，不改写为已完成。
3. 构建当前展示版：`bash build_native_game_verification.sh /home/lmxxf/work/tmp-test/minhook.KmbJvO/repo /tmp/reshade680.MNSVvW/include OUTPUT.addon64 --tiled`。先确认外部MinHook/ReShade路径仍存在；DLL已封存，不依赖临时二进制存活。
4. `deploy_native_tiled_verification.ps1`是旧手动版的一次性部署脚本，硬编码旧SHA且拒绝覆盖备份；不要拿它重装当前展示版。当前展示版SHA/回退方法以本页为准。
5. 不推送/发布旧网盘包作为完成版。完整目标保持未完成。
