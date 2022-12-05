#include "io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>


void read_file(char *file_name, void *buffer, unsigned int *buffer_len) {
	
	  FILE *fp = fopen(file_name, "w+b");
	  fseek(fp, 0L, SEEK_END);
	  unsigned int file_size = ftell(fp);
	  fseek(fp, 0L, SEEK_SET); 
	  buffer = malloc(file_size);
	  memset(buffer, 0, file_size);

	  fread(buffer, file_size, 1, fp);

	  *buffer_len = file_size;
	
}
