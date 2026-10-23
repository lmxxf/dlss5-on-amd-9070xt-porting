# B隔离实现与运行入口：C512 FFN→projection单边TCHAIN

生产未改；只这条边、16个C512块。数学/权重/数据布局未改，A及phase2两负候选未混入。设计见[design.md](design.md)，独立同步审计见[source-review.md](source-review.md)。GPU结果由root续写。

## 最终产物

工作根 `/home/lmxxf/work/dlss5-gap-phase3-20261023/B/`：

- `frame.exe`：初轮O2 host，SHA `110b83d51c5fef4bd842d907225ea3687d834f3c651ea6cae94bf2188f5c88d4`，已触发cold pool保护失败，保留不覆盖。
- **`frame-warmed.exe`**：修准备期池稳定后的O2 host，SHA `0e914317e4de5a3898cee28a77cbb115d422e2575d2d52b1020c4dfad25b9ff6`；后续用新case复验，GPU模块不变。
- `c512-m32-deep.generated.hip`、`deep_fast-packed-fast.generated.hip`：主测trace0。
- `c512-m32-deep.trace.generated.hip`、`deep_fast-packed-fast.trace.generated.hip`：独立诊断trace1。
- 四份源码在[generated/](generated/)归档。**实际二进制校准：producer与consumer均用空`RTC_EXTRA_OPTS`。**此前按recipe字段要求producer max-ilp的建议已撤回：fresh max-ilp改变89个旧函数及多项资源；empty候选则保持生产91个旧函数代码/资源全同。recipe/manifest中的opts字段不能替代机器码核验。max-ilp产物隔离留账未上GPU，见`isa-review.md`。
- `source/`为完整隔离副本；[candidate.patch](candidate.patch)、[prepare.py](prepare.py)、[host-fragment.inc](host-fragment.inc)、[build-host.sh](build-host.sh)可复现。

本地LLVM21 trace0/trace1四模块均语法通过。正式COMGR需root持锁编译后核旧函数代码/资源保持，再上机；本地LLVM不是COMGR基线替代。

## 实验开关

|变量|作用|
|---|---|
|`NR_C512_CHAIN=0`|原producer/consumer；不额外retain、不进入链化|
|`NR_C512_CHAIN=1`|16边新producer发布、consumer等待；只此consumer AnyOrder|
|`NR_C512_CHAIN_ORDERED=1`|新协议的普通ordered控制；默认0才AnyOrder|
|`NR_C512_CHAIN_WAIT=1..16384`|poll次数上限，默认16384；不是固定毫秒|
|`NR_C512_CHAIN_FORCE_TIMEOUT=0..15`|该slot给consumer多等8贡献，使目标不可达；验证零写和整批拒绝，建议WAIT1|
|`NR_C512_CHAIN_START=4294967288`|初始counter/target=2^32−8，验证首轮跨32bit边界|
|`NR_C512_CHAIN_START=18446744073709551608`|64bit最后8倍数，下一次+8前永久停链回原路；不reset|
|`NR_C512_CHAIN_TRACE_DIR=<独立目录>`|构造时预分配两trace buffer；final drain后写`chain-trace.json`；必须搭配trace1模块才有marker|

默认无NR变量时没有chain初始化；显式0/1两臂同host同candidate模块，可共享冷counter分配，但NR0维持原pool成本。baseline是否与生产等价须由root实际Gold/ISA核。

完整Frame每帧最终原Flush之后只在NR1调用`DiagnosticNetwork().ReleaseChainFrame()`，且在elapsed截止前计入候选；final `CheckChainStatus()`在计时外显式drain、读sticky并逐项核16×tiles counter==该slot target。最终日志包含requested/enabled/calls/anyorder/ordered/overflow/status/counter_values/counter_mismatches。任何status或mismatch非0使进程失败，整批不用于性能。

## 两kernel ABI（稳定）

