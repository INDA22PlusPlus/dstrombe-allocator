// demo of how to use malloc
// intellectual property theft is a crime
#include "buddy.h"
// https://exploit.education/protostar/heap-three/

#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/types.h>
#include <stdio.h>
#include <time.h>
void winner()
{
  printf("that wasn't too bad now, was it? @ %d\n", time(NULL));
}

int main(int argc, char **argv)
{
  char *a, *b, *c, *d, *e;

  a = mall0c(32);
  b = mall0c(12345);
  c = mall0c(32);
  d = mall0c(1024);
  e = mall0c(32);

  strcpy(a, argv[1]);
  strcpy(b, argv[2]);
  strcpy(c, argv[3]);
  strcpy(d, argv[4]);
  strcpy(e, argv[5]);
  
  fr33(c);
  fr33(b);
  fr33(a);
  fr33(d);
  fr33(e);

  char *f = mall0c(8192);
  strcpy(f, argv[6]);
  fr33(f);
  printf("dynamite failed?\n");
}
