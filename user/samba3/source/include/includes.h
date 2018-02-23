#ifndef _INCLUDES_H
#define _INCLUDES_H
/* 
   Unix SMB/CIFS implementation.
   Machine customisation and include handling
   Copyright (C) Andrew Tridgell 1994-1998
   Copyright (C) 2002 by Martin Pool <mbp@samba.org>
   
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3 of the License, or
   (at your option) any later version.
   
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
   
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "lib/replace/replace.h"

/* replace some options for LFS support. */
#include "config-lfs.h"

/* make sure we have included the correct config.h */
#ifndef NO_CONFIG_H /* for some tests */
#ifndef CONFIG_H_IS_FROM_SAMBA
#error "make sure you have removed all config.h files from standalone builds!"
#error "the included config.h isn't from samba!"
#endif
#endif /* NO_CONFIG_H */

/* only do the C++ reserved word check when we compile
   to include --with-developer since too many systems
   still have comflicts with their header files (e.g. IRIX 6.4) */

#if !defined(__cplusplus) && defined(DEVELOPER)
#define class #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define private #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define public #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define protected #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define template #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define this #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define new #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define delete #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#define friend #error DONT_USE_CPLUSPLUS_RESERVED_NAMES
#endif

#include "local.h"

#ifdef AIX
#define DEFAULT_PRINTING PRINT_AIX
#define PRINTCAP_NAME "/etc/qconfig"
#endif

#ifdef HPUX
#define DEFAULT_PRINTING PRINT_HPUX
#endif

#ifdef QNX
#define DEFAULT_PRINTING PRINT_QNX
#endif

#ifdef SUNOS4
/* on SUNOS4 termios.h conflicts with sys/ioctl.h */
#undef HAVE_TERMIOS_H
#endif

#ifndef _PUBLIC_
#ifdef HAVE_VISIBILITY_ATTR
#  define _PUBLIC_ __attribute__((visibility("default")))
#else
#  define _PUBLIC_
#endif
#endif

#if defined(__GNUC__) && !defined(__cplusplus)
/** gcc attribute used on function parameters so that it does not emit
 * warnings about them being unused. **/
#  define UNUSED(param) param __attribute__ ((unused))
#else
#  define UNUSED(param) param
/** Feel free to add definitions for other compilers here. */
#endif

#ifdef RELIANTUNIX
/*
 * <unistd.h> has to be included before any other to get
 * large file support on Reliant UNIX. Yes, it's broken :-).
 */
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif
#endif /* RELIANTUNIX */

#include "system/capability.h"
#include "system/dir.h"
#include "system/filesys.h"
#include "system/glob.h"
#include "system/iconv.h"
#include "system/locale.h"
#include "system/network.h"
#include "system/passwd.h"
#include "system/readline.h"
#include "system/select.h"
#include "system/shmem.h"
#include "system/syslog.h"
#include "system/terminal.h"
#include "system/time.h"
#include "system/wait.h"

#if defined(HAVE_RPC_RPC_H)
/*
 * Check for AUTH_ERROR define conflict with rpc/rpc.h in prot.h.
 */
#if defined(HAVE_SYS_SECURITY_H) && defined(HAVE_RPC_AUTH_ERROR_CONFLICT)
#undef AUTH_ERROR
#endif
/*
 * HP-UX 11.X has TCP_NODELAY and TCP_MAXSEG defined in <netinet/tcp.h> which
 * was included above.  However <rpc/rpc.h> includes <sys/xti.h> which defines
 * them again without checking if they already exsist.  This generates
 * two "Redefinition of macro" warnings for every single .c file that is
 * compiled.
 */
#if defined(HPUX) && defined(TCP_NODELAY)
#undef TCP_NODELAY
#endif
#if defined(HPUX) && defined(TCP_MAXSEG)
#undef TCP_MAXSEG
#endif
#include <rpc/rpc.h>
#endif

#if defined(HAVE_YP_GET_DEFAULT_DOMAIN) && defined(HAVE_SETNETGRENT) && defined(HAVE_ENDNETGRENT) && defined(HAVE_GETNETGRENT)
#define HAVE_NETGROUP 1
#endif

#if defined (HAVE_NETGROUP)
#if defined(HAVE_RPCSVC_YP_PROT_H)
/*
 * HP-UX 11.X has TCP_NODELAY and TCP_MAXSEG defined in <netinet/tcp.h> which
 * was included above.  However <rpc/rpc.h> includes <sys/xti.h> which defines
 * them again without checking if they already exsist.  This generates
 * two "Redefinition of macro" warnings for every single .c file that is
 * compiled.
 */
#if defined(HPUX) && defined(TCP_NODELAY)
#undef TCP_NODELAY
#endif
#if defined(HPUX) && defined(TCP_MAXSEG)
#undef TCP_MAXSEG
#endif
#include <rpcsvc/yp_prot.h>
#endif
#if defined(HAVE_RPCSVC_YPCLNT_H)
#include <rpcsvc/ypclnt.h>
#endif
#endif /* HAVE_NETGROUP */

