# 3zwr1 AMDNR 0.3.3.2（c32w）剑星实机对照（2026-09-26）

AMDNR 是 OptiScaler 分支，内置我们的 RE9 runtime（`LmxxfNrRuntime.dll`，MIT 署名 Kien）+ 加密 pak（CNG 派生密钥、表认证、强制保留其署名声明；魔数 `LMXXFPK1`）。0.3.3.2 新增 "c32w" 单 wave C32 核（其自报 1080p 网络 17.8→15.3ms，基线是其所用的我们旧 RE9 runtime 核）。c32w 六个导出名与我们 prod8 C32 入口一一对应（可直接顶替），非今晨 c32-wave1 的命名；核在加密 pak 内，未解密比对。

切换 `swap3z.ps1`（我们的 dxgi/OptiScaler.ini/ReShade64.dll/dlss5-amd.addon64 改名 *.ours，装其 OptiScaler.dll→dxgi.dll + OptiScaler\ + runtime dll/pak，ini `NrBackend=lmxxf`；已切回并核对 dxgi fbfb6676）。

日志：`net=1920x1080 color_job=1705x960 c32w=on hist=on` —— 其网络固定跑 1080 档，2K 质量输入原样放进 1080 网络。无 ViT 复用。

| 设置（剑星） | 3z AMDNR 0.3.3.2 | 我们（0.31 + C32 vec，同日） |
|---|---|---|
| 2K 质量，主菜单 / 简单场景 | 40～41 / 45 | 57 / 60（上限；900 档） |
| 1080P 原生 AA，主菜单 / 简单场景 | 42 / 45～46 | 简单场景晃动（复用失效）49～50，静止 50～51 |

结论：同为 1080 档网络、不复用，我们约快 4 帧（~9%）。2K 质量差距更大来自其固定 1080 档。

`nr_bench.cpp`：直接调 runtime C API 的计时程序（可适配其 v1 ABI：API 160B、帧结构 88B、assets 传 .pak 路径），我们的 RE9 runtime 1920×1080 完整一帧 16.4～16.5ms；其 runtime 在 EnqueueHip 访问违例，未继续逆向（改用游戏内对照）。
