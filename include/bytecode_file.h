#ifndef TANTRUMS_BYTECODE_FILE_H
#define TANTRUMS_BYTECODE_FILE_H

#include "value.h"
#include "chunk.h"

/*
 * .42ass bytecode file format (cross-platform, little-endian):
 *
 *   Header:
 *     Magic      : "42AS" (4 bytes)
 *     Version    : uint8
 *
 *   Then one top-level function (the script), which recursively
 *   contains all other functions as constants.
 *
 *   Function:
 *     name_len   : uint32  (0 for top-level script)
 *     name       : name_len bytes
 *     arity      : uint32
 *     const_count: uint32
 *     constants  : [const_count entries]
 *       Each constant:
 *         tag    : uint8  (0=int, 1=float, 2=string, 3=bool_true,
 *                          4=bool_false, 5=null, 6=function)
 *         data   : varies by tag
 *     code_len   : uint32
 *     code       : code_len bytes
 *     line_count : uint32
 *     lines      : line_count * int32
 */

#define BYTECODE_MAGIC "42AS"
#define BYTECODE_VERSION 3

/* Write compiled function to .42ass file. Returns true on success. */
bool bytecode_write(const char* path, ObjFunction* script);

/* Read .42ass file into an ObjFunction. Returns nullptr on failure. */
ObjFunction* bytecode_read(const char* path);

#endif