# Step 1：新版 ACO 同机对账与 Small 同帧拆分

生产起点41c28059，GPU/驱动 RX9070XT / 32.0.31007.2048（Vulkan自报26.10.07.02）。新版源码只读自 /home/lmxxf/work/dlss5-mochi-008，完整复制到 /home/lmxxf/work/dlss5-phase4-mochi-build 构建；不修改原树。没有驱动版本白名单绕过，也未改系统驱动。nr_driver_identity/nr_pipeline_binary检查的是AMD驱动身份、pipeline_binary/maintenance5扩展及feature；原样通过PAL导入。

## 新版 ACO 纯 NN 同批结果

| 模型与处理尺寸 | HIP ms | ACO ms | 差 ms | HIP较ACO慢 |
|---|---:|---:|---:|---:|
| original1088 | 8.301703 | 5.670747 | 2.630956 | 46.40% |
| small1080 | 5.005096 | 3.430510 | 1.574586 | 45.90% |
| small900 | 3.841217 | 2.607886 | 1.233331 | 47.29% |

每档三轮NMMN，每进程80 warm +160 measured、每臂960稳态帧，严格串行；每个slot独立进程。HIP GPU event、Vulkan原生GPU timestamp总计/160。M没有逐帧明细，故不捏造其p99。所有正式slot均匹配对应冷校验输出的硬SHA；M三种输出另下载逐float有限值验证，N每次都检查首尾RGB32逐位同/有限值。

Old d1185d2/v0.0.2.5 的0.456466ms差距不能代表新版ACO。本批三档均约46%耗时差，否定“已证明接近硬件上限”。这仍是两套实现的耗时差，不是纯编译器差异：融合、布局、提交/链化和舍入路径不同；未证明跨实现逐位同或把全部差值归因为ISA调度。

模型选择：Original CC_SILVER_AARDWOLD，计划指纹ce136b3b…deb8efb；Small CC_SMALL_FP8，b584f69e…62ed4c（31块）。不编译NR_INT4、不加载EXPERIMENTAL/INT4数据。实际HIP模型由ACTUAL_MODEL requested/effective=1/1或0/0逐slot断言；旧纯网宿主遗留的静态full71/MP1字样不作为Small身份依据。

## ACO兼容与身份

53个Linux SPIR-V按上游FNV64算法逐一匹配原包53个.nrp，见aco-record-identity.json。使用原样NR_ACO_DIR，原生Windows SPIR-V仅作为PAL shell，实际ACO记录导入成功，未启用NR_ACO_NATIVE_KERNELS。CLI原样按windows/build/arch/rdna4.sh产品宏构建，Vulkan头文件/导入库只在隔离目录准备。首次管线创建约59秒，独立startup_compile预算180秒，游戏检查仍15秒；启动/建管线不进160帧计时。未强改驱动或把native fallback冒充ACO。所有缓存、计划、输出都写R4隔离目录。

Original使用锁版模型包只读（SHA2b41c888…f7fbd）；Small包SHA7e966e26…6e33。输入是同源半精度可精确表示的RGBA梯度：有效区两端一致，HIP处理区使用reflect padding，M使用其host-boundary同尺寸；Original1920×1088、Small1920×1152/1600×1024。源码、shader、可执行文件、计划、输入及模块身份见source-lock/各manifest。所有NMMN帧均historyoff/style1/seed0，不启用有损模型或跳块。

## Small同帧GPU阶段拆分

以下使用真实冻结HDR capture，和上表合成输入属于不同数据集，不能跨表相减。复用现有HIP timing与D3D12 probe，只在隔离诊断宿主打开；每档三轮ABBA（无event生产Frame vs诊断Frame），各960稳态帧。诊断中每帧同时取NN GPU event及encode/input/network/neural/decode/copy时间戳，前80帧丢弃。

| 档位 | 无event Frame wall | 诊断Frame wall | 同帧NN GPU | 前后处理GPU合计 | bridge区间−NN | 诊断增加wall |
|---|---:|---:|---:|---:|---:|---:|
| small900 | 4.147897 | 4.314636 | 3.707780 | 0.157123 | 0.220516 | +0.166739 |
| small1080 | 5.414077 | 5.577623 | 4.768868 | 0.241598 | 0.323158 | +0.163545 |

前后处理GPU合计=encode+input+neural+decode+copy；bridge网络区间包含跨API交接/提交间隙，不与纯NN混称。Frame wall还包含CPU及同步开销，wall−NN不能全部叫前后处理GPU。诊断增加的开销已用无event对照明示；无event生产默认未改。细项、逐轮和输出身份见step1-statistics.json及step1-finite.json。

全程game-check、进程检查、D≥100GB、CreateNew独占gpu.lock、15秒游戏看门狗，保留每个slot，不刷轮。Step1后清理raw输出/每slot exe等大文件；后续Step2/3需要的隔离宿主、输入与模块交付保留。锁版d1185d2原始文件树和原始v0.0.8源码树前后核验，未触碰游戏安装或已发布ZIP。
