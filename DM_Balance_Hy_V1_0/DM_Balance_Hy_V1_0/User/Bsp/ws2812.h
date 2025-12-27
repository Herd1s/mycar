#ifndef __WS2812_H__
#define __WS2812_H__

#include "main.h"
#include <stdint.h>

// 控制的 LED 数量
#ifndef WS2812_LED_COUNT
#define WS2812_LED_COUNT 21
#endif

// API
void WS2812_Init(TIM_HandleTypeDef *htim, DMA_HandleTypeDef *hdma);
void WS2812_SetPixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b);
void WS2812_Clear(void);
void WS2812_Show(void);

// 阻塞发送（等待 DMA 完成）
void WS2812_ShowBlocking(void);

// 返回 1 表示正在发送，0 表示空闲
uint8_t WS2812_IsBusy(void);

#endif // __WS2812_H__
