/*  LLVMCodegen.cpp  —  Walk Tantrums AST → LLVM IR → .obj
 *
 *  TantrumsValue = i64 (NaN-boxed).  All runtime calls return i64 directly.
 *  No struct return, no sret, no hidden pointer — just i64 in RAX.
 *
 *  NOTE: We do NOT use `using namespace llvm` because `llvm::Value`
 *  conflicts with the Tantrums `Value` typedef from value.h.
 *  All LLVM types are fully qualified with the llvm:: prefix.
 */
#include "LLVMCodegen.h"
#include "runtime.h"
#include "token.h"

#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/GlobalVariable.h>
#include <llvm/IR/DerivedTypes.h>
#include <llvm/IR/LegacyPassManager.h>
#include <llvm/Passes/PassBuilder.h>
#include <llvm/Target/TargetMachine.h>
#include <llvm/Target/TargetOptions.h>
#include <llvm/MC/TargetRegistry.h>
#include <llvm/Support/TargetSelect.h>
#include <llvm/Support/FileSystem.h>
#include <llvm/Support/raw_ostream.h>
#include <llvm/TargetParser/Host.h>

/* On Windows we invoke lld-link.exe as an external process
 * to avoid pulling in LLVMWindowsManifest/libxml2 dependencies. */

#include <map>
#include <vector>
#include <string>
#include <cstring>
#include <cstdio>

/* ══════════════════════════════════════════════════════════════════
 *  Codegen state  —  all LLVM types are llvm:: prefixed
 * ══════════════════════════════════════════════════════════════════ */

struct LoopInfo {
    llvm::BasicBlock* header;
    llvm::BasicBlock* exit;
    llvm::BasicBlock* cont_tgt;
};

struct FuncSigInfo {
    std::string ret_type;
    int param_count;
};

struct Codegen {
    llvm::LLVMContext                   ctx;
    std::unique_ptr<llvm::Module>       mod;
    std::unique_ptr<llvm::IRBuilder<>>  B;

    llvm::Type* i64Ty;
    llvm::Type* i32Ty;
    llvm::Type* i8PtrTy;
    llvm::Type* voidTy;

    llvm::Function* curFunc = nullptr;

    /* locals: name → alloca of i64 */
    std::vector<std::map<std::string, llvm::AllocaInst*>> scopes;

    /* globals: name → GlobalVariable (i64) */
    std::map<std::string, llvm::GlobalVariable*> globals;

    /* user function map */
    std::map<std::string, llvm::Function*> userFuncs;
    std::map<std::string, FuncSigInfo> funcSigs;

    std::vector<LoopInfo> loopStack;
    CompileMode mode = MODE_BOTH;

<<<<<<< HEAD
    /* try_stack / try_depth globals for setjmp */
    llvm::GlobalVariable* tryStackGV = nullptr;
    llvm::GlobalVariable* tryDepthGV = nullptr;

=======
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
    /* ── helpers ─────────────────────────────────────── */

    llvm::AllocaInst* createEntryAlloca(llvm::Function* F, const std::string& name) {
        llvm::IRBuilder<> tmpB(&F->getEntryBlock(), F->getEntryBlock().begin());
        return tmpB.CreateAlloca(i64Ty, nullptr, name);
    }

    void pushScope() { scopes.emplace_back(); }
    void popScope()  { if (!scopes.empty()) scopes.pop_back(); }

    llvm::AllocaInst* lookupLocal(const std::string& name) {
        for (int i = (int)scopes.size() - 1; i >= 0; i--) {
            auto it = scopes[i].find(name);
            if (it != scopes[i].end()) return it->second;
        }
        return nullptr;
    }

    void setLocal(const std::string& name, llvm::AllocaInst* a) {
        if (!scopes.empty()) scopes.back()[name] = a;
    }

    llvm::Value* makeInt(int64_t n)  { return llvm::ConstantInt::get(i64Ty, tv_int(n)); }
    llvm::Value* makeFloat(double d) { return llvm::ConstantInt::get(i64Ty, tv_float(d)); }
    llvm::Value* makeBool(bool b)    { return llvm::ConstantInt::get(i64Ty, b ? TV_TRUE : TV_FALSE); }
    llvm::Value* makeNull()          { return llvm::ConstantInt::get(i64Ty, TV_NULL); }

    llvm::Value* callRT(const char* name, llvm::ArrayRef<llvm::Value*> args) {
        llvm::Function* fn = mod->getFunction(name);
        if (!fn) { fprintf(stderr, "BUG: runtime function '%s' not declared\n", name); return makeNull(); }
        llvm::CallInst* ci = B->CreateCall(fn, args);
        if (fn->getReturnType()->isVoidTy()) return nullptr;
        return ci;
    }

    llvm::Constant* makeStringConstant(const std::string& s) {
        auto* strConst = llvm::ConstantDataArray::getString(ctx, s, true);
        auto* gv = new llvm::GlobalVariable(*mod, strConst->getType(), true,
                                            llvm::GlobalValue::PrivateLinkage, strConst, ".str");
        gv->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
        llvm::Constant* zero = llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), 0);
        llvm::Constant* idxs[] = { zero, zero };
        return llvm::ConstantExpr::getGetElementPtr(strConst->getType(), gv, idxs, true);
    }

    llvm::ConstantInt* i32Val(int n) { return llvm::ConstantInt::get(llvm::Type::getInt32Ty(ctx), n); }
<<<<<<< HEAD
    llvm::ConstantInt* i64Val(int64_t n) { return llvm::ConstantInt::get(llvm::Type::getInt64Ty(ctx), n); }
=======
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
};

/* ══════════════════════════════════════════════════════════════════
 *  Forward declarations
 * ══════════════════════════════════════════════════════════════════ */
static void declareRuntimeFunctions(Codegen& cg);
static void prescan(Codegen& cg, ASTNode* program);
static void codegenProgram(Codegen& cg, ASTNode* program);
static void codegenStmt(Codegen& cg, ASTNode* node);
static llvm::Value* codegenExpr(Codegen& cg, ASTNode* node);

/* ══════════════════════════════════════════════════════════════════
 *  Runtime function declarations
 * ══════════════════════════════════════════════════════════════════ */

