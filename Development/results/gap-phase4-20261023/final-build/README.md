# 第四阶段最终完整二进制payload（CPU构建完成）

生产包含此前已采纳的Original compact范围、刀1 head-major V-fragment默认路径及刀2 Small compact helper；已STOP的MIX/N-pair/QT32/TCHAIN/wait-tail候选均未混入。最终生产header SHA09cb03d5…2d734，与刀2已测candidate整头逐字节同。

按仓库正式scripts/build-addon.sh --hip和scripts/build-runtime.sh构建addon/runtime成功。最终目录：`/home/lmxxf/work/dlss5-gap-phase4-20261023/final-payload/`。包含两宿主、runtime import library、HIP双架构各40模块及两级SHA清单。80模块从knife1已验final-payload逐项复制，与生产SUMS和实际ELF target全核同，未重编设备代码。

回执：`recipe.json`列精确命令；`source-manifest.json`锁本地源文件集合；`production.patch`记录相对Git HEAD的生产改动；`modules-80.json`保80项身份/目标/复制读回结果；`receipt.json`锁最终全部文件SHA。

这里是完整宿主＋设备模块payload，不是含权重、配置与游戏宿主的玩家发行ZIP。本次没有GPU/smoke、目标机操作、游戏安装或发布。gfx1200仍只编译/身份核验，不能冒实机证据。后续root的默认/回退/烟测应引用本最终身份。
