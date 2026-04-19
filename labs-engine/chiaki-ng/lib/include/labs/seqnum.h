// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_SEQNUM_H
#define LABS_SEQNUM_H

#include "common.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// RFC 1982

#define LABS_DEFINE_SEQNUM(bits, greater_sint) \
\
typedef uint##bits##_t LabsSeqNum##bits; \
\
static inline bool labs_seq_num_##bits##_lt(LabsSeqNum##bits a, LabsSeqNum##bits b) \
{ \
	if(a == b) \
		return false; \
	greater_sint d = (greater_sint)b - (greater_sint)a; \
	return (a < b && d < ((LabsSeqNum##bits)1 << (bits - 1))) \
		|| ((a > b) && -d > ((LabsSeqNum##bits)1 << (bits - 1))); \
} \
\
static inline bool labs_seq_num_##bits##_gt(LabsSeqNum##bits a, LabsSeqNum##bits b) \
{ \
	if(a == b) \
		return false; \
	greater_sint d = (greater_sint)b - (greater_sint)a; \
	return (a < b && d > ((LabsSeqNum##bits)1 << (bits - 1))) \
		   || ((a > b) && -d < ((LabsSeqNum##bits)1 << (bits - 1))); \
}

LABS_DEFINE_SEQNUM(16, int32_t)
LABS_DEFINE_SEQNUM(32, int64_t)
#undef LABS_DEFINE_SEQNUM

#ifdef __cplusplus
}
#endif

#endif // LABS_SEQNUM_H
