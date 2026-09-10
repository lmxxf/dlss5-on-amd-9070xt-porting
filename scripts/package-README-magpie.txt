DLSS5-AMD 0.09 · Magpie 版
============================

把 DLSS 5 的神经网络（DLSSNR）跑在 AMD RX 9070 XT（RDNA4）上，以 Magpie 窗口缩放器为载体：
任何能开 1920x1080 无边框窗口的游戏都能用，不需要游戏自己支持 FSR 或 DLSS。
Magpie 抓取游戏窗口 -> Magpie 的 FSR3 效果（含 AMD 光流估运动向量）-> 本插件截下 FSR3 的
ffxDispatch 调用，换成 DLSS 5 网络 -> Magpie 显示。

本包内容
--------
  dxgi.dll                       ReShade 6.8 加载器（原版，未修改；放在 Magpie.exe 旁边就会被加载）
  dlss5-amd.addon64              本移植的 DLL（.addon64 是 ReShade 的扩展名，不要改名）
  DLSS5-D3D12-721\               微软 DirectX 12 Agility SDK 1.721 预览运行时，Shader Model 6.10 需要它
  DLSS5-AMD\                     权重、编译好的着色器、运行参数（必须和 dlss5-amd.addon64 在同一目录）
  SHA256SUMS.txt                 文件校验

需要
----
  1. RX 9070 / 9070 XT（RDNA4）+ AMD 26.10.07.02 预览驱动（正式驱动没有 Shader Model 6.10 的 wave matrix）。
  2. Magpie 的实验分支（带 FSR3/FSR4 效果的那个）：https://github.com/SAOG0721/Magpie 的 Release，
     解压出 Magpie.exe 所在目录。本包不含 Magpie。
  3. 显示器分辨率不限，但游戏窗口必须是 1920x1080 无边框，Magpie 缩放选"原始尺寸"（不放大）。
     网络只认 1920x1080 进、1920x1080 出。

安装
----
  1. 把本包的四项（dxgi.dll、dlss5-amd.addon64、DLSS5-D3D12-721、DLSS5-AMD）复制进 Magpie.exe 所在目录。
     如果那里已有 dxgi.dll（别的 ReShade/mod），先备份。
  2. 启动 Magpie 一次再退出，编辑 %LOCALAPPDATA%\Magpie\config\v4\config.json：
     "duplicateFrameDetectionMode": 1  改成  "duplicateFrameDetectionMode": 0
     （重复帧检测开着时 Magpie 会跳过没变化的帧，插件就收不到帧。）
  3. 再启动 Magpie：
     - 效果组：新建一组，只加 FSR3 -> FSR3_SR 一个效果，参数 Optical Flow Method 选 AMDOF。
     - 缩放模式：缩放选"原始尺寸"。
     - 游戏：显示模式无边框窗口，1920x1080。
  4. 在游戏里按 Magpie 的缩放热键激活。前 20~30 秒是网络初始化（读 600MB 权重、建 179 个着色器），
     这段时间画面是 Magpie 自己的 FSR3；初始化完成后帧率会掉到 30 左右，那就是 DLSS 5 接管了。
     再按一次热键停止缩放就是对比。

已知
----
  - 每帧约 33~36 ms（网络 24 ms + Magpie 捕获/呈现），30 fps 上下；游戏本身可以照常 60 fps。
  - 输入是显示用的 8 位 sRGB 图（不是游戏内钩子那种线性 HDR 场景色），画面会比游戏内版本更白、更"处理感"。
  - 运动向量来自 Magpie 的光流估计，快速运动会有块状闪烁。
  - 日志：DLSS5-AMD\logs\native-game-oneshot.txt（初始化）、native-submission-order.txt（每帧观察）。
  - F6 是本插件的开关键（全局）；如果同一台机器上游戏里也装了本插件的游戏版，两边会一起切。

卸载
----
  删除 Magpie 目录里的 dxgi.dll、dlss5-amd.addon64、DLSS5-D3D12-721、DLSS5-AMD 四项。

来源
----
  https://github.com/lmxxf/dlss5-on-amd-9070xt-porting（源码、每个 tag 的改动、开发记录）
