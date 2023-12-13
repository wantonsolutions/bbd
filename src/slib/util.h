#pragma once
#ifndef _UTIL_H_
#define _UTIL_H_

#include <string>
#include <linux/kernel.h>
#include <sched.h>
#include <chrono>
#include <vector>
using namespace std;

#define mb() asm volatile("mfence" ::: "memory")
#define wmb()	asm volatile("sfence" ::: "memory")
#define rmb()	asm volatile("lfence":::"memory")
bool IsPowerOfTwo(ulong x);
string uint64t_to_bin_string(uint64_t num);
uint64_t bin_string_to_uint64_t(string s);
uint8_t reverse(uint8_t b);
uint64_t reverse_uint64_t(uint64_t n);
chrono::nanoseconds get_current_ns(void);
string vector_to_string(vector<unsigned int> values);

#endif