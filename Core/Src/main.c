/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2025 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "adc.h"
#include "i2c.h"
#include "stm32f1xx_hal_adc.h"
#include "tim.h"
#include "usart.h"
#include "gpio.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* 引入 FDC2214 驱动接口与串口调试封装（用于 fdc_read_result_raw, fdc_calibrate_baseline, fdc_debug_print） */
#include "fdc2214.h"
#include "usart_debug.h"
/* TIM2/3 控制封装 */
#include "tim_control.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/

/* USER CODE BEGIN PV */

/* TIM3 中断请求翻转标志 */
// static volatile uint8_t tim3_toggle_flag = 0;

/* 基线数组（每通道），在启动时可以通过 fdc_calibrate_baseline 填充或动态更新 */
// static uint32_t baseline[4] = {0, 0, 0, 0};
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */

  /* USER CODE END 1 */

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_I2C1_Init();
  MX_USART1_UART_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_ADC1_Init();
  /* USER CODE BEGIN 2 */
  
  /* 1. 初始化串口接收以接收命令 */
  fdc_debug_init();  /* 这会启动串口中断接收 */
  
  // /* 2. 启动TIM2 PWM输出（占空比可通过串口设置）*/
  // TIM2_Control_Init();
  // TIM2_PWM_SetDutyPercent(90);  /* 设置默认占空比50% */                //改接收再振动################

  // /* 3. 启动TIM3并设置默认频率（20Hz）*/
  // HAL_TIM_Base_Start_IT(&htim3);  /* 启动定时器中断 */
  // TIM3_SetSquareFreqHz(100);       /* 设置默认频率 */

  fdc_debug_print("PWMPercent FreqHz");
  /* FDC2214 初始化（如果需要）*/
  {
    int r = fdc_init();
    if (r != FDC_OK) {
      /* 使用限频打印，避免初始化阶段反复打印导致串口占满 */
      fdc_debug_print_limited("fdc_init failed: %s (%d)\r\n", fdc_err_str(r), r);
    } else {
      fdc_debug_print("fdc_init OK\r\n");
    }
  }

  HAL_ADC_Start(&hadc1); //启动ADC1

  // /* 启动时对 4 个通道做基线校准（将结果写入上方定义的 baseline[] 数组）
  //  * 说明：如果不做校准，baseline 默认为 0（在头部已初始化为 0），
  //  *      那么 delta = raw - baseline 会等于 raw，本质上没有去偏移效果。
  //  * 这里使用  次采样取平均，必要时可调整 samples 的值。
  //  */
  // {
  //   const uint8_t samples = 100; /* 建议 8..64，根据噪声与时间权衡 */
  //   for (int ch = 0; ch < 4; ++ch) {
  //     int rc = fdc_calibrate_baseline((fdc_channel_t)ch, &baseline[ch], samples);
  //     if (rc != FDC_OK) {
  //       /* 校准失败时限频打印错误，避免串口刷屏 */
  //       fdc_debug_print_limited("calibrate CH%d failed: %s (%d)\r\n", ch, fdc_err_str(rc), rc);
  //     } else {
  //       /* 成功打印基线值一次（调试用），随后主循环会使用 baseline[] */
  //       fdc_debug_print("calibrate CH%d baseline=%lu\r\n", ch, (unsigned long)baseline[ch]);
  //     }
  //     /* 小延时，给总线和传感器一点恢复时间 */
  //     HAL_Delay(50);
  //   }
  // }

  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */

    
  // /* 处理由TIM3 Update 触发的安全翻转请求（带软件死区） */
  // TIM3_HandlePendingToggle();

  
     /* 2. FDC2214采样（如需要） */
  for (int ch = 0; ch < 4; ++ch) {
    /* 声明一个局部变量用于保存本次读取到的原始值（32-bit 容器）
     * 注意：FDC2214 的有效位可能高达 28 位，使用 32-bit 容器以免溢出
     */
    uint32_t raw = 0;

    /* 调用驱动函数读取指定通道的原始值
     * fdc_read_result_raw 会按手册要求先读取 DATA_CHx（MSB）再读取 DATA_LSB_CHx（LSB）
     * 返回 FDC_OK 表示读取成功；若返回错误码，则代表 I2C 通信或设备状态异常
     */
    if (fdc_read_result_raw((fdc_channel_t)ch, &raw) == FDC_OK) {
      /* 成功读取后：
       * 根据 datasheet 将 RAW(DATAx) 转换为振荡频率 f_sensor，再由已知电感 L 和并联电容 C0
       * 计算被测电容值（单位 F），这里演示使用 fref = 40 MHz, C0 = 20 pF。
       * 注意：L 必须由硬件线圈实际测量或由电路设计提供 -- 下面使用示例值 L = 1e-6 H （1 uH），请根据实际测量修改。
       */
      const double fref_hz = 40e6; /* 40 MHz */
      const double C0_f = 20e-12;  /* 20 pF */
      const double L_h = 18e-6;     /* 18 uH, 请根据实际线圈电感替换 */
      double fsensor = fdc_raw_to_freq(raw, fref_hz); 
      double C_f = fdc_freq_to_capacitance(fsensor, L_h, C0_f);
      /* 将电容转换为 pF 便于阅读 */
      double C_pf = (C_f > 0.0) ? (C_f * 1e12) : -1.0;//三元条件运算符:条件表达式 ? 表达式1 : 表达式2;先判断「条件表达式」的真假，然后根据判断结果分别执行表达式1或表达式2。如果条件为真（非0），则返回表达式1的结果；否则，返回表达式2的结果。


      /* 打印通道、原始值、频率与电容（pF）。限频打印已在初始化时用于错误，主循环打印频率较低（每轮 50ms）。 */
      if (C_pf >= 0.0) {//printf 的浮点支持被禁用了（在 STM32 的 newlib/nano printf 默认不含 %f）
          uint32_t f_hz = (uint32_t)(fsensor + 0.5);           /* 四舍五入 整数 Hz */
          uint32_t C_milli_pf = (uint32_t)(C_pf + 0.5); /* 四舍五入 */
          fdc_debug_print("CH%d raw=%lu f=%luHz C=%lu m-pF\r\n",ch, (unsigned long)raw, (unsigned long)f_hz, (unsigned long)C_milli_pf);
      } else {
          fdc_debug_print("CH%d raw=%lu   f=%.1f Hz   C=ERR\r\n", ch, (unsigned long)raw, fsensor);
      }
    } else {
      /* 读取失败：打印错误信息（此处可改用限频打印以避免循环刷屏） */
      fdc_debug_print_limited("Read CH%d failed\r\n", ch);//  fdc_debug_print_limited("PWMPercent FreqHz_limit");
    }

    /* 在读取通道之间加入短延时：
     * - 避免 I2C 总线上的紧凑访问导致从机忙或总线争用
     * - 给被测电路/传感器一点时间稳定（视测量速率与硬件而定可调整）//调###############
     */
    HAL_Delay(5);
  } 


  
  HAL_ADC_PollForConversion(&hadc1, 100);
  uint32_t adcValue = HAL_ADC_GetValue(&hadc1);
  float voltage = (adcValue / 4095.0f) * 3.3f; // Assuming a 3.3V reference voltage
  float test_value = 0.666666666;
  fdc_debug_print("test: %1.2f\r\n", test_value);
  fdc_debug_print("ADC1 Value: %1.2f\r\n", voltage);

  // /* 先处理串口命令（如果有），把命令放在主循环处理，避免在ISR中调用HAL函数 */
  // {
  //   char cmd[32];
  //   if (fdc_debug_get_command(cmd, sizeof(cmd))) {
  //     /* 由主循环调用命令处理，安全执行TIM相关的HAL操作 */
  //     HandleTIM3Command(cmd);
  //   }
  // }



  // /* 诊断：如果用户在串口发送了字符但没有看到回显，
  //  * 可以通过 RX 事件计数确认中断是否触发。
  //  * 这里每轮读取并清零计数，若 >0 则打印一次（不会太频繁）。
  //  */
  // {
  //   int rxev = fdc_debug_get_rx_events();
  //   if (rxev>0) {
  //     fdc_debug_print("RX events=%d\r\n", rxev);
  //   }
  // }

  // /* 在一轮四通道读取完成后再等待较长的周期，控制总体采样率 */
  HAL_Delay(50);
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};
  RCC_PeriphCLKInitTypeDef PeriphClkInit = {0};

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSEPredivValue = RCC_HSE_PREDIV_DIV1;
  RCC_OscInitStruct.HSIState = RCC_HSI_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLMUL = RCC_PLL_MUL9;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_HCLK_DIV1;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
  PeriphClkInit.PeriphClockSelection = RCC_PERIPHCLK_ADC;
  PeriphClkInit.AdcClockSelection = RCC_ADCPCLK2_DIV6;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInit) != HAL_OK)
  {
    Error_Handler();
  }
}

/* USER CODE BEGIN 4 */
// /* OC 中断回调：不要在回调中直接做耗时操作，改为设置请求标志，由主循环处理（以便做死区延时） */
// void HAL_TIM_OC_DelayElapsedCallback(TIM_HandleTypeDef *htim){
//   if (htim->Instance == TIM2 && htim->Channel == HAL_TIM_ACTIVE_CHANNEL_2) {
//     /* 设置方向切换请求标志，主循环会调用 TIM2_HandlePendingToggle() 做安全切换 */
//     tim2_request_toggle_flag = 1;
//   }
// }

/* 基本更新中断回调：用于 TIM3 的半周期计时（在 MX_TIM3_Init 中启用了 Update IRQ）
 * 我们在这里设置同样的请求标志，由主循环执行带死区的翻转处理。
 */
/* TIM3更新中断回调：请求翻转IN1/IN2（在主循环中执行带死区保护的翻转）*/
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim){
  if (htim->Instance == TIM3) {
    tim3_toggle_flag = 1;  /* TIM3_HandlePendingToggle处理 */
  }
}
/* USER CODE END 4 */

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
