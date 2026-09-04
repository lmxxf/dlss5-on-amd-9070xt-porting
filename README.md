# 297 期模型结构分析资料

## 当前结果

严格审计后的固定输入离线移植已经完成。链路从真正的seq0 prefill数值输入开始，在RX9070XT连续执行blocks0–69，不注入NVIDIA中间activation；block70由四层HLSL spatial effective kernel完成。最终256×144内容ROI见`dlss5-seq0-to-rgb-amd-final.png`：checkerboard未训练像素Pearson correlation 0.918201、MAE 0.014286，AMD全量cosine 0.927845，block70最新耗时3.459ms。完整证据见`dlss5-amd-final-validation.json`。范围是固定输入离线correctness port，不宣称跨帧泛化或游戏内实时可用。

动态产品化正在进行：`d3d12_dynamic_resource_probe.cpp`已在RX9070XT的《剑星》进程内稳定识别3840×2160主swapchain，并通过ReShade immediate command list闭合逐帧copy-in／AMD compute／copy-out。进一步直接hook官方`ffxDispatch`取得当前帧权威Color RGBA16F、Depth R32F、Motion RG16F与FSR输出合同；一张真实游戏帧已在AMD上连续执行DLSS5-derived block0与blocks1–3，四段耗时94.361/306.832/231.226/306.251ms。证据见`dlss5-amd-dynamic-milestone.json`。严格状态仍是未完成：blocks4–70尚未接入resident addon，当前屏幕compute仅为回写链诊断，不能称为DLSS5最终输出。

2026-09-04后续已把两张不同游戏帧各自执行到block70，并在运行中的swapchain完成两份R10结果热切换；全幅block70为520.721–539.875ms，自动入口为`run_dynamic_frame_pipeline.sh`。但严格动态审计发现两帧的neural output SHA逐byte相同：固定帧live-correction主链到block13已坍缩，后续skip虽重新带入差异，fixed-frame block70 spatial head仍将其映成同一输出。当前画面变化来自post合同的Color base，不是神经分支，因此目标仍未完成。反证和checkpoint SHA见`dlss5-amd-dynamic-fullchain.json`；下一步从动态路径移除固定帧校正并换回通用block70 prefix/body/outconv。

上述坍缩随后被解除：动态raw路径不再应用任何`*-live-correction.bin`，两张实景帧的差异已连续穿过blocks0–69与ViT；block70按225个局部tile分成9批执行，neural输出99.9999919%元素不同。通用head也已运行：CSR prefix仅2432个非零、全幅1H body约1.49–1.56s、outconv约37–41ms，两份最终RGBA SHA不同并在同一游戏进程热切换。证据见`dlss5-amd-dynamic-raw-general.json`。严格剩余问题是明显周期条纹：已知block70 prefix local physical-record排列未exact（旧固定帧上限约0.43 correlation）；5090当前离线，尚不能抓新多帧oracle闭合该排列。因此功能动态链已证明，但画质移植仍未完成。旧含固定帧校正的自动脚本已重命名为`run_dynamic_frame_pipeline_fixed_correction.sh`，避免误用。

周期条纹随后确认并非network residual：旧probe在capture前执行了诊断fallback compute，未叠神经的backbuffer dump本身已有相同条纹。现已禁用无外部输出时的fallback，并把R10 capture移到任何override之前；新frame5400 clean capture无条纹。把通用head residual叠回clean R10后画面正常，两份clean结果在游戏内热切换成功。证据见`dlss5-amd-clean-display-validation.json`。严格保留一项边界：clean base与已算residual来自相近但非同一frame；下一步把raw pipeline改成单命令处理同一clean capture并测端到端延迟。

同帧边界现已闭合。`run_dynamic_frame_pipeline.sh FRAME`只接受同一编号的FFX Color与override前4K backbuffer，连续执行raw blocks0–69、通用block70 prefix/body/outconv，再原子发布R10。frame5400主菜单与frame16800实际洞穴场景均完整跑通，packed SHA分别为`e2f63091...6480`与`923c35a7...f3e9`；frame16800的Color/Depth/Motion/backbuffer四路均非静态，最终游戏画面无条纹。两份输出又在同一游戏进程按场景→主菜单→场景热切换，probe于frame600/3600/6000确认三次加载。证据见`dlss5-amd-synchronous-dynamic-validation.json`。严格状态仍未完成：frame16800端到端约484秒，现有多进程、逐层建D3D12设备和GiB级磁盘/SSH往返必须改成常驻GPU graph，才能称为实时动态渲染。

实时化首轮已开始：实测单帧中间FP32文件10.36GB；block69进程wall 1846.427ms而GPU fence仅346.684ms。新增Windows原生`merge_upsample_skip.cpp`并替换block48/56/62/66的Linux NumPy往返，block62逐byte回归通过，每帧先消除约1.13GB SSH传输。它仍是过渡步骤，最终要求是层间tensor全留在常驻D3D12 resource。

现有32-channel与64/128/256/512-channel runner又加入版本化持久CSO cache。block69冷→热wall为3944→1129ms，block65为3629→1616ms，输出均逐byte不变；它消除了重复shader编译，但尚未消除逐层device创建和FP32文件读写。

全分辨率32-channel attention已补QKV预计算，block69 GPU fence从346.684ms降至约308–313ms；shift=0/1两类输出均逐byte exact。12KB groupshared window缓存实测反而回退到331.767ms，已撤销。当前naive FP32/SM5.1 kernel本身仍远超实时预算，后续除常驻graph外还必须转FP16与wave/matrix级实现。

随后硬件探针确认RX9070XT支持SM6.8、Wave32/64与native 16-bit，AMD Lab已部署DXC 1.9.2602.24。关键控制实验推翻“必须先牺牲精度上FP16”：仅把32-channel FFN改由DXC以SM6.2 FP32编译，block69便从346.684ms降至24.582ms，且逐byte exact；blocks66–69合计100.980ms，最终仍逐byte exact。FP16 QKV反而略慢且有微误差，未进入正式链。

同一无损路线扩到64-channel：block65从912.447ms降至35.721ms，25.5倍；encoder blocks5–8与decoder blocks62–65四种shift串联后均逐byte exact。统一构建入口现为`build_sm6_ffn_shaders.ps1`。

更宽层不能全展开，已改成expand/project两pass并复用QKV scratch：128-channel block13从681.549ms降至89.630ms；256-channel block55从251.426ms降至22.407ms；512-channel gated block47从122.174ms降至23.053ms。blocks9–13、23–29、40–47、49–55四段累计回归全部逐byte exact。普通Swin五档宽度至此均已进入无损加速路径，下一热点是ViT/attention与常驻graph。

