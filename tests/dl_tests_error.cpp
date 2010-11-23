/* copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info */

#include <gtest/gtest.h>

#include <dl/dl.h>
#include <dl/dl_txt.h>
#include <dl/dl_convert.h>

#include "dl_test_common.h"

class DLError : public DL {};

TEST_F(DLError, all_errors_defined_in_error_to_string)
{
	for(dl_error_t Err = DL_ERROR_OK; Err < DL_ERROR_INTERNAL_ERROR; Err = (dl_error_t)((unsigned int)Err + 1))
		EXPECT_STRNE("Unknown error!", dl_error_to_string(Err));
}

TEST_F(DLError, buffer_to_small_returned)
{
	// testing that DL_ERROR_BUFFER_TO_SMALL is returned when buffers are to small ;)

	Pods p;
	unsigned char packed[1];

	// TODO: fix this test!
	// EXPECT_DL_ERR_EQ( DL_ERROR_BUFFER_TO_SMALL, dl_instance_store( Ctx, Pods::TYPE_ID, &p, packed, DL_ARRAY_LENGTH(packed) ) );
}

TEST_F(DLError, type_mismatch_returned)
{
	// testing that DL_ERROR_TYPE_MISMATCH is returned if provided type is not matching type stored in instance

	unused u;
	unsigned char packed[sizeof(unused) * 10]; // large enough buffer!
	unsigned char swaped[sizeof(unused) * 10]; // large enough buffer!
	unsigned char bus_buffer[sizeof(unused) * 10]; // large enough buffer!
	         char bus_text[sizeof(unused) * 10]; // large enough buffer!

	dl_endian_t other_endian = DL_ENDIAN_HOST == DL_ENDIAN_LITTLE ? DL_ENDIAN_BIG : DL_ENDIAN_LITTLE;

	EXPECT_DL_ERR_OK( dl_instance_store( Ctx, unused::TYPE_ID, &u, packed, DL_ARRAY_LENGTH(packed) ) );
	EXPECT_DL_ERR_OK( dl_convert( Ctx, unused::TYPE_ID, packed, DL_ARRAY_LENGTH(packed), swaped, DL_ARRAY_LENGTH(swaped), other_endian, sizeof(void*) ) );

	// test all functions in...

#define EXPECT_DL_ERR_TYPE_MISMATCH( err ) EXPECT_DL_ERR_EQ( DL_ERROR_TYPE_MISMATCH, err )
	Pods p;
	// dl.h
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_instance_load( Ctx, Pods::TYPE_ID, &p, packed, DL_ARRAY_LENGTH(packed) ) );

	// dl_convert.h
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_convert( Ctx, Pods::TYPE_ID, packed, DL_ARRAY_LENGTH(packed), bus_buffer, DL_ARRAY_LENGTH(bus_buffer), other_endian,   sizeof(void*) ) );
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_convert( Ctx, Pods::TYPE_ID, swaped, DL_ARRAY_LENGTH(swaped), bus_buffer, DL_ARRAY_LENGTH(bus_buffer), DL_ENDIAN_HOST, sizeof(void*) ) );

	EXPECT_DL_ERR_TYPE_MISMATCH( dl_convert_inplace( Ctx, Pods::TYPE_ID, packed, DL_ARRAY_LENGTH(packed), other_endian,   sizeof(void*) ) );
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_convert_inplace( Ctx, Pods::TYPE_ID, swaped, DL_ARRAY_LENGTH(swaped), DL_ENDIAN_HOST, sizeof(void*) ) );

	unsigned int dummy = 0;
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_convert_calc_size( Ctx, Pods::TYPE_ID, packed, DL_ARRAY_LENGTH(packed), sizeof(void*), &dummy ) );
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_convert_calc_size( Ctx, Pods::TYPE_ID, swaped, DL_ARRAY_LENGTH(swaped), sizeof(void*), &dummy ) );

	// dl_txt.h
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_txt_unpack( Ctx, Pods::TYPE_ID, packed, DL_ARRAY_LENGTH(packed), bus_text, DL_ARRAY_LENGTH(bus_text) ) );
	EXPECT_DL_ERR_TYPE_MISMATCH( dl_txt_unpack_calc_size( Ctx, Pods::TYPE_ID, packed, DL_ARRAY_LENGTH(packed), &dummy ) );
#undef EXPECT_DL_ERR_TYPE_MISMATCH
}
