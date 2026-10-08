# 当前工作计划

更新：2026-10-08（仅剑星模式3试装状态更新）。此文件整份重写，保存当前状态与尚未完成事项；历史过程见 DevHistory.md。

## 工作规矩

- 具体编译、实验、安装、归档交子代理；主进程只调度与审交账。
- 有人提 PR，能合入就尽量合入。
- DevHistory 只追加；WorkingPlan 整份重写。公开记录只写客观工程事实。
- 单队列使用 GPU：先 game-check、原子 gpu.lock、15 秒游戏看门狗，实验实际写入/缓存盘至少 100GB（9070仍用D；5090本次小探针用C，D数据只读、共享锁极小例外）。游戏运行时不抢 GPU、不换载荷、不结束正常游戏；继续独立 CPU 工作。
- 无损候选保持 K 累加顺序、舍入、FP8 编解码、NaN 与正负零合同。先小筛，有可靠收益才进正式门；慢轮或尾延迟退步不刷轮掩盖。
- 性能使用连续 TimingOnly、首尾读回，中间不扫描图像；坏事件负值批隔离。微核与 DUP 边际不相加当整网或 FPS。
- 默认不变、配置逐字保留；安装前备份、安装后 readback 与 exact 快照同步。短记录后及时提交，不 push、不改外层仓、不擅发布。

## 已完成的发布与当前安装

- 0.40 基线 tag 为 c81a88bc。0.41 已构建、三包验证并交付；台账提交 523723fd，二进制源码标记 ab8e3e82。正式 annotated tag `0.41` 指向 523723fdb3fa5b322beb1cc9dcfd3f8183eaf334，已仅推该 tag。
- 0.41 三包在 `/home/lmxxf/work/dlss5-release-0.41/` 与 Windows `D:\給網友打包`，SHA256SUMS/发布台账齐全。镜像已记录于 70ebd402：夸克 https://pan.quark.cn/s/dbda3e470f8f ，Gofile https://gofile.io/d/YAENU0ex 。已上传 ZIP 不再修改。
- 发布默认为 MULTI_PASS=1、MULTI_PASS_PREDICT=1、SKIN_PROTECT=0。预测只在选择 3x 时执行两遍真实网络并预测第三遍；显式 PREDICT=0 为真实三遍，1x/2x 不受影响。
- 发布包为 38 模块/架构、共 76；每架构五行公开 LLVM23.1.2，其余 COMGR LLVM21，双架构 ELF 目标已核。旧 rtc 忽略目标的问题已通过当前源码重编工具与目标检查处理，不再列为待发布阻碍。
- 发布后的三刀已完成：76750a80 最终 RGB 共享输出免一次 copy；9bbd3749 block4 pool→首 C64 字节边；c756f296 仅真实 1440/FAST1 的同数学 SP-fast 持久队列。各自数值、正式平均/p99及必要宿主兼容门通过，收益不能跨批相加为 FPS 承诺。
- 当前本机载荷（不是正式发行包）：剑星addon4ceef29f为模式3源码e8fa0661；鬼武者根/_storage_ runtime3eaed204仍模式1。每游戏79模块（仅gfx1201 C32 normal/RTZ/FAST/norm900更新，gfx1200旧模块保持）、SUMSC08125C2与原64B row77c745；模式3安装不改模块。剑星最近回滚：`D:\DLSSNR-Lab\mode3-stellar-deploy-20261008\backups\20261008-112330\rollback.ps1`；双游戏模式1安装回滚仍见mode1-deploy-20261008/backups/20261008-022921。
- 当前玩家配置与发行默认分开：剑星MODE3 / ENHANCE_STRENGTH1，鬼武者MODE1；两者MP1/PRED0/SKIN0、HEIGHT=auto/FREE_RES=0，原强度/AE偏好保持。未声明MV/depth合同，使用明确静态回退；时序模式要求MP1，切模式/增强强度需重启。未代启动游戏；模式3完整NN/游戏观感及性能验收未做，等待用户试用反馈。
- 既往实玩剑星 1x 约57.6fps、快速3x约37fps；鬼武者900P快速3x约49fps、强度更新后无异常。均为用户观察，未提供三刀后的同场景 ABBA/FPS 验证。
- PR15 已正式 merge 8a6c7bc1，保留贡献者作者；Enqueue 入口恢复已选 HIP device，0.41 已含。RE9 强度文件数字覆盖已含；auto/缺省继续尊重宿主参数。中英文 README/配置页与公众号使用说明已完成。

## 临时0.41-a交付（用户授权，正式0.41不动）

- 默认off的regular FFX pre-upscale MP1 history可选分支已接实际游戏框架，key为TEMPORAL_HISTORY_EXPERIMENT；MV_UNJITTERED=1是显式试验前提，未检测context flag。MP1/AE0/graph0/skin0/overlap0限制，旧prefix/smooth/history路径不混用；首/reset/缺MV/曝光变/gap与F9→MP3回旧Body/seed0，回1先cold。
- 720/1080 core独立gate与off/cold/hot门逐位0diff finite，actualFrame720 metadata门通过。参考gate短GPU均1080约9.15→14.18ms（+5.03ms），明确临时质量试验成本，无FPS保证；无真实roof改善结论。源码freeze1a22ee96，结果history-trial-041a-20261006。
- regular临时ZIP正在隔离构建，保三刀，新3模块/架构共6。未安装本机、未改玩家配置；RE9/Magpie不假称支持实验。stock gfx1200发现27行错target，打包前按source/recipe identity复用官方041正确target或canonical定向重编，保pool字节与SPfast导出；不能带错target交付。source identity核验stock-recipe-identity.json。后续网友试包与优化按用户安排进行。

## 第一优先：建筑房顶闪烁（受控阶段已闭环，真实场景待验证）

用户优先序校准：仍先处理闪烁。下列真实输入与场景验证缺口是当前主线；本轮未新跑实验。

