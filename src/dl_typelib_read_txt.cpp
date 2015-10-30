#include <dl/dl_typelib.h>
#include <dl/dl_txt.h>

#include <yajl/yajl_parse.h>

#include "dl_types.h"

#include <stdlib.h> // atol
#include <ctype.h>

#define DL_PACK_ERROR_AND_FAIL( pack_ctx, err, fmt, ... ) { dl_log_error( pack_ctx->ctx, fmt, ##__VA_ARGS__ ); pack_ctx->error_code = err; return 0x0; }

enum dl_load_txt_tl_state
{
	DL_LOAD_TXT_TL_STATE_ROOT,
	DL_LOAD_TXT_TL_STATE_ROOT_MAP,
	DL_LOAD_TXT_TL_STATE_MODULE_NAME,
	DL_LOAD_TXT_TL_STATE_USERCODE,

	DL_LOAD_TXT_TL_STATE_ENUMS,
	DL_LOAD_TXT_TL_STATE_ENUMS_MAP,
	DL_LOAD_TXT_TL_STATE_ENUM_VALUE_MAP,
	DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ITEM,
	DL_LOAD_TXT_TL_STATE_ENUM_VALUE_VALUE,
	DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST,
	DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST_ITEM,

	DL_LOAD_TXT_TL_STATE_TYPES,
	DL_LOAD_TXT_TL_STATE_TYPES_MAP,

	DL_LOAD_TXT_TL_STATE_TYPE_MAP,
	DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_LIST,
	DL_LOAD_TXT_TL_STATE_TYPE_ALIGN,
	DL_LOAD_TXT_TL_STATE_TYPE_EXTERN,
	DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_MAP,
	DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_NAME,
	DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_TYPE,
	DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT,
	DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_COMMENT,

	DL_LOAD_TXT_TL_STATE_INVALID
};

struct dl_load_txt_tl_ctx
{
	dl_error_t error_code;

	const char* src_str;
	yajl_handle yajl;

	dl_load_txt_tl_state stack[128];
	size_t stack_item;

	size_t def_value_start;
	int cur_default_value_depth;

	dl_ctx_t ctx;
	dl_type_desc*   active_type;
	dl_member_desc* active_member;

	dl_enum_desc*       active_enum;
	dl_enum_value_desc* active_enum_value;

	void push( dl_load_txt_tl_state s )
	{
		stack[++stack_item] = s;
	}

	void pop()
	{
		--stack_item;
	}

	dl_load_txt_tl_state state()
	{
		return stack[stack_item];
	}
};

template <typename T>
static T* dl_grow_array( dl_allocator* alloc, T* ptr, size_t* cap, size_t min_inc )
{
	size_t old_cap = *cap;
	size_t new_cap = ( ( old_cap < min_inc ) ? old_cap + min_inc : old_cap ) * 2;
	if( new_cap == 0 )
		new_cap = 8;
	*cap = new_cap;
	return (T*)dl_realloc( alloc, ptr, new_cap * sizeof( T ), old_cap * sizeof( T ) );
}

static uint32_t dl_alloc_string( dl_ctx_t ctx, const char* str, size_t str_len )
{
	if( ctx->typedata_strings_cap - ctx->typedata_strings_size < str_len + 2 )
	{
		ctx->typedata_strings = dl_grow_array( &ctx->alloc, ctx->typedata_strings, &ctx->typedata_strings_cap, str_len + 2 );
	}
	uint32_t pos = (uint32_t)ctx->typedata_strings_size;
	memcpy( &ctx->typedata_strings[ pos ], str, str_len );
	ctx->typedata_strings[ pos + str_len ] = 0;
	ctx->typedata_strings_size += str_len + 1;
	return pos;
}

static dl_type_desc* dl_alloc_type( dl_ctx_t ctx, dl_typeid_t tid )
{
	if( ctx->type_capacity <= ctx->type_count )
	{
		size_t cap = ctx->type_capacity;
		ctx->type_ids   = dl_grow_array( &ctx->alloc, ctx->type_ids, &cap, 0 );
		ctx->type_descs = dl_grow_array( &ctx->alloc, ctx->type_descs, &ctx->type_capacity, 0 );
	}

	unsigned int type_index = ctx->type_count;
	++ctx->type_count;

	ctx->type_ids[ type_index ] = tid;
	dl_type_desc* type = ctx->type_descs + type_index;
	memset( type, 0x0, sizeof( dl_type_desc ) );
	type->flags = DL_TYPE_FLAG_DEFAULT;
	type->member_start = ctx->member_count;
	type->member_count = 0;

	return type;
}

static dl_member_desc* dl_alloc_member( dl_ctx_t ctx )
{
	if( ctx->member_capacity <= ctx->member_count )
		ctx->member_descs = dl_grow_array( &ctx->alloc, ctx->member_descs, &ctx->member_capacity, 0 );

	unsigned int member_index = ctx->member_count;
	++ctx->member_count;

	dl_member_desc* member = ctx->member_descs + member_index;
	memset( member, 0x0, sizeof( dl_member_desc ) );
	member->default_value_offset = 0xFFFFFFFF;
	member->default_value_size = 0;
	return member;
}

static dl_enum_desc* dl_alloc_enum( dl_ctx_t ctx, dl_typeid_t tid )
{
	if( ctx->enum_capacity <= ctx->enum_count )
	{
		size_t cap = ctx->enum_capacity;
		ctx->enum_ids   = dl_grow_array( &ctx->alloc, ctx->enum_ids, &cap, 0 );
		ctx->enum_descs = dl_grow_array( &ctx->alloc, ctx->enum_descs, &ctx->enum_capacity, 0 );
	}

	unsigned int enum_index = ctx->enum_count;
	++ctx->enum_count;

	ctx->enum_ids[ enum_index ] = tid;

	dl_enum_desc* e = &ctx->enum_descs[enum_index];
	e->value_start = ctx->enum_value_count;
	e->value_count = 0;
	e->alias_count = 0;
	e->alias_start = ctx->enum_alias_count;
	return e;
}