Producer：`split_ffn_one_w2f8_w16_bin_chain(in, weights, contract8, tokens, ready_u64, producer_trace_u64)`，64线程，`ceil(tokens/32)*8`组。

Consumer：`split_projection_frag_w16_bskip_chain(contract8, weights, skip_bytes_as_float_ptr, out_f32, out8, tokens, ready_u64, target_u64, status_u32, wait_limit_u32, consumer_trace_u64)`，32线程，`tokens/2`组。

每边ready是`tokens/16`个uint64；每个tile8group累计ACQ_REL贡献。producer两个wave均过两次WG barrier；无效尾wave不发布。consumer leader acquire、readfirstlane广播、全lane acquire成功后才读输入；失败在任何input/skip/weight读取前零写自有完整tile并置sticky。

只读新符号预热发生在构造。counter/status构造预分配和上传一次；无每帧memset。每slot独立target单调+8，超界永久停链。所有新函数存在才允许链化；合法缺导出回原函数/原布局，计数应0。dup/profile等会破坏8贡献的配置已拒绝。

## 实际重叠判据

trace1 producer每有效wave在完成输出stores并release之后记录pre-publication时钟，再做atomic贡献；consumer完成第一条真实WMMA，将结果volatile store并release后读取message131 GPU-global realtime。仅“进入核/开始等待”不算重叠。

文件按16边保存最后一帧：producer数组长度`ceil(tokens/32)*8*2`，consumer数组`tokens/2`。1152的2160token有8个无效producer尾wave，marker应0，其余必须非0。ready逐项等target。

[analyze-trace.py](analyze-trace.py)核所有marker/尾部/ready，并统计每边`consumer_first_mma_complete < max(producer_prepublish)`的个数。ordered控制必须0，AnyOrder有正数才证明本轮真实计算重叠；不按猜测频率换算毫秒。trace0才做正式ABBA，诊断marker成本不混性能。

## 预热与失败边界

cold `PrepareForExternalInput`每次预热Enqueue+Synchronize后释放hold；否则暖身引用可能使第一实际帧在external wait之后扩大pool。另加candidate pool miss guard，在外部wait之后拒绝同步分配。

零输出是安全结束失败实验，不是正确网络fallback；任何sticky都拒整批。强制timeout、32carry/64极限、缺export、2160尾wave须单独验。root的独立无producer/poison+canary timeout-zero probe不在该host中隐藏执行。

源码准备最初两次host编译暴露DiagnosticNetwork只在BRIDGE_ISOLATE宏下；已用`DLSS5_C512_CHAIN_LAB`仅开放getter，**未打开会自动追加其它GPU工作量的BRIDGE_ISOLATE benchmark分支**。早期失败不当数值/性能证据。

## 首轮准备期失败与host修复（非性能刷轮）

root首轮GPU记录：独立timeout-zero probe通过，baseline1152 Gold通过；candidate退出1，停在 `chain pool was not prepared before external wait`，没有进入性能验收。一轮cold Enqueue不足以使新增长hold下的best-fit pool稳定；该保护门按设计阻止了外部producer wait之后同步分配。

修复仅改host：NR1在external wait之前，同scratch/profile执行有界暖身；每个真实Enqueue后Sync→Release。对完整旧warm cycle（默认AE0为一次，可选AE normal/off为两次）比较pool.size、总capacity bytes和weight_uploads，连续两cycle均不增才标记prepared。总真实Enqueue上限8；不稳定则throw，不移除afterwait分配保护。NR0保持原暖身。

日志记录`warm runs/cycle/profile/pool/bytes/uploads/stable/enabled/overflow`及`warm_ready`。counter/targets/status/trace没有reset，预热推进的epochs全部计入最终检查。near64bit极限的ordered退路如实记录enabled0/overflow1，不冒充chain预热成功。

新`New()`失败信息增加请求bytes、pool大小/bytes、held数量和max_free，便于继续定位；没有在失败后自动重试GPU或放开同步分配。

