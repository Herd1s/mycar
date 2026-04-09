from __future__ import annotations

from dataclasses import dataclass


@dataclass
class Citation:
    key: str
    title: str
    source: str
    year: int
    traceable: bool


class CitationValidator:
    def validate(self, citations: list[Citation]) -> list[str]:
        issues: list[str] = []
        for c in citations:
            if not c.traceable:
                issues.append(f"[{c.key}] 不可追溯：请补充 DOI/URL/馆藏信息")
            if c.year < 1900:
                issues.append(f"[{c.key}] 年份异常：{c.year}")
            if not c.source.strip():
                issues.append(f"[{c.key}] 来源缺失")
        return issues