static dl_enum_value_desc* dl_alloc_enum_value( dl_ctx_t ctx )
{
	if( ctx->enum_value_capacity <= ctx->enum_value_count )
		ctx->enum_value_descs = dl_grow_array( &ctx->alloc, ctx->enum_value_descs, &ctx->enum_value_capacity, 0 );

	unsigned int value_index = ctx->enum_value_count;
	++ctx->enum_value_count;

	dl_enum_value_desc* value = ctx->enum_value_descs + value_index;
	value->main_alias = 0;

	return value;
}

static dl_enum_alias_desc* dl_alloc_enum_alias( dl_ctx_t ctx, const unsigned char* name, size_t name_len )
{
	if( ctx->enum_alias_capacity <= ctx->enum_alias_count )
		ctx->enum_alias_descs = dl_grow_array( &ctx->alloc, ctx->enum_alias_descs, &ctx->enum_alias_capacity, 0 );

	unsigned int alias_index = ctx->enum_alias_count;
	++ctx->enum_alias_count;

	dl_enum_alias_desc* alias = &ctx->enum_alias_descs[ alias_index ];
	alias->value_index = 0xFFFFFFFF;
	alias->name = dl_alloc_string( ctx, (const char*)name, name_len );
	return alias;
}

static void dl_load_txt_tl_handle_default( dl_load_txt_tl_ctx* state )
{
	if( state->cur_default_value_depth == 0 )
	{
		uint32_t haxx_value = ( (uint32_t)state->def_value_start ) | (uint32_t)( ( yajl_get_bytes_consumed( state->yajl ) - state->def_value_start ) << 16 );
		// haxx
		state->active_member->default_value_offset = haxx_value;

		state->pop();
	}
}

struct dl_load_state_name_map
{
	const char* name;
	dl_load_txt_tl_state state;
};

static dl_load_txt_tl_state dl_load_state_from_string( dl_load_state_name_map* map, size_t map_len, const unsigned char* str_val, size_t str_len )
{
	for( size_t i = 0; i < map_len; ++i )
		if( strncmp( (const char*)str_val, map[i].name, str_len ) == 0 )
			return map[i].state;

	return DL_LOAD_TXT_TL_STATE_INVALID;
}

static int dl_load_txt_tl_on_map_start( void* ctx )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_ROOT:  state->push( DL_LOAD_TXT_TL_STATE_ROOT_MAP );  break;
		case DL_LOAD_TXT_TL_STATE_ENUMS: state->push( DL_LOAD_TXT_TL_STATE_ENUMS_MAP ); break;
		case DL_LOAD_TXT_TL_STATE_TYPES: state->push( DL_LOAD_TXT_TL_STATE_TYPES_MAP ); break;

		case DL_LOAD_TXT_TL_STATE_TYPES_MAP:
			DL_ASSERT( state->active_type != 0x0 );
			DL_ASSERT( state->active_member == 0x0 );
			DL_ASSERT( state->active_enum == 0x0 );
			DL_ASSERT( state->active_enum_value == 0x0 );
			state->push( DL_LOAD_TXT_TL_STATE_TYPE_MAP );
			break;

		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_LIST:
		{
			DL_ASSERT( state->active_type != 0x0 );
			DL_ASSERT( state->active_member == 0x0 );
			DL_ASSERT( state->active_enum == 0x0 );
			DL_ASSERT( state->active_enum_value == 0x0 );

			state->active_member = dl_alloc_member( state->ctx );

			state->push( DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_MAP );
		}
		break;

		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_MAP:
		{
			// start to parse values
		}
		break;

		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ITEM:
		{

		}
		break;

		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			++state->cur_default_value_depth;
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}
	return 1;
}

