生产仅合入 VFRAG 资格函数及其注释，整头逐字等于已测候选 `8ae0e238…ed28`。C512 pair 模块清单 `c8e6d3fa…c237` 保持不变。本次未改 GPU 源码或模块。

Small 四档和 Original 三档 AE 路径现允许；FAST1、单 pass、graph/observer 等排除门、三 export 成对预热、默认与显式关闭优先级均沿用。Small 1088 仍是 free 1920×1072 输入得到的处理尺寸；普通 1080 输入为 1152。AE 数学、anchor、reuse gate 和默认 500ms idle 无改动。

采纳依据是 root 已完成的 72 Small Gold 与 156 AE 帧及四档三轮 ABBA。720 的平均收益仅 4.663µs，且第二轮退 2.378µs；12 轮 p99 全改善，不能说各轮 mean 全胜。最终默认/off 验证由 root 执行，本审计不新增目标机活动。
