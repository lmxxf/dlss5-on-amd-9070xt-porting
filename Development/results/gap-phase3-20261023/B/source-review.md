# 第三阶段B同步/生命周期独立审计

2026-10-23。只读CPU审计，不GPU/RTC、不改生产。本轮只验证一个C512 FFN→projection边，A已STOP，不混入A、刀2或刀3负候选。

## 最小可接受方案（已发实现者，等待实际patch复核）

初步建议pair先筛；root随后澄清用户要求16块完整Frame三轮，因此**最终任务范围为先pair协议/重叠/失败验证，再完整16块Frame验证，不以pair性能负替代整网结题**。不先建通用TCHAIN框架。完整Frame采用下文受限失败输出方案，最终有任何sticky error都拒整批。

### producer计数与尾wave

`c512_ffn_one_w2f8_w16_body`是64线程/2wave，`first=(bid/8)*32+wave*16`、group=`bid%8`。每个wave只写该16token tile的64通道；一个tile需8个group全部完成才可被projection读取。

原body在`if(first<tokens)`里面做运算，但没有全WG早退。最小稳妥wrapper应让全64线程完成body后统一：WG barrier → agent release fence → WG barrier，然后两个wave leader分别对有效tile的ready做ACQ_REL加一。invalid尾wave必须参加barrier但不发布、不越界读写ready。

只在lane0发release不足以覆盖其它lane未完成的store；裸`s_barrier`也不替代内存fence。发布点要在ISA验证全部out8 stores完成、agent release之后；atomic计数串接八个producer的release序列，不能使用普通store ready。

### consumer等待、广播与失败

projection为32线程/一wave，每WG处理16token×64输出通道。入口在读取contract8之前，由leader用agent acquire轮询对应tile计数，完成条件为`ready>=8*epoch`。leader结果通过真实wave广播提供统一分支，所有lane在成功后执行agent acquire fence再读取输入。

等待必须有界并检查失败状态；不允许一部分lane返回而其余lane进入后续barrier。

**接受的完整Frame实验失败方案**：超时经wave广播形成统一分支，在任何contract8/skip/weight读取之前，将本WG负责的全部F32输出及tiled out8写零、原子设置sticky error，再整wave return。后继普通ordered kernel必须等待这些零写完成。这样下游读取的是已定义值，最后`CheckChainStatus()`在计时外drain、读sticky后拒绝整个批次。error>0的Gold或性能均无效。此零输出是为了安全完成失败实验，**不是正确回退、不是生产支持**；因此无需增加16次ordered recovery。

producer与consumer的输出区互异，失败consumer零写不应覆盖仍在写的contract8。16块ready必须独立槽或有完整block身份，不能共用一个计数把前块8次贡献误当后块就绪。

### epoch、重置与生命周期

- ready和epoch使用64位计数；epoch0保留禁用/未初始化含义。预先拒绝`epoch>UINT64_MAX/8`，不要溢出后复用旧ready。
- 最小pair实验可要求每pair完成后drain、无帧间并发；ready初始化一次，epoch单调递增。若逐pair必须reset，其成本计入candidate窗口且严格在producer之前，不假装免费。
- 所有buffer/weight/module/函数句柄在测试开始或external producer wait之前准备。等待已经入流后不得hipMalloc、同步upload或初始化counter。
- input、contract8、skip、output、ready/status/trace及模块owner保留到两kernel完成并drain，不能因host局部Tensor reset被pool复用。完整Frame需使用有界保留策略：既防同帧提前回收，也不能240帧持续累积owner；旧帧释放前须有实际completion证明。
- 仅该projection使用局部AnyOrder launch；不能让全局`pdl_anyorder`状态泄漏给后续attention/memset。若修改共享状态，异常也必须恢复。
- 最终root决定：NR0保持真实生产，不人为增加retain；NR1的16槽contract8/compact保留与FrameFlush后clear成本计入候选。两臂可共用新host/模块及冷counter初始化，但不能给control加工作隐藏候选代价。收益报告为完整候选组合，不能只归因链化原子或重叠。此前“对照同retain”的建议撤回。
- 失败也先drain，再释放owner；不使用kill运行游戏或重启试验来掩盖失败。