static int dl_load_txt_tl_on_map_key( void* ctx, const unsigned char* str_val, size_t str_len )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_ROOT_MAP:
		{
			dl_load_state_name_map map[] = { { "module",   DL_LOAD_TXT_TL_STATE_MODULE_NAME },
											 { "enums",    DL_LOAD_TXT_TL_STATE_ENUMS },
											 { "types",    DL_LOAD_TXT_TL_STATE_TYPES },
											 { "usercode", DL_LOAD_TXT_TL_STATE_USERCODE } };

			dl_load_txt_tl_state s = dl_load_state_from_string( map, DL_ARRAY_LENGTH( map ), str_val, str_len );
			if( s == DL_LOAD_TXT_TL_STATE_INVALID )
				DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR,
										"Got key \"%.*s\", in root-map, expected \"module\", \"enums\" or \"types\"!",
										(int)str_len, str_val );

			state->push( s );
		}
		break;
		case DL_LOAD_TXT_TL_STATE_ENUMS_MAP:
		{
			// TODO: typeid should be patched when type is done by using all members etc.
			dl_typeid_t tid = dl_internal_hash_buffer( str_val, str_len );

			dl_enum_desc* e = dl_alloc_enum( state->ctx, tid );
			e->name = dl_alloc_string( state->ctx, (const char*)str_val, str_len );
			state->active_enum = e;
			state->push( DL_LOAD_TXT_TL_STATE_ENUM_VALUE_MAP );
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPES_MAP:
		{
			// ... check that type is not already in tld ...
			// ... alloc new type ...
			// TODO: typeid should be patched when type is done by using all members etc.
			dl_typeid_t tid = dl_internal_hash_buffer( str_val, str_len );

			dl_type_desc* type = dl_alloc_type( state->ctx, tid );
			type->name = dl_alloc_string( state->ctx, (const char*)str_val, str_len );
			type->size[ DL_PTR_SIZE_32BIT ] = 0;
			type->size[ DL_PTR_SIZE_64BIT ] = 0;

			state->active_type = type;
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MAP:
		{
			dl_load_state_name_map map[] = { { "members", DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_LIST },
											 { "align",   DL_LOAD_TXT_TL_STATE_TYPE_ALIGN },
											 { "extern",  DL_LOAD_TXT_TL_STATE_TYPE_EXTERN } };

			dl_load_txt_tl_state s = dl_load_state_from_string( map, DL_ARRAY_LENGTH( map ), str_val, str_len );

			if( s == DL_LOAD_TXT_TL_STATE_INVALID )
				DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR,
										"Got key \"%.*s\", in type, expected \"members\"!",
										(int)str_len, str_val );
			state->push( s );
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_MAP:
		{
			dl_load_state_name_map map[] = { { "name",    DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_NAME },
											 { "type",    DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_TYPE },
											 { "default", DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT },
											 { "comment", DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_COMMENT } };

			dl_load_txt_tl_state s = dl_load_state_from_string( map, DL_ARRAY_LENGTH( map ), str_val, str_len );

			if( s == DL_LOAD_TXT_TL_STATE_INVALID )
				DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR,
										"Got key \"%.*s\", in member def, expected \"name\" or \"type\"!",
										(int)str_len, str_val );

			if( s == DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT )
			{
				DL_ASSERT( state->cur_default_value_depth == 0 );
				state->def_value_start = yajl_get_bytes_consumed( state->yajl );
			}

			// TODO: check that key was not already set!

			state->push( s );
		}
		break;
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_MAP:
		{
			// alloc new value
			dl_enum_value_desc* value = dl_alloc_enum_value( state->ctx );

			// alloc an alias for the base name
			dl_enum_alias_desc* alias = dl_alloc_enum_alias( state->ctx, str_val, str_len );
			alias->value_index = (uint32_t)(value - state->ctx->enum_value_descs);
			value->main_alias  = (uint32_t)(alias - state->ctx->enum_alias_descs);

			++state->active_enum->alias_count;
			++state->active_enum->value_count;

			state->active_enum_value = value;
			state->push( DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ITEM );
		}
		break;
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ITEM:
		{
			dl_load_state_name_map map[] = { { "value",   DL_LOAD_TXT_TL_STATE_ENUM_VALUE_VALUE },
											 { "aliases", DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST } };
			dl_load_txt_tl_state s = dl_load_state_from_string( map, DL_ARRAY_LENGTH( map ), str_val, str_len );

			if( s == DL_LOAD_TXT_TL_STATE_INVALID )
				DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR,
										"Got key \"%.*s\", in enum def, expected \"value\" or \"aliases\"!",
										(int)str_len, str_val );
			state->push( s );
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}