#ifndef HAVE_KRB5_H
#undef HAVE_KRB5
#endif

#if HAVE_LBER_H
#include <lber.h>
#if defined(HPUX) && !defined(_LBER_TYPES_H)
/* Define ber_tag_t and ber_int_t for using
 * HP LDAP-UX Integration products' LDAP libraries.
*/
#ifndef ber_tag_t
typedef unsigned long ber_tag_t;
typedef int ber_int_t;
#endif
#endif /* defined(HPUX) && !defined(_LBER_TYPES_H) */
#ifndef LBER_USE_DER
#define LBER_USE_DER 0x01
#endif
#endif

#if HAVE_LDAP_H
#include <ldap.h>
#ifndef LDAP_CONST
#define LDAP_CONST const
#endif
#ifndef LDAP_OPT_SUCCESS
#define LDAP_OPT_SUCCESS 0
#endif
/* Solaris 8 and maybe other LDAP implementations spell this "..._INPROGRESS": */
#if defined(LDAP_SASL_BIND_INPROGRESS) && !defined(LDAP_SASL_BIND_IN_PROGRESS)
#define LDAP_SASL_BIND_IN_PROGRESS LDAP_SASL_BIND_INPROGRESS
#endif
/* Solaris 8 defines SSL_LDAP_PORT, not LDAPS_PORT and it only does so if
   LDAP_SSL is defined - but SSL is not working. We just want the
   port number! Let's just define LDAPS_PORT correct. */
#if !defined(LDAPS_PORT)
#define LDAPS_PORT 636
#endif
#else
#undef HAVE_LDAP
#endif

#if HAVE_GSSAPI_GSSAPI_H
#include <gssapi/gssapi.h>
#elif HAVE_GSSAPI_GSSAPI_GENERIC_H
#include <gssapi/gssapi_generic.h>
#elif HAVE_GSSAPI_H
#include <gssapi.h>
#endif

#if HAVE_COM_ERR_H
#include <com_err.h>
#endif

#if HAVE_SYS_ATTRIBUTES_H
#include <sys/attributes.h>
#endif

#ifndef ENOATTR
#define ENOATTR ENODATA
#endif

/* mutually exclusive (SuSE 8.2) */
#if HAVE_ATTR_XATTR_H
#include <attr/xattr.h>
#elif HAVE_SYS_XATTR_H
#include <sys/xattr.h>
#endif

#ifdef HAVE_SYS_EA_H
#include <sys/ea.h>
#endif

#ifdef HAVE_SYS_EXTATTR_H
#include <sys/extattr.h>
#endif

#ifdef HAVE_SYS_UIO_H
#include <sys/uio.h>
#endif

#if HAVE_LANGINFO_H
#include <langinfo.h>
#endif

#if HAVE_NETGROUP_H
#include <netgroup.h>
#endif

#if defined(HAVE_AIO_H) && defined(WITH_AIO)
#include <aio.h>
#endif

/* skip valgrind headers on 64bit AMD boxes */
#ifndef HAVE_64BIT_LINUX
/* Special macros that are no-ops except when run under Valgrind on
 * x86.  They've moved a little bit from valgrind 1.0.4 to 1.9.4 */
#if HAVE_VALGRIND_MEMCHECK_H
        /* memcheck.h includes valgrind.h */
#include <valgrind/memcheck.h>
#elif HAVE_VALGRIND_H
#include <valgrind.h>
#endif
#endif

/* If we have --enable-developer and the valgrind header is present,
 * then we're OK to use it.  Set a macro so this logic can be done only
 * once. */
#if defined(DEVELOPER) && !defined(HAVE_64BIT_LINUX)
#if (HAVE_VALGRIND_H || HAVE_VALGRIND_VALGRIND_H)
#define VALGRIND
#endif
#endif


/* we support ADS if we want it and have krb5 and ldap libs */
#if defined(WITH_ADS) && defined(HAVE_KRB5) && defined(HAVE_LDAP)
#define HAVE_ADS
#endif

/*
 * Define VOLATILE if needed.
 */

#if defined(HAVE_VOLATILE)
#define VOLATILE volatile
#else
#define VOLATILE
#endif

/*
 * Define additional missing types
 */
#if defined(HAVE_SIG_ATOMIC_T_TYPE) && defined(AIX)
typedef sig_atomic_t SIG_ATOMIC_T;
#elif defined(HAVE_SIG_ATOMIC_T_TYPE) && !defined(AIX)
typedef sig_atomic_t VOLATILE SIG_ATOMIC_T;
#else
typedef int VOLATILE SIG_ATOMIC_T;
#endif

#ifndef uchar
#define uchar unsigned char
#endif

#ifdef HAVE_UNSIGNED_CHAR
#define schar signed char
#else
#define schar char
#endif

/*
   Samba needs type definitions for int16, int32, uint16 and uint32.

   Normally these are signed and unsigned 16 and 32 bit integers, but
   they actually only need to be at least 16 and 32 bits
   respectively. Thus if your word size is 8 bytes just defining them
   as signed and unsigned int will work.
*/

