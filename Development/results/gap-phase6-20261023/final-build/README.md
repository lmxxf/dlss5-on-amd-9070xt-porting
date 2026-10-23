# 第六阶段最终payload已封口

最终目录：`/home/lmxxf/work/dlss5-gap-phase6-20261023/final-payload/`。正式addon/runtime此前已按仓库脚本构建，今次不重编；保留刀1完整HIP80（gfx1200/gfx1201各40），重新核SHA/ELF全同生产SUMS `2753bd68…16026`。两级SHA清单齐全。

只采纳QK三shape限定：Original900、Small900、Small1080；其它shape维持V-only。Up fullA与half-only均STOP，生产无HIP_UP_WIDE_BYTE_PAIR，未把候选模块放进payload。全scope实验pure1088的−13.577µs不是此限定生产收益。

最终身份见receipt.json、modules-80.json；host源/产物沿用host-build-receipt.json及host-source-manifest.json，GPU源另记gpu-source-identity.json，工作树差异见production.patch。已有14jobs51帧限定默认/关闭/缺export/AE独立复核通过；A停止的既有Frame/pure统计也已独立复算，无删样本或扩测。

本构建者未目标操作/GPU/RTC、未安装游戏或发布ZIP，未为最后裁决重编宿主。这里是宿主＋80设备模块payload，不含权重/配置/游戏宿主的玩家发行ZIP。gfx1200仍仅编译/身份范围。