static void declareRuntimeFunctions(Codegen& cg) {
    auto& M = *cg.mod;
    llvm::Type* i64 = cg.i64Ty;
    llvm::Type* i32 = cg.i32Ty;
    llvm::Type* v   = cg.voidTy;
    llvm::Type* p8  = cg.i8PtrTy;
    llvm::Type* pi64 = llvm::PointerType::getUnqual(cg.ctx);

    auto decl = [&](const char* name, llvm::Type* ret, llvm::ArrayRef<llvm::Type*> args, bool vararg = false) {
        llvm::FunctionType* ft = llvm::FunctionType::get(ret, args, vararg);
        llvm::Function::Create(ft, llvm::Function::ExternalLinkage, name, &M);
    };

    decl("rt_print",       v,   {pi64, i32});
    decl("rt_string_from_cstr", i64, {p8});
    decl("rt_input",       i64, {i64});
    decl("rt_len",         i64, {i64});
    decl("rt_range",       i64, {i64, i64, i64});
    decl("rt_type",        i64, {i64});
    decl("rt_list_new",    i64, {pi64, i32});
    decl("rt_map_new",     i64, {pi64, pi64, i32});
    decl("rt_index_get",   i64, {i64, i64});
    decl("rt_index_set",   v,   {i64, i64, i64});
    decl("rt_append",      v,   {i64, i64});
    decl("rt_alloc",       i64, {i64, p8});
    decl("rt_free_val",    v,   {i64});
    decl("rt_ptr_deref",   i64, {i64});
    decl("rt_ptr_set",     v,   {i64, i64});
    decl("rt_add",         i64, {i64, i64});
    decl("rt_sub",         i64, {i64, i64});
    decl("rt_mul",         i64, {i64, i64});
    decl("rt_div",         i64, {i64, i64});
    decl("rt_mod",         i64, {i64, i64});
    decl("rt_negate",      i64, {i64});
    decl("rt_not",         i64, {i64});
    decl("rt_eq",          i64, {i64, i64});
    decl("rt_neq",         i64, {i64, i64});
    decl("rt_lt",          i64, {i64, i64});
    decl("rt_gt",          i64, {i64, i64});
    decl("rt_lte",         i64, {i64, i64});
    decl("rt_gte",         i64, {i64, i64});
    decl("rt_is_truthy",   i32, {i64});
    decl("rt_for_in_step", i64, {i64, pi64});
    decl("rt_for_in_has_next", i32, {i64, i64});
    decl("rt_throw",       v,   {i64});
<<<<<<< HEAD
    decl("rt_try_push",    v,   {});
    decl("rt_try_exit",    v,   {});
    decl("rt_caught_val",  i64, {});
    decl("rt_get_jmpbuf",  p8,  {});

    /* _setjmp on MSVC x64: int _setjmp(ptr jmpbuf, ptr frameaddr)
     * Must be called directly in generated code with the frame pointer. */
    decl("_setjmp",        i32, {p8, p8});

=======
    decl("rt_try_enter",   i32, {});
    decl("rt_try_exit",    v,   {});
    decl("rt_caught_val",  i64, {});
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
    decl("rt_cast",        i64, {i64, i32});
    decl("rt_enter_scope", v,   {});
    decl("rt_exit_scope",  v,   {});
    decl("rt_mark_escaped",v,   {i64});
    decl("rt_free_collection", v, {i64});
    decl("rt_init",        v,   {});
    decl("rt_shutdown",    v,   {});
    decl("rt_getCurrentTime",      i64, {});
    decl("rt_toSeconds",           i64, {i64});
    decl("rt_toMilliseconds",      i64, {i64});
    decl("rt_toMinutes",           i64, {i64});
    decl("rt_toHours",             i64, {i64});
    decl("rt_getProcessMemory",    i64, {});
    decl("rt_getHeapMemory",       i64, {});
    decl("rt_getHeapPeakMemory",   i64, {});
    decl("rt_bytesToKB",           i64, {i64});
    decl("rt_bytesToMB",           i64, {i64});
    decl("rt_bytesToGB",           i64, {i64});
<<<<<<< HEAD
    
    decl("rt_math_sin",          i64, {i64});
    decl("rt_math_cos",          i64, {i64});
    decl("rt_math_tan",          i64, {i64});
    decl("rt_math_sec",          i64, {i64});
    decl("rt_math_cosec",        i64, {i64});
    decl("rt_math_cot",          i64, {i64});
    decl("rt_math_floor",        i64, {i64});
    decl("rt_math_ceil",         i64, {i64});
    decl("rt_math_random_int",   i64, {i64, i64});
    decl("rt_math_random_float", i64, {i64, i64});
=======
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
}

/* ══════════════════════════════════════════════════════════════════
 *  Prescan — forward-declare user functions
 * ══════════════════════════════════════════════════════════════════ */