ViT31–38已开始优化：只读回最终result先省约10ms；GPU timestamp把旧block31拆为Expand45.111/Contract48.091/QKV16.913/Attention119.646/Projection2.197ms。Attention改为每个query×head只算一次softmax并同时累计32维V后降至33.964ms（3.52倍），完整blocks31–38最终输出逐byte exact。五个ViT shader亦加入持久CSO cache，单block冷/热wall为4208/574ms；常驻graph仍是消除剩余wall开销的必要条件。

随后64值分块共享令Contract从约58.5ms降至6.5–7.4ms；同法对Expand反而变慢，已撤回。Attention再改为SM6.6 Wave32，以`WaveActiveSum`并行32维QK，降至22.873ms。block31约92.987ms，完整八层GPU约770ms，最终block38逐byte exact。当前ViT最大热点转为Expand。

Expand改为每线程累计相邻4个输出后，热态降至8.3–19.2ms；`float8`无额外收益，保留`float4`。ViT正式组合现为float4 Expand＋tiled Contract＋Wave32 Attention，完整八层每层57.7–67.9ms、合计约496ms，较最初约1.87秒快3.8倍，最终block38仍逐byte exact。下一热点是QKV与Attention本体。

QKV矩阵／归一化两pass曾把单层约15.6ms降至12.4ms，且block31逐byte exact；但八层累计后block38降至correlation0.993642、NRMSE11.37%，故已撤回。正式单pass QKV恢复后八层最终再次逐byte exact。后续优化必须保留完整八层累计门，不能用单block量化相等替代。

普通Swin attention现已按`token×head`并行并把projection拆成独立pass；专用`qkv_head()`只加载当前16维，避免每head白读完整QKV。128-channel Attention从32.959ms降至0.700ms，block13从约90ms降至约25–31ms。64/256/512通道attention也降至约0.4–1.5ms；blocks9–13、40–47、49–55、57–61、62–65累计回归全部逐byte exact。普通Swin当前主要计算热点已转为QKV/FFN，系统级热点仍是逐层进程和文件I/O。

首个真正多层resident graph已落地：`d3d12_swin_chain.cpp`在单device/单command-list内用default-heap ping-pong连续执行多层，只在入口/末端传输。128-channel blocks57–61含最终copy为115.188ms，进程wall从2944.246ms降至649.723ms（4.53倍）；blocks9–13为102.950ms。宿主随后泛化到256/512/64通道：blocks15–21/49–55、23–29/40–47、5–8/62–65均已resident化，六段末端全部逐byte exact。普通Swin仅剩32-channel首尾两段尚未resident。

32-channel另由`d3d12_swin32_chain.cpp`收束：blocks1–4为56.594ms，blocks66–69为64.085ms，均含最终copy且逐byte exact，并已接入正式动态脚本。至此blocks1–69内所有普通Swin连续段均不再逐层创建device或落中间文件；下一步是ViT八层resident与跨stage整图合并。

ViT八层resident亦已完成：`d3d12_vit_chain_amd.cpp`一次上传八套参数，复用main ping-pong与branch/hidden/QKV/attention scratch，单device/单command-list执行blocks31–38。frame16800含最终copy为513.721ms，block38逐byte exact；wall从4211.536ms降至1245.663ms（3.38倍），正式ViT脚本已切换。当前所有连续Swin/ViT段均resident化，剩余是跨stage统一graph和GPU本体继续加速。

block70先移除跨机拼接：Windows原生prepare与旧Python输出SHA一致；body shader直接读取tiled prefix，不再生成HWC mosaic，输出逐byte exact且GPU约786→564ms。正式脚本每帧消除约3.2GB SSH传输。完整本地prepare→prefix→body→outconv wall仍为10.095秒，最终RGBA exact；下一步是把这四步收进单device resident block70 graph。

block70 resident graph随后完成：`d3d12_block70_chain.cpp`单device串联prefix/body/outconv，中间1.06GB张量只留显存，GPU＋最终copy为95.885ms；再把CPU tile prepare并入同一进程后wall从10.095秒降至1.373秒，最终RGBA仍逐byte exact。正式pipeline已切换该单进程入口。要到实时仍需与上游/游戏backbuffer共享GPU resource并继续把约96ms计算压低。

block70 GPU timestamp进一步定位prefix/FFN/QKV/attention/outconv约11.2/47.7/11.9/13.9/2.8ms；FFN换成专用DXC SM6.2 FP32后热态降至20.811ms，总GPU＋copy降至70.222ms且逐byte exact。当前主要边界转为上游activation与游戏backbuffer仍经CPU/file传递。

DXC QKV实测更慢且有极小非exact，已撤回。另新增Windows原生`preblock_tiles_to_hwc.cpp`，frame16800输出SHA与原Python完全一致；正式pipeline不再跨机pull/push两份267MB block0张量。当前跨机数据主要剩输入Color/backbuffer、block39准备与最终RGBA/R10。

block39 join与block30补行也已改成Windows原生工具，输出SHA均与原Python路径完全一致；固定block39 bias矩阵不再每帧重造。至此blocks1–70所有中间tensor都留在AMD机，Linux/Windows传输只剩帧输入预处理和最终验证产物。下一项权威指标是重跑完整frame16800后的新端到端wall。

block70 prefix现可直接读取block69/block0两份HWC SRV，不再构造530MB tile input；frame16800最终RGBA逐byte exact，热态GPU＋copy进一步为67.130ms。该改动wall只省约15ms，但完成了上游resident tensor直接接入所需的资源合同。完整pipeline脚本现会打印`pipeline_wall_seconds`。

Windows整网编排`run_dynamic_network_resident.ps1`已跑通：从block0 HWC到block70最终RGBA wall为20.883秒，输出逐byte exact。一次SSH已取代二十多次远程调用，但内部仍启动12个D3D12进程；当前瓶颈明确是跨stage本地文件/device边界，下一阶段为全网单进程常驻图与addon内执行。

阶段剖面显示20.338秒中encoder0–13/14–30分别占4.029/4.871秒，decoder各段1.28–2.29秒，而对应GPU本体大多仅50–150ms。下一合并单元为encoder0–13单device图，并保留block4/block8两条decoder skip；不再继续优化PowerShell/SSH控制层。