1. 当前证据：111.mp4 是约4.11秒、119帧、29fps的竖幅拍屏最终画面，没有原始网络输入、MV/depth或开关 A/B。不能伪造网络复现或由视频直接归因 HIP。
2. 重新跟踪同一房顶表面后确认局部亮度反复。1.586/1.621/1.690 秒 roof Y=96.6/116.2/98.1，UI=79.78/79.87/81.55；第一步 roof 跳变明显大于参考 UI。最初关注人物运动/草地而暗示静物稳定已纠正。拍屏曝光、透视、游戏自身 TAA/高光仍有混杂。
3. 网友场景线索尚未独立复验：跳32–36、38部分抑闪；全跳31–38房顶反光基本消失；40后块对该反光无影响；31–38单跳任意一个仍闪；奇偶各跳4块分别抑中间/边缘。31–38实际均为同形完整全局 ViT，无奇偶 shift，37没有特殊结构。删除反光不等于保留反光并稳定时序，跳块不是无损修复。
4. 尚待确认网友使用 `DLSS5_SKIP_BLOCKS`（全遍）还是 `DLSS5_MULTI_PASS_SKIP_BLOCKS`（第二遍以后）；不猜。该缺口不阻碍独立时序代码分析。
5. 当前原生 pre 路径每帧 reset=true/seed0；可有 prefix history 输入，但没有原版 motion 重投影与门控 post history。OUTPUT_SMOOTH 是独立近似，不能代称完整原时序。
6. 新确认资产缺口：当前 post70-head.f32 仅 RGB 的3×32；原生16×32权重中 row6 为非零 history gate，但现 unpack 只导出 row0/2/4。原 blend half=0.73974609375。已独立恢复 gate32、保RGB96不变；原SASS确认两K16 HMMA.F16及SIG/blend/FFMA合同。真实5090两Eval确认Reset1→0、seed0→1与history/MV空→非空。标准CUDA查询当前context为空不代表资源不存在；10-06沿私有资源包装映射已确认有效history1920×1080 RGBA16F/alpha1，与原post内部blend输出逐half相同，API final解码域不同。
7. mochi ReShade History 默认1；History0仅关闭 post blend，prefix history 与 seed推进仍存在。低层 API 默认不同；网友所谓另一家未具名，默认状态未独立核实。
8. 已完成默认关闭的MP1实验：纯空间、prefix-only、prefix+gate三路，固定seed0/逐帧seed拆因子。小合法NN48行、valid1920×1080/proc1152合成NN18行全finite；off/first/reset对独立当前基线0字节差，自重复0字节差。仅有效RGB存历史，padding镜像/后处理有效区分离。10-06原HALF4 surface gold确认RTZ，隔离store从RNE改显式RTZ；该RNE缺陷只在实验原型，不是生产闪烁已证实归因。首批继承模板AE1已隔离为diagnostic；正确VIT_ADAPTIVE=0重测过。
9. 原post16×16 closed/zeroMV/+1px/对角亚像素gold与软件5tap/严格gate全float-bit0；head24576控制特征半码0差。AMD SIG最大3ULP差已量化，实验严格用NV half域表。原型保rawΣ×reciprocal的融合减RGB顺序，F64 head仍参考实现。均值/同geometry波动分开；18行1080合成数据不显示普遍抑波动，不宣称闪修。
10. 未改默认、未装游戏、未改0.41包。尚需真实连续输入/MV/jitter/exposure/reset与真实反光/遮挡拖影验证、MP3各遍历史规划；仅保留实验原型，不把跳块当无损方案。结果见results/temporal-sequence-20261005及post-history-gate-20261005。该缺真实源不阻碍独立优化；本轮已按顺序完成下面两项裁决。

本阶段新完成：history-contract-20261006已确认私有D3D包装映射、post先读旧history再保存有效blend、内部RGBA16F的受控RTZ；temporal-replay-contract-20261006隔离store对四组原gold float-bit0。实际codec/input/motion/coordinate shader转码两次hash相同/finite/mirror同，direct-UV prepared MP1三路×两seed×两帧12行baseline/repeat byte0；CPU租约/manifest/receipt门与完整Windows链接已过。真实源采集入口b439efc6/adbb2bf2仅CPU完整链接与保守source-write/alias/thread守门，未安装/GPU采集；Mode2 FFX-only无同期NR闪最终输出，timing_valid=false。

下一步必须取得真实连续场景源并核资源身份、effective flags/modules、color/MV/exposure与jitter/depth合同。已备统一manifest封存与explicit recipe→原codec→direct-UV prepared-index工具链；保留seed=null、upscale[0,0]与未核identity为blocker，不填曝光1、不猜方向。离线converter direct raw FFX fit未跑FSR，仅受控replay；新regular入口按已有生产FSR/codec接metadata，真实画质、遮挡/拖影/反光稳定性仍未验证，MP3分遍/共享history未定。新临时分支默认off，现装生产/玩家配置/载荷不改；本阶段不继续追加synthetic案例代替真实证据。

## 持续优化目标：同shape/full71追平或超过mochi

