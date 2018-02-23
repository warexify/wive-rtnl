/**************************************************************************
 *
 *  BRIEF MODULE DESCRIPTION
 *     init setup for Ralink RT2880 solution
 *
 *  Copyright 2007 Ralink Inc. (bruce_chang@ralinktech.com.tw)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 **************************************************************************
 * May 2007 Bruce Chang
 *
 * Initial Release
 *
 *
 *
 **************************************************************************
 */

#include <linux/init.h>
#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/serialP.h>
#include <linux/serial.h>
#include <linux/serial_core.h>
#include <asm/bootinfo.h>
#include <asm/io.h>
#include <asm/serial.h>
#include <asm/rt2880/prom.h>
#include <asm/rt2880/generic.h>
#include <asm/rt2880/surfboard.h>
#include <asm/rt2880/surfboardint.h>
#include <asm/rt2880/rt_mmap.h>

extern unsigned long surfboard_sysclk;
extern unsigned long mips_machgroup;
u32 mips_cpu_feq;

/* Environment variable */
typedef struct {
	char *name;
	char *val;
} t_env_var;

int prom_argc;
int *_prom_argv, *_prom_envp;

/* PROM version of rs_table - needed for Serial Console */
struct serial_state prom_rs_table[] = {
       SERIAL_PORT_DFNS        /* Defined in serial.h */
};

/*
 * YAMON (32-bit PROM) pass arguments and environment as 32-bit pointer.
 * This macro take care of sign extension, if running in 64-bit mode.
 */
#define prom_envp(index) ((char *)(((int *)(int)_prom_envp)[(index)]))

unsigned char *prom_getenv(char *envname)
{
	/*
	 * Return a pointer to the given environment variable.
	 * In 64-bit mode: we're using 64-bit pointers, but all pointers
	 * in the PROM structures are only 32-bit, so we need some
	 * workarounds, if we are running in 64-bit mode.
	 */

#ifdef DEBUG
	int i, index=0;
	i = strlen(envname);

	while (prom_envp(index)) {
		if(strncmp(envname, prom_envp(index), i) == 0) {
			return(prom_envp(index+1));
		}
		index += 2;
	}
#endif
	return NULL;
}

static void prom_usbinit(void)
{
#ifndef CONFIG_RALINK_RT3052
	u32 reg=0;
#endif
#if defined (CONFIG_RALINK_RT3883) || defined (CONFIG_RALINK_RT3352) || defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_RT6855) || defined (CONFIG_RALINK_MT7620)
	reg = *(volatile u32 *)KSEG1ADDR((RALINK_SYSCTL_BASE + 0x34));
	reg = reg | RALINK_UDEV_RST | RALINK_UHST_RST;
	*(volatile u32 *)KSEG1ADDR((RALINK_SYSCTL_BASE + 0x34))= reg;

	reg = *(volatile u32 *)KSEG1ADDR((RALINK_SYSCTL_BASE + 0x30));
#if defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_RT6855) || defined (CONFIG_RALINK_MT7620)
	reg = reg & ~(RALINK_UPHY0_CLK_EN);
#else
	reg = reg & ~(RALINK_UPHY0_CLK_EN | RALINK_UPHY1_CLK_EN);
#endif
	*(volatile u32 *)KSEG1ADDR((RALINK_SYSCTL_BASE + 0x30))= reg;

#elif defined (CONFIG_RALINK_RT3052)
	*(volatile u32 *)KSEG1ADDR((RALINK_USB_OTG_BASE + 0xE00)) = 0xF;	// power saving
#endif
}