static int dl_load_txt_tl_on_map_end( void* ctx )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_ROOT_MAP:
			state->pop(); // pop stack
			break;
		case DL_LOAD_TXT_TL_STATE_ENUMS_MAP:
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_ENUMS_MAP
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_ENUMS
			break;
		case DL_LOAD_TXT_TL_STATE_TYPES_MAP:
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_TYPES_MAP
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_TYPES
			break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MAP:
		{
			dl_type_desc* type = state->active_type;

			DL_ASSERT( type != 0x0 );
			DL_ASSERT( state->active_member == 0x0 );

			// ... calc size, align and member stuff here + set typeid ...
			type->member_count = state->ctx->member_count - type->member_start;


			state->active_type = 0x0;
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_TYPE_MAP
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_MAP:
			DL_ASSERT( state->active_type != 0x0 );
			DL_ASSERT( state->active_member != 0x0 );

			// ... check that all needed stuff was set ...

			state->active_member = 0x0;

			state->pop(); // pop DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_MAP
			break;

		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_MAP:
		{
			DL_ASSERT( state->active_type == 0x0 );
			DL_ASSERT( state->active_member == 0x0 );
			DL_ASSERT( state->active_enum != 0x0 );
			DL_ASSERT( state->active_enum_value == 0x0 );
			state->active_enum = 0x0;
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_ENUM_VALUE_MAP
		}
		break;
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ITEM:
		{
			DL_ASSERT( state->active_type == 0x0 );
			DL_ASSERT( state->active_member == 0x0 );
			DL_ASSERT( state->active_enum != 0x0 );
			DL_ASSERT( state->active_enum_value != 0x0 );
			state->active_enum_value = 0x0;
			state->pop(); // pop DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_MAP
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			--state->cur_default_value_depth;
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}


static void dl_set_member_size_and_align_from_builtin( dl_type_t storage, dl_member_desc* member )
{
	switch( storage )
	{
		case DL_TYPE_STORAGE_INT8:
		case DL_TYPE_STORAGE_UINT8:
			member->set_size( 1, 1 );
			member->set_align( 1, 1 );
			break;
		case DL_TYPE_STORAGE_INT16:
		case DL_TYPE_STORAGE_UINT16:
			member->set_size( 2, 2 );
			member->set_align( 2, 2 );
			break;
		case DL_TYPE_STORAGE_FP32:
		case DL_TYPE_STORAGE_INT32:
		case DL_TYPE_STORAGE_UINT32:
		case DL_TYPE_STORAGE_ENUM:
			member->set_size( 4, 4 );
			member->set_align( 4, 4 );
			break;
		case DL_TYPE_STORAGE_FP64:
		case DL_TYPE_STORAGE_INT64:
		case DL_TYPE_STORAGE_UINT64:
			member->set_size( 8, 8 );
			member->set_align( 8, 8 );
			break;
		case DL_TYPE_STORAGE_STR:
		case DL_TYPE_STORAGE_PTR:
			member->set_size( 4, 8 );
			member->set_align( 4, 8 );
			break;
		default:
			DL_ASSERT( false );
	}
}

struct dl_builtin_type
{
	const char* name;
	dl_type_t   type;
};

static const dl_builtin_type BUILTIN_TYPES[] = {
	{ "int8",   DL_TYPE_STORAGE_INT8 },
	{ "uint8",  DL_TYPE_STORAGE_UINT8 },
	{ "int16",  DL_TYPE_STORAGE_INT16 },
	{ "uint16", DL_TYPE_STORAGE_UINT16 },
	{ "int32",  DL_TYPE_STORAGE_INT32 },
	{ "uint32", DL_TYPE_STORAGE_UINT32 },
	{ "int64",  DL_TYPE_STORAGE_INT64 },
	{ "uint64", DL_TYPE_STORAGE_UINT64 },
	{ "fp32",   DL_TYPE_STORAGE_FP32 },
	{ "fp64",   DL_TYPE_STORAGE_FP64 },
	{ "string", DL_TYPE_STORAGE_STR },
};

dl_type_t dl_make_type( dl_type_t atom, dl_type_t storage )
{
	return (dl_type_t)( (unsigned int)atom | (unsigned int)storage );
}

static size_t dl_read_uint( const char* str, const char* end, unsigned int* out )
{
	const char* iter = str;
	char num[256];
	size_t num_len = 0;
	while( isdigit(*iter) && iter != end )
	{
		num[num_len++] = *iter;
		++iter;
	}
	num[num_len] = '\0';
	*out = (unsigned int)atol( num );
	return num_len;
}

static int dl_parse_type( dl_load_txt_tl_ctx* state, const char* str, size_t str_len, dl_member_desc* member )
{
	// ... strip whitespace ...
	char   type_name[2048];
	size_t type_name_len = 0;
	DL_ASSERT( str_len < DL_ARRAY_LENGTH( type_name ) );

	const char* iter = str;
	const char* end  = str + str_len;

	while( ( isalnum( *iter ) || *iter == '_' ) && ( iter != end ) )
	{
		type_name[type_name_len++] = *iter;
		++iter;
	}
	type_name[type_name_len] = '\0';

	bool is_ptr = false;
	bool is_array = false;
	bool is_inline_array = false;
	unsigned int inline_array_len = 0;

	if( iter != end )
	{
		if( *iter == '*' )
			is_ptr = true;
		if( *iter == '[' )
		{
			++iter;
			if( *iter == ']' )
				is_array = true;
			else
			{
				iter += dl_read_uint( iter, end, &inline_array_len );
				if( *iter != ']' )
					DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR, "parse error!?!" );
				is_inline_array = true;
			}
		}
	}

	if( strcmp( "bitfield", type_name ) == 0 )
	{
		if( *iter != ':' )
			DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR, "bitfield has a bad format, should be \"bitfield:<num_bits>\"" );

		++iter;

		member->type = dl_make_type( DL_TYPE_ATOM_BITFIELD, DL_TYPE_STORAGE_UINT8 );
		member->type_id = 0;
		unsigned int bits;
		iter += dl_read_uint( iter, end, &bits );
		member->SetBitFieldBits( bits );

		// type etc?
		return 1;
	}

	for( size_t i = 0; i < DL_ARRAY_LENGTH( BUILTIN_TYPES ); ++i )
	{
		const dl_builtin_type* builtin = &BUILTIN_TYPES[i];
		if( strcmp( type_name, builtin->name ) == 0 )
		{
			// handle it ...
			if( is_ptr )
				DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR, "pointer to pod is not supported!" );

			if( is_array )
			{
				member->type = dl_make_type( DL_TYPE_ATOM_ARRAY, builtin->type );
				member->type_id = 0;
				member->set_size( 8, 16 );
				member->set_align( 4, 8 );
				return 1;
			}

			dl_set_member_size_and_align_from_builtin( builtin->type, member );

			if( is_inline_array )
			{
				member->type = dl_make_type( DL_TYPE_ATOM_INLINE_ARRAY, builtin->type );
				member->type_id = 0;
				member->size[DL_PTR_SIZE_32BIT] *= inline_array_len;
				member->size[DL_PTR_SIZE_64BIT] *= inline_array_len;
				member->set_inline_array_cnt( inline_array_len );
				return 1;
			}

			member->type = dl_make_type( DL_TYPE_ATOM_POD, builtin->type );
			member->type_id = 0;
			dl_set_member_size_and_align_from_builtin( builtin->type, member );
			return 1;
		}
	}

	member->type_id = dl_internal_hash_string( type_name );

	if( is_ptr )
	{
		member->type = dl_make_type( DL_TYPE_ATOM_POD, DL_TYPE_STORAGE_PTR );
		member->set_size( 4, 8 );
		member->set_align( 4, 8 );
		return 1;
	}

	if( is_array )
	{
		member->type = dl_make_type( DL_TYPE_ATOM_ARRAY, DL_TYPE_STORAGE_STRUCT );
		member->set_size( 8, 16 );
		member->set_align( 4, 8 );
		return 1;
	}

	if( is_inline_array )
	{
		member->type = dl_make_type( DL_TYPE_ATOM_INLINE_ARRAY, DL_TYPE_STORAGE_STRUCT );
		// hack here is used to later set the size, this can be removed if we store inline array length
		// in the same space as bitfield bits and offset.
		member->set_size( inline_array_len, inline_array_len );
		member->set_align( 0, 0 );
		member->set_inline_array_cnt( inline_array_len );
		return 1;
	}

	member->type = dl_make_type( DL_TYPE_ATOM_POD, DL_TYPE_STORAGE_STRUCT );
	member->set_size( 0, 0 );
	member->set_align( 0, 0 );
	return 1;
}