static void prescan(Codegen& cg, ASTNode* program) {
    if (program->type != NODE_PROGRAM) return;
    for (int i = 0; i < program->as.program.count; i++) {
        ASTNode* n = program->as.program.nodes[i];
        if (n->type != NODE_FUNC_DECL) continue;

        const char* name = n->as.func_decl.name;
        int arity = n->as.func_decl.param_count;

        FuncSigInfo sig;
        sig.ret_type = n->as.func_decl.ret_type ? n->as.func_decl.ret_type : "";
        sig.param_count = arity;
        cg.funcSigs[name] = sig;

        std::vector<llvm::Type*> paramTys(arity, cg.i64Ty);
        llvm::FunctionType* ft = llvm::FunctionType::get(cg.i64Ty, paramTys, false);
        std::string llName = std::string("__t_") + name;
        llvm::Function* fn = llvm::Function::Create(ft, llvm::Function::InternalLinkage, llName, cg.mod.get());
        cg.userFuncs[name] = fn;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Expression codegen  —  returns llvm::Value* (i64)
 * ══════════════════════════════════════════════════════════════════ */

static llvm::Value* codegenExpr(Codegen& cg, ASTNode* node) {
    if (!node) return cg.makeNull();

    switch (node->type) {
    case NODE_INT_LIT:   return cg.makeInt(node->as.int_literal);
    case NODE_FLOAT_LIT: return cg.makeFloat(node->as.float_literal);
    case NODE_BOOL_LIT:  return cg.makeBool(node->as.bool_literal);
    case NODE_NULL_LIT:  return cg.makeNull();

    case NODE_STRING_LIT: {
        std::string s(node->as.string_literal.value, node->as.string_literal.length);
        llvm::Value* str = cg.makeStringConstant(s);
        return cg.callRT("rt_string_from_cstr", {str});
    }

    case NODE_IDENTIFIER: {
        const char* name = node->as.identifier.name;
        llvm::AllocaInst* a = cg.lookupLocal(name);
        if (a) return cg.B->CreateLoad(cg.i64Ty, a, name);
        auto git = cg.globals.find(name);
        if (git != cg.globals.end())
            return cg.B->CreateLoad(cg.i64Ty, git->second, name);
        return cg.makeNull();
    }

    case NODE_UNARY: {
        llvm::Value* operand = codegenExpr(cg, node->as.unary.operand);
        switch (node->as.unary.op) {
        case TOKEN_MINUS: return cg.callRT("rt_negate", {operand});
        case TOKEN_BANG:  return cg.callRT("rt_not", {operand});
        case TOKEN_STAR:  return cg.callRT("rt_ptr_deref", {operand});
        default: return operand;
        }
    }

    case NODE_BINARY: {
        /* Short-circuit AND */
        if (node->as.binary.op == TOKEN_AND) {
            llvm::Value* lhs = codegenExpr(cg, node->as.binary.left);
            llvm::Value* cond = cg.B->CreateICmpNE(
                cg.callRT("rt_is_truthy", {lhs}), cg.i32Val(0));
            llvm::Function* F = cg.curFunc;
            llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(cg.ctx, "and.rhs", F);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(cg.ctx, "and.merge", F);
            llvm::BasicBlock* lhsBB = cg.B->GetInsertBlock();
            cg.B->CreateCondBr(cond, rhsBB, mergeBB);
            cg.B->SetInsertPoint(rhsBB);
            llvm::Value* rhs = codegenExpr(cg, node->as.binary.right);
            llvm::BasicBlock* rhsEnd = cg.B->GetInsertBlock();
            cg.B->CreateBr(mergeBB);
            cg.B->SetInsertPoint(mergeBB);
            llvm::PHINode* phi = cg.B->CreatePHI(cg.i64Ty, 2, "and.val");
            phi->addIncoming(lhs, lhsBB);
            phi->addIncoming(rhs, rhsEnd);
            return phi;
        }
        /* Short-circuit OR */
        if (node->as.binary.op == TOKEN_OR) {
            llvm::Value* lhs = codegenExpr(cg, node->as.binary.left);
            llvm::Value* cond = cg.B->CreateICmpNE(
                cg.callRT("rt_is_truthy", {lhs}), cg.i32Val(0));
            llvm::Function* F = cg.curFunc;
            llvm::BasicBlock* rhsBB = llvm::BasicBlock::Create(cg.ctx, "or.rhs", F);
            llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(cg.ctx, "or.merge", F);
            llvm::BasicBlock* lhsBB = cg.B->GetInsertBlock();
            cg.B->CreateCondBr(cond, mergeBB, rhsBB);
            cg.B->SetInsertPoint(rhsBB);
            llvm::Value* rhs = codegenExpr(cg, node->as.binary.right);
            llvm::BasicBlock* rhsEnd = cg.B->GetInsertBlock();
            cg.B->CreateBr(mergeBB);
            cg.B->SetInsertPoint(mergeBB);
            llvm::PHINode* phi = cg.B->CreatePHI(cg.i64Ty, 2, "or.val");
            phi->addIncoming(lhs, lhsBB);
            phi->addIncoming(rhs, rhsEnd);
            return phi;
        }

        llvm::Value* lhs = codegenExpr(cg, node->as.binary.left);
        llvm::Value* rhs = codegenExpr(cg, node->as.binary.right);
        switch (node->as.binary.op) {
        case TOKEN_PLUS:          return cg.callRT("rt_add", {lhs, rhs});
        case TOKEN_MINUS:         return cg.callRT("rt_sub", {lhs, rhs});
        case TOKEN_STAR:          return cg.callRT("rt_mul", {lhs, rhs});
        case TOKEN_SLASH:         return cg.callRT("rt_div", {lhs, rhs});
        case TOKEN_PERCENT:       return cg.callRT("rt_mod", {lhs, rhs});
        case TOKEN_EQUAL_EQUAL:   return cg.callRT("rt_eq",  {lhs, rhs});
        case TOKEN_BANG_EQUAL:    return cg.callRT("rt_neq", {lhs, rhs});
        case TOKEN_LESS:          return cg.callRT("rt_lt",  {lhs, rhs});
        case TOKEN_GREATER:       return cg.callRT("rt_gt",  {lhs, rhs});
        case TOKEN_LESS_EQUAL:    return cg.callRT("rt_lte", {lhs, rhs});
        case TOKEN_GREATER_EQUAL: return cg.callRT("rt_gte", {lhs, rhs});
        default: return cg.makeNull();
        }
    }

    case NODE_CALL: {
        ASTNode* callee = node->as.call.callee;
<<<<<<< HEAD
        int argc = node->as.call.arg_count;

        /* Directly intercept math.[function]() for lightning-fast native codegen without Map lookups */
        if (callee->type == NODE_INDEX && 
            callee->as.index_access.object->type == NODE_IDENTIFIER &&
            callee->as.index_access.index->type == NODE_STRING_LIT) {
            
            const char* obj_name = callee->as.index_access.object->as.identifier.name;
            const char* prop_name = callee->as.index_access.index->as.string_literal.value;
            
            if (strcmp(obj_name, "math") == 0) {
                if (strcmp(prop_name, "sin") == 0 && argc >= 1) return cg.callRT("rt_math_sin", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "cos") == 0 && argc >= 1) return cg.callRT("rt_math_cos", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "tan") == 0 && argc >= 1) return cg.callRT("rt_math_tan", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "sec") == 0 && argc >= 1) return cg.callRT("rt_math_sec", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "cosec") == 0 && argc >= 1) return cg.callRT("rt_math_cosec", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "cot") == 0 && argc >= 1) return cg.callRT("rt_math_cot", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "floor") == 0 && argc >= 1) return cg.callRT("rt_math_floor", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "ceil") == 0 && argc >= 1) return cg.callRT("rt_math_ceil", {codegenExpr(cg, node->as.call.args[0])});
                if (strcmp(prop_name, "random_int") == 0 && argc >= 2) return cg.callRT("rt_math_random_int", {codegenExpr(cg, node->as.call.args[0]), codegenExpr(cg, node->as.call.args[1])});
                if (strcmp(prop_name, "random_float") == 0 && argc >= 2) return cg.callRT("rt_math_random_float", {codegenExpr(cg, node->as.call.args[0]), codegenExpr(cg, node->as.call.args[1])});
            }
        }

        if (callee->type != NODE_IDENTIFIER) return cg.makeNull();
        const char* name = callee->as.identifier.name;
