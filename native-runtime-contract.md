# 原生移植的当前边界

2026-09-06：完整 AMD RGB512→blocks0～70→RGB 已与原版逐值一致。
真实移位校准后证据：`release/native-runtime-rgb512/amd/final-validation.json`（786432值，different0）。旧配置结果另存。
这是受控输入、mask1/mode1、当前给定移位配置的实验，不是游戏验收。

## 进入1080p前必须解决

已发现并直接捕获原配置差异：`native-runtime-parameters.json`来自新版只读探针的CPU参数blob，已消歧40～47并确认48～69移位。AMD全链已改用`native_runtime_shifts.h`并通过新配置512回归；这不替代1080p几何与其他输入合同验证。

- **输入尺寸不能混淆。** 原`live-preblock-v2/preblock-live-0.bin`的0xf0/0xf4是H/W=2176/3840（`run_original_preblock_oracle.cpp`明确按H/W打包）。1920×1088是半尺寸候选，尚未新抓取1080p原运行确认。旧geometry表的960×544对应下采样分支，不能直接当新preblock的RGB输入尺寸。
- **不是只解除assert。** ViT QKV/attention/linear及31～38八block全GPU串联已验证64/256-token；256-token证据为`release/native-vit/chain256-3003`，三次最终输出与原始整链一致。真实非正方形尺寸仍需要原kernel的有效token、padding/mask与物理地址验证。不能把512推理再拉伸当1080p数值移植。
- **部分窗口及跳接。** 当前decoder tail写死512夹具尺寸。需要逐阶段正确处理非4/8倍数边缘、池化/head重排及跳接；不能沿用旧proxy表就声称已证实。
- **原游戏参数。** 当前移位序列、preblock控制单色纹理输入和post mask1/mode1并未覆盖原游戏全部输入。需从原运行确认移位、纹理与标量；独立原kernel参数化测试不能证明原调度器使用了同样参数。
- **输出头数值范围。** 整数HMMA头对scaled accumulator非整数或越界显式NaN，避免静默近似。应在真实特征上验证/完善该范围，不能在游戏里吞掉NaN改回底图。
- **实际部署验收。** 新代码目前仅在实验目录；游戏DLL/公开包仍为旧版本。需在退出游戏后备份、部署新DLL，并在真实游戏场景用同帧输入/输出及开关对照证明最终神经输出提交到画面。DLL加载、进程运行、角落FPS都不足以验收。

完整测试启动会重新编译shader，不能将数分钟初始化当每帧耗时；当前每帧fence等待约2.4秒也不是游戏FPS。先保证正确，再做速度优化，不跳帧伪造性能。
