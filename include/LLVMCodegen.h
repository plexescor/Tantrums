/*  LLVMCodegen.h  —  LLVM IR code generator for Tantrums */
#ifndef TANTRUMS_LLVM_CODEGEN_H
#define TANTRUMS_LLVM_CODEGEN_H

#include "ast.h"
#include "compiler.h"
#include <string>

bool llvm_codegen_compile(ASTNode* program, CompileMode mode,
                          const char* source_path,
                          const std::string& outputObj);

bool llvm_codegen_link(const std::string& objPath,
                       const std::string& exePath);

#endif /* TANTRUMS_LLVM_CODEGEN_H */
