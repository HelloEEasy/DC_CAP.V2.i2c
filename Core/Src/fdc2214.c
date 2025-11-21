/*
 * fdc2214.c
 * 简易 FDC2214 驱动实现（基于 STM32 HAL I2C）
 *
 * 说明：
 * - 本驱动使用 STM32 HAL 的阻塞 I2C 接口（HAL_I2C_Master_Transmit / Receive），
 *   实现读写寄存器、读取 24-bit 通道结果、设备初始化与基线校准等常用功能。
 * - 文件中仍然保留了一些占位寄存器值（请以手头的 FDC2214 数据手册为准）      //调###############
 * - 注释为逐行详细中文注释，解释每一行代码的目的、原因和实现注意点，便于阅读与移植。
 */

#include "fdc2214.h"
#include "i2c.h"    /* 提供 extern I2C_HandleTypeDef hi2c1；由 CubeMX 在工程中生成 */
#include "main.h"   /* 提供 HAL_Delay、以及工程级别的头文件包含 */
#include <stdint.h>  /* 提供标准整型定义（uint8_t/uint16_t/uint32_t/uint64_t） */
#include "usart_debug.h"
// #include <math.h>
/* hi2c1 是在工程其他地方（通常由 CubeMX 生成的 i2c.c）声明并初始化的 I2C 句柄。
 * 在本模块中通过 extern 声明使用它进行 I2C 传输。
 */
extern I2C_HandleTypeDef hi2c1;


/*
 * 内部工具函数：i2c_tx_retry
 * 说明：对 HAL_I2C_Master_Transmit 做简单的重试封装。
 * 参数：
 *  - pData: 指向要发送的数据缓冲区的指针（包含寄存器地址 + 数据等）
 *  - Size: 要发送的字节数
 *  - Timeout: HAL I2C 函数的超时时间（ms）
 *  - retries: 最大重试次数
 * 返回：FDC_OK（0）表示成功，FDC_ERR_I2C 表示在重试后仍然失败
 * 设计理由：I2C 总线在实际使用中会遇到偶发通信错误（仲裁、噪声、拉低等），
 * 使用短次数重试通常可以提高鲁棒性而不引入复杂的错误恢复逻辑。            //学###############
 */
static int i2c_tx_retry(uint8_t *pData, uint16_t Size, uint32_t Timeout, int retries)
{
    /* HAL_StatusTypeDef 用于接收 HAL API 的返回状态（HAL_OK / HAL_ERROR / HAL_BUSY / HAL_TIMEOUT） */
    HAL_StatusTypeDef st;                      //学###############
    /* 循环尝试发送，最多尝试 retries 次 */
    for (int i = 0; i < retries; ++i) {
        /* 使用 HAL 的阻塞式主机发送函数：地址使用 FDC2214_ADDR_HAL（左移后）
         * 注意：HAL 接口的 DevAddress 参数期望的是 7-bit 地址左移一位后的值（即 8-bit 地址格式）
         */
        st = HAL_I2C_Master_Transmit(&hi2c1, FDC2214_ADDR_HAL, pData, Size, Timeout);
        /* 如果发送成功，立即返回 FDC_OK（0）表示驱动层成功 */
        if (st == HAL_OK) return FDC_OK;
        /* 若非成功，等待少量时间再重试（短延时 5ms 可避免紧循环占用总线） */
        HAL_Delay(5);
    }
    /* 所有重试耗尽仍失败，则返回 I2C 错误码 */
    return FDC_ERR_I2C;
}


/*
 * 内部工具函数：i2c_rx_retry
 * 说明：对 HAL_I2C_Master_Receive 做简单的重试封装，与 i2c_tx_retry 对称。
 * 参数与返回值语义与 i2c_tx_retry 相同。
 */
static int i2c_rx_retry(uint8_t *pData, uint16_t Size, uint32_t Timeout, int retries)
{
    HAL_StatusTypeDef st;
    for (int i = 0; i < retries; ++i) {
        /* 接收数据：从设备读取 Size 字节到 pData 缓冲区 */
        st = HAL_I2C_Master_Receive(&hi2c1, FDC2214_ADDR_HAL, pData, Size, Timeout);
        if (st == HAL_OK) return FDC_OK;
        /* 收到非 OK 状态时短延时再重试 */
        HAL_Delay(5);
    }
    return FDC_ERR_I2C;
}