static void prom_init_sysclk(void)
{
#if defined (CONFIG_RALINK_MT7621)
	int cpu_fdiv = 0;
	int cpu_ffrac = 0;
	int fbdiv = 0;
#endif

	u32 reg __maybe_unused;

#if defined (CONFIG_RT3052_FPGA) || defined (CONFIG_RT3352_FPGA) || defined (CONFIG_RT3883_FPGA) || defined (CONFIG_RT5350_FPGA)
        mips_cpu_feq = 40000000;
#elif defined (CONFIG_RT6855_FPGA) || defined (CONFIG_MT7620_FPGA) || defined (CONFIG_MT7628_FPGA)
        mips_cpu_feq = 50000000;
#elif defined (CONFIG_MT7621_FPGA)
        mips_cpu_feq = 50000000;
#else
        u8 clk_sel;

        reg = (*((volatile u32 *)(RALINK_SYSCTL_BASE + 0x10)));

#if defined (CONFIG_RT3052_ASIC)
        clk_sel = (reg>>18) & 0x01;
#elif defined (CONFIG_RT3352_ASIC)
        clk_sel = (reg>>8) & 0x01;
#elif defined (CONFIG_RT5350_ASIC)
	{
        u8 clk_sel2;
        clk_sel = (reg>>8) & 0x01;
        clk_sel2 = (reg>>10) & 0x01;
        clk_sel |= (clk_sel2 << 1 );
	}
#elif defined (CONFIG_RT3883_ASIC)
        clk_sel = (reg>>8) & 0x03;
#elif defined (CONFIG_MT7620_ASIC) || defined (CONFIG_MT7628_ASIC)
	reg = (*((volatile u32 *)(RALINK_SYSCTL_BASE + 0x58)));
	if( reg & ((0x1UL) << 24) ){
		clk_sel = 1;	/* clock from BBP PLL (480MHz ) */
	}else{
		clk_sel = 0;	/* clock from CPU PLL (600MHz) */
	}
#elif defined (CONFIG_MT7621_ASIC)
	clk_sel = 0;
	reg = (*((volatile u32 *)(RALINK_SYSCTL_BASE + 0x2C)));
	if( reg & ((0x1UL) << 30)) {
		clk_sel = 1; // CPU PLL
	} else {
		clk_sel = 0; // GPLL (500Mhz)
	}
#elif defined (CONFIG_RT6855_ASIC)
        clk_sel = 0;
#else
#error Please Choice System Type
#endif
        switch(clk_sel) {
#if defined (CONFIG_RALINK_RT3052)
#if defined (CONFIG_RALINK_RT3350)
		// MA10 is floating
	case 0:
	case 1:
		mips_cpu_feq = (320*1000*1000);
		break;
#else
	case 0:
		mips_cpu_feq = (320*1000*1000);
		break;
	case 1:
		mips_cpu_feq = (384*1000*1000);
		break;
#endif
#elif defined (CONFIG_RALINK_RT3352)
	case 0:
		mips_cpu_feq = (384*1000*1000);
		break;
	case 1:
		mips_cpu_feq = (400*1000*1000);
		break;
#elif defined (CONFIG_RALINK_RT3883)
	case 0:
		mips_cpu_feq = (250*1000*1000);
		break;
	case 1:
		mips_cpu_feq = (384*1000*1000);
		break;
	case 2:
		mips_cpu_feq = (480*1000*1000);
		break;
	case 3:
		mips_cpu_feq = (500*1000*1000);
		break;
#elif defined(CONFIG_RALINK_RT5350)
	case 0:
		mips_cpu_feq = (360*1000*1000);
		break;
	case 1:
		//reserved
		break;
	case 2:
		mips_cpu_feq = (320*1000*1000);
		break;
	case 3:
		mips_cpu_feq = (300*1000*1000);
		break;
#elif defined (CONFIG_RALINK_RT6855)
	case 0:
		mips_cpu_feq = (400*1000*100);
		break;
#elif defined (CONFIG_RALINK_MT7620) || defined (CONFIG_RALINK_MT7628)
	case 0:
		reg = (*((volatile u32 *)(RALINK_SYSCTL_BASE + 0x54)));
		if(!(reg & CPLL_SW_CONFIG)){
			mips_cpu_feq = (600*1000*1000);
		}else{
			/* read CPLL_CFG0 to determine real CPU clock */
			int mult_ratio = (reg & CPLL_MULT_RATIO) >> CPLL_MULT_RATIO_SHIFT;
			int div_ratio = (reg & CPLL_DIV_RATIO) >> CPLL_DIV_RATIO_SHIFT;
			mult_ratio += 24;	/* begin from 24 */
			if(div_ratio == 0)	/* define from datasheet */
				div_ratio = 2;
			else if(div_ratio == 1)
				div_ratio = 3;
			else if(div_ratio == 2)
				div_ratio = 4;
			else if(div_ratio == 3)
				div_ratio = 8;
			mips_cpu_feq = ((BASE_CLOCK * mult_ratio ) / div_ratio) * 1000 * 1000;
		}

		break;
	case 1:
		mips_cpu_feq = (480*1000*1000);
		break;
#elif defined (CONFIG_RALINK_MT7621)
        case 0:
		reg = (*(volatile u32 *)(RALINK_SYSCTL_BASE + 0x44));
		cpu_fdiv = ((reg >> 8) & 0x1F);
		cpu_ffrac = (reg & 0x1F);
                mips_cpu_feq = (500 * cpu_ffrac / cpu_fdiv) * 1000 * 1000;
                break;
        case 1: //CPU PLL
		reg = (*(volatile u32 *)(RALINK_MEMCTRL_BASE + 0x648));
		fbdiv = ((reg >> 4) & 0x7F) + 1;
		reg = (*(volatile u32 *)(RALINK_SYSCTL_BASE + 0x10)); 
		reg = (reg >> 6) & 0x7;
		if(reg >= 6) { //25Mhz Xtal
			mips_cpu_feq = 25 * fbdiv * 1000 * 1000;
		} else if(reg >=3) { //40Mhz Xtal
			mips_cpu_feq = 20 * fbdiv * 1000 * 1000;
		} else { // 20Mhz Xtal
			/* TODO */
		}
		break;
#else
#error Please Choice Chip Type
#endif
	}
#endif
#if defined (CONFIG_RT3883_ASIC)
	if ((reg>>17) & 0x1) { //DDR2
		switch (clk_sel) {
		case 0:
			surfboard_sysclk = (125*1000*1000);
			break;
		case 1:
			surfboard_sysclk = (128*1000*1000);
			break;
		case 2:
			surfboard_sysclk = (160*1000*1000);
			break;
		case 3:
			surfboard_sysclk = (166*1000*1000);
			break;
		}
	}
	else { //SDR
		switch (clk_sel) {
		case 0:
			surfboard_sysclk = (83*1000*1000);
			break;
		case 1:
			surfboard_sysclk = (96*1000*1000);
			break;
		case 2:
			surfboard_sysclk = (120*1000*1000);
			break;
		case 3:
			surfboard_sysclk = (125*1000*1000);
			break;
		}
	}

#elif defined(CONFIG_RT5350_ASIC)
	switch (clk_sel) {
	case 0:
		surfboard_sysclk = (120*1000*1000);
		break;
	case 1:
		//reserved
		break;
	case 2:
		surfboard_sysclk = (80*1000*1000);
		break;
	case 3:
		surfboard_sysclk = (100*1000*1000);
		break;
	}

#elif defined (CONFIG_RALINK_RT6855)
	surfboard_sysclk = mips_cpu_feq/4;
#elif defined (CONFIG_RALINK_MT7620) || defined (CONFIG_RALINK_MT7628)
	/* FIXME , SDR -> /4,   DDR -> /3, but currently "surfboard_sysclk" */
	surfboard_sysclk = mips_cpu_feq/4;
#elif defined (CONFIG_RALINK_MT7621)
	surfboard_sysclk = mips_cpu_feq/4;
#else
	surfboard_sysclk = mips_cpu_feq/3;
#endif
	printk("\n The CPU feqenuce set to %u MHz\n",mips_cpu_feq / 1000 / 1000);
}