#ifndef uint8
#define uint8 unsigned char
#endif

#if !defined(int16) && !defined(HAVE_INT16_FROM_RPC_RPC_H)
#  if (SIZEOF_SHORT == 4)
#    define int16 __ERROR___CANNOT_DETERMINE_TYPE_FOR_INT16;
#  else /* SIZEOF_SHORT != 4 */
#    define int16 short
#  endif /* SIZEOF_SHORT != 4 */
   /* needed to work around compile issue on HP-UX 11.x */
#  define _INT16	1
#endif

/*
 * Note we duplicate the size tests in the unsigned 
 * case as int16 may be a typedef from rpc/rpc.h
 */

#if !defined(uint16) && !defined(HAVE_UINT16_FROM_RPC_RPC_H)
#if (SIZEOF_SHORT == 4)
#define uint16 __ERROR___CANNOT_DETERMINE_TYPE_FOR_INT16;
#else /* SIZEOF_SHORT != 4 */
#define uint16 unsigned short
#endif /* SIZEOF_SHORT != 4 */
#endif

#if !defined(int32) && !defined(HAVE_INT32_FROM_RPC_RPC_H)
#  if (SIZEOF_INT == 4)
#    define int32 int
#  elif (SIZEOF_LONG == 4)
#    define int32 long
#  elif (SIZEOF_SHORT == 4)
#    define int32 short
#  else
     /* uggh - no 32 bit type?? probably a CRAY. just hope this works ... */
#    define int32 int
#  endif
   /* needed to work around compile issue on HP-UX 11.x */
#  define _INT32	1
#endif

/*
 * Note we duplicate the size tests in the unsigned 
 * case as int32 may be a typedef from rpc/rpc.h
 */

#if !defined(uint32) && !defined(HAVE_UINT32_FROM_RPC_RPC_H)
#if (SIZEOF_INT == 4)
#define uint32 unsigned int
#elif (SIZEOF_LONG == 4)
#define uint32 unsigned long
#elif (SIZEOF_SHORT == 4)
#define uint32 unsigned short
#else
/* uggh - no 32 bit type?? probably a CRAY. just hope this works ... */
#define uint32 unsigned
#endif
#endif

/*
 * check for 8 byte long long
 */

#if !defined(uint64)
#if (SIZEOF_LONG == 8)
#define uint64 unsigned long
#elif (SIZEOF_LONG_LONG == 8)
#define uint64 unsigned long long
#endif	/* don't lie.  If we don't have it, then don't use it */
#endif

#if !defined(int64)
#if (SIZEOF_LONG == 8)
#define int64 long
#elif (SIZEOF_LONG_LONG == 8)
#define int64 long long
#endif	/* don't lie.  If we don't have it, then don't use it */
#endif


/*
 * Types for devices, inodes and offsets.
 */

#ifndef SMB_DEV_T
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_DEV64_T)
#    define SMB_DEV_T dev64_t
#  else
#    define SMB_DEV_T dev_t
#  endif
#endif

#ifndef LARGE_SMB_DEV_T
#  if (defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_DEV64_T)) || (defined(SIZEOF_DEV_T) && (SIZEOF_DEV_T == 8))
#    define LARGE_SMB_DEV_T 1
#  endif
#endif

#ifdef LARGE_SMB_DEV_T
#define SDEV_T_VAL(p, ofs, v) (SIVAL((p),(ofs),(v)&0xFFFFFFFF), SIVAL((p),(ofs)+4,(v)>>32))
#define DEV_T_VAL(p, ofs) ((SMB_DEV_T)(((SMB_BIG_UINT)(IVAL((p),(ofs))))| (((SMB_BIG_UINT)(IVAL((p),(ofs)+4))) << 32)))
#else 
#define SDEV_T_VAL(p, ofs, v) (SIVAL((p),(ofs),v),SIVAL((p),(ofs)+4,0))
#define DEV_T_VAL(p, ofs) ((SMB_DEV_T)(IVAL((p),(ofs))))
#endif

/*
 * Setup the correctly sized inode type.
 */

#ifndef SMB_INO_T
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_INO64_T)
#    define SMB_INO_T ino64_t
#  else
#    define SMB_INO_T ino_t
#  endif
#endif

#ifndef LARGE_SMB_INO_T
#  if (defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_INO64_T)) || (defined(SIZEOF_INO_T) && (SIZEOF_INO_T == 8))
#    define LARGE_SMB_INO_T 1
#  endif
#endif

#ifdef LARGE_SMB_INO_T
#define SINO_T_VAL(p, ofs, v) (SIVAL((p),(ofs),(v)&0xFFFFFFFF), SIVAL((p),(ofs)+4,(v)>>32))
#define INO_T_VAL(p, ofs) ((SMB_INO_T)(((SMB_BIG_UINT)(IVAL(p,ofs)))| (((SMB_BIG_UINT)(IVAL(p,(ofs)+4))) << 32)))
#else 
#define SINO_T_VAL(p, ofs, v) (SIVAL(p,ofs,v),SIVAL(p,(ofs)+4,0))
#define INO_T_VAL(p, ofs) ((SMB_INO_T)(IVAL((p),(ofs))))
#endif

