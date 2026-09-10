# Development/tools — 测试台 A/B 与现场探针（光之朱雀日常用的那几件，从 scratchpad 收进仓库）

在仓库根目录跑（`release/<name>/` 相对根目录）。远端 `amd9070` = RX 9070 XT 的 Windows 机，实验目录 `D:\DLSSNR-Lab\<name>`。

- `ab.sh <name> <runner.ps1> [files...]`：robocopy 克隆 split-direct 到 `D:\DLSSNR-Lab\<name>`，scp 文件，跑 runner，取回日志和输出，算 PSNR。
- `abn.sh <rounds> <A> <runnerA> <B> <runnerB>`：两目录交替跑 N 轮，打印每段 min-of-means。
- `cmpmin.py <A> <B>`：`release/<A>/rounds/*.log` 与 B 的每段所有帧最小值对比（比 abn 的输出更稳）。
- `bench-norebuild.ps1`：scripts/bench.ps1 去掉全部 dxc 编译的版本（只设 env + 跑 exe），25 秒一轮，做跳块扫描/显存统计用。要与已编好 cso 的目录配套。
- `sweep.sh`：`DLSS5_SKIP_BLOCKS` 逐块跳过扫描，写 sweep.log。
- `gpu.ps1 / vram.ps1 / shared.ps1 / benchvram.ps1`：远端现场探针——各进程 GPU 引擎占用、显存、被挤到系统内存的量（Shared Usage）、测试台进程显存峰值。掉帧时先跑这三个。
matrix-probe\matrix_probe.exe + D3D12\D3D12Core.dll rebuilt 09-10 from Development/d3d12_shader_model_probe.cpp (-DDLSS5_AGILITY_PROBE -DDLSS5_AGILITY_VERSION=721) after the cleanup deleted them
- `dump.sh <name> <runner> <DUMP_BLOCK4 code> <file> [extra env]`：跑一次（3 帧）取一份中间量 dump（配合 `DLSS5_TEST_ISOLATE=<stage>&& set DLSS5_TEST_ISOLATE_REPEAT=1` 抓某一段的 scratch）。
- `isolate.sh <name> x <isolate list> [repeat]`：不重编（目录里要有 bench-norebuild.ps1），打印各段隔离 ms。
- `make-norebuild.sh`：从 scripts/bench.ps1 重新生成 bench-norebuild.ps1（去掉全部 dxc 行）；bench.ps1 改了就重跑一次。
