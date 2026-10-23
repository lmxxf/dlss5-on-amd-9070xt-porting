# 刀1独立源审：ViT compact/W16/byte 资格扩到1088与900

2026-10-23。只读审计，未执行GPU/RTC、未改生产代码。本次只审刀1，后续刀不在本报告范围。阅读源码基线 `c24e3a97cdf596d0c997cee7deda91c981d520ce`；实际采纳/数值/性能以主进程验证回执为准。

## 裁决

**结构上可继续验证。** Contract/projection compact权重地址与空间尺寸无关，400token满足16-token tile约束；现有算子没有要求640token。最小改动应只把两处 `H==1152` 资格改为显式列举 `(1920,1152)/(1920,1088)/(1600,960)`，保留原设备、模块、数学、选项和冷启动条件。不要借此启用720、free、Small900/448或gfx1200。

但不能单凭源码声明400token逐位等价：现有W16 projection与非W16 projection的residual累加位置不同（下文），且热MP模式不是每帧重新判断资格。必须按下面的矩阵确认实际导出、原输出和回环。

## 1. 400token的地址/几何检查

位置均相对仓库根。

- `Development/HIP/hip_reference_network.h:1039–1042`：Original900处理区1600×960，deep50×30，head25×15补成25×16，因此400token；不是Small900的1600×1024/448token。
- `Down(...head=true)`（约830行）在缺额token写零；ViT的Gather按16-token组内置换，400是16倍数，最后组不跨界。`Gather()`（约883行）创建400×1024索引。
- `hip/vit_stream.inc:22–66`：projection模板TM1/TN4，`first=bid/16*16`、`row=bid%16*64`；400组覆盖25个token tile与16个通道tile。最后first384，最多访问token399，完整16行，无部分tile问题。
- `hip/deep_fast.hip:763–795`：contract每组16token×64通道；`first=local/16*16`、`col=local%16*64`；同样400组覆盖完整400×1024输出。
- `vit_stream_qkv_frag_bin_w5f8`（`vit_stream.inc:222–262`）每5wave共享80token权重。400/16=25，恰好5组，无额外inactivewave；此路径原来900已使用，本刀不改其kernel。
- `vit_attention_fused_400_bytein_bout`已有生产400token分支；本刀不换attention数学或token数量。

容量/读写范围：

| 物体 | 400token大小 | 边界 |
|---|---:|---|
| hidden E4M3，400×4096 | 1,638,400B | contract只访问token0..399、K0..4095 |
| contract/projection byte边，400×1024 | 409,600B | 分配器用`New(n*256)`，其单位为float字节容量 |
| 同形F32回退边 | 1,638,400B | byte能力不足时保原分配与pack |
| contract compact权重 | 4,198,400B | FP8矩阵[0,4,194,304)，1024个F32尾随其后 |
| projection compact权重 | 1,052,672B | FP8矩阵[0,1,048,576)，1024个F32尾随其后 |

权重数量与tail地址不随token数量变化。`CompactContractWeight` / `CompactProjectionWeight`（约442–467行）只接已知block31..38名字和固定形状，保完整长度检查；W16只是矩阵字节排列，尾部复制保持原F32位。

## 2. 必须保留的成对路由

`InitCompactContract` / `InitCompactProjection`（约475–504行）先取得cached function，再让loader生成与其匹配的compact布局；`Run()`约656行用weight pointer集合选对应函数。不能把布局判断改为“文件名带compact”或“环境请求W16”。

现有必保条件：

- 真gfx1201且设备名9070 XT（`CompactVitTailDeviceEligible`）；gfx1200仍旧路。
- `!opt.integration.select_module`。自定义模块选择器可能返回不同数学或布局，不能偷偷给它W16/compact数据。
- FAST1、冷启动MP1、nongraph、非experimental temporal、非sparse。
- `vit_contract_byte_edge`及各自fragment/blocked/n64/fused/split-K资格。
- 只有对应新export存在才打包；不存在就选择对应旧布局+旧函数。
- byte跨块必须同时有compact contract bstream和projection bout；少一个就保F32跨块+原pack。

`HasFn`会缓存缺失，`Fn`使用已缓存句柄；不能为这次扩资格在帧内增加module load或同步上传。

## 3. 缺单export的具体回退测试

应使用**合法可加载模块**缺一个指定可选导出，日志要证明module load成功。损坏ELF/删整个必需模块不算fallback测试。