#ifndef SMB_OFF_T
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_OFF64_T)
#    define SMB_OFF_T off64_t
#  else
#    define SMB_OFF_T off_t
#  endif
#endif

#if defined(HAVE_LONGLONG)
#define SMB_BIG_UINT unsigned long long
#define SMB_BIG_INT long long
#define SBIG_UINT(p, ofs, v) (SIVAL(p,ofs,(v)&0xFFFFFFFF), SIVAL(p,(ofs)+4,(v)>>32))
#define BIG_UINT(p, ofs) ((((SMB_BIG_UINT) IVAL(p,(ofs)+4))<<32)|IVAL(p,ofs))
#else
#define SMB_BIG_UINT unsigned long
#define SMB_BIG_INT long
#define SBIG_UINT(p, ofs, v) (SIVAL(p,ofs,v),SIVAL(p,(ofs)+4,0))
#define BIG_UINT(p, ofs) (IVAL(p,ofs))
#endif

#define SMB_BIG_UINT_BITS (sizeof(SMB_BIG_UINT)*8)

/* this should really be a 64 bit type if possible */
#define br_off SMB_BIG_UINT

#define SMB_OFF_T_BITS (sizeof(SMB_OFF_T)*8)

/*
 * Set the define that tells us if we can do 64 bit
 * NT SMB calls.
 */

#ifndef LARGE_SMB_OFF_T
#  if (defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_OFF64_T)) || (defined(SIZEOF_OFF_T) && (SIZEOF_OFF_T == 8))
#    define LARGE_SMB_OFF_T 1
#  endif
#endif

#ifdef LARGE_SMB_OFF_T
#define SOFF_T(p, ofs, v) (SIVAL(p,ofs,(v)&0xFFFFFFFF), SIVAL(p,(ofs)+4,(v)>>32))
#define SOFF_T_R(p, ofs, v) (SIVAL(p,(ofs)+4,(v)&0xFFFFFFFF), SIVAL(p,ofs,(v)>>32))
#define IVAL_TO_SMB_OFF_T(buf,off) ((SMB_OFF_T)(( ((SMB_BIG_UINT)(IVAL((buf),(off)))) & ((SMB_BIG_UINT)0xFFFFFFFF) )))
#define IVAL2_TO_SMB_BIG_UINT(buf,off) ( (((SMB_BIG_UINT)(IVAL((buf),(off)))) & ((SMB_BIG_UINT)0xFFFFFFFF)) | \
		(( ((SMB_BIG_UINT)(IVAL((buf),(off+4)))) & ((SMB_BIG_UINT)0xFFFFFFFF) ) << 32 ) )
#else 
#define SOFF_T(p, ofs, v) (SIVAL(p,ofs,v),SIVAL(p,(ofs)+4,0))
#define SOFF_T_R(p, ofs, v) (SIVAL(p,(ofs)+4,v),SIVAL(p,ofs,0))
#define IVAL_TO_SMB_OFF_T(buf,off) ((SMB_OFF_T)(( ((uint32)(IVAL((buf),(off)))) & 0xFFFFFFFF )))
#define IVAL2_TO_SMB_BIG_UINT(buf,off) ( (((SMB_BIG_UINT)(IVAL((buf),(off)))) & ((SMB_BIG_UINT)0xFFFFFFFF)) | \
		                (( ((SMB_BIG_UINT)(IVAL((buf),(off+4)))) & ((SMB_BIG_UINT)0xFFFFFFFF) ) << 32 ) )
#endif

/*
 * Type for stat structure.
 */

#ifndef SMB_STRUCT_STAT
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STAT64) && defined(HAVE_OFF64_T)
#    define SMB_STRUCT_STAT struct stat64
#  else
#    define SMB_STRUCT_STAT struct stat
#  endif
#endif

/*
 * Type for dirent structure.
 */

#ifndef SMB_STRUCT_DIRENT
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STRUCT_DIRENT64)
#    define SMB_STRUCT_DIRENT struct dirent64
#  else
#    define SMB_STRUCT_DIRENT struct dirent
#  endif
#endif

/*
 * Type for DIR structure.
 */

#ifndef SMB_STRUCT_DIR
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STRUCT_DIR64)
#    define SMB_STRUCT_DIR DIR64
#  else
#    define SMB_STRUCT_DIR DIR
#  endif
#endif

/*
 * Defines for 64 bit fcntl locks.
 */

#ifndef SMB_STRUCT_FLOCK
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STRUCT_FLOCK64) && defined(HAVE_OFF64_T)
#    define SMB_STRUCT_FLOCK struct flock64
#  else
#    define SMB_STRUCT_FLOCK struct flock
#  endif
#endif

#ifndef SMB_F_SETLKW
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STRUCT_FLOCK64) && defined(HAVE_OFF64_T)
#    define SMB_F_SETLKW F_SETLKW64
#  else
#    define SMB_F_SETLKW F_SETLKW
#  endif
#endif