=======
        if (callee->type != NODE_IDENTIFIER) return cg.makeNull();
        const char* name = callee->as.identifier.name;
        int argc = node->as.call.arg_count;
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174

        /* ── Built-in functions ── */
        if (strcmp(name, "print") == 0) {
            llvm::AllocaInst* arr = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(argc > 0 ? argc : 1), "print_args");
            for (int i = 0; i < argc; i++) {
                llvm::Value* v = codegenExpr(cg, node->as.call.args[i]);
                llvm::Value* ptr = cg.B->CreateGEP(cg.i64Ty, arr, {cg.i32Val(i)});
                cg.B->CreateStore(v, ptr);
            }
            cg.callRT("rt_print", {arr, cg.i32Val(argc)});
            return cg.makeNull();
        }
        if (strcmp(name, "input") == 0) {
            llvm::Value* prompt = argc >= 1 ? codegenExpr(cg, node->as.call.args[0]) : cg.makeNull();
            return cg.callRT("rt_input", {prompt});
        }
        if (strcmp(name, "len") == 0) {
            llvm::Value* arg = argc >= 1 ? codegenExpr(cg, node->as.call.args[0]) : cg.makeNull();
            return cg.callRT("rt_len", {arg});
        }
        if (strcmp(name, "range") == 0) {
            llvm::Value* a = argc >= 1 ? codegenExpr(cg, node->as.call.args[0]) : cg.makeNull();
            llvm::Value* b = argc >= 2 ? codegenExpr(cg, node->as.call.args[1]) : cg.makeNull();
            llvm::Value* c = argc >= 3 ? codegenExpr(cg, node->as.call.args[2]) : cg.makeNull();
            return cg.callRT("rt_range", {a, b, c});
        }
        if (strcmp(name, "type") == 0) {
            llvm::Value* arg = argc >= 1 ? codegenExpr(cg, node->as.call.args[0]) : cg.makeNull();
            return cg.callRT("rt_type", {arg});
        }
        if (strcmp(name, "append") == 0) {
            llvm::Value* list = argc >= 1 ? codegenExpr(cg, node->as.call.args[0]) : cg.makeNull();
            llvm::Value* val = argc >= 2 ? codegenExpr(cg, node->as.call.args[1]) : cg.makeNull();
            cg.callRT("rt_append", {list, val});
            return cg.makeNull();
        }
        /* Time API */
        if (strcmp(name, "getCurrentTime") == 0)     return cg.callRT("rt_getCurrentTime", {});
        if (strcmp(name, "toSeconds") == 0 && argc>0) return cg.callRT("rt_toSeconds", {codegenExpr(cg, node->as.call.args[0])});
        if (strcmp(name, "toMilliseconds") == 0 && argc>0) return cg.callRT("rt_toMilliseconds", {codegenExpr(cg, node->as.call.args[0])});
        if (strcmp(name, "toMinutes") == 0 && argc>0) return cg.callRT("rt_toMinutes", {codegenExpr(cg, node->as.call.args[0])});
        if (strcmp(name, "toHours") == 0 && argc>0)   return cg.callRT("rt_toHours", {codegenExpr(cg, node->as.call.args[0])});
        /* Memory API */
        if (strcmp(name, "getProcessMemory") == 0)   return cg.callRT("rt_getProcessMemory", {});
        if (strcmp(name, "getVmMemory") == 0)        return cg.callRT("rt_getHeapMemory", {});
        if (strcmp(name, "getVmPeakMemory") == 0)    return cg.callRT("rt_getHeapPeakMemory", {});
        if (strcmp(name, "bytesToKB") == 0 && argc>0) return cg.callRT("rt_bytesToKB", {codegenExpr(cg, node->as.call.args[0])});
        if (strcmp(name, "bytesToMB") == 0 && argc>0) return cg.callRT("rt_bytesToMB", {codegenExpr(cg, node->as.call.args[0])});
        if (strcmp(name, "bytesToGB") == 0 && argc>0) return cg.callRT("rt_bytesToGB", {codegenExpr(cg, node->as.call.args[0])});

        /* ── User function call ── */
        auto it = cg.userFuncs.find(name);
        if (it != cg.userFuncs.end()) {
            std::vector<llvm::Value*> args;
            for (int i = 0; i < argc; i++)
                args.push_back(codegenExpr(cg, node->as.call.args[i]));
            return cg.B->CreateCall(it->second, args);
        }
        fprintf(stderr, "[Codegen] Unknown function: %s\n", name);
        return cg.makeNull();
    }

    case NODE_LIST_LIT: {
        int count = node->as.list_literal.count;
        llvm::AllocaInst* arr = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(count > 0 ? count : 1), "list_items");
        for (int i = 0; i < count; i++) {
            llvm::Value* v = codegenExpr(cg, node->as.list_literal.nodes[i]);
            cg.B->CreateStore(v, cg.B->CreateGEP(cg.i64Ty, arr, {cg.i32Val(i)}));
        }
        return cg.callRT("rt_list_new", {arr, cg.i32Val(count)});
    }

    case NODE_MAP_LIT: {
        int count = node->as.map_literal.count;
        llvm::AllocaInst* ka = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(count > 0 ? count : 1), "map_keys");
        llvm::AllocaInst* va = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(count > 0 ? count : 1), "map_vals");
        for (int i = 0; i < count; i++) {
            llvm::Value* k = codegenExpr(cg, node->as.map_literal.keys[i]);
            llvm::Value* v = codegenExpr(cg, node->as.map_literal.values[i]);
            cg.B->CreateStore(k, cg.B->CreateGEP(cg.i64Ty, ka, {cg.i32Val(i)}));
            cg.B->CreateStore(v, cg.B->CreateGEP(cg.i64Ty, va, {cg.i32Val(i)}));
        }
        return cg.callRT("rt_map_new", {ka, va, cg.i32Val(count)});
    }

    case NODE_INDEX:
        return cg.callRT("rt_index_get", {
            codegenExpr(cg, node->as.index_access.object),
            codegenExpr(cg, node->as.index_access.index)
        });

    case NODE_ALLOC: {
        llvm::Value* init = codegenExpr(cg, node->as.alloc_expr.init);
        const char* tn = node->as.alloc_expr.type_name ? node->as.alloc_expr.type_name : "dynamic";
        llvm::Value* typeStr = cg.makeStringConstant(tn);
        return cg.callRT("rt_alloc", {init, typeStr});
    }

    case NODE_ASSIGN: {
        llvm::Value* val = codegenExpr(cg, node->as.assign.value);
        const char* name = node->as.assign.name;
        llvm::AllocaInst* a = cg.lookupLocal(name);
        if (a) { cg.B->CreateStore(val, a); return val; }
        auto git = cg.globals.find(name);
        if (git != cg.globals.end()) { cg.B->CreateStore(val, git->second); return val; }
        /* Implicit global */
        auto* gv = new llvm::GlobalVariable(*cg.mod, cg.i64Ty, false,
                                            llvm::GlobalValue::InternalLinkage,
                                            llvm::ConstantInt::get(cg.i64Ty, TV_NULL), name);
        cg.globals[name] = gv;
        cg.B->CreateStore(val, gv);
        return val;
    }

    case NODE_INDEX_ASSIGN: {
        if (node->as.index_assign.index == nullptr) {
            llvm::Value* val = codegenExpr(cg, node->as.index_assign.value);
            llvm::Value* ptr = codegenExpr(cg, node->as.index_assign.object);
            cg.callRT("rt_ptr_set", {ptr, val});
            return val;
        }
        llvm::Value* obj = codegenExpr(cg, node->as.index_assign.object);
        llvm::Value* idx = codegenExpr(cg, node->as.index_assign.index);
        llvm::Value* val = codegenExpr(cg, node->as.index_assign.value);
        cg.callRT("rt_index_set", {obj, idx, val});
        return val;
    }

    case NODE_POSTFIX: {
        if (node->as.postfix.operand->type != NODE_IDENTIFIER) return cg.makeNull();
        const char* name = node->as.postfix.operand->as.identifier.name;
        bool is_inc = (node->as.postfix.op == TOKEN_PLUS_PLUS);
        llvm::AllocaInst* a = cg.lookupLocal(name);
        llvm::GlobalVariable* gv = nullptr;
        if (!a) { auto git = cg.globals.find(name); if (git != cg.globals.end()) gv = git->second; }
        llvm::Value* ptr = a ? (llvm::Value*)a : (llvm::Value*)gv;
        if (!ptr) return cg.makeNull();
        llvm::Value* old = cg.B->CreateLoad(cg.i64Ty, ptr, "old");
        llvm::Value* one = cg.makeInt(1);
        llvm::Value* nv = is_inc ? cg.callRT("rt_add", {old, one}) : cg.callRT("rt_sub", {old, one});
        cg.B->CreateStore(nv, ptr);
        return old;
    }

    default:
        return cg.makeNull();
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Statement codegen
 * ══════════════════════════════════════════════════════════════════ */

