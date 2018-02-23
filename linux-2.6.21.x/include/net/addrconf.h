#ifndef _ADDRCONF_H
#define _ADDRCONF_H

#define RETRANS_TIMER	HZ

#define MAX_RTR_SOLICITATIONS		3
#define RTR_SOLICITATION_INTERVAL	(4*HZ)

#define MIN_VALID_LIFETIME		(2*3600)	/* 2 hours */

#define TEMP_VALID_LIFETIME		(7*86400)
#define TEMP_PREFERRED_LIFETIME		(86400)
#define REGEN_MAX_RETRY			(3)
#define MAX_DESYNC_FACTOR		(600)

#define ADDR_CHECK_FREQUENCY		(120*HZ)

#define IPV6_MAX_ADDRESSES		16

#include <linux/in.h>
#include <linux/in6.h>

struct prefix_info {
	__u8			type;
	__u8			length;
	__u8			prefix_len;

#if defined(__BIG_ENDIAN_BITFIELD)
	__u8			onlink : 1,
			 	autoconf : 1,
				reserved : 6;
#elif defined(__LITTLE_ENDIAN_BITFIELD)
	__u8			reserved : 6,
				autoconf : 1,
				onlink : 1;
#else
#error "Please fix <asm/byteorder.h>"
#endif
	__be32			valid;
	__be32			prefered;
	__be32			reserved2;

	struct in6_addr		prefix;
};


#ifdef __KERNEL__

#include <linux/netdevice.h>
#include <net/if_inet6.h>
#include <net/ipv6.h>

#define IN6_ADDR_HSIZE_SHIFT	4
#define IN6_ADDR_HSIZE		(1 << IN6_ADDR_HSIZE_SHIFT)

extern int			addrconf_init(void);
extern void			addrconf_cleanup(void);

extern int			addrconf_add_ifaddr(void __user *arg);
extern int			addrconf_del_ifaddr(void __user *arg);
extern int			addrconf_set_dstaddr(void __user *arg);

extern int			ipv6_chk_addr(struct in6_addr *addr,
					      struct net_device *dev,
					      int strict);
#ifdef CONFIG_IPV6_MIP6
extern int			ipv6_chk_home_addr(struct in6_addr *addr);
#endif

extern int			ipv6_chk_prefix(struct in6_addr *addr,
						struct net_device *dev);

extern struct inet6_ifaddr *	ipv6_get_ifaddr(struct in6_addr *addr,
						struct net_device *dev,
						int strict);
extern int			ipv6_get_saddr(struct dst_entry *dst, 
					       struct in6_addr *daddr,
					       struct in6_addr *saddr);
extern int			ipv6_dev_get_saddr(struct net_device *dev, 
					       struct in6_addr *daddr,
					       struct in6_addr *saddr);
extern int			ipv6_get_lladdr(struct net_device *dev,
						struct in6_addr *addr,
						unsigned char banned_flags);
extern int			ipv6_rcv_saddr_equal(const struct sock *sk, 
						      const struct sock *sk2);
extern void			addrconf_join_solict(struct net_device *dev,
					struct in6_addr *addr);
extern void			addrconf_leave_solict(struct inet6_dev *idev,
					struct in6_addr *addr);

/*
 *	multicast prototypes (mcast.c)
 */
extern int ipv6_sock_mc_join(struct sock *sk, int ifindex, 
		  struct in6_addr *addr);
extern int ipv6_sock_mc_drop(struct sock *sk, int ifindex, 
		  struct in6_addr *addr);
extern void ipv6_sock_mc_close(struct sock *sk);
extern int inet6_mc_check(struct sock *sk, struct in6_addr *mc_addr,
		struct in6_addr *src_addr);

extern int ipv6_dev_mc_inc(struct net_device *dev, struct in6_addr *addr);
extern int __ipv6_dev_mc_dec(struct inet6_dev *idev, struct in6_addr *addr);
extern int ipv6_dev_mc_dec(struct net_device *dev, struct in6_addr *addr);
extern void ipv6_mc_up(struct inet6_dev *idev);
extern void ipv6_mc_down(struct inet6_dev *idev);
extern void ipv6_mc_init_dev(struct inet6_dev *idev);
extern void ipv6_mc_destroy_dev(struct inet6_dev *idev);
extern void addrconf_dad_failure(struct inet6_ifaddr *ifp);

extern int ipv6_chk_mcast_addr(struct net_device *dev, struct in6_addr *group,
		struct in6_addr *src_addr);
extern int ipv6_is_mld(struct sk_buff *skb, int nexthdr);

extern void addrconf_prefix_rcv(struct net_device *dev,
				u8 *opt, int len, bool sllao);

/*
 *	anycast prototypes (anycast.c)
 */
extern int ipv6_sock_ac_join(struct sock *sk,int ifindex,struct in6_addr *addr);
extern int ipv6_sock_ac_drop(struct sock *sk,int ifindex,struct in6_addr *addr);
extern void ipv6_sock_ac_close(struct sock *sk);
extern int inet6_ac_check(struct sock *sk, struct in6_addr *addr, int ifindex);

