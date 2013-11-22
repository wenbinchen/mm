/*
 * Author: Xiangfu Liu <xiangfu@openmobilefree.net>
 * Bitcoin:	1CanaaniJzgps8EV6Sfmpb7T8RutpaeyFn
 *
 * This is free and unencumbered software released into the public domain.
 * For details see the UNLICENSE file at the root of the source tree.
 */

#include <stdint.h>

#include "minilibc.h"
#include "system_config.h"
#include "defines.h"
#include "intr.h"
#include "io.h"


#define UART_RINGBUFFER_SIZE_RX 128
#define UART_RINGBUFFER_MASK_RX (UART_RINGBUFFER_SIZE_RX-1)

static char rx_buf[UART_RINGBUFFER_SIZE_RX];
static volatile unsigned int rx_produce;
static volatile unsigned int rx_consume;

#define UART_RINGBUFFER_SIZE_TX 128
#define UART_RINGBUFFER_MASK_TX (UART_RINGBUFFER_SIZE_TX-1)

static char tx_buf[UART_RINGBUFFER_SIZE_TX];
static unsigned int tx_produce;
static unsigned int tx_consume;
static volatile int tx_cts;

static int force_sync;

static struct lm32_uart *uart = (struct lm32_uart *)UART0_BASE;
static struct lm32_uart *uart1 = (struct lm32_uart *)UART1_BASE;

void uart_isr(void)
{
	uint8_t stat = readb(&uart->iir);

	if (stat & LM32_UART_STAT_RX_EVT) {
		while (readb(&uart->lsr) & LM32_UART_LSR_DR) {
			rx_buf[rx_produce] = readb(&uart->rxtx);
			rx_produce = (rx_produce + 1) & UART_RINGBUFFER_MASK_RX;
		}
	}

	if (stat & LM32_UART_STAT_TX_EVT) {
		if (tx_produce != tx_consume) {
			writeb(tx_buf[tx_consume], &uart->rxtx);
			tx_consume = (tx_consume + 1) & UART_RINGBUFFER_MASK_TX;
		} else {
			tx_cts = 1;
			stat = readb(&uart->ier);
			stat &= ~LM32_UART_IER_THRI;
			writeb(stat, &uart->ier);
		}
	}

	irq_ack(IRQ_UART);
}

/* Do not use in interrupt handlers! */
char uart_read(void)
{
	char c;

	while (rx_consume == rx_produce);
	c = rx_buf[rx_consume];
	rx_consume = (rx_consume + 1) & UART_RINGBUFFER_MASK_RX;
	return c;
}

int uart_read_nonblock(void)
{
	return (rx_consume != rx_produce);
}

void uart_write(char c)
{
	unsigned int oldmask;
	unsigned char stat;

	oldmask = irq_getmask();
	irq_setmask(0);

	if (force_sync) {
		while (!(readb(&uart->lsr) & (LM32_UART_LSR_THRR | LM32_UART_LSR_TEMT)))
			;
		writeb(c, &uart->rxtx);
	} else {
		stat = readb(&uart->ier);
		stat |= LM32_UART_IER_THRI;
		writeb(stat, &uart->ier);

		if (tx_cts) {
			tx_cts = 0;
			writeb(c, &uart->rxtx);
		} else {
			tx_buf[tx_produce] = c;
			tx_produce = (tx_produce + 1) & UART_RINGBUFFER_MASK_TX;
		}
	}
	irq_setmask(oldmask);
}

void uart_force_sync(int f)
{
	force_sync = f;
}

void uart_init(void)
{
	uint32_t mask;
	uint8_t value;

	rx_produce = 0;
	rx_consume = 0;
	tx_produce = 0;
	tx_consume = 0;
	tx_cts = 1;

	irq_ack(IRQ_UART);

	/* enable UART interrupts */
	writeb(LM32_UART_IER_RBRI, &uart->ier);
	mask = irq_getmask();
	mask |= IRQ_UART;
	irq_setmask(mask);

	/* Line control 8 bit, 1 stop, no parity */
	writeb(LM32_UART_LCR_8BIT, &uart->lcr);

	/* Modem control, DTR = 1, RTS = 1 */
	writeb(LM32_UART_MCR_DTR | LM32_UART_MCR_RTS, &uart->mcr);

	/* Set baud rate */
	value = (CPU_FREQUENCY / UART_BAUD_RATE) & 0xff;
	writeb(value, &uart->divl);
	value = (CPU_FREQUENCY / UART_BAUD_RATE) >> 8;
	writeb(value, &uart->divh);

}

void uart_puts(const char *s)
{
	while (*s) {
		if (*s == '\n')
			uart_write('\r');
		uart_write(*s++);
	}
}

void uart_nwrite(const char *s, unsigned int l)
{
	while (l--)
		uart_write(*s++);
}


void uart1_init(void)
{
	uint8_t value;

	/* Line control 8 bit, 1 stop, no parity */
	writeb(LM32_UART_LCR_8BIT, &uart1->lcr);

	/* Modem control, DTR = 1, RTS = 1 */
	writeb(LM32_UART_MCR_DTR | LM32_UART_MCR_RTS, &uart1->mcr);

	/* Set baud rate */
	value = (CPU_FREQUENCY / UART_BAUD_RATE) & 0xff;
	writeb(value, &uart1->divl);
	value = (CPU_FREQUENCY / UART_BAUD_RATE) >> 8;
	writeb(value, &uart1->divh);

}

void uart1_write(char c)
{
	if (c == '\n')
		uart1_write('\r');

	while (!(readb(&uart1->lsr) & (LM32_UART_LSR_THRR | LM32_UART_LSR_TEMT)))
		;
	writeb(c, &uart1->rxtx);
}

void uart1_puts(const char *s)
{
	while (*s)
		uart1_write(*s++);
}