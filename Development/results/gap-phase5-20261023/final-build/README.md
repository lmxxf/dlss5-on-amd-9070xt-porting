# Phase5最终完整二进制payload

正式scripts/build-addon.sh --hip及scripts/build-runtime.sh均CPU构建成功。最终目录：`/home/lmxxf/work/dlss5-gap-phase5-20261023/final-payload/`。包含addon/runtime、runtime导入库、HIP双架构各40模块和两级SHA清单。

新增只收C512 pair-only，原串行norm保持；以及Small固定几何/共享AE gate的VFRAG准入。permlanex16与combined候选不带入。生产header SHA8ae0e238…ed28，逐字等刀2实测候选。HIP80来自刀1正式final-payload，全部与生产SUMS（c8e6d3fa…c237）逐SHA匹配，复制读回与ELF目标40+40均通过，没有设备代码重编。

源码、recipe、工作树差异和产物身份分别见source-manifest.json、recipe.json、production.patch、receipt.json，80模块明细见modules-80.json。构建日志为addon.log/runtime.log。

root已执行最终正式Frame默认/on/off9jobs36帧，独立本地复核逐帧hash、AE CSV、默认/关闭实际路由均PASS（knife2/independent-final-default-review.json）；实际用正式8b2132…C512模块。此Frame验证不冒称本构建者运行了addon/runtime API smoke。

本构建者无SSH/GPU/RTC/目标机操作，没有安装游戏或发布ZIP。这里是宿主＋设备模块payload，不含权重/配置/游戏平台宿主的完整玩家发行ZIP；gfx1200仍只编译/身份核验。
