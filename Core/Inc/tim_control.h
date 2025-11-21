/*
 * tim_control.h
 * 1) TIM2 CH1 PWM输出控制（占空比可调）
 * 2) TIM3 低频振动控制（20/60/100Hz），通过中断翻转IN1/IN2
 * 支持串口命令配置PWM占空比和振动频率
 *
 * 使用说明：
 * - TIM2 PWM输出：
 *   1. 调用 TIM2_Control_Init() 启动PWM
 *   2. 使用 TIM2_PWM_SetDutyPercent(percent) 设定占空比（0-100）
 * 
 * - TIM3 振动控制：
 *   1. 在main中已由CubeMX生成的MX_TIM3_Init()配置并启动定时器
 *   2. 使用 TIM3_SetSquareFreqHz() 设置频率（如20/60/100Hz）
 *   3. 主循环中调用 TIM3_HandlePendingToggle() 执行IN1/IN2翻转
 *   4. 通过串口发送命令（如"f20"）可动态调整频率
 */

#ifndef __TIM_CONTROL_H
#define __TIM_CONTROL_H

#include <stdint.h>
#include <stdbool.h>

/* 初始化并启动TIM2 PWM输出 */
void TIM2_Control_Init(void);

/* 设置TIM2 CH1 PWM占空比（0-100%）
 * 内部自动根据ARR换算并立即生效
 */
void TIM2_PWM_SetDutyPercent(uint8_t percent);

/* 设置TIM3频率（Hz）
 * - 支持20/60/100Hz等低频档位
 * - 会自动计算ARR并重启定时器
 */
void TIM3_SetSquareFreqHz(uint32_t hz);

/* TIM3中断设置的翻转请求标志 */
extern volatile uint8_t tim3_toggle_flag;

/* 主循环调用：执行IN1/IN2翻转（带2ms软件死区） */
void TIM3_HandlePendingToggle(void);

/* 解析串口命令
 * - PWM占空比：数字表示百分比，如"50"设置为50%
 * - TIM3频率："f"开头，如"f20"设置为20Hz
 */
void HandleTIM3Command(const char *cmd);

#endif /* __TIM_CONTROL_H */