static void codegenStmt(Codegen& cg, ASTNode* node) {
    if (!node) return;

    switch (node->type) {
    case NODE_EXPR_STMT:
        codegenExpr(cg, node->as.child);
        break;

    case NODE_VAR_DECL: {
        const char* name = node->as.var_decl.name;
        llvm::Value* init;
        if (node->as.var_decl.init) {
            init = codegenExpr(cg, node->as.var_decl.init);
        } else {
            if (node->as.var_decl.type_name) {
                if (strcmp(node->as.var_decl.type_name, "int") == 0)        init = cg.makeInt(0);
                else if (strcmp(node->as.var_decl.type_name, "float") == 0) init = cg.makeFloat(0.0);
                else if (strcmp(node->as.var_decl.type_name, "bool") == 0)  init = cg.makeBool(false);
                else if (strcmp(node->as.var_decl.type_name, "string") == 0)
                    init = cg.callRT("rt_string_from_cstr", {(llvm::Value*)cg.makeStringConstant("")});
                else if (strcmp(node->as.var_decl.type_name, "list") == 0) {
                    llvm::AllocaInst* ea = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(1));
                    init = cg.callRT("rt_list_new", {ea, cg.i32Val(0)});
                }
                else if (strcmp(node->as.var_decl.type_name, "map") == 0) {
                    llvm::AllocaInst* ek = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(1));
                    llvm::AllocaInst* ev = cg.B->CreateAlloca(cg.i64Ty, cg.i32Val(1));
                    init = cg.callRT("rt_map_new", {ek, ev, cg.i32Val(0)});
                }
                else init = cg.makeNull();
            } else init = cg.makeNull();
        }
        /* Auto-cast */
        if (node->as.var_decl.type_name && node->as.var_decl.init &&
            node->as.var_decl.init->type != NODE_ALLOC) {
            int cast_tag = -1;
            if (strcmp(node->as.var_decl.type_name, "int") == 0)    cast_tag = 0;
            if (strcmp(node->as.var_decl.type_name, "float") == 0)  cast_tag = 1;
            if (strcmp(node->as.var_decl.type_name, "string") == 0) cast_tag = 2;
            if (strcmp(node->as.var_decl.type_name, "bool") == 0)   cast_tag = 3;
            if (cast_tag >= 0)
                init = cg.callRT("rt_cast", {init, cg.i32Val(cast_tag)});
        }

        if (cg.scopes.size() > 1) {
            llvm::AllocaInst* a = cg.createEntryAlloca(cg.curFunc, name);
            cg.B->CreateStore(init, a);
            cg.setLocal(name, a);
        } else {
            auto* gv = new llvm::GlobalVariable(*cg.mod, cg.i64Ty, false,
                                                llvm::GlobalValue::InternalLinkage,
                                                llvm::ConstantInt::get(cg.i64Ty, TV_NULL), name);
            cg.globals[name] = gv;
            cg.B->CreateStore(init, gv);
        }
        break;
    }

    case NODE_BLOCK:
        cg.pushScope();
        cg.callRT("rt_enter_scope", {});
        for (int i = 0; i < node->as.block.count; i++) {
            codegenStmt(cg, node->as.block.nodes[i]);
<<<<<<< HEAD
            if (cg.B->GetInsertBlock()->getTerminator()) break;
        }
        if (!cg.B->GetInsertBlock()->getTerminator())
            cg.callRT("rt_exit_scope", {});
