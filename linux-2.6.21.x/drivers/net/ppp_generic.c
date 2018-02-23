/*
 * Generic PPP layer for Linux.
 *
 * Copyright 1999-2002 Paul Mackerras.
 *
 *  This program is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU General Public License
 *  as published by the Free Software Foundation; either version
 *  2 of the License, or (at your option) any later version.
 *
 * The generic PPP layer handles the PPP network interfaces, the
 * /dev/ppp device, packet and VJ compression, and multilink.
 * It talks to PPP `channels' via the interface defined in
 * include/linux/ppp_channel.h.  Channels provide the basic means for
 * sending and receiving PPP frames on some kind of communications
 * channel.
 *
 * Part of the code in this driver was inspired by the old async-only
 * PPP driver, written by Michael Callahan and Al Longyear, and
 * subsequently hacked by Paul Mackerras.
 *
 * ==FILEVERSION 20041108==
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/kmod.h>
#include <linux/mm.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/poll.h>
#include <linux/ppp_defs.h>
#include <linux/filter.h>
#include <linux/if_ppp.h>
#include <linux/ppp_channel.h>
#include <linux/ppp-comp.h>
#include <linux/skbuff.h>
#include <linux/rtnetlink.h>
#include <linux/if_arp.h>
#include <linux/ip.h>
#include <linux/spinlock.h>
#include <linux/rwsem.h>
#include <linux/stddef.h>
#include <linux/device.h>
#include <linux/mutex.h>
#ifdef CONFIG_SLHC
#include <net/slhc_vj.h>
#endif
#include <asm/unaligned.h>
#include <asm/atomic.h>

#ifdef CONFIG_PPP_PREVENT_DROP_SESSION_ON_FULL_CPU_LOAD
#include <linux/kernel_stat.h>
/* limit cpu load */
extern int ppp_cpu_load;
static unsigned long load;
static unsigned long cpu_prev_jiffies;
#ifdef CONFIG_RALINK_WATCHDOG
extern void RaWdgReload(void);
extern int WdgLoadValue;
#endif
#endif

#ifdef CONFIG_RALINK_GPIO_LED_VPN
#include <linux/ralink_gpio.h>
ralink_gpio_led_info ppp_led;
extern int ralink_gpio_led_set(ralink_gpio_led_info ppp_led);
static unsigned long led_prev_jiffies;
#endif

#if defined (CONFIG_RA_HW_NAT) || defined (CONFIG_RA_HW_NAT_MODULE)
#include "../../net/nat/hw_nat/ra_nat.h"
extern int (*ra_sw_nat_hook_rx)(struct sk_buff *skb);
#endif

#define PPP_VERSION	"2.4.2"

/*
 * Network protocols we support.
 */
#define NP_IP	0		/* Internet Protocol V4 */
#define NP_IPV6	1		/* Internet Protocol V6 */
#ifndef CONFIG_PPP_FASTPATH
#define NP_IPX	2		/* IPX protocol */
#define NP_AT	3		/* Appletalk protocol */
#define NP_MPLS_UC 4		/* MPLS unicast */
#define NP_MPLS_MC 5		/* MPLS multicast */
#endif

#ifndef CONFIG_PPP_FASTPATH
#define NUM_NP	6		/* Number of NPs. */
#else
#define NUM_NP	2		/* Number of NPs. */
#endif

#ifdef CONFIG_PPP_MULTILINK
#define MPHDRLEN	6	/* multilink protocol header length */
#define MPHDRLEN_SSN	4	/* ditto with short sequence numbers */
#endif

/*
 * An instance of /dev/ppp can be associated with either a ppp
 * interface unit or a ppp channel.  In both cases, file->private_data
 * points to one of these.
 */
struct ppp_file {
	enum {
		INTERFACE=1, CHANNEL
	}		kind;
	struct sk_buff_head xq;		/* pppd transmit queue */
	struct sk_buff_head rq;		/* receive queue for pppd */
	wait_queue_head_t rwait;	/* for poll on reading /dev/ppp */
	atomic_t	refcnt;		/* # refs (incl /dev/ppp attached) */
	int		hdrlen;		/* space to leave for headers */
	int		index;		/* interface unit / channel number */
	int		dead;		/* unit/channel has been shut down */
};

#define PF_TO_X(pf, X)		container_of(pf, X, file)

#define PF_TO_PPP(pf)		PF_TO_X(pf, struct ppp)
#define PF_TO_CHANNEL(pf)	PF_TO_X(pf, struct channel)

/*
 * Data structure describing one ppp unit.
 * A ppp unit corresponds to a ppp network interface device
 * and represents a multilink bundle.
 * It can have 0 or more ppp channels connected to it.
 */
struct ppp {
	struct ppp_file	file;		/* stuff for read/write/poll 0 */
	struct file	*owner;		/* file that owns this unit 48 */
	struct list_head channels;	/* list of attached channels 4c */
	int		n_channels;	/* how many channels are attached 54 */
	spinlock_t	rlock;		/* lock for receive side 58 */
	spinlock_t	wlock;		/* lock for transmit side 5c */
	int		mru;		/* max receive unit 60 */
	unsigned int	flags;		/* control bits 64 */
	unsigned int	xstate;		/* transmit state bits 68 */
	unsigned int	rstate;		/* receive state bits 6c */
	int		debug;		/* debug flags 70 */
#ifdef CONFIG_SLHC
	struct slcompress *vj;		/* state for VJ header compression */
#endif
	enum NPmode	npmode[NUM_NP];	/* what to do with each net proto 78 */
	struct sk_buff	*xmit_pending;	/* a packet ready to go out 88 */
	struct compressor *xcomp;	/* transmit packet compressor 8c */
	void		*xc_state;	/* its internal state 90 */
	struct compressor *rcomp;	/* receive decompressor 94 */
	void		*rc_state;	/* its internal state 98 */
	unsigned long	last_xmit;	/* jiffies when last pkt sent 9c */
	unsigned long	last_recv;	/* jiffies when last pkt rcvd a0 */
	struct net_device *dev;		/* network interface device a4 */
	int		closing;	/* is device closing down? a8 */
#ifdef CONFIG_PPP_MULTILINK
	int		nxchan;		/* next channel to send something on */
	u32		nxseq;		/* next sequence number to send */
	int		mrru;		/* MP: max reconst. receive unit */
	u32		nextseq;	/* MP: seq no of next packet */
	u32		minseq;		/* MP: min of most recent seqnos */
	struct sk_buff_head mrq;	/* MP: receive reconstruction queue */
#endif /* CONFIG_PPP_MULTILINK */
	struct net_device_stats stats;	/* statistics */
#ifdef CONFIG_PPP_FILTER
	struct sock_filter *pass_filter;	/* filter for packets to pass */
	struct sock_filter *active_filter;/* filter for pkts to reset idle */
	unsigned pass_len, active_len;
#endif /* CONFIG_PPP_FILTER */
};

/*
 * Bits in flags: SC_NO_TCP_CCID, SC_CCP_OPEN, SC_CCP_UP, SC_LOOP_TRAFFIC,
 * SC_MULTILINK, SC_MP_SHORTSEQ, SC_MP_XSHORTSEQ, SC_COMP_TCP, SC_REJ_COMP_TCP,
 * SC_MUST_COMP
 * Bits in rstate: SC_DECOMP_RUN, SC_DC_ERROR, SC_DC_FERROR.
 * Bits in xstate: SC_COMP_RUN
 */
#ifdef CONFIG_PPP_MULTILINK
#define SC_FLAG_BITS	(SC_NO_TCP_CCID|SC_CCP_OPEN|SC_CCP_UP|SC_LOOP_TRAFFIC \
			 |SC_MULTILINK|SC_MP_SHORTSEQ|SC_MP_XSHORTSEQ \
			 |SC_COMP_TCP|SC_REJ_COMP_TCP|SC_MUST_COMP)
#else
#define SC_FLAG_BITS	(SC_NO_TCP_CCID|SC_CCP_OPEN|SC_CCP_UP|SC_LOOP_TRAFFIC \
			 |SC_MP_SHORTSEQ|SC_MP_XSHORTSEQ \
			 |SC_COMP_TCP|SC_REJ_COMP_TCP|SC_MUST_COMP)
#endif

/*
 * Private data structure for each channel.
 * This includes the data structure used for multilink.
 */
struct channel {
	struct ppp_file	file;		/* stuff for read/write/poll */
	struct list_head list;		/* link in all/new_channels list */
	struct ppp_channel *chan;	/* public channel data structure */
	struct rw_semaphore chan_sem;	/* protects `chan' during chan ioctl */
	spinlock_t	downl;		/* protects `chan', file.xq dequeue */
	struct ppp	*ppp;		/* ppp unit we're connected to */
	struct list_head clist;		/* link in list of channels per unit */
	rwlock_t	upl;		/* protects `ppp' */
#ifdef CONFIG_PPP_MULTILINK
	u8		avail;		/* flag used in multilink stuff */
	u8		had_frag;	/* >= 1 fragments have been sent */
	u32		lastseq;	/* MP: last sequence # received */
#endif /* CONFIG_PPP_MULTILINK */
};

/*
 * SMP locking issues:
 * Both the ppp.rlock and ppp.wlock locks protect the ppp.channels
 * list and the ppp.n_channels field, you need to take both locks
 * before you modify them.
 * The lock ordering is: channel.upl -> ppp.wlock -> ppp.rlock ->
 * channel.downl.
 */

/*
 * A cardmap represents a mapping from unsigned integers to pointers,
 * and provides a fast "find lowest unused number" operation.
 * It uses a broad (32-way) tree with a bitmap at each level.
 * It is designed to be space-efficient for small numbers of entries
 * and time-efficient for large numbers of entries.
 */
#define CARDMAP_ORDER	5
#define CARDMAP_WIDTH	(1U << CARDMAP_ORDER)
#define CARDMAP_MASK	(CARDMAP_WIDTH - 1)

struct cardmap {
	int shift;
	unsigned long inuse;
	struct cardmap *parent;
	void *ptr[CARDMAP_WIDTH];
};
static void *cardmap_get(struct cardmap *map, unsigned int nr);
static int cardmap_set(struct cardmap **map, unsigned int nr, void *ptr);
static unsigned int cardmap_find_first_free(struct cardmap *map);
static void cardmap_destroy(struct cardmap **map);

/*
 * all_ppp_mutex protects the all_ppp_units mapping.
 * It also ensures that finding a ppp unit in the all_ppp_units map
 * and updating its file.refcnt field is atomic.
 */
static DEFINE_MUTEX(all_ppp_mutex);
static struct cardmap *all_ppp_units;
static atomic_t ppp_unit_count = ATOMIC_INIT(0);

/*
 * all_channels_lock protects all_channels and last_channel_index,
 * and the atomicity of find a channel and updating its file.refcnt
 * field.
 */
static DEFINE_SPINLOCK(all_channels_lock);
static LIST_HEAD(all_channels);
static LIST_HEAD(new_channels);
static int last_channel_index;
static atomic_t channel_count = ATOMIC_INIT(0);

/* Get the PPP protocol number from a skb */
#define PPP_PROTO(skb) get_unaligned_be16((skb)->data)

/* We limit the length of ppp->file.rq to this (arbitrary) value */
#define PPP_MAX_RQLEN	32

#ifdef CONFIG_PPP_MULTILINK
/*
 * Maximum number of multilink fragments queued up.
 * This has to be large enough to cope with the maximum latency of
 * the slowest channel relative to the others.  Strictly it should
 * depend on the number of channels and their characteristics.
 */
#define PPP_MP_MAX_QLEN	128

/* Multilink header bits. */
#define B	0x80		/* this fragment begins a packet */
#define E	0x40		/* this fragment ends a packet */

/* Compare multilink sequence numbers (assumed to be 32 bits wide) */
#define seq_before(a, b)	((s32)((a) - (b)) < 0)
#define seq_after(a, b)		((s32)((a) - (b)) > 0)
#endif

