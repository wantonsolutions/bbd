CC := g++ # define the C/C++ compiler to use
#create an executable for local.cpp
# LOCAL_STUB_LOGGER := ../../src/lib
# INCLUDES := -I$(LOCAL_STUB_LOGGER)
# LIBRARIES := -L$(LOCAL_STUB_LOGGER) -llocal_stub_logger
all: local
#compile local.cpp
local: local.cpp
	$(CC) $(CFLAGS) -o local local.cpp -llocal_stub_logger

clean:
	rm -f local