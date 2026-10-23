# 刀2 public Frame兼容计划（CPU源审/编排，尚未GPU）

复用ISA代理四份host，不改其源码/二进制：`frame-{baseline,candidate}-{standard,free1072}.exe`。标准输入1296×720；free1072编译BENCH_WIDTH1920/BENCH_HEIGHT1072，必须用实际16,465,920B输入，不能把标准输入改flag冒充。host接口完全沿用benchmark_live_capture。

## 最小覆盖

|case|几何|每arm帧|设置/接受条件|
|---|---|---:|---|
|public切模式+direct输出|900、720、free1072|10|0.5,0.5,1,1,2,2,3,3,0.5,0.5；逐帧跨arm同，0/1/8/9 Small回环同|
|Small AE1|900、720、free1072|6|始终31块Small，idle钉86400000ms，输出/AE decision逐帧配对|
|Small MODE1|free1072|6|temporal参数1、reset_every3；冷/暖/暖/重置/暖/暖，必须实际use_history1|

共14jobs/108逐帧检查；只有正确性，不用其wall_ms作性能判断。switch和AE1三几何覆盖新增Small布局；MODE1用已有free1072全viewport做最小真历史门，不额外造两套host。

## 为什么是public重建，不是同Network硬切

fixture调用`dlss5_mode::Request`，随后进入`NativeGameFrame::ProcessSubmittedFrame`。该公开入口先Resolve实际几何：processing/valid尺寸变化时Flush→销毁resources→CreateUnlocked→再SyncModelMode；不是fixture直接调用低层Network::SetModelMode。

|选择|Small实际processing|Original实际processing|public往返|
|---|---|---|---|
|900 tier|1600×1024，448token|1600×960，400token|切1和回0.5各重建一次|
|720 tier|1344×768，288token|1280×768，240token|宽度变化，同样两次重建|
|free输入1920×1072|1920×1088，640token|1920×1152，640token|高度变化，同样两次重建|

常规Small1080即使ROWS1088也仍processing1152，故第三档必须是真free1072。

设置`DLSS5_DIRECT_IO=3`，`SMALL_FRAME_DIRECT_TEST=1`、`SMALL_FRAME_CAPTURE=1`。fixture会在Process之后重新取DirectOutput、检查当前输出尺寸、实际copy/readback，且验证SuppressFps保留。不要用output_same指针地址判重建：释放后地址可能复用。实际核三次Network构造几何/31→71→31日志，或本case的frame_create_step rebuild事件；switch的DIRECT mode序列必须完整。

`HOT_RELOAD=0`、`MULTI_PASS_HOTKEY=0`、`VIT_REUSE_HOTKEY=0`避免文件轮询覆盖fixture Request；这是测试公开mode请求入口，不声称模拟了真实键盘按键。MP预测/skin/skip关闭，保证2/3是明确旧网次数。

## 必须修正的通用runner判据

当前run-jobs.ps1在设置frame_count时硬要求每帧changed_half_values_from_first==0，且compare_to把所有输出都比单一SHA。这两项**不能用于模式切换或MODE1**。

最终root runner已提供窄开关：`allow_frame_changes=true`仅跳过changed_first==0，继续保留frame_count、checked、finite；`copy_exe=true`将核SHA后的exe放case/frame.exe。最终可执行清单是`compat-jobs.json`（由`make-compatibility-jobs.py`复用gold-corrected清单生成），**保留frame_count，不设gold/compare_to**。`compat-analyze.py --evidence DIR --output JSON`再按out-frame-i逐帧配对，核switch回环、mode/direct/geometry、AE及MODE1。全部后验门过才算兼容通过；早期spec中绕开frame_count的建议不再使用。

`make-compatibility-spec.py`仅生成设计JSON，不执行目标机/GPU；--assets必填，应指向同时有Original、small/、noise、history和shader的组合根。它输出flag_overrides，须合并到已验证生产flags，不能仅用这少量键导致丢掉原算子profile。原Small权重在small-model路径94文件不等于有完整双模型/codec资产。

## MODE1与AE的真实有效性

standard1296×720和tier900/720长宽比不同，会letterbox；NativeGameFrame要求MODE1 full viewport，因此不能拿该standard host强测MODE1，FIT_INPUT0也不改变Make的几何。free1072是完整viewport，不存在这问题。

MODE1必须CLI temporal=1，否则fixture reset=!temporal会每帧重置，得到假历史测试。reset_every3用于6帧实际cold/warm/reset；检查`history model=Small blend=0.76708984375`和history日志至少两段use_history1。没有depth/有效metadata时是明确static fallback，只验证这一已支持模式，不伪称有效MV重投影。

MODE1存在250ms wall idle reset；若逐帧落盘造成额外reset，应如实核日志，不能拿已全cold的结果当warm门。AE则可用已有DLSS5_VIT_ADAPTIVE_IDLE_MS=86400000钉住idle，并将ADAPTIVE_LOG写到case固定绝对路径；日志只正确性测试使用，不混性能。Small AE gain日志应明确逻辑11..18/资产31..38；AE1下字节跨块资格拒绝属于预期，不能以AE0路径计数代替。

NativeLabRoot按**exe所在目录**寻找DLSS5-AMD，不按WorkingDirectory；现runner只在case cwd创建DLSS5-AMD，故history/rebuild日志可能落D:\DLSSNR-Lab全局。root应收该case对应新增日志段，或把同SHA exe放到case旁再建立local config；不能读取旧global日志充本次证据。stderr的model/processing与DIRECT序列可辅助证明，MODE1 use_history必须拿到本次记录。

## 强制门与限制

每个Small-only case明确requested0.5/effective0.5/blocks31，出现missing-small-assets/回落1x即失败；switch至少两次真实进入Small。Small阶段VFRAG仍应0（本刀未放开Small V布局）；Original中段可按刀1资格启用，不把其计数归到Small。保留完整帧输出/hash、模式/几何/AE/history记录；无新GPU模块、无下一刀。

## 最终日志/资产隔离

复制exe后case/DLSS5-AMD/native-game-flags.txt使NativeLabRoot明确选本case，重建与history日志不再借用全局历史。collector需额外收`.txt`，尤其`DLSS5-AMD/logs/native-game-oneshot.txt`、`native-hip.txt`；MODE1日志在`DLSS5-AMD/fast-history-mode1.log`。

无需copy_assets：benchmark的ASSETS实参传到Frame.Create和recreate_directory，codec shader明确从该目录读，noise/weight/history亦同；模块有显式路径。SHOW_FPS0与SuppressFps令overlay不触发lab-root shader路径，原生RGBA16也不走format-convert fallback。manifest明确copy_assets=false，不把源码目录或权重复制到每个case。

注意全局VFRAG析构计数可能含构造时Original预热的一遍（8层），不能把它直接归为Small实际启用；实际Small模型/geometry及后续输出按本计划单独核。

## 本次MODE1实际序列校准

实际6帧两臂均[0,0,1,0,1,1]，首帧864.3142/830.7558ms触发固定250ms idle，第二帧额外冷重置。原理想固定序列断言说明已保留；分析器仅在首帧>250ms且显式reset/冷图像/双方序列/逐帧SHA全部吻合时允许这一额外首冷。frames2/4/5真实暖历史仍必需，未新增GPU轮次。见compatibility-results-review.md。
