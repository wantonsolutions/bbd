from ctypes import *
import pickle
so_file = "my_functions.so"
MY_FUNCTIONS = CDLL(so_file)
# print(type(my_functions))



def take_bytes_and_dump(my_bytes):
	global MY_FUNCTIONS
	print("Python: sending type({}), data({})".format(type(my_bytes), my_bytes.hex()))
	print()
	my_write_function = MY_FUNCTIONS.write
	# print(my_write_function)

	my_write_function.argtypes = c_void_p, c_int
	my_write_function.restype = c_bool
	ret = my_write_function(my_bytes, len(my_bytes))
	print()
	return len(my_bytes)

def read_dumped_bytes(my_bytes_size):
	my_read_function = MY_FUNCTIONS.read
	my_read_function.restype = c_void_p
	ret_v = my_read_function()
	# print(ret_v, type(ret_v))
	b = cast(ret_v, POINTER(c_char * my_bytes_size))
	# print(b.contents)
	# print(b.contents[0])
	# print(b.contents[0].hex())
	print('Python: bytes returned:', end = '')
	retrieved_payload = []
	for i in range(my_bytes_size):
		print(b.contents[i].hex(), end='')
		retrieved_payload.append(b.contents[i])
	print()
	return b''.join(retrieved_payload)

# print(MY_FUNCTIONS.null())



my_bytes = "alexliu".encode('ascii')
# take_bytes_and_dump(my_bytes)
# read_dumped_bytes()

a = []
# a.register()


# a.append(9)

# a.new_append = def(): 

# a.append = new_append

# a.append()
# call_class_method_given_name(a, 'append', 9)

def call_class_method_given_name(class_instance, method, sorted_method_names, vs):
	# the traditional way
	# print(class_instance, method, vs)
	# method_get = getattr(class_instance, method)
	# method_get(*vs)
	# print(class_instance)

	payload_with_method = pickle.dumps((method, vs), 0)
	
	my_bytes_size = take_bytes_and_dump(payload_with_method)
	
	retrieved_payload = read_dumped_bytes(my_bytes_size)

	method_retrieved, vs_retrieved = pickle.loads(retrieved_payload)

	method_get = getattr(class_instance, method)

	method_get(*vs)

	print(class_instance)
	# print(payload_with_method.hex())
	# print(retrieved_payload.hex())


class Add():
	def __init__(self):
		pass

	def add(self, a, b):
		return a + b



class_name = list



class_dict = class_name.__dict__
# print(class_dict)

method_names = []
for k, v in class_dict.items():
	if 'method' in str(v) and "__" not in str(k):
		if str(k) not in method_names:
			method_names.append(str(k))

method_names = sorted(method_names)
print(
	'Class Name: {}\n\
	Class Methods: {}'.format(
		class_name,
		method_names
))

import time
a = Add()
stime = time.time()
for i in range(0, 1):
	a.add(0, i)
etime = time.time()
print()
# print(etime - stime)


instance = class_name()

stime = time.time()
for i in range(0, 1):
	call_class_method_given_name(instance, 'append', method_names, (1,))
etime = time.time()
print()
# print(etime - stime)


def register_class_method(class_name, method_name):
	pass