/* Prototypes. */
static int ppp_unattached_ioctl(struct ppp_file *pf, struct file *file,
				unsigned int cmd, unsigned long arg);
static void ppp_xmit_process(struct ppp *ppp);
static void ppp_send_frame(struct ppp *ppp, struct sk_buff *skb);
static void ppp_push(struct ppp *ppp);
static void ppp_channel_push(struct channel *pch);
static void ppp_receive_frame(struct ppp *ppp, struct sk_buff *skb,
			      struct channel *pch);
static void ppp_receive_error(struct ppp *ppp);
static void ppp_receive_nonmp_frame(struct ppp *ppp, struct sk_buff *skb);
static struct sk_buff *ppp_decompress_frame(struct ppp *ppp,
					    struct sk_buff *skb);
#ifdef CONFIG_PPP_MULTILINK
static void ppp_receive_mp_frame(struct ppp *ppp, struct sk_buff *skb,
				struct channel *pch);
static void ppp_mp_insert(struct ppp *ppp, struct sk_buff *skb);
static struct sk_buff *ppp_mp_reconstruct(struct ppp *ppp);
static int ppp_mp_explode(struct ppp *ppp, struct sk_buff *skb);
#endif /* CONFIG_PPP_MULTILINK */
static int ppp_set_compress(struct ppp *ppp, unsigned long arg);
static void ppp_ccp_peek(struct ppp *ppp, struct sk_buff *skb, int inbound);
static void ppp_ccp_closed(struct ppp *ppp);
static struct compressor *find_compressor(int type);
static void ppp_get_stats(struct ppp *ppp, struct ppp_stats *st);
static struct ppp *ppp_create_interface(int unit, int *retp);
static void init_ppp_file(struct ppp_file *pf, int kind);
static void ppp_shutdown_interface(struct ppp *ppp);
static void ppp_destroy_interface(struct ppp *ppp);
static struct ppp *ppp_find_unit(int unit);
static struct channel *ppp_find_channel(int unit);
static int ppp_connect_channel(struct channel *pch, int unit);
static int ppp_disconnect_channel(struct channel *pch);
static void ppp_destroy_channel(struct channel *pch);

static struct class *ppp_class;

/* Translates a PPP protocol number to a NP index (NP == network protocol) */
static inline int proto_to_npindex(int proto)
{
	switch (proto) {
	case PPP_IP:
		return NP_IP;
	case PPP_IPV6:
		return NP_IPV6;
#ifndef CONFIG_PPP_FASTPATH
	case PPP_IPX:
		return NP_IPX;
	case PPP_AT:
		return NP_AT;
	case PPP_MPLS_UC:
		return NP_MPLS_UC;
	case PPP_MPLS_MC:
		return NP_MPLS_MC;
#endif
	}
	return -EINVAL;
}

/* Translates an NP index into a PPP protocol number */
static const int npindex_to_proto[NUM_NP] = {
	PPP_IP,
	PPP_IPV6,
#ifndef CONFIG_PPP_FASTPATH
	PPP_IPX,
	PPP_AT,
	PPP_MPLS_UC,
	PPP_MPLS_MC,
#endif
};

/* Translates an ethertype into an NP index */
static inline int ethertype_to_npindex(int ethertype)
{
	switch (ethertype) {
	case ETH_P_IP:
		return NP_IP;
	case ETH_P_IPV6:
		return NP_IPV6;
#ifndef CONFIG_PPP_FASTPATH
	case ETH_P_IPX:
		return NP_IPX;
	case ETH_P_PPPTALK:
	case ETH_P_ATALK:
		return NP_AT;
	case ETH_P_MPLS_UC:
		return NP_MPLS_UC;
	case ETH_P_MPLS_MC:
		return NP_MPLS_MC;
#endif
	}
	return -1;
}

/* Translates an NP index into an ethertype */
static const int npindex_to_ethertype[NUM_NP] = {
	ETH_P_IP,
	ETH_P_IPV6,
#ifndef CONFIG_PPP_FASTPATH
	ETH_P_IPX,
	ETH_P_PPPTALK,
	ETH_P_MPLS_UC,
	ETH_P_MPLS_MC,
#endif
};

/*
 * Locking shorthand.
 */
#define ppp_xmit_lock(ppp)	spin_lock_bh(&(ppp)->wlock)
#define ppp_xmit_unlock(ppp)	spin_unlock_bh(&(ppp)->wlock)
#define ppp_recv_lock(ppp)	spin_lock_bh(&(ppp)->rlock)
#define ppp_recv_unlock(ppp)	spin_unlock_bh(&(ppp)->rlock)
#define ppp_lock(ppp)		do { ppp_xmit_lock(ppp); \
				     ppp_recv_lock(ppp); } while (0)
#define ppp_unlock(ppp)		do { ppp_recv_unlock(ppp); \
				     ppp_xmit_unlock(ppp); } while (0)

/*
 * /dev/ppp device routines.
 * The /dev/ppp device is used by pppd to control the ppp unit.
 * It supports the read, write, ioctl and poll functions.
 * Open instances of /dev/ppp can be in one of three states:
 * unattached, attached to a ppp unit, or attached to a ppp channel.
 */
static int ppp_open(struct inode *inode, struct file *file)
{
	/*
	 * This could (should?) be enforced by the permissions on /dev/ppp.
	 */
	if (!capable(CAP_NET_ADMIN))
		return -EPERM;
	return 0;
}

static int ppp_release(struct inode *inode, struct file *file)
{
	struct ppp_file *pf = file->private_data;
	struct ppp *ppp;

	if (pf) {
		file->private_data = NULL;
		if (pf->kind == INTERFACE) {
			ppp = PF_TO_PPP(pf);
			if (file == ppp->owner)
				ppp_shutdown_interface(ppp);
		}
		if (atomic_dec_and_test(&pf->refcnt)) {
			switch (pf->kind) {
			case INTERFACE:
				ppp_destroy_interface(PF_TO_PPP(pf));
				break;
			case CHANNEL:
				ppp_destroy_channel(PF_TO_CHANNEL(pf));
				break;
			}
		}
	}
	return 0;
}

static ssize_t ppp_read(struct file *file, char __user *buf,
			size_t count, loff_t *ppos)
{
	struct ppp_file *pf = file->private_data;
	DECLARE_WAITQUEUE(wait, current);
	ssize_t ret;
	struct sk_buff *skb = NULL;
	struct iovec iov;

	ret = count;

	if (!pf)
		return -ENXIO;
	add_wait_queue(&pf->rwait, &wait);
	for (;;) {
		set_current_state(TASK_INTERRUPTIBLE);
		skb = skb_dequeue(&pf->rq);
		if (skb)
			break;
		ret = 0;
		if (pf->dead)
			break;
		if (pf->kind == INTERFACE) {
			/*
			 * Return 0 (EOF) on an interface that has no
			 * channels connected, unless it is looping
			 * network traffic (demand mode).
			 */
			struct ppp *ppp = PF_TO_PPP(pf);
			if (ppp->n_channels == 0 &&
			    (ppp->flags & SC_LOOP_TRAFFIC) == 0)
				break;
		}
		ret = -EAGAIN;
		if (file->f_flags & O_NONBLOCK)
			break;
		ret = -ERESTARTSYS;
		if (signal_pending(current))
			break;
		schedule();
	}
	set_current_state(TASK_RUNNING);
	remove_wait_queue(&pf->rwait, &wait);

	if (!skb)
		goto out;

	ret = -EOVERFLOW;
	if (skb->len > count)
		goto outf;
	ret = -EFAULT;
	iov.iov_base = buf;
	iov.iov_len = count;
	if (skb_copy_datagram_iovec(skb, 0, &iov, skb->len))
		goto outf;
	ret = skb->len;

 outf:
	kfree_skb(skb);
 out:
	return ret;
}

static ssize_t ppp_write(struct file *file, const char __user *buf,
			 size_t count, loff_t *ppos)
{
	struct ppp_file *pf = file->private_data;
	struct sk_buff *skb;
	ssize_t ret;

	if (!pf)
		return -ENXIO;
	ret = -ENOMEM;
	skb = alloc_skb(count + pf->hdrlen, GFP_KERNEL);
	if (!skb)
		goto out;
	skb_reserve(skb, pf->hdrlen);
	ret = -EFAULT;
	if (copy_from_user(skb_put(skb, count), buf, count)) {
		kfree_skb(skb);
		goto out;
	}

	skb_queue_tail(&pf->xq, skb);

	switch (pf->kind) {
	case INTERFACE:
		ppp_xmit_process(PF_TO_PPP(pf));
		break;
	case CHANNEL:
		ppp_channel_push(PF_TO_CHANNEL(pf));
		break;
	}

	ret = count;

 out:
	return ret;
}

/* No kernel lock - fine */
static unsigned int ppp_poll(struct file *file, poll_table *wait)
{
	struct ppp_file *pf = file->private_data;
	unsigned int mask;

	if (!pf)
		return 0;
	poll_wait(file, &pf->rwait, wait);
	mask = POLLOUT | POLLWRNORM;
	if (skb_peek(&pf->rq))
		mask |= POLLIN | POLLRDNORM;
	if (pf->dead)
		mask |= POLLHUP;
	else if (pf->kind == INTERFACE) {
		/* see comment in ppp_read */
		struct ppp *ppp = PF_TO_PPP(pf);
		if (ppp->n_channels == 0 &&
		    (ppp->flags & SC_LOOP_TRAFFIC) == 0)
			mask |= POLLIN | POLLRDNORM;
	}

	return mask;
}

#ifdef CONFIG_PPP_FILTER
static int get_filter(void __user *arg, struct sock_filter **p)
{
	struct sock_fprog uprog;
	struct sock_filter *code = NULL;
	int len, err;

	if (copy_from_user(&uprog, arg, sizeof(uprog)))
		return -EFAULT;

	if (!uprog.len) {
		*p = NULL;
		return 0;
	}

	len = uprog.len * sizeof(struct sock_filter);
	code = memdup_user(uprog.filter, len);
	if (IS_ERR(code))
		return PTR_ERR(code);

	err = sk_chk_filter(code, uprog.len);
	if (err) {
		kfree(code);
		return err;
	}

	*p = code;
	return uprog.len;
}
#endif /* CONFIG_PPP_FILTER */

static long ppp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct ppp_file *pf = file->private_data;
	struct ppp *ppp;
	int err = -EFAULT, val, i;
#ifdef CONFIG_SLHC
	int val2;
#endif
	struct ppp_idle idle;
	struct npioctl npi;
	int unit, cflags;
#ifdef CONFIG_SLHC
	struct slcompress *vj;