/*
 * fdc_write_reg
 * 写入一个 16-bit 寄存器到 FDC2214。
 * FDC2214 的 I2C 写操作协议通常为：先发送 1 字节的寄存器地址，随后发送 2 字节的数据（MSB 首）。
 * 我们将寄存器地址 + MSB + LSB 一次性放在发送缓冲区并调用 HAL 发送。
 * 参数：reg - 目标寄存器地址（8-bit）
 *       value - 要写入的 16-bit 值（驱动将按大端顺序发送）
 * 返回：FDC_OK 或 FDC_ERR_I2C
 */
int fdc_write_reg(uint8_t reg, uint16_t value)
{
    /* 发送缓冲区：第 0 字节为寄存器地址，第 1/2 字节为 16-bit 数据（MSB, LSB） */
    uint8_t buf[3]; 
    buf[0] = reg; /* 寄存器地址 */
    buf[1] = (uint8_t)((value >> 8) & 0xFF); /* 高 8 位（MSB） */
    buf[2] = (uint8_t)(value & 0xFF);        /* 低 8 位（LSB） */

    /* 使用重试封装发送，超时使用驱动头文件定义的宏 */
    return i2c_tx_retry(buf, 3, FDC2214_I2C_TIMEOUT_MS, 3);
}


/*
 * fdc_read_reg
 * 读取一个 16-bit 寄存器的值。
 * FDC2214 的读取步骤：先写入 1 字节的寄存器地址（设置内部指针），随后从设备读出 2 字节数据（MSB, LSB）。
 * 参数：reg - 要读取的寄存器地址
 *       value - 输出指针，存放读取到的 16-bit 值（大端合成）
 * 返回：FDC_OK / 错误码
 */
int fdc_read_reg(uint8_t reg, uint16_t *value)
{
    /* 参数校验：禁止空指针 */
    if (value == NULL) return FDC_ERR_INVALID_PARAM;

    /* 把寄存器地址放到单字节缓冲区中，作为写指令发送给设备，告诉设备后续要读哪个寄存器 */
    uint8_t regb = reg;

    /* 将寄存器地址发送到设备（设置内部寄存器指针）。若失败直接返回错误码 */
    int ret = i2c_tx_retry(&regb, 1, FDC2214_I2C_TIMEOUT_MS, 3);
    if (ret != FDC_OK) return ret; /* 如果写地址阶段失败，则无需继续读取 */

    /* 读取 2 字节的数据（MSB, LSB） */
    uint8_t rx[2];   //从从机地址的寄存器（就是上面发送的寄存器）读取2个字节的数据到pData也就是rx缓冲区
    ret = i2c_rx_retry(rx, 2, FDC2214_I2C_TIMEOUT_MS, 3);
    if (ret != FDC_OK) return ret; /* 读取阶段失败，返回错误 */

    /* 按大端序合成 16-bit 值：rx[0] 为高字节，rx[1] 为低字节 */   //学###############
    *value = ((uint16_t)rx[0] << 8) | rx[1];
    return FDC_OK;
}


/*
 * fdc_read_result_raw
 * 读取某一通道的原始 24-bit 转  换结果（FDC2214 为 28-bit 内部结果，但寄存器输出分为 MSB/LSB 等，
 * 本实现假设按 3 字节连续方式读取 24-bit 的 MSB..LSB，具体寄存器映像请以 datasheet 为准）。  //调###############
 * 参数：ch - 通道枚举（FDC_CH0..FDC_CH3）
 *       raw24 - 输出指针，返回合成后的 24-bit 无符号值（放入 32-bit 容器）
 * 返回：FDC_OK / 参数错误 / I2C 错误
 */