### AnyOrder与重叠的证据

实际调用必须是已解析的`hipExtModuleLaunchKernel`并传`hipExtAnyOrderLaunch=1`，记录返回码和实际callee；缺API/不支持可退ordered作正确性对照，但不能宣称测到了AnyOrder收益。[HIP执行控制API](https://rocmdocs.amd.com/projects/HIP/en/develop/reference/hip_runtime_api/modules/execution_control.html)

requested1、API成功、consumer入口时间早，都不能单独证明有用计算重叠。诊断应使用GPU全局realtime：consumer第一条MMA结果先真正store并release，再取时间，与最后producer发布前时间比较；ordered版本应无此交叉。trace构建与trace-off性能构建分开，性能版不带时间戳。

共享内存同步和全局可见性分别核对；HIP的同步函数包含对应fence，但本项目使用裸builtin/自定义宏，不能仅凭`barrier`名字套官方高级API保证。[HIP 7.0.2语言扩展](https://rocm.docs.amd.com/projects/HIP/en/docs-7.0.2/how-to/hip_cpp_language_extensions.html)

## 需要保留的旧负账

10-12 `vit-tile-ready`证明了真实MMA重叠，但公平pair约70.043→95.770µs，STOP。它说明协议可工作不等于值得用。本轮换到当前C512 FFN→projection是用户明确授权的另一条边；不能把旧失败撤回，也不能拿其重叠证据冒本轮新核实测。

## 当前状态

以上是可接受最小设计，不是已经通过的实现审计。实际patch到达后核producer所有lane可见性、八贡献者计数、实际C512三档pad尾、状态失败路径、64位epoch及owner。


## CPU映射检查

`mapping-review.json`已枚举实际producer/consumer网格（无GPU）：

| C512 padded tokens | Producer64-thread WG | Consumer32-thread WG | ready槽 | 无效producer wave |
|---:|---:|---:|---:|---:|
|1504（900）|376|752|94|0|
|2048（1088）|512|1024|128|0|
|2160（1152）|544|1080|135|8|

每ready槽恰有8个group贡献、8个projection输出WG消费。**1152是必须覆盖的invalid-wave尾例**：最后8个producer WG的wave1 first=2160无效，必须过WG barrier但不能访问ready[135]。64位最后允许epoch为2305843009213693951，对应target18446744073709551608，下一epoch应拒绝而非wrap。

## design.md设计稿复核

已读实现者`design.md`。其中producer全WG发布、64位累计target、consumer16384次有界acquire与成功后全lane fence符合上述最小方案；`s_sleep(1)`不等于固定等待毫秒数。target每次加8，在下一次加法前以`target>UINT64_MAX-8`拒绝并永久退ordered，可避免wrap；最后可达target是UINT64_MAX-7。

该稿“失败不写输出”仅适用于独立pair，不能直接用于用户要求的full16块；root已确定full使用零写+sticky拒批，不需要另加审批。等待最终patch核实这个差别。


## full Frame owner方案复核

接受16个C512槽持有contract8与compact/skip至当前Frame既有Flush成功之后。`ReleaseChainFrame()`放在该完成边界之后且计时内clear，末尾`CheckChainStatus()`计时外drain读sticky。NR0保生产pool行为。

只看到后继ordinary kernel已经入队，不等于它已经完成；此时host提前清owner仍可能令池重用。用stream命令顺序证明更小hold理论上可行，但还要覆盖AnyOrder、异步memset与timeout零写的alias边界，本轮不建议增加这种复杂性。固定16槽是有界生命周期，不能改成逐帧无限push；旧slot覆盖也必须在上帧完成之后。

Flush失败/异常时不得先clear再drain。应先恢复owner device、确认HIP stream完成，失败则保留owner并拒绝实例；“失败路径强行free”不可接受。约35MiB只是预估逻辑持有量，实际pool/owned容量差需要回执，不能当物理驻留峰值。

## 实际实现源审（CPU）：通过，可进入pair安全门

已逐项读取隔离目录 `/home/lmxxf/work/dlss5-gap-phase3-20261023/B/source` 的生成源码、`candidate.patch`、`host-fragment.inc`和`source-identity.json`。下面裁决限当前实验配置：Original/FAST1/MP1/full71、三指定shape、正常单次派发、无graph/sparse/experimental；未冒游戏或长期产品支持。

### producer

新`split_ffn_one_w2f8_w16_bin_chain`调用原`<false,true>` body后，所有64lane到达barrier→agent release→barrier。原body的invalid尾wave只跳计算，没有函数早退，因而不会漏barrier。只有每wave lane0且first<tokens才对自己tile的uint64 ready做ACQ_REL原子加1；8个group按同一ready地址形成RMW发布链。

trace时间取在release完成、atomic发布之前，只valid wave写自己的位置。真实GPU还应看最终ISA stores/wait/fence先于signal；源码正确不代编译器核验。

### consumer与失败输出

新consumer只用bid/tokens确定tile/row；在读任何contract8/skip/weight之前由lane0轮询agent acquire。16384次上界、sleep1保持有界；readfirstlane在leader分支重汇合后广播状态。成功全lane agent acquire再调用原projection body，数学与布局不改。

失败分支直接按原索引写本WG完整F32和out8零tile，原子OR sticky后return，不读producer数据。每wave只有一条一致分支，没有部分lane跨barrier；零写区域不与producer的contract8重叠。普通有序后继保证它读取的是已完成写入的定义值。整批最后status非0拒绝，不能保留这批性能作有效样本。

### host epoch/AnyOrder/owner

- ready为16个slot×tiles×8B，slot严格映射23..30与40..47；每slot的uint64 target独立加8。构造时一次alloc/init，帧内不reset。
- 最大值前关闭整个实例chain并退旧路，不wrap、不悄悄把counter清零；发生中途关闭时各slot保各自最后target，不把它们强行视为同一epoch。
- 新资格已显式拒`pdl_anyorder`，确保producer走普通ordered；consumer仅在精确新函数名分支调用独立`chain_ext_launch(...flags=1)`，不改全局pdl状态。
- 新旧argument次序、producer64thread/consumer32thread及groups规则保持；新增ready/target/status/wait/trace在各自新ABI中配对。
- `chain_keep[16][2]`只在实际候选边保存compact与contract8；同帧直到benchmark现有submit.Flush后才clear。NR0的Release早return，不给baseline增加retain。
- 最后`CheckChainStatus`已经补齐owner hipSetDevice→显式hipStreamSynchronize→status D2H→拒绝；不借默认stream拷贝语义假设完成。
- 析构已有owner选择/stream drain后才clear keep/ready/status；构造异常会先sync再清已分配counter。

### 专用getter宏

已核三层header只通过`DLSS5_C512_CHAIN_LAB`额外暴露DiagnosticNetwork getter。build-host使用该宏，**没有开启`DLSS5_BENCH_BRIDGE_ISOLATE`**；benchmark后面原有的大段isolate GPU实验仍不编入。

DiagnosticNetwork本身会调用resources.submit.Flush；默认同步模式该Flush只有mutex/状态检查，不新增GPU工作。最终已改为benchmark仅在`NR_C512_CHAIN=1`时调用该getter/Release；NR0计时路径不再调用它。candidate承担getter/Flush/clear全部成本，末尾双方校验在计时外。

### 仍需实际证据

1. pair ordered旧/ordered协议/AnyOrder协议两种输出逐位同，计数正好target，status0；2160尾和多epoch都覆盖。
2. forced timeout验证完整零写+sticky、canary；failure批必须被host拒绝。
3. trace诊断证明consumer第一条真实MMA结果完成早于最后producer发布；API成功或入口早不够。
4. trace-off full16块actual calls/AnyOrder返回码、Gold、三档三轮ABBA；不以pair负替full任务。
5. 计时包含候选新增同步/原子/owner hold+clear；最后status检查计时外。不得逐帧往owner vector追加导致显存增长。

另已提醒实现者拒绝`opt.dup_prefix`等重复派发诊断：重复producer会让计数提前达到target，而不是8个不同group齐。当前正式Frame配置dup空不触发，但准入明确拒绝优于依赖隐含配置。该检查不应演变成通用调度平台。


## 首次GPU门后的冷准备修复审查（进行中）

root回报：独立timeout-zero已PASS、baseline完整Gold通过；candidate首真实帧被after-external-wait的pool-miss保护拒绝，没有性能结果。保留该失败，不能以它作为链化慢或正常无效输出的数据。

最终测试流程不再额外造pair：完整Frame短Gold + timeout独立probe + ordered/AnyOrder trace，然后完整16块正式ABBA。前文pair建议作为设计历史保留，不替用户要求的full结题。

新增保护已看到：`New()`在candidate且external_input_wait时若pool无可复用buffer就throw，绝不在等待未提交producer时调用hipMalloc。第一次warm即使Sync+Release也可能因best-fit池布局尚未稳定而不够。

可接受的准备期修复：NR1在同一scratch/profile上最多8轮，每轮Enqueue→Synchronize→ReleaseChainFrame，再比较pool.size与weight_upload计数；连续2轮都不增长才登记prepared profile。NR0保原一次（原AE需要的额外warm不改变）。这只是分配池准备，不清ready/target/status/trace，不重置epoch、不抹force-failure；新增所有预热发生external wait之前，原afterwait拒malloc保护必须保留。

即使观察到两轮稳定也不是任意未来输入/配置绝不分配的数学证明；该实验固定shape/profile，afterwait guard继续兜住任何漏项。8轮未稳必须throw，不移除guard继续跑。新产物另名`frame-warmed.exe`，保旧frame与首失败日志。


## frame-warmed最终准备期源审：PASS（本轮AE0固定实验）

已核实际`warm-fragment.inc`与生成header，SHA绑定在`warm-source-review.json`：

- NR1最多**总8次Enqueue**，可选AE额外状态共享同一总预算；每个cycle包含原profile Enqueue与必要AEoff Enqueue，恢复原AE后才比较；每次同一scratch/hist/out，Synchronize成功后ReleaseChainFrame。
- 每轮比较进入前/完成后pool.size、各allocation capacity总和、weight_upload计数，三者完全不变才stable++，任一变动清stable；连续2轮才返回。8次未稳throw，不登记prepared。
- pool_bytes遍历用const引用，不意外增加tensor use_count或改变best-fit资格。
- ready、16个target、sticky status、trace、force-slot均未重新初始化；warm执行的producer发布正常计入epoch。近上界逻辑照常关闭chain、记录overflow，不得将关闭后的成功Gold当chain性能。
- NR0明确仍原一次Enqueue/Synchronize，原AE额外一次保留，没有候选保留/清理/多轮工作灌给baseline。
- afterwait pool-miss拒绝仍在，新增错误详情为所需bytes、pool数量/容量、held数和最大free块；它不会为“跑通”而进入hipMalloc。
- prepared profile只在成功稳定后写入；异常保持原恢复AE状态/完成清理路径。warm只清普通实验history状态，不清链协议错误。

最终又在交付前收紧为完整profile cycle比较，消除了先稳AE1再稳AEoff的分段固定点问题：每cycle执行原状态与必要AEoff并恢复后才计stable，总Enqueue仍≤8；NR0旧分支逐字保持。当前GPU实验仍AE0，这项AE分组只有源码核对，不冒AE实机验收。保留afterwait guard意味着任何漏准备仍安全拒绝。

最终基准每帧仅NR1调用DiagnosticNetwork/Release；NR0计时内不调用getter。末尾Check双方在计时外显式owner stream drain、读sticky及全部16×tiles ready，每项必须等于对应slot target；任一错误拒整批。trace仅最后实际成功帧可用于重叠分析，首真实帧失败时的warm残留时间戳不能冒成实帧trace。

旧frame、旧header和首pool保护失败日志保留，新名`frame-warmed.exe`。未获得本轮GPU速度结论，审计者未执行GPU/RTC。

最终完整cycle版再次复核通过；`warm-source-review.json`已刷新到实际最终header/fragment SHA，未执行GPU/RTC。