/*
** This function sets up the local prom_rs_table used only for the fake console
** console (mainly printk for debug display and no input processing)
** and also sets up the global rs_table used for the actual serial console.
** To get the correct baud_base value, prom_init_sysclk() must be called before
** this function is called.
*/
static struct uart_port serial_req[2];
static int prom_init_serial_port(void)
{

  /*
   * baud rate = system clock freq / (CLKDIV * 16)
   * CLKDIV=system clock freq/16/baud rate
   */
  memset(serial_req, 0, 2*sizeof(struct uart_port));

  serial_req[0].type       = PORT_16550A;
  serial_req[0].line       = 0;
  serial_req[0].irq        = SURFBOARDINT_UART;
  serial_req[0].flags      = STD_COM_FLAGS;
#if defined (CONFIG_RALINK_RT3883) || defined (CONFIG_RALINK_RT3352) || \
    defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_RT6855)
  serial_req[0].uartclk    = 40000000;
#else
  serial_req[0].uartclk    = surfboard_sysclk;
#endif
  serial_req[0].iotype     = UPIO_AU;
  serial_req[0].membase	   = (char *)KSEG1ADDR(RALINK_UART_BASE);
  serial_req[0].regshift   = 2;
  serial_req[0].mapbase    = KSEG1ADDR(RALINK_UART_BASE);

  serial_req[1].type       = PORT_16550A;
  serial_req[1].line       = 1;
  serial_req[1].irq        = SURFBOARDINT_UART1;
  serial_req[1].flags      = STD_COM_FLAGS;
