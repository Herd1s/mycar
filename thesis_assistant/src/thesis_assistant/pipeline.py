from __future__ import annotations

from dataclasses import dataclass

from .citation import Citation, CitationValidator
from .formatter import ThesisFormatter
from .gbt_rules import GB_T_7713_1_2025
from .models import BudgetController, MockModel, ModelRouter


@dataclass
class ThesisRequest:
    topic: str
    materials: list[str]
    preferred_model: str | None = None


class ThesisAssistantService:
    def __init__(self, max_budget: float = 5.0):
        models = [
            MockModel("fast-cheap", input_cost_per_1k=0.001, output_cost_per_1k=0.002),
            MockModel("balanced", input_cost_per_1k=0.003, output_cost_per_1k=0.006),
            MockModel("quality", input_cost_per_1k=0.01, output_cost_per_1k=0.02),
        ]
        self.router = ModelRouter(models=models, budget=BudgetController(max_cost=max_budget))
        self.formatter = ThesisFormatter(GB_T_7713_1_2025)
        self.citation_validator = CitationValidator()

    def plan_outline(self, req: ThesisRequest) -> str:
        prompt = (
            f"为题目《{req.topic}》生成论文框架，需符合{GB_T_7713_1_2025.standard}。"
            "输出需区分：问题定义、原理分析、方法、实验、结论与展望。"
        )
        return self.router.run(prompt, preferred=req.preferred_model)

    def draft_section(self, section_title: str, req: ThesisRequest) -> str:
        prompt = (
            f"请基于资料摘要生成章节草稿：{section_title}。"
            f"资料：{'；'.join(req.materials)}。"
            "每个关键结论必须标注[需来源]，并提醒作者最终确认。"
        )
        return self.router.run(prompt, preferred=req.preferred_model)

    def suggest_fig_table_formula(self, chapter: int) -> dict[str, str]:
        return {
            "figure": self.formatter.normalize_caption("figure", chapter, 1, "系统总体架构图"),
            "table": self.formatter.normalize_caption("table", chapter, 1, "实验参数设置"),
            "equation": self.formatter.normalize_equation(r"E=mc^2", chapter, 1),
        }

    def verify(self, sections: list[str], citations: list[Citation]) -> dict[str, object]:
        format_report = self.formatter.check_structure(sections)
        citation_issues = self.citation_validator.validate(citations)
        return {
            "format_passed": format_report.passed,
            "missing_sections": format_report.missing_sections,
            "warnings": format_report.warnings,
            "citation_issues": citation_issues,
        }
