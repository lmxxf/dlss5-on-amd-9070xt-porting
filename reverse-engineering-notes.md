# DLSSNR 模型结构逆向记录

这份记录只收 297 期相对 Hikari 初稿新增的二进制证据与方法。296 期已经验证的 PE 分段、资源树、SHA-256 和许可证放在 `../296/`，这里不重复造一份。

## 1. Hikari 初稿停在哪里

初稿从以下字符串拼出四级 U-Net：

- `cc_tinlayout_fused_swin_1h_32_1`
- `cc_tinlayout_fused_swin_2h_64_2`
- `cc_tinlayout_fused_swin_4h_128_4`
- `cc_tinlayout_fused_swin_8h_256_8`
- `CCVitAttention`
- `UpsampleSkip`

方向基本对，但漏了 512、1024 两档，并把“DLL 里编译了这些 kernel”直接写成了“模型按这个顺序执行”。

## 2. 完整宽度阶梯

继续归并 kernel 名称，可以得到六档：

| 档位 | 直接证据 | 能读出的数字 |
|---|---|---|
| 1 | `cc_tinlayout_fused_swin_1h_32_1` | 1 head，32 通道 |
| 2 | `cc_tinlayout_fused_swin_2h_64_2` | 2 heads，64 通道 |
| 3 | `cc_tinlayout_fused_swin_4h_128_4` | 4 heads，128 通道 |
| 4 | `cc_tinlayout_fused_swin_8h_256_8` | 8 heads，256 通道 |
| 5 | `cc_split_swin_16h_*_512` | 16 heads，512 通道 |
| 6 | `CCVitAttentionLayer specialized for Cin=1024` | ViT Attention 输入宽度 1024 |

第五档从 `fused` 变成 `split`：能确认它被拆成 QKV、projection、feed-forward 等多个核心；“因为宽度上升导致融合不合算”只是合理工程解释，DLL 没有留下原因说明。

## 3. 1024 → 512 的回程是实锤

DLL 同时包含：

- `CCDecInputUpsampleLayer currently specialized for 1024->512 (dec5)`
- `cc_dec_input_upsample_1024_512`

它们把 1024 通道瓶颈和 512 通道解码入口直接接了起来。`dec5` 这个内部名字也说明开发者把它当作解码路径的一部分，但不能只靠名字推出总共有几级 decoder。

## 4. U 形与跳接的直接证据

32、64、128、256 四个 fused Swin 家族都有 `_ds` 与 `_upsample` 变体，分别对应下采样和上采样。更硬的是运行时报错：

```text
CCTinlayoutFusedSwin1HLayer (upsample) requires 2 inputs (main + skip)
CCTinlayoutFusedSwin2HLayer (upsample) requires 2 inputs (main + skip)
CCTinlayoutFusedSwin4HLayer (upsample) requires 2 inputs (main + skip)
CCTinlayoutFusedSwin8HLayer (upsample) requires 2 inputs (main + skip)
CCDecInputUpsampleLayer requires 2 inputs (main, skip)
CCTinlayoutFusedPostBlockSwin1HLayer requires 2 inputs (main + enc0 skip)
```

这里的 `main + skip` 和 `enc0 skip` 是程序自己对输入槽位的命名。它们支持“编码端特征会送到解码端”的 U-Net 式跳接，不再只是看到 `UpsampleSkip` 后按常识补图。

## 5. `WEIGHTS_HT` 不是无名浮点流

权重资源文件偏移 `0x114a160`。开头 8 字节的小端整数就是资源总大小 147,695,410；接下来 8 字节是第一条名字长度 19，随后正好是 19 字节的：

```text
block0.layer0.layer
```

随后是若干长度、类型字段与权重数据。旧版通过扫描记录名前缀，得到 `block0` 到 `block70`，编号连续，共 71 个 block；部分 block 下还有 `layer1` 到 `layer4`。

顺序 parser 进一步确认：资源开头 8 字节为总长，此后每条记录都是 `name_length → name → body_span → body`。按 `body_span` 跳转，不搜索下一条字符串，可以连续解析 153 条记录并精确落到资源末尾。旧正则统计的 152 条漏掉了 `block70.layer0.blend_scale`。

153 条记录还共同满足：

- `body_span = payload_size + 40`；
- `payload_size = element_count × 2`；
- 类型码恒为 1；
- payload 按 IEEE FP16 解码时，抽样记录全部为有限数；
- 单元素记录 `block70.layer0.blend_scale` 解码为 `0.73974609375`。

因此，当前样本的**存储权重是 FP16**；文件中的 `_fp8` kernel 名称描述 GPU 执行路径，不能反推嵌入权重也按 FP8 保存。全部记录合计约 7380 万个 FP16 标量。加载阶段如何把 FP16 权重转换／重排给 FP8 kernel，仍需从 CPU 初始化逻辑确认。

