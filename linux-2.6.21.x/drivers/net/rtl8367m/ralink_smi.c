
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/ralink_gpio.h>

#include "ralink_smi.h"

#define SMI_ACK_RETRY_COUNT 5

static spinlock_t g_smi_lock;
static u32 g_gpio_sda = 1;
static u32 g_gpio_sck = 2;
static u32 g_clk_delay_ns = 1500;
static u8  g_addr_slave = 0xB8;

/////////////////////////////////////////////////////////////////////////////////

void _gpio_direction_output(u32 pin)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODIR));
		tmp |= (1L << pin);
		*(volatile u32 *)(RALINK_REG_PIODIR) = cpu_to_le32(tmp);
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DIR));
		tmp |= (1L << (pin-24));
		*(volatile u32 *)(RALINK_REG_PIO3924DIR) = cpu_to_le32(tmp);
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DIR));
		tmp |= (1L << (pin-40));
		*(volatile u32 *)(RALINK_REG_PIO5140DIR) = cpu_to_le32(tmp);
	}
}

void _gpio_direction_input(u32 pin)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODIR));
		tmp &= ~(1L << pin);
		*(volatile u32 *)(RALINK_REG_PIODIR) = cpu_to_le32(tmp);
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DIR));
		tmp &= ~(1L << (pin-24));
		*(volatile u32 *)(RALINK_REG_PIO3924DIR) = cpu_to_le32(tmp);
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DIR));
		tmp &= ~(1L << (pin-40));
		*(volatile u32 *)(RALINK_REG_PIO5140DIR) = cpu_to_le32(tmp);
	}
}

void _gpio_set_value(u32 pin, u32 value)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODATA));
		if (value)
			tmp |= (1L << pin);
		else
			tmp &= ~(1L << pin);
		*(volatile u32 *)(RALINK_REG_PIODATA) = cpu_to_le32(tmp);
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DATA));
		if (value)
			tmp |= (1L << (pin-24));
		else
			tmp &= ~(1L << (pin-24));
		*(volatile u32 *)(RALINK_REG_PIO3924DATA) = cpu_to_le32(tmp);
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DATA));
		if (value)
			tmp |= (1L << (pin-40));
		else
			tmp &= ~(1L << (pin-40));
		*(volatile u32 *)(RALINK_REG_PIO5140DATA) = cpu_to_le32(tmp);
	}
}

u32 _gpio_get_value(u32 pin)
{
	u32 tmp;

	if (pin <= 23) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIODATA));
		tmp = (tmp >> pin) & 1L;
	}
	else if (pin <= 39) {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO3924DATA));
		tmp = (tmp >> (pin-24)) & 1L;
	}
	else {
		tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_PIO5140DATA));
		tmp = (tmp >> (pin-40)) & 1L;
	}

	return tmp;
}

u32 _gpio_mode_set_bit(u32 idx, u32 value)
{
	u32 tmp;

	if (idx > 31) idx = 31;

	tmp = le32_to_cpu(*(volatile u32 *)(RALINK_REG_GPIOMODE));
	if (value)
		tmp |= (1L << idx);
	else
		tmp &= ~(1L << idx);
	*(volatile u32 *)(RALINK_REG_GPIOMODE) = cpu_to_le32(tmp);
	
	return tmp;
}

u32 _gpio_mode_get(void)
{
	return le32_to_cpu(*(volatile u32 *)(RALINK_REG_GPIOMODE));
}

void _gpio_mode_set(u32 value)
{
	*(volatile u32 *)(RALINK_REG_GPIOMODE) = cpu_to_le32(value);
}


/////////////////////////////////////////////////////////////////////////////////

static inline void _smi_clk_delay(void)
{
	ndelay(g_clk_delay_ns);
}

static void _smi_start(void)
{
	/*
	 * Set GPIO pins to output mode, with initial state:
	 * SCK = 0, SDA = 1
	 */
	_gpio_direction_output(g_gpio_sck);
	_gpio_set_value(g_gpio_sck, 0);
	
	_gpio_direction_output(g_gpio_sda);
	_gpio_set_value(g_gpio_sda, 1);
	_smi_clk_delay();

	/* CLK 1: 0 -> 1, 1 -> 0 */
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();

	/* CLK 2: */
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 1);
}

static void _smi_stop(void)
{
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 0);
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sda, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 1);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 1);

	/* add a click */
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 0);
	_smi_clk_delay();
	_gpio_set_value(g_gpio_sck, 1);

	/* set GPIO pins to input mode */
	_gpio_direction_input(g_gpio_sda);
	_gpio_direction_input(g_gpio_sck);
}

static void _smi_write_bits(u32 data, u32 len)
{
	for (; len > 0; len--) {
		_smi_clk_delay();

		/* prepare data */
		_gpio_set_value(g_gpio_sda, !!(data & ( 1 << (len - 1))));
		_smi_clk_delay();

		/* clocking */
		_gpio_set_value(g_gpio_sck, 1);
		_smi_clk_delay();
		_gpio_set_value(g_gpio_sck, 0);
	}
}

