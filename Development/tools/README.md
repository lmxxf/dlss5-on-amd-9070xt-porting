# Development/tools — 测试台 A/B 与现场探针（光之朱雀日常用的那几件，从 scratchpad 收进仓库）

在仓库根目录跑（`release/<name>/` 相对根目录）。远端 `amd9070` = RX 9070 XT 的 Windows 机，实验目录 `D:\DLSSNR-Lab\<name>`。

- `ab.sh <name> <runner.ps1> [files...]`：robocopy 克隆 split-direct 到 `D:\DLSSNR-Lab\<name>`，scp 文件，跑 runner，取回日志和输出，算 PSNR。
- `abn.sh <rounds> <A> <runnerA> <B> <runnerB>`：两目录交替跑 N 轮，打印每段 min-of-means。
- `cmpmin.py <A> <B>`：`release/<A>/rounds/*.log` 与 B 的每段所有帧最小值对比（比 abn 的输出更稳）。
- `bench-norebuild.ps1`：scripts/bench.ps1 去掉全部 dxc 编译的版本（只设 env + 跑 exe），25 秒一轮，做跳块扫描/显存统计用。要与已编好 cso 的目录配套。
- `sweep.sh`：`DLSS5_SKIP_BLOCKS` 逐块跳过扫描，写 sweep.log。
- `gpu.ps1 / vram.ps1 / shared.ps1 / benchvram.ps1`：远端现场探针——各进程 GPU 引擎占用、显存、被挤到系统内存的量（Shared Usage）、测试台进程显存峰值。掉帧时先跑这三个。
