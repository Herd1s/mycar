#include "usb_comm.h"
#include "usbd_cdc_if.h"
#include <string.h>

// 轻量级 (x.yyy) 浮点转字符串，避免依赖printf浮点
static int append_uint(char *out, size_t cap, uint32_t v)
{
    char tmp[12];
    int n = 0;
    if (v == 0) { if (cap > 0) out[0] = '0'; return 1; }
    while (v && n < (int)sizeof(tmp)) { tmp[n++] = (char)('0' + (v % 10)); v /= 10; }
    if ((size_t)n > cap) n = (int)cap;
    for (int i = 0; i < n; ++i) out[i] = tmp[n - 1 - i];
    return n;
}

static int ftoa3(char *out, size_t cap, float value)
{
    if (cap == 0) return 0;
    int idx = 0;
    if (value < 0) { out[idx++] = '-'; value = -value; if ((size_t)idx >= cap) return idx; }
    // 四舍五入到 3 位小数
    int32_t scaled = (int32_t)(value * 1000.0f + 0.5f);
    uint32_t ip = (uint32_t)(scaled / 1000);
    uint32_t frac = (uint32_t)(scaled % 1000);
    idx += append_uint(out + idx, cap > (size_t)idx ? cap - idx : 0, ip);
    if ((size_t)idx >= cap) return idx;
    out[idx++] = '.';
    if ((size_t)idx >= cap) return idx;
    // 始终输出 3 位小数，带前导零
    out[idx++] = (char)('0' + (frac / 100) % 10);
    if ((size_t)idx >= cap) return idx;
    out[idx++] = (char)('0' + (frac / 10) % 10);
    if ((size_t)idx >= cap) return idx;
    out[idx++] = (char)('0' + (frac % 10));
    return idx;
}

volatile usb_cmd_t g_usb_cmd = {0};