=======
            if (node->as.block.nodes[i]->type == NODE_RETURN ||
                node->as.block.nodes[i]->type == NODE_THROW) break;
        }
        cg.callRT("rt_exit_scope", {});
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
        cg.popScope();
        break;

    case NODE_IF: {
        llvm::Value* cv = codegenExpr(cg, node->as.if_stmt.cond);
        llvm::Value* cond = cg.B->CreateICmpNE(cg.callRT("rt_is_truthy", {cv}), cg.i32Val(0), "ifcond");
        llvm::Function* F = cg.curFunc;
        llvm::BasicBlock* thenBB = llvm::BasicBlock::Create(cg.ctx, "then", F);
        llvm::BasicBlock* elseBB = llvm::BasicBlock::Create(cg.ctx, "else", F);
        llvm::BasicBlock* mergeBB = llvm::BasicBlock::Create(cg.ctx, "ifcont", F);
        cg.B->CreateCondBr(cond, thenBB, elseBB);
        cg.B->SetInsertPoint(thenBB);
        codegenStmt(cg, node->as.if_stmt.then_b);
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(mergeBB);
        cg.B->SetInsertPoint(elseBB);
        if (node->as.if_stmt.else_b) codegenStmt(cg, node->as.if_stmt.else_b);
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(mergeBB);
        cg.B->SetInsertPoint(mergeBB);
        break;
    }

    case NODE_WHILE: {
        llvm::Function* F = cg.curFunc;
        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(cg.ctx, "while.cond", F);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(cg.ctx, "while.body", F);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(cg.ctx, "while.exit", F);
        cg.B->CreateBr(condBB);
        cg.B->SetInsertPoint(condBB);
        llvm::Value* cv = codegenExpr(cg, node->as.while_stmt.cond);
        cg.B->CreateCondBr(cg.B->CreateICmpNE(cg.callRT("rt_is_truthy", {cv}), cg.i32Val(0)), bodyBB, exitBB);
        cg.loopStack.push_back({condBB, exitBB, condBB});
        cg.B->SetInsertPoint(bodyBB);
        codegenStmt(cg, node->as.while_stmt.body);
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(condBB);
        cg.loopStack.pop_back();
        cg.B->SetInsertPoint(exitBB);
        break;
    }

    case NODE_FOR_IN: {
        llvm::Function* F = cg.curFunc;
        cg.pushScope();
        cg.callRT("rt_enter_scope", {});
        llvm::Value* iterable = codegenExpr(cg, node->as.for_in.iterable);
        llvm::AllocaInst* iterA = cg.createEntryAlloca(F, "$iter");
        cg.B->CreateStore(iterable, iterA);
        llvm::AllocaInst* counterA = cg.createEntryAlloca(F, "$idx");
        cg.B->CreateStore(llvm::ConstantInt::get(cg.i64Ty, 0), counterA);
        llvm::AllocaInst* varA = cg.createEntryAlloca(F, node->as.for_in.var_name);
        cg.B->CreateStore(cg.makeNull(), varA);
        cg.setLocal(node->as.for_in.var_name, varA);

        llvm::BasicBlock* condBB = llvm::BasicBlock::Create(cg.ctx, "for.cond", F);
        llvm::BasicBlock* bodyBB = llvm::BasicBlock::Create(cg.ctx, "for.body", F);
        llvm::BasicBlock* incrBB = llvm::BasicBlock::Create(cg.ctx, "for.incr", F);
        llvm::BasicBlock* exitBB = llvm::BasicBlock::Create(cg.ctx, "for.exit", F);
        cg.B->CreateBr(condBB);

        cg.B->SetInsertPoint(condBB);
        llvm::Value* iter = cg.B->CreateLoad(cg.i64Ty, iterA);
        llvm::Value* idx = cg.B->CreateLoad(cg.i64Ty, counterA);
        llvm::Value* hasNext = cg.callRT("rt_for_in_has_next", {iter, idx});
        cg.B->CreateCondBr(cg.B->CreateICmpNE(hasNext, cg.i32Val(0)), bodyBB, exitBB);

        cg.B->SetInsertPoint(bodyBB);
        llvm::Value* step = cg.callRT("rt_for_in_step", {iter, counterA});
        cg.B->CreateStore(step, varA);
        cg.loopStack.push_back({condBB, exitBB, incrBB});
        codegenStmt(cg, node->as.for_in.body);
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(incrBB);
        cg.loopStack.pop_back();

        cg.B->SetInsertPoint(incrBB);
        cg.B->CreateBr(condBB);

        cg.B->SetInsertPoint(exitBB);
        cg.callRT("rt_exit_scope", {});
        cg.popScope();
        break;
    }

    case NODE_FUNC_DECL: {
        const char* fname = node->as.func_decl.name;
        auto it = cg.userFuncs.find(fname);
        if (it == cg.userFuncs.end()) break;
        llvm::Function* fn = it->second;
        llvm::Function* savedFunc = cg.curFunc;
        cg.curFunc = fn;
        llvm::BasicBlock* entry = llvm::BasicBlock::Create(cg.ctx, "entry", fn);
        cg.B->SetInsertPoint(entry);
        cg.pushScope();
        int pi = 0;
        for (auto& arg : fn->args()) {
            std::string pname = node->as.func_decl.params[pi].name;
            llvm::AllocaInst* a = cg.createEntryAlloca(fn, pname);
            cg.B->CreateStore(&arg, a);
            cg.setLocal(pname, a);
            pi++;
        }
        ASTNode* body = node->as.func_decl.body;
        if (body && body->type == NODE_BLOCK) {
            for (int i = 0; i < body->as.block.count; i++)
                codegenStmt(cg, body->as.block.nodes[i]);
        } else codegenStmt(cg, body);
        if (!cg.B->GetInsertBlock()->getTerminator())
            cg.B->CreateRet(cg.makeNull());
        cg.popScope();
        cg.curFunc = savedFunc;
        break;
    }

    case NODE_RETURN: {
        llvm::Value* retVal = node->as.child ? codegenExpr(cg, node->as.child) : cg.makeNull();
        cg.B->CreateRet(retVal);
        break;
    }

    case NODE_THROW: {
        llvm::Value* val = codegenExpr(cg, node->as.child);
        cg.callRT("rt_throw", {val});
        cg.B->CreateUnreachable();
        break;
    }

    case NODE_FREE:
        cg.callRT("rt_free_val", {codegenExpr(cg, node->as.child)});
        break;

    case NODE_TRY_CATCH: {
        llvm::Function* F = cg.curFunc;
        llvm::BasicBlock* tryBB   = llvm::BasicBlock::Create(cg.ctx, "try", F);
        llvm::BasicBlock* catchBB = llvm::BasicBlock::Create(cg.ctx, "catch", F);
        llvm::BasicBlock* endBB   = llvm::BasicBlock::Create(cg.ctx, "tryend", F);
<<<<<<< HEAD

        /* Get pointer to current jmp_buf slot, call _setjmp with frame addr */
        llvm::Value* jbPtr = cg.callRT("rt_get_jmpbuf", {});
        llvm::Function* frameAddrFn = llvm::Intrinsic::getDeclaration(
            cg.mod.get(), llvm::Intrinsic::frameaddress,
            {llvm::PointerType::getUnqual(cg.ctx)});
        llvm::Value* frameAddr = cg.B->CreateCall(frameAddrFn, {cg.i32Val(0)});
        llvm::Value* sjResult = cg.callRT("_setjmp", {jbPtr, frameAddr});
        cg.B->CreateCondBr(
            cg.B->CreateICmpEQ(sjResult, cg.i32Val(0)), tryBB, catchBB);

        /* Try body (setjmp returned 0 — normal path) */
        cg.B->SetInsertPoint(tryBB);
        cg.callRT("rt_try_push", {});  /* increment depth */
        codegenStmt(cg, node->as.try_catch.try_body);
        if (!cg.B->GetInsertBlock()->getTerminator()) {
            cg.callRT("rt_try_exit", {});
            cg.B->CreateBr(endBB);
        }

        /* Catch body (setjmp returned 1 via longjmp) */
=======
        llvm::Value* result = cg.callRT("rt_try_enter", {});
        cg.B->CreateCondBr(cg.B->CreateICmpEQ(result, cg.i32Val(1)), catchBB, tryBB);
        cg.B->SetInsertPoint(tryBB);
        codegenStmt(cg, node->as.try_catch.try_body);
        cg.callRT("rt_try_exit", {});
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(endBB);
>>>>>>> edf689ec3e6372e0694493081bf10302d2b11174
        cg.B->SetInsertPoint(catchBB);
        cg.pushScope();
        if (node->as.try_catch.err_var) {
            llvm::Value* err = cg.callRT("rt_caught_val", {});
            llvm::AllocaInst* errA = cg.createEntryAlloca(F, node->as.try_catch.err_var);
            cg.B->CreateStore(err, errA);
            cg.setLocal(node->as.try_catch.err_var, errA);
        }
        codegenStmt(cg, node->as.try_catch.catch_body);
        cg.popScope();
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(endBB);
        cg.B->SetInsertPoint(endBB);
        break;
    }

    case NODE_SWITCH: {
        llvm::Function* F = cg.curFunc;
        llvm::Value* subject = codegenExpr(cg, node->as.switch_stmt.subject);
        llvm::AllocaInst* swA = cg.createEntryAlloca(F, "$sw");
        cg.B->CreateStore(subject, swA);
        int n_cases = node->as.switch_stmt.case_count;
        int def_idx = node->as.switch_stmt.default_idx;
        llvm::BasicBlock* endBB = llvm::BasicBlock::Create(cg.ctx, "sw.end", F);
        cg.loopStack.push_back({nullptr, endBB, nullptr});
        for (int i = 0; i < n_cases; i++) {
            if (i == def_idx) continue;
            llvm::BasicBlock* caseBB = llvm::BasicBlock::Create(cg.ctx, "sw.case", F);
            llvm::BasicBlock* nextBB = llvm::BasicBlock::Create(cg.ctx, "sw.next", F);
            llvm::Value* swVal = cg.B->CreateLoad(cg.i64Ty, swA);
            llvm::Value* caseVal = codegenExpr(cg, node->as.switch_stmt.case_values[i]);
            llvm::Value* eq = cg.callRT("rt_eq", {swVal, caseVal});
            llvm::Value* cond = cg.callRT("rt_is_truthy", {eq});
            cg.B->CreateCondBr(cg.B->CreateICmpNE(cond, cg.i32Val(0)), caseBB, nextBB);
            cg.B->SetInsertPoint(caseBB);
            codegenStmt(cg, node->as.switch_stmt.case_bodies[i]);
            if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(endBB);
            cg.B->SetInsertPoint(nextBB);
        }
        if (def_idx >= 0) codegenStmt(cg, node->as.switch_stmt.case_bodies[def_idx]);
        if (!cg.B->GetInsertBlock()->getTerminator()) cg.B->CreateBr(endBB);
        cg.loopStack.pop_back();
        cg.B->SetInsertPoint(endBB);
        break;
    }

    case NODE_BREAK:
        if (!cg.loopStack.empty()) {
            cg.B->CreateBr(cg.loopStack.back().exit);
            cg.B->SetInsertPoint(llvm::BasicBlock::Create(cg.ctx, "post.break", cg.curFunc));
        }
        break;

    case NODE_CONTINUE:
        if (!cg.loopStack.empty() && cg.loopStack.back().cont_tgt) {
            cg.B->CreateBr(cg.loopStack.back().cont_tgt);
            cg.B->SetInsertPoint(llvm::BasicBlock::Create(cg.ctx, "post.cont", cg.curFunc));
        }
        break;

    case NODE_AUTOFREE:  break;
    case NODE_ALLOW_LEAKS: break;
    case NODE_USE: break;
    case NODE_PROGRAM: break;

    default:
        codegenExpr(cg, node);
        break;
    }
}

