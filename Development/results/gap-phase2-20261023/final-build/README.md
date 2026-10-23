# 第二阶段最终宿主构建（CPU完成）

最终生产仅保刀1的两处compact资格：原1920×1152不变，新增Original1920×1088/1600×960。刀2FMA_MIX、刀3N-pair均STOP且未进入生产。生产源码差异见 `production.patch`，全部选定本地源文件SHA见 `source-manifest.json`，构建身份/命令/产物SHA见 `receipt.json`。

使用仓库正式 `scripts/build-addon.sh ... --hip` 和 `scripts/build-runtime.sh ...`，两者CPU编译成功。输出 `/home/lmxxf/work/dlss5-gap-phase2-20261023/final-payload/`，包含addon、runtime及runtime导入库，附SHA256SUMS。没有重编HSACO，仍搭配已验证的原80双架构模块；本目录不重复复制这些模块。

本次未执行GPU/smoke、未安装游戏、未打发行ZIP。宿主构建成功不代替部署/运行时烟测；root后续可按需要使用这些正式产物。首次命令因cwd已在仓库而多写一级mkdir，日志重定向在编译前失败；已删除自己建立的空目录并纠正路径，说明保留 `prebuild-path-failure.log`，不隐去失败。