- 当前1088/640共同编码输入首对账FAST1约8.8808ms、mochi7.8672ms，差1.0136ms；FAST0差1.1428ms。生产1152与mochi1088异proc另列，不选有利小窗口作完成判据。
- 16-query数学阶梯B（仅score halfFMA/map）及C（加64key half树）原语gold0diff、双arch无spill，但同1088整网首ABBA B平均+0.00761ms，C+0.03643ms且p99变差。不收，不D、不旧M32扩扫；52.20/52.63dB仅对当前FAST1一份synthetic输出，非NVIDIA oracle。ISA显示Bscalar转换往返、C更多halfadd/shuffle，不能判纯数学无解。结果vit-math-stair-20261006。
- 当前低扰动stage只探C51223–30/ViT31–38一pair，ordered/pdl_calls0、raw同；插桩反使total快，不能认精确族贡献或均摊。空timedpair正、非blocking query与1us CPU delay负，支持event特有效应但未证明driver flush。完整框架既有真实HDR双档首筛正，NETspan与完整wall分项记账。
- 同batchT/U没有支持untimed优于timed。正式timedpair三round900平均/p99均正，1152平均均正但round3 p99+0.08934ms，按门不收、不刷samevariant；当前没有足够新生产优化，900仍差约0.6ms、同1088差1.01ms。旧logger无tag只可受限分布关联，不据NET尾下降就定CPU全因果。
- 无guard旧single900prototype首筛mean/p99正（different source）；safe1152首筛也正但旧记录计数缺。**更正：safe900formal旧stderr有frame-scope-fallback；APP900新权威计数Create1/Destroy1/Record0，均不是有效pulse性能试验，撤回正式性能负账归类。** 原CSV/平均/p99保留作normalNN/资源条件观察，不认single Record慢。
- APP900初计数Record0是错误lifetimePDL veto导致fallback，不是性能负账。独立primary审6ecf9a0c确认Body22→普通Down(c256)→固定marker的有序边界；隔离仅在真实该Down普通Run返回后flag+当前selectorfalse放宽，PDL1不改，不推广任意位置。实际900 cold/warm/改HDR+seed7/return Record5/PDL>0/bit0finite，MP3与history-used回退门过。
- 真on APP O2/NET0同exe首screen：900mean−0.09279/p99−0.19514ms、1152−0.09209/−0.12544ms，两侧candidateRecord160/160与createDestroy1平衡，诊断outerAPIs0，原postquery160。900lifetimePDL640/firstprefix2、1152PDL0，raw同finite。真正formalround1双档已过：900mean−0.08184/p99−0.11964ms，1152−0.09068/−0.05103ms，实际Record320(80warm+240steady)/API资源/PDL边界/bit0finite全过；原cold全部保留。独立反汇编同1us计时源，微小插值正号按原端点/控制漂移完整交根进程，不自设容差或舍负号。尚未收；正式三round双档全部过且逐轮即时判：900 pooledmean−0.108606/p99−0.15644ms；1152−0.103064/−0.04322ms(eachside1440steady)。六round全mean/p99显著正、3840candidate Records成功与资源balance/diag0/PDL边界/rawbit0finite一致，旧pseudo-on不借证。下一reviewable实际scope可选接入/正常framefallback兼容及H900独立合格后组合实测，不把局部合格写goal完成，不改现装/config/ZIP。

- C32原fragment轴已纠正，不再把原布局误判列向。保两WMMA/原矩阵布局仅rsqrt hoist独立unit/prefix/fullraw0、occupancy16，1088首screen约−0.0611ms，actualAPP900三formal平均全正；round3p99+0.000140ms原值保留，MinGW计时1us量化/np分位数插值使0.14us低于分辨率，三轮pooledp99−0.07458ms、control漂移35–48us。根进程据f38c3cb4裁该轮测量内持平，只进900兼容，不能写每轮p99全负；1152首p99+0.07399ms仍负、不能放行。全目标未完成，不与pulse收益相加、不混数学与提交。B2/C2短筛无足够稳定收益不收。

## 第二优先：解释mochizuki差距（备用CPU工作；新实验未执行）

闪烁仍为第一优先；等待真实连续源期间可继续只读CPU锁账与准备，优化GPU实验按闪烁主线安排。

1. **锁账与同步纯网络对照**：当前0.41＋三刀/full71/FAST0与FAST1，对锁定mochi0.0.2.5旧exe/SPV/plan/effective宏/源/模型与输入。当前本地mochi4f62a8a不是旧测速d1185d2；现已找回旧源码、70资产与逐帧chunk1默认锁证，撤回旧“一次提交500帧”误读与0.1～0.3ms猜额。固定valid/proc/token/有效块、MP1/PRED0/SKIN0/AE0、Style/seed/history；统一GPU边界与D2D口径。纯Network现分支CPU chrono＋逐帧sync不是GPU timing，保同步加GPU事件；无逐核事件、首尾读回。旧差距与事件均摊/DUP仅诊断，不认精确贡献。
2. **提交交叉对照**：先每帧同步；pool复用/PDL keep/SP代际安全审核通过后才1→8→500新诊断批量，不能直接删sync。GPU均值与CPU吞吐分别列，同实现输出逐位不变；错误/坏事件/增长即止。
3. **同HIP16-query数学阶梯**：FAST0/当前FAST1→仅score halfFMA/位图→64key half分母树→仅确认后的最终half/context乘half inverse（旧有效NR_ACC_F16=0，不试对手未启用的每块AV截断）。当前分母/AV为f32累加。每步数值、资源、整网平均/p99独立记账；新增状态/资源不能称纯数学，有损研究不部署，B/C无收益不自动D。仅显著资源变化才复查旧M32交互。
4. **局部ISA排程**：新热点账证明等待段后再做，不预估收益。跨层窗口≤4父子依赖，不能简单寄存器跨层，暂无新候选；旧全空消融是整网边际并改变下游，不是本体上界；8streams负账不泛化为所有队列严格上界，不重开旧线。

具体证据、最小实验门与止损见results/mochizuki-gap-audit-20261006。闪烁主线及以下两项已止负账继续保留。

## ViT960 contract 大 tile（静态负账，已止）

- 与旧 attention 恒960常量化不同：尝试两wave各16tokens共享64列K512 FP8权重32KiB LDS，保四K1024 partial、skip、FAST_H与byte出口原顺序。
- canonical双arch实际baseline208VGPR/0spill/0private/4KiB LDS；候选256VGPR/171spill/688B private每thread/32KiB LDS。静态门直接拒，不跑GPU、不给上游微核数字作整网承诺、不扫更多tile参数。
- 候选仅保实验源码与ISA资源/hash，生产未改、未安装。记录main64257116，results/vit-contract960-20261005。无新瓶颈证据不重开。

