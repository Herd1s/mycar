from __future__ import annotations

from pathlib import Path


def export_word(content: str, target: Path) -> Path:
    target.write_text(content, encoding="utf-8")
    return target


def export_pdf(content: str, target: Path) -> Path:
    # MVP 阶段先导出文本占位；生产版可接入 pandoc/office 转换链路。
    target.write_text(content, encoding="utf-8")
    return target