/* ══════════════════════════════════════════════════════════════════
 *  Top-level program codegen
 * ══════════════════════════════════════════════════════════════════ */

static void codegenProgram(Codegen& cg, ASTNode* program) {
    if (program->type != NODE_PROGRAM) return;

    llvm::FunctionType* initFT = llvm::FunctionType::get(cg.voidTy, false);
    llvm::Function* initFn = llvm::Function::Create(initFT, llvm::Function::InternalLinkage,
                                                    "__tantrums_init", cg.mod.get());
    llvm::BasicBlock* initEntry = llvm::BasicBlock::Create(cg.ctx, "entry", initFn);
    cg.B->SetInsertPoint(initEntry);
    cg.curFunc = initFn;
    cg.pushScope();

    for (int i = 0; i < program->as.program.count; i++) {
        ASTNode* n = program->as.program.nodes[i];
        /* Function declarations are codegen'd in a second pass below */
        if (n->type == NODE_FUNC_DECL) continue;
        /* Everything else (var decls, expr stmts, if, while, for-in,
         * print, directives...) runs at program startup */
        codegenStmt(cg, n);
    }
    cg.B->CreateRetVoid();

    for (int i = 0; i < program->as.program.count; i++) {
        ASTNode* n = program->as.program.nodes[i];
        if (n->type == NODE_FUNC_DECL) codegenStmt(cg, n);
    }

    /* C main() */
    llvm::FunctionType* mainFT = llvm::FunctionType::get(llvm::Type::getInt32Ty(cg.ctx), false);
    llvm::Function* mainFn = llvm::Function::Create(mainFT, llvm::Function::ExternalLinkage,
                                                    "main", cg.mod.get());
    llvm::BasicBlock* mainEntry = llvm::BasicBlock::Create(cg.ctx, "entry", mainFn);
    cg.B->SetInsertPoint(mainEntry);
    cg.curFunc = mainFn;
    cg.callRT("rt_init", {});
    cg.B->CreateCall(initFn);
    auto it = cg.userFuncs.find("main");
    if (it != cg.userFuncs.end()) cg.B->CreateCall(it->second);
    cg.callRT("rt_shutdown", {});
    cg.B->CreateRet(llvm::ConstantInt::get(llvm::Type::getInt32Ty(cg.ctx), 0));
}

