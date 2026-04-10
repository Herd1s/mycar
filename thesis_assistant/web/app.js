const form = document.getElementById("demo-form");
const output = document.getElementById("output");

const modelPricing = {
  "fast-cheap": { input: 0.001, output: 0.002 },
  balanced: { input: 0.003, output: 0.006 },
  quality: { input: 0.01, output: 0.02 },
};

function estimateCost(textLength, model) {
  const pricing = modelPricing[model];
  const inTokens = Math.ceil(textLength / 4);
  const outTokens = 400;
  return ((inTokens / 1000) * pricing.input + (outTokens / 1000) * pricing.output).toFixed(4);
}

form.addEventListener("submit", (event) => {
  event.preventDefault();
  const topic = document.getElementById("topic").value.trim();
  const materials = document
    .getElementById("materials")
    .value.split("\n")
    .map((line) => line.trim())
    .filter(Boolean);
  const model = document.getElementById("model").value;
  const budget = Number(document.getElementById("budget").value || 0);

  const est = Number(estimateCost(topic.length + materials.join("").length, model));
  if (est > budget) {
    output.textContent = `预算不足：预计消耗 ${est} 元，预算仅 ${budget.toFixed(2)} 元。\n请切换更低成本模型或提升预算。`;
    return;
  }

  const checklist = [
    "封面",
    "中文摘要",
    "英文摘要",
    "目录",
    "正文",
    "参考文献",
  ];

  output.textContent = [
    `【题目】${topic}`,
    `【模型】${model}（预计成本：￥${est}）`,
    "",
    "【论文框架规划】",
    "1. 绪论：研究背景、问题定义、研究意义",
    "2. 原理分析：理论模型、关键假设、适用边界",
    "3. 方法设计：系统架构、算法流程、实现细节",
    "4. 实验与结果：实验设置、对比指标、误差分析",
    "5. 结论与展望：结论归纳、局限性与后续方向",
    "",
    "【章节草拟（节选）】",
    "第1章 绪论：本研究提出……[需来源]",
    "第2章 原理分析：根据文献与实验可得……[需来源]",
    "",
    "【图表公式建议】",
    "图1-1 系统总体架构图",
    "表2-1 实验参数设置",
    "公式(3-2)：L = λ1·Ltask + λ2·Lreg",
    "",
    "【规范校验】",
    `结构检查：${checklist.join("、")}`,
    "引用检查：关键结论需补充 DOI/URL/馆藏信息",
    "",
    "【导出】",
    "可导出 Word / PDF（示例前端原型）",
    "",
    "【合规声明】",
    "最终论文内容必须由作者确认；关键结论必须有来源、可追溯、可复核。",
  ].join("\n");
});