int fdc_read_result_raw(fdc_channel_t ch, uint32_t *raw24)
{
    /* 验证输出指针不为 NULL */
    if (raw24 == NULL) return FDC_ERR_INVALID_PARAM;
    /* 验证通道枚举值在允许范围内 */
    if (ch < FDC_CH0 || ch > FDC_CH3) return FDC_ERR_INVALID_PARAM;

    /* 按 datasheet：每个通道由 DATA_CHx (MSB 部分) 和 DATA_LSB_CHx (LSB 部分) 两个 16-bit 寄存器组成。
     * 必须先读 MSB 寄存器（DATA_CHx），再读对应的 LSB 寄存器（DATA_LSB_CHx），以保证读取的一致性。
     */

    /* 计算 MSB/LSB 地址：DATA_CH0 在0x00，通道间间隔为 2
    在很多传感器（如这里的 FDC2214 是一款电容传感器）中，16 位或更高位数的数据通常会分存到两个 8 位寄存器中 */
    uint8_t addr_msb = (uint8_t)(FDC2214_REG_DATA_CH0 + (ch * 2));
    // 注释提到 “通道间间隔为 2”，意思是每个通道的数据寄存器（包含 MSB 和 LSB）占用 2 个连续地址。
    // 因此，通道号 ch 每增加 1，地址偏移量就增加 2。通道 1（ch=1）：偏移 1*2=2 → MSB 地址 = 0x00 + 2 = 0x02
    uint8_t addr_lsb = (uint8_t)(addr_msb + 1);

    /* 读 MSB 寄存器（2 字节） */
    int ret = i2c_tx_retry(&addr_msb, 1, FDC2214_I2C_TIMEOUT_MS, 3); /* 设置寄存器地址 */
    if (ret != FDC_OK) return ret;
    uint8_t msb_buf[2];
    ret = i2c_rx_retry(msb_buf, 2, FDC2214_I2C_TIMEOUT_MS, 3); /* 读取 MSB 的两个字节 */
    if (ret != FDC_OK) return ret;

    /* 读 LSB 寄存器（2 字节） */
    ret = i2c_tx_retry(&addr_lsb, 1, FDC2214_I2C_TIMEOUT_MS, 3);
    if (ret != FDC_OK) return ret;
    uint8_t lsb_buf[2];
    ret = i2c_rx_retry(lsb_buf, 2, FDC2214_I2C_TIMEOUT_MS, 3);
    if (ret != FDC_OK) return ret;

    /* 合成 28-bit 结果：MSB 的低 12-bit 为高位部分，LSB 提供低 16-bit
     * result = (MSB[11:0] << 16) | LSB[15:0]
     */
    uint32_t msb16 = ((uint16_t)msb_buf[0] << 8) | msb_buf[1];
    uint32_t lsb16 = ((uint16_t)lsb_buf[0] << 8) | lsb_buf[1];
    *raw24 = ((msb16 & 0x0FFFU) << 16) | (lsb16 & 0xFFFFU);
    return FDC_OK;
}


/*
 * fdc_read_device_id
 * 便捷函数：读取 DEVICE_ID 寄存器（16-bit）并返回
 */
int fdc_read_device_id(uint16_t *did)
{
    /* 直接调用通用寄存器读取函数，寄存器地址由头文件宏定义 */
    return fdc_read_reg(FDC2214_REG_DEVICE_ID, did);  //学###############
}


/*
 * fdc_set_ref_clk_external
 * 将 CONFIG (0x1A) 的 REF_CLK_SRC 位设置为 1，以选择外部时钟源。
 * 实现使用读-改-写，确保不破坏 CONFIG 中的其他位。
 */
int fdc_set_ref_clk_external(void)
{
    uint16_t cfg = 0;
    int ret = fdc_read_reg(FDC2214_REG_CONFIG, &cfg);
    if (ret != FDC_OK) return ret; /* 读取失败，返回上层处理 */

    /* 置位 REF_CLK_SRC（位 9） */
    cfg |= FDC2214_CONFIG_REF_CLK_SRC_MASK;

    /* 写回 CONFIG 寄存器 */
    return fdc_write_reg(FDC2214_REG_CONFIG, cfg);
}

/* 返回错误码对应的可读字符串，便于打印调试信息 */
const char *fdc_err_str(int e)
{
    switch (e) {
        case FDC_OK: return "OK";
        case FDC_ERR_I2C: return "I2C_ERROR";
        case FDC_ERR_INVALID_PARAM: return "ERR_INVALID_PARAM";
        case FDC_ERR_TIMEOUT: return "ERR_TIMEOUT";
        case FDC_ERR_UNKNOWN: return "ERR_UNKNOWN";
        default: return "ERR_OTHER";
    }
}

/* 将寄存器原始输出 DATAx (raw24) 转换为振荡器频率 f_sensor（Hz）。
 * 依据 datasheet：DATAx = f_sensor * 2^28 / f_ref
 */
double fdc_raw_to_freq(uint32_t raw24, double fref_hz)
{
    /* 使用 double 进行计算以保留精度 */
    return ((double)raw24) * fref_hz / (double)(1ULL << 28);
}

/* 根据振荡频率计算被测电容 C（法拉）
 * 公式：C = 1/(L*(2*pi*f_sensor)^2) - C0
 */