完整新管线已实跑：端到端49.594秒，较历史483.906秒快约9.76倍；Windows blocks1–70为20.403秒，证据见`dlss5-amd-resident-pipeline-validation.json`。新packed SHA与历史同名frame16800不同，是因为frame号被另一游戏进程复用覆盖，Color/backbuffer FNV均已变化，不是resident数值回归。脚本现同时打印两路输入SHA，避免再把进程内frame计数当全局身份。

最终RGBA→R10打包与原始R10→RGBA解码均已移到Windows原生工具，两端输出SHA与NumPy逐byte一致。正式pipeline不再跨机传132MB RGBA，只传29MB Color、33MB原始backbuffer验证副本与33MB最终R10；R10在AMD机本地原子发布给运行中的游戏。

Swin resident宿主现可把上一stage的downsample/enter直接接到下一段GPU input。block4→5–8、block8→9–13、block14→15–21三条边均逐byte exact；encoder0–13 wall从4.029秒降至3.181秒，encoder14–30从4.871秒降至4.340秒。它删除三个stage-input文件/进程，但完整encoder仍需进一步合成单device图。

block22→23特殊边也已GPU直连：136行池化为68行，尾4行在GPU清零后补成H72；blocks23–29输出exact。encoder14–30进一步降至3.999秒。下一步把block30 pool/enter、H34→H36 padding直接并入ViT八层resident宿主。

block30 pool/enter与H34→H36零padding现已并入ViT resident宿主，block30-body可直接跑到block38且逐byte exact；独立downsample/pad两进程和两份文件已删除。predown CSO cache热后：encoder0–13=2.960秒、encoder14–30=3.200秒、含block30 predown的ViT=1.279秒；两个encoder较初始合计减少约2.74秒。

同宽块继续合并：decoder48–55与56–61分别成为单次8/6层chain；encoder9–14、15–22、23–30分别成为6/8/8层chain，段末全部exact。encoder0–14热态2.853秒，encoder15–30降至1.399秒；Windows blocks1–70总wall由20.403秒进一步降至15.317秒，最终RGBA逐byte exact。

decoder四条跨stage现全部由resident宿主直接执行affine＋2×upsample＋skip再进入Swin，projected/prefix中间文件与独立merge进程均删除。block48→55/56→61/62→65/66→69分别为159.024/126.077/97.136/67.733ms且exact；Windows blocks1–70 wall进一步降至12.441秒，最终RGBA逐byte exact。

block39也已GPU直连：ViT main与block30 skip不再CPU拼1536维tensor，block39 affine直接写H72 stagein后执行blocks40–47；200.058ms且exact。整网wall降至11.540秒，相对resident初版20.403秒减少43.4%。下一边界是block70直接GPU打包R10，只回读33MB。

block70现由GPU以0.267ms直接打包R10，只回读33MB，不再落132MB RGBA或启动CPU pack；packed SHA逐byte一致。Windows blocks1–70 wall进一步降至10.590秒，相对resident初版20.403秒减48.1%。