#endif
	void __user *argp = (void __user *)arg;
	int __user *p = argp;

	if (!pf)
		return ppp_unattached_ioctl(pf, file, cmd, arg);

	if (cmd == PPPIOCDETACH) {
		/*
		 * We have to be careful here... if the file descriptor
		 * has been dup'd, we could have another process in the
		 * middle of a poll using the same file *, so we had
		 * better not free the interface data structures -
		 * instead we fail the ioctl.  Even in this case, we
		 * shut down the interface if we are the owner of it.
		 * Actually, we should get rid of PPPIOCDETACH, userland
		 * (i.e. pppd) could achieve the same effect by closing
		 * this fd and reopening /dev/ppp.
		 */
		err = -EINVAL;
		if (pf->kind == INTERFACE) {
			ppp = PF_TO_PPP(pf);
			if (file == ppp->owner)
				ppp_shutdown_interface(ppp);
		}
		if (atomic_long_read(&file->f_count) < 2) {
			ppp_release(NULL, file);
			err = 0;
		} else
			printk(KERN_DEBUG "PPPIOCDETACH file->f_count=%ld\n",
			       atomic_long_read(&file->f_count));
		return err;
	}

	if (pf->kind == CHANNEL) {
		struct channel *pch;
		struct ppp_channel *chan;

		pch = PF_TO_CHANNEL(pf);

		switch (cmd) {
		case PPPIOCCONNECT:
			if (get_user(unit, p))
				break;
			err = ppp_connect_channel(pch, unit);
			break;

		case PPPIOCDISCONN:
			err = ppp_disconnect_channel(pch);
			break;

		default:
			down_read(&pch->chan_sem);
			chan = pch->chan;
			err = -ENOTTY;
			if (chan && chan->ops->ioctl)
				err = chan->ops->ioctl(chan, cmd, arg);
			up_read(&pch->chan_sem);
		}
		return err;
	}

	if (pf->kind != INTERFACE) {
		/* can't happen */
		printk(KERN_ERR "PPP: not interface or channel??\n");
		return -EINVAL;
	}

	ppp = PF_TO_PPP(pf);
	switch (cmd) {
	case PPPIOCSMRU:
		if (get_user(val, p))
			break;
		ppp->mru = val;
		err = 0;
		break;

	case PPPIOCSFLAGS:
		if (get_user(val, p))
			break;
		ppp_lock(ppp);
		cflags = ppp->flags & ~val;
		ppp->flags = val & SC_FLAG_BITS;
		ppp_unlock(ppp);
		if (cflags & SC_CCP_OPEN)
			ppp_ccp_closed(ppp);
		err = 0;
		break;

	case PPPIOCGFLAGS:
		val = ppp->flags | ppp->xstate | ppp->rstate;
		if (put_user(val, p))
			break;
		err = 0;
		break;

	case PPPIOCSCOMPRESS:
		err = ppp_set_compress(ppp, arg);
		break;

	case PPPIOCGUNIT:
		if (put_user(ppp->file.index, p))
			break;
		err = 0;
		break;

	case PPPIOCSDEBUG:
		if (get_user(val, p))
			break;
		ppp->debug = val;
		err = 0;
		break;

	case PPPIOCGDEBUG:
		if (put_user(ppp->debug, p))
			break;
		err = 0;
		break;

	case PPPIOCGIDLE:
		idle.xmit_idle = (jiffies - ppp->last_xmit) / HZ;
		idle.recv_idle = (jiffies - ppp->last_recv) / HZ;
		if (copy_to_user(argp, &idle, sizeof(idle)))
			break;
		err = 0;
		break;

	case PPPIOCSMAXCID:
#ifdef CONFIG_SLHC
		if (get_user(val, p))
			break;
		val2 = 15;
		if ((val >> 16) != 0) {
			val2 = val >> 16;
			val &= 0xffff;
		}
		vj = slhc_init(val2+1, val+1);
		if (!vj) {
			printk(KERN_ERR "PPP: no memory (VJ compressor)\n");
			err = -ENOMEM;
			break;
		}
		ppp_lock(ppp);
		if (ppp->vj)
			slhc_free(ppp->vj);
		ppp->vj = vj;
		ppp_unlock(ppp);
#endif
		err = 0;
		break;

	case PPPIOCGNPMODE:
	case PPPIOCSNPMODE:
		if (copy_from_user(&npi, argp, sizeof(npi)))
			break;
		err = proto_to_npindex(npi.protocol);
		if (err < 0)
			break;
		i = err;
		if (cmd == PPPIOCGNPMODE) {
			err = -EFAULT;
			npi.mode = ppp->npmode[i];
			if (copy_to_user(argp, &npi, sizeof(npi)))
				break;
		} else {
			ppp->npmode[i] = npi.mode;
			/* we may be able to transmit more packets now (??) */
			netif_wake_queue(ppp->dev);
		}
		err = 0;
		break;

#ifdef CONFIG_PPP_FILTER
	case PPPIOCSPASS:
	{
		struct sock_filter *code;
		err = get_filter(argp, &code);
		if (err >= 0) {
			ppp_lock(ppp);
			kfree(ppp->pass_filter);
			ppp->pass_filter = code;
			ppp->pass_len = err;
			ppp_unlock(ppp);
			err = 0;
		}
		break;
	}
	case PPPIOCSACTIVE:
	{
		struct sock_filter *code;
		err = get_filter(argp, &code);
		if (err >= 0) {
			ppp_lock(ppp);
			kfree(ppp->active_filter);
			ppp->active_filter = code;
			ppp->active_len = err;
			ppp_unlock(ppp);
			err = 0;
		}
		break;
	}
#endif /* CONFIG_PPP_FILTER */

#ifdef CONFIG_PPP_MULTILINK
	case PPPIOCSMRRU:
		if (get_user(val, p))
			break;
		ppp_recv_lock(ppp);
		ppp->mrru = val;
		ppp_recv_unlock(ppp);
		err = 0;
		break;
#endif /* CONFIG_PPP_MULTILINK */

	default:
		err = -ENOTTY;
	}

	return err;
}

static int ppp_unattached_ioctl(struct ppp_file *pf, struct file *file,
				unsigned int cmd, unsigned long arg)
{
	int unit, err = -EFAULT;
	struct ppp *ppp;
	struct channel *chan;
	int __user *p = (int __user *)arg;

	switch (cmd) {
	case PPPIOCNEWUNIT:
		/* Create a new ppp unit */
		if (get_user(unit, p))
			break;
		ppp = ppp_create_interface(unit, &err);
		if (!ppp)
			break;
		file->private_data = &ppp->file;
		ppp->owner = file;
		err = -EFAULT;
		if (put_user(ppp->file.index, p))
			break;
		err = 0;
		break;

	case PPPIOCATTACH:
		/* Attach to an existing ppp unit */
		if (get_user(unit, p))
			break;
		mutex_lock(&all_ppp_mutex);
		err = -ENXIO;
		ppp = ppp_find_unit(unit);
		if (ppp) {
			atomic_inc(&ppp->file.refcnt);
			file->private_data = &ppp->file;
			err = 0;
		}
		mutex_unlock(&all_ppp_mutex);
		break;

	case PPPIOCATTCHAN:
		if (get_user(unit, p))
			break;
		err = -ENXIO;
		spin_lock_bh(&all_channels_lock);
		chan = ppp_find_channel(unit);
		if (chan) {
			atomic_inc(&chan->file.refcnt);
			file->private_data = &chan->file;
			err = 0;
		}
		spin_unlock_bh(&all_channels_lock);
		break;

	default:
		err = -ENOTTY;
	}
	return err;
}

static const struct file_operations ppp_device_fops = {
	.owner		= THIS_MODULE,
	.read		= ppp_read,
	.write		= ppp_write,
	.poll		= ppp_poll,
	.unlocked_ioctl = ppp_ioctl,
	.open		= ppp_open,
	.release	= ppp_release
};

#define PPP_MAJOR	108

/* Called at boot time if ppp is compiled into the kernel,
   or at module load time (from init_module) if compiled as a module. */
static int __init ppp_init(void)
{
	int err;

	printk(KERN_INFO "PPP generic driver version " PPP_VERSION "\n");
	err = register_chrdev(PPP_MAJOR, "ppp", &ppp_device_fops);
	if (err) {
	    printk(KERN_ERR "failed to register PPP device (%d)\n", err);
	    goto out;
	}

	ppp_class = class_create(THIS_MODULE, "ppp");
	if (IS_ERR(ppp_class)) {
		err = PTR_ERR(ppp_class);
		goto out_chrdev;
	}

	/* not a big deal if we fail here :-) */
	device_create(ppp_class, NULL, MKDEV(PPP_MAJOR, 0), "ppp");

#ifdef CONFIG_RALINK_GPIO_LED_VPN
	printk(KERN_INFO "PPP vpn led has gpio %d\n", GPIO_VPN_LED1);
	ppp_led.gpio = GPIO_VPN_LED1;
	ppp_led.on = 1;
	ppp_led.off = 1;
	ppp_led.blinks = 1;
	ppp_led.rests = 1;
	ppp_led.times = 1;
#endif
	return 0;

out_chrdev:
	unregister_chrdev(PPP_MAJOR, "ppp");
out:
	return err;

}

/*
 * Network interface unit routines.
 */
static int
ppp_start_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct ppp *ppp = netdev_priv(dev);
	int npi, proto;
	unsigned char *pp;

	npi = ethertype_to_npindex(ntohs(skb->protocol));
	if (npi < 0)
		goto outf;

	/* Drop, accept or reject the packet */
	switch (ppp->npmode[npi]) {
	case NPMODE_PASS:
		break;
	case NPMODE_QUEUE:
		/* it would be nice to have a way to tell the network
		   system to queue this one up for later. */
		goto outf;
	case NPMODE_DROP:
	case NPMODE_ERROR:
		goto outf;
	}

	/* Put the 2-byte PPP protocol number on the front,
	   making sure there is room for the address and control fields. */
	if (skb_cow_head(skb, PPP_HDRLEN))
		goto outf;

	pp = skb_push(skb, 2);
	proto = npindex_to_proto[npi];
	put_unaligned_be16(proto, pp);

	skb_queue_tail(&ppp->file.xq, skb);
	ppp_xmit_process(ppp);
	return NETDEV_TX_OK;

 outf:
	kfree_skb(skb);
	++ppp->stats.tx_dropped;
	return NETDEV_TX_OK;
}

static struct net_device_stats *
ppp_net_stats(struct net_device *dev)
{
	struct ppp *ppp = (struct ppp *) dev->priv;

	return &ppp->stats;
}

static int
ppp_net_ioctl(struct net_device *dev, struct ifreq *ifr, int cmd)
{
	struct ppp *ppp = netdev_priv(dev);
	int err = -EFAULT;
	void __user *addr = (void __user *) ifr->ifr_ifru.ifru_data;
	struct ppp_stats stats;
	struct ppp_comp_stats cstats;
	char *vers;

	switch (cmd) {
	case SIOCGPPPSTATS:
		ppp_get_stats(ppp, &stats);
		if (copy_to_user(addr, &stats, sizeof(stats)))
			break;
		err = 0;
		break;

	case SIOCGPPPCSTATS:
		memset(&cstats, 0, sizeof(cstats));
		if (ppp->xc_state)
			ppp->xcomp->comp_stat(ppp->xc_state, &cstats.c);
		if (ppp->rc_state)
			ppp->rcomp->decomp_stat(ppp->rc_state, &cstats.d);
		if (copy_to_user(addr, &cstats, sizeof(cstats)))
			break;
		err = 0;
		break;

	case SIOCGPPPVER:
		vers = PPP_VERSION;
		if (copy_to_user(addr, vers, strlen(vers) + 1))
			break;
		err = 0;
		break;

	default:
		err = -EINVAL;
	}

	return err;
}

static void ppp_setup(struct net_device *dev)
{
	dev->hard_header_len = PPP_HDRLEN;
	dev->mtu = PPP_MRU;
	dev->addr_len = 0;
	dev->tx_queue_len = 3;
	dev->type = ARPHRD_PPP;
	dev->flags = IFF_POINTOPOINT | IFF_NOARP | IFF_MULTICAST;
	dev->priv_flags &= ~IFF_XMIT_DST_RELEASE;
}

/*
 * Transmit-side routines.
 */

/*
 * Called to do any work queued up on the transmit side
 * that can now be done.
 */
static inline void
ppp_xmit_process(struct ppp *ppp)
{
	struct sk_buff *skb;

	ppp_xmit_lock(ppp);
	if (!ppp->closing) {
		ppp_push(ppp);
		while (!ppp->xmit_pending &&
		       (skb = skb_dequeue(&ppp->file.xq)))
			ppp_send_frame(ppp, skb);
		/* If there's no work left to do, tell the core net
		   code that we can accept some more. */
		if (!ppp->xmit_pending && !skb_peek(&ppp->file.xq))
			netif_wake_queue(ppp->dev);
		else
			netif_stop_queue(ppp->dev);
	}
	ppp_xmit_unlock(ppp);
}

