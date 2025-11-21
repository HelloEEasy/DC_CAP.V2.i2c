/*
 * tim_control.c
 * 1) TIM2 CH1: PWM输出控制（占空比可调）
 * 2) TIM3: 产生低频振动（20/60/100Hz），由中断翻转IN1/IN2
 * 支持串口命令配置PWM占空比和振动频率
 */
#include "tim_control.h"
#include "tim.h"      /* 提供 htim2 的 extern */
#include "main.h"     /* 提供 IN1/IN2 引脚定义 */
#include "gpio.h"
#include "usart_debug.h"
#include "stm32f1xx_hal.h"
#include <string.h>
#include <stdlib.h>


/* 由TIM3中断设置，主循环处理IN1/IN2翻转请求 */
volatile uint8_t tim3_toggle_flag = 0;

/* IN1/IN2翻转的软件死区（毫秒） */
static const uint32_t TOGGLE_DEADTIME_MS = 2;

/* 当前状态变量（供帮助命令打印） */
static volatile uint8_t s_current_duty_percent = 0;
static volatile uint32_t s_current_tim3_freq_hz = 0;

/* 初始化TIM2的PWM输出 */
void TIM2_Control_Init(void)
{
    HAL_TIM_PWM_Start(&htim2, TIM_CHANNEL_1);
}

void TIM2_PWM_SetDutyPercent(uint8_t percent)
{
    if (percent > 100) percent = 100;
    s_current_duty_percent = percent;
    uint32_t arr = htim2.Init.Period;
    uint32_t pulse = ((uint32_t)percent * (arr + 1)) / 100U;
    if (pulse > arr) pulse = arr;
    __HAL_TIM_SET_COMPARE(&htim2, TIM_CHANNEL_1, pulse);
}

/* 为 TIM3 生成低频方波：设置目标频率 hz（例如 20/60/100）
 * 逻辑：计算半周期对应的计数（ARR），并以 Update 中断翻转引脚。
 * 注意：计算使用 htim3.Init.Prescaler（请确保 CubeMX 中已正确设置 PSC）
 */
void TIM3_SetSquareFreqHz(uint32_t hz)
{
    if (hz == 0) return;

    /* 读取 TIM3 的 PSC（注意：Init 已由 MX_TIM3_Init 配置） */
    uint32_t psc = htim3.Init.Prescaler;
    /* 以 72 MHz 系统时钟为基准计算 tick 频率：f_tick = 72MHz / (PSC+1)
     * （适用于 APB1 定时器在本工程中为 72 MHz 的情况）
     */
    uint64_t f_tick = 72000000ULL / (uint64_t)(psc + 1);

    /* half-period ticks = f_tick / (2 * hz) */
    uint64_t half_ticks = f_tick / (2ULL * (uint64_t)hz);
    if (half_ticks == 0) half_ticks = 1;
    uint32_t arr = (uint32_t)(half_ticks - 1ULL);
    if (arr > 0xFFFF) arr = 0xFFFF;

    /* 安全更新：停止中断，写 ARR，清计数器，重启中断 */
    HAL_TIM_Base_Stop_IT(&htim3); 
    __HAL_TIM_SET_AUTORELOAD(&htim3, arr);
    __HAL_TIM_SET_COUNTER(&htim3, 0);
    HAL_TIM_Base_Start_IT(&htim3);

    /* 记录当前频率，供帮助命令查看 */
    s_current_tim3_freq_hz = hz;
}

