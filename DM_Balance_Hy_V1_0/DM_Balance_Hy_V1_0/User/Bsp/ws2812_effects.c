#include "ws2812_effects.h"
#include "ws2812.h"
#include <math.h>
#include <string.h>
#include "uart_bsp.h"

void white_breath(void)
{
	// 平滑呼吸效果（基于时间），使用正弦缓动并做伽马校正以减小可见阶梯
	// 由 FreeRTOS 周期性调用（例如每 10ms），函数内部用 HAL_GetTick() 计算实际 dt
	const float period_s = 10.0f; // 呼吸周期（秒）——可调，增大更慢
	const float gamma = 2.2f;    // 伽马系数，靠近人眼感知校正
	const uint8_t max_brightness = 220; // 最大亮度上限，避免全白刺眼

	static float phase = 0.0f; // 以弧度为单位
	static uint32_t last_ms = 0;

	uint32_t now = HAL_GetTick();
	float dt = 0.0f;
	if (last_ms == 0) dt = 0.0f; else dt = (now - last_ms) / 1000.0f;
	last_ms = now;

	// 若 dt 非常小（首次或调试），允许一次更新以初始化显示
	if (dt <= 0.0f) dt = 0.0f;

	// 每次按真实时间推进相位
	phase += (2.0f * 3.14159265358979323846f) * (dt / period_s);
	if (phase > 2.0f * 3.14159265358979323846f) phase -= 2.0f * 3.14159265358979323846f;

	// 正弦缓动到 [0,1]
	float v = 0.5f * (1.0f + sinf(phase));
	// 伽马校正：提高低亮度的连续性，减少阶梯感
	float v_gamma = powf(v, gamma);

	uint8_t val = (uint8_t)(v_gamma * (float)max_brightness + 0.5f);

	for (uint16_t i = 0; i < WS2812_LED_COUNT; ++i)
	{
		WS2812_SetPixel(i, val, val, val);
	}

	// 仅在 DMA 空闲时发起一次传输
	if (!WS2812_IsBusy())
	{
		WS2812_Show();
	}
}

// 从中间向两边亮（平滑扩散填充）
void center_to_sides(void)
{
	// 参数：可调
	const float speed_leds_per_sec = 6.0f; // 扩展速度（LED 单位 / 秒）
	const float tail = 2.5f;              // 边缘柔和长度（LED）
	const float gamma = 2.2f;            // 伽马校正
	const uint8_t max_brightness = 200;   // 最大亮度

	static float radius = 0.0f;          // 当前半径（以 LED 单位计）
	static uint32_t last_ms = 0;
	// 状态机：0=扩展，1=闪烁等待遥控，2=常亮
	static int state = 0;
	// 用于整体闪烁（全部亮或灭）
	static uint32_t blink_last_ms = 0;
	static uint8_t blink_on = 0;

	uint32_t now = HAL_GetTick();
	float dt = 0.0f;
	if (last_ms == 0) dt = 0.0f; else dt = (now - last_ms) / 1000.0f;
	last_ms = now;

	// 以速度推进半径（仅在扩展状态）
	if (state == 0)
	{
		radius += speed_leds_per_sec * dt;
	}

	// 中心位置（支持奇偶数 LED）
	float mid = (WS2812_LED_COUNT - 1) / 2.0f;
	float max_radius = mid;

	// 当半径超过最大时进入闪烁等待状态（不再重置），直到遥控器在线后常亮
	if (state == 0 && radius > max_radius + tail)
	{
		state = 1; // 进入闪烁
		radius = max_radius; // 固定在边界
	}

	// 填充像素：行为根据状态不同
	for (int i = 0; i < WS2812_LED_COUNT; ++i)
	{
		float d = fabsf((float)i - mid);
		float intensity = 0.0f;
		if (state == 0)
		{
			if (d <= radius)
			{
				intensity = 1.0f;
			}
			else if (d <= radius + tail)
			{
				float t = (d - radius) / tail; // 0..1
				// 使用平滑二次衰减
				intensity = 1.0f - t * t;
			}
			else
			{
				intensity = 0.0f;
			}
		}
		else if (state == 1)
		{
			// 闪烁状态：全部亮或全部灭（基于时间切换）
			const uint32_t blink_period_ms = 200; // 200ms 切换一次
			uint32_t now2 = HAL_GetTick();
			if (blink_last_ms == 0) blink_last_ms = now2;
			if ((now2 - blink_last_ms) >= blink_period_ms)
			{
				blink_on = !blink_on;
				blink_last_ms = now2;
			}
			intensity = blink_on ? 1.0f : 0.0f;
		}
		else // state == 2 常亮
		{
			intensity = 1.0f;
		}

		// 伽马校正后映射到 0..max_brightness
		float ig = powf(intensity, 1.0f / gamma);
		uint8_t val = (uint8_t)(ig * (float)max_brightness + 0.5f);
		WS2812_SetPixel(i, val, val, val);
	}

	// 启动一次传输（若 DMA 空闲）
	if (!WS2812_IsBusy())
	{
		WS2812_Show();
	}

	// 状态推进与条件：如果处于闪烁状态且遥控器在线则转为常亮
	if (state == 1 && remoter.online)
	{
		state = 2; // 常亮
		// 立即将所有灯设置为最大亮度并显示一次
		for (int i = 0; i < WS2812_LED_COUNT; ++i) WS2812_SetPixel(i, max_brightness, max_brightness, max_brightness);
		if (!WS2812_IsBusy()) WS2812_Show();
	}

	// 无需额外相位推进（闪烁由 blink_on / blink_last_ms 控制）
}

