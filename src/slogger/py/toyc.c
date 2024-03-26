#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
//#include <stdstr.h>
const int BUF_SIZE = 1024; 
static char buf[BUF_SIZE];

bool write(void *op, int size){
	memset(buf, 0, BUF_SIZE * sizeof(buf[0]));
	strcpy(buf, op);
	printf("size %d\n", size);
	printf("bytes received:%s\n", buf);
	return true;
}

void* read(){	
	//memset(buf, 1, BUF_SIZE * sizeof(buf[0]));
	return buf;
}

