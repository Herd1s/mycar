#ifndef __USB_COMM_H__
#define __USB_COMM_H__

#include "main.h"
#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// USB 指令结构：(cmd_vel, cmd_wel, flag1, flag2, flag3, flag4)
typedef struct
{
    float cmd_vel;   // 纵向速度指令
    float cmd_wel;   // 角速度/转向指令（按需解释）
    uint8_t flag1;
    uint8_t flag2;
    uint8_t flag3;
    uint8_t flag4;
    uint8_t new_cmd;        // 置1表示有新指令到达（读取后由上层清零）
    uint32_t last_rx_tick;  // 最后接收时间（ms）
} usb_cmd_t;

// 最新解析到的 USB 指令（在接收回调中更新）
extern volatile usb_cmd_t g_usb_cmd;

// 供 CDC 接收回调调用：将原始数据流写入解析器（自动识别形如 (a,b,c,d,e,f) 的帧）
void USB_Comm_OnReceive(const uint8_t *buf, uint32_t len);

// 发送 (x,y) 数据（ASCII，格式："(x.yyy,y.yyy)\r\n"），返回 USBD_OK/USBD_BUSY/USBD_FAIL
uint8_t USB_SendXY(float x, float y);

#ifdef __cplusplus
}
#endif

#endif /* __USB_COMM_H__ */
