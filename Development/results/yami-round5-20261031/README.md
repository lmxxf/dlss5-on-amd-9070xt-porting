# Round 5 交付索引

结论与全部性能表见 [独立报告](GAP-EVALUATION-ROUND5-20261031.md)。最终选择为 `minimal`，只有 gfx1201 现有 norm900 分支启用新归约；没有性能上限证明。

- `final-decision.json`：最终选择；`final-five-decision.json` 保留较大组合全部失败的裁决。
- `minimal-long-statistics.json`、`minimal-long/evidence/`：独立长批数据。
- `final-configuration-verdict.json`：20配置138文件对逐位一致。
- `final-source-artifact-verification.json`：当前源码与80对象对应。
- `payload/HIP/`：双架构80个 HSACO 与清单；`payload/` 中六个宿主沿用 b60，SHA 见 `unchanged-hosts.json`。
- `production-norm-only/source/hip/`：最终源码快照；主工作区已整合。
- `final-status.json`：远端任务清场与文件审计。

从仓库根目录重建本文：

```sh
python3 Development/results/yami-round5-20261031/write-round5-report.py
```

实验执行脚本、预登记方案、逐轮统计和失败记录留在本目录；各 `*-evidence.zip` 及对应 evidence 目录保存日志/CSV/清单。二进制与部分大型实验产物被 gitignore 排除，但保留在磁盘，不能只复制已跟踪文件当作完整证据包。尚未提交或部署。