## 小请求晚批复用（真实1440短筛负账，已止）

- 默认0实验宏；≤8MiB请求只在原use_count1/足够capacity集合再排近期possible-use块，包含较大capacity。用logical launch-batch：连续DUP一批、SP init/run/recovery一批；非launch P可多延迟，不能当物理GPU完成/精确lastuse。graph旁路，PDL引用条件不变，不提前复用/释放。CPU七case过。
- 同源A0/A1、相同当前39/gfx1201模块，实际valid2560×1440/proc2560×1472/960token，full71/FAST1/MP1/PRED0/SKIN0/AE0。完整框架连续首尾读回、160帧/槽弃80：首ABBA A15.680819→B15.742244ms，慢0.061425ms，两B槽均慢于两A；raw全SHA相同。
- 策略实际命中recent_rejects34560/进程。按门立即止，不刷剩余轮、不跑无意义正常19/额外档、不改生产/安装。candidate.patch、CPU模型、CSV/hash齐；自己实验raw清理，weights/输入未动，锁已释放。main661c41a9，results/pool-coldage-20261006。
- 上述三项本轮均有具体结论；用户游戏载荷/config与0.41发布ZIP/tag不变。闪烁真实场景仍待连续源/实玩，不写已修复。

## 其他未完成事项与限制

- Issue13：独立5090原版 exact合同双帧 p95约14.94%已闭环，原入口一致、原post FP16 surface已抓。不能据此解释所有 production闪烁。Test20报告23.315485%、注入各自 exact pre-down降16.327520%，但其源码/commit/effective flags/module SHA尚缺；取得指纹后再定位。prefix16/投影32是原核寄存器/LDS中间值，不能用CPU仿写冒充未改原模型输出。公开数据与tiny pre-down包在 https://gofile.io/d/FWpuapJe 。
- Issue4：贡献者双HIP设备 A/B/C证明入口绑定修复；本机只有一个HIP设备，真实双设备与线程ID验证仍缺。LUID选择本已正确，400标签可能来自懒 GetFunction而非launch，不泛称跨adapter问题。issue未擅关闭。
- gfx1200：模块目标头已核，真实对应硬件运行仍待；不拿gfx1201验证代替。720几何独立NVIDIA oracle仍待。
- AE720旧runner曾漂移；同HEAD fresh基线正常/AE/CSV/roll已过，旧产物异常未定位。无新具体证据不扩大重查。
- 真2K FREE_RES=1真实游戏观感/整帧成本待玩家选择；当前FREE0，不悄然把900配置切2K。HDR、FG、长期运动/遮挡质量不能由少帧离线门推广。
- PRE_UPSCALE auto 的 Forza/WoLong实玩待确认；剑星 native PRE1仍覆盖auto。剑星 native STRENGTH=auto覆盖custom数值的层级需设置时明确，不能擅改层级规则。
- 内存增长尚未复现；D3D/HIP固定交接税与占比已有研究，不推普遍速度越快必停滞或HIP无解，不开展新TDR试验。
- 已封存负账：C512 LUT、C32固定几何、ViT960 attention常量化、其他LLVM23行盲扫、IO_FUSE尾延迟、无效C512_T8宏、DEC_F8W/COMPACT路线。只有新瓶颈证据才重开；已收 directRGBA、ViT byteedge、C512 directpack、1440融合及三刀不重复当新候选。

## 2026-10-06 生产单marker闭账与组合下一步

- 044f2cd4（Hbase25c2df78）当前可构建生产source；auto锁gfx1201/Runtime7/实际driver32.0.31007.2048/currentactivecaps+full71FAST1/MP1/AE0/nongraph/ordinaryDown boundary，0关、1明确同arch/topology未测driver实验。existingconfig/数字/qualitydefaults/现装/ZIP未改。
- 11516因我错误mandatorySP optionalinit_pair导致capfalse/Record0，保失败logs不认性能；实测SPpair0已CPUELF锁，6904修后autoRecord5/raw0finite，8696新MP1PRED1inactive-policytrueautoRecord5/raw0finite。runtime预测仅MP3实际active，MP2/3仍fallback，不把requestedPRED1视MP1调用。
- 32413全部10slots/30pairedframesraw0finite和资源过：MP3off/history用时off/reset无historyon/graph同PDL0off/显式1真实Frame900→1152→900各新租约。原exit1仅stdout误查stderr，CPU真实重建及全部counts闭账，无GPU重刷。freeze.json/canonical库SHA与scope-cpu-closure已归档。小GPU门支线停止，无额外synthetic。
- H900生产route25c2df78与a25d82df兼容已闭，normal19/seed/validnormal25完整fallback、真实热MP1H→MP3base→MP1H raw0；两路线组合必须从正式0.41ZIP/tag/payload锁同O2NET0真实APP直接比，不加独立百分比。旧78164PRED1导致pulse0不称完整组合；新044fsource需真Record计数。900MP1可H+P，1152MP1仅P，MP3两者实际fallback，分cell明列。目标仍有mochi残差待fresh纯NN更新，不写已追平。

## 2026-10-06 正式0.41→当前组合四cell首筛（9b1eeaf3）