double fdc_freq_to_capacitance(double fsensor_hz, double L_h, double C0_f)
{
    if (fsensor_hz <= 0.0 || L_h <= 0.0) return -1.0; /* error sentinel */
    double omega = 2.0 * 3.14159265358979323846 * fsensor_hz;
    double C_total = 1.0 / (L_h * omega * omega);
    double C = C_total - C0_f;
    return C;
}


/*
 * fdc_soft_reset
 * 软件复位设备的一个示例实现：
 * - 有些芯片通过写 RESET_DEV 或 CONFIG 寄存器的特定位实现复位。
 * - 这里演示写入一个占位复位命令（RESET_CMD），在投入实际硬件前请用 datasheet 中的确切命令替换该常量。
 */
int fdc_soft_reset(void)
{
    /* 占位的复位命令：高位 0x8000 表示触发某个复位位（仅示例）
     * 实际上请以 datasheet 的 RESET_DEV 或 CONFIG 字段为准。 */
    const uint16_t RESET_CMD = 0x8000; /* 占位值，切勿直接用于生产 */
    /* 调用通用写寄存器函数写入 CONFIG（或 RESET_DEV）寄存器触发复位 */
    return fdc_write_reg(FDC2214_REG_CONFIG, RESET_CMD);
}


/*
 * fdc_init
 * 简单的设备初始化示例：
 * 1) 读取 DEVICE_ID，确认设备存在并可通信
 * 2) （可选）软件复位
 * 3) 写入若干默认配置寄存器以使能测量
 * 注意：本函数所写入的寄存器值为占位值，请在使用前用手册中的建议值替换           //调###############
 */
int fdc_init(void)
{
    /* 读取设备 ID，确保 I2C 通信正常。如果读失败，返回 I2C 错误 */
    uint16_t did = 0;//value - 输出指针，存放读取到的 16-bit 值（大端合成）,就是did，用于存放设备ID
    if (fdc_read_device_id(&did) != FDC_OK) {
        /* 设备未响应或 I2C 错误 */
        return FDC_ERR_I2C;
    }
    if (did != FDC2214_EXPECTED_DEVICE_ID) {
        fdc_debug_print("Warning: unexpected DEVICE_ID=0x%04X, continuing\r\n", did);
        /* 可选择不返回错误，继续初始化 */
    }
    /* 可选：检查设备 ID 是否为期望值。如果你知道确切的 DEVICE_ID，可以启用下面的检查。 */
    /* if (did != FDC2214_EXPECTED_DEVICE_ID) return FDC_ERR_UNKNOWN; */
    /* 根据用户要求写入一组初始化寄存器值（顺序：CLOCK_DIVIDERS, DRIVE_CURRENT, SETTLECOUNT,
     * RCOUNT, MUX_CONFIG, 最后写入 CONFIG）。这些值均由用户指定以覆盖之前的 REF_CLK_SRC 单个位设置。
     * 注意：此处对某些寄存器使用了显式地址（例如 0x1E/0x1F），这是按照用户指示写入。
     */

    /* 1)分频器配置 CLOCK_DIVIDERS CH0..CH3 = 0x2001 */
    if (fdc_write_reg(FDC2214_REG_CLOCK_DIVIDERS_CH0, 0x2001) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_CLOCK_DIVIDERS_CH1, 0x2001) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_CLOCK_DIVIDERS_CH2, 0x2001) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_CLOCK_DIVIDERS_CH3, 0x2001) != FDC_OK) return FDC_ERR_I2C;

    /* 2) DRIVE_CURRENT registers: 按用户要求写入到 0x1E,0x1F,0x20,0x21 = 0x7800
     * 说明：头文件中常见的 DRIVE_CURRENT_CH0..CH3 为 0x20..0x23，
     * 这里仍按用户指定的地址写入（包括 0x1E/0x1F）。
     * 
    传感器驱动电流 */
    if (fdc_write_reg(0x1E, 0x7800) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(0x1F, 0x7800) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(0x20, 0x7800) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(0x21, 0x7800) != FDC_OK) return FDC_ERR_I2C;

    /* 3)各通道所需时间 SETTLECOUNT CH0..CH3 = 0x000A */
    if (fdc_write_reg(FDC2214_REG_SETTLECOUNT_CH0, 0x000A) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_SETTLECOUNT_CH1, 0x000A) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_SETTLECOUNT_CH2, 0x000A) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_SETTLECOUNT_CH3, 0x000A) != FDC_OK) return FDC_ERR_I2C;

    /* 5)转换时间 RCOUNT CH0..CH3 = 0x1866 */
    if (fdc_write_reg(FDC2214_REG_RCOUNT_CH0, 0x1866) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_RCOUNT_CH1, 0x1866) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_RCOUNT_CH2, 0x1866) != FDC_OK) return FDC_ERR_I2C;
    if (fdc_write_reg(FDC2214_REG_RCOUNT_CH3, 0x1866) != FDC_OK) return FDC_ERR_I2C;

    /* 6) MUX_CONFIG (0x1B) = 0xC20D */
    if (fdc_write_reg(FDC2214_REG_MUX_CONFIG, 0xC20D) != FDC_OK) return FDC_ERR_I2C;

    /* 7) CONFIG (0x1A) = 0x1601 - 按用户要求一次性写入（替换掉之前只设置 REF_CLK_SRC 的逻辑） */
    if (fdc_write_reg(FDC2214_REG_CONFIG, 0x1601) != FDC_OK) return FDC_ERR_I2C;

    /* 如果需要进一步配置测量参数（例如 RCOUNT、SETTLECOUNT、DRIVE_CURRENT 等），   //调###############
     * 请在这里分别写入对应寄存器：
     *   fdc_write_reg(FDC2214_REG_RCOUNT_CH0, value);
     *   fdc_write_reg(FDC2214_REG_SETTLECOUNT_CH0, value);
     *   fdc_write_reg(FDC2214_REG_DRIVE_CURRENT_CH0, value);
     * 这些都是按通道逐个配置的，不同应用会有不同的推荐值，务必参照 datasheet。
     */

    /* 等待小段时间，让设备内部电路收敛并使配置生效（例如内部参考/振荡器等） */
    HAL_Delay(10);
    return FDC_OK;
}


