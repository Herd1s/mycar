#include "ws2812.h"
#include <string.h>

// WS2812 定时常量（纳秒），用于 800kHz:
// T0H 约 350ns 高电平，T1H 约 900ns 高电平，总周期约 1250ns
// 我们会根据调用者配置的定时器时钟将这些时间换算为计数刻度。

#define BITS_PER_LED 24

static TIM_HandleTypeDef *ws_htim = NULL;
static DMA_HandleTypeDef *ws_hdma = NULL;

// LED 颜色缓存（WS2812 使用 GRB 顺序）
static uint8_t led_buf[WS2812_LED_COUNT][3];

// DMA 缓冲：每位对应一个 CCR 值。STM32H7 上 TIM 的 CCR 寄存器为 32 位，
// 因此使用 uint32_t 以保证外设/内存对齐。

static uint32_t dma_buf[(WS2812_LED_COUNT * BITS_PER_LED) + 50]; // +50 for reset slots
static uint32_t dma_buf_len = 0;

// 定时配置（计数刻度）
static uint32_t period_ticks = 0; // ARR + 1
static uint32_t t0h_ticks = 0;
static uint32_t t1h_ticks = 0;

// 繁忙标志
static volatile uint8_t ws_busy = 0;
// timestamp of last WS2812_Show() call (ms) for timeout watchdog
static uint32_t ws_last_send_ms = 0;

// 说明：调用者应配置定时器使得定时器时钟与 WS2812 的周期 (~1250ns) 对应。
// 为方便起见，这里从定时器的 ARR 值计算出各时间的刻度，调用者必须传入已配置好的 TIM。

void WS2812_Init(TIM_HandleTypeDef *htim, DMA_HandleTypeDef *hdma)
{
    ws_htim = htim;
    ws_hdma = hdma;
    ws_busy = 0;
    memset(led_buf, 0, sizeof(led_buf));

    // 从定时器寄存器推导出定时参数：不能跨平台直接查询输入时钟，
    // 因此根据定时器的 PSC 和 ARR 值推算。假设定时器已由调用者初始化并启动（PWM 模式，ARR 对应 ~1.25us）。

    // 读取 ARR 以确定周期计数（ticks）
    period_ticks = (uint32_t)(ws_htim->Instance->ARR) + 1U;

    // 对应 WS2812 定时：T0H 约 0.35us；T1H 约 0.9us；周期约 1.25us
    // 按比例计算对应的计数刻度
    t0h_ticks = (uint32_t)((period_ticks * 350UL) / 1250UL);
    t1h_ticks = (uint32_t)((period_ticks * 900UL) / 1250UL);

    // Ensure not zero
    if (t0h_ticks == 0) t0h_ticks = 1;
    if (t1h_ticks == 0) t1h_ticks = 2;
}

// 内部函数：根据 led_buf 构建 DMA 发送缓冲
static void build_dma_buffer(void)
{
    uint32_t pos = 0;
    for (uint16_t i = 0; i < WS2812_LED_COUNT; ++i)
    {
        // WS2812 要求颜色数据按 GRB 顺序
        uint8_t green = led_buf[i][1];
        uint8_t red = led_buf[i][0];
        uint8_t blue = led_buf[i][2];
        uint8_t colors[3] = {green, red, blue};
        for (int c = 0; c < 3; ++c)
        {
            for (int bit = 7; bit >= 0; --bit)
            {
                uint8_t b = (colors[c] >> bit) & 0x01;
                dma_buf[pos++] = b ? (uint32_t)t1h_ticks : (uint32_t)t0h_ticks;
            }
        }
    }
    // 复位时间：确保输出低电平 >50us。周期约 1.25us，约需 40 个周期，所以这里留 50 个零位。
    for (int i = 0; i < 50; ++i)
    {
        dma_buf[pos++] = 0UL;
    }
    dma_buf_len = pos;
}

// 启动 DMA 传输（非阻塞）
void WS2812_Show(void)
{
    if (ws_htim == NULL || ws_hdma == NULL) return;
    if (ws_busy) return;

    build_dma_buffer();

    ws_busy = 1;
    // record send time for watchdog in case DMA callback never fires
    ws_last_send_ms = HAL_GetTick();

    // 确保 TIM 的 PWM 通道配置使用 CCR1 作为比较寄存器并在更新/比较时触发 DMA 请求
    // 在重新配置前停止定时器通道
    HAL_TIM_PWM_Stop_DMA(ws_htim, TIM_CHANNEL_1);

    // 启动从内存到外设的 DMA 传输，目标为 TIM1->CCR1。
    // 注意：在许多 STM32 系列中 CCRx 为 16 位寄存器；这里使用 HAL 的 DMA API，内存单元为半字（根据 DMA 配置）。
    // DMA 的具体配置应由调用者完成（正确的 TIM CH1 请求、内存到外设方向），这里只做基础的启动操作。

    if (HAL_TIM_PWM_Start_DMA(ws_htim, TIM_CHANNEL_1, (uint32_t *)dma_buf, (uint32_t)dma_buf_len) != HAL_OK)
    {
        // error
        ws_busy = 0;
    }
}

// 阻塞版本：等待 DMA 完成
void WS2812_ShowBlocking(void)
{
    WS2812_Show();
    // Wait for completion
    while (ws_busy) { __NOP(); }
}

uint8_t WS2812_IsBusy(void)
{
    // simple timeout: if busy for too long (e.g., >200ms), assume transfer aborted and clear flag
    if (ws_busy)
    {
        uint32_t now = HAL_GetTick();
        if ((now - ws_last_send_ms) > 200U)
        {
            // timeout — clear busy to allow new transfers
            ws_busy = 0;
        }
    }
    return ws_busy;
}

void WS2812_Clear(void)
{
    memset(led_buf, 0, sizeof(led_buf));
}

void WS2812_SetPixel(uint16_t idx, uint8_t r, uint8_t g, uint8_t b)
{
    if (idx >= WS2812_LED_COUNT) return;
    led_buf[idx][0] = r;
    led_buf[idx][1] = g;
    led_buf[idx][2] = b;
}

// DMA 传输完成的回调应清除繁忙标志。如果用户没有定义对应的 HAL 回调，这里提供一个可调用的辅助函数。
// 用户应在其 HAL 的 DMA 中断或定时器完成回调中调用 `WS2812_TransferCompleteCallback()`。

// 提供一个中断处理辅助函数；用户应在 `HAL_TIM_PWM_PulseFinishedCallback` 或 DMA 完成回调中调用它。

void WS2812_TransferCompleteCallback(void)
{
    // 停止 PWM DMA 以避免重复触发
    if (ws_htim != NULL)
    {
        HAL_TIM_PWM_Stop_DMA(ws_htim, TIM_CHANNEL_1);
    }
    ws_busy = 0;
}

/* 集成说明：
   请不要在此处直接定义与 HAL 相同名称的回调函数，这可能与 HAL 库已有的定义冲突并导致链接时重定义错误。
   正确的做法是在你的 HAL 回调中调用 `WS2812_TransferCompleteCallback()`。例如：

   void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef *htim)
   {
       if (htim == &htim1) {
           WS2812_TransferCompleteCallback();
       }
   }

   或在 DMA 完成回调中：

   void HAL_DMA_TxCpltCallback(DMA_HandleTypeDef *hdma)
   {
       if (hdma == &hdma_tim1_ch1) {
           WS2812_TransferCompleteCallback();
       }
   }

*/