/* 内部通用切换实现：在主循环中被调用，假定调用者已清除对应的请求标志,必须在主循环中调用（不能在 ISR 中调用，因为包含延时）。 */
static void perform_toggle(void)
{
    /* 读取当前 IN1/IN2 状态并决定目标方向
     * 目标策略：在每次切换时，先把两个输入拉低（死区），等待短暂时间，
     * 然后写入目标方向（仅一个输入为高，另一个低）。
     */
    GPIO_PinState in1 = HAL_GPIO_ReadPin(IN1_GPIO_Port, IN1_Pin);
    GPIO_PinState in2 = HAL_GPIO_ReadPin(IN2_GPIO_Port, IN2_Pin);

    GPIO_PinState target_in1 = GPIO_PIN_RESET;
    GPIO_PinState target_in2 = GPIO_PIN_RESET;

    if (in1 == GPIO_PIN_SET && in2 == GPIO_PIN_RESET) {
        /* 当前为 IN1 正向 -> 切换到 IN2 */
        target_in1 = GPIO_PIN_RESET;
        target_in2 = GPIO_PIN_SET;
    } else if (in1 == GPIO_PIN_RESET && in2 == GPIO_PIN_SET) {
        /* 当前为 IN2 正向 -> 切换到 IN1 */
        target_in1 = GPIO_PIN_SET;
        target_in2 = GPIO_PIN_RESET;
    } else if (in1 == GPIO_PIN_RESET && in2 == GPIO_PIN_RESET) {
        /* 双低：默认设为 IN1 正向 */
        target_in1 = GPIO_PIN_SET;
        target_in2 = GPIO_PIN_RESET;
    } else {
        /* 双高（理论上不应出现），先拉低后设为 IN1 */
        target_in1 = GPIO_PIN_SET;
        target_in2 = GPIO_PIN_RESET;
    }

    /* 先拉低，保证在死区内无任何输入被驱动 */
    HAL_GPIO_WritePin(IN1_GPIO_Port, IN1_Pin, GPIO_PIN_RESET);
    HAL_GPIO_WritePin(IN2_GPIO_Port, IN2_Pin, GPIO_PIN_RESET);

    /* 主循环内短延时作为软件死区（注意：不要在 ISR 中调用 HAL_Delay） */
    HAL_Delay(TOGGLE_DEADTIME_MS);

    /* 设置目标方向，完成切换 */
    HAL_GPIO_WritePin(IN1_GPIO_Port, IN1_Pin, target_in1);
    HAL_GPIO_WritePin(IN2_GPIO_Port, IN2_Pin, target_in2);
}

void TIM3_HandlePendingToggle(void)
{
    if (!tim3_toggle_flag) return;
    tim3_toggle_flag = 0;
    perform_toggle();
}

/* 解析串口命令：支持PWM占空比和TIM3频率设置
 * 格式：
 * 1. 纯数字（如"50"）：设置TIM2 PWM占空比为50%
 * 2. f开头（如"f20"）：设置TIM3频率为20Hz
 */
void HandleTIM3Command(const char *cmd)
{
    if (cmd == NULL) return;
    
    /* 跳过前导空格 */
    while (*cmd == ' ' || *cmd == '\t') ++cmd;
    /* 检查帮助命令："h" 或 "?" */
    if ((cmd[0] == 'h' || cmd[0] == 'H' || cmd[0] == '?') && (cmd[1] == '\0')) {
        fdc_debug_print("STATUS: PWM duty=%u%%, TIM3=%lu Hz\r\n", (unsigned)s_current_duty_percent, (unsigned long)s_current_tim3_freq_hz);
        return;
    }
    
    /* 检查命令类型 */
    if (*cmd == 'f' || *cmd == 'F') {
        /* TIM3频率设置 */
        cmd++; /* 跳过f字符 */
        int val = atoi(cmd);// 将命令转换为整数（占空比百分比）
        if (val <= 0) {
            fdc_debug_print("Invalid TIM3 freq: %s\r\n", cmd);
            return;
        }
        TIM3_SetSquareFreqHz((uint32_t)val);
        fdc_debug_print("TIM3 freq set to %d Hz\r\n", val);
    } else if (*cmd >= '0' && *cmd <= '9') {
        /* PWM占空比设置 */
        int val = atoi(cmd);
        if (val < 0 || val > 100) {
            fdc_debug_print("Invalid PWM duty: %s (0-100)\r\n", cmd);
            return;
        }
        TIM2_PWM_SetDutyPercent((uint8_t)val);
        fdc_debug_print("PWM duty set to %d%%\r\n", val);
    } else {
        fdc_debug_print("Invalid command: %s\r\n", cmd);
    }
}
