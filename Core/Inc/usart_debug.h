/* usart_debug.h
 * 简单的 USART 调试输出封装，使用 huart1 (USART1)
 * 波特率在 CubeMX 中按用户要求设置为 115200
 */
#ifndef __USART_DEBUG_H__
#define __USART_DEBUG_H__

#include "main.h"

/* 调试输出函数，内部使用 HAL_UART_Transmit 阻塞发送 */
void fdc_debug_init(void); /* 如需初始化额外资源可在此实现 */
void fdc_debug_print(const char *fmt, ...);
/* 限频打印：在周期性或循环中调用错误打印时使用，默认限频为 1000 ms。
 * 该函数会保证同一处的频繁调用不会导致串口刷屏。实现为全局限频。
 */
void fdc_debug_print_limited(const char *fmt, ...);

/* 从主循环读取已接收到的一条完整命令（非阻塞）
 * out: 输出缓冲区，maxlen: 缓冲区长度
 * 返回 1 表示有命令已读取并复制到 out；返回 0 表示无命令
 */
int fdc_debug_get_command(char *out, int maxlen);

/* 诊断接口：获取并清除自上次读取以来 UART Rx 回调触发计数（用于确认回调是否被调用） */
int fdc_debug_get_rx_events(void);

#endif /* __USART_DEBUG_H__ */