| 被移除/屏蔽的导出 | 源码预期 | 必看证据 |
|---|---|---|
| `vit_stream_contract_frag_bout_compactw_w16` | 若非W16 compact基函数在，则选非W16 compact及其匹配bstream；否则contract旧布局 | contract_w16实际0；指针与callee匹配；输出Gold |
| `vit_stream_contract_frag_bstream_bout_compactw_w16` | 保W16 compact contract普通输入函数，interbyte/entry-exit byte全关 | 不得调用不存在的bstream；pack恢复；projection float输出 |
| `vit_stream_project_n64_bb_compactw_w16` | 选非W16 compact projection或旧projection | projection_w16实际0；不可沿用W16排列指针 |
| `vit_stream_project_n64_bb_bout_compactw_w16` | 保W16 compact projection F32输出，interbyte/entry-exit byte全关 | 不得生成byte输出给F32消费者 |
| contract所有compact导出 | contract旧权重/旧函数；projection可独立compact；字节跨块关 | 不需整套compact全禁，但两套pointer合同各自正确 |
| projection所有compact导出 | projection旧权重/旧函数；contract可独立compact；字节跨块关 | 同上 |
| 原`vit_stream_contract_frag_bout`（原byte-edge基础导出） | `vit_contract_byte_edge=false`，两compact入口拒绝 | 走既有half-stream回退，不假称新compact已验 |

最低建议GPU覆盖“缺contract bstream”和“缺projection bout”两种正好能漏出byte格式错配的情况；其余用真实helper的CPU路由fixture或按风险补小GPU门。全模块缺失是构造错误，不承诺恢复。

### 预存legacy-cache边界

两个loader显式“已有legacy key就返回legacy pointer”，pointer没有compact tag，因此普通F32路径能正确落旧函数。但**interbyte的准入目前只看函数句柄，不看当次返回pointer是否legacy**。仓库没有非compact的 `vit_stream_contract_frag_bstream_bout` 导出；如果人为预种旧key，同时保留compact bstream/bout句柄，则可能将legacy pointer送到字节分支后寻找不存在的旧bstream函数。

正常构造中资格、模块和layout开关固定，没发现公开热setter会在同一个model map里先旧layout再开启compact；Small热切换保留两套map，也不是释放后重用，因此不能称这是本刀已证生产bug。但如果沿用“任意legacy cache都安全回退”的承诺，应补**实际helper＋真实Run分支**的预种fixture；不应只跑loader返回旧pointer的旧CPU测试便称跨块也已覆盖。若fixture触发，最小处理是当前模型存在legacy对应key时关闭interbyte，而非让旧pointer走新tail地址。

## 4. 数学边界：必须实际测900 Gold

Contract W16/非W16的共同次序是：residual初始化total，四个K1024 partition分别从零累计，逐partition加进total；W16将相邻K16装进一个16B包，实际K顺序仍一致。

Projection需要更谨慎：`hip/vit_stream.inc:30–62` W16分支把skip×scale先放total，再从零计算第一partition后加进total；非W16分支把skip×scale作为第一partition的accumulator起始值，随后逐K16 MMA。两者在浮点数中并非全域代数可互换。已有640实测Gold同；这次没有改该kernel，但400的不同输入不能靠640历史Gold自动覆盖。

因此验证措辞应为“400token完整输入/序列对当前Gold逐位同”，不要写成“源审证明对所有输入等价”。若900出现差异，先定位projection第一partition/skip位置，不把它误判成compact尾地址错。

## 5. 热切换与Small：最容易漏验的地方

### 5.1 MP资格只在构造时检查一次

`InitCompact*`在构造约940–942行调用；`SetPassCount`（约1262行）只改pass和资源，**不会清除compact函数句柄或重选布局**。

因此现行语义为：

- 冷MP2/3会拒compact。
- 冷MP1后热切MP2/3，已取得的compact可继续使用；这个行为在既有1152就存在。
- 冷MP2后热切回MP1，仍保持旧路径，不会凭热切自动获得compact。

本刀不能只测冷MP2输出正常就宣称“MP2一定fallback”。最低要测MP1→2→1完整输出回环、以及冷MP2→1旧路；模式首次切换都必须在producer wait之前完成预热和双feed准备。

### 5.2 Small模型与同名key

`SetModelMode`（约1234–1237行）交换`weights`与`inactive_model_weights`，同时换AE gain/history head。compact pointer集合不随map交换清空：两模型allocation都常驻，这个union-tag设计正常可辨两套指针，不应该为本刀随意清空它。

