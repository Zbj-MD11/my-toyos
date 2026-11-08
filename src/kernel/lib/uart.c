/* low-level driver routines for 16550a UART. */

#include "mod.h"

// from printf.c 终止输出的标志
extern volatile int panicked;

// uart 初始化
void uart_init(void)
{
  	// 关闭中断
	WriteReg(IER, 0x00);

	// 进入设置比特率的模式
	WriteReg(LCR, LCR_BAUD_LATCH);

	// 设置比特率的低位和高位，最终设置为38.4K
	WriteReg(0, 0x03);
  	WriteReg(1, 0x00);

	// 设置传输字节长度为8bit,不校验
	WriteReg(LCR, LCR_EIGHT_BITS);

	// 清零和使能FIFO模式
	WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

	// 使能输出队列和接收队列的中断
	WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
}

// 单个字符输出
void uart_putc_sync(int c)
{
	// 关闭中断
	push_off();

	// 如果错误发生则卡住
	while (panicked)
		;

	// 等待TX队列进入idle状态
	while ((ReadReg(LSR) & LSR_TX_IDLE) == 0)
		;

	// 输出
	WriteReg(THR, c);

	// 开启中断
	pop_off();
}

// 单个字符输入
// 失败返回-1
int uart_getc_sync(void)
{
	if (ReadReg(LSR) & 0x01)
		return ReadReg(RHR);
	else
		return -1;
}
// 记录当前输入行的字符数量
static int line_len = 0;

// 记录上一个字符是否为 CR，用于处理 CRLF
static int last_was_cr = 0;

// 中断处理（键盘输入 -> 屏幕输出）
void uart_intr(void)
{
    while (1)
    {
        int c = uart_getc_sync();

        // 接收队列为空，退出
        if (c == -1){
			 break;
		}
           

        // 1. 处理 CR
        if (c == '\r'){
            uart_putc_sync('\r');
            uart_putc_sync('\n');

            line_len = 0;
            last_was_cr = 1;
        }

        // 2. 处理 LF
        else if (c == '\n'){
            // 如果前一个字符不是 CR，说明这是独立的 LF
            if (!last_was_cr)
            {
                uart_putc_sync('\r');
                uart_putc_sync('\n');

                line_len = 0;
            }

            // 无论是否忽略 LF，都清除 CR 状态
            last_was_cr = 0;
        }

        // 3. 处理 BS 和 DEL
        else if (c == '\b' || c == 0x7f){
            last_was_cr = 0;

            // 只有当前行存在字符时才能退格
            if (line_len > 0)
            {
                uart_putc_sync('\b');
                uart_putc_sync(' ');
                uart_putc_sync('\b');

                line_len--;
            }
        }

        // 4. 普通字符
        else
        {
            last_was_cr = 0;

            uart_putc_sync(c);
            line_len++;
        }
    }
}