/* ══════════════════════════════════════════════════════════════════
 *  Public API
 * ══════════════════════════════════════════════════════════════════ */

bool llvm_codegen_compile(ASTNode* program, CompileMode mode,
                          const char* source_path,
                          const std::string& outputObj) {
    /* Use native target init — InitializeAllTargets doesn't reliably
     * pull in target libraries when linking statically. */
    llvm::InitializeNativeTarget();
    llvm::InitializeNativeTargetAsmPrinter();
    llvm::InitializeNativeTargetAsmParser();

    Codegen cg;
    cg.mode = mode;
    cg.mod = std::make_unique<llvm::Module>("tantrums", cg.ctx);
    cg.B = std::make_unique<llvm::IRBuilder<>>(cg.ctx);
    cg.i64Ty   = llvm::Type::getInt64Ty(cg.ctx);
    cg.i32Ty   = llvm::Type::getInt32Ty(cg.ctx);
    cg.i8PtrTy = llvm::PointerType::getUnqual(cg.ctx);
    cg.voidTy  = llvm::Type::getVoidTy(cg.ctx);

    declareRuntimeFunctions(cg);
    prescan(cg, program);
    codegenProgram(cg, program);

    std::string verifyErr;
    llvm::raw_string_ostream verifyOS(verifyErr);
    if (llvm::verifyModule(*cg.mod, &verifyOS)) {
        fprintf(stderr, "[Tantrums] LLVM verify error:\n%s\n", verifyErr.c_str());
        return false;
    }

#ifdef TANTRUMS_TARGET_TRIPLE
    llvm::Triple triple(std::string(TANTRUMS_TARGET_TRIPLE));
#else
    llvm::Triple triple(llvm::sys::getDefaultTargetTriple());
#endif
    cg.mod->setTargetTriple(triple);

    std::string err;
    const llvm::Target* target = llvm::TargetRegistry::lookupTarget(triple.str(), err);
    if (!target) { fprintf(stderr, "[Tantrums] Target lookup failed: %s\n", err.c_str()); return false; }

    llvm::TargetOptions opt;
    auto TM = target->createTargetMachine(triple, "generic", "", opt, llvm::Reloc::PIC_);
    cg.mod->setDataLayout(TM->createDataLayout());

    /* Optimize O2 */
    {
        llvm::LoopAnalysisManager LAM;
        llvm::FunctionAnalysisManager FAM;
        llvm::CGSCCAnalysisManager CGAM;
        llvm::ModuleAnalysisManager MAM;
        llvm::PassBuilder PB(TM);
        PB.registerModuleAnalyses(MAM);
        PB.registerCGSCCAnalyses(CGAM);
        PB.registerFunctionAnalyses(FAM);
        PB.registerLoopAnalyses(LAM);
        PB.crossRegisterProxies(LAM, FAM, CGAM, MAM);
        auto MPM = PB.buildPerModuleDefaultPipeline(llvm::OptimizationLevel::O2);
        MPM.run(*cg.mod, MAM);
    }

    /* Emit .obj */
    std::error_code EC;
    llvm::raw_fd_ostream dest(outputObj, EC, llvm::sys::fs::OF_None);
    if (EC) { fprintf(stderr, "[Tantrums] Could not open output: %s\n", EC.message().c_str()); return false; }

    llvm::legacy::PassManager pass;
    if (TM->addPassesToEmitFile(pass, dest, nullptr, llvm::CodeGenFileType::ObjectFile)) {
        fprintf(stderr, "[Tantrums] Target cannot emit object files.\n");
        return false;
    }
    pass.run(*cg.mod);
    dest.flush();
    printf("[Tantrums] Emitted object: %s\n", outputObj.c_str());
    return true;
}

bool llvm_codegen_link(const std::string& objPath,
                       const std::string& exePath) {
    std::string runtimeLib = TANTRUMS_RUNTIME_OBJ;
    /* Use clang++ as linker driver — it automatically includes CRT startup
     * objects (mainCRTStartup) and links the correct CRT libraries.
     * Bare lld-link doesn't do this, causing pre-main crashes. */
    std::string cmd = "clang++";
    cmd += " --target=x86_64-pc-windows-msvc";
    cmd += " -fuse-ld=lld";
    cmd += " \"" + objPath + "\"";
    cmd += " \"" + runtimeLib + "\"";
    cmd += " -o \"" + exePath + "\"";
    int rc = system(cmd.c_str());
    if (rc != 0) {
        fprintf(stderr, "[Tantrums] Linking failed (exit %d).\n", rc);
        return false;
    }
    printf("[Tantrums] Linked: %s\n", exePath.c_str());
    return true;
}
