#include <stdio.h>

const int BUF_SIZE = 1024; 
int square(int i) {
	return i * i;
}

bool write(void *op, int size){
	char buf[BUF_SIZE];
	strcpy(buf, op, size);
	printf("%s\n", buf);
}

void* read(){
	char buf[BUF_SIZE];
	memset(buf, 1, BUF_SIZE * sizeof(buf[0]));
	return buf
}