- 同发布041锁定codec/模型输入、各自真实O2/NET0完整APP、单ABBA160弃80，allrawbit0/finite：900MP1平均−0.200425/p99−0.16323ms（实际H+PulseRecord160）；1152MP1−0.185519/−0.13871ms（Hscope0/PulseRecord160）。这是直接组合测量，不是独立收益相加。
- 900MP3−0.043969/−0.03935ms、1152MP3−0.071875/−0.03256ms，H与Pulse均按scope实际回退。只有单round且MP3弱/可能漂移，不宣称H/P在MP3有新增稳定收益；不擅安装/改cfg/ZIP。
- C512 denominator新组织八gold/三方NNraw0且1088微小首筛，actualAPP1152平均−0.000294ms/p99+0.05417ms，已止不收不刷，不列待收收益。
- 新C32FFN feature舍入疑点历史9/28已有NR_Q32_STAGE与half边界讨论/0x3f880001反例；此前未独立跑“feature直接FP8、residual保half”分支。不能重做packed-half activation bit4负账或称Hrtz无效，新方向只能明确该出口有损阶梯，原acc/gold/实际Mo配方先锁。
- fresh1088/common640纯NN首对账60566已完成：当前FAST1 GPU均值8.931545125ms、锁定mochi 7.885259375ms，余差1.046285750ms（results/fresh-mochi1088-20261006）。同编码输入/Style1/seed0/full71/逐帧同步边界；当前与此前43项manifest共享42项SHA全同，H900scope不启、Bridge-onlyPulse不进directNN。M只有聚合GPU时域，不能补造逐帧p99；APP约0.2ms不能从此pureNN余差扣除，也不能跨批称回退。目标尚未完成。

## 2026-10-06 C32 feature出口独立审与当前接受门

- prepare.py主模块唯一变化是FFN feature从half rounded量化改为ffn[ci] F32直量化；cw_rtz_half8与residual memcpy逐字保留，activation bit4/投影/存储/helper不变。canonical old与stock三sections同，old/new 26导出ABI同、prefix128VGPR/4096LDS/0spill；不推占用率或速度收益。
- 91972原gold失败保留：动态half-vector bitcast捕获误重复element0，anchor读3bc1；98d6e633 ISA与输入定位证明这是gold捕获bug，非生产helper损坏。70170仅修捕获补门已exit0/release，residual bit0且anchor3c40；主候选module不变，小核通过不认生产接受。
- 下一必要门为锁定actual fullNN old/new输出误差、finite与重复性；有损实验不默认、不以feature差码直接宣画质收益。18d8829e原NV半累加→FP8合同仍成立，Mo直F32位点不能证明新路线更贴原NV；需要相同位点/输入合同才能比较。ViT实际640 QK/AV次数同且无尾padding，额外分母已有B/C/C2负账，不重开旧阶梯。

## 2026-10-06 供数研究停止与硬件诊断准备

- C32 direct-feature（150cb252）：整网valid PSNR51.1442dB相对OLD，非NV oracle；residual保原half/repeatfinite过。38260 GPU均值−0.014088/p99−0.028294ms，wall−0.013644/−0.08372ms但control漂移较大；只局部24VALU，解释实验止，不APP/formal/集成，不能解释1.046ms主差。
- Mo真实PAL由isolated pipeline-create cache.text/SHA精确映射（f6c7d2bc）：g_attn182VGPR/9216LDS、VT114VGPR/0LDS，wave32/scratch0。production C512为189VGPR/SHA9290…，e63625b5纠正旧256误用；不能直接从182/189推占用率。
- VT trload仅原AoS地址供数替换、不改producer/数学；1008285e/9a0241a9 CPU及50448 wave32 bytegold通过，b6cf93bd canonical仅1/78函数变、其余77bytes同。实际整网raw0finite；24057 GPU mean−0.03505/p99+0.03204ms、wall−0.01045/+0.02385ms，尾负即止，不formal/APP/集成。与旧物理转置/M32/960合同分开，不换名重开封存负账。
- 下一诊断复用现成RDTS RDP CLI/SPM+SQTT；旧mixed D3D/HIP capture改输出，21.7%stall已撤回，纯HIP重复核hot-cache也不能当fullNN。现parser实际仅busy/stall/cache/fetch/write/PCIe，未证VALU/SALU/phasewait；CLI help无可见SQTT-off，抓取会改clocks，非native性能。
- 无capture countvalidation31928已exit0/release：同1088/640当前fullNN、cold161派发、80warm累积13041，每frame稳定161，候选one-based13042/count161整帧窗口。普通/ext Run与SP四直接API分支hostcounter全覆盖，无逐核events；pureHIP首末rawbit0finite，尚待capture边界审/同hostreference与TraceConfig及clocksrestore门，不预写硬件瓶颈结论。

- 校准capture97053已exit0/release：同31928 exe纯HIP，候选13042/count161，2000frames长活；首末raw与同host无capture/lockedstock逐位同，driver日志setpeak+restore，未独立读硬件clock。TraceConfig一致但global初始化偏移/完整frame名字门未闭，不能称161名序列已经确认。真实5763SPM样本、interval4096，rawtimestamp跨度1028352（单位转换未证）；窗口memorybusy87.275/stall19.784/L0hit77.317/L2hit96.863%，不是正常native帧时份额/核心瓶颈因果。4SqttData证SPM+SQTT，现reader缺gfx12dispatchmarker解码；math找现RGPexport，先CPU解码而非盲重采。详见results/fullnn-spm-20261006。

- mochi同条件窗口3747已exit0/release（results/mochi-spm-20261006）：原d1185exe未改，nocapture两末reference83819重复/finite并与freshM同；实际steps.size123×chunk1提交闭合modeldispatch/frame，但noise初始化/RDPglobaloffset未证。warm80/2000长活、候选9842/count123，捕获末raw自身same、原资产未改、driverrestore日志过；无firstbeforewarm。5296samples/4096interval、4SQTT，memoryunitbusy90.236/stall12.944%、icache65.075%、L0/L2hit75.191/95.293%，只是窗口观测；不能将busy称GPU利用率或推compute/bandwidth/正常帧份额。当前HIP/M窗口分别161/123renderops未frameverified，provider/device/config与SQTT映射先CPU独立审，不盲再采。