static struct sk_buff *
pad_compress_skb(struct ppp *ppp, struct sk_buff *skb)
{
	struct sk_buff *new_skb;
	int len;
	int new_skb_size = ppp->dev->mtu +
		ppp->xcomp->comp_extra + ppp->dev->hard_header_len;
	int compressor_skb_size = ppp->dev->mtu +
		ppp->xcomp->comp_extra + PPP_HDRLEN;
	new_skb = alloc_skb(new_skb_size, GFP_ATOMIC);
	if (!new_skb) {
		if (net_ratelimit())
			printk(KERN_ERR "PPP: no memory (comp pkt)\n");
		return NULL;
	}
	if (ppp->dev->hard_header_len > PPP_HDRLEN)
		skb_reserve(new_skb, ppp->dev->hard_header_len - PPP_HDRLEN);

	/* compressor still expects A/C bytes in hdr */
	len = ppp->xcomp->compress(ppp->xc_state, skb->data - 2,
				   new_skb->data, skb->len + 2,
				   compressor_skb_size);
	if (len > 0 && (ppp->flags & SC_CCP_UP)) {
		kfree_skb(skb);
		skb = new_skb;
		skb_put(skb, len);
		skb_pull(skb, 2);	/* pull off A/C bytes */
	} else if (len == 0) {
		/* didn't compress, or CCP not up yet */
		kfree_skb(new_skb);
		new_skb = skb;
	} else {
		/*
		 * (len < 0)
		 * MPPE requires that we do not send unencrypted
		 * frames.  The compressor will return -1 if we
		 * should drop the frame.  We cannot simply test
		 * the compress_proto because MPPE and MPPC share
		 * the same number.
		 */
		if (net_ratelimit())
			printk(KERN_ERR "ppp: compressor dropped pkt\n");
		kfree_skb(skb);
		kfree_skb(new_skb);
		new_skb = NULL;
	}
	return new_skb;
}

/*
 * Compress and send a frame.
 * The caller should have locked the xmit path,
 * and xmit_pending should be 0.
 */
static void
ppp_send_frame(struct ppp *ppp, struct sk_buff *skb)
{
	int proto = PPP_PROTO(skb);
#ifdef CONFIG_SLHC
	struct sk_buff *new_skb;
	int len;
	unsigned char *cp;
#endif

	if (proto < 0x8000) {
#ifdef CONFIG_PPP_FILTER
		/* check if we should pass this packet */
		/* the filter instructions are constructed assuming
		   a four-byte PPP header on each packet */
		*skb_push(skb, 2) = 1;
		if (ppp->pass_filter &&
		    sk_run_filter(skb, ppp->pass_filter, ppp->pass_len) == 0) {
			if (ppp->debug & 1)
				printk(KERN_DEBUG "PPP: outbound frame not passed\n");
			kfree_skb(skb);
			return;
		}
		/* if this packet passes the active filter, record the time */
		if (!(ppp->active_filter &&
		      sk_run_filter(skb, ppp->active_filter, ppp->pass_len) == 0))
			ppp->last_xmit = jiffies;
		skb_pull(skb, 2);
#else
		/* for data packets, record the time */
		ppp->last_xmit = jiffies;
#endif /* CONFIG_PPP_FILTER */
	}

	++ppp->stats.tx_packets;
	ppp->stats.tx_bytes += skb->len - 2;

#ifdef CONFIG_SLHC
	switch (proto) {
	case PPP_IP:
		if (!ppp->vj || (ppp->flags & SC_COMP_TCP) == 0)
			break;
		/* try to do VJ TCP header compression */
		new_skb = alloc_skb(skb->len + ppp->dev->hard_header_len - 2,
				    GFP_ATOMIC);
		if (!new_skb) {
			printk(KERN_ERR "PPP: no memory (VJ comp pkt)\n");
			goto drop;
		}
		skb_reserve(new_skb, ppp->dev->hard_header_len - 2);
		cp = skb->data + 2;
		len = slhc_compress(ppp->vj, cp, skb->len - 2,
				    new_skb->data + 2, &cp,
				    !(ppp->flags & SC_NO_TCP_CCID));
		if (cp == skb->data + 2) {
			/* didn't compress */
			kfree_skb(new_skb);
		} else {
			if (cp[0] & SL_TYPE_COMPRESSED_TCP) {
				proto = PPP_VJC_COMP;
				cp[0] &= ~SL_TYPE_COMPRESSED_TCP;
			} else {
				proto = PPP_VJC_UNCOMP;
				cp[0] = skb->data[2];
			}
			kfree_skb(skb);
			skb = new_skb;
			cp = skb_put(skb, len + 2);
			cp[0] = 0;
			cp[1] = proto;
		}
		break;
	case PPP_CCP:
		/* peek at outbound CCP frames */
		ppp_ccp_peek(ppp, skb, 0);
		break;
	}
#else
	if (proto == PPP_CCP)
		/* peek at outbound CCP frames */
		ppp_ccp_peek(ppp, skb, 0);
#endif

	if (proto != PPP_LCP && proto != PPP_CCP) {
#ifdef CONFIG_PPP_PREVENT_DROP_SESSION_ON_FULL_CPU_LOAD
	    /* this is simple cpu based policer need for prevent drop session at high cpu load */
	    if (ppp_cpu_load >= 50000) {
		    /* skip all data in current time period */
		    if ((jiffies - cpu_prev_jiffies) >= (HZ>>1)) {
			/* get cpu load */
			load = weighted_cpuload(0);
			/* store current jiffies */
			cpu_prev_jiffies = jiffies;
		    }
		    /* drop if load high in current interval */
		    if (load > ppp_cpu_load) {
#ifdef DEBUG
			if (net_ratelimit())
			    printk(KERN_DEBUG "PPP: HIGH CPU LOAD %lu DROP PACKET\n", load);

#endif
#ifdef CONFIG_RALINK_WATCHDOG
			/* Refresh Ralink hardware watchdog timer, prevent reboot at high cpu load */
    			if(WdgLoadValue)
			    RaWdgReload();
#endif
			goto drop;      /* drop packet ... */
		    }
	    }
#endif
#ifdef CONFIG_RALINK_GPIO_LED_VPN
	    if ((jiffies - led_prev_jiffies) >= (HZ>>2)) {
		    /* blink led */
		    ralink_gpio_led_set(ppp_led);
		    led_prev_jiffies = jiffies;
	    }
#endif
	}

	/* try to do packet compression */
	if ((ppp->xstate & SC_COMP_RUN) && ppp->xc_state &&
	    proto != PPP_LCP && proto != PPP_CCP) {
		if (!(ppp->flags & SC_CCP_UP) && (ppp->flags & SC_MUST_COMP)) {
			if (net_ratelimit())
				printk(KERN_ERR "ppp: compression required but down - pkt dropped.\n");
			goto drop;
		}
		skb = pad_compress_skb(ppp, skb);
		if (!skb)
			goto drop;
	}

	/*
	 * If we are waiting for traffic (demand dialling),
	 * queue it up for pppd to receive.
	 */
	if (ppp->flags & SC_LOOP_TRAFFIC) {
		if (ppp->file.rq.qlen > PPP_MAX_RQLEN)
			goto drop;
		skb_queue_tail(&ppp->file.rq, skb);
		wake_up_interruptible(&ppp->file.rwait);
		return;
	}

	ppp->xmit_pending = skb;
	ppp_push(ppp);

	return;

drop:
#ifdef CONFIG_RALINK_GPIO_LED_VPN
        ralink_gpio_led_set(ppp_led);
#endif
	kfree_skb(skb);
	++ppp->stats.tx_errors;
}

/*
 * Try to send the frame in xmit_pending.
 * The caller should have the xmit path locked.
 */
static inline void
ppp_push(struct ppp *ppp)
{
	struct list_head *list;
	struct channel *pch;
	struct sk_buff *skb = ppp->xmit_pending;

	if (!skb)
		return;

	list = &ppp->channels;
	if (list_empty(list)) {
		/* nowhere to send the packet, just drop it */
		ppp->xmit_pending = NULL;
		kfree_skb(skb);
		return;
	}

#ifdef CONFIG_PPP_MULTILINK
	if ((ppp->flags & SC_MULTILINK) == 0) {
#endif
		/* not doing multilink: send it down the first channel */
		list = list->next;
		pch = list_entry(list, struct channel, clist);

		spin_lock_bh(&pch->downl);
		if (pch->chan) {
			if (pch->chan->ops->start_xmit(pch->chan, skb))
				ppp->xmit_pending = NULL;
		} else {
			/* channel got unregistered */
			kfree_skb(skb);
			ppp->xmit_pending = NULL;
		}
		spin_unlock_bh(&pch->downl);
		return;
#ifdef CONFIG_PPP_MULTILINK
	}

	/* Multilink: fragment the packet over as many links
	   as can take the packet at the moment. */
	if (!ppp_mp_explode(ppp, skb))
		return;
#endif /* CONFIG_PPP_MULTILINK */

	ppp->xmit_pending = NULL;
	kfree_skb(skb);
}

