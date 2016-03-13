#include <gtest/gtest.h>

#include "dl_test_common.h"
#include <dl/dl_typelib.h>
#include <dl/dl_txt.h>

#define STRINGIFY( ... ) #__VA_ARGS__

struct DLTypeLib : public ::testing::Test
{
	dl_ctx_t ctx;

	void SetUp()
	{
		dl_create_params_t p;
		DL_CREATE_PARAMS_SET_DEFAULT(p);
		EXPECT_DL_ERR_OK( dl_context_create( &ctx, &p ) );
	}

	void TearDown()
	{
		EXPECT_DL_ERR_OK( dl_context_destroy( ctx ) );
	}
};

struct DLTypeLibTxt : public DLTypeLib {};

TEST_F( DLTypeLib, simple_read_write )
{
	static const unsigned char small_tl[] =
	{
		#include "generated/small.bin.h"
	};

	// ... load typelib ...
	EXPECT_DL_ERR_OK( dl_context_load_type_library( ctx, small_tl, sizeof(small_tl) ) );

	// ... get size ...
	size_t tl_size = 0;
	EXPECT_DL_ERR_OK( dl_context_write_type_library( ctx, 0x0, 0, &tl_size ) );

	// ... same size before and after write ...
	EXPECT_EQ( sizeof( small_tl ), tl_size );

	unsigned char* written = (unsigned char*)malloc( tl_size + 4 );
	written[ tl_size + 0 ] = 0xFE;
	written[ tl_size + 1 ] = 0xFE;
	written[ tl_size + 2 ] = 0xFE;
	written[ tl_size + 3 ] = 0xFE;

	EXPECT_DL_ERR_OK( dl_context_write_type_library( ctx, written, tl_size, &tl_size ) );

	EXPECT_EQ( sizeof( small_tl ), tl_size );
	EXPECT_EQ( 0xFE, written[ tl_size + 0 ] );
	EXPECT_EQ( 0xFE, written[ tl_size + 1 ] );
	EXPECT_EQ( 0xFE, written[ tl_size + 2 ] );
	EXPECT_EQ( 0xFE, written[ tl_size + 3 ] );

	// ... ensure equal ...
	EXPECT_EQ( 0, memcmp( written, small_tl, sizeof( small_tl ) ) );

	free( written );
}