/*DC2214 是电容式测量芯片，测到的是“绝对电容/电容变动”的数字值。
不同 PCB 布线、芯片安装位置、传感器初始电容、周围寄生电容和生产公差都会导致“静态偏置”（baseline）存在。
真实应用中我们更关心“变化量”（delta），即相对基线的变化（例如触摸、接近、物体靠近等），
因此要先测出一个参考基线，然后把实时值减去基线才能得到有意义的变化量。
环境因素（温度、湿度、电源噪声）会慢变或突变，基线校准可以在已知静态场景下把这些偏置去掉，
提高检测的鲁棒性与灵敏度

在 main.c 中定义了 static uint32_t baseline[4]，并在主循环用 delta = raw - baseline[ch] 来判断变化。
所以要先调用 fdc_calibrate_baseline(ch, &baseline[ch], samples)（通常在初始化阶段的 USER CODE 部分）来填
 baseline，否则 delta 将没有意义或出现大偏移。

明确说明该函数用于启动时或环境变化时采集参考基线，便于后续用 raw - baseline 计算 delta。
 * fdc_calibrate_baseline
 * 对指定通道做简单的基线（baseline）平均校准：
 * - 连续读取 samples 次原始值并返回平均值
 * - 该函数用于在初始化阶段或环境变化时采集参考基线
 * 参数：ch - 通道编号
 *       baseline_out - 输出平均值
 *       samples - 采样次数（建议至少 8+）
 */
int fdc_calibrate_baseline(fdc_channel_t ch, uint32_t *baseline_out, uint8_t samples)
{
    /* 参数验证：baseline_out 不可为 NULL，samples 不可为 0 */
    if (baseline_out == NULL || samples == 0) return FDC_ERR_INVALID_PARAM;

    /* 使用 64-bit 累加器避免在累加大量 24-bit 值时溢出 */
    uint64_t acc = 0;
    for (uint8_t i = 0; i < samples; ++i) {
        uint32_t v = 0;
        /* 读取当前通道的原始值；若读取失败则把错误直接返回给上层（调用者可决定重试策略） */
        int ret = fdc_read_result_raw(ch, &v);
        if (ret != FDC_OK) return ret;
        /* 累加采样值 */
        acc += v;
        /* 等待短延时以避免紧密循环导致总线拥塞或让传感器振荡稳定 */
        HAL_Delay(5);//5ms 是个经验值，可根据你的测量速率与 datasheet 推荐调整 //调###############
    }

    /* 计算平均值并返回 32-bit 基线值（舍弃小数部分）
    多次采样取平均可以抑制随机噪声和瞬态抖动（比如 I2C 抖动、传感器瞬时跳动） */
    *baseline_out = (uint32_t)(acc / samples);
    return FDC_OK;
}
