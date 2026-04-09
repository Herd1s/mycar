from dataclasses import dataclass


@dataclass(frozen=True)
class RuleBase:
    standard: str
    replaces: str
    thesis_required_sections: tuple[str, ...]
    citation_style: str
    equation_numbering: str


GB_T_7713_1_2025 = RuleBase(
    standard="GB/T 7713.1—2025",
    replaces="GB/T 7713.1—2006",
    thesis_required_sections=(
        "封面",
        "中文摘要",
        "英文摘要",
        "目录",
        "符号说明（如有）",
        "正文",
        "参考文献",
        "附录（如有）",
        "致谢（如有）",
    ),
    citation_style="GB/T 7714",
    equation_numbering="按章编号，例如 (3-2)",
)