static int dl_load_txt_tl_on_string( void* ctx, const unsigned char* str_val, size_t str_len )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_MODULE_NAME: state->pop(); break;
		case DL_LOAD_TXT_TL_STATE_USERCODE: break;
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST_ITEM:
		{
			dl_enum_alias_desc* alias = dl_alloc_enum_alias( state->ctx, str_val, str_len );
			alias->value_index = (uint32_t)(state->active_enum_value - state->ctx->enum_value_descs);
			++state->active_enum->alias_count;
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_NAME:
		{
			state->active_member->name = dl_alloc_string( state->ctx, (const char*)str_val, (size_t)str_len );
			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_TYPE:
		{
			if( dl_parse_type( state, (const char*)str_val, str_len, state->active_member ) == 0 )
				return 0;

			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_COMMENT:
		{
			// HANDLE COMMENT HERE!
			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}

static int dl_load_txt_on_integer( void * ctx, long long integer )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_TYPE_ALIGN:
		{
			DL_ASSERT( state->active_type       != 0x0 );
			DL_ASSERT( state->active_member     == 0x0 );
			DL_ASSERT( state->active_enum       == 0x0 );
			DL_ASSERT( state->active_enum_value == 0x0 );

			dl_type_desc* type = state->active_type;
			if( type->alignment[DL_PTR_SIZE_32BIT] != 0 )
				DL_PACK_ERROR_AND_FAIL( state, DL_ERROR_TXT_PARSE_ERROR, "%s has \"align\" set multiple times!", dl_internal_type_name( state->ctx, type ) );

			type->alignment[DL_PTR_SIZE_32BIT] = (uint32_t)integer;
			type->alignment[DL_PTR_SIZE_64BIT] = (uint32_t)integer;

			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ITEM:
		{
			DL_ASSERT( state->active_type       == 0x0 );
			DL_ASSERT( state->active_member     == 0x0 );
			DL_ASSERT( state->active_enum       != 0x0 );
			DL_ASSERT( state->active_enum_value != 0x0 );
			state->active_enum_value->value = (uint32_t)integer;
			state->active_enum_value = 0x0;
			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_VALUE:
		{
			DL_ASSERT( state->active_type       == 0x0 );
			DL_ASSERT( state->active_member     == 0x0 );
			DL_ASSERT( state->active_enum       != 0x0 );
			DL_ASSERT( state->active_enum_value != 0x0 );
			state->active_enum_value->value = (uint32_t)integer;
			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}
	return 1;
}

static int dl_load_txt_on_double( void * ctx, double /*dbl*/ )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}
	return 1;
}

static int dl_load_txt_on_array_start( void* ctx )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST:
			state->push(DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST_ITEM );
			break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_LIST:
			break;
		case DL_LOAD_TXT_TL_STATE_USERCODE:
			break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			++state->cur_default_value_depth;
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}

static int dl_load_txt_on_array_end( void* ctx )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST_ITEM:
			state->pop(); // DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST_ITEM
			state->pop(); // DL_LOAD_TXT_TL_STATE_ENUM_VALUE_ALIAS_LIST
			break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_LIST:
			state->pop();
			break;
		case DL_LOAD_TXT_TL_STATE_USERCODE:
			state->pop();
			break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			--state->cur_default_value_depth;
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}

static int dl_load_txt_on_null( void* ctx )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}

static int dl_load_txt_on_bool( void* ctx, int value )
{
	dl_load_txt_tl_ctx* state = (dl_load_txt_tl_ctx*)ctx;

	switch( state->state() )
	{
		case DL_LOAD_TXT_TL_STATE_TYPE_EXTERN:
		{
			if( value )
				state->active_type->flags |= (uint32_t)DL_TYPE_FLAG_IS_EXTERNAL;
			state->pop();
		}
		break;
		case DL_LOAD_TXT_TL_STATE_TYPE_MEMBER_DEFAULT:
			dl_load_txt_tl_handle_default( state );
			break;
		default:
			DL_ASSERT( false );
			return 0;
	}

	return 1;
}

static inline int dl_internal_str_format(char* DL_RESTRICT buf, size_t buf_size, const char* DL_RESTRICT fmt, ...)
{
	va_list args;
	va_start( args, fmt );
	int res = vsnprintf( buf, buf_size, fmt, args );
	buf[buf_size - 1] = '\0';
	va_end( args );
	return res;
}

