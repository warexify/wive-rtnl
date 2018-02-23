/* header auto-generated by pidl */

#include "librpc/ndr/libndr.h"
#include "librpc/gen_ndr/xattr.h"

#ifndef _HEADER_NDR_xattr
#define _HEADER_NDR_xattr

#define NDR_XATTR_CALL_COUNT (0)
enum ndr_err_code ndr_push_tdb_xattr(struct ndr_push *ndr, int ndr_flags, const struct tdb_xattr *r);
enum ndr_err_code ndr_pull_tdb_xattr(struct ndr_pull *ndr, int ndr_flags, struct tdb_xattr *r);
void ndr_print_tdb_xattr(struct ndr_print *ndr, const char *name, const struct tdb_xattr *r);
enum ndr_err_code ndr_push_tdb_xattrs(struct ndr_push *ndr, int ndr_flags, const struct tdb_xattrs *r);
enum ndr_err_code ndr_pull_tdb_xattrs(struct ndr_pull *ndr, int ndr_flags, struct tdb_xattrs *r);
void ndr_print_tdb_xattrs(struct ndr_print *ndr, const char *name, const struct tdb_xattrs *r);
enum ndr_err_code ndr_push_security_descriptor_hash(struct ndr_push *ndr, int ndr_flags, const struct security_descriptor_hash *r);
enum ndr_err_code ndr_pull_security_descriptor_hash(struct ndr_pull *ndr, int ndr_flags, struct security_descriptor_hash *r);
void ndr_print_security_descriptor_hash(struct ndr_print *ndr, const char *name, const struct security_descriptor_hash *r);
void ndr_print_xattr_NTACL_Info(struct ndr_print *ndr, const char *name, const union xattr_NTACL_Info *r);
enum ndr_err_code ndr_push_xattr_NTACL(struct ndr_push *ndr, int ndr_flags, const struct xattr_NTACL *r);
enum ndr_err_code ndr_pull_xattr_NTACL(struct ndr_pull *ndr, int ndr_flags, struct xattr_NTACL *r);
void ndr_print_xattr_NTACL(struct ndr_print *ndr, const char *name, const struct xattr_NTACL *r);
#endif /* _HEADER_NDR_xattr */
