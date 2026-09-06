# 原生移植的当前边界

2026-09-06：完整 AMD RGB512→blocks0～70→RGB 已与原版逐值一致。
真实移位校准后证据：`release/native-runtime-rgb512/amd/final-validation.json`（786432值，different0）。旧配置结果另存。
这是受控输入、mask1/mode1、当前给定移位配置的实验，不是游戏验收。

## 进入1080p前必须解决

已发现并直接捕获原配置差异：`native-runtime-parameters.json`来自新版只读探针的CPU参数blob，已消歧40～47并确认48～69移位。AMD全链已改用`native_runtime_shifts.h`并通过新配置512回归；这不替代1080p几何与其他输入合同验证。

- **输入尺寸不能混淆。** 新PID25972捕获见`native-runtime-parameters-1080.json`：用户设置1080p桌面/无边框窗口后，ViT实际HW=20/32（640 tokens），post HW=1152/1920。旧1920×1088、30×18猜测不能作为实现依据；post尺寸也不能直接等同屏幕可见区域或preblock纹理范围，仍须逐层核对。
- **不是只解除assert。** ViT31～38八block已在640-token随机输入、chunk级独立fence提交下三帧逐值通过（`release/native-vit/chain640-3006`）。普通整段提交有已记录TDR，不能直接用于游戏。32×20 repack正反映射已验证，仍缺完整图像链及动态输入验证。不能把512推理再拉伸当1080p数值移植。
- **部分窗口及跳接。** 当前decoder tail写死512夹具尺寸。需要逐阶段正确处理非4/8倍数边缘、池化/head重排及跳接；不能沿用旧proxy表就声称已证实。
  已完成的独立GPU证据：60×36→32×20 pool/head（有效30×18、补齐零）、decoder39左上裁剪到60×36、decoder40～47实际尺寸串联，以及block48输出120×72。它们的输入来自独立fixture，不能替代encoder到decoder的实际跳接验证；更大尺寸56～70仍未打通。
- **原游戏参数。** 当前移位序列、preblock控制单色纹理输入和post mask1/mode1并未覆盖原游戏全部输入。需从原运行确认移位、纹理与标量；独立原kernel参数化测试不能证明原调度器使用了同样参数。
  新标量审计`preblock-scalar-profile-comparison.json`确认有效纹理尺寸字段0x90/94及0xd0/d4为1920×1080，0xf0/f4处理范围为1920×1152；当前受控preblock caller会将前者也覆盖为处理尺寸。必须把有效纹理范围与网络补齐范围分开，受控1152高RGB测试不能作为真实游戏边缘采样证据。
- **输出头数值范围。** 整数HMMA头对scaled accumulator非整数或越界显式NaN，避免静默近似。应在真实特征上验证/完善该范围，不能在游戏里吞掉NaN改回底图。
- **实际部署验收。** 新代码目前仅在实验目录；游戏DLL/公开包仍为旧版本。需在退出游戏后备份、部署新DLL，并在真实游戏场景用同帧输入/输出及开关对照证明最终神经输出提交到画面。DLL加载、进程运行、角落FPS都不足以验收。

完整测试启动会重新编译shader，不能将数分钟初始化当每帧耗时；当前每帧fence等待约2.4秒也不是游戏FPS。先保证正确，再做速度优化，不跳帧伪造性能。
