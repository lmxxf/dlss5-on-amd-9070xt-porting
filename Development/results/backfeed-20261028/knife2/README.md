# 刀2：全部STOP

以a71f5d72为基线。contract/project/qkv三个独立宏候选正式COMGR编译。重编baseline全部103函数机器码与资源元数据同现载荷；整文件不同来自源码身份。每候选只改一个目标函数。

contract：b8 32→0、b128 0→2，LDS4096不变、无spill，但VGPR112→113。project：b8 32→0、b128 0→2，LDS0→1024、无spill，但VGPR112→113。按本轮VGPR无退步硬门均STOP，未上GPU；不声称它们已经过Gold或性能测试。

qkv：78VGPR/16KB LDS/0spill不变，b8 16→0，b128 1→2。六档36帧Gold逐位同、invalid0，六档各3轮4slot ABBA/240弃80完成。Original1088 mean−3.003µs但p99+8.805µs；1152 mean+5.726/p99+13.560µs；Small720 mean+0.192µs，三轮不稳定。其它三档生产用_qkfrag，未执行改核，计时变化不算候选收益。qkv STOP。可选expand未执行。

生产源恢复a71f5d72，未更换任何生产模块；补丁、编译源生成脚本、ISA、CSV与命令保留。首次作业准备脚本字符串错误及baseline整SHA拒绝均发生GPU前；随后补全103函数code/resources核同，再锁生产原SHA进入测试。GPU锁已释放，未改游戏/ZIP。
