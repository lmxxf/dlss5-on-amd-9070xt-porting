# 0.46.1 OP 常规完整包

2026-11-08 交付，仅 OptiScaler 包。公开下载：[夸克网盘](https://pan.quark.cn/s/78a7de295d1f)。修复提交 `5f30cc5d`，正式 O2 HIP 插件 SHA256 `c762cf36d2a12bceca8750fbcc42fa0e1f8a2aecb2084bbcaf3f259a4e0c7347`。

- 成品：`D:\給網友打包\OptiScaler-DLSS5-AMD-0.46.1.zip`
- 大小：525,149,494 字节。
- ZIP SHA256：`2b7e8f4e7fdf54e6f718ca405cf98b7d9003c9d1d3f27f4875a969e504e1ffcc`。
- 同目录 `.zip.sha256` 已生成；700文件逐条解压流SHA读回通过。
- 以已核SHA的0.46 OP完整ZIP为底包；690个旧文件逐SHA保持，包括80 HIP模块、模型权重、着色器、通用默认配置与OptiScaler宿主。
- 新增PBZ安装说明、可选同步前置/FSR4关闭兼容配置，以及匹配修复源码 `sources/addon-source-0.46.1.tar.gz`（SHA `c5fedfa22cee3ce574b250061f29d2f5bfc6605b8d31020377e8f7eadfb1ee93`）。源码包含实际MinHook/ReShade依赖和构建脚本，两个修复源文件与提交逐字节一致。

用户确认修复后能进入《影之刃零》；随后只读日志到18300次FSR调用，含processed1/replay0。实玩保留ASYNC0/FSR4false，尚未证明它们是否必需。通用默认不改，PBZ按包内说明复用已确认配置。未作长时间稳定性、其他游戏回归、全模式逐位或FPS证明。

打包时游戏正在运行，只在独立发行目录做CPU压缩/校验，不改游戏目录，不执行GPU测试。插件以游戏已验同一二进制入包；继承底包已验shader且逐SHA不变。原0.46 ZIP未改，未做Magpie/RE9包，未上传、push或创建远端release。

过程记录：首次脚本函数H与PowerShell内建alias冲突，在解包前退出；改HashFile。首次ZIP审计因.NET生成反斜杠路径误报缺条目，规范化路径后由audit.ps1重新逐文件验证，同一ZIP全部通过，未重压刷结果。脚本/回执保留本目录。

用户提供上述夸克公开入口；未独立下载核验网盘文件。已交付ZIP及SHA保持不变，链接仅更新仓库文档。
