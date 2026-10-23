# C512 LDS TR支持排查：本目标不支持

没有GPU候选或GPU launch。正式COMGR3.0、gfx1201、锁定rtc工具SHA的两次CPU编译均按预期拒绝，原command/exit/stdout/stderr见B-evidence。

- 用户给出的ds_load_tr8形式：COMGR报builtin未声明，并提示ds_read_tr8名称。
- 按其提示改为ds_read_tr8：COMGR明确报needs target feature gfx950-insts。
- 本地LLVM23对ds_load_tr8支持目标为gfx1250；同一汇编gfx1201拒绝、gfx1250成功，Builtin/selector也限定gfx1250Plus/wave32。

这与gfx1201可用的global_load_tr不同，不把两者混为一种硬件。两次runner失败是预期CPU编译拒绝，均释放锁，未生成可执行gfx1201模块。

旧C512_COMPACT_VT读端64条u8换b64却增加写端32条b8，09-30及10-03已有负/不稳定收益；本轮没有新的写端组织机制，不重刷旧VT，也不据此断言所有LDS优化都不可能。继续只实测刀2A的Up出口成对转换。

上游主仓当前builtin定义亦把ds_load_tr8_b64_v2i32限定为gfx1250-insts,wavefrontsize32：[LLVM官方定义](https://raw.githubusercontent.com/llvm/llvm-project/main/clang/include/clang/Basic/BuiltinsAMDGPU.td)（本轮检索核对；本机实际COMGR拒绝证据仍独立保留）。
