#ifndef TANTRUMS_VM_H
#define TANTRUMS_VM_H

#include "chunk.h"
#include "table.h"
#include "value.h"

typedef struct {
    ObjFunction* function;
    uint8_t*     ip;
    Value*       slots;       /* first stack slot for this frame */
} CallFrame;

#define MAX_EXCEPTION_HANDLERS 64

typedef struct {
    uint8_t* catch_ip;      /* IP of the catch block */
    int      frame_count;   /* frame count when try was entered */
    Value*   stack_top;     /* stack top when try was entered */
    int      err_slot;      /* local slot for error variable (-1 if none) */
} ExceptionHandler;

struct VM {
    CallFrame frames[MAX_FRAMES];
    int       frame_count;
    Value     stack[MAX_STACK];
    Value*    stack_top;
    Table     globals;
    Obj*      objects;        /* linked list of all heap objects */
    ExceptionHandler handlers[MAX_EXCEPTION_HANDLERS];
    int       handler_count;
};

typedef enum {
    INTERPRET_OK,
    INTERPRET_COMPILE_ERROR,
    INTERPRET_RUNTIME_ERROR,
} InterpretResult;

void            vm_init(VM* vm);
void            vm_free(VM* vm);
InterpretResult vm_interpret(VM* vm, const char* source);
InterpretResult vm_interpret_compiled(VM* vm, ObjFunction* script);
void            vm_push(VM* vm, Value value);
Value           vm_pop(VM* vm);
Value           vm_peek(VM* vm, int distance);
bool            vm_runtime_error(VM* vm, const char* fmt, ...);

#endif
