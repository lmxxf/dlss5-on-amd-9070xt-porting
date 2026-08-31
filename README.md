# 297 期模型结构分析资料

- `reverse-engineering-notes.md`：相对 Hikari 初稿新增的宽度阶梯、skip 证据、71 个权重 block 与下一步逆向路线。
- `extract_model_evidence.py`：零依赖证据提取脚本，输出 kernel 家族、直接报错证据、权重 block/layer 编号和偏移。
- `model-overview.svg`：正文结构图；只画二进制能支撑的骨架，不能当作 NVIDIA 完整执行图。
- `amd-port-plan.md`：从权重格式、执行图、离线出图到 AMD GPU 实时化的分阶段实验路线；每一步都附验收标准和可成文选题。

296 期的样本哈希、PE 资源树和许可证基础分析见 `../296/`。