- 两边Budget3单稳健性门53594/96434顺序exit0/release，actualCLI483/369、sameexe/ownrawexact/finite/assets/driverrestore过。sample17326/16496、4096interval/providerconfig同；HIPmemorybusy87.275→89.816/stall19.784→20.349%，M90.236→90.205/12.944→12.922%，cache变幅≤0.103pp。HIPbusy明显窗口敏感，stall/cache差仍只是同provider窗口观察，不能推1.046ms因果或compute/DRAM。3模型预算不称3完整frames，SQTT容量/样本自洽不证无丢包；无下一采集。下一CPU研究仅按独立provider/实际source机制定位，见fullnn-spm预算对照。

## 2026-10-06 实际161/123序列与唯一精确融合点CPU审

- 小门89772/15597顺序exit0/release/rawsamefinite；HIP首cold161实际Fn key、M原--wiring finaldisp123已取，steady只核总count不冒逐名同。extra38由阶段账闭合：C32front+1，encoderC64/C128/C256 +4/+6/+5，C512head+1，ViTpack/反gather+9，decoder39−1，decoderC256/C128/C64+5/+5/+3，其余0（results/dispatch-sequence-20261006）。不将38归attention或估ms。
- SP_SMALL旧d7b29df2/APP/normal配方负账不是当前W16/FAST1/pure1088整stageDS/UPS融合同条件复测；原内层4→3/2→3组织不赚仍封存，新条件本身不构成重开证据。ViTproducerpack、consumerinlinepackV、gather各负账明确，不能换名重复。M1kernel仍写globaltiles；HIP6层各独立FP8output存储12,533,760B/stage，不将allocation当DRAM流量。
- 当前SP16..21之后先Body22再Down，不能挂pool在SP21或扩SP22混recovery。Body22实际c64-wave2-fast/W2_FAST_NUM3，raw?3调用Hrtz有效identity、写fullF32；Down缺fasttwin实际加载normalpacked/H半RNE链。实际loadedSHA已锁，融合必须两helper独立，不偷偷补fastmodule或half化raw。
- Body22 shift2(sx0,sy4)/8×8窗口同WG全256channel，2×2pool可windowlocal；完整Down256→512投影需保pool_H/F、K32两K16WMMA+Hacc顺序。已有Down16连续rasterhelper跨多producerWG不能直接尾call。新的register→pooledLDS约8448B/屏障/寄存器成本待编；135窗口projectiontiles vs原128(+5.47%)，rawskips3仍decoder48读不得删写。只静态可行，不收益结论；math下一CPU隔离设计，旧C64/C128 fence+rawreread负账不泛盖该directreg方案。

## 2026-10-06 Body22路线停止与下一唯一Up边界CPU候选

- Body22→Down路线全STOP：共享pooledLDS41216版本gold/raw/count161→160过，首GPU mean−0.014568/p99+0.0632174ms、wall−0.01015/+0.07612；regcache private192静态止，noescape private0/LDS32768/VGPR242、gold0/count161→160过却GPUmean约0/p99+0.0099048、wall+0.0112/+0.03254且control漂移，root不APP/formal/继续调。86300694/7b1faa96/3ed96f5e/fbd2f6e0留真实证，不算生产收益。
- actual下一边界是Up48→普通Body48→SP49..54，非Prefix49/SP50..54（up48-body48-audit）。deep11a25/Body65848 SHA锁；Up独立RNEH(total)/RNEmerge(F(skip3)*scale)→fp8add0 bytes，BodyFAST3原每head32 Q/K norm及原w2_rtz8 byte残差保。不能直接复用FAST H改Up半边界。
- 单WG8×8高→4×4低/完整256channel supply可静态构造，Up中间2,088,960B可少materialize但skips3原raw仍必读、SP不扩。135lowwindowtile vs原128 flat tile使UpWMMA+5.47%，寄存器/LDS活跃度未证。旧UP_DIRECT4倍MMA/W16small/SPinterior负账不是该C256边界同形；仅一个CPU隔离候选由math生成，未GPU/未生产。

- Up48 shadow42950 actualUp/mainBody/tapBody均byte0，原stockgraph输出153ac finite/hash过；只能认局部producer/Body合同。wholeNN隔离route普通stockgold已由执行者通过，source还发现Up admission缺actualwaveactive导致unsupportedfallback可能消费F32sentinel；owner最小补actualwave_owned_active/HasFn及dtor先选owner，修headerSHA3a4686…待其新版gold，旧正常active数据不作废。低/skip强Tensor持有、Bodypost0/Newbyte/成功launch后reset源审闭；不通用API恢复/生产接受（route-review）。M32×数学候选目前原语门过、整NN误差待，不和losslessUp收益混加。

- Up48→Body48单路线fbedb85e停止：post0/sourceguard新版whole raw0finite/repeat0、actual161→160、shadowproducer/body0均过，92402首GPUmean−0.003447/p99−0.011528ms但wallmean+0.003681/p99+0.11964且GPUcontrol漂移+0.022009更大，root不APP/formal/调同候选。无生产改变，局部gold不等性能收益。
- Mo score-only反事实fb5c136f：65资产/60SPV仅1变，QK/AV/半den64/布局/末端保；自身repeatfinite过、vs原MoPSNR52.7257/max0.03102，GPU+0.0093625ms/host+0.0049但controldrift+0.0252125/no逐帧p99，解释实验STOP，不替锁baseline，不足解释1.046ms。
- graph0外层capture-only CPU审PASS（842a3ac8）：warm80/current固定fullNN161SP保持、PDL0/pulse0；实际cache/alloc/plan拒、SP宏0及无前缀env/rollover门、Endonce/statusNone、输入权重pool/SP backing保持与Owner清理源核闭。只枚举Kernel0/64Params/Fnmultiset161然后销图，不instantiate/replay，不ReplayReady；实际capture-only43912已exit0/release，161Kernel/Funcmultiset/EndNone/销图门过；当时无replay。旧Graph主动禁SP路线不同不混。