#ifndef SMB_F_SETLK
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STRUCT_FLOCK64) && defined(HAVE_OFF64_T)
#    define SMB_F_SETLK F_SETLK64
#  else
#    define SMB_F_SETLK F_SETLK
#  endif
#endif

#ifndef SMB_F_GETLK
#  if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_STRUCT_FLOCK64) && defined(HAVE_OFF64_T)
#    define SMB_F_GETLK F_GETLK64
#  else
#    define SMB_F_GETLK F_GETLK
#  endif
#endif

/*
 * Type for aiocb structure.
 */

#ifndef SMB_STRUCT_AIOCB
#  if defined(WITH_AIO)
#    if defined(HAVE_EXPLICIT_LARGEFILE_SUPPORT) && defined(HAVE_AIOCB64)
#      define SMB_STRUCT_AIOCB struct aiocb64
#    else
#      define SMB_STRUCT_AIOCB struct aiocb
#    endif
#  else
#    define SMB_STRUCT_AIOCB int /* AIO not being used but we still need the define.... */
#  endif
#endif

#ifndef HAVE_STRUCT_TIMESPEC
struct timespec {
	time_t tv_sec;            /* Seconds.  */
	long tv_nsec;           /* Nanoseconds.  */
};
#endif

#ifndef MIN
#define MIN(a,b) ((a)<(b)?(a):(b))
#endif

#ifndef MAX
#define MAX(a,b) ((a)>(b)?(a):(b))
#endif

#ifdef HAVE_BROKEN_GETGROUPS
#define GID_T int
#else
#define GID_T gid_t
#endif

#ifndef NGROUPS_MAX
#define NGROUPS_MAX 32 /* Guess... */
#endif

/* Our own fstrings */

/*
                  --------------
                 /              \
                /      REST      \
               /        IN        \
              /       PEACE        \
             /                      \
             | The infamous pstring |
             |                      |
             |                      |
             |      7 December      |
             |                      |
             |         2007         |
            *|     *  *  *          | *
   _________)/\\_//(\/(/\)/\//\/\///|_)_______
*/

#ifndef FSTRING_LEN
#define FSTRING_LEN 256
typedef char fstring[FSTRING_LEN];
#endif

/* Lists, trees, caching, database... */
#include "xfile.h"
#include "intl.h"
#include "dlinklist.h"
#include "tdb.h"
#include "util_tdb.h"

