#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
//#include <stdstr.h>
const int BUF_SIZE = 1024; 
static char buf[BUF_SIZE];

bool write(void *op, int size){
	memset(buf, 0, BUF_SIZE);
	strncpy(buf, op, size);
	buf[size] = '\0';
	printf("C: Bytes received Size:%d\n", size);
	printf("C: Bytes received:%s\n", buf);
	return true;
}

void* read(){	
	//memset(buf, 1, BUF_SIZE * sizeof(buf[0]));
	return buf;
}

void* null(){	
	//memset(buf, 1, BUF_SIZE * sizeof(buf[0]));
	return NULL;
}
