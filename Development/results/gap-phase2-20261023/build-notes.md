# 第二阶段刀2/刀3：CPU构建入口与基线复现

2026-10-23。本轮仅调查构建，并重建**未改动**的两个基线模块；未实施刀2/刀3、未GPU、未远端RTC、未动生产或游戏。

## 结论

- **C32可在本机直接精确重建**：`c32-wave1-fast` 用正式LLVM23 recipe，11.466秒，整个 `.hsaco` SHA256 **`8a5d9ddc139cfd44c242acade1b684810af6a40633a0ebc378209cd510818e83`**，与当前40模块清单及10-17最终载荷逐字节同。
- **deep_fast-packed-fast不能把本地LLVM输出冒充正式COMGR基线**：正式模块由Windows驱动 `amd_comgr_3.dll` 编译、空额外opts。本机未找到可用COMGR DLL/so；以现有LLVM21 fork重建同一生成源码耗时7.315秒，但整个模块SHA和`.text`都不同，故只可做CPU语法/初步ISA筛查，正式验证仍需主进程GPU锁队列下的COMGR编译。
- 两模块从当前 `hip/` 经正式Python recipe生成的合并源码，与最终载荷目录 `.generated.hip` **逐字节相同**。变化不能归因于后来代码/宏；C32匹配、deep不匹配是当前工具链验证结果。

基线回执：[C32构建配方/耗时/完整命令](c32-baseline-build.json)、[deep LLVM21与COMGR各段SHA](deep-llvm21-baseline-check.json)。

## 工具路径

|用途|路径/身份|
|---|---|
|正式LLVM23 C32编译器|`/home/lmxxf/work/llvm-build-23/bin/clang`，23.1.2，commit `85ac560262434c9ccfc0c183ec22d4138ed647fb`|
|正式LLVM23链接器|`/home/lmxxf/work/llvm-build-23/bin/ld.lld`|
|反汇编/元数据|同目录 `llvm-objdump`、`llvm-readobj`|
|旧LLVM21 fork（不是当前deep精确替身）|`/home/lmxxf/work/llvm-build-dlss5-gfx12/bin/clang`，21.0.0git，commit `94aca371a8e1c37e281c139eadbd0146120bcca2`|
|本地正式配方解释器|`Development/tools/llvm-fork/compile-modules.py`|
|权威配方表|`hip/build-modules.ps1`|
|正式COMGR工具源|`hip/rtc_compile.cpp`；本地历史当前工具 `/home/lmxxf/work/dlss5-five-build/rtc_compile-current.exe`|
|当前完整对照载荷|`/home/lmxxf/work/dlss5-five-build/final-payload/HIP/gfx1201/`，40/40 SHA已核|
|本轮基线输出|`/home/lmxxf/work/dlss5-phase2-build-20261023/baseline-{llvm23,llvm21}/`|

## C32正式命令已落脚本

运行本目录 [cpu-baseline-build.sh](cpu-baseline-build.sh)，默认只重建未改动 `c32-wave1-fast` 并核对当前清单SHA；不会上传、加载或执行。

实际调用参数：`--bin /home/lmxxf/work/llvm-build-23/bin --targets gfx1201 --only c32-wave1-fast --compiler-rows llvm23 --row-opts --target-feature=-real-true16 --jobs 1`。

关键点：

- `--row-opts` 必须保留。C32后端参数为 `-enable-post-misched=0`、`-amdgpu-sched-strategy=max-ilp`。
- `--target-feature=-real-true16` 同时影响前后端。LLVM23默认true16与已有内联汇编的寄存器语法不兼容，不能漏。
- Python脚本默认 `--bin` 是旧LLVM21，**必须显式指向LLVM23**。
- 前端Windows辅助ABI、C++14、`-nogpuinc/-nogpulib`、source命名与cuid，脚本已经固定；不要另写一条简化hipcc命令。
- C32 source是 `c32_fused_ffn_attention.hip + wave_owned_c32.inc`，激活热点在后者当前362/390行；900档norm twin另带 `CW_NORM_HOIST=1`，若做C32改动需另评该模块而非只换一个文件宣称所有档覆盖。

## deep的正式COMGR路径

`build-modules.ps1 -Only deep_fast-packed-fast -Targets gfx1201` 会只编这一行。正式编译调用是：

```powershell
# 仅示意由主进程已持锁/空闲/看门狗wrapper调用；本轮未执行。
& <当前工具目录>\rtc_compile.exe <实验输出>\deep_fast-packed-fast.hsaco <实验输出>\deep_fast-packed-fast.generated.hip comgr gfx1201
```

- 工具**从Windows System32加载** `amd_comgr_3.dll`；不加载本地任意DLL。不必SDK。
- `RTC_EXTRA_OPTS` 对此行应清空；整个正式流水使用 `-RowOpts` 也不会给该行添加opts。
- 当前recipe只合并 `deep_fast.hip`；宏为 `HIP_ISA_HALF=1, HIP_DEC_WIDE=1, HIP_VIT_ATTN_NATIVE_HALF=1, HIP_VIT_ATTN_PROB_PAIR=1, HIP_VIT_ATTN_TRANSPOSED_AV=1, HIP_VIT_ATTN_TRANSPOSED_SCORE=1, HIP_BRANCHLESS_F=1, C512_F_MASK=1, C512_T8_TAIL_VEC=1, HIP_BYTE_F_ADD0=1, HIP_VIT_ATTN_RCP=1, VIT_FAST_NUM=15`。
- **不要额外猜加 `HIP_PREPACKED_WEIGHTS=1`**：生成器的隐式规则是名字以 `-packed` 结尾；`deep_fast-packed-fast` 不满足，现行生成文本确实没有该宏。照权威recipe即可。
- 10-17旧远端工具曾忽略gfx1200参数；以后用当前 `rtc_compile.cpp` 编译出的工具，并核实际ELF目标，不能只看文件夹名。
- 对照生产deep SHA：`00e39349bc8a1a02e5a61b4606d1541d5ed1e4988de8c17db3cc5c8380c095ce`。

## ISA检查入口

```bash
/home/lmxxf/work/llvm-build-23/bin/llvm-objdump -d --mcpu=gfx1201 <实验模块.hsaco>
/home/lmxxf/work/llvm-build-23/bin/llvm-readobj --notes <实验模块.hsaco>
```

先用阶段一 `isa/analyze.py` 的symbol-size截断算法取实际核体，避免把对齐padding算进指令数。注意同名kernel在多模块存在，按实际trace的module别名选。

刀2的C32 FMA_MIX可在本地精确工具链上快速试验ISA；任何新增短asm必须看寄存器约束、MODE与舍入。刀3若涉及deep的ViT数学，LLVM23/21本地重编只能算另一种编译器实验，不能跨编译器把收益归到数学改动。正式COMGR基线/候选应同工具、同批配方，只改目标算术。
