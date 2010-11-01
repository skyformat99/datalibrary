''' copyright (c) 2010 Fredrik Kihlander, see LICENSE for more info '''

import json, sys

def read_type_library_definition(filename):
	try:
		def comment_remover(text):
			import re
			def replacer(match):
				s = match.group(0)
				if s.startswith('/'):
					return ""
				else:
					return s
			pattern = re.compile( r'//.*?$|/\*.*?\*/|\'(?:\\.|[^\\\'])*\'|"(?:\\.|[^\\"])*"', re.DOTALL | re.MULTILINE )
			return re.sub(pattern, replacer, text)

		return json.loads( comment_remover(open(filename).read()) )
	except ValueError as error:
		print 'Error parsing type library:\n\t' + str(error)
		sys.exit(1)
	except IOError as error:
		print error
		sys.exit(1)

def align(val, alignment):
	return (val + alignment - 1) & ~(alignment - 1)

def is_power_of_2(n):
	from math import log
	return log(n, 2) % 1.0 == 0.0

def next_power_of_2(_Val):
	from math import log, ceil
	return int(2 ** ceil(log((_Val / 8) + 1, 2)))

def prepare_bitfields(_Members):
	i = 0
	while i < len(_Members):
		member = _Members[i]
		if member['type'] == 'bitfield':
			bf_pos  = i
			bf_bits = 0

			while i < len(_Members) and _Members[i]['type'] == 'bitfield':
				member = _Members[i]
				member.update( last_in_bf = False, bfoffset = bf_bits )
				bf_bits += member['bits']
				i += 1
			
			member['last_in_bf'] = True
			
			BITFIELD_STORAGE_TYPES = { 1 : 'uint8', 2 : 'uint16', 4 : 'uint32', 8 : 'uint64' }
			if is_power_of_2(bf_bits):
				size = bf_bits / 8
			else:
				size = next_power_of_2(bf_bits)
			
			for bfmember in _Members[bf_pos:i]:
				bfmember.update( size32 = size, size64 = size, align32 = size, align64 = size, subtype = BITFIELD_STORAGE_TYPES[size] )
				
			i -= 1
		i += 1

def calc_size_and_align_struct_r(struct, types, type_info):
	struct_name   = struct[0]
	struct_attrib = struct[1]
	if struct_name in type_info:
		return
	
	members = struct_attrib['members']
	
	prepare_bitfields(members)
		
	offset32 = 0
	offset64 = 0
	type_align32  = 0
	type_align64  = 0
	
	if 'align' in struct_attrib:
		type_align32 = struct_attrib['align']
		type_align64 = struct_attrib['align']
	
	struct_attrib['original_align'] = type_align32
	for member in members:
		type = member['type']

		if type == 'inline-array':
			subtype = member['subtype']
			if subtype not in type_info:
				calc_size_and_align_struct_r((subtype, types[subtype]), types, type_info)
			
			subtype = type_info[subtype]
			count   = member['count']
			member.update( size32 = count * subtype['size32'], size64 = count * subtype['size64'], align32 = subtype['align32'], align64 = subtype['align64'] )

		elif type == 'bitfield': pass
		else:
			if type not in type_info:
				calc_size_and_align_struct_r((type, types[type]), types, type_info)
			subtype = type_info[type]

			if 'align' in member:
				forced_align = member['align']
				member.update( size32 = subtype['size32'], size64 = subtype['size64'], align32 = forced_align, align64 = forced_align )
			else:
				member.update( size32 = subtype['size32'], size64 = subtype['size64'], align32 = subtype['align32'], align64 = subtype['align64'] )
		
		# calc offset
		offset32 = align(offset32, member['align32'])
		offset64 = align(offset64, member['align64'])
		
		member.update( offset32 = offset32, offset64 = offset64 )
		
		if type != 'bitfield' or member['last_in_bf']:
			offset32 += member['size32']
			offset64 += member['size64']
		
		type_align32 = max(type_align32, member['align32'])
		type_align64 = max(type_align64, member['align64'])
	
	type_size32 = align(offset32, type_align32)
	type_size64 = align(offset64, type_align64)
	
	struct_attrib.update( size32 = type_size32, size64 = type_size64, align32 = type_align32, align64 = type_align64 )
	
	type_info[struct_name] = { 'size32' : type_size32, 'align32' : type_align32, 'size64' : type_size64, 'align64' : type_align64 }
	
	if 'cpp-alias' in struct_attrib: type_info[struct_name].update( alias = struct_attrib['cpp-alias'] )
	if 'cs-alias' in struct_attrib:  type_info[struct_name].update( alias = struct_attrib['cs-alias'] )
		
