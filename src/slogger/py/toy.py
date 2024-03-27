from ctypes import *
import pickle
import time
from functools import partial

so_file = "../../lib/liblocal_stub_logger.so"
MY_FUNCTIONS = CDLL(so_file)
# print(type(my_functions))



def take_bytes_and_dump(my_bytes):
	global MY_FUNCTIONS
	# print("Python: sending type({}), data({})".format(type(my_bytes), my_bytes.hex()))
	# print()
	my_write_function = MY_FUNCTIONS.write
	# print(my_write_function)

	my_write_function.argtypes = c_void_p, c_int
	my_write_function.restype = c_bool
	ret = my_write_function(my_bytes, len(my_bytes))
	# print()
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
	# print('Python: bytes returned:', end = '')
	retrieved_payload = []
	for i in range(my_bytes_size):
		# print(b.contents[i].hex(), end='')
		retrieved_payload.append(b.contents[i])
	# print()
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

def call_class_method_given_name(class_instance, method, vs):
	# the traditional way
	# print(class_instance, method, vs)
	# method_get = getattr(class_instance, method)
	# method_get(*vs)
	# print(class_instance)

	payload_with_method = pickle.dumps((method, vs), 0)
	
	my_bytes_size = take_bytes_and_dump(payload_with_method)
	
	retrieved_payload = read_dumped_bytes(my_bytes_size)

	method_retrieved, vs_retrieved = pickle.loads(retrieved_payload)

	method_get = getattr(class_instance, "old_" + method)

	ret = method_get(*vs)

	if ret != None:
		print("class instance: {}, ret of exec: {}".format(class_instance, ret))
	else:
		print("class instance: {}".format(class_instance, ret))

	return ret
	# print(payload_with_method.hex())
	# print(retrieved_payload.hex())

class Add():
	def __init__(self):
		pass

	def add(self, a, b):
		return a + b


# import time
# a = Add()
# stime = time.time()
# for i in range(0, 1):
# 	a.add(0, i)
# etime = time.time()
# print()
# # print(etime - stime)


def call_class_method_given_name_wrapper(bbd_class_instance = None, bbd_method_name = None, args = None, kwargs = None):
	# the traditional way
	# print(class_instance, method, vs)
	# method_get = getattr(class_instance, method)
	# method_get(*vs)
	# print(class_instance)

	payload_with_method = pickle.dumps((bbd_method_name, args, kwargs), 0)
	#print(payload_with_method.hex())
	# pickle.loads(payload_with_method)
	#print(payload_with_method.hex())
	#print(type(payload_with_method))
	
	my_bytes_size = take_bytes_and_dump(payload_with_method)
	#print(my_bytes_size)
	
	retrieved_payload = read_dumped_bytes(my_bytes_size)
	#print(retrieved_payload.hex())

	method_retrieved, args, kwargs = pickle.loads(retrieved_payload)

	method_get = getattr(bbd_class_instance, "old_" + bbd_method_name)

	ret = method_get(*args, **kwargs)

	# if ret != None:
	# 	print("class instance: {}, ret of exec: {}".format(bbd_class_instance, ret))
	# else:
	# 	print("class instance: {}".format(bbd_class_instance, ret))
	pass
	return ret


def new_func(*args, bbd_class_instance = None, bbd_method_name = None, **kwargs):
	# print("new_func")
	# print(args)
	# print(bbd_class_instance)
	# print(bbd_method_name)
	# print(kwargs)
	# print("new_func ends")
	return call_class_method_given_name_wrapper(bbd_class_instance = bbd_class_instance, bbd_method_name = bbd_method_name, args=args, kwargs = kwargs)
	

def craft_func(class_name, method_name, old_func_name, *args, **kwargs):
	pass

def register_class_method(class_instance, method_name):
	print('Registering class: {} method: {}'.format(class_instance, method_name))
	# Craft new function that has the same name as method_name
	new_func_name = "distributed_{}".format(method_name)
	# new_func_pointer = 
	# setattr(class_name, method_name, new_func_name)
	old_func_pointer = getattr(class_instance, method_name)
	old_func_name = "old_{}".format(method_name)
	setattr(class_instance, old_func_name, old_func_pointer)

	# set current
	p = partial(new_func, bbd_class_instance = class_instance, bbd_method_name = method_name)
	setattr(class_instance, method_name, p)
	# getattr(class_instance, method_name)(1,2)

import marshal
from queue import Queue

CLASS_NAME = Queue
METHOD_NAME = 'put'


class_dict = CLASS_NAME.__dict__
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
		CLASS_NAME,
		method_names
))

instance = CLASS_NAME()

stime = time.time()
for i in range(0, 100000):
	instance.put(i)
etime = time.time()
print(etime - stime)

instance = CLASS_NAME()

register_class_method(instance, METHOD_NAME)
print()
print('---Finished Registering---\n\n')


print('----- Running new add -----')
stime = time.time()
for i in range(1, 100000):
	instance.put(i)
etime = time.time()
print(etime - stime)

# print(res)
print(instance.get())
print('----- End Running New Add -----')

