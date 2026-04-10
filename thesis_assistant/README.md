# 学位论文研究与规范编排助手（MVP）

> 目标：在不替代作者学术判断的前提下，提供“资料整理—结构规划—章节草拟—规范检查—导出”的闭环工具。

## 功能覆盖

- 多模型 API 路由与成本控制（可指定模型，超预算阻断）
- 基于资料摘要的论文框架规划
- 基于资料的章节草拟（自动标记关键结论需来源）
- 图、表、公式规范化命名与编号建议
- 引用可追溯性校验
- 结构格式检查（规则底座：GB/T 7713.1—2025，替代 2006 版）
- 导出 Word/PDF（当前 MVP 以文本占位导出，便于后续接入 Pandoc/Office）

## 重要合规原则

1. **最终论文内容必须由作者确认。**
2. **所有关键结论必须有来源，且可追溯、可复核。**
3. **系统输出仅作为写作辅助，不构成学术结论背书。**

## 快速开始

```bash
cd thesis_assistant
PYTHONPATH=src python main.py \
  --topic "多传感器融合下轮腿机器人运动控制研究" \
  --materials "文献A摘要" "文献B实验数据说明" \
  --budget 3.0
```

输出文件默认位于 `outputs/`：

- `thesis_assistant_output.docx`
- `thesis_assistant_output.pdf`

## 目录结构

```text
thesis_assistant/
├── main.py
├── pyproject.toml
├── README.md
└── src/thesis_assistant/
    ├── citation.py
    ├── exporter.py
    ├── formatter.py
    ├── gbt_rules.py
    ├── models.py
    └── pipeline.py
```

## 下一步建议

- 接入真实模型提供方（OpenAI/Anthropic/本地模型）与统一 Token 统计
- 增加 RAG（文献段落检索）与“结论-证据”双向映射
- 对接 `.docx` 模板库，实现页眉页脚、题注、目录、参考文献自动排版
- 增加参考文献去重、格式自动修复（GB/T 7714）
