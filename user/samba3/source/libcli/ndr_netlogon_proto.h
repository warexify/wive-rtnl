#ifndef _____LIBCLI_NDR_NETLOGON_PROTO_H__
#define _____LIBCLI_NDR_NETLOGON_PROTO_H__

#undef _PRINTF_ATTRIBUTE
#define _PRINTF_ATTRIBUTE(a1, a2) PRINTF_ATTRIBUTE(a1, a2)
/* This file was automatically generated by mkproto.pl. DO NOT EDIT */

/* this file contains prototypes for functions that are private
 * to this subsystem or library. These functions should not be
 * used outside this particular subsystem! */


/* The following definitions come from ../libcli/ndr_netlogon.c  */

enum ndr_err_code ndr_push_NETLOGON_SAM_LOGON_REQUEST(struct ndr_push *ndr, int ndr_flags, const struct NETLOGON_SAM_LOGON_REQUEST *r);
enum ndr_err_code ndr_pull_NETLOGON_SAM_LOGON_REQUEST(struct ndr_pull *ndr, int ndr_flags, struct NETLOGON_SAM_LOGON_REQUEST *r);
enum ndr_err_code ndr_push_NETLOGON_SAM_LOGON_RESPONSE_EX_with_flags(struct ndr_push *ndr, int ndr_flags, const struct NETLOGON_SAM_LOGON_RESPONSE_EX *r);
enum ndr_err_code ndr_pull_NETLOGON_SAM_LOGON_RESPONSE_EX_with_flags(struct ndr_pull *ndr, int ndr_flags, struct NETLOGON_SAM_LOGON_RESPONSE_EX *r,
								     uint32_t nt_version_flags);
#undef _PRINTF_ATTRIBUTE
#define _PRINTF_ATTRIBUTE(a1, a2)

#endif /* _____LIBCLI_NDR_NETLOGON_PROTO_H__ */

