#include "ws2812.h"
#include "tim.h"
#include "dma.h"

// 确认 DMA 句柄符号与 tim.c 中生成的句柄一致
extern DMA_HandleTypeDef hdma_tim1_ch1;

void HAL_DMA_TxCpltCallback(DMA_HandleTypeDef *hdma)
{
    if (hdma == &hdma_tim1_ch1)
    {
        WS2812_TransferCompleteCallback();
    }
}

// 可选：处理半完成或错误回调以便调试
void HAL_DMA_ErrorCallback(DMA_HandleTypeDef *hdma)
{
    if (hdma == &hdma_tim1_ch1)
    {
        // 停止 DMA 和定时器，确保输出线处于已知状态
        WS2812_TransferCompleteCallback();
    }
}
