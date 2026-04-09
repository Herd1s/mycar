from __future__ import annotations

from dataclasses import dataclass

from .gbt_rules import RuleBase


@dataclass
class FormatReport:
    passed: bool
    missing_sections: list[str]
    warnings: list[str]


class ThesisFormatter:
    def __init__(self, rules: RuleBase):
        self.rules = rules

    def check_structure(self, sections: list[str]) -> FormatReport:
        missing = [s for s in self.rules.thesis_required_sections if s not in sections]
        warnings: list[str] = []
        if "正文" in sections and "参考文献" not in sections:
            warnings.append("正文已存在但缺少参考文献章节")
        return FormatReport(passed=not missing, missing_sections=missing, warnings=warnings)

    def normalize_equation(self, formula: str, chapter: int, index: int) -> str:
        return f"{formula}\\tag{{{chapter}-{index}}}"

    def normalize_caption(self, kind: str, chapter: int, index: int, text: str) -> str:
        prefix = "图" if kind == "figure" else "表"
        return f"{prefix}{chapter}-{index} {text}"