#include "lib/talloc/talloc.h"
/* And a little extension. Abort on type mismatch */
#define talloc_get_type_abort(ptr, type) \
	(type *)talloc_check_name_abort(ptr, #type)

#include "event.h"
#include "nt_status.h"
#include "ads.h"
#include "ads_dns.h"
#include "interfaces.h"
#include "trans2.h"
#include "nterr.h"
#include "ntioctl.h"
#include "charset.h"
#include "dynconfig.h"
#include "util_getent.h"
#include "debugparse.h"
#include "version.h"
#include "privileges.h"
#include "messages.h"
#include "locking.h"
#include "smb.h"
#include "nameserv.h"
#include "secrets.h"
#include "byteorder.h"
#include "privileges.h"
#include "rpc_misc.h"
#include "rpc_dce.h"
#include "mapping.h"
#include "passdb.h"
#include "rpc_secdes.h"
#include "gpo.h"
#include "msdfs.h"
#include "rap.h"
#include "md5.h"
#include "hmacmd5.h"
#include "ntlmssp.h"
#include "auth.h"
#include "ntdomain.h"
#include "rpc_svcctl.h"
#include "rpc_ntsvcs.h"
#include "rpc_lsa.h"
#include "reg_objects.h"
#include "reg_db.h"
#include "rpc_spoolss.h"
#include "rpc_eventlog.h"
#include "rpc_perfcount.h"
#include "rpc_perfcount_defs.h"
#include "librpc/gen_ndr/notify.h"
#include "librpc/gen_ndr/xattr.h"
#include "librpc/gen_ndr/ndr_nbt.h"
#include "librpc/gen_ndr/messaging.h"
#include "librpc/rpc/dcerpc.h"
#include "nt_printing.h"
#include "idmap.h"
#include "client.h"

#include "session.h"
#include "asn_1.h"
#include "popt.h"
#include "mangle.h"
#include "module.h"
#include "nsswitch/winbind_client.h"
#include "spnego.h"
#include "rpc_client.h"
#include "dbwrap.h"
#include "packet.h"
#include "ctdbd_conn.h"
#include "talloc_stack.h"
#include "memcache.h"
#include "async_req.h"
#include "async_smb.h"
#include "async_sock.h"

#include "lib/smbconf/smbconf.h"
#include "lib/smbconf/smbconf_init.h"
#include "lib/smbconf/smbconf_reg.h"
#include "lib/smbconf/smbconf_txt.h"

/* Defines for wisXXX functions. */
#define UNI_UPPER    0x1
#define UNI_LOWER    0x2
#define UNI_DIGIT    0x4
#define UNI_XDIGIT   0x8
#define UNI_SPACE    0x10

#include "nsswitch/winbind_nss.h"

/* forward declaration from printing.h to get around 
   header file dependencies */

struct printjob;

/* forward declarations from smbldap.c */

#include "smbldap.h"

#include "smb_ldap.h"

struct dns_reg_state;

void dns_register_smbd(struct dns_reg_state ** dns_state_ptr,
		unsigned port,
		int *maxfd,
		fd_set *listen_set,
		struct timeval *timeout);

void dns_register_close(struct dns_reg_state ** dns_state_ptr);


bool dns_register_smbd_reply(struct dns_reg_state *dns_state,
		fd_set *lfds, struct timeval *timeout);

/*
 * Reasons for cache flush.
 */

enum flush_reason_enum {
    SEEK_FLUSH,
    READ_FLUSH,
    WRITE_FLUSH,
    READRAW_FLUSH,
    OPLOCK_RELEASE_FLUSH,
    CLOSE_FLUSH,
    SYNC_FLUSH,
    SIZECHANGE_FLUSH,
    /* NUM_FLUSH_REASONS must remain the last value in the enumeration. */
    NUM_FLUSH_REASONS};

#include "nss_info.h"
#include "modules/nfs4_acls.h"
#include "nsswitch/libwbclient/wbclient.h"

/* generated rpc server implementation functions */
#include "librpc/gen_ndr/srv_echo.h"
#include "librpc/gen_ndr/srv_svcctl.h"
#include "librpc/gen_ndr/srv_lsa.h"
#include "librpc/gen_ndr/srv_eventlog.h"
#include "librpc/gen_ndr/srv_winreg.h"
#include "librpc/gen_ndr/srv_initshutdown.h"
#include "librpc/gen_ndr/srv_netlogon.h"
#include "librpc/gen_ndr/srv_samr.h"
#include "librpc/gen_ndr/srv_wkssvc.h"
#include "librpc/gen_ndr/srv_srvsvc.h"
#include "librpc/gen_ndr/srv_ntsvcs.h"
#include "librpc/gen_ndr/srv_dssetup.h"
#include "librpc/gen_ndr/srv_dfs.h"

/***** automatically generated prototypes *****/
#ifndef NO_PROTO_H
#include "proto.h"
#endif

#if defined(HAVE_POSIX_ACLS)
#include "modules/vfs_posixacl.h"
#endif

#if defined(HAVE_TRU64_ACLS)
#include "modules/vfs_tru64acl.h"
#endif

#if defined(HAVE_SOLARIS_ACLS) || defined(HAVE_UNIXWARE_ACLS)
#include "modules/vfs_solarisacl.h"
#endif

#if defined(HAVE_HPUX_ACLS)
#include "modules/vfs_hpuxacl.h"
#endif

#if defined(HAVE_IRIX_ACLS)
#include "modules/vfs_irixacl.h"
#endif

#ifdef HAVE_LDAP
#include "ads_protos.h"
#endif

/* We need this after proto.h to reference GetTimeOfDay(). */
#include "smbprofile.h"

/* String routines */

#include "srvstr.h"
#include "safe_string.h"

/* prototypes from lib/util_transfer_file.c */
#include "transfer_file.h"

#ifdef __COMPAR_FN_T
#define QSORT_CAST (__compar_fn_t)
#endif

#ifndef QSORT_CAST
#define QSORT_CAST (int (*)(const void *, const void *))
#endif

#ifndef DEFAULT_PRINTING
#ifdef HAVE_CUPS
#define DEFAULT_PRINTING PRINT_CUPS
#define PRINTCAP_NAME "cups"
#elif defined(SYSV)
#define DEFAULT_PRINTING PRINT_SYSV
#define PRINTCAP_NAME "lpstat"
#else
#define DEFAULT_PRINTING PRINT_BSD
#define PRINTCAP_NAME "/etc/printcap"
#endif
#endif

#ifndef PRINTCAP_NAME
#define PRINTCAP_NAME "/etc/printcap"
#endif

#ifndef SIGCLD
#define SIGCLD SIGCHLD
#endif

#ifndef SIGRTMIN
#define SIGRTMIN 32
#endif

#ifndef MAP_FILE
#define MAP_FILE 0
#endif

#if defined(HAVE_PUTPRPWNAM) && defined(AUTH_CLEARTEXT_SEG_CHARS)
#define OSF1_ENH_SEC 1
#endif

#ifndef ALLOW_CHANGE_PASSWORD
#if (defined(HAVE_TERMIOS_H) && defined(HAVE_DUP2) && defined(HAVE_SETSID))
#define ALLOW_CHANGE_PASSWORD 1
#endif
#endif

/* what is the longest significant password available on your system? 
 Knowing this speeds up password searches a lot */
#ifndef PASSWORD_LENGTH
#define PASSWORD_LENGTH 8
#endif

#ifndef HAVE_PIPE
#define SYNC_DNS 1
#endif

#ifndef SEEK_SET
#define SEEK_SET 0
#endif

#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK 0x7f000001
#endif

#ifndef INADDR_NONE
#define INADDR_NONE 0xffffffff
#endif

#ifndef HAVE_CRYPT
#define crypt ufc_crypt
#endif

#ifndef O_ACCMODE
#define O_ACCMODE (O_RDONLY | O_WRONLY | O_RDWR)
#endif

#if defined(HAVE_CRYPT16) && defined(HAVE_GETAUTHUID)
#define ULTRIX_AUTH 1
#endif

#if (defined(USE_SETRESUID) && !defined(HAVE_SETRESUID_DECL))
/* stupid glibc */
int setresuid(uid_t ruid, uid_t euid, uid_t suid);
#endif
#if (defined(USE_SETRESUID) && !defined(HAVE_SETRESGID_DECL))
int setresgid(gid_t rgid, gid_t egid, gid_t sgid);
#endif

/*
 * Some older systems seem not to have MAXHOSTNAMELEN
 * defined.
 */
#ifndef MAXHOSTNAMELEN
#define MAXHOSTNAMELEN 255
#endif

/* yuck, I'd like a better way of doing this */
#define DIRP_SIZE (256 + 32)

/*
 * glibc on linux doesn't seem to have MSG_WAITALL
 * defined. I think the kernel has it though..
 */

#ifndef MSG_WAITALL
#define MSG_WAITALL 0
#endif

/* default socket options. Dave Miller thinks we should default to TCP_NODELAY
   given the socket IO pattern that Samba uses */
#ifdef TCP_NODELAY
#define DEFAULT_SOCKET_OPTIONS "TCP_NODELAY"
#else
#define DEFAULT_SOCKET_OPTIONS ""
#endif

/* dmalloc -- free heap debugger (dmalloc.org).  This should be near
 * the *bottom* of include files so as not to conflict. */
#ifdef ENABLE_DMALLOC
#  include <dmalloc.h>
#endif


/* Some POSIX definitions for those without */
 
#ifndef S_IFDIR
#define S_IFDIR         0x4000
#endif
#ifndef S_ISDIR
#define S_ISDIR(mode)   ((mode & 0xF000) == S_IFDIR)
#endif
#ifndef S_IRWXU
#define S_IRWXU 00700           /* read, write, execute: owner */
#endif
#ifndef S_IRUSR
#define S_IRUSR 00400           /* read permission: owner */
#endif
#ifndef S_IWUSR
#define S_IWUSR 00200           /* write permission: owner */
#endif
#ifndef S_IXUSR
#define S_IXUSR 00100           /* execute permission: owner */
#endif
#ifndef S_IRWXG
#define S_IRWXG 00070           /* read, write, execute: group */
#endif
#ifndef S_IRGRP
#define S_IRGRP 00040           /* read permission: group */
#endif
#ifndef S_IWGRP
#define S_IWGRP 00020           /* write permission: group */
#endif
#ifndef S_IXGRP
#define S_IXGRP 00010           /* execute permission: group */
#endif
#ifndef S_IRWXO
#define S_IRWXO 00007           /* read, write, execute: other */
#endif
#ifndef S_IROTH
#define S_IROTH 00004           /* read permission: other */
#endif
#ifndef S_IWOTH
#define S_IWOTH 00002           /* write permission: other */
#endif
#ifndef S_IXOTH
#define S_IXOTH 00001           /* execute permission: other */
#endif

/* For sys_adminlog(). */
#ifndef LOG_EMERG
#define LOG_EMERG       0       /* system is unusable */
#endif

#ifndef LOG_ALERT
#define LOG_ALERT       1       /* action must be taken immediately */
#endif

#ifndef LOG_CRIT
#define LOG_CRIT        2       /* critical conditions */
#endif

#ifndef LOG_ERR
#define LOG_ERR         3       /* error conditions */
#endif

#ifndef LOG_WARNING
#define LOG_WARNING     4       /* warning conditions */
#endif

#ifndef LOG_NOTICE
#define LOG_NOTICE      5       /* normal but significant condition */
#endif

#ifndef LOG_INFO
#define LOG_INFO        6       /* informational */
#endif

#ifndef LOG_DEBUG
#define LOG_DEBUG       7       /* debug-level messages */
#endif

#if HAVE_KERNEL_SHARE_MODES
#ifndef LOCK_MAND 
#define LOCK_MAND	32	/* This is a mandatory flock */
#define LOCK_READ	64	/* ... Which allows concurrent read operations */
#define LOCK_WRITE	128	/* ... Which allows concurrent write operations */
#define LOCK_RW		192	/* ... Which allows concurrent read & write ops */
#endif
#endif

extern int DEBUGLEVEL;

#define MAX_SEC_CTX_DEPTH 8    /* Maximum number of security contexts */


#ifdef GLIBC_HACK_FCNTL64
/* this is a gross hack. 64 bit locking is completely screwed up on
   i386 Linux in glibc 2.1.95 (which ships with RedHat 7.0). This hack
   "fixes" the problem with the current 2.4.0test kernels 
*/
#define fcntl fcntl64
#undef F_SETLKW 
#undef F_SETLK 
#define F_SETLK 13
#define F_SETLKW 14
#endif


/* Needed for sys_dlopen/sys_dlsym/sys_dlclose */
#ifndef RTLD_GLOBAL
#define RTLD_GLOBAL 0
#endif

#ifndef RTLD_LAZY
#define RTLD_LAZY 0
#endif

#ifndef RTLD_NOW
#define RTLD_NOW 0
#endif

/* needed for some systems without iconv. Doesn't really matter
   what error code we use */
#ifndef EILSEQ
#define EILSEQ EIO
#endif

/* add varargs prototypes with printf checking */
/*PRINTFLIKE2 */
int fdprintf(int , const char *, ...) PRINTF_ATTRIBUTE(2,3);
/*PRINTFLIKE1 */
int d_printf(const char *, ...) PRINTF_ATTRIBUTE(1,2);
/*PRINTFLIKE2 */
int d_fprintf(FILE *f, const char *, ...) PRINTF_ATTRIBUTE(2,3);

/* PRINTFLIKE2 */
void sys_adminlog(int priority, const char *format_str, ...) PRINTF_ATTRIBUTE(2,3);

/* PRINTFLIKE2 */
int fstr_sprintf(fstring s, const char *fmt, ...) PRINTF_ATTRIBUTE(2,3);

int d_vfprintf(FILE *f, const char *format, va_list ap) PRINTF_ATTRIBUTE(2,0);

int smb_xvasprintf(char **ptr, const char *format, va_list ap) PRINTF_ATTRIBUTE(2,0);

int asprintf_strupper_m(char **strp, const char *fmt, ...) PRINTF_ATTRIBUTE(2,3);
char *talloc_asprintf_strupper_m(TALLOC_CTX *t, const char *fmt, ...) PRINTF_ATTRIBUTE(2,3);

/* we used to use these fns, but now we have good replacements
   for snprintf and vsnprintf */
#define slprintf snprintf
#define vslprintf vsnprintf

/* we need to use __va_copy() on some platforms */
#ifdef HAVE_VA_COPY
#define VA_COPY(dest, src) va_copy(dest, src)
#else
#ifdef HAVE___VA_COPY
#define VA_COPY(dest, src) __va_copy(dest, src)
#else
#define VA_COPY(dest, src) (dest) = (src)
#endif
#endif

/*
 * Veritas File System.  Often in addition to native.
 * Quotas different.
 */
#if defined(HAVE_SYS_FS_VX_QUOTA_H)
#define VXFS_QUOTA
#endif

#ifndef XATTR_CREATE
#define XATTR_CREATE  0x1       /* set value, fail if attr already exists */
#endif

#ifndef XATTR_REPLACE
#define XATTR_REPLACE 0x2       /* set value, fail if attr does not exist */
#endif

#ifdef HAVE_LDAP

/* function declarations not included in proto.h */
LDAP *ldap_open_with_timeout(const char *server, int port, unsigned int to);

#endif	/* HAVE_LDAP */

#if defined(HAVE_LINUX_READAHEAD) && ! defined(HAVE_READAHEAD_DECL)
ssize_t readahead(int fd, off64_t offset, size_t count);
#endif

/* TRUE and FALSE are part of the C99 standard and gcc, but
   unfortunately many vendor compilers don't support them.  Use True
   and False instead. */

#ifdef TRUE
#undef TRUE
#endif
#define TRUE __ERROR__XX__DONT_USE_TRUE

#ifdef FALSE
#undef FALSE
#endif
#define FALSE __ERROR__XX__DONT_USE_FALSE

/* If we have blacklisted mmap() try to avoid using it accidentally by
   undefining the HAVE_MMAP symbol. */

#ifdef MMAP_BLACKLIST
#undef HAVE_MMAP
#endif

#define CONST_DISCARD(type, ptr)      ((type) ((void *) (ptr)))
#define CONST_ADD(type, ptr)          ((type) ((const void *) (ptr)))

#ifndef NORETURN_ATTRIBUTE
#if (__GNUC__ >= 3)
#define NORETURN_ATTRIBUTE __attribute__ ((noreturn))
#else
#define NORETURN_ATTRIBUTE
#endif
#endif

void smb_panic( const char *why ) NORETURN_ATTRIBUTE ;
void dump_core(void) NORETURN_ATTRIBUTE ;
void exit_server(const char *const reason) NORETURN_ATTRIBUTE ;
void exit_server_cleanly(const char *const reason) NORETURN_ATTRIBUTE ;
void exit_server_fault(void) NORETURN_ATTRIBUTE ;

#ifdef HAVE_LIBNSCD
#include "libnscd.h"
#endif

#if defined(HAVE_IPV6)
void in6_addr_to_sockaddr_storage(struct sockaddr_storage *ss,
				  struct in6_addr ip);
#endif

#endif /* _INCLUDES_H */