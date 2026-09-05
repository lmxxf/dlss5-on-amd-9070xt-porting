《剑星》AMD DLSS5-derived 实验运行包 — 1080p

这是目前的实验移植版，不是 NVIDIA/AMD/游戏厂商的官方产品。
已测试：Windows 11、RX 9070 XT、《剑星》1920×1080窗口模式。
当前实际游戏约19fps，不是30fps完成版。其他显卡、游戏版本、HDR尚未验证。

文件：
  d3d12.dll                         ReShade加载器
  dlss5-1080p-runtime.addon64        我们的Windows DLL，内含权重和shader
  ReShade-LICENSE.txt / MinHook-LICENSE.txt  组件版权说明

安装：
1. 完全退出游戏。
2. Steam → 剑星 → 管理 → 浏览本地文件，进入 SB\Binaries\Win64。
3. 若已有 d3d12.dll、同名addon或其他ReShade/mod加载器，先备份，勿直接覆盖未知配置。
4. 将本包 d3d12.dll 和 dlss5-1080p-runtime.addon64 复制到该目录。
   .addon64本身就是DLL；扩展名是给ReShade识别用的，请勿改名。
5. 游戏设置使用1920×1080、AMD FSR超分辨率路径，第一版先关闭帧生成。
6. 正常从Steam启动。首次进入后等待约30–45秒初始化模型，再进入存档。

不需要下载原始模型DLL，不需要D:\DLSSNR-Lab，不需要Python、编译器、SDK或开发者模式。
使用Windows系统自带DirectML；模型数据和shader从addon自身PE资源读取，不向外部目录释放权重。
日志位于Windows临时目录：按Win+R，输入 %TEMP% ，查看 dlss5-1080p-runtime.log。
日志应包含 runtime_assets=embedded 和持续增长的 display_residual generation。

F6循环切换：神经输出／原生画面对照／左右对照。
注意：F6只是画面对照，后台仍执行网络，不能用它比较关闭网络后的帧率。

卸载：退出游戏后删除本次复制的两个文件，恢复自己备份的原文件。
不要删除原本就存在的其他mod或配置。更新游戏或更换版本后需重新验证兼容性。
