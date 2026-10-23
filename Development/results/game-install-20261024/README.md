# v0.45 双游戏安装回执

已完成《剑星》与《鬼武者》核心payload安装。仅替换剑星addon、鬼武者root/_storage_ runtime及两套HIP80/三层校验清单，共169项全部SHA读回。复用phase6最终宿主/模块，没有重编或ABBA。

- 完整备份：1502文件，2687928349B；目录 `D:\DLSSNR-Lab\game-install-20261024\backup-20261024-012751`。范围为0.44包列出的现存模组文件、整个DLSS5-AMD/shaders/_storage_及根目录ini/cfg。
- 回滚：`D:\DLSSNR-Lab\game-install-20261024\rollback.ps1`；恢复安装前备份并核SHA，新增安装项按计划删除。JSON明确UTF8读取，解析与1502项/2游戏元数据门已通过，未为测试而回滚成功安装。
- 16份配置记录保持字节及存在状态，其中两游戏default/custom/native六份与首次preflight SHA完全一致。安装不改MP、MODE、F9或其它个人设置。
- 鬼武者root与_storage_ runtime分别API smoke，均exit0，硬15秒；打包后再次读回169项和配置16项通过。
- 预检/两次后续事务错误均保留原日志；前两次未完成替换，第三次安装与smoke成功后回执求和出错，自动恢复全部原文件后重试。一次smoke新增派生shader缓存未被删除，已纳入最终备份；不宣称失败恢复后整个目录没有新增文件。

证据：receipt.json、backup-manifest.json、install-plan.json、config-before/after.json、两路smoke日志、rollback.ps1和rollback-verify.log。最终锁/进程/磁盘见 ../../releases/0.45/final-state.json。
