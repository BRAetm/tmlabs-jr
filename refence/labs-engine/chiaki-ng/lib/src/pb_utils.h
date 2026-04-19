// SPDX-License-Identifier: LicenseRef-AGPL-3.0-only-OpenSSL

#ifndef LABS_PB_UTILS_H
#define LABS_PB_UTILS_H

#include <pb_encode.h>
#include <pb_decode.h>

static inline bool labs_pb_encode_string(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	char *str = *arg;

	if (!pb_encode_tag_for_field(stream, field))
		return false;

	return pb_encode_string(stream, (uint8_t*)str, strlen(str));
}

typedef struct
{
    int32_t items[4];
    int32_t num_items;
}
List;

static inline bool labs_pb_encode_list(pb_ostream_t *ostream, const pb_field_t *field, void *const *arg)
{
    List *myList = *arg;

    for (int i = 0; i < myList->num_items; i++)
    {
        if (!pb_encode_tag_for_field(ostream, field))
            return false;

        if (!pb_encode_varint(ostream, myList->items[i]))
            return false;
    }
    return true;
}

typedef struct labs_pb_buf_t
{
	size_t size;
	uint8_t *buf;
} LabsPBBuf;

static inline bool labs_pb_encode_buf(pb_ostream_t *stream, const pb_field_t *field, void *const *arg)
{
	LabsPBBuf *buf = *arg;

	if (!pb_encode_tag_for_field(stream, field))
		return false;

	return pb_encode_string(stream, buf->buf, buf->size);
}


typedef struct labs_pb_decode_buf_t
{
	size_t max_size;
	size_t size;
	uint8_t *buf;
} LabsPBDecodeBuf;

static inline bool labs_pb_decode_buf(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	LabsPBDecodeBuf *buf = *arg;
	if(stream->bytes_left > buf->max_size)
	{
		buf->size = 0;
		return false;
	}

	buf->size = stream->bytes_left;
	bool r = pb_read(stream, buf->buf, buf->size);
	if(!r)
		buf->size = 0;
	return r;
}


typedef struct labs_pb_decode_buf_alloc_t
{
	size_t size;
	uint8_t *buf;
} LabsPBDecodeBufAlloc;

static inline bool labs_pb_decode_buf_alloc(pb_istream_t *stream, const pb_field_t *field, void **arg)
{
	LabsPBDecodeBufAlloc *buf = *arg;
	buf->size = stream->bytes_left;
	buf->buf = malloc(buf->size);
	if(!buf->buf)
		return false;
	bool r = pb_read(stream, buf->buf, buf->size);
	if(!r)
		buf->size = 0;
	return r;
}

#endif // LABS_PB_UTILS_H