旧host和源码身份：[initial-host-identity.json](initial-host-identity.json)、`initial-candidate.patch`及工作目录`initial-host-snapshot/`。修复差异：[warmfix.patch](warmfix.patch)、[warm-fragment.inc](warm-fragment.inc)、[warmfix-identity.json](warmfix-identity.json)、[build-host-warmed.sh](build-host-warmed.sh)。五份生成源码（含baseline）逐SHA不变，四个GPU模块没有重编。本轮只CPU构建，新的Gold/实际warm_runs由root运行。

## GPU结题：真实重叠成立，完整Frame净负 STOP

采用**已复现生产的empty-opts producer**，不是max-ilp组；consumer空opts。新host/mode0和mode1共用candidate主测模块，原91/89函数代码/资源保持，trace0全模块无clock。候选特有的hold/既有FrameFlush后释放、同步协议全部计入；NR0没有人为追加hold。

### 有效性与安全门

- 独立无producer的16token超时probe：两输出payload全0，输入/ready/trace poison与前后canary保持；程序与CPU独立raw回读都PASS。
- 首个full候选被afterwait缺池保护拒绝，未计性能。保旧host/失败目录；只修NR1准备期最多8次warm、池容量/上传数连续2cycle稳定，依旧禁止wait后malloc。1152实际4次cold warm；counter/status/trace不清，不隐藏错误。NR0仍原暖身。
- 三档×三图样×双arm×3帧54帧Gold同。另三档ordered/AnyOrder trace各3帧18帧同，carry/limit两例6帧同，实际旧模块缺导出两例6帧同：主Gold/诊断共84帧（另smoke2六帧通过，旧失败单列）。
- 32位进位case最后ready/target=4294967344逐项同；64位极限case只执行准备期16条链后永久关闭，overflow1/status0/输出Gold同，未执行溢出加法。
- 强制slot0等待未来target且WAIT1：安全完成，最后status1/counter_mismatches0、进程exit1，整批拒绝；不是正确回退，不是有效性能数据。

### 有用计算重叠（独立trace构建）

三档ordered控制所有16边overlap0；AnyOrder三档全部16边有consumer首条真实WMMA完成早于最后producer pre-publication：900共6641、1088共13091、1152共14795个consumer marker。marker经实际WMMA结果volatile store、release/wait后读取gfx12全局clock131；不是consumer入核或自旋起点。只比较时间先后，不假设tick频率。trace资源与执行有扰动，**这些数量不当主测帧的精确重叠份额**。

### 完整Frame 3轮ABBA/档

240帧/slot弃80，每arm960稳态样本；所有槽status0/counter_mismatches0、全部16边实际命中，calls按记录的cold warm+240帧计算，非硬编码只算240帧。首尾Gold同。

|档位|baseline ms|chain ms|差ms|pooled p99差ms|
|---|---:|---:|---:|---:|
|900|6.447297|6.533707|**+0.086410**|+0.097634|
|1088|8.850874|8.975739|**+0.124866**|+0.115762|
|1152|9.140692|9.193507|**+0.052815**|+0.065640|

**九轮均值与p99全部退步，STOP，不进生产、不扫等待预算/协议变体。** 真实重叠不足以抵消整套候选成本；未做因果消融，不把惩罚单独归给atomic/fence/hold或某个cache。也不将这条具体协议否定全部TCHAIN。候选pool在1152预热稳定为57项/369091448B，这只是池容量，不是VRAM峰值或已锁定基线差。

原CSV、命令、warm/counter回执、故障原始日志、traceJSON和独立回读在evidence/；statistics.json与各trace-summary.json可复算。实际producer编译选项由baseline整HSACO身份裁决，早先误认可选max-ilp字段为生产实际选项的过程留在compile-recipe-correction.md，错误组从未上GPU。GPU/RTC均持锁守门，最终释放。当前生产仍仅第二阶段刀1。
