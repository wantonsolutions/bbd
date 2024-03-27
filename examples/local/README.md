# Local Example

This example demonstrates how to build and run a local version of logger. The local version is single threaded and does not require a network connection. It is useful for debugging and testing the logging interface.

## Building
To compile the example run make.

The make file will build only the local logger to reduce dependencies. The local logger is located
in `src/slogger/local_stub_logger.c`. Some constants exist inside such as the default entry size for
the logger which a user can change manually. Default settings should be sufficient for basic
debugging.

The local build creates a shared object file which will be stored in `src/lib` The same object will
also be coppied directly to `/usr/local/lib` for ease of linking. This may require you to give a
sudo password when compiling.

The result of building is `local`

## Example Code
The local example code contained in `local.cpp` adds and subtracts from a global number. Each of the
addition and subtraction operations are placed on the log.

### Apply_Ops Function
This is the most complicated function that a user has to implement. Apply ops goes though the log 
one entry at a time and applies each of the operations that other threads have made. It is 
**CRITICAL** that the user sets up the `peek` and `next_op` calls exactly as shown here. Otherwise
they may mis applying an operation.

### Execute Function
Execute shows a basic example of how to serialize arguments onto the log.
