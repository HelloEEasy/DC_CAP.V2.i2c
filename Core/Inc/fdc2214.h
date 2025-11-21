/*
 * fdc2214.h
 * FDC2214 capacitive sensing IC driver (header)
 *
 * 说明：基于 TI FDC2214 数据手册实现的简易驱动头文件。
 * 使用 STM32 HAL 的 I2C 句柄 `hi2c1`。
 * 请根据手册校对寄存器常量（这里已填入常见值）。
 */

#ifndef __FDC2214_H__
#define __FDC2214_H__

#include "main.h"

/* 7-bit I2C 地址（请根据 ADDR 引脚实际接法修改） 
when ADDR=L, I2C address = 0x2A, when ADDR=H, I2C address =0x2B.*///D
#define FDC2214_ADDR_7BIT    0x2A
/* HAL 接口通常使用左移 1 位的地址作为 DevAddress 参数 */
#define FDC2214_ADDR_HAL     (FDC2214_ADDR_7BIT << 1)

#define FDC2214_I2C_TIMEOUT_MS   100U

/* 设备寄存器（已按 TI 数据手册 Register Map 替换为精确地址） */
/* 数据寄存器：每通道由两个寄存器表示：DATA_CHx (MSB 部分) 和 DATA_LSB_CHx (LSB 部分) *///D
#define FDC2214_REG_DATA_CH0        0x00
#define FDC2214_REG_DATA_LSB_CH0    0x01
#define FDC2214_REG_DATA_CH1        0x02
#define FDC2214_REG_DATA_LSB_CH1    0x03
#define FDC2214_REG_DATA_CH2        0x04
#define FDC2214_REG_DATA_LSB_CH2    0x05
#define FDC2214_REG_DATA_CH3        0x06
#define FDC2214_REG_DATA_LSB_CH3    0x07

/* 通道配置寄存器 *///D，100SPS（SPS，全称Samples per Second ，即每秒采样次数，是转化速率的单位。100SPS每次采样时间TSAMPLE=10ms，1s 100次
//转换时间 Conversion Time (tC0)= 1/N × (TSAMPLE – settling time – channel switching delay) = 1/4 (10,000 – 4 – 1) =2.49875ms
#define FDC2214_REG_RCOUNT_CH0      0x08//N是通道数，确定转换时间寄存器值，请使用以下公式并求解CH0_RCOUNT转换时间：(tC0)= (CH0_RCOUNT × 16)/fREF0
#define FDC2214_REG_RCOUNT_CH1      0x09//CH0_RCOUNT=fREF0*tC0/16=(40*10的6次方*2.49875*10的-3次方)/16=6,246.875=》向下舍入：0x1866
#define FDC2214_REG_RCOUNT_CH2      0x0A
#define FDC2214_REG_RCOUNT_CH3      0x0B

#define FDC2214_REG_OFFSET_CH0      0x0C//0x0C-0x0F转换偏移，此项一般不做配置。只在FDC2112/FDC2114中设置。
#define FDC2214_REG_OFFSET_CH1      0x0D
#define FDC2214_REG_OFFSET_CH2      0x0E
#define FDC2214_REG_OFFSET_CH3      0x0F

#define FDC2214_REG_SETTLECOUNT_CH0 0x10
#define FDC2214_REG_SETTLECOUNT_CH1 0x11
#define FDC2214_REG_SETTLECOUNT_CH2 0x12
#define FDC2214_REG_SETTLECOUNT_CH3 0x13
//0x14-0x17通道0-3的频率选择，当采用差分传感器时，应配置为[13:12]为应设为b01。
//当采用单端传感器的时候（罗的用单端，采用单端输入，设置0x14的值为0x2001），[13:12]应配置为b10。[9:0]配置的参数用来缩放最大转换频率
#define FDC2214_REG_CLOCK_DIVIDERS_CH0 0x14
#define FDC2214_REG_CLOCK_DIVIDERS_CH1 0x15
#define FDC2214_REG_CLOCK_DIVIDERS_CH2 0x16
#define FDC2214_REG_CLOCK_DIVIDERS_CH3 0x17

/* 状态与全局配置寄存器 *///D
#define FDC2214_REG_STATUS         0x18
#define FDC2214_REG_STATUS_CONFIG  0x19
#define FDC2214_REG_CONFIG         0x1A//D
#define FDC2214_REG_MUX_CONFIG     0x1B//D

/* 参考/频率/错误/复位等控制寄存器 (按手册地址) */
// #define FDC2214_REG_ERROR_CONFIG   0x19
// #define FDC2214_REG_RESET_DEV      0x1C

