#!/bin/bash

cc -fPIC -shared -o my_functions.so toyc.c

python3 toy.py