这说明权重包至少有 71 个序列化分组，但不能写成“模型有 71 层”：

- serialization block 可能对应一个算子、一组融合算子或一个复合模块；
- `layer0...layer4` 也只是记录子项，不等于 Transformer layer；
- 名字没有携带 Swin/ViT/Conv 类型。

脚本会输出每个 block 出现的 layer 编号和首次文件偏移，方便继续分析记录头格式及各 block 的数据长度。

## 6. 权重规模暴露出的镜像分段

把记录按数值 block 编号重排并汇总元素数，会出现高度规则的镜像结构：

| 候选段 | block | 每个 block 的 FP16 标量数 | 证据强度 |
|---|---:|---:|---|
| 输入／前处理 | 0 | 10,848 | 仅权重规模 |
| 32 通道主体 | 1–3 | 10,336 | 规模重复 + 已知 32 kernel |
| 32→64 过渡 | 4 | 11,360 | 位于两组重复块之间 |
| 64 通道主体 | 5–7 | 30,880 | 规模重复 + 已知 64 kernel |
| 64→128 过渡 | 8 | 34,968 | 位于两组重复块之间 |
| 128 通道主体 | 9–13 | 98,592 | 规模重复 + 已知 128 kernel |
| 128→256 过渡 | 14 | 114,968 | 位于两组重复块之间 |
| 256 通道主体 | 15–21 | 344,616 | 规模重复 + 已知 256 kernel |
| 256→512 过渡 | 22 | 410,144 | 位于 256 与 split-Swin 之间 |
| 512 split-Swin | 23–29 | 984,096 | 每块四条 layer 记录 |
| 512→1024 候选入口 | 30 | 1,246,248 | 比相邻 512 block 多一条 layer4 |
| 1024 ViT 瓶颈 | 31–38 | 6,293,577 | 八个完全同构的五记录大块 |
| 1024→512 解码入口 | 39 | 262,656 | 与 `cc_dec_input_upsample_1024_512` 直接证据对齐 |
| 512 split-Swin 回程 | 40–47 | 984,096 | 八个四记录块，与 23–29 镜像 |
| 512→256 过渡 | 48 | 410,392 | 位于 512 与 256 重复块之间 |
| 256 通道回程 | 49–55 | 344,616 | 与 15–21 镜像 |
| 256→128 过渡 | 56 | 115,088 | 位于两组重复块之间 |
| 128 通道回程 | 57–61 | 98,592 | 与 9–13 镜像 |
| 128→64 过渡 | 62 | 35,024 | 位于两组重复块之间 |
| 64 通道回程 | 63–65 | 30,880 | 与 5–7 镜像 |
| 64→32 过渡 | 66 | 11,392 | 位于两组重复块之间 |
| 32 通道回程 | 67–69 | 10,336 | 与 1–3 镜像 |
| 输出／混合 | 70 | 10,905 | 唯一带 `blend_scale` 的 block |

这张表比单纯按字符串画 U 形更强：block 编号、参数规模与六档宽度共同形成镜像。但除 `block39` 可与明确的 `1024→512` 名字强对齐外，其余 block type 仍是候选映射，要由 `build_blocks` 的派发表最终确认。

## 7. 三张纸还缺一张映射表

现在掌握的是：

| 证据 | 回答什么 | 不回答什么 |
|---|---|---|
| kernel 名称 | 编译进 DLL 的算子、宽度和实现变体 | 当前 preset 实际调用顺序 |
| 权重记录名 | block 数量、子记录和文件位置 | 每个 block 属于哪类网络模块 |
| 报错字符串 | 某些层要求哪些输入，如 main/skip | 整张执行图 |

DLL 里的 `CG2RNetworkManager::BuildActiveNetwork`、`CCNetwork::build_blocks`、`ResolvePresetToDescriptor` 表明运行时存在“网络描述符 → 构图”的步骤。下一步真正值钱的目标是：

1. 找到 network descriptor 的存储位置或构造函数；
2. 还原 block type 的枚举／字符串派发表；
3. 建立 `blockN → block type → kernel family → 输入输出` 映射；
4. 再谈精确层数、每级 block 数和完整执行顺序。

## 8. 复现

```bash
python3 wechat/assets/297/extract_model_evidence.py \
  /mnt/c/Users/lmxxf/Downloads/nvidia/nvngx_dlssnr.dll
```

原始字符串快速查看：

```bash
strings -a -n 4 /mnt/c/Users/lmxxf/Downloads/nvidia/nvngx_dlssnr.dll \
  | rg -i 'swin_[0-9]+h|vit_attention|1024_512|main.*skip|enc0 skip|block type'
```

边界：`strings` 是证据侦察工具，不是反编译器。它能告诉我们“有哪些名字”，不能独自恢复控制流。
