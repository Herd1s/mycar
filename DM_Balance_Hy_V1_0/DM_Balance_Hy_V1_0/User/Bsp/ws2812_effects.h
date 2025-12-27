#ifndef __WS2812_EFFECTS_H__
#define __WS2812_EFFECTS_H__

/* ws2812_effects 模块：保留白色呼吸效果函数声明（实现位于 ws2812_effects.c）。
	之前完整效果库已移除；若需要更多效果请恢复实现文件。 */

void white_breath(void);
// 从中间向两边亮的效果（非阻塞）
void center_to_sides(void);

#endif // __WS2812_EFFECTS_H__