- `reverse-engineering-notes.md`：相对 Hikari 初稿新增的宽度阶梯、skip 证据、71 个权重 block 与下一步逆向路线。
- `porting-worklog.md`：DLSSNR → AMD 的实际工作日志；记录设备拓扑、每日进度、失败、工作假设和下一步。
- `extract_model_evidence.py`：零依赖证据提取脚本，输出 kernel 家族、直接报错证据、权重 block/layer 编号和偏移。
- `parse_weights_archive.py`：顺序解析 `WEIGHTS_HT` 的 153 条顶层记录，不依赖字符串搜索；验证 FP16 payload、元素数与资源边界闭合，可导出 `weights-index.json`。
- `weights-index.json`：当前泄露样本的顶层权重索引；记录名字、跨度、payload 偏移／大小、类型码和元素数，不包含权重 payload 本身。
- `weights-arena-index.json`：按 NVIDIA live GPU VA 验证过的 512-byte 对齐规则生成的 AMD arena 索引；153 条 record 的相对显存偏移，总大小 147,719,680 字节。
- `d3d12_weight_arena_test.cpp`：最小 Windows D3D12 权重宿主；强制选择 AMD adapter，上传完整 FP16 arena 到 default heap、读回逐字节校验，并由 HLSL compute shader 按 153 个 record offset 实际读取权重。
- `block0-tensor-layout.json`：block0 的 10 个内部 FP16 张量边界与形状；元素数精确闭合到 10,848。
- `block0-live-view-candidate.json`：严格审计后的block0 view诊断。720种token-bit候选中最佳空间连续性H/V=0.906857/0.906012；seq0 prefill对该view直接affine仅0.084，但经block2下一层裁判为0.981858，说明输入抓取正确、block0输出basis仍未exact闭合。
- `d3d12_block0_prefill_test.cpp` / `block0-prefill-effective.bin` / `seq0-to-block69-amd-validation.json`：RX9070XT从真正seq0 prefill执行3×3 block0 effective后连续跑到block69，不注入NVIDIA中间activation。block0 cosine0.895223，进入block2仍有0.981650；最终block69对NVIDIA held-out0.996528。剩余硬缺口仅为block70 global physical bank→prefix的正确输入排布。
- `d3d12_block70_spatial_test.cpp` / `block70-spatial-effective.bin` / `prepare_block70_spatial_input.py`：最终block70固定输入AMD实现。四层3×3 CNN只用checkerboard一半像素训练，另一半对NVIDIA RGB correlation0.918201；RX9070XT最新执行3.459ms，GPU全量cosine0.927845。
- `block0_reference.py`：block0 的可读 FP32 参考实现；覆盖 adapter、depthwise、FFN 多项式／skip 和当前待 NVIDIA 中间层校准的 8×8 cosine attention。
- `infer_fused_layouts.py` / `fused-layouts.json`：自动闭合 32／64／128／256-channel fused-Swin record 的重复张量容量；明确区分容量闭合与尚待 unswizzle 的 tensor-core 物理排列。
- `dlssnr_layer_oracle_probe.cpp`：安全 hook 第一条 1H forward，导出真实网络尺寸、GPU VA、CubinBackendNGX／kernel backend live object 链，供定位 NvAPI dispatch。
- `read_process_pointer.ps1`：从 SSH 只读解析 live object/vtable 指针及所属模块 RVA。
- `capture_raw_buffer.cu` / `capture_raw_buffer_cubin.inc`：Spark CUDA 13 编译的 sm_120 `uint4` raw-copy kernel 及 5,680-byte 嵌入版本；用于在原 NvAPI CUBIN command chain 内复制私有 activation VA。
- `run_original_1h_oracle.cpp` / `make_1h_identity_weights.py`：在 DGX Spark sm_121 上直接加载泄露 sm_120 CUBIN，以 identity-skip 权重逐 byte 扫描 8×8×32 FP8 tinlayout。
- `run_original_preblock_oracle.cpp`：直接启动 0x108-byte ABI 的原始 pre-block DS CUBIN；输入 8×8 RGBA32F texture，并同时导出 8×8×32 主 FP8 activation 与 4×4×32 downsample activation。
- `run_original_downsample_oracle.cpp`：批量直跑 block4 原始 shifted DS CUBIN；把 8×8×32 physical tiles 转为 4×4×32 compact E4M3 view，并提供 kernel 必需的 auxiliary scratch。
- `run_original_2h_inpview_oracle.cpp`：批量直跑 block5 原始 2H inpview CUBIN；每四个 compact tiles 扩展成一个 8×8×64 E4M3 tile。
- `run_original_fused_global.cpp`：统一的全图 CUBIN runner；支持 plain／inpview／1H-DS／multihead-DS、global grid、shift 与 main/aux 双 view，取代已降级的逐 tile 数值 runner。
- `run_original_split_global.cpp`：按真实 `2/1/4/1` launch 拓扑执行512-channel split-Swin四层。
- `run_original_vit1d_global.cpp` / `run_original_vit1d_chain.cpp`：8×8 ViT 的cluster-aware runner；已闭合单block，跨block仍缺NvAPI `flag=1`同步原语。
- `run_original_vit_repack.cpp`：独立执行原始8×8 ViT 1D→2D repack，用于把任意ViT断点接回decoder做画面级验收。
- `run_original_vit_contract.cpp` / `run_original_vit_qkv.cpp`：独立执行 ViT Contract 与 QKV，并显式携带 main/work/aux 三路状态；用于绕开跨block同步、逐层恢复数值 view。
- `run_original_vit_qkv_matrix.cpp` / `run_original_vit_qkv_dataset.cpp`：复用同一 CUDA context 的 QKV basis 与 dataset oracle；前者恢复三套 physical output view，后者验证 Q/K per-head normalization 与 V 线性语义。
- `d3d12_nvapi_qkv_matrix.cpp`：5090专用QKV basis宿主；每轮恢复Contract main/work/aux并同组提交standard+chained，1024 basis约5.8秒完成。
- `block31-qkv-effective.fp8` / `.json` / `vit-{q,k,v}-offsets.i32`：block31权威NVAPI-pair 1024-basis矩阵、三套offset及地址bit公式；真实held-out Q/K归一化correlation 0.9826/0.9716，V线性correlation 0.9922。
- `block31-qkv-work-effective.f16` / `.json`：QKV写入work planes 0/2/4的三张FP16 auxiliary矩阵；held-out correlation均超过0.9999997，补齐Attention需要的另外32个token。
- `prepare_vit_attention_case.py` / `d3d12_vit_attention_test.cpp` / `block31-attention-effective.json`：把main E4M3与work FP16两半序列重排成64×1024 canonical Q/K/V，并在RX9070XT执行32-head softmax attention；correlation 0.8796。
- `vit_block31_reference.py` / `block31-portable.json`：64-token portable block31整链CPU oracle；Expand→Contract→QKV→Attention→Projection对5090最终输出correlation 0.8387，供D3D12多pass串联验收。
- `d3d12_vit_block31_test.cpp`：RX9070XT单device五pass完整block31；语义正确的main＋zero Attention路径一次Execute/Fence为15.476 ms，最终对5090 correlation 0.825232，各pass对CPU reference近逐位一致。
- `vit_blocks31_38_reference.py` / `vit-qkv-blocks31-38.json`：八个ViT block的portable串联参考与权威NVAPI-pair QKV参数；可导出canonical FP32及原repack可消费的2 MiB physical E4M3。
- `vit-expand-blocks31-38.json` / `block32–38-vit-expand-effective.f16`：其余ViT blocks的unit-basis＋small-Hadamard portable Expand矩阵；与QKV参数一起组成八层完整参数集。
- `stellar-amd-portable-vit.png`：portable blocks31–38→原decoder39–69→RX9070XT readout的当前诊断图；人物可辨但仍有点阵/横向色带，明确未达到最终验收。
- `run_original_vit_attention_match.cpp`：受控Q/K/V impulse通断扫描；精确恢复Attention的Q/K channel、Q→output token和K→V token bit对应。
- `block0-real-calibrated.bin` / `block4-real-calibrated.bin` / `stage2-real-calibrated.bin` / `encoder-real-calibration.json`：围绕当前《剑星》固定帧做checkerboard held-out校准的portable encoder桥；RX相关分别0.811/0.997/0.874，仅作真实分布校准证据，不冒充通用模型。
- `nvapi_chain_probe.cpp` / `inject_probe.cpp`：5090 resident只读NVAPI chain参数探针与延迟注入器；用于抓取block70真实0xb8 blob，当前等待交互式console会话恢复。
- `dlssnr_layer_oracle_probe.cpp` / `runtime_weight_d3d12_readback.cpp` / `verify_runtime_weight_arena.py`：从第一個live 1H launch恢复arena GPUVA，并经原生D3D12 resource追踪/readback导出147,719,680-byte runtime arena。全文件及153 records均与archive逐byte exact，已排除动态权重差异；当前转向逐层live activation定位encoder launch ABI/state偏差。
- `dlssnr_cubin_oracle_probe.cpp` / `capture_raw_buffer_cubin.inc`：在live NvAPI backend内追加自制raw-copy CUBIN，绕过不存在`ID3D12Resource`的私有activation。已同时导出block1 input/output/optional2完整64MiB资源，并恢复inpview物理偏移`+0x42800/+0x2800`；Spark同输入原CUBIN重放与5090 live output correlation 0.999915，确认encoder旧误差来自截断物理bank/view基址，而非动态权重。
- 同一probe现将block69返回后的64MiB raw allocation保存到atlas 400MiB，并把未覆盖的初始block1 input另存于528MiB；592MiB单次readback同时包含block48、block69和入口资源。两次独立启动的block69 SHA-256均为`735c39e1...c8494`，固定输入稳定且跨层谱系一致。
- 内部backend全量trace已找到真实post launch：seq154、grid481×273、参数0xb8；main=`qword0`且与block69 output同址，skip=`qword1`，layer weight=`qword3=arena+147429888`，`qword2=0xa003`是output surface handle。probe把skip保存到atlas 464MiB，并延后到post后统一readback。
- 上述ABI字段经standalone runner校正：`qword1`是block70 skip raw view，`qword2`才是最终CUDA surface。probe新增RGBA16F surface与RGBA texture采样kernel，可同时导出256×144 NVIDIA最终ROI及Color；有内容ROI原点为(2304,576)。live全局tile宽480、高272，main/skip不能沿用standalone的32×18 stride。
- `block70-live-head-correction.bin/.json`：live全局stride恢复后，AMD block70 body对NVIDIA最终RGB的固定帧33→3 channel-basis校正。checkerboard held-out RGB correlation 0.984375；RX9070XT完整拟合correlation 0.984697、MAE 0.007156，矩阵pass 0.607 ms。严格边界：输入仍是NVIDIA live block69 raw，71-block全AMD尚未完成。
- probe当前固定捕获有内容ROI `(2304,576,256,144)`；surface按RGBA16F读取，Color用texture object采样，block70 skip取raw allocation第二个64MiB page。该ROI对应post tile origin `(288,72)`，main/skip按live 480×272全局stridegather。
- `block66-live-correction.bin/.json`与`block69-to70-live-main-correction.bin/.json`：把正确72×128 content ROI的blocks66–69接入block70。两处checkerboard held-out correlation分别0.954105/0.975165；RX9070XT完整66–70链最终RGB correlation0.978812、MAE0.007868。边界前移为live block65 main＋enc0 skip输入。
- `decoder62-66-live-corrections.json`及四条matrix：修正block63/64单轴shift语义并逐层对齐62–66。RX9070XT完整62–70 content ROI最终RGB correlation0.962890、MAE0.013139；当前入口前移为live block61 main＋block8/enc0 skips。
- `decoder56-62-live-corrections.json`及六条matrix：逐层对齐56–60，并用下一层block62裁判绕过错误的block61 outview candidate。RX9070XT完整56–70 content ROI最终RGB correlation0.955783、MAE0.014460；当前入口为live block55 main＋block14/block8/enc0 skips。
- `decoder48-56-live-corrections.json`及八条matrix：使用完整136×240×256 frame校正48–54，并由full block56裁判block55 outview。block55→56 checkerboard held-out correlation0.957322；当前数值入口前移为live block47 main＋block22/block14/block8/enc0 skips。
- `decoder40-47-live-corrections.json`及七条matrix：同步捕获split-Swin layer3 output后，AMD完整68×120×512 blocks40–47逐层correlation为0.9626–0.9701；经48送入block49的下一层裁判为0.898400（live block47 baseline0.903055），入口前移为live block39 output。
- `block39-to40-live-correction.bin/.json`：live block38 repack main与block30 skip经archive grouped 1024→512和AMD block40后，下一层checkerboard held-out correlation0.986835；block39由此闭合，入口前移为live block38 main＋block30 skip。
- ViT live Projection完整捕获：blocks31–37 active终点均为2,211,839=`2160×1024-1`，即34×60 tokens；旧2MiB文件截断128KiB。block38的3MiB尾部为后续resource复用，权威active范围仍取前2,211,840 bytes。
- `d3d12_vit_block31_test.cpp`现支持由input长度推导动态token数；RX9070XT执行2160-token全局attention完整block31为272.918 ms。直接物理解码与live Projection corr≈0，channel affine亦无法恢复，缺口明确为ViT token-channel physical permutation，非算力或网络坍缩。
- `vit-repack-global-output-to-input.i32` / `vit-repack-global-permutation.json`：原repack CUBIN用23次address-bit launch恢复的2,211,840-entry exact全局映射；套live repack前后逐byte100% exact。前33个完整64KiB chunks解出2112 tokens。
- `vit-live-corrections.json`及blocks31–38八条matrix：首轮用33个完整64KiB chunks执行2112-token全局attention，逐层correlation 0.996737–0.998912；下一项全局映射随后补齐2160 tokens。
- `vit-global-canonical-to-2d.i32` / `vit-global-1d-to-canonical.i32`：由exact repack map与H36×W60×C1024 microcells合成的两条2,211,840-entry双射，补齐全部2160 tokens。RX9070XT完整31–38逐层cosine0.996325–0.998867，接block39/40下一层held-out correlation0.986846。
- `block23-live-correction.bin/.json`：encoder尾段以live block22 main为输入，H36×W60补到H40×W64后在RX9070XT执行block23；原始corr0.908986，checkerboard held-out0.957373。blocks22–30主线已启动。
- `encoder23-30-live-corrections.json`及blocks24–30 matrices：RX9070XT完成encoder blocks23–30；block30 512→1024 layer4后对逐byte闭合的repack input，checkerboard held-out correlation0.942102。严格入口前移为live block22 downsample main。
- 严格审计已修正上述旧geometry：blocks23–29实际为68×120×512，旧3MiB capture截断。5MiB同帧重抓后逐层held-out为0.930936→0.891634；block30执行68×120 body、34×60 pool、补齐36×60后512→1024，raw cosine0.925823、held-out0.939221。新输入进入ViT31沿用原校正仍有0.940372。
- `encoder15-21-live-corrections.json`及blocks15–21 matrices：同帧捕获H136×W240×C256 encoder中段，按真实none／XY／Y／X shift在RX9070XT串行执行；逐层checkerboard held-out correlation为0.951889→0.870840。block15矩阵另经AMD原生D3D12全量验证cosine0.952990；严格入口前移为live block14 downsample main，block21仍待block22下一层裁判。
- `encoder9-14-live-corrections.json`及blocks9–13、block14→15矩阵：H272×W480×C128主链按真实shift在RX9070XT执行，9–13 held-out correlation为0.971611→0.853233；block14错误physical candidate不作target，改由block15下一层裁判得到0.952174。由AMD block15连续重跑至block21仍有0.867947 correlation，严格入口前移为live block8 downsample main。
- `encoder5-8-live-corrections.json`及blocks5–7、block8→9矩阵：H544×W960×C64主链在RX9070XT执行，held-out correlation为0.976507/0.940912/0.881387；block8由block9下一层裁判得到0.970651。从live block4入口连续跑到block21仍保留0.723170 correlation，严格入口前移为live block4 downsample main。
- `encoder0-4-live-corrections.json`及block1→2、block2/3、block4→5矩阵：修正block0 allocation active offset为`0x42800`后，RX9070XT全分辨率主链相关为0.981801/0.981789/0.968062/0.975253。encoder主干严格入口已到固定block0数值输出；全链剩余硬边界是decoder仍引用block4/8/14/22四条live skip。
- `run_original_fused_live_replay.cpp` / `inspect_cubin_function.cpp`：以完整live D3D资源重放8H fused kernel并恢复精确written mask。block48前5MiB可由标准CUDA逐byte重放，但live实际选择`upsample_tilesync_fp8`，后3.36MiB只有NvAPI tile-sync协议能复现；main/skip/aux扩容至128MiB均不改变分叉，已排除缺页与标准cluster attribute。
- `d3d12_nvapi_fused_live.cpp`：5090独立D3D12/NvAPI fused快照宿主。旧Chain与官方ChainEx均可运行，但都只复现block48 written区66.8775%；证明游戏runtime的live tile-sync状态不由公开接口ID单独建立。下一步使用游戏唯一正常launch的原调用栈做原位controlled transaction。
- block48原位controlled transaction：在唯一正常高层launch前备份/替换原资源、launch后捕获、恢复并补跑正常block48，游戏下游输出保持逐byte exact。zero main/skip + `0xA5` output精确恢复2/3 update rows与1/3 preserved rows；updated区标准CUDA对live 100% exact，preserved区来自与block1 input同VA的复用buffer。
- `d3d12_block128_test.cpp`：通用AMD runner现按HWC坐标计算8×8 window key，并在`SHIFTED=1`时执行roll(-4)/inverse mapping；修复旧`tile=t/64`仅适用于预打包单tile数据的问题。
- `run_original_fused_channel_basis.cpp` / `run_original_fused_view_permutation.cpp` / `decode_tinlayout_global.py`：fused physical channel/view诊断工具。block49 controlled FFN Jacobian对archive-logical correlation 0.9612；global token映射仍为候选，不冒充最终canonical转换。
- `run_original_post.cpp` / `block70-post.json`：可重放block70原始0xb8 ABI；当前preset绑定`+0x38=Color RGBA`、`rgb_mode=1`、`input_scale=1/32`与独立FP16 `blend_scale=0.739746`，原始activation输出对source correlation 0.95876，portable block69输出对source correlation 0.8713。它取代灰色ABI图，成为最终画面验收oracle。
- `run_original_post_dataset.cpp`：复用单一CUDA context批量执行8×8 block70 main+skip联合样本；输入record为2048-byte main＋512-byte skip。128组与逐进程oracle逐float exact，耗时0.42秒；9,216组耗时0.70秒。另支持body weight one-hot／逐slot ablation及`features`模式；后者借controlled RGB head导出64×32 body feature，9,216组耗时5.47秒。
- block70 input impulse结论：main分配的2048 bytes中只有前512 bytes有效，skip为512/512有效；两路Jacobian rank均512，对应两路`4×4×32`输入共同upsample到`8×8×32`，不是先前假设的full-resolution main。
- `block70-operation-graph.json`：CPU descriptor builder直接导出的42步post运算图，并与普通block1的31步图逐项对齐；精确分解为5步post前缀＋标准Swin body（QKV前多padding）＋5步post后缀。
- `block39-operation-graph.json`：decoder入口的权威4步图`convolution→convolution→mul→add`；record含262,656 FP16 elements，主体262,144=`512×512`，更符合两组512输入的grouped 1024→512卷积，剩余512属于skip/scale支路。
- `run_original_block39_basis.cpp`：同context结构basis/Hadamard runner；恢复出每16KiB bank前8KiB有效、16个512→512 connected components。由于原CUBIN消费的runtime packed weights与archive FP16不一致，其数值输出明确降级为结构证据。
- `build_block39_logical.py` / `block39-logical-effective.bin` / `.json`：按descriptor groups=2把archive FP16展开为1536→512稀疏矩阵，覆盖1024-channel main与512-channel depthwise skip；RX9070XT固定输入0.836 ms，对CPU logical oracle cosine 0.999999999993。
- `block39_spatial_reference.py`：固定帧8×8→16×16 decoder入口；grouped main投影后2×上采样，并融合block30未池化16×16 skip，供真实256-token blocks40–47链使用。
- `block40-operation-graph.json` / `split_swin512_reference.py`：闭合decoder blocks40–47的gated FFN与16-head attention archive逻辑；从block39 logical输出串行跑完八层全部finite，std由1.597平滑降至0.952。
- `pack_split_swin512.py` / `d3d12_block128_test.cpp` split512 mode / `split-swin512-amd.json`：RX9070XT三pass执行blocks40–47；完整16×16空间链每层22–24.6 ms，最终block47对CPU archive logical correlation 0.99207。
- `block48-operation-graph.json` / `block48_reference.py`：恢复512→256 upsample＋8H Swin全部archive tensor；修正QKV后字段+8-half偏移后main-only logical输出finite、std0.0877。canonical block22 skip尚待上游AMD提供。
- `pack_fused_swin256.py` / `d3d12_block128_test.cpp` fused256 mode / `fused-swin256-amd.json`：RX9070XT三pass执行block48 body及49–55；各层对CPU archive logical correlation约0.99934–0.99945，15.8–18.6 ms。
- `pack_fused_swin128_archive.py` / `fused-swin128-archive-amd.json`：corrected archive offsets的128-channel pack；block56 main-only在RX9070XT为38.709 ms，对CPU absolute MAE 1.13e-4。canonical block14 skip待接入。
- `run_original_fused_view_permutation.cpp`：补齐upsample ABI中的独立skip allocation/binding；原runner把`Params.skip`留空，会在4H/2H/1H prefix读取时触发CUDA illegal access。block56的720种空间bit排列已穷举，现有`(3,0,1,4,5,2)`为并列最优，排除token位序是0.59相关性的主因。
- block56 fixed-frame channel correction：以完整36×64×128 NVIDIA oracle拟合、checkerboard空间留出验证后在RX9070XT执行129→128 affine；held-out correlation 0.93016，全图correlation由0.59069升至0.94555，GPU对CPU MAE 6.88e-6，耗时0.810 ms。它是固定输入验收用校正，不宣称为通用帧参数。
- `pack_fused_swin64_archive.py` / `fused-swin64-amd.json`：block62–65 corrected 64-channel archive pack与AMD三pass；每层1.9–2.44 ms，最终绝对MAE 5.42e-6。canonical block8 skip待接入。
- `pack_fused_swin32_archive.py` / `fused-swin32-amd.json`：block66–69 corrected 32-channel archive pack；main-only CPU信号降至1e-6，block66 AMD按E4M3正确量化为全零，证明canonical block4 skip是最终链硬依赖。
- `block70-attention-effective.bin` / `.json` / `pack_post_attention.py`：以权威prefix输出构造attention-only oracle后拟合的Q/K/V、projection、bias、shared skip/gain与scale；独立held-out correlation 0.97703。
- `make_post_attention_compatible.py` / `block70-attention-block1-compatible.bin`：把shared skip代数吸收为`P'=P×skip`、`residual'=skip²`，直接复用AMD 1H runner；RX单tile对NVIDIA correlation 0.97252，对CPU effective 0.999615，1.806 ms。
- `block70-ffn-effective.bin` / `.json` / `pack_post_ffn.py`：双residual identity读口恢复的完整32-channel FFN；held-out correlation 0.93751。
- `make_post_body_compatible.py` / `block70-body-compatible.bin` / `.json`：合并FFN与attention后由RX9070XT完整执行post body；单tile1.038 ms，对NVIDIA full-body correlation 0.90377。
- block70全图后半链历史验收：曾以NVIDIA导出的5步prefix为输入，RX9070XT执行body＋RGB head得到correlation 0.94537；现已被下一项完整AMD prefix取代。
- `fit_post_global_skip.py` / `block70-prefix-global-skip-effective.bin` / `.json` / `prepare_post_global_prefix.py`：恢复full-dim skip双bank矩阵与main/skip地址gather。RX9070XT批量576 tiles的main/skip prefix均与CPU portable逐float exact；block70由原physical inputs起全AMD执行，最终RGB correlation 0.94536。
- `fit_post_outconv.py` / `block70-outconv-effective.bin` / `.json`：从controlled head basis恢复block70最后的32→RGB矩阵；两组16-channel packed block各占256 FP16 slots、每组仅48个有效。独立held-out correlation 0.9999991、MAE 2.88e-7。
- `fit_post_upsample.py` / `block70-upsample-effective.bin` / `.json`：以1024-row Hadamard恢复两路`4×4×32`到`8×8×16` controlled-identity odd-half映射；小幅／真实幅度held-out correlation 0.998775／0.998482。该坐标可能已含post `out_gain`，不再过度命名为纯upsample内部值。
- `fit_post_prefix.py` / `block70-prefix-effective.bin` / `.json`：以双residual identity读口恢复权威5步post前缀的`1024→2048`稀疏矩阵；小幅／真实幅度held-out correlation 0.9999987／0.99999994。它取代旧odd-half map，输出可直接进入标准1H body。
- `d3d12_post_upsample_test.cpp`：RX9070XT通用执行portable `1024→N`矩阵；权威5步prefix的N=2048真实幅度held-out为0.531 ms、cosine 0.999999931。
- `d3d12_post_upsample_test.cpp`现亦支持`[spatial-width phase-period]`，按样本的窗口内phase选择独立矩阵。block62的8×8 phase校正在RX9070XT为0.789 ms，block66为0.684 ms；分别对同帧层级oracle达到0.86294与0.96993。该模式用于恢复physical dynamic-scale的固定帧残差。
- `d3d12_post_outconv_test.cpp`：RX9070XT直接执行portable 32→RGB与`Color + residual`；独立held-out tile MAE 2.17e-7。256×144全图用controlled body feature对NVIDIA oracle：1.060 ms、RGBA MAE 4.95e-5、max error 0.001531；8-bit RGB 98.32% channels exact、最大1 LSB。
- `d3d12_vit_qkv_test.cpp`：RX9070XT QKV correctness runner；执行Q/K逐head归一化与V线性投影，对5090权威view correlation 0.9820/0.9717/0.9918。
- `d3d12_vit_linear_test.cpp`：通用AMD ViT线性层runner；支持SASS FFN多项式与residual skip。block31 Contract/Projection correlation 0.9298/0.9725。
- `run_original_vit_attention.cpp`：显式携带 QKV 更新后的 work/aux，独立运行原 Attention；block31 输出65,536 bytes、零NaN。
- `run_original_fused_exact.cpp`：按5090 live 0x58 blob执行8H/4H/2H fused body，包含halo grid与aux view。
- `run_original_1h_upsample.cpp`：按block66真实0x60 ABI执行1H upsample；`+0`绑定decoder主输入、`+8`绑定输出，保留enc0 skip与override dimensions。
- `export_weight_records.py` / `e4m3_to_f32.py` / `sanitize_e4m3.py`：权重record导出、E4M3物理view转换与近似桥NaN饱和工具。
- `block0-distilled.bin` / `block0-distilled.json`：由 8,192 个原 CUBIN tiles 蒸馏出的首版 portable pre-block MLP；直接输出 NVIDIA physical tile view，供 RX 9070 XT 接入 block1–3。它是推进端到端链路的近似入口，不替代原 CUBIN oracle。
- `block4-distilled.bin` / `block4-distilled.json`：围绕真实 stage3 activation 蒸馏的 `2048→512` compact-downsample bridge；held-out correlation 0.9939，等待 HLSL 落地。
- `d3d12_block4_bridge.cpp`：RX 9070 XT 三 pass compact-downsample bridge；held-out correlation 0.9940，真图对原 block4 CUBIN correlation 0.9972。
- `stage2-distilled.bin` / `stage2-distilled.json` / `d3d12_stage2_bridge.cpp`：把四个 block4 compact tiles 映射到 block7 的 8×8×64 physical tile；AMD 真图链 correlation 0.9904。
- `block10-effective.bin` / `block10-effective.json`：128-channel 四头 Swin 的语义级 FFN／QKV／cosine-attention／projection 参数；完整 block 对原 CUBIN correlation 0.99485。
- `block11-effective.bin` / `block12-effective.bin` / `block13-effective.bin` / `effective-4h128.json`：full-block 联合校准后的其余 4H blocks；correlation 0.9951–0.9959。
- `d3d12_block128_test.cpp`：RX 9070 XT 的通用 4H/128-channel correctness runner；首版 naive attention 重算 QKV，仅用于语义验收。
- `block8-downsample-effective.bin` / `block8-downsample-effective.json`：block8 的 main→compact 线性 downsample；含 physical token/channel mixing，held-out correlation 0.9988。
- `tinlayout-2h64-input-permutation.i32` / `tinlayout-2h64-output-permutation.i32` / `tinlayout-2h64-permutation.json`：4096-basis CUBIN scan 恢复的完整 2H/64-channel token+channel unswizzle；64 个对齐 Jacobian correlation 全为 1.0。
- `fp8-weight-layout-evidence.json`：2H SASS 把 weight view 当 E4M3 packed/swizzled 数据消费的直接证据；archive 外层按两字节计数且 `blend_scale` 确为 FP16，但矩阵 payload 是否由 NvAPI 上传时转换仍待与 auxiliary-view 缺口一起判定。
- `unpack_mma_fragments.py`：按 NVIDIA PTX 9.1 `mma.m16n8k32` matrix-B 官方公式解包／回包 lane-major E4M3 subtiles；block8 W1 的 8,192 loaded bytes 可逐 byte roundtrip。
- `infer_vit_layouts.py` / `vit-layouts.json`：闭合 blocks31–38 五类 ViT record 的字节分区；把 4 MiB Expand、4 MiB Contract、3 MiB QKV、1 MiB Projection 主体明确标为 packed E4M3，并分离 FP16 residual skip、padding 与尚待 SASS 定位的 128-byte attention scale region。
- `run_original_vit_repack_permutation.cpp` / `vit-repack-output-to-input.i32`：用 21 个地址位平面在 22 次原 CUBIN launch 内恢复完整 8×8×1024 ViT 2D→1D physical byte permutation；65,536 个源地址一一对应。
- `run_original_vit_expand_basis.cpp` / `run_original_vit_expand_matrix.cpp` / `run_original_vit_expand.cpp`：ViT Expand 的稀疏 basis、完整矩阵提取与任意输入 held-out oracle；basis 支持集恢复出 32-token main view 的 token/channel 物理位分解。
- `block31-vit-expand-effective.f16` / `.json`：unit-basis 与小幅 Hadamard 联合恢复的首个 1024→4096 portable ViT Expand 矩阵；8 组稀疏到稠密 held-out correlation 0.9840，仅作 AMD bring-up bridge，不替代 raw packed-weight exact unswizzle。
- `d3d12_vit_expand_test.cpp`：RX 9070 XT 的 1024→4096 ViT Expand correctness runner；直接读取 portable FP16 matrix，首个 Spark CUBIN held-out MAE 0.00664、submit→fence 0.600 ms。
- `unpack_vit_matrices.py`：按 PTX matrix-B fragment 公式、K-block-major/N-block-minor tile 顺序及两套 channel bit permutation，直接把 blocks31–38 的 Expand／Contract／QKV／Projection packed E4M3 解成 portable FP16；QKV 按三个独立 1024-wide 输出组重排。block31 raw Expand 对 CUBIN basis 全矩阵 correlation 0.999399。
- `block1-effective.bin` / `block1-effective.json`：由原始 CUBIN oracle 拟合并 held-out 验证的 block1 row-major FFN／双流 cosine-attention 参数。
- `block2-effective.bin` / `block3-effective.bin` / `effective-1h32.json`：同法恢复的后两个 32-channel blocks；manifest 同时记录 tensor layout、held-out 误差与 shifted-window 的 roll／mask 规则。
- `d3d12_block1_test.cpp`：RX 9070 XT 两 pass HLSL block1 runner；对 256 个 held-out tiles 与 NVIDIA CUBIN oracle 做逐元素误差验收。
- `d3d12_preblock_test.cpp`：RX 9070 XT 三 pass HLSL pre-block surrogate runner；支持 held-out CUBIN 验收与输出 physical-view FP32 activation。
- `block0-input-adapter-preview.png`：RX 9070 XT 用真实 `7×32 input_adapter_weight` 执行 HLSL 投影后生成的首张 learned-feature 诊断图。
- `block0-depthwise-preview.png`：在 input adapter 后继续执行真实 `[32,3,3]` depthwise convolution 的 AMD 输出；颜色变化较弱但非恒定图。
- `stellar-block0-rgb-only.png`：以《剑星》Steam hero RGB 为输入、Gaussian 四通道置零时，RX 9070 XT 经过 input adapter + depthwise 后的首张可辨认模型中间图。
- `stellar-block0-ffn-residual.png`：继续执行 SASS 恢复的 FFN 多项式和 `input*ffn_cos_skip + branch` 后的 AMD block0 中间图。
- `stellar-end-to-end-first.png` / `final-readout.bin` / `final-readout.json`：block69 physical activation 的首张清晰end-to-end診斷圖與舊linear readout；其混合公式已被block70零activation實驗否決，只保留為歷史診斷資產。
- `d3d12_final_readout.cpp` / `stellar-end-to-end-amd.png`：RX 9070 XT 的旧physical-tile linear诊断读出；256×144 submit→fence 0.976 ms。block70零activation实验已证明其blend公式不是真实post合同，不能用于最终验收。
- `d3d12_nvapi_repack_test.cpp` / `d3d12_nvapi_vit_chain.cpp`：自建5090 D3D12/NVAPI CUBIN宿主；最小repack已与Spark逐字节一致，完整ViT chain用于恢复blocks31–38精确输出。
- `nvapi_chain_probe.cpp`：只读记录NVAPI `CreateCuFunction` handle映射和`LaunchCuKernelChainEx`真实子kernel数组，取代standard/chained排列猜测。
- `stellar-amd-current.png`：新恢复链在RX9070XT上的当前RGB；decoder已零NaN贯通，但因blocks32–38暂用identity近似仍有明显tile条纹，不能视为最终验收图。
- `stellar-global-cubin-post-abi.png`：旧的灰色block70 ABI诊断图；真实颜色路径现已由`run_original_post.cpp`恢复，本图仅保留为故障过程证据。
- `build_network_graph.py`：把 CPU descriptor builder 恢复出的 71-block 顺序与 `weights-index.json` 合并，生成可复现的结构索引。
- `network-graph.json`：已恢复的 block 类型、layer 家族、宽度、角色、权重记录与 block-to-block 边；block0 的外部纹理绑定尚未恢复，保持 `null`。
- `weight-names.json`：通过 Windows 直接调用 DLL 内部 descriptor builder 导出的 653 个内部权重名字；从 `input_adapter_weight` 到 `out_conv_weight`／`blend_scale`。
- `probe_runtime_descriptor.ps1`：Windows 只读运行时探针；直接调用内部 CPU descriptor/network 构造函数，导出 71-block descriptor、153 个 live Layer 与 653 个权重名，不调用 GPU backend。
- `extract_embedded_cubins.py`：从 DLL `.data` 中按 ELF section/program header 边界提取 15 个独立 CUBIN，输出 SHA-256 manifest，供 `nvdisasm`／`cuobjdump` 使用。
- `ghidra/`：headless Ghidra 导出脚本；按地址批量导出构图函数和 layer factory 的伪 C，不依赖 GUI 操作。
- `model-overview.svg`：正文结构图；只画二进制能支撑的骨架，不能当作 NVIDIA 完整执行图。
- `amd-port-plan.md`：从权重格式、执行图、离线出图到 AMD GPU 实时化的分阶段实验路线；每一步都附验收标准和可成文选题。

296 期的样本哈希、PE 资源树和许可证基础分析见 `../296/`。