TEST_F( DLTypeLibTxt, simple_read_write )
{
	const char single_member_typelib[] = STRINGIFY({
		"module" : "small",
		"types" : {
			"single_int" : { "members" : [ { "name" : "member", "type" : "uint32" } ] }
		}
	});

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_OK, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, missing_type )
{
	const char single_member_typelib[] = STRINGIFY({
		"module" : "small",
		"types" : {
			"single_int" : { "members" : [ { "name" : "member", "type" : "i_do_not_exist" } ] }
		}
	});

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_TYPE_NOT_FOUND, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, missing_comma )
{
	const char single_member_typelib[] = STRINGIFY({
		"module" : "small",
		"types" : {
			"single_int" : { "members" : [
			                   { "name" : "member1", "type" : "uint8" }
			                   { "name" : "member2", "type" : "uint8" }
			                 ] }
		}
	});

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_TXT_PARSE_ERROR, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, crash1 )
{
	const char single_member_typelib[] = STRINGIFY({
		"enums": {
			"anim_spline_type": { "ANIM_SPLINE_TYPE_ONCE": 0, "ANIM_SPLINE_TYPE_PING_PONG": 1 },
		    "timer_type": { "LOOP": 1, "ONCE": 0 }
		  },
		  "types": {
		    "a": { "members": [ { "type": "uint8", "name": "a" } ] },
		    "b": { "members": [ { "type": "uint8", "name": "a" } ] },
		    "c": { "members": [ { "type": "uint8", "name": "a", "default": true } ] },
		    "d": { "members": [ { "type": "uint8", "name": "a" } ] },
		    "e": { "members": [ { "type": "uint8", "name": "a" } ] },
		    "f": { "members": [ { "type": "uint8", "name": "a" } ] },
		    "g": { "members": [ { "type": "uint8", "name": "a" } ] },
		    "h": { "members": [ { "type": "uint8", "name": "a" } ] }
		  },
		  "module": "component_init_data"
	});

	// ... load typelib ...
	EXPECT_DL_ERR_OK( dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, nonexisting_type )
{
	const char single_member_typelib[] = STRINGIFY({
		"types" : {
			"note_t"  : { "members" : [ { "name" : "points", "type" : "int[]" } ] },
			"track_t" : { "members" : [ { "name" : "notes", "type" : "notes_t[]" } ] }
		}
	});

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_TYPE_NOT_FOUND, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, struct_with_no_members1 )
{
	const char single_member_typelib[] = STRINGIFY({ "types" : { "no_members"  : {} } });

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_TYPELIB_MISSING_MEMBERS_IN_TYPE, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, struct_with_no_members2 )
{
	const char single_member_typelib[] = STRINGIFY({ "types" : { "no_members"  : { "members" : [] } } });

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_TYPELIB_MISSING_MEMBERS_IN_TYPE, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

TEST_F( DLTypeLibTxt, invalid_default_value_array )
{
	const char single_member_typelib[] = STRINGIFY({
		"types" : {
			"note_t"  : { "members" : [ { "name" : "points", "type" : "int32", "default" : [] } ] }
		}
	});

	// ... load typelib ...
	EXPECT_DL_ERR_EQ( DL_ERROR_INVALID_DEFAULT_VALUE, dl_context_load_txt_type_library( ctx, single_member_typelib, sizeof(single_member_typelib)-1 ) );
}

static uint8_t* test_pack_txt_type_lib( const char* lib_txt, size_t lib_txt_size, size_t* out_size )
{
	dl_ctx_t ctx;
	dl_create_params_t p;
	DL_CREATE_PARAMS_SET_DEFAULT(p);
	p.error_msg_func = test_log_error;
	EXPECT_DL_ERR_OK( dl_context_create( &ctx, &p ) );
	EXPECT_DL_ERR_OK( dl_context_load_txt_type_library( ctx, lib_txt, lib_txt_size ) );

	EXPECT_DL_ERR_OK( dl_context_write_type_library( ctx, 0x0, 0, out_size ) );
	uint8_t* packed = (uint8_t*)malloc(*out_size);
	EXPECT_DL_ERR_OK( dl_context_write_type_library( ctx, packed, *out_size, 0x0 ) );

	dl_context_destroy( ctx );
	return packed;
}

TEST_F( DLTypeLib, enum_in_2_tlds )
{
	// capturing bug where enum-values would not be loaded as expected in binary tld.
	// alias-offests were not patched correctly.

	const char typelib1[] = STRINGIFY({ "module" : "tl1", "enums" : { "e1" : { "e1_v1" : 1, "e1_v2" : 2 } }, "types" : { "tl1_type" : { "members" : [ { "name" : "m1", "type" : "e1" } ] } } });
	const char typelib2[] = STRINGIFY({ "module" : "tl2", "enums" : { "e2" : { "e2_v1" : 3, "e2_v2" : 4 } }, "types" : { "tl2_type" : { "members" : [ { "name" : "m2", "type" : "e2" } ] } } });

	size_t tl1_size;
	size_t tl2_size;
	uint8_t* tl1 = test_pack_txt_type_lib( typelib1, sizeof(typelib1)-1, &tl1_size );
	uint8_t* tl2 = test_pack_txt_type_lib( typelib2, sizeof(typelib2)-1, &tl2_size );

	EXPECT_DL_ERR_OK( dl_context_load_type_library( ctx, tl1, tl1_size ) );
	EXPECT_DL_ERR_OK( dl_context_load_type_library( ctx, tl2, tl2_size ) );

	uint8_t outbuf[256];
	const char test1[] = STRINGIFY( { "tl1_type" : { "m1" : "e1_v1" } } );
	const char test2[] = STRINGIFY( { "tl2_type" : { "m2" : "e2_v1" } } );
	EXPECT_DL_ERR_OK( dl_txt_pack( ctx, test1, outbuf, sizeof(outbuf), 0x0 ) );
	EXPECT_DL_ERR_OK( dl_txt_pack( ctx, test2, outbuf, sizeof(outbuf), 0x0 ) );

	free(tl1);
	free(tl2);
}
