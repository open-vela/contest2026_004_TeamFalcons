#pragma once
#include <assert.h>
#include <stdio.h>
#define DEBUGASSERT assert
#define nerr(...) ((void)printf(__VA_ARGS__))
#define ninfo(...) ((void)0)

#define nwarn(...) ((void)printf(__VA_ARGS__))
