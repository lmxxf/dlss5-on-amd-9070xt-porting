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

## 指令级视角（09-10 起）

- `rgp-capture.ps1`（放在 `D:\DLSSNR-Lab\`）：无界面 RGP 抓取。测试台没有 swapchain，`DLSS5_TEST_PRESENT=1` 让它开一个 64×64 的 composition swapchain、每帧 Present 一次并在结束前多停 30 秒（trace 传输）。**必须在交互桌面会话里跑**（SSH 会话建不了 swapchain）：`schtasks /create /tn dlss5rgp /tr "powershell -ExecutionPolicy Bypass -File D:\DLSSNR-Lab\rgp-capture.ps1 -Folder D:\DLSSNR-Lab\<链> -Frame 8 -Out D:\DLSSNR-Lab\logs\x.rgp" /sc once /st 00:00 /it /f /rl highest && schtasks /run /tn dlss5rgp`。RGP 抓取时驱动把时钟锁到峰值——同一测试台 27.4ms（平时 36），别把抓取时的数字当基线。
- `isa-stats.py <x.rgp> <workdir>`：从 .rgp 里切出全部 pipeline 的 PAL ELF，用 RDTS 的 `rga.exe -s bin` 反汇编成 gfx1201 ISA，打印每个核的 scratch（溢出）/VGPR/LDS/指令数/wmma/LDS 访问/访存/s_wait/转换指令数，并按代码里的常量给核打标签（cso 的 DXBC hash 是占位符，对不回名字）。`rga.exe -s dx12` 走不通（不认 SM6.10 的 DXIL，也没法带 Agility 721）。
- `Development/pso_blob_dump.cpp`：`GetCachedBlob()` 只有 954 字节的钥匙，驱动缓存 `%LOCALAPPDATA%\AMD\DxcCache\*.parc` 是自有压缩格式——两条都走不通，留作记录。
- 看 .rgp 的时间线要 RGP 图形界面（`RadeonGPUProfiler.exe`）：事件耗时、每条指令的延迟（开 `-Instr` 抓）。
