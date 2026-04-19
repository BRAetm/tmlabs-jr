// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_BITSTREAM_H
#define LABS_BITSTREAM_H

#include <stdint.h>

#include "common.h"
#include "log.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct labs_bitstream_t
{
	LabsLog *log;
	LabsCodec codec;
	union
	{
		struct
		{
			struct
			{
				uint32_t log2_max_frame_num_minus4;
			} sps;
		} h264;

		struct
		{
			struct
			{
				uint32_t log2_max_pic_order_cnt_lsb_minus4;
			} sps;
		} h265;
	};
} LabsBitstream;

typedef enum labs_bitstream_slice_type_t
{
	LABS_BITSTREAM_SLICE_UNKNOWN = 0,
	LABS_BITSTREAM_SLICE_I,
	LABS_BITSTREAM_SLICE_P,
} LabsBitstreamSliceType;

typedef struct labs_bitstream_slice_t
{
	LabsBitstreamSliceType slice_type;
	unsigned reference_frame;
} LabsBitstreamSlice;

LABS_EXPORT void labs_bitstream_init(LabsBitstream *bitstream, LabsLog *log, LabsCodec codec);
LABS_EXPORT bool labs_bitstream_header(LabsBitstream *bitstream, uint8_t *data, unsigned size);
LABS_EXPORT bool labs_bitstream_slice(LabsBitstream *bitstream, uint8_t *data, unsigned size, LabsBitstreamSlice *slice);
LABS_EXPORT bool labs_bitstream_slice_set_reference_frame(LabsBitstream *bitstream, uint8_t *data, unsigned size, unsigned reference_frame);

#ifdef __cplusplus
}
#endif

#endif // LABS_BITSTREAM_H