- Small与Original有同名block31..38角色，但不同权重内容；map分离是必要条件。
- 1152 Small已可能获得compact（构造MP实际1且原资格没有`!small_active`），不能为修1088/900顺手全禁Small导致现有行为改变。
- Small正式900处理1600×1024、448token，应继续旧compact资格；增加Original1600×960不能误变为`W==1600`。
- 若独立harness设置`allow_small_model + small_geometry`，Small在1600×960会是52×32 deep、28×16=448token；新资格只按W/H会连该非默认组合也放进来。若要明确仅Original扩资格，需保1152既有Small行为、对新增尺寸加Original条件；但冷Original→Small热切仍有旧句柄，必须统一设计或以实际测试覆盖，不能只在构造加`!small_active`解决热路径。

建议本刀最小产品scope：真实宿主Original900/1088与既有1152；Small900保持1024处理区旧路；必须至少复验1152的0.5→1→2→1→0.5，以及真实宿主900模型/尺寸切换重建路径。

### 5.3 external producer wait

`D3D12Bridge::Enqueue`在入等待前调用`PrepareForExternalInput`（bridge约307行）。后者按mode/temporal/AE建立prepared-profile集合，先Synchronize、冷上传、Enqueue warm，再记录profile；不能把新compact第一次调用拖到wait后。

最有价值回归沿用10-18已建立的受控未完成producer gate：预热后切模型/MP，返回时producer仍未完成；后续执行完成输出和冷启动相同。只跑普通同步路径看不到懒上传死锁。

## 6. 建议验收矩阵与完成标准

| 类别 | 用例 | 通过条件 |
|---|---|---|
| 900主数值 | Original1600×960/400，FAST1/MP1，至少3帧；静态＋动/seed变输入 | 当前旧路径逐位Gold、finite/repeat；实际compact W16+byte callee；无新增错误 |
| 1088主数值 | Original1920×1088/640，同上 | 沿用第一阶段raw裁判并核实际导出；不能只核请求flag |
| 已支持回归 | 1920×1152/640默认 | 旧0900 Gold保持，资格与载荷未退化 |
| 性能 | 900与1088各3轮4-slot ABBA，同输入/模块/driver | mean/p99原数据、漂移与反例保留；完整frame另量，不用pureNN代APP |
| FAST0 | 新两尺寸FAST0冷实例 | 无新增compact pointer/callee，输出与旧FAST0一致 |
| MP冷/热 | 冷2→1；冷1→2→1（需时再3） | 输出各对自己的冷基线；双feed ready；不混称每种都fallback |
| 模型/尺寸 | 1152 0.5→1→2→1→0.5；900真实Small1024↔Original960 | 各模型回环逐位，requested/effective、geometry、head/blend正确，旧Small资产无交叉 |
| AE/F8 | 900 AE开→EXACT→AE；固定idle reset间隔 | adaptive_active时byte跨块关，compact F32仍可用；决策CSV/输出对照匹配 |
| 可选export | 缺contract-bstream；缺projection-bout；缺W16-base的非W16回退 | 模块load成功、whole interbyte关或正确成对降档、Gold |
| selector | 自定义select_module指回stock | compact资格拒且旧weight/callee，不能只检查环境变量 |
| 失败守门 | graph、sparse、experimental、其它几何 | 保旧qualification；不用每项都大GPU跑，真实helper分支/CPUfixture可验证拒绝 |
| producer gate | 首次模型/MP预热后，未完成external producer | 等待前不发生新weight upload/moduleLoad；完成后Gold，无死锁 |

除了新尺寸主数值/性能、缺单export、热切与producer-gate，不建议无目的扩大测试。第一阶段已经验证过的1088数据可复用其锁定产物；如果最终生产helper和该产物不同，则补最终默认构建的Gold与callee身份即可，不必重刷全部性能轮。

## 7. 交付前应确认的最小事实

1. 最终仅两处geometry gate（或一个公共helper）有意变化，原flags和fallback条件保持。
2. 两个新尺寸的完整Graph/Frame真实compact调用计数与导出名，不用`VitW16Mode=7`冒实际启用。
3. 900所有输出对自己的Gold，1088对第一阶段153ac；不互拿1152的0900。
4. 仅资源容量报告来自实际weights统计；理论120MiB不冒实际驻留峰值。
5. 最终不部署游戏、不改正式ZIP，除非主进程收到本轮具体授权。