- graph0单次replay新源只读PASS（single-replay-review，96ce799f）：GetEdges/Kahn每步唯一零入度/161逐Fn强排序才instantiate1；同stream0xff poison→launch1→sync→finite/全RGB153ac原位门，不用Enqueue替代。发现Owner drain失败仅留handle却继续Net析构，owner最小改flush+_Exit73不继续借用资源释放；不称API恢复。实际单probe72596已exit0/release（03ef727f）：161Kernel/160edges唯一排序、instantiate1/replay1、poison后全RGB153ac bit0finite通过；当前仅固定input/seed0一帧，未来输入变化/history/多次/性能仍未升级。

- bounded3fixed+1changed新源57b4a1e7只读PASS（bounded-replay-review）：CapturedNet记录后不再Enqueue，pool/SPparams/指针保持；每次poison/launch/sync/read有限值，changed同GPU地址R棋盘±1/64、alpha/padding/seed0保持，独立ReferenceNet自身pool/plans/weights eager给新gold，第四次必须等gold且异initial。Ref资源非graph借用可各自销毁，Captured资源到销图保持；实际74023已exit0/release（bf2c7e4b）：3fixed raw153ac、changed独立Ref/Replay7fe026…同且异initial，四sinkpoison/finite/bit0；仍不速度/生产/任意seedshape宣称。

- graph0同work性能source公平性审PASS（0087d9d0/performance-review）：同O2caller/current1088FAST1/full71SP161/MP1AE0/PDL0/pulse0，setupcapture/instantiate与DAG日志分列；3总read=commonsetup1+selected首末2，均steady外，selected80warm160measure，无热Fnrecord，outerbegin/submit/end/EventSync同界，Elapsed finite/>0拒批，GraphAPI241不硬件counter/OSsubmit。
- 首ABBA26358fb3 actual87130通过：GPUmean−0.080413/p99−0.084621ms、wall−0.100147/−0.08799，两B快、GPUcontrol漂移+0.006763ms，12raw153ac finite、SPdisabled/errors/fallback0；root R2报GPU−0.095575/p99−0.179433、wall−0.101866/−0.0919，R3 actual49361已PASS（4efbcf46），GPUmean−0.096417/p99−0.123420、wall−0.100059/−0.07079ms；三轮完整36raw153ac。只固定形状samework纯NN诊断，不APP/生产、不从独立M1.046285750差扣减；正常自主clock未因源码证明恒定，无RDP强峰。

- 三轮samework纯NN formal已完成：pooledGPU mean−0.090802/p99−0.130112ms，wall−0.100691/−0.098590（4efbcf46），每轮均值/尾与正确性过，未加刷轮。它是固定1088/currentmath/SP161的队列组织收益，不和APPpulse/H相加、不直接扣独立Mgap，不GoalComplete。
- 下一actualAPP graph prototype仅CPU待源（aad9ce62应用scope）：1152 menuHDR原codec/D3D-HIP交接，MP1AE0historyoffseed0/PDL0/pulse0/NET0，两mode同条件；只capture neural Enqueue，pre/post/外sync在外，Frame/map/pool/模块和graph owner全持，captured后不PrepareFrame/eager以免变内部指针。先changedHDR同独立原Framegold，实际APP速度/生产/长期回放未验，源落地后独立审，不空转旧门/不GPU。

- actualAPP1152初prototype源审曾PASS但暖池前提未由源验建立（graph0-app-source-review）：原Frame/codec不改，Bridge生产者wait→仅neuralLabHelper→signal/consumerwait，初realframe eagerseed0warm/capture/initialreplay返回前完成调用，无互等环（overlap0）。捕后Prepare拒/onlyGraphLaunch、mappedptr固定、Frame/Bridge/Net各自owner保持，closegraph在Release进口buffers之前。现在只授权paired12step seq2 HDRF16 gold/NET0同条件，源审不等actualAPP gold或速度/生产接受；首帧setup/日志不可当性能。

- APP70977/bae176b7真实前置失败保：eager独立Frame12seq2全finite；replay第一capture Newguard拒新alloc，无capturedgraph/instantiate/replay/pairedraw。原Frame未调用Runtime-only seed1 Prepare，我此前推调用路径错误已显式撤回；SP.initial160不是NNkernel数，实际eager每frame Fn161。不认数学/API失败或图不支持，不假已经温热。
- math唯一CPU暖池修e351892d源审PASS：真实hipMallocRaw calls/success+upload/Fn/planmiss与所有cache容器/rollover连续2次无变化+同Fn，≤80 seed0 Enq/Sync后局部outTensor已退栈才cap；未插Runtime.prepare，New/Upload/Fn/Plan捕获guard保，动态nodes来自真实eager Fn，actualW/H/span/maps/loadedfile/style记录。只是源门，尚待真实warm计数/随后capture12gold，不新增其他实验/生产。

- APP稳定暖池/12HDR gold19487已实际PASS（85abc115）：原1296×720F16 menu源，actualNN valid1920×1080/proc1920×1152，mappedspans35,389,440/26,542,080B/noalias；warm4=(malloc209→213→213→213,pool23→27→27→27)连续末2稳定才cap；actual161kernel/160DAG、graph12、逐stepF16bit0finite和mappedptr稳定。70977失败仍保，未Runtime.prepare；StyleFeature−1是stockStyle1“不覆盖”标记，非负style。此前CPUpending已由此true门闭，但没有APP性能/生产接受。
- APP性能e05173fe actual新源独立审PASS：原benchmark.cpp byte同Frame.ProcessSubmittedFrame+原Drain wall，两mode同原codec/interop/NET0/PDL0/pulse0；160total弃80=80steady，每slot仅首末2D2H在计时外。两mode首Frame共同boundedstablewarm、候选capture+DAG/instantiate单列，ready无getenv/Fnvector/lastFnstore/打印、新timinggetter；原postSignalQuery/外wait+signal+drain保，不写零API。GraphAPI160/ordinary0终态核，all160wallrows finite/>0数据门后才能mean/p99。实际singleABBA37548已exit0/release（535eab68），见下项；不套pure3reads或跨批扣Mgap/生产长期声明。

