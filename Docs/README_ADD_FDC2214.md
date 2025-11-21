把 FDC2214 数据手册加入工程并在 CubeMX 中添加 USART1 初始化

步骤 1 — 把 PDF 复制到工程目录（PowerShell）：

1. 打开 PowerShell（你提到 Windows 环境）。
2. 执行（把 C:\path\to\fdc2214.pdf 替换为你本地 PDF 的实际路径）：

   Copy-Item -Path "C:\path\to\fdc2214.pdf" -Destination "e:\Pelma_DC-CAP\Docs\fdc2214.pdf"

步骤 2 — 在 CubeMX 中为现有工程添加 USART1（不破坏用户代码）：

1. 打开 STM32CubeMX，选择 File -> Open Project，并打开你当前工程的 .ioc 文件（例：Pelma_DC-CAP.ioc）。
2. 在左侧的 Pinout & Configuration 界面里找到 Connectivity -> USART1，点击启用（Mode 选择 Asynchronous）。
3. 在 Configuration -> USART1 中设置波特率为 115200，Word length 8 Bits, Parity None, Stop bits 1。
4. 如果需要调试输出到特定引脚，确认 Pinout 里 TX/RX 的具体引脚（例如 PA9/PA10 或 MCU 不同，核对你的板子）。
5. 保存 .ioc 文件。
6. 点击 Project -> Generate Code（或工具栏的 Generate Code），CubeMX 会更新生成的 `usart.c/usart.h`，并在 `main.c` 中保留 `/* USER CODE BEGIN */` / `/* USER CODE END */` 区域，CubeMX 不会覆盖这些用户区间的代码。

注意事项：
- 在生成代码前，确保你在 IDE（VSCode/STM32CubeIDE）中没有打开正在被写入的文件，避免冲突。
- CubeMX 可能会修改 `main.c` 的初始化顺序（按配置的外设顺序），但它会保留用户代码区域。如果你在 `main.c` 中需要在 I2C 初始化之前或之后执行自定义初始化，请把代码放在对应的 `USER CODE` 区域。
- 生成代码后，检查 `Core/Src/usart.c`、`Core/Inc/usart.h` 是否已生成并包含 `MX_USART1_UART_Init()`。如果生成，请确保 `usart_debug.c` 中的 `extern UART_HandleTypeDef huart1;` 与生成的句柄一致（通常为 `huart1`）。

如果你把 `fdc2214.pdf` 放入 `e:\Pelma_DC-CAP\Docs`，回复我“已放入”，我会读取并用手册的确切寄存器表替换代码中的占位常量并微调初始化序列，然后再运行一次静态检查并把修改提交到仓库。
