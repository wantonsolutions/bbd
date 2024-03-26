from ctypes import *
so_file = "my_functions.so"
my_functions = CDLL(so_file)
# print(type(my_functions))

my_bytes = "alexliu".encode('ascii')
print("python bytes: type({}), data({})".format(type(my_bytes), my_bytes))
print()
my_write_function = my_functions.write
# print(my_write_function)

my_write_function.argtypes = c_void_p, c_int
my_write_function.restype = c_bool
print(my_write_function(my_bytes, len(my_bytes)))
print()


my_read_function = my_functions.read
my_read_function.restype = c_void_p
ret_v = my_read_function()
# print(ret_v, type(ret_v))
b = cast(ret_v, POINTER(c_char * len(my_bytes)))
# print(b.contents)
# print(b.contents[0])
# print(b.contents[0].hex())
print('C Bytes Returned:', end = '')
for i in range(len(my_bytes)):
	print(b.contents[i].decode("ascii"), end='')
print()