# 刀2 B不变的expanded折叠/Contract预取：STOP

保持普通wave2 B=4；SP配方原本B=2，保持2。W16按tile0 expanded[B]→量化hidden0，再tile1 expanded[B]→hidden1，在tile1激活前读取2份Contract W16权重；每份Expand/Contract权重仍被全部B个qt复用。K16顺序、Contract按ht/tile顺序与舍入不改，但源码允许输入LDS片段重读，不冒称所有读取都保持。

普通C256折叠CPU门失败：初版及调度屏障版256VGPR/44B private/10spill；强制LDS重读版139spill；两个明确词法作用域版仍6spill。均未上GPU，不用spill版本测快慢。只保留C64/C128非W16提前Contract的候选P，VGPR199/242及LDS不变、0spill，权重请求不增加。候选S独立测试持久核折叠：run184→153、recover235→204VGPR，LDS32768不变、0spill，B2不变。

P、S分别七档Gold42帧逐位同、四档3ABBA。P四档无稳定净收益（Orig1088pooled−20.903但R1+14.255/p99pooled+1.986；Small两档mean及尾有退化）。S Orig1088三轮mean全退，pooled+58.954µs/p99+89.374µs。Small稳态不执行SP，包含的原1x预热不冒充SP的Small收益。SP计数/原日志保留，未改counter/barrier协议。两者均STOP，不混合，不继续刷轮。生产源码/80模块不动，无部署、ZIP/push。
