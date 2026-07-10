#include <stdio.h>

#include "base.h"

int main(int argc, char *argv[]) {
   string hello = S("Hello world!!");

   printf(STR_FMT "\n", STR_ARG(hello));

   return 0;
}