extern int ipv6_dev_ac_inc(struct net_device *dev, struct in6_addr *addr);
extern int __ipv6_dev_ac_dec(struct inet6_dev *idev, struct in6_addr *addr);
extern int ipv6_chk_acast_addr(struct net_device *dev, struct in6_addr *addr);


/* Device notifier */
extern int register_inet6addr_notifier(struct notifier_block *nb);
extern int unregister_inet6addr_notifier(struct notifier_block *nb);

static inline struct inet6_dev *
__in6_dev_get(struct net_device *dev)
{
	return rcu_dereference(dev->ip6_ptr);
}

static inline struct inet6_dev *
in6_dev_get(struct net_device *dev)
{
	struct inet6_dev *idev = NULL;
	rcu_read_lock();
	idev = __in6_dev_get(dev);
	if (idev)
		atomic_inc(&idev->refcnt);
	rcu_read_unlock();
	return idev;
}

extern void in6_dev_finish_destroy(struct inet6_dev *idev);

static inline void
in6_dev_put(struct inet6_dev *idev)
{
	if (atomic_dec_and_test(&idev->refcnt))
		in6_dev_finish_destroy(idev);
}

#define __in6_dev_put(idev)  atomic_dec(&(idev)->refcnt)
#define in6_dev_hold(idev)   atomic_inc(&(idev)->refcnt)


extern void inet6_ifa_finish_destroy(struct inet6_ifaddr *ifp);

static inline void in6_ifa_put(struct inet6_ifaddr *ifp)
{
	if (atomic_dec_and_test(&ifp->refcnt))
		inet6_ifa_finish_destroy(ifp);
}

#define __in6_ifa_put(ifp)	atomic_dec(&(ifp)->refcnt)
#define in6_ifa_hold(ifp)	atomic_inc(&(ifp)->refcnt)


extern void			addrconf_forwarding_on(void);

/*
 *	compute link-local solicited-node multicast address
 */

static inline void addrconf_addr_solict_mult(const struct in6_addr *addr,
					     struct in6_addr *solicited)
{
	ipv6_addr_set(solicited,
		      __constant_htonl(0xFF020000), 0,
		      __constant_htonl(0x1),
		      __constant_htonl(0xFF000000) | addr->s6_addr32[3]);
}

static inline bool ipv6_addr_is_multicast(const struct in6_addr *addr)
{
	return (addr->s6_addr32[0] & __constant_htonl(0xFF000000)) == __constant_htonl(0xFF000000);
}

static inline bool ipv6_addr_is_ll_all_nodes(const struct in6_addr *addr)
{
	return (((addr->s6_addr32[0] ^ htonl(0xff020000)) |
		addr->s6_addr32[1] | addr->s6_addr32[2] |
		(addr->s6_addr32[3] ^ htonl(0x00000001))) == 0);
}

static inline bool ipv6_addr_is_ll_all_routers(const struct in6_addr *addr)
{
	return (((addr->s6_addr32[0] ^ htonl(0xff020000)) |
		addr->s6_addr32[1] | addr->s6_addr32[2] |
		(addr->s6_addr32[3] ^ htonl(0x00000002))) == 0);
}

static inline bool ipv6_addr_is_isatap(const struct in6_addr *addr)
{
	return ((addr->s6_addr32[2] | htonl(0x02000000)) == htonl(0x02005EFE));
}

static inline int ipv6_isatap_eui64(u8 *eui, __be32 addr)
{
	eui[0] = (ipv4_is_zeronet(addr) || ipv4_is_private_10(addr) ||
		  ipv4_is_loopback(addr) || ipv4_is_linklocal_169(addr) ||
		  ipv4_is_private_172(addr) || ipv4_is_test_192(addr) ||
		  ipv4_is_anycast_6to4(addr) || ipv4_is_private_192(addr) ||
		  ipv4_is_test_198(addr) || ipv4_is_multicast(addr) ||
		  ipv4_is_lbcast(addr)) ? 0x00 : 0x02;
	eui[1] = 0;
	eui[2] = 0x5E;
	eui[3] = 0xFE;
	memcpy (eui+4, &addr, 4);
	return 0;
}

static inline u32 ipv6_addr_hash(const struct in6_addr *addr)
{
	/*
	 * We perform the hash function over the last 64 bits of the address
         * This will include the IEEE address token on links that support it.
	 */

	return HASH_2WORDS(addr->s6_addr32[2],  addr->s6_addr32[3], 0)
		& (IN6_ADDR_HSIZE - 1);
}

static inline bool ipv6_addr_is_solict_mult(const struct in6_addr *addr)
{
	return (addr->s6_addr32[0] == htonl(0xff020000) &&
		addr->s6_addr32[1] == htonl(0x00000000) &&
		addr->s6_addr32[2] == htonl(0x00000001) &&
		addr->s6_addr[12] == 0xff);
}

#ifdef CONFIG_PROC_FS
extern int if6_proc_init(void);
extern void if6_proc_exit(void);
#endif

#endif
#endif