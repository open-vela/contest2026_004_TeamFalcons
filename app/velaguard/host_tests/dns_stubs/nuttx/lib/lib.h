#pragma once
#include <stdlib.h>
#define lib_malloc malloc
#define lib_zalloc(n) calloc(1,(n))
#define lib_free free
