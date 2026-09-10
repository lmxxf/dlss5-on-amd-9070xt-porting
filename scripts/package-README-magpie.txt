DLSS5-AMD 0.09 · Magpie 版
============================

把 DLSS 5 的神经网络（DLSSNR）跑在 AMD RX 9070 XT（RDNA4）上，以 Magpie 窗口缩放器为载体：
任何能开 1920x1080 无边框窗口的游戏都能用，不需要游戏自己支持 FSR 或 DLSS。
Magpie 抓取游戏窗口 -> Magpie 的 FSR3 效果（含 AMD 光流估运动向量）-> 本插件截下 FSR3 的
ffxDispatch 调用，换成 DLSS 5 网络 -> Magpie 显示。

本包内容
--------
  Magpie.exe 及其文件            Magpie 实验分支 0.6.6（SAOG0721/Magpie，GPL-3，许可见 LICENSE-Magpie.txt；A 卡用不到的 NVIDIA 运行库已去掉）
  config\config.json             Magpie 便携模式配置（预设好的效果组和选项）
  dxgi.dll                       ReShade 6.8 加载器（原版，未修改；放在 Magpie.exe 旁边就会被加载）
  dlss5-amd.addon64              本移植的 DLL（.addon64 是 ReShade 的扩展名，不要改名）
  DLSS5-D3D12-721\               微软 DirectX 12 Agility SDK 1.721 预览运行时，Shader Model 6.10 需要它
  DLSS5-AMD\                     权重、编译好的着色器、运行参数（必须和 dlss5-amd.addon64 在同一目录）
  SHA256SUMS.txt                 文件校验

需要
----
  1. RX 9070 / 9070 XT（RDNA4）+ AMD 26.10.07.02 预览驱动（正式驱动没有 Shader Model 6.10 的 wave matrix）。
  2. 本包已含 Magpie 实验分支（https://github.com/SAOG0721/Magpie）。
  3. 显示器分辨率不限，但游戏窗口必须是 1920x1080 无边框，Magpie 缩放选"原始尺寸"（不放大）。
     网络只认 1920x1080 进、1920x1080 出。

安装（整包版：Magpie 本体已经在里面，解压即用）
----
  1. 解压到任意目录（路径别带中文），运行 Magpie.exe。配置是便携模式（config\config.json 随包），已经预设好：
     效果组 "DLSS5-AMD"（只有 FSR3_SR，光流 AMDOF）、缩放"原始尺寸"、重复帧检测已关。
     如果你自己改了配置，要保证：效果组只放 FSR3 -> FSR3_SR、缩放选"原始尺寸"、设置里重复帧检测选"从不"。
  2. 游戏：显示模式无边框窗口，1920x1080。
  3. 在游戏里按 Magpie 的缩放热键（默认 Win+Shift+A）激活。前 20~30 秒是网络初始化（读 600MB 权重、建 179 个着色器），
     这段时间画面是 Magpie 自己的 FSR3；初始化完成后帧率会掉到 30 左右，那就是 DLSS 5 接管了。
     再按一次热键停止缩放就是对比。

已知
----
  - 每帧约 33~36 ms（网络 24 ms + Magpie 捕获/呈现），30 fps 上下；游戏本身可以照常 60 fps。
  - 输入是显示用的 8 位 sRGB 图（不是游戏内钩子那种线性 HDR 场景色）；插件按 sRGB 直通处理（DLSS5_CODEC_SRGB=1），
    亮度和原图一致。偶尔会在暗部皮肤上出现 8 像素的方块闪一下（网络时序分支的毛病，DLSS5_HISTORY_GUARD 压掉了大部分）。
  - 运动向量来自 Magpie 的光流估计（效果参数 Optical Flow Method 选 AMDOF）；光流在平坦暗部会给出几万像素的垃圾向量，
    插件把超过 64 像素的向量当静止处理（DLSS5-AMD\native-game-flags.txt 的 DLSS5_MOTION_MAX_PX），否则会出现黑色/粉色的方块闪烁。
  - 停止缩放再激活，插件会重新接管（再等 20 秒）。
  - 日志：DLSS5-AMD\logs\native-game-oneshot.txt（初始化）、native-submission-order.txt（每帧观察）。
  - F6 是本插件的开关键（全局）；如果同一台机器上游戏里也装了本插件的游戏版，两边会一起切。

卸载
----
  整个目录删掉即可，不写注册表、不碰 AppData。

来源
----
  https://github.com/lmxxf/dlss5-on-amd-9070xt-porting（源码、每个 tag 的改动、开发记录）