static void _smi_read_bits(u32 len, u32 *data)
{
	u32 u;
	_gpio_direction_input(g_gpio_sda);

	for (*data = 0; len > 0; len--) {
		_smi_clk_delay();

		/* clocking */
		_gpio_set_value(g_gpio_sck, 1);
		_smi_clk_delay();
		u = !!_gpio_get_value(g_gpio_sda);
		_gpio_set_value(g_gpio_sck, 0);

		*data |= (u << (len - 1));
	}

	_gpio_direction_output(g_gpio_sda);
	_gpio_set_value(g_gpio_sda, 0);
}

static int _smi_wait_for_ack(void)
{
	int retry_cnt;

	retry_cnt = 0;
	do {
		u32 ack = 0;

		_smi_read_bits(1, &ack);
		if (ack == 0)
			break;

		if (++retry_cnt > SMI_ACK_RETRY_COUNT) {
			return -1;
		}
	} while (1);

	return 0;
}

static int _smi_write_byte(u8 data)
{
	_smi_write_bits(data, 8);
	return _smi_wait_for_ack();
}

static void _smi_read_byte_x(u32 byte_index, u8 *data)
{
	u32 t;

	/* read data */
	_smi_read_bits(8, &t);
	*data = (t & 0xff);

	/* send an ACK */
	_smi_write_bits(byte_index, 1);
}

/////////////////////////////////////////////////////////////////////////////////

void smi_init(u32 gpio_sda, u32 gpio_sck, u32 clk_delay_ns, u8 addr_slave)
{
	spin_lock_init(&g_smi_lock);
	
	g_gpio_sda = gpio_sda;
	g_gpio_sck = gpio_sck;
	g_clk_delay_ns = clk_delay_ns;
	g_addr_slave = addr_slave;
}

int smi_read(uint32 addr, uint32 *data)
{
	unsigned long flags;
	u8 lo = 0;
	u8 hi = 0;
	int ret = RT_ERR_FAILED;

	spin_lock_irqsave(&g_smi_lock, flags);

	_smi_start();

	/* send READ command */
	ret = _smi_write_byte(g_addr_slave | 0x01);
	if (ret)
		goto out;

	/* set ADDR[7:0] */
	ret = _smi_write_byte(addr & 0xff);
	if (ret)
		goto out;

	/* set ADDR[15:8] */
	ret = _smi_write_byte(addr >> 8);
	if (ret)
		goto out;

	/* read DATA[7:0] */
	_smi_read_byte_x(0, &lo);
	
	/* read DATA[15:8] */
	_smi_read_byte_x(1, &hi);

	*data = ((u32) lo) | (((u32) hi) << 8);

	ret = RT_ERR_OK;

 out:
	_smi_stop();
	spin_unlock_irqrestore(&g_smi_lock, flags);

	return ret;
}

int smi_write(uint32 addr, uint32 data)
{
	unsigned long flags;
	int ret = RT_ERR_FAILED;

	spin_lock_irqsave(&g_smi_lock, flags);

	_smi_start();

	/* send WRITE command */
	ret = _smi_write_byte(g_addr_slave);
	if (ret)
		goto out;

	/* set ADDR[7:0] */
	ret = _smi_write_byte(addr & 0xff);
	if (ret)
		goto out;

	/* set ADDR[15:8] */
	ret = _smi_write_byte(addr >> 8);
	if (ret)
		goto out;

	/* write DATA[7:0] */
	ret = _smi_write_byte(data & 0xff);
	if (ret)
		goto out;

	/* write DATA[15:8] */
	ret = _smi_write_byte(data >> 8);
	if (ret)
		goto out;

	ret = RT_ERR_OK;

 out:
	_smi_stop();
	spin_unlock_irqrestore(&g_smi_lock, flags);

	return ret;
}

/////////////////////////////////////////////////////////////////////////////////

int gpio_set_pin_direction(u32 pin, u32 use_direction_output)
{
	unsigned long flags;

	if (pin > RALINK_GPIO_NUMBER)
		return -EINVAL;

	spin_lock_irqsave(&g_smi_lock, flags);
	if (use_direction_output)
		_gpio_direction_output(pin);
	else
		_gpio_direction_input(pin);
	spin_unlock_irqrestore(&g_smi_lock, flags);

	return 0;
}

int gpio_set_pin_value(u32 pin, u32 value)
{
	unsigned long flags;

	if (pin > RALINK_GPIO_NUMBER)
		return -EINVAL;

	spin_lock_irqsave(&g_smi_lock, flags);
	_gpio_set_value(pin, value);
	spin_unlock_irqrestore(&g_smi_lock, flags);

	return 0;
}

int gpio_get_pin_value(u32 pin, u32 *value)
{
	if (pin > RALINK_GPIO_NUMBER)
		return -EINVAL;

	*value = _gpio_get_value(pin);

	return 0;
}

int gpio_set_mode_bit(u32 idx, u32 value)
{
	unsigned long flags;

	if (idx > 31)
		return -EINVAL;

	spin_lock_irqsave(&g_smi_lock, flags);
	_gpio_mode_set_bit(idx, value);
	spin_unlock_irqrestore(&g_smi_lock, flags);

	return 0;
}

void gpio_set_mode(u32 value)
{
	unsigned long flags;
	spin_lock_irqsave(&g_smi_lock, flags);
	_gpio_mode_set(value);
	spin_unlock_irqrestore(&g_smi_lock, flags);
}

void gpio_get_mode(u32 *value)
{
	*value = _gpio_mode_get();
}