static bool dl_load_txt_build_default_data( dl_ctx_t ctx, const char* lib_data, unsigned int member_index )
{
	if( ctx->member_descs[member_index].default_value_offset == 0xFFFFFFFF )
		return true;

	// TODO: check that this is not outside the buffers
	dl_type_desc*   def_type   = dl_alloc_type( ctx, dl_internal_hash_string( "a_type_here" ) );
	dl_member_desc* def_member = dl_alloc_member( ctx );

	dl_member_desc* member = &ctx->member_descs[member_index];

	uint32_t def_start = member->default_value_offset & 0xFFFF;
	uint32_t def_len   = member->default_value_offset >> 16;

	char def_buffer[2048]; // TODO: no hardcode =/

	// TODO: check that typename do not exist in the ctx!

	size_t name_start = ctx->typedata_strings_size;
	def_type->name = dl_alloc_string( ctx, "a_type_here", 11 );
	def_type->size[DL_PTR_SIZE_HOST]      = member->size[DL_PTR_SIZE_HOST];
	def_type->alignment[DL_PTR_SIZE_HOST] = member->alignment[DL_PTR_SIZE_HOST];
	def_type->member_count = 1;

	memcpy( def_member, member, sizeof( dl_member_desc ) );
	def_member->offset[0] = 0;
	def_member->offset[1] = 0;

	dl_internal_str_format( def_buffer, 2048, "{\"type\":\"a_type_here\",\"data\":{\"%s\"%.*s}}", dl_internal_member_name( ctx, member ), (int)def_len, lib_data + def_start );

	size_t prod_bytes;
	dl_error_t err;
	err = dl_txt_pack( ctx, def_buffer, 0x0, 0, &prod_bytes );
	if( err != DL_ERROR_OK )
	{
		dl_log_error( ctx, "failed to pack default-value for member \"%s\" with error \"%s\"",
											dl_internal_member_name( ctx, member ),
											dl_error_to_string( err ) );
		return false;
	}

	uint8_t* pack_buffer = (uint8_t*)dl_alloc( &ctx->alloc, prod_bytes );

	dl_txt_pack( ctx, def_buffer, pack_buffer, prod_bytes, 0x0 );

	// TODO: convert packed instance to typelib endian/ptrsize here!

	size_t inst_size = prod_bytes - sizeof( dl_data_header );

	ctx->default_data = (uint8_t*)dl_realloc( &ctx->alloc, ctx->default_data, ctx->default_data_size + inst_size, ctx->default_data_size );
	memcpy( ctx->default_data + ctx->default_data_size, pack_buffer + sizeof( dl_data_header ), inst_size );

	dl_free( &ctx->alloc, pack_buffer );

	member->default_value_offset = (uint32_t)ctx->default_data_size;
	member->default_value_size   = (uint32_t)inst_size;
	ctx->default_data_size += inst_size;

	--ctx->type_count;
	--ctx->member_count;
	ctx->typedata_strings_size = name_start;
	return true;
}

static dl_member_desc* dl_load_txt_find_first_bitfield_member( dl_member_desc* start, dl_member_desc* end )
{
	while( start <= end )
	{
		if( start->AtomType() == DL_TYPE_ATOM_BITFIELD )
			return start;
		++start;
	}
	return 0x0;
}

static dl_member_desc* dl_load_txt_find_last_bitfield_member( dl_member_desc* start, dl_member_desc* end )
{
	while( start <= end )
	{
		if( start->AtomType() != DL_TYPE_ATOM_BITFIELD )
			return start - 1;
		++start;
	}
	return end;
}

static void dl_load_txt_fixup_bitfield_members( dl_ctx_t ctx, dl_type_desc* type )
{
	dl_member_desc* start = ctx->member_descs + type->member_start;
	dl_member_desc* end   = start + type->member_count - 1;

	while( true )
	{
		dl_member_desc* group_start = dl_load_txt_find_first_bitfield_member( start, end );
		if( group_start == 0x0 )
			return; // done!
		dl_member_desc* group_end = dl_load_txt_find_last_bitfield_member( group_start, end );

		unsigned int group_bits = 0;
		for( dl_member_desc* iter = group_start; iter <= group_end; ++iter )
		{
			iter->SetBitFieldOffset( group_bits );
			group_bits += iter->BitFieldBits();
		}

		// TODO: handle higher bit-counts than 64!
		dl_type_t storage = DL_TYPE_STORAGE_UINT8;
		if     ( group_bits <= 8  ) storage = DL_TYPE_STORAGE_UINT8;
		else if( group_bits <= 16 ) storage = DL_TYPE_STORAGE_UINT16;
		else if( group_bits <= 32 ) storage = DL_TYPE_STORAGE_UINT32;
		else if( group_bits <= 64 ) storage = DL_TYPE_STORAGE_UINT64;
		else
			DL_ASSERT( false );

		for( dl_member_desc* iter = group_start; iter <= group_end; ++iter )
		{
			iter->SetStorage( storage );
			dl_set_member_size_and_align_from_builtin( storage, iter );
		}

		start = group_end + 1;
	}
}

static void dl_load_txt_fixup_enum_members( dl_ctx_t ctx, dl_type_desc* type )
{
	dl_member_desc* iter = ctx->member_descs + type->member_start;
	dl_member_desc* end  = ctx->member_descs + type->member_start + type->member_count;

	for( ; iter != end; ++iter )
	{
		dl_type_t storage = iter->StorageType();
		if( storage == DL_TYPE_STORAGE_STRUCT )
		{
			dl_enum_desc* e = (dl_enum_desc*)dl_internal_find_enum( ctx, iter->type_id );
			if( e != 0x0 ) // this was really an enum!
			{
				iter->SetStorage( DL_TYPE_STORAGE_ENUM );
				if( iter->AtomType() == DL_TYPE_ATOM_POD )
					dl_set_member_size_and_align_from_builtin( DL_TYPE_STORAGE_ENUM, iter );
				else if( iter->AtomType() == DL_TYPE_ATOM_INLINE_ARRAY )
				{
					unsigned int count = iter->size[0];
					dl_set_member_size_and_align_from_builtin( DL_TYPE_STORAGE_ENUM, iter );
					iter->size[DL_PTR_SIZE_32BIT] *= count;
					iter->size[DL_PTR_SIZE_64BIT] *= count;
				}
			}
		}
	}
}