#ifdef CONFIG_PPP_MULTILINK
static bool mp_protocol_compress __read_mostly = true;
module_param(mp_protocol_compress, bool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(mp_protocol_compress,
		 "compress protocol id in multilink fragments");

/*
 * Divide a packet to be transmitted into fragments and
 * send them out the individual links.
 */
static int ppp_mp_explode(struct ppp *ppp, struct sk_buff *skb)
{
	int len, fragsize;
	int i, bits, hdrlen, mtu;
	int flen;
	int navail, nfree;
	int nbigger;
	unsigned char *p, *q;
	struct list_head *list;
	struct channel *pch;
	struct sk_buff *frag;
	struct ppp_channel *chan;

	nfree = 0;	/* # channels which have no packet already queued */
	navail = 0;	/* total # of usable channels (not deregistered) */
	hdrlen = (ppp->flags & SC_MP_XSHORTSEQ)? MPHDRLEN_SSN: MPHDRLEN;
	i = 0;
	list_for_each_entry(pch, &ppp->channels, clist) {
		navail += pch->avail = (pch->chan != NULL);
		if (pch->avail) {
			if (skb_queue_empty(&pch->file.xq) ||
			    !pch->had_frag) {
				pch->avail = 2;
				++nfree;
			}
			if (!pch->had_frag && i < ppp->nxchan)
				ppp->nxchan = i;
		}
		++i;
	}

	/*
	 * Don't start sending this packet unless at least half of
	 * the channels are free.  This gives much better TCP
	 * performance if we have a lot of channels.
	 */
	if (nfree == 0 || nfree < navail / 2)
		return 0;	/* can't take now, leave it in xmit_pending */

	/* Do protocol field compression */
	p = skb->data;
	len = skb->len;
	if (*p == 0 && mp_protocol_compress) {
		++p;
		--len;
	}

	/*
	 * Decide on fragment size.
	 * We create a fragment for each free channel regardless of
	 * how small they are (i.e. even 0 length) in order to minimize
	 * the time that it will take to detect when a channel drops
	 * a fragment.
	 */
	fragsize = len;
	if (nfree > 1)
		fragsize = DIV_ROUND_UP(fragsize, nfree);
	/* nbigger channels get fragsize bytes, the rest get fragsize-1,
	   except if nbigger==0, then they all get fragsize. */
	nbigger = len % nfree;

	/* skip to the channel after the one we last used
	   and start at that one */
	list = &ppp->channels;
	for (i = 0; i < ppp->nxchan; ++i) {
		list = list->next;
		if (list == &ppp->channels) {
			i = 0;
			break;
		}
	}

	/* create a fragment for each channel */
	bits = B;
	while (nfree > 0 || len > 0) {
		list = list->next;
		if (list == &ppp->channels) {
			i = 0;
			continue;
		}
		pch = list_entry(list, struct channel, clist);
		++i;
		if (!pch->avail)
			continue;

		/*
		 * Skip this channel if it has a fragment pending already and
		 * we haven't given a fragment to all of the free channels.
		 */
		if (pch->avail == 1) {
			if (nfree > 0)
				continue;
		} else {
			--nfree;
			pch->avail = 1;
		}

		/* check the channel's mtu and whether it is still attached. */
		spin_lock_bh(&pch->downl);
		if (pch->chan == NULL) {
			/* can't use this channel, it's being deregistered */
			spin_unlock_bh(&pch->downl);
			pch->avail = 0;
			if (--navail == 0)
				break;
			continue;
		}

		/*
		 * Create a fragment for this channel of
		 * min(max(mtu+2-hdrlen, 4), fragsize, len) bytes.
		 * If mtu+2-hdrlen < 4, that is a ridiculously small
		 * MTU, so we use mtu = 2 + hdrlen.
		 */
		if (fragsize > len)
			fragsize = len;
		flen = fragsize;

		/*
		 * hdrlen includes the 2-byte PPP protocol field, but the
		 * MTU counts only the payload excluding the protocol field.
		 * (RFC1661 Section 2)
		 */
		mtu = pch->chan->mtu - (hdrlen - 2);
		if (mtu < 4)
			mtu = 4;
		if (flen > mtu)
			flen = mtu;
		if (flen == len)
			bits |= E;
		frag = alloc_skb(flen + hdrlen + (flen == 0), GFP_ATOMIC);
		if (!frag)
			goto noskb;
		q = skb_put(frag, flen + hdrlen);

		/* make the MP header */
		put_unaligned_be16(PPP_MP, q);
		if (ppp->flags & SC_MP_XSHORTSEQ) {
			q[2] = bits + ((ppp->nxseq >> 8) & 0xf);
			q[3] = ppp->nxseq;
		} else {
			q[2] = bits;
			q[3] = ppp->nxseq >> 16;
			q[4] = ppp->nxseq >> 8;
			q[5] = ppp->nxseq;
		}

		/*
		 * Copy the data in.
		 * Unfortunately there is a bug in older versions of
		 * the Linux PPP multilink reconstruction code where it
		 * drops 0-length fragments.  Therefore we make sure the
		 * fragment has at least one byte of data.  Any bytes
		 * we add in this situation will end up as padding on the
		 * end of the reconstructed packet.
		 */
		if (!flen)
			*skb_put(frag, 1) = 0;
		else
			memcpy(q + hdrlen, p, flen);

		/* try to send it down the channel */
		chan = pch->chan;
		if (!skb_queue_empty(&pch->file.xq) ||
		    !chan->ops->start_xmit(chan, frag))
			skb_queue_tail(&pch->file.xq, frag);
		pch->had_frag = 1;
		p += flen;
		len -= flen;
		++ppp->nxseq;
		bits = 0;
		spin_unlock_bh(&pch->downl);

		if (--nbigger == 0 && fragsize > 0)
			--fragsize;
	}
	ppp->nxchan = i;

	return 1;

 noskb:
	spin_unlock_bh(&pch->downl);
	if (ppp->debug & 1)
		printk(KERN_ERR "PPP: no memory (fragment)\n");
	++ppp->stats.tx_errors;
	++ppp->nxseq;
	return 1;	/* abandon the frame */
}
#endif /* CONFIG_PPP_MULTILINK */

/*
 * Try to send data out on a channel.
 */
static void
ppp_channel_push(struct channel *pch)
{
	struct sk_buff *skb;
	struct ppp *ppp;

	spin_lock_bh(&pch->downl);
	if (pch->chan) {
		while (!skb_queue_empty(&pch->file.xq)) {
			skb = skb_dequeue(&pch->file.xq);
			if (!pch->chan->ops->start_xmit(pch->chan, skb)) {
				/* put the packet back and try again later */
				skb_queue_head(&pch->file.xq, skb);
				break;
			}
		}
	} else {
		/* channel got deregistered */
		skb_queue_purge(&pch->file.xq);
	}
	spin_unlock_bh(&pch->downl);
	/* see if there is anything from the attached unit to be sent */
	if (skb_queue_empty(&pch->file.xq)) {
		read_lock_bh(&pch->upl);
		ppp = pch->ppp;
		if (ppp)
			ppp_xmit_process(ppp);
		read_unlock_bh(&pch->upl);
	}
}

/*
 * Receive-side routines.
 */

/* misuse a few fields of the skb for MP reconstruction */
#define sequence	priority
#define BEbits		cb[0]

static inline void
ppp_do_recv(struct ppp *ppp, struct sk_buff *skb, struct channel *pch)
{
	ppp_recv_lock(ppp);
	if (!ppp->closing)
		ppp_receive_frame(ppp, skb, pch);
	else
		kfree_skb(skb);
	ppp_recv_unlock(ppp);
}

void
ppp_input(struct ppp_channel *chan, struct sk_buff *skb)
{
	struct channel *pch = chan->ppp;
	int proto;

	if (!pch) {
		kfree_skb(skb);
		return;
	}

	read_lock_bh(&pch->upl);
	if (!pskb_may_pull(skb, 2)) {
		kfree_skb(skb);
		if (pch->ppp) {
			++pch->ppp->dev->stats.rx_length_errors;
			ppp_receive_error(pch->ppp);
		}
		goto done;
	}

	proto = PPP_PROTO(skb);
	if (!pch->ppp || proto >= 0xc000 || proto == PPP_CCPFRAG) {
		/* put it on the channel queue */
		skb_queue_tail(&pch->file.rq, skb);
		/* drop old frames if queue too long */
		while (pch->file.rq.qlen > PPP_MAX_RQLEN &&
		       (skb = skb_dequeue(&pch->file.rq)))
			kfree_skb(skb);
		wake_up_interruptible(&pch->file.rwait);
	} else {
		ppp_do_recv(pch->ppp, skb, pch);
	}
done:
	read_unlock_bh(&pch->upl);
}

/* Put a 0-length skb in the receive queue as an error indication */
void
ppp_input_error(struct ppp_channel *chan, int code)
{
	struct channel *pch = chan->ppp;
	struct sk_buff *skb;

	if (!pch)
		return;

	read_lock_bh(&pch->upl);
	if (pch->ppp) {
		skb = alloc_skb(0, GFP_ATOMIC);
		if (skb) {
			skb->len = 0;		/* probably unnecessary */
			skb->cb[0] = code;
			ppp_do_recv(pch->ppp, skb, pch);
		}
	}
	read_unlock_bh(&pch->upl);
}

/*
 * We come in here to process a received frame.
 * The receive side of the ppp unit is locked.
 */
static inline void
ppp_receive_frame(struct ppp *ppp, struct sk_buff *skb, struct channel *pch)
{
	/* note: a 0-length skb is used as an error indication */
	if (likely(skb->len > 0)) {
		if (skb->ip_summed == CHECKSUM_COMPLETE)
		    skb->ip_summed = CHECKSUM_NONE;
#ifdef CONFIG_PPP_MULTILINK
		/* XXX do channel-level decompression here */
		if (PPP_PROTO(skb) == PPP_MP)
			ppp_receive_mp_frame(ppp, skb, pch);
		else
#endif /* CONFIG_PPP_MULTILINK */
			ppp_receive_nonmp_frame(ppp, skb);
	} else {
		kfree_skb(skb);
		ppp_receive_error(ppp);
	}
}

static inline  void
ppp_receive_error(struct ppp *ppp)
{
	++ppp->stats.rx_errors;
#ifdef CONFIG_SLHC
	if (ppp->vj)
		slhc_toss(ppp->vj);
#endif
}

static void
ppp_receive_nonmp_frame(struct ppp *ppp, struct sk_buff *skb)
{
#ifdef CONFIG_SLHC
	struct sk_buff *ns;
	int len;
#endif
	int npi, proto;

	/*
	 * Decompress the frame, if compressed.
	 * Note that some decompressors need to see uncompressed frames
	 * that come in as well as compressed frames.
	 */
	if (ppp->rc_state && (ppp->rstate & SC_DECOMP_RUN) &&
	    (ppp->rstate & (SC_DC_FERROR | SC_DC_ERROR)) == 0) {
		skb = ppp_decompress_frame(ppp, skb);

		/* Packet dropped */
		if (unlikely(skb == NULL)) {
			++ppp->stats.rx_dropped;
			return;
		}
	}

	if (unlikely(ppp->flags & SC_MUST_COMP && ppp->rstate & SC_DC_FERROR))
		goto err;

	proto = PPP_PROTO(skb);

#ifdef CONFIG_SLHC
	switch (proto) {
	case PPP_VJC_COMP:
		/* decompress VJ compressed packets */
		if (!ppp->vj || (ppp->flags & SC_REJ_COMP_TCP))
			goto err;

		if (skb_tailroom(skb) < 124 || skb_cloned(skb)) {
			/* copy to a new sk_buff with more tailroom */
			ns = dev_alloc_skb(skb->len + 128);
			if (!ns) {
				printk(KERN_ERR"PPP: no memory (VJ decomp)\n");
				goto err;
			}
			skb_reserve(ns, 2);
			skb_copy_bits(skb, 0, skb_put(ns, skb->len), skb->len);
			kfree_skb(skb);
			skb = ns;
		}
		else
			skb->ip_summed = CHECKSUM_NONE;

		len = slhc_uncompress(ppp->vj, skb->data + 2, skb->len - 2);
		if (len <= 0) {
			printk(KERN_DEBUG "PPP: VJ decompression error\n");
			goto err;
		}
		len += 2;
		if (len > skb->len)
			skb_put(skb, len - skb->len);
		else if (len < skb->len)
			skb_trim(skb, len);
		proto = PPP_IP;
		break;

	case PPP_VJC_UNCOMP:
		if (!ppp->vj || (ppp->flags & SC_REJ_COMP_TCP))
			goto err;

		/* Until we fix the decompressor need to make sure
		 * data portion is linear.
		 */
		if (!pskb_may_pull(skb, skb->len))
			goto err;
		if (slhc_remember(ppp->vj, skb->data + 2, skb->len - 2) <= 0) {
			printk(KERN_ERR "PPP: VJ uncompressed error\n");
			goto err;
		}
		proto = PPP_IP;
		break;

	case PPP_CCP:
		ppp_ccp_peek(ppp, skb, 1);
		break;
	}
#else
	if (proto == PPP_CCP)
		ppp_ccp_peek(ppp, skb, 1);
#endif

	++ppp->stats.rx_packets;
	ppp->stats.rx_bytes += skb->len - 2;

	npi = proto_to_npindex(proto);
	if (unlikely(npi < 0)) {
		/* control or unknown frame - pass it to pppd */
		skb_queue_tail(&ppp->file.rq, skb);
		/* limit queue length by dropping old frames */
		while (ppp->file.rq.qlen > PPP_MAX_RQLEN &&
		       (skb = skb_dequeue(&ppp->file.rq)))
			kfree_skb(skb);
		/* wake up any process polling or blocking on read */
		wake_up_interruptible(&ppp->file.rwait);

	} else {
		/* network protocol frame - give it to the kernel */

#ifdef CONFIG_PPP_FILTER
		/* check if the packet passes the pass and active filters */
		/* the filter instructions are constructed assuming
		   a four-byte PPP header on each packet */
		if (ppp->pass_filter || ppp->active_filter) {
			if (skb_unclone(skb, GFP_ATOMIC))
				goto err;

			*skb_push(skb, 2) = 0;
			if (ppp->pass_filter &&
			    sk_run_filter(skb, ppp->pass_filter, ppp->pass_len) == 0) {
				if (ppp->debug & 1)
					printk(KERN_DEBUG "PPP: inbound frame "
					       "not passed\n");
				kfree_skb(skb);
				return;
			}
			if (!(ppp->active_filter &&
			      sk_run_filter(skb, ppp->active_filter, ppp->pass_len) == 0))
				ppp->last_recv = jiffies;
			__skb_pull(skb, 2);
		} else
#endif /* CONFIG_PPP_FILTER */
			ppp->last_recv = jiffies;

		if ((ppp->dev->flags & IFF_UP) == 0 ||
		    ppp->npmode[npi] != NPMODE_PASS) {
			kfree_skb(skb);
		} else {
			/* chop off protocol */
			skb_pull_rcsum(skb, 2);
			skb->dev = ppp->dev;
			skb->protocol = htons(npindex_to_ethertype[npi]);
			skb_reset_mac_header(skb);
			netif_rx(skb);
		}
	}
	return;

 err:
	kfree_skb(skb);
	ppp_receive_error(ppp);
}

static struct sk_buff *
ppp_decompress_frame(struct ppp *ppp, struct sk_buff *skb)
{
	int proto = PPP_PROTO(skb);
	struct sk_buff *ns;
	int len;

	/* Until we fix all the decompressor's need to make sure
	 * data portion is linear.
	 */
	if (!pskb_may_pull(skb, skb->len))
		goto err;

	if (proto == PPP_COMP) {
		int obuff_size;

		switch(ppp->rcomp->compress_proto) {
		case CI_MPPE:
			obuff_size = ppp->mru + PPP_HDRLEN + 1;
			break;
		default:
			obuff_size = ppp->mru + PPP_HDRLEN;
			break;
		}

		ns = dev_alloc_skb(obuff_size);
		if (!ns) {
			printk(KERN_ERR "ppp_decompress_frame: no memory\n");
			goto err;
		}

		/* the decompressor still expects the A/C bytes in the hdr */
		len = ppp->rcomp->decompress(ppp->rc_state, skb->data - 2,
				skb->len + 2, ns->data, obuff_size);
		if (len < 0) {
			/* Drop the packet and continue */
			if (len == DECOMP_DROPERROR) {
				kfree_skb(ns);
				kfree_skb(skb);
				skb = NULL;
				goto err2;
			}
			/* Pass the compressed frame to pppd as an
			   error indication. */
			if (len == DECOMP_FATALERROR)
				ppp->rstate |= SC_DC_FERROR;
			kfree_skb(ns);
			goto err;
		}
#if defined(CONFIG_RA_HW_NAT) || defined(CONFIG_RA_HW_NAT_MODULE)
#if !defined(HNAT_USE_TAILROOM)
		if(ra_sw_nat_hook_rx != NULL)
			memcpy(FOE_INFO_START_ADDR(ns), FOE_INFO_START_ADDR(skb), FOE_INFO_LEN); // copy FoE Info
#endif
#endif
		kfree_skb(skb);
		skb = ns;
		skb_put(skb, len);
		skb_pull(skb, 2);	/* pull off the A/C bytes */

	} else {
		/* Uncompressed frame - pass to decompressor so it
		   can update its dictionary if necessary. */
		if (ppp->rcomp->incomp)
			ppp->rcomp->incomp(ppp->rc_state, skb->data - 2,
					   skb->len + 2);
	}

	return skb;

 err:
	ppp->rstate |= SC_DC_ERROR;
 err2:
	ppp_receive_error(ppp);
	return skb;
}

#ifdef CONFIG_PPP_MULTILINK
/*
 * Receive a multilink frame.
 * We put it on the reconstruction queue and then pull off
 * as many completed frames as we can.
 */
static void
ppp_receive_mp_frame(struct ppp *ppp, struct sk_buff *skb, struct channel *pch)
{
	u32 mask, seq;
	struct channel *ch;
	int mphdrlen = (ppp->flags & SC_MP_SHORTSEQ)? MPHDRLEN_SSN: MPHDRLEN;

	if (!pskb_may_pull(skb, mphdrlen + 1) || ppp->mrru == 0)
		goto err;		/* no good, throw it away */

	/* Decode sequence number and begin/end bits */
	if (ppp->flags & SC_MP_SHORTSEQ) {
		seq = ((skb->data[2] & 0x0f) << 8) | skb->data[3];
		mask = 0xfff;
	} else {
		seq = (skb->data[3] << 16) | (skb->data[4] << 8)| skb->data[5];
		mask = 0xffffff;
	}
	skb->BEbits = skb->data[2];
	skb_pull(skb, mphdrlen);	/* pull off PPP and MP headers */

	/*
	 * Do protocol ID decompression on the first fragment of each packet.
	 */
	if ((skb->BEbits & B) && (skb->data[0] & 1))
		*skb_push(skb, 1) = 0;

	/*
	 * Expand sequence number to 32 bits, making it as close
	 * as possible to ppp->minseq.
	 */
	seq |= ppp->minseq & ~mask;
	if ((int)(ppp->minseq - seq) > (int)(mask >> 1))
		seq += mask + 1;
	else if ((int)(seq - ppp->minseq) > (int)(mask >> 1))
		seq -= mask + 1;	/* should never happen */
	skb->sequence = seq;
	pch->lastseq = seq;

	/*
	 * If this packet comes before the next one we were expecting,
	 * drop it.
	 */
	if (seq_before(seq, ppp->nextseq)) {
		kfree_skb(skb);
		++ppp->stats.rx_dropped;
		ppp_receive_error(ppp);
		return;
	}

	/*
	 * Reevaluate minseq, the minimum over all channels of the
	 * last sequence number received on each channel.  Because of
	 * the increasing sequence number rule, we know that any fragment
	 * before `minseq' which hasn't arrived is never going to arrive.
	 * The list of channels can't change because we have the receive
	 * side of the ppp unit locked.
	 */
	list_for_each_entry(ch, &ppp->channels, clist) {
		if (seq_before(ch->lastseq, seq))
			seq = ch->lastseq;
	}
	if (seq_before(ppp->minseq, seq))
		ppp->minseq = seq;

	/* Put the fragment on the reconstruction queue */
	ppp_mp_insert(ppp, skb);

	/* If the queue is getting long, don't wait any longer for packets
	   before the start of the queue. */
	if (skb_queue_len(&ppp->mrq) >= PPP_MP_MAX_QLEN) {
		struct sk_buff *mskb = skb_peek(&ppp->mrq);
		if (seq_before(ppp->minseq, mskb->sequence))
			ppp->minseq = mskb->sequence;
	}

	/* Pull completed packets off the queue and receive them. */
	while ((skb = ppp_mp_reconstruct(ppp))) {
		if (pskb_may_pull(skb, 2))
		ppp_receive_nonmp_frame(ppp, skb);
		else {
			++ppp->dev->stats.rx_length_errors;
			kfree_skb(skb);
			ppp_receive_error(ppp);
		}
	}

	return;

 err:
	kfree_skb(skb);
	ppp_receive_error(ppp);
}

/*
 * Insert a fragment on the MP reconstruction queue.
 * The queue is ordered by increasing sequence number.
 */
static void
ppp_mp_insert(struct ppp *ppp, struct sk_buff *skb)
{
	struct sk_buff *p;
	struct sk_buff_head *list = &ppp->mrq;
	u32 seq = skb->sequence;

	/* N.B. we don't need to lock the list lock because we have the
	   ppp unit receive-side lock. */
	for (p = list->next; p != (struct sk_buff *)list; p = p->next)
		if (seq_before(seq, p->sequence))
			break;
	__skb_insert(skb, p->prev, p, list);
}

/*
 * Reconstruct a packet from the MP fragment queue.
 * We go through increasing sequence numbers until we find a
 * complete packet, or we get to the sequence number for a fragment
 * which hasn't arrived but might still do so.
 */
static struct sk_buff *
ppp_mp_reconstruct(struct ppp *ppp)
{
	u32 seq = ppp->nextseq;
	u32 minseq = ppp->minseq;
	struct sk_buff_head *list = &ppp->mrq;
	struct sk_buff *p, *tmp;
	struct sk_buff *head, *tail;
	struct sk_buff *skb = NULL;
	int lost = 0, len = 0;

	if (ppp->mrru == 0)     /* do nothing until mrru is set */
		return NULL;
	head = list->next;
	tail = NULL;
	skb_queue_walk_safe(list, p, tmp) {
	again:
		if (seq_before(p->sequence, seq)) {
			/* this can't happen, anyway ignore the skb */
			printk(KERN_ERR "ppp_mp_reconstruct bad seq %u < %u\n",
			       p->sequence, seq);
			__skb_unlink(p, list);
			kfree_skb(p);
			continue;
		}
		if (p->sequence != seq) {
			u32 oldseq;
			/* Fragment `seq' is missing.  If it is after
			   minseq, it might arrive later, so stop here. */
			if (seq_after(seq, minseq))
				break;
			/* Fragment `seq' is lost, keep going. */
			lost = 1;
			oldseq = seq;
			seq = seq_before(minseq, p->sequence)?
				minseq + 1: p->sequence;

			if (ppp->debug & 1)
				printk(KERN_DEBUG, ppp->dev,
					      "lost frag %u..%u\n",
					      oldseq, seq-1);
			goto again;
		}

		/*
		 * At this point we know that all the fragments from
		 * ppp->nextseq to seq are either present or lost.
		 * Also, there are no complete packets in the queue
		 * that have no missing fragments and end before this
		 * fragment.
		 */

		/* B bit set indicates this fragment starts a packet */
		if (p->BEbits & B) {
			head = p;
			lost = 0;
			len = 0;
		}

		len += p->len;

		/* Got a complete packet yet? */
		if (lost == 0 && (p->BEbits & E) && (head->BEbits & B)) {
			if (len > ppp->mrru + 2) {
				++ppp->stats.rx_length_errors;
				printk(KERN_DEBUG "PPP: reconstructed packet"
				       " is too long (%d)\n", len);
			} else {
				tail = p;
				break;
			}
			ppp->nextseq = seq + 1;
		}

		/*
		 * If this is the ending fragment of a packet,
		 * and we haven't found a complete valid packet yet,
		 * we can discard up to and including this fragment.
		 */
		if (p->BEbits & E) {
			struct sk_buff *tmp2;

			skb_queue_reverse_walk_from_safe(list, p, tmp2) {
				__skb_unlink(p, list);
				kfree_skb(p);
                        }
			head = skb_peek(list);
			if (!head)
				break;
		}

		++seq;
	}

	/* If we have a complete packet, copy it all into one skb. */
	if (tail != NULL) {
		/* If we have discarded any fragments,
		   signal a receive error. */
		if (head->sequence != ppp->nextseq) {
			if (ppp->debug & 1)
				printk(KERN_DEBUG "  missed pkts %u..%u\n",
				       ppp->nextseq, head->sequence-1);
			++ppp->stats.rx_dropped;
			ppp_receive_error(ppp);
		}

		skb = head;
		if (head != tail) {
			struct sk_buff **fragpp = &skb_shinfo(skb)->frag_list;
			p = skb_queue_next(list, head);
			__skb_unlink(skb, list);
			skb_queue_walk_from_safe(list, p, tmp) {
				__skb_unlink(p, list);
				*fragpp = p;
				p->next = NULL;
				fragpp = &p->next;

				skb->len += p->len;
				skb->data_len += p->len;
				skb->truesize += p->truesize;

				if (p == tail)
					break;
			}
		} else {
			__skb_unlink(skb, list);
		}

		ppp->nextseq = tail->sequence + 1;
		head = tail->next;
	}

	return skb;
}
#endif /* CONFIG_PPP_MULTILINK */

/*
 * Channel interface.
 */

/*
 * Create a new, unattached ppp channel.
 */
int
ppp_register_channel(struct ppp_channel *chan)
{
	struct channel *pch;

	pch = kzalloc(sizeof(struct channel), GFP_KERNEL);
	if (!pch)
		return -ENOMEM;
	pch->ppp = NULL;
	pch->chan = chan;
	chan->ppp = pch;
	init_ppp_file(&pch->file, CHANNEL);
	pch->file.hdrlen = chan->hdrlen;
#ifdef CONFIG_PPP_MULTILINK
	pch->lastseq = -1;
#endif /* CONFIG_PPP_MULTILINK */
	init_rwsem(&pch->chan_sem);
	spin_lock_init(&pch->downl);
	rwlock_init(&pch->upl);
	spin_lock_bh(&all_channels_lock);
	pch->file.index = ++last_channel_index;
	list_add(&pch->list, &new_channels);
	atomic_inc(&channel_count);
	spin_unlock_bh(&all_channels_lock);
	return 0;
}

/*
 * Return the index of a channel.
 */
inline int ppp_channel_index(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;

	if (pch)
		return pch->file.index;
	return -1;
}

/*
 * Return the PPP unit number to which a channel is connected.
 */
int ppp_unit_number(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;
	int unit = -1;

	if (pch) {
		read_lock_bh(&pch->upl);
		if (pch->ppp)
			unit = pch->ppp->file.index;
		read_unlock_bh(&pch->upl);
	}
	return unit;
}

/*
 * Return the PPP device interface name of a channel.
 */
char *ppp_dev_name(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;
	char *name = NULL;

	if (pch) {
		read_lock_bh(&pch->upl);
		if (pch->ppp && pch->ppp->dev)
			name = pch->ppp->dev->name;
		read_unlock_bh(&pch->upl);
	}
	return name;
}

/*
 * Disconnect a channel from the generic layer.
 * This must be called in process context.
 */
void
ppp_unregister_channel(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;

	if (!pch)
		return;		/* should never happen */
	chan->ppp = NULL;

	/*
	 * This ensures that we have returned from any calls into the
	 * the channel's start_xmit or ioctl routine before we proceed.
	 */
	down_write(&pch->chan_sem);
	spin_lock_bh(&pch->downl);
	pch->chan = NULL;
	spin_unlock_bh(&pch->downl);
	up_write(&pch->chan_sem);
	ppp_disconnect_channel(pch);
	spin_lock_bh(&all_channels_lock);
	list_del(&pch->list);
	spin_unlock_bh(&all_channels_lock);
	pch->file.dead = 1;
	wake_up_interruptible(&pch->file.rwait);
	if (atomic_dec_and_test(&pch->file.refcnt))
		ppp_destroy_channel(pch);
}

/*
 * Callback from a channel when it can accept more to transmit.
 * This should be called at BH/softirq level, not interrupt level.
 */
void
ppp_output_wakeup(struct ppp_channel *chan)
{
	struct channel *pch = chan->ppp;

	if (!pch)
		return;
	ppp_channel_push(pch);
}

/*
 * Compression control.
 */

/* Process the PPPIOCSCOMPRESS ioctl. */
static int
ppp_set_compress(struct ppp *ppp, unsigned long arg)
{
	int err;
	struct compressor *cp, *ocomp;
	struct ppp_option_data data;
	void *state, *ostate;
	unsigned char ccp_option[CCP_MAX_OPTION_LENGTH];

	err = -EFAULT;
	if (copy_from_user(&data, (void __user *) arg, sizeof(data)) ||
	    (data.length <= CCP_MAX_OPTION_LENGTH &&
	     copy_from_user(ccp_option, (void __user *) data.ptr, data.length)))
		goto out;
	err = -EINVAL;
	if (data.length > CCP_MAX_OPTION_LENGTH ||
	    ccp_option[1] < 2 || ccp_option[1] > data.length)
		goto out;

	cp = find_compressor(ccp_option[0]);
#ifdef CONFIG_KMOD
	if (!cp) {
		request_module("ppp-compress-%d", ccp_option[0]);
		cp = find_compressor(ccp_option[0]);
	}
#endif /* CONFIG_KMOD */
	if (!cp)
		goto out;

	err = -ENOBUFS;
	if (data.transmit) {
		state = cp->comp_alloc(ccp_option, data.length);
		if (state) {
			ppp_xmit_lock(ppp);
			ppp->xstate &= ~SC_COMP_RUN;
			ocomp = ppp->xcomp;
			ostate = ppp->xc_state;
			ppp->xcomp = cp;
			ppp->xc_state = state;
			ppp_xmit_unlock(ppp);
			if (ostate) {
				ocomp->comp_free(ostate);
				module_put(ocomp->owner);
			}
			err = 0;
		} else
			module_put(cp->owner);

	} else {
		state = cp->decomp_alloc(ccp_option, data.length);
		if (state) {
			ppp_recv_lock(ppp);
			ppp->rstate &= ~SC_DECOMP_RUN;
			ocomp = ppp->rcomp;
			ostate = ppp->rc_state;
			ppp->rcomp = cp;
			ppp->rc_state = state;
			ppp_recv_unlock(ppp);
			if (ostate) {
				ocomp->decomp_free(ostate);
				module_put(ocomp->owner);
			}
			err = 0;
		} else
			module_put(cp->owner);
	}

 out:
	return err;
}

/*
 * Look at a CCP packet and update our state accordingly.
 * We assume the caller has the xmit or recv path locked.
 */
static void
ppp_ccp_peek(struct ppp *ppp, struct sk_buff *skb, int inbound)
{
	unsigned char *dp;
	int len;

	if (!pskb_may_pull(skb, CCP_HDRLEN + 2))
		return;	/* no header */
	dp = skb->data + 2;

	switch (CCP_CODE(dp)) {
	case CCP_CONFREQ:

		/* A ConfReq starts negotiation of compression
		 * in one direction of transmission,
		 * and hence brings it down...but which way?
		 *
		 * Remember:
		 * A ConfReq indicates what the sender would like to receive
		 */
		if(inbound)
			/* He is proposing what I should send */
			ppp->xstate &= ~SC_COMP_RUN;
		else
			/* I am proposing to what he should send */
			ppp->rstate &= ~SC_DECOMP_RUN;

		break;

	case CCP_TERMREQ:
	case CCP_TERMACK:
		/*
		 * CCP is going down, both directions of transmission
		 */
		ppp->rstate &= ~SC_DECOMP_RUN;
		ppp->xstate &= ~SC_COMP_RUN;
		break;

	case CCP_CONFACK:
		if ((ppp->flags & (SC_CCP_OPEN | SC_CCP_UP)) != SC_CCP_OPEN)
			break;
		len = CCP_LENGTH(dp);
		if (!pskb_may_pull(skb, len + 2))
			return;		/* too short */
		dp += CCP_HDRLEN;
		len -= CCP_HDRLEN;
		if (len < CCP_OPT_MINLEN || len < CCP_OPT_LENGTH(dp))
			break;
		if (inbound) {
			/* we will start receiving compressed packets */
			if (!ppp->rc_state)
				break;
			if (ppp->rcomp->decomp_init(ppp->rc_state, dp, len,
					ppp->file.index, 0, ppp->mru, ppp->debug)) {
				ppp->rstate |= SC_DECOMP_RUN;
				ppp->rstate &= ~(SC_DC_ERROR | SC_DC_FERROR);
			}
		} else {
			/* we will soon start sending compressed packets */
			if (!ppp->xc_state)
				break;
			if (ppp->xcomp->comp_init(ppp->xc_state, dp, len,
					ppp->file.index, 0, ppp->debug))
				ppp->xstate |= SC_COMP_RUN;
		}
		break;

	case CCP_RESETACK:
		/* reset the [de]compressor */
		if ((ppp->flags & SC_CCP_UP) == 0)
			break;
		if (inbound) {
			if (ppp->rc_state && (ppp->rstate & SC_DECOMP_RUN)) {
				ppp->rcomp->decomp_reset(ppp->rc_state);
				ppp->rstate &= ~SC_DC_ERROR;
			}
		} else {
			if (ppp->xc_state && (ppp->xstate & SC_COMP_RUN))
				ppp->xcomp->comp_reset(ppp->xc_state);
		}
		break;
	}
}

/* Free up compression resources. */
static void
ppp_ccp_closed(struct ppp *ppp)
{
	void *xstate, *rstate;
	struct compressor *xcomp, *rcomp;

	ppp_lock(ppp);
	ppp->flags &= ~(SC_CCP_OPEN | SC_CCP_UP);
	ppp->xstate = 0;
	xcomp = ppp->xcomp;
	xstate = ppp->xc_state;
	ppp->xc_state = NULL;
	ppp->rstate = 0;
	rcomp = ppp->rcomp;
	rstate = ppp->rc_state;
	ppp->rc_state = NULL;
	ppp_unlock(ppp);

	if (xstate) {
		xcomp->comp_free(xstate);
		module_put(xcomp->owner);
	}
	if (rstate) {
		rcomp->decomp_free(rstate);
		module_put(rcomp->owner);
	}
}

/* List of compressors. */
static LIST_HEAD(compressor_list);
static DEFINE_SPINLOCK(compressor_list_lock);

struct compressor_entry {
	struct list_head list;
	struct compressor *comp;
};

static struct compressor_entry *
find_comp_entry(int proto)
{
	struct compressor_entry *ce;

	list_for_each_entry(ce, &compressor_list, list) {
		if (ce->comp->compress_proto == proto)
			return ce;
	}
	return NULL;
}

/* Register a compressor */
int
ppp_register_compressor(struct compressor *cp)
{
	struct compressor_entry *ce;
	int ret;
	spin_lock(&compressor_list_lock);
	ret = -EEXIST;
	if (find_comp_entry(cp->compress_proto))
		goto out;
	ret = -ENOMEM;
	ce = kmalloc(sizeof(struct compressor_entry), GFP_ATOMIC);
	if (!ce)
		goto out;
	ret = 0;
	ce->comp = cp;
	list_add(&ce->list, &compressor_list);
 out:
	spin_unlock(&compressor_list_lock);
	return ret;
}

/* Unregister a compressor */
void
ppp_unregister_compressor(struct compressor *cp)
{
	struct compressor_entry *ce;

	spin_lock(&compressor_list_lock);
	ce = find_comp_entry(cp->compress_proto);
	if (ce && ce->comp == cp) {
		list_del(&ce->list);
		kfree(ce);
	}
	spin_unlock(&compressor_list_lock);
}

/* Find a compressor. */
static struct compressor *
find_compressor(int type)
{
	struct compressor_entry *ce;
	struct compressor *cp = NULL;

	spin_lock(&compressor_list_lock);
	ce = find_comp_entry(type);
	if (ce) {
		cp = ce->comp;
		if (!try_module_get(cp->owner))
			cp = NULL;
	}
	spin_unlock(&compressor_list_lock);
	return cp;
}

/*
 * Miscelleneous stuff.
 */

static void
ppp_get_stats(struct ppp *ppp, struct ppp_stats *st)
{
#ifdef CONFIG_SLHC
	struct slcompress *vj = ppp->vj;
#endif
	memset(st, 0, sizeof(*st));
	st->p.ppp_ipackets = ppp->stats.rx_packets;
	st->p.ppp_ierrors = ppp->stats.rx_errors;
	st->p.ppp_ibytes = ppp->stats.rx_bytes;
	st->p.ppp_opackets = ppp->stats.tx_packets;
	st->p.ppp_oerrors = ppp->stats.tx_errors;
	st->p.ppp_obytes = ppp->stats.tx_bytes;
#ifdef CONFIG_SLHC
	if (!vj)
		return;
	st->vj.vjs_packets = vj->sls_o_compressed + vj->sls_o_uncompressed;
	st->vj.vjs_compressed = vj->sls_o_compressed;
	st->vj.vjs_searches = vj->sls_o_searches;
	st->vj.vjs_misses = vj->sls_o_misses;
	st->vj.vjs_errorin = vj->sls_i_error;
	st->vj.vjs_tossed = vj->sls_i_tossed;
	st->vj.vjs_uncompressedin = vj->sls_i_uncompressed;
	st->vj.vjs_compressedin = vj->sls_i_compressed;
#endif
}

/*
 * Stuff for handling the lists of ppp units and channels
 * and for initialization.
 */

/*
 * Create a new ppp interface unit.  Fails if it can't allocate memory
 * or if there is already a unit with the requested number.
 * unit == -1 means allocate a new number.
 */
static struct ppp *
ppp_create_interface(int unit, int *retp)
{
	struct ppp *ppp;
	struct net_device *dev = NULL;
	int ret = -ENOMEM;
	int i;

	dev = alloc_netdev(sizeof(struct ppp), "", ppp_setup);
	if (!dev)
		goto out1;

	ppp = netdev_priv(dev);
	ppp->dev = dev;
	ppp->mru = PPP_MRU;
	init_ppp_file(&ppp->file, INTERFACE);
	ppp->file.hdrlen = PPP_HDRLEN - 2;	/* don't count proto bytes */
	for (i = 0; i < NUM_NP; ++i)
		ppp->npmode[i] = NPMODE_PASS;
	INIT_LIST_HEAD(&ppp->channels);
	spin_lock_init(&ppp->rlock);
	spin_lock_init(&ppp->wlock);
#ifdef CONFIG_PPP_MULTILINK
	ppp->minseq = -1;
	skb_queue_head_init(&ppp->mrq);
#endif /* CONFIG_PPP_MULTILINK */

	dev->hard_start_xmit = ppp_start_xmit;
	dev->get_stats = ppp_net_stats;
	dev->do_ioctl = ppp_net_ioctl;

	ret = -EEXIST;
	mutex_lock(&all_ppp_mutex);
	if (unit < 0)
		unit = cardmap_find_first_free(all_ppp_units);
	else if (cardmap_get(all_ppp_units, unit) != NULL)
		goto out2;	/* unit already exists */

	/* Initialize the new ppp unit */
	ppp->file.index = unit;
	sprintf(dev->name, "ppp%d", unit);

	ret = register_netdev(dev);
	if (ret) {
		printk(KERN_ERR "PPP: couldn't register device %s (%d)\n",
		       dev->name, ret);
		goto out2;
	}

	atomic_inc(&ppp_unit_count);
	ret = cardmap_set(&all_ppp_units, unit, ppp);
	if (ret)
		goto out3;

	mutex_unlock(&all_ppp_mutex);
	*retp = 0;
	return ppp;

out3:
	atomic_dec(&ppp_unit_count);
out2:
	mutex_unlock(&all_ppp_mutex);
	free_netdev(dev);
out1:
	*retp = ret;
	return NULL;
}

/*
 * Initialize a ppp_file structure.
 */
static void
init_ppp_file(struct ppp_file *pf, int kind)
{
	pf->kind = kind;
	skb_queue_head_init(&pf->xq);
	skb_queue_head_init(&pf->rq);
	atomic_set(&pf->refcnt, 1);
	init_waitqueue_head(&pf->rwait);
}

/*
 * Take down a ppp interface unit - called when the owning file
 * (the one that created the unit) is closed or detached.
 */
static void ppp_shutdown_interface(struct ppp *ppp)
{
	mutex_lock(&all_ppp_mutex);
	/* This will call dev_close() for us. */
	ppp_lock(ppp);
	if (!ppp->closing) {
		ppp->closing = 1;
		ppp_unlock(ppp);
		unregister_netdev(ppp->dev);
	} else
		ppp_unlock(ppp);

	cardmap_set(&all_ppp_units, ppp->file.index, NULL);
	ppp->file.dead = 1;
	ppp->owner = NULL;
	wake_up_interruptible(&ppp->file.rwait);
	mutex_unlock(&all_ppp_mutex);
}

/*
 * Free the memory used by a ppp unit.  This is only called once
 * there are no channels connected to the unit and no file structs
 * that reference the unit.
 */
static void ppp_destroy_interface(struct ppp *ppp)
{
	atomic_dec(&ppp_unit_count);

	if (!ppp->file.dead || ppp->n_channels) {
		/* "can't happen" */
		printk(KERN_ERR "ppp: destroying ppp struct %p but dead=%d "
		       "n_channels=%d !\n", ppp, ppp->file.dead,
		       ppp->n_channels);
		return;
	}

	ppp_ccp_closed(ppp);
#ifdef CONFIG_SLHC
	if (ppp->vj) {
		slhc_free(ppp->vj);
		ppp->vj = NULL;
	}
#endif
	skb_queue_purge(&ppp->file.xq);
	skb_queue_purge(&ppp->file.rq);
#ifdef CONFIG_PPP_MULTILINK
	skb_queue_purge(&ppp->mrq);
#endif /* CONFIG_PPP_MULTILINK */
#ifdef CONFIG_PPP_FILTER
	kfree(ppp->pass_filter);
	ppp->pass_filter = NULL;
	kfree(ppp->active_filter);
	ppp->active_filter = NULL;
#endif /* CONFIG_PPP_FILTER */

	kfree_skb(ppp->xmit_pending);

	free_netdev(ppp->dev);
}

/*
 * Locate an existing ppp unit.
 * The caller should have locked the all_ppp_mutex.
 */
static struct ppp *
ppp_find_unit(int unit)
{
	return cardmap_get(all_ppp_units, unit);
}

/*
 * Locate an existing ppp channel.
 * The caller should have locked the all_channels_lock.
 * First we look in the new_channels list, then in the
 * all_channels list.  If found in the new_channels list,
 * we move it to the all_channels list.  This is for speed
 * when we have a lot of channels in use.
 */
static struct channel *
ppp_find_channel(int unit)
{
	struct channel *pch;

	list_for_each_entry(pch, &new_channels, list) {
		if (pch->file.index == unit) {
			list_move(&pch->list, &all_channels);
			return pch;
		}
	}
	list_for_each_entry(pch, &all_channels, list) {
		if (pch->file.index == unit)
			return pch;
	}
	return NULL;
}

/*
 * Connect a PPP channel to a PPP interface unit.
 */
static int
ppp_connect_channel(struct channel *pch, int unit)
{
	struct ppp *ppp;
	int ret = -ENXIO;
	int hdrlen;

	mutex_lock(&all_ppp_mutex);
	ppp = ppp_find_unit(unit);
	if (!ppp)
		goto out;
	write_lock_bh(&pch->upl);
	ret = -EINVAL;
	if (pch->ppp)
		goto outl;

	ppp_lock(ppp);
	if (pch->file.hdrlen > ppp->file.hdrlen)
		ppp->file.hdrlen = pch->file.hdrlen;
	hdrlen = pch->file.hdrlen + 2;	/* for protocol bytes */
	if (hdrlen > ppp->dev->hard_header_len)
		ppp->dev->hard_header_len = hdrlen;
	list_add_tail(&pch->clist, &ppp->channels);
	++ppp->n_channels;
	pch->ppp = ppp;
	atomic_inc(&ppp->file.refcnt);
	ppp_unlock(ppp);
	ret = 0;

 outl:
	write_unlock_bh(&pch->upl);
 out:
	mutex_unlock(&all_ppp_mutex);
	return ret;
}

/*
 * Disconnect a channel from its ppp unit.
 */
static int
ppp_disconnect_channel(struct channel *pch)
{
	struct ppp *ppp;
	int err = -EINVAL;

	write_lock_bh(&pch->upl);
	ppp = pch->ppp;
	pch->ppp = NULL;
	write_unlock_bh(&pch->upl);
	if (ppp) {
		/* remove it from the ppp unit's list */
		ppp_lock(ppp);
		list_del(&pch->clist);
		if (--ppp->n_channels == 0)
			wake_up_interruptible(&ppp->file.rwait);
		ppp_unlock(ppp);
		if (atomic_dec_and_test(&ppp->file.refcnt))
			ppp_destroy_interface(ppp);
		err = 0;
	}
	return err;
}

/*
 * Free up the resources used by a ppp channel.
 */
static void ppp_destroy_channel(struct channel *pch)
{
	atomic_dec(&channel_count);

	if (!pch->file.dead) {
		/* "can't happen" */
		printk(KERN_ERR "ppp: destroying undead channel %p !\n",
		       pch);
		return;
	}
	skb_queue_purge(&pch->file.xq);
	skb_queue_purge(&pch->file.rq);
	kfree(pch);
}

static void __exit ppp_cleanup(void)
{
	/* should never happen */
	if (atomic_read(&ppp_unit_count) || atomic_read(&channel_count))
		printk(KERN_ERR "PPP: removing module but units remain!\n");
	cardmap_destroy(&all_ppp_units);
	if (unregister_chrdev(PPP_MAJOR, "ppp") != 0)
		printk(KERN_ERR "PPP: failed to unregister PPP device\n");
	device_destroy(ppp_class, MKDEV(PPP_MAJOR, 0));
	class_destroy(ppp_class);
}

/*
 * Cardmap implementation.
 */
static void *cardmap_get(struct cardmap *map, unsigned int nr)
{
	struct cardmap *p;
	int i;

	for (p = map; p != NULL; ) {
		if ((i = nr >> p->shift) >= CARDMAP_WIDTH)
			return NULL;
		if (!p->shift)
			return p->ptr[i];
		nr &= ~(CARDMAP_MASK << p->shift);
		p = p->ptr[i];
	}
	return NULL;
}

static int cardmap_set(struct cardmap **pmap, unsigned int nr, void *ptr)
{
	struct cardmap *p;
	int i;

	p = *pmap;
	if (p == NULL || (nr >> p->shift) >= CARDMAP_WIDTH) {
		do {
			/* need a new top level */
			struct cardmap *np = kzalloc(sizeof(*np), GFP_KERNEL);
			if (!np)
				goto enomem;
			np->ptr[0] = p;
			if (p != NULL) {
				np->shift = p->shift + CARDMAP_ORDER;
				p->parent = np;
			} else
				np->shift = 0;
			p = np;
		} while ((nr >> p->shift) >= CARDMAP_WIDTH);
		*pmap = p;
	}
	while (p->shift > 0) {
		i = (nr >> p->shift) & CARDMAP_MASK;
		if (p->ptr[i] == NULL) {
			struct cardmap *np = kzalloc(sizeof(*np), GFP_KERNEL);
			if (!np)
				goto enomem;
			np->shift = p->shift - CARDMAP_ORDER;
			np->parent = p;
			p->ptr[i] = np;
		}
		if (ptr == NULL)
			clear_bit(i, &p->inuse);
		p = p->ptr[i];
	}
	i = nr & CARDMAP_MASK;
	p->ptr[i] = ptr;
	if (ptr != NULL)
		set_bit(i, &p->inuse);
	else
		clear_bit(i, &p->inuse);
	return 0;
 enomem:
	return -ENOMEM;
}

static unsigned int cardmap_find_first_free(struct cardmap *map)
{
	struct cardmap *p;
	unsigned int nr = 0;
	int i;

	if ((p = map) == NULL)
		return 0;
	for (;;) {
		i = find_first_zero_bit(&p->inuse, CARDMAP_WIDTH);
		if (i >= CARDMAP_WIDTH) {
			if (p->parent == NULL)
				return CARDMAP_WIDTH << p->shift;
			p = p->parent;
			i = (nr >> p->shift) & CARDMAP_MASK;
			set_bit(i, &p->inuse);
			continue;
		}
		nr = (nr & (~CARDMAP_MASK << p->shift)) | (i << p->shift);
		if (p->shift == 0 || p->ptr[i] == NULL)
			return nr;
		p = p->ptr[i];
	}
}

static void cardmap_destroy(struct cardmap **pmap)
{
	struct cardmap *p, *np;
	int i;

	for (p = *pmap; p != NULL; p = np) {
		if (p->shift) {
			for (i = 0; i < CARDMAP_WIDTH; ++i)
				if (p->ptr[i] != NULL)
					break;
			if (i < CARDMAP_WIDTH) {
				np = p->ptr[i];
				p->ptr[i] = NULL;
				continue;
			}
		}
		np = p->parent;
		kfree(p);
	}
	*pmap = NULL;
}

/* Module/initialization stuff */

module_init(ppp_init);
module_exit(ppp_cleanup);

EXPORT_SYMBOL(ppp_register_channel);
EXPORT_SYMBOL(ppp_unregister_channel);
EXPORT_SYMBOL(ppp_channel_index);
EXPORT_SYMBOL(ppp_unit_number);
EXPORT_SYMBOL(ppp_dev_name);
EXPORT_SYMBOL(ppp_input);
EXPORT_SYMBOL(ppp_input_error);
EXPORT_SYMBOL(ppp_output_wakeup);
EXPORT_SYMBOL(ppp_register_compressor);
EXPORT_SYMBOL(ppp_unregister_compressor);
MODULE_LICENSE("GPL");
MODULE_ALIAS_CHARDEV_MAJOR(PPP_MAJOR);
MODULE_ALIAS("/dev/ppp");
