#include <sqlite3.h>
#include <cstdio>
int main(){ printf("sqlite %s\n", sqlite3_libversion()); return 0; }