// 简单流式解析器：缓冲并寻找形如 "(a,b,c,d,e,f)" 的一帧
void USB_Comm_OnReceive(const uint8_t *buf, uint32_t len)
{
    static char rxbuf[256]; // 增大缓冲区以应对粘包
    static uint32_t rxtail = 0; // 当前缓冲长度

    if (!buf || len == 0) return;
    if (len > sizeof(rxbuf)) len = sizeof(rxbuf);

    // 若缓冲将溢出，丢弃最旧数据，保留尾部
    if (rxtail + len > sizeof(rxbuf)) {
        uint32_t keep = sizeof(rxbuf) / 2;
        memmove(rxbuf, rxbuf + (rxtail > keep ? (rxtail - keep) : 0), (rxtail > keep ? keep : rxtail));
        rxtail = (rxtail > keep ? keep : rxtail);
    }
    memcpy(rxbuf + rxtail, buf, len);
    rxtail += len;

    // 循环解析缓冲区中的所有完整包
    while (1)
    {
        // 寻找第一个 '('
        int start = -1;
        for (int i = 0; i < (int)rxtail; ++i) { if (rxbuf[i] == '(') { start = i; break; } }
        if (start < 0) {
            // 没有起始符，清空缓冲区（除非缓冲区末尾可能是半个包，这里简化处理：如果太长没找到就清空）
            // 为安全起见，如果缓冲区满了还没找到，就清空；否则保留等待后续数据
            if (rxtail == sizeof(rxbuf)) rxtail = 0;
            return; 
        }

        // 寻找 start 之后的第一个 ')'
        int end = -1;
        for (int i = start + 1; i < (int)rxtail; ++i) { if (rxbuf[i] == ')') { end = i; break; } }
        
        if (end < 0) {
            // 找到了 '(' 但没找到 ')'，可能是半个包
            // 如果 start > 0，可以丢弃 start 之前的垃圾数据
            if (start > 0) {
                uint32_t remain = rxtail - (uint32_t)start;
                memmove(rxbuf, rxbuf + start, remain);
                rxtail = remain;
            }
            return; // 等待更多数据
        }

        // 找到了完整包 (start..end)，尝试解析
        char frame[96];
        int flen = end - start + 1;
        if (flen >= (int)sizeof(frame)) flen = (int)sizeof(frame) - 1;
        memcpy(frame, rxbuf + start, (size_t)flen);
        frame[flen] = '\0';

        // 解析逻辑
        char *p = frame;
        if (*p == '(') ++p;
        char *q = strchr(p, ')');
        if (q) *q = '\0';

        float fv1 = 0, fv2 = 0; int fg[4] = {0};
        int part = 0;
        char *save = p;
        for (char *s = p; ; ++s) {
            if (*s == ',' || *s == '\0') {
                char tmp[24];
                size_t seglen = (size_t)(s - save);
                if (seglen >= sizeof(tmp)) seglen = sizeof(tmp) - 1;
                memcpy(tmp, save, seglen); tmp[seglen] = '\0';
                
                char *ts = tmp; while (*ts == ' ' || *ts == '\t' || *ts == '\r' || *ts == '\n') ++ts;
                char *te = ts + strlen(ts);
                while (te > ts && (te[-1] == ' ' || te[-1] == '\t' || te[-1] == '\r' || te[-1] == '\n')) --te;
                *te = '\0';

                if (part == 0 || part == 1) {
                    int neg = (*ts == '-'); if (neg) ++ts;
                    uint32_t ip = 0; while (*ts >= '0' && *ts <= '9') { ip = ip * 10 + (uint32_t)(*ts - '0'); ++ts; }
                    float val = (float)ip;
                    if (*ts == '.') {
                        ++ts; float base = 0.1f;
                        while (*ts >= '0' && *ts <= '9') { val += base * (float)(*ts - '0'); base *= 0.1f; ++ts; }
                    }
                    if (neg) val = -val;
                    if (part == 0) fv1 = val; else fv2 = val;
                } else if (part >= 2 && part <= 5) {
                    int v = 0; int neg = 0; if (*ts == '-') { neg = 1; ++ts; }
                    while (*ts >= '0' && *ts <= '9') { v = v * 10 + (*ts - '0'); ++ts; }
                    fg[part - 2] = neg ? -v : v;
                }
                ++part;
                save = s + 1;
                if (*s == '\0') break;
            }
            if (*s == '\0') break;
        }

        if (part >= 6) {
            g_usb_cmd.cmd_vel = fv1;
            g_usb_cmd.cmd_wel = fv2;
            g_usb_cmd.flag1 = (uint8_t)fg[0];
            g_usb_cmd.flag2 = (uint8_t)fg[1];
            g_usb_cmd.flag3 = (uint8_t)fg[2];
            g_usb_cmd.flag4 = (uint8_t)fg[3];
            g_usb_cmd.new_cmd = 1;
            g_usb_cmd.last_rx_tick = HAL_GetTick();
        }

        // 移除已处理的包（包括它前面的垃圾数据）
        uint32_t remain = rxtail - (uint32_t)(end + 1);
        if (remain > 0) {
            memmove(rxbuf, rxbuf + end + 1, remain);
        }
        rxtail = remain;
        
        // 继续循环处理缓冲区中剩余的数据
    }
}

uint8_t USB_SendXY(float x, float y)
{
    char buf[64];
    int idx = 0;
    if (sizeof(buf) < 16) return USBD_FAIL;
    buf[idx++] = '(';
    idx += ftoa3(buf + idx, sizeof(buf) - (size_t)idx, x);
    if ((size_t)idx < sizeof(buf)) buf[idx++] = ','; else return USBD_FAIL;
    idx += ftoa3(buf + idx, sizeof(buf) - (size_t)idx, y);
    if ((size_t)idx < sizeof(buf)) buf[idx++] = ')'; else return USBD_FAIL;
    if ((size_t)idx < sizeof(buf)) buf[idx++] = '\r'; else return USBD_FAIL;
    if ((size_t)idx < sizeof(buf)) buf[idx++] = '\n'; else return USBD_FAIL;
    return CDC_Transmit_HS((uint8_t*)buf, (uint16_t)idx);
}