def calc_size_and_align(data):
	# fill type_info with default types!
	type_info = {	'uint8'   : { 'size32' : 1, 'align32' : 1, 'size64' : 1,  'align64' : 1 },
					'uint16'  : { 'size32' : 2, 'align32' : 2, 'size64' : 2,  'align64' : 2 },
					'uint32'  : { 'size32' : 4, 'align32' : 4, 'size64' : 4,  'align64' : 4 }, 
					'uint64'  : { 'size32' : 8, 'align32' : 8, 'size64' : 8,  'align64' : 8 }, 
					'int8'    : { 'size32' : 1, 'align32' : 1, 'size64' : 1,  'align64' : 1 }, 
					'int16'   : { 'size32' : 2, 'align32' : 2, 'size64' : 2,  'align64' : 2 }, 
					'int32'   : { 'size32' : 4, 'align32' : 4, 'size64' : 4,  'align64' : 4 }, 
					'int64'   : { 'size32' : 8, 'align32' : 8, 'size64' : 8,  'align64' : 8 }, 
					'fp32'    : { 'size32' : 4, 'align32' : 4, 'size64' : 4,  'align64' : 4 }, 
					'fp64'    : { 'size32' : 8, 'align32' : 8, 'size64' : 8,  'align64' : 8 },
					'string'  : { 'size32' : 4, 'align32' : 4, 'size64' : 8,  'align64' : 8 },
					'pointer' : { 'size32' : 4, 'align32' : 4, 'size64' : 8,  'align64' : 8 },
					'array'   : { 'size32' : 8, 'align32' : 4, 'size64' : 12, 'align64' : 8 } }

	# fill enum types!
	if 'module_enums' in data:
		for enum_type, values in data['module_enums'].items():
			type_info[enum_type] = { 'size32' : 4, 'align32' : 4, 'size64' : 4, 'align64' : 4 }
	
	types = data['module_types']
	for struct in types.items():
		calc_size_and_align_struct_r(struct, types, type_info)
		
def calc_enum_vals(data):
	if 'module_enums' in data:
		enum_dict = data['module_enums']
		for enum in enum_dict.items():
			values = enum[1]
			
			last_value = -1
			new_values = []
			
			val_name = ''
			for val in values:
				if type(val) == unicode:
					last_value += 1
					val_name = str(val)
				else:
					assert len(val) == 1
					key = val.keys()[0]
					last_value = val[key]
					val_name = str(key)
				
				new_values.append((val_name, last_value))
			
			enum_dict[enum[0]] = new_values

def check_for_bad_data(_Data):
	# add error-check plox!
	pass
			
from header_writer_cpp import HeaderWriterCPP
from header_writer_cs import HeaderWriterCS
from type_lib_writer import TypeLibraryWriter
import logging

# 'main'-function!

def parse_options():
	from optparse import OptionParser

	parser = OptionParser(description = "dl_tlc is the type library compiler for DL and converts a text-typelibrary to binary type-libraray.")
	parser.add_option("-o", "--output",                       dest="output",                 help="write type library to file")
	parser.add_option("-c", "--cpp-header",                   dest="cppheader",              help="write C++-header to file")
	parser.add_option("-s", "--cs-header",                    dest="csheader",               help="write C#-header to file")
	parser.add_option("-v", "--verbose", action="store_true", dest="verbose", default=False, help="enable verbose output")

	(options, args) = parser.parse_args()

	if len(args) < 1:
		parser.print_help()
		sys.exit(0)
	
	options.input = args[0]
	
	if options.verbose:
		logging.basicConfig(level=logging.DEBUG)
	else:
		logging.basicConfig(level=logging.ERROR)
	
	return options

if __name__ == "__main__":
	options = parse_options()

	data = read_type_library_definition(options.input)
	
	calc_enum_vals(data)
	calc_size_and_align(data)
	
	check_for_bad_data(data)

	# write headers
	if options.cppheader:
		options.cppheader = open(options.cppheader, 'w')
		hw = HeaderWriterCPP(options.cppheader)
		hw.write_header(data)
		hw.write_enums(data)
		hw.write_structs(data)
		hw.finalize(data)
		options.cppheader.close()
		
	if options.csheader:
		options.csheader = open(options.csheader, 'w')
		hw = HeaderWriterCS(options.csheader, data['module_name'].upper())
		hw.write_enums(data)
		hw.write_structs(data)
		options.csheader.close()

	# write binary type library
	if options.output:
		options.output = open(options.output, 'wb')
		tlw = TypeLibraryWriter(options.output)
		tlw.write(data)
		options.output.close()