- APP actual首性能37548/535eab68硬件PASS：原Frame wall eager9.678319→graph9.603994ms，mean−0.074325/p99−0.07518；两B均快，两Amean drift+0.011963/p99 drift−0.07324保。640计时行finite/>0、8首末F16文件对原gold bit0finite，actualGraphAPI0/160/160/0、candidate161kernel/160DAG/steadyrecord0。首cold1.27–1.46s保但不混80steady。只有singlefirstscreen、两arm pulse0/PDL0/NET0，不NaturalGameFPS/NETGPU/生产。
- 必要R2/R3 samefreeze已各轮合并avg/p99/正确性PASS（f7a48855），无R4；个别slot尾负不被合并门抹掉（见下）。productionpulseAuto协作条件由remaining仅CPU审中，暂无部署；pure4ef三轮收益与APPwall本项不相加，独立Mgap未刷新不能扣减。

- APP三轮正式合并门f7a48855实际完成（37548/65206/52791）：pooled wall mean−0.070848/p99−0.09415ms，24首末F16rawsamefinite、candidateGraph160与161DAG过。**R2/R3 B2自身p99退步必须保**：R2B2p99=10.304040/max11.745ms，R3B2p99=10.022260；pooled candidate max11.745>control10.182。不是allslot尾改善、无未来不卡顿保证/生产接受，原cold/drift/top5/max全保。纯NN4ef收益不相加。
- 新productbaseline小12HDR gold源审PASS（graph0-product-baseline-review/e45afd81）：sameFrame/codec/HDR/PDL0NET0，EagerAuto实际枚举2对ReplayOff0，错role/ReplayAuto拒。Auto逐ordinary检查真实driver/caps/CreateOk1/active/accepted+1和record_ok；Off create/recordcalls0。普通Enq计数排capturehost记录，warm计数动态不假Record12；新PRED1请求MP1实际inactive，新exe/profile非旧PRED0freeze。CPU后验gold成本不自动当perf公平，后perf需静态hot路径源审；GPU/性能/部署未新增。

- productbaseline12HDR actual60494/ee3919ba已PASS：Auto2 driver_validated1/caps1/Create1，ordinary15=warm4+12−1、RecordOk/accepted/site15、reject0/PDLcalls0，cleanupDestroyDrain1；ReplayOff普通warm4+Graph12且Create/Record0/161DAG。两独立Frame12stepF16bit0finite，是真auto对offgraph资源/输出门而非性能。旧gold每NN后验检查不对称，未用其wall判快慢。
- 下一quietproductperf待actual新源审：原O2benchmark/同HDR/NET0PDL0 EagerAuto2对ReplayOff0，只移gold热后验检查；nativePulseScope/真实EventRecord逻辑不改，setup与末CPUcounters核RecordAccepted=warm+frames−1、no newHIPquery/逐帧打印。源未到不复旧gold，不承诺APP新baseline收益/生产；sharedamend禁，纯/APP/marker收益不相加。

- quietProduct actual新源已落且独立性能公平审PASS（quiet-performance-review）：原benchmark.cpp byte同gold；nativePulse Configure/eligibility/RejectMask/marker片段逐字同，不删生产scope/Record。热gold后验移setup/final，ordinary只Enq+累计、两role选定Frame累计，无新增HIPquery/打印/环境读取诊断；原nativePulse自身guard照留。setup真AutoCreate/driver/caps/warmRecords，末Auto N=actualwarm+frames−1普通/record/accepted相等且Graph0，Off资源0/ordinarywarm/Graphframes；错计数非0拒批。原Frame160弃80/2edge读取外/NET0/PDL0/PRED1inactive/codec/postsignalquery/drain/lifetime同域，实际新性能数据待单ABBA，不加旧收益/no生产。

- directproduct实际53360/6afdb3ba首筛STOP：Auto2 Create1/ordinary与RecordAccepted163，对Off Graph160/资源0；8F16rawsamefinite、时间有效。steady mean ReplayOff比Auto **+0.006975ms**、p99−0.0451ms；control mean漂移+0.013213ms，不能证明普遍变慢，也不满足替换收益门。不formal/重刷/defaultGraph，不借旧off/off三轮或相加收益。
- 下一仅CPU ReplayAuto最小机制准备：官方EventRecord7/GetEvent接口已见；需162节点(161kernel+1真实lease事件)的dtype/handle/强DAG位置门，graph/exec先销后leaseClose、每帧host eligibility及固定key复核。capture时host Record累计不等GPU重放次数；实际DLL导出/节点支持和raw尚未验证，仍无生产变化。


- 2026-10-07 PR12当前3ef0b6e本地合并并补consumer MP1 startup/hotguard、RGB首次状态回归、MV有效区failclosed；FastHistory新flag默认0，近似策略与reference独立，开启需重启/明确guide/匹配row+exports。源码scope/最终runtime与MSVC WARP/9070接口suite1392、受影响补检14793通过；未真实游戏/性能/fullNNaux/gfx1200门，不改0.41/041a包与安装。具体证据results/pr12-integration-20261007；ReplayAuto暂停，原mochi差距目标未完成。

- 2026-10-07 用户授权AMD低频实验及两游戏试装：统一TEMPORAL_MODE0/1/2默认0，2为独立SAOG算法编码域适配；小数学20385和Runtime实际API68326通过，mode0保旧字节；未知MV静态颜色门控不假补偿，初版同步/成本观感未知。22724已备份部署Mode2/MP1/PRED0、Addon7383/两runtime0f3c，78模块/SUMS不变；正式包/tag不动，等用户游戏反馈，不goalcomplete。证据low-frequency-temporal-20261007/lowfreq-runtime-20261007。