#if defined (CONFIG_RALINK_RT3883) || defined (CONFIG_RALINK_RT3352) || \
    defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_RT6855)
  serial_req[1].uartclk    = 40000000;
#else
  serial_req[1].uartclk    = surfboard_sysclk;
#endif
  serial_req[1].iotype     = UPIO_AU;
  serial_req[1].membase	   = (char *)KSEG1ADDR(RALINK_UART_LITE_BASE);
  serial_req[1].regshift   = 2;
  serial_req[1].mapbase    = KSEG1ADDR(RALINK_UART_LITE_BASE);
#ifdef CONFIG_SERIAL_CORE
    /* Switch UART LITE/UART BASE mode must after prom_meminit	*/
  early_serial_setup(&serial_req[0]);
  early_serial_setup(&serial_req[1]);
#endif

  return 0;

}

static void serial_setbrg(unsigned long wBaud)
{
	//fix at 57600 8 n 1 n
 	*(volatile u32 *)(RALINK_SYSCTL_BASE + 0xC08)= 0;
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0xC10)= 0;
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0xC14)= 0x3;
#if defined (CONFIG_RALINK_RT3883) || defined (CONFIG_RALINK_RT3352) ||  defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_RT6855) || defined (CONFIG_RALINK_MT7620)
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0xC28)= (40000000 / SURFBOARD_BAUD_DIV / 57600);
#else
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0xC28)= (surfboard_sysclk / SURFBOARD_BAUD_DIV / 57600);
#endif
	//fix at 57600 8 n 1 n
 	*(volatile u32 *)(RALINK_SYSCTL_BASE + 0x508)= 0;
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0x510)= 0;
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0x514)= 0x3;
#if defined (CONFIG_RALINK_RT3883) || defined (CONFIG_RALINK_RT3352) || defined (CONFIG_RALINK_RT5350) || defined (CONFIG_RALINK_RT6855) || defined (CONFIG_RALINK_MT7620)
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0x528)= (40000000 / SURFBOARD_BAUD_DIV / 57600);
#else
        *(volatile u32 *)(RALINK_SYSCTL_BASE + 0x528)= (surfboard_sysclk / SURFBOARD_BAUD_DIV / 57600);
#endif
}

int serial_init(unsigned long wBaud)
{
        serial_setbrg(wBaud);

        return (0);
}

__init void prom_init(void)
{
	mips_machgroup = MACH_GROUP_RT2880;
	mips_machtype = MACH_RALINK_ROUTER;

#ifdef CONFIG_UBOOT_CMDLINE
	prom_argc = fw_arg0;
	_prom_argv = (int *)fw_arg1;
	_prom_envp = (int *)fw_arg2;
#endif
	prom_init_cmdline();
	prom_init_sysclk();

	set_io_port_base(KSEG1);
	write_c0_wired(0);

	serial_init(57600);			/* Kernel driver serial init */
	prom_init_serial_port();		/* Set rate. Needed for Serial Console */
	prom_meminit();				/* Autodetect RAM size and set need variables */
	prom_usbinit();				/* USB power saving*/

#if defined(CONFIG_RT3352_FPGA)  ||  defined(CONFIG_RT3883_FPGA) || defined(CONFIG_RT5350_FPGA) || defined (CONFIG_MT7620_FPGA)
	printk("FPGA mode LINUX started...\n");
#elif defined(CONFIG_RT3352_ASIC) || defined (CONFIG_RT3883_ASIC) || defined (CONFIG_RT5350_ASIC) || defined (CONFIG_MT7620_ASIC)
	printk("ASIC mode LINUX started...\n");
#else
	printk("LINUX started...\n");
#endif
	printk("The CPU/SYS frequency set to %d/%lu MHz\n", mips_cpu_feq / 1000 / 1000, surfboard_sysclk / 1000 / 1000);
}