dl_error_t dl_load_txt_verify_type( dl_ctx_t ctx, dl_type_desc* type )
{
	unsigned int mem_start = type->member_start;
	unsigned int mem_end   = type->member_start + type->member_count;

	for( unsigned int member_index = mem_start; member_index < mem_end; ++member_index )
	{
		dl_member_desc* member = ctx->member_descs + member_index;
		dl_type_t storage = member->StorageType();

		if( storage == DL_TYPE_STORAGE_STRUCT )
		{
			const dl_type_desc* sub_type = dl_internal_find_type( ctx, member->type_id );
			if( sub_type == 0x0 )
			{
				dl_log_error( ctx, "---%s.%s is set to a type that is not present in typelibrary!",
									dl_internal_type_name( ctx, type ),
									dl_internal_member_name( ctx, member ) );
				return DL_ERROR_TYPE_NOT_FOUND;
			}
		}
		else if( storage == DL_TYPE_STORAGE_ENUM )
		{
			const dl_enum_desc* sub_type = dl_internal_find_enum( ctx, member->type_id );
			if( sub_type == 0x0 )
			{
				dl_log_error( ctx, "---%s.%s is set to an enum that is not present in typelibrary!",
									dl_internal_type_name( ctx, type ),
									dl_internal_member_name( ctx, member ) );
				return DL_ERROR_TYPE_NOT_FOUND;
			}
		}
	}

	return DL_ERROR_OK;
}

/**
 * return true if the calculation was successful.
 */
dl_error_t dl_load_txt_calc_type_size_and_align( dl_ctx_t ctx, dl_type_desc* type )
{
	// ... is the type already processed ...
	if( type->size[0] > 0 )
		return DL_ERROR_OK;

	if( type->member_count == 0 )
	{
		dl_log_error( ctx, "type \"%s\" has no members!", dl_internal_type_name( ctx, type ) );
		return DL_ERROR_TYPELIB_MISSING_MEMBERS_IN_TYPE;
	}

	dl_load_txt_fixup_bitfield_members( ctx, type );
	dl_load_txt_fixup_enum_members( ctx, type );

	uint32_t size[2]  = { 0, 0 };
	uint32_t align[2] = { type->alignment[DL_PTR_SIZE_32BIT], type->alignment[DL_PTR_SIZE_64BIT] };

	unsigned int mem_start = type->member_start;
	unsigned int mem_end   = type->member_start + type->member_count;

	dl_member_desc* bitfield_group_start = 0x0;

	for( unsigned int member_index = mem_start; member_index < mem_end; ++member_index )
	{
		dl_member_desc* member = ctx->member_descs + member_index;

		dl_type_t atom = member->AtomType();
		dl_type_t storage = member->StorageType();

		if( atom == DL_TYPE_ATOM_INLINE_ARRAY || atom == DL_TYPE_ATOM_POD )
		{
			if( storage == DL_TYPE_STORAGE_STRUCT )
			{
				const dl_type_desc* sub_type = dl_internal_find_type( ctx, member->type_id );
				if( sub_type == 0x0 )
					continue;

				if( sub_type->size[0] == 0 )
					dl_load_txt_calc_type_size_and_align( ctx, (dl_type_desc*)sub_type );

				if( atom == DL_TYPE_ATOM_INLINE_ARRAY )
				{
					member->size[DL_PTR_SIZE_32BIT] *= sub_type->size[DL_PTR_SIZE_32BIT];
					member->size[DL_PTR_SIZE_64BIT] *= sub_type->size[DL_PTR_SIZE_64BIT];
				}
				else
					member->copy_size( sub_type->size );

				member->copy_align( sub_type->alignment );
			}

			bitfield_group_start = 0x0;
		}
		else if( atom == DL_TYPE_ATOM_BITFIELD )
		{
			if( bitfield_group_start )
			{
				member->offset[DL_PTR_SIZE_32BIT] = bitfield_group_start->offset[DL_PTR_SIZE_32BIT];
				member->offset[DL_PTR_SIZE_64BIT] = bitfield_group_start->offset[DL_PTR_SIZE_64BIT];
				continue;
			}
			bitfield_group_start = member;
		}
		else
			bitfield_group_start = 0x0;

		member->offset[DL_PTR_SIZE_32BIT] = dl_internal_align_up( size[DL_PTR_SIZE_32BIT], member->alignment[DL_PTR_SIZE_32BIT] );
		member->offset[DL_PTR_SIZE_64BIT] = dl_internal_align_up( size[DL_PTR_SIZE_64BIT], member->alignment[DL_PTR_SIZE_64BIT] );
		size[DL_PTR_SIZE_32BIT] = member->offset[DL_PTR_SIZE_32BIT] + member->size[DL_PTR_SIZE_32BIT];
		size[DL_PTR_SIZE_64BIT] = member->offset[DL_PTR_SIZE_64BIT] + member->size[DL_PTR_SIZE_64BIT];

		align[DL_PTR_SIZE_32BIT] = member->alignment[DL_PTR_SIZE_32BIT] > align[DL_PTR_SIZE_32BIT] ? member->alignment[DL_PTR_SIZE_32BIT] : align[DL_PTR_SIZE_32BIT];
		align[DL_PTR_SIZE_64BIT] = member->alignment[DL_PTR_SIZE_64BIT] > align[DL_PTR_SIZE_64BIT] ? member->alignment[DL_PTR_SIZE_64BIT] : align[DL_PTR_SIZE_64BIT];
	}

	type->size[DL_PTR_SIZE_32BIT]      = dl_internal_align_up( size[DL_PTR_SIZE_32BIT], align[DL_PTR_SIZE_32BIT] );
	type->size[DL_PTR_SIZE_64BIT]      = dl_internal_align_up( size[DL_PTR_SIZE_64BIT], align[DL_PTR_SIZE_64BIT] );
	type->alignment[DL_PTR_SIZE_32BIT] = align[DL_PTR_SIZE_32BIT];
	type->alignment[DL_PTR_SIZE_64BIT] = align[DL_PTR_SIZE_64BIT];

	return DL_ERROR_OK;
}

