from __future__ import annotations

import argparse
import json
from pathlib import Path

from thesis_assistant.citation import Citation
from thesis_assistant.exporter import export_pdf, export_word
from thesis_assistant.pipeline import ThesisAssistantService, ThesisRequest


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="学位论文研究与规范编排助手")
    parser.add_argument("--topic", required=True, help="论文题目")
    parser.add_argument("--materials", nargs="*", default=[], help="资料摘要")
    parser.add_argument("--model", default=None, help="指定模型路由名称")
    parser.add_argument("--budget", type=float, default=5.0, help="最大预算")
    parser.add_argument("--out", type=Path, default=Path("outputs"), help="输出目录")
    return parser


def main() -> None:
    args = build_parser().parse_args()
    service = ThesisAssistantService(max_budget=args.budget)
    req = ThesisRequest(topic=args.topic, materials=args.materials, preferred_model=args.model)

    outline = service.plan_outline(req)
    chapter = service.draft_section("第1章 绪论", req)
    suggestions = service.suggest_fig_table_formula(chapter=1)

    verify_report = service.verify(
        sections=["封面", "中文摘要", "英文摘要", "目录", "正文", "参考文献"],
        citations=[Citation("ref1", "示例文献", "CNKI", 2024, True)],
    )

    output_text = "\n\n".join(
        [
            "# 论文框架规划\n" + outline,
            "# 章节草拟\n" + chapter,
            "# 图表公式建议\n" + json.dumps(suggestions, ensure_ascii=False, indent=2),
            "# 规范与引用校验\n" + json.dumps(verify_report, ensure_ascii=False, indent=2),
            "# 合规声明\n最终论文内容必须由作者确认；关键结论需提供来源并可追溯、可复核。",
        ]
    )

    args.out.mkdir(parents=True, exist_ok=True)
    word_path = export_word(output_text, args.out / "thesis_assistant_output.docx")
    pdf_path = export_pdf(output_text, args.out / "thesis_assistant_output.pdf")
    print(f"已导出: {word_path} / {pdf_path}")


if __name__ == "__main__":
    main()