/* 注意：REF_CLK_SRC 位位于 CONFIG 寄存器 (0x1A) 的 bit[9]。
 * 将该位设置为 1 可选择使用外部时钟作为 controller/参考时钟。
 * 我们提供位索引与掩码宏，便于读-改-写操作而不覆盖其它位。
 */
#define FDC2214_CONFIG_REF_CLK_SRC_BIT   (9U)
#define FDC2214_CONFIG_REF_CLK_SRC_MASK  (1U << FDC2214_CONFIG_REF_CLK_SRC_BIT)  /* 0x0200 */
#define FDC2214_CONFIG_REF_CLK_SRC_INTERNAL  (0U)
#define FDC2214_CONFIG_REF_CLK_SRC_EXTERNAL (FDC2214_CONFIG_REF_CLK_SRC_MASK)


/* 驱动电流寄存器（每通道） *///D
#define FDC2214_REG_DRIVE_CURRENT_CH0 0x1E
#define FDC2214_REG_DRIVE_CURRENT_CH1 0x1F
#define FDC2214_REG_DRIVE_CURRENT_CH2 0x20
#define FDC2214_REG_DRIVE_CURRENT_CH3 0x21

/* 厂商 ID / 设备 ID 寄存器地址（寄存器在高地址区） *///D
#define FDC2214_REG_MANUF_ID      0x7E
#define FDC2214_REG_DEVICE_ID     0x7F

/* 如果手册中给出 DEVICE_ID 的期望值，可在此定义以便 fdc_init 校验 */
/* TI 数据手册中给出的 Manufacturer ID = 0x5449, Device ID (FDC2212/2214) = 0x3055 */
#define FDC2214_MANUFACTURER_ID   0x5449
#define FDC2214_EXPECTED_DEVICE_ID 0x3055

typedef enum {
    FDC_CH0 = 0,
    FDC_CH1 = 1,
    FDC_CH2 = 2,
    FDC_CH3 = 3,
} fdc_channel_t;

typedef enum {
    FDC_OK = 0,
    FDC_ERR_I2C = -1,
    FDC_ERR_INVALID_PARAM = -2,
    FDC_ERR_TIMEOUT = -3,
    FDC_ERR_UNKNOWN = -10,
} fdc_status_t;

/* API 用法说明：
 * - 在 main 的初始化阶段调用 fdc_init()。
 * - 使用 fdc_read_result_raw() 读取 24-bit 原始结果（unit: raw counts / frequency）。
 * - 若需要写特殊寄存器或进行复杂配置，可直接使用 fdc_read_reg / fdc_write_reg。
 */
int fdc_init(void);
int fdc_write_reg(uint8_t reg, uint16_t value);
int fdc_read_reg(uint8_t reg, uint16_t *value);
int fdc_read_result_raw(fdc_channel_t ch, uint32_t *raw24);
int fdc_soft_reset(void);
int fdc_read_device_id(uint16_t *did);

/* 返回错误字符串，供上层打印使用 */
const char *fdc_err_str(int e);

/* 将寄存器原始输出 DATAx (无符号整数，驱动中为 28-bit 有效位) 转换为振荡器频率 f_sensor（Hz）
 * 公式：DATAx = f_sensor * 2^28 / f_ref  => f_sensor = DATAx * f_ref / 2^28
 * 参数：raw24 - 驱动读取到的原始值（uint32_t）
 *       fref_hz - 参考时钟频率（Hz），例如 40e6 表示 40 MHz
 * 返回：fsensor (Hz)（double）
 */
double fdc_raw_to_freq(uint32_t raw24, double fref_hz);

/* 根据振荡频率计算被测电容 C（法拉）
 * 公式：f_sensor = 1 / (2*pi*sqrt(L*(C + C0))) -> 由 datasheet/文档变形得到：
 * C = 1/(L*(2*pi*f_sensor)^2) - C0
 * 参数：fsensor_hz - 振荡频率 (Hz)
 *       L_h - 已知电感量 (H)
 *       C0_f - 并联固定电容 (F), 如 20e-12 表示 20 pF
 * 返回：C (F)
 */
double fdc_freq_to_capacitance(double fsensor_hz, double L_h, double C0_f);

/* 简单基线校准：读取 N 次样本求均值，保存到用户数组 */
int fdc_calibrate_baseline(fdc_channel_t ch, uint32_t *baseline_out, uint8_t samples);

#endif /* __FDC2214_H__ */