static bool dl_context_load_txt_type_has_subdata( dl_ctx_t ctx, const dl_type_desc* type )
{
	unsigned int mem_start = type->member_start;
	unsigned int mem_end   = type->member_start + type->member_count;

	// do the type have subdata?
	for( unsigned int member_index = mem_start; member_index < mem_end; ++member_index )
	{
		dl_member_desc* member = ctx->member_descs + member_index;
		dl_type_t atom = member->AtomType();
		dl_type_t storage = member->StorageType();

		switch( atom )
		{
			case DL_TYPE_ATOM_ARRAY:
				return true;
			default:
				break;
		}

		switch( storage )
		{
			case DL_TYPE_STORAGE_STR:
			case DL_TYPE_STORAGE_PTR:
				return true;
			case DL_TYPE_STORAGE_STRUCT:
			{
				const dl_type_desc* subtype = dl_internal_find_type( ctx, member->type_id );
				if( dl_context_load_txt_type_has_subdata( ctx, subtype ) )
					return true;
			}
			break;
			default:
				break;
		}
	}

	return false;
}

static void dl_context_load_txt_type_set_flags( dl_ctx_t ctx, dl_type_desc* type )
{
	if( dl_context_load_txt_type_has_subdata( ctx, type ) )
		type->flags |= (uint32_t)DL_TYPE_FLAG_HAS_SUBDATA;
}

dl_error_t dl_context_load_txt_type_library( dl_ctx_t dl_ctx, const char* lib_data, size_t lib_data_size )
{
	// ... parse it ...
//	yajl_alloc_funcs my_yajl_alloc = {
//		0x0, // dl_internal_pack_alloc,
//		0x0, // dl_internal_pack_realloc,
//		0x0, // dl_internal_pack_free,
//		0x0
//	};

	yajl_callbacks callbacks = {
		dl_load_txt_on_null,
		dl_load_txt_on_bool,
		dl_load_txt_on_integer,
		dl_load_txt_on_double,
		0x0, // dl_internal_pack_on_number,
		dl_load_txt_tl_on_string,
		dl_load_txt_tl_on_map_start,
		dl_load_txt_tl_on_map_key,
		dl_load_txt_tl_on_map_end,
		dl_load_txt_on_array_start,
		dl_load_txt_on_array_end
	};

	unsigned int start_type = dl_ctx->type_count;
	unsigned int start_member = dl_ctx->member_count;

	dl_load_txt_tl_ctx state;
	state.stack_item = 0;
	state.stack[state.stack_item] = DL_LOAD_TXT_TL_STATE_ROOT;
	state.ctx = dl_ctx;
	state.active_type = 0x0;
	state.active_member = 0x0;
	state.active_enum = 0x0;
	state.active_enum_value = 0x0;
	state.cur_default_value_depth = 0;
	state.error_code = DL_ERROR_OK;
	state.src_str = lib_data;
	state.yajl = yajl_alloc( &callbacks, /*&my_yajl_alloc*/0x0, &state );
	yajl_config( state.yajl, yajl_allow_comments, 1 );

	yajl_status my_yajl_status = yajl_parse( state.yajl, (const unsigned char*)lib_data, lib_data_size );
	size_t bytes_consumed = yajl_get_bytes_consumed( state.yajl );

	if( my_yajl_status != yajl_status_ok )
	{
		unsigned int line = 0;
		unsigned int column = 0;
		if( bytes_consumed != 0 ) // error occured!
		{

			const char* ch  = lib_data;
			const char* end = lib_data + bytes_consumed;

			while( ch != end )
			{
				if( *ch == '\n' )
				{
					++line;
					column = 0;
				}
				else
					++column;

				++ch;
			}

			dl_log_error( dl_ctx, "At line %u, column %u", line, column);
		}

		char* error_str = (char*)yajl_get_error( state.yajl, 1 /* verbose */, (const unsigned char*)lib_data, (unsigned int)lib_data_size );
		dl_log_error( dl_ctx, "%s", error_str );
		yajl_free_error( state.yajl, (unsigned char*)error_str );

		yajl_free( state.yajl );
		if( state.error_code == DL_ERROR_OK )
			return DL_ERROR_TXT_PARSE_ERROR;
		return state.error_code;
	}

	yajl_free( state.yajl );

	// ... calculate size of types ...
	for( unsigned int i = start_type; i < dl_ctx->type_count; ++i )
	{
		dl_error_t err = dl_load_txt_calc_type_size_and_align( dl_ctx, dl_ctx->type_descs + i );
		if( err != DL_ERROR_OK )
			return err;
	}

	// ... check that all types resolve ...
	for( unsigned int i = start_type; i < dl_ctx->type_count; ++i )
	{
		dl_error_t err = dl_load_txt_verify_type( dl_ctx, dl_ctx->type_descs + i );
		if( err != DL_ERROR_OK )
			return err;
	}

	for( unsigned int i = start_member; i < dl_ctx->member_count; ++i )
		if( !dl_load_txt_build_default_data( dl_ctx, lib_data, i ) )
			return DL_ERROR_INVALID_DEFAULT_VALUE;

	for( unsigned int i = start_type; i < dl_ctx->type_count; ++i )
		dl_context_load_txt_type_set_flags( dl_ctx, dl_ctx->type_descs + i );

	return DL_ERROR_OK;
}
