from __future__ import annotations

from dataclasses import dataclass
from typing import Protocol


class ModelAdapter(Protocol):
    name: str
    input_cost_per_1k: float
    output_cost_per_1k: float

    def generate(self, prompt: str) -> str:
        ...


@dataclass
class MockModel:
    name: str
    input_cost_per_1k: float
    output_cost_per_1k: float

    def generate(self, prompt: str) -> str:
        return (
            f"[模型:{self.name}] 已生成草稿（示例）。\\n"
            "注意：以下内容仅供作者修改确认，关键结论必须补充可核验来源。"
        )


@dataclass
class BudgetController:
    max_cost: float
    spent: float = 0.0

    def estimate(self, in_tokens: int, out_tokens: int, model: ModelAdapter) -> float:
        return (in_tokens / 1000) * model.input_cost_per_1k + (out_tokens / 1000) * model.output_cost_per_1k

    def ensure_budget(self, estimate_cost: float) -> None:
        if self.spent + estimate_cost > self.max_cost:
            raise RuntimeError(
                f"预算不足：当前已花费 {self.spent:.4f}，预计新增 {estimate_cost:.4f}，预算上限 {self.max_cost:.4f}"
            )

    def consume(self, estimate_cost: float) -> None:
        self.spent += estimate_cost


class ModelRouter:
    def __init__(self, models: list[ModelAdapter], budget: BudgetController):
        self.models = models
        self.budget = budget

    def pick(self, preferred: str | None = None) -> ModelAdapter:
        if preferred:
            for model in self.models:
                if model.name == preferred:
                    return model
            raise ValueError(f"未找到模型: {preferred}")
        return sorted(self.models, key=lambda x: (x.input_cost_per_1k + x.output_cost_per_1k))[0]

    def run(self, prompt: str, preferred: str | None = None) -> str:
        model = self.pick(preferred)
        estimate_cost = self.budget.estimate(len(prompt) // 4, 400, model)
        self.budget.ensure_budget(estimate_cost)
        output = model.generate(prompt)
        self.budget.consume(estimate_cost)
        return output
