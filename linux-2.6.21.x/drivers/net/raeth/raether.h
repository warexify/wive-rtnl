#ifndef RA2882ETHEND_H
#define RA2882ETHEND_H

#define TX_TIMEOUT	(6*HZ)		/* netdev watchdog timeout */

#define DEFAULT_MTU	1500		/* default MTU set to device */

/* mtu and rx sizes */
#if defined (CONFIG_RAETH_JUMBOFRAME)
#define	MAX_RX_LENGTH	4096		/* limit size for rx packets 1Gb */
#else
#define	MAX_RX_LENGTH	1536		/* limit size for rx packets 100Mb */
#endif

#ifdef DSP_VIA_NONCACHEABLE
#define ESRAM_BASE	0xa0800000	/* 0x0080-0000  ~ 0x00807FFF */
#else
#define ESRAM_BASE	0x80800000	/* 0x0080-0000  ~ 0x00807FFF */
#endif

#define RX_RING_BASE	((int)(ESRAM_BASE + 0x7000))
#define TX_RING_BASE	((int)(ESRAM_BASE + 0x7800))

#define NUM_TX_RINGS 	4

#ifdef CONFIG_RAETH_MEMORY_OPTIMIZATION
#ifdef CONFIG_RAETH_ROUTER
#define NUM_RX_DESC     128
#define NUM_TX_DESC    	128
#elif defined CONFIG_RT_3052_ESW
#define NUM_RX_DESC     64
#define NUM_TX_DESC     64
#else
#define NUM_RX_DESC     128
#define NUM_TX_DESC     128
#endif
#else
#if defined(CONFIG_GE1_RGMII_FORCE_1000) || defined(CONFIG_GE2_RGMII_FORCE_1000)
/* To avoid driver tx ring full */
#define NUM_RX_DESC	512
#define NUM_TX_DESC	512
#elif (defined CONFIG_RT_3052_ESW) && !defined(CONFIG_BCM_NAT)
#define NUM_RX_DESC     128
#define NUM_TX_DESC     128
#else
#define NUM_RX_DESC	256
#define NUM_TX_DESC	256
#endif
#endif

#if defined(CONFIG_BCM_NAT)
#define	DEV_WEIGHT	128
#elif defined (CONFIG_RAETH_ROUTER) || defined (CONFIG_RT_3052_ESW)
#define	DEV_WEIGHT	32
#elif defined(CONFIG_GE1_RGMII_FORCE_1000) || defined(CONFIG_GE2_RGMII_FORCE_1000)
#define	DEV_WEIGHT	128
#else
#define	DEV_WEIGHT	128
#endif

#ifndef CONFIG_RAETH_NAPI
#if defined(CONFIG_RALINK_RT3883) || defined(CONFIG_RALINK_MT7620)
#define NUM_RX_MAX_PROCESS 2
#else
#define NUM_RX_MAX_PROCESS 16
#endif
#endif

#define DEV_NAME        "eth2"
#define DEV2_NAME       "eth3"

#define GMAC2_OFFSET    0x22
#if ! defined (CONFIG_RALINK_RT6855A)
#define GMAC0_OFFSET    0x28
#else
#define GMAC0_OFFSET    0xE000
#endif
#define GMAC1_OFFSET    0x2E

#if defined(CONFIG_RALINK_RT6855A)
#define IRQ_ENET0	22
#else
#define IRQ_ENET0	3 	/* hardware interrupt #3, defined in RT2880 Soc Design Spec Rev 0.03, pp43 */
#endif

#define FE_INT_STATUS_REG (*(volatile unsigned long *)(FE_INT_STATUS))
#define FE_INT_STATUS_CLEAN(reg) (*(volatile unsigned long *)(FE_INT_STATUS)) = reg

//#define RAETH_DEBUG
#ifdef RAETH_DEBUG
#define RAETH_PRINT(fmt, args...) printk(KERN_INFO fmt, ## args)
#else
#define RAETH_PRINT(fmt, args...) { }
#endif
u32 mii_mgr_read(u32 phy_addr, u32 phy_register, u32 *read_data);
u32 mii_mgr_write(u32 phy_addr, u32 phy_register, u32 write_data);
#endif
