/* usart_debug.c
 * 使用 huart1 (USART1) 的阻塞式 debug 打印封装
 * 说明：为简化调试，使用 vsnprintf 写入缓冲区，再用 HAL_UART_Transmit 发送
 */

#include "usart_debug.h"
#include "tim_control.h"
#include <stdarg.h>
#include <stdio.h>
#include "main.h" // main.h 通常包含 HAL 的头文件和项目的外部句柄声明
#include "stm32f1xx_hal_uart.h"
#include <stdint.h>

/* 如果 main.h 没有声明 huart1，可以在工程中自行在 usart.c/h 中声明。
 * 这里仍然使用 extern 声明以避免依赖特定生成文件名。
 */
extern UART_HandleTypeDef huart1; // CubeMX/工程生成的 USART1 句柄

/* 串口接收缓冲区 */
static char rx_buffer[32];
static volatile uint8_t rx_idx = 0;

/* 命令就绪缓冲区（ISR写入，主循环读取） */
static char cmd_buffer[32];
static volatile uint8_t cmd_ready = 0;

/* 诊断：回调触发计数（主循环可读取并清零） */
static volatile uint32_t s_rx_events = 0;

void fdc_debug_init(void)
{
    /* 启动单字节接收，进入中断模式 */
    if (HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_buffer[rx_idx], 1) != HAL_OK) {
        /* 若启动失败，尝试打印（若UART已可用）以便快速诊断 */
        fdc_debug_print("fdc_debug_init: HAL_UART_Receive_IT failed\r\n");
    }  
}

void fdc_debug_print(const char *fmt, ...)
{
    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    /* 使用阻塞传输保持实现简单且可靠用于调试 */
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 200);
}

void fdc_debug_print_limited(const char *fmt, ...)
{
    /* 全局限频实现：同一个函数调用点在短时间内的重复打印会被抑制。
     * 简单实现：使用一个静态最后打印时间戳（ms），限制为 1000 ms。
     * 这是全局限频（适合大多数场景）。若需要更细粒度（按 tag 限频），可扩展实现。
     */
    static uint32_t last_tick = 0;
    uint32_t now = HAL_GetTick();
    if ((now - last_tick) < 1000U) return; /* 少于 1s，跳过打印 */
    last_tick = now;

    char buf[256];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len < 0) return;
    if (len > (int)sizeof(buf)) len = sizeof(buf);
    HAL_UART_Transmit(&huart1, (uint8_t *)buf, (uint16_t)len, 200);
}

/* UART接收中断回调：用于接收命令
 * 协议：以回车或换行结束一条命令
 * - 纯数字（如"50"): 设置TIM2 PWM占空比为50%
 * - "fNN" (如"f20"): 设置TIM3频率为NN Hz
 */
void HAL_UART_RxCpltCallback(UART_HandleTypeDef *huart)
{
    if (huart->Instance != USART1) return;

    char c = rx_buffer[rx_idx];// 获取刚接收到的字符
    if (c == '\r' || c == '\n') {
        if (rx_idx > 0) {
            rx_buffer[rx_idx] = '\0';
            /* 将命令复制到主循环可读取的缓冲区，并设置就绪标志（ISR安全） */
            /* 注意：短命令拷贝，保持简单且原子性足够 */
            for (int i = 0; i <= rx_idx && i < (int)sizeof(cmd_buffer)-1; ++i) cmd_buffer[i] = rx_buffer[i];
            cmd_buffer[rx_idx < (int)sizeof(cmd_buffer)-1 ? rx_idx : (int)sizeof(cmd_buffer)-1] = '\0';
            cmd_ready = 1;
            rx_idx = 0;
        }
    } else {
        if (rx_idx < (int)sizeof(rx_buffer) - 1) {
            rx_idx++;// 移动索引准备接收下一个字符
        } else {
            /* 缓冲区溢出，重置 */
            rx_idx = 0;
        }
    }

    /* 标记一个回调事件（主循环可查询），不在 ISR 中执行阻塞操作 */
    s_rx_events++;

    /* 继续接收下一个字节；使用回调传入的 huart 指针以提高鲁棒性 */
    HAL_UART_Receive_IT(&huart1, (uint8_t*)&rx_buffer[rx_idx], 1);
}

/* 主循环调用以非阻塞方式获取一条完整命令（若有） */
int  fdc_debug_get_command(char *out, int maxlen)
{
    if (!cmd_ready) return 0;
    /* 复制并清标志（简单原子操作） */
    int i = 0;
    for (; i < maxlen-1 && i < (int)sizeof(cmd_buffer) && cmd_buffer[i] != '\0'; ++i) {
        out[i] = cmd_buffer[i];
    }
    out[i] = '\0';
    cmd_ready = 0;
    return 1;
}

/* 读取并清除从上次读取以来的 Rx 回调事件计数（供主循环诊断使用） */
int fdc_debug_get_rx_events(void)
{
    int v = (int)s_rx_events;
    s_rx_events = 0;
    return v;
}
