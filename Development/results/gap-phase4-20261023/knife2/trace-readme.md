# Small四profile独立三帧trace（CPU交付，尚未GPU执行）

复用phase2已用过的launch wrapper：在真实`hipModuleLaunchKernel`/AnyOrder调用边界，以实际function handle反查`functions`注册表，能看到compact paired handle最终导出名。**没有SetObserver、dump/profile、GPU计时或BRIDGE_ISOLATE**；记录会影响CPU发射节奏，只作路径/数值诊断，不用于性能。

## 产物/调用

工作根 `/home/lmxxf/work/dlss5-gap-phase4-20261023/knife2/`：

- `trace-baseline.exe` SHA `93ac8fd925c21b138737f0e0f3dc5493c714e8abf7e3746e5228b8023a50107f`
- `trace-candidate.exe` SHA `112d090d47cf4f8585dc3945e902c4838124de8bf41d3f27e1e9e52366bc64d0`

接口：`trace-*.exe ASSETS MODULES FLAGS PROCESSING_RGBA32F OUTDIR PROFILE [BASELINE_OUTDIR]`。

PROFILE仅接受`small900`、`small720`、`smallfree1088`、`small1080`。固定3帧；候选附同profile的BASELINE_OUTDIR时，会读取baseline三份`frame-N.rgb32f`逐float bit比较，非0直接失败，保留已写raw。每臂自身三帧也须finite/repeat bit0。Small缺资产/不支持导致effective1x直接拒绝，不把fallback当Small结果。

4个输入在`trace-inputs/`，完整大小/SHA见[make-trace-inputs.json](make-trace-inputs.json)。输入为已编码RGBA32F受控梯度（RGB亦精确binary16），在各profile的valid宽高外按原镜像公式填充；非真实捕获。options明确`small_geometry=true`、`integration.allow_small_model=true`，post_shift3与生产Frame一致，AE0/historyoff/FAST1/seed0、实际31块。

## 实际输出字段

`SMALL_TRACE_PROFILE`逐帧包含实际W/H、requested/effective/small_active、31blocks、Deep尺寸、实际ViT宽高/token、compact与W16/bstream/bout指针资格、EntryExit与C512 byte资格，以及active weights count/容量、pool count/容量。

`SMALL_TRACE_DISPATCH`逐派发真实module:callee，包括compact contract/projection实际函数、attention448选640或288选400、gather、Up39具体`_w_bin[_f8w16]`等。

`SMALL_TRACE_COUNT`给总派发数、vit_pack_input次数、F32输出Up39 byte-input前缀命中数；完整列表用来区分1152可能使用的byteout路径。任何UNMAPPED handle会失败。

V-fragment同时记录累计量与**每帧增量**，Small必须看delta而不能把别的模型预热累计值误判为Small使用。本工具直接构造Small Network，未经过Bridge双模型预热；仍采用snapshot/delta避免口径混淆。weights/pool为CPU对象的分配容量，不是驱动驻留峰值。

实际Small token数在每帧开始强校为448/288/640；skip列表必须为空。裸Network尺寸与此前实际Frame几何CPU证据配对，free profile指合法1920×1072→processing1088，不是把普通Small1088请求当实跑。

复现：[prepare-trace.py](prepare-trace.py)、[trace-fragment.inc](trace-fragment.inc)、[trace.cpp](trace.cpp)、[build-trace.sh](build-trace.sh)、[trace-source-identity.json](trace-source-identity.json)、[trace-build-identity.json](trace-build-identity.json)。四份measurement host逐SHA保持，trace为独立产物；生产源码、80模块和目标机未改。
