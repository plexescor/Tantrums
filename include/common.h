#ifndef TANTRUMS_COMMON_H
#define TANTRUMS_COMMON_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>

#define TANTRUMS_VERSION "0.1.0"

#define MAX_STACK    1024
#define MAX_FRAMES   256
#define MAX_LOCALS   256
#define MAX_CONSTANTS 65536

// Uncomment for debug output
// #define DEBUG_TRACE_EXECUTION
// #define DEBUG_PRINT_BYTECODE

#define TANTRUMS_ASSERT(cond, msg) \
    do { if (!(cond)) { fprintf(stderr, "[TANTRUMS INTERNAL] %s\n", msg); exit(1); } } while(0)

#endif
