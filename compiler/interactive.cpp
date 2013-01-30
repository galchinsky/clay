#include "clay.hpp"
#include "interactive.hpp"
#include "lexer.hpp"
#include "parser.hpp"
#include "codegen.hpp"
#include "error.hpp"
#include "loader.hpp"
#include "invoketables.hpp"
#include "env.hpp"

#include <setjmp.h>
#include <signal.h>
#include <stdio.h>

namespace clay {

    typedef llvm::SmallString<16U> Str;

    const char* replAnonymousFunctionName = "__replAnonymousFunction__";

    jmp_buf recovery;

    static bool printAST = false;

    static void eval(llvm::StringRef code, JitContext* ctx);

    string newFunctionName()
    {
        static int funNum = 0;
        string buf;
        llvm::raw_string_ostream funName(buf);
        funName << replAnonymousFunctionName << funNum;
        ++funNum;
        return funName.str();
    }

    string stripSpaces(string const& s)
    {
        size_t i;
        for (i = 0; i < s.size(); ++i) {
            if (!isSpace(s[i]))
                break;
        }
        return s.substr(i, s.length());
    }

    static vector<Token> addTokens() {
        char buf[255];
        string line = fgets(buf, 255, stdin);
        line = stripSpaces(line);
        SourcePtr source = new Source(line, 0);
        vector<Token> tokens;
        tokenize(source, 0, line.length(), tokens);
        return tokens;
    }
  
    static void cmdGlobals(const vector<Token>& tokens, JitContext* ctx) {
        ModulePtr m;
        if (tokens.size() == 1) {
            m = ctx->module;
        } else if (tokens.size() == 2) {
            m = ctx->cst->globalModules[tokens[1].str];
        } else {
            llvm::errs() << ":globals [module name]\n";
            return;
        }

        string buf;
        llvm::raw_string_ostream code(buf);

        llvm::StringMap<ObjectPtr>::const_iterator g = m->globals.begin();
        for (; g != m->globals.end(); ++g) {
            if (g->second->objKind == GLOBAL_VARIABLE) {
                IdentifierPtr name = ((GlobalVariable*)g->second.ptr())->name;
                code << "println(\"" << name->str << " : \", " 
                                     << "Type(" << name->str << "), \" : \", "
                                     << name->str <<");\n";
            }
        }
        code.flush();
        eval(buf, ctx);
    }

    static void cmdModules(const vector<Token>& tokens, JitContext* ctx) {
        if (tokens.size() != 1) {
            llvm::errs() << "Warning: command parameters are ignored\n";
        }
        llvm::StringMap<ModulePtr>::const_iterator iter = 
            ctx->cst->globalModules.begin();
        for (; iter != ctx->cst->globalModules.end(); ++iter) {
            llvm::errs() << iter->getKey() << "\n";
        }
    }

    static void cmdOverloads(const vector<Token>& tokens, JitContext* ctx) {
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i].tokenKind == T_IDENTIFIER) {
                Str identStr = tokens[i].str;

                ObjectPtr obj = lookupPrivate(ctx->module, Identifier::get(identStr));
                if (obj == NULL || obj->objKind != PROCEDURE) {
                    llvm::errs() << identStr << " is not a procedure name\n";
                    continue;
                }

                vector<InvokeSet*> sets = lookupInvokeSets(obj.ptr(), ctx->cst);
                for (size_t k = 0; k < sets.size(); ++k) {
                    llvm::errs() << "        ";
                    for (size_t l = 0; l < sets[k]->argsKey.size(); ++l) {
                        llvm::errs() << sets[k]->argsKey[l] << " : ";
                    }
                    llvm::errs() << "\n";
                }
            }
        }
    }

    static void cmdPrint(const vector<Token>& tokens, JitContext* ctx) {
        for (size_t i = 1; i < tokens.size(); ++i) {
            if (tokens[i].tokenKind == T_IDENTIFIER) {
                Str identifier = tokens[i].str;
                llvm::StringMap<ImportSet>::const_iterator iter = 
                    ctx->module->allSymbols.find(identifier);
                if (iter == ctx->module->allSymbols.end()) {
                    llvm::errs() << "Can't find identifier " << identifier.c_str();
                } else {
                    for (size_t i = 0; i < iter->second.size(); ++i) {
                        llvm::errs() << iter->second[i] << "\n";
                    }
                }
            }
        }
    }

    static void replCommand(string const& line, JitContext* ctx)
    {
        SourcePtr source = new Source(line, 0);
        vector<Token> tokens;
        //TODO: don't use compiler's tokenizer
        tokenize(source, 0, line.length(), tokens);
        Str cmd = tokens[0].str;
        if (cmd == "q") {
            exit(0);
        } else if (cmd == "globals") {
            cmdGlobals(tokens, ctx);
        } else if (cmd == "modules") {
            cmdModules(tokens, ctx);
        } else if (cmd == "overloads") {
            cmdOverloads(tokens, ctx);
        } else if (cmd == "print") {
            cmdPrint(tokens, ctx);
        } else if (cmd == "ast_on") {
            printAST = true;
        } else if (cmd == "ast_off") {
            printAST = false;
        } else if (cmd == "rebuild") {
            //TODO : this command should re-codegen everything
        }
        else {
            llvm::errs() << "Unknown interactive mode command: " << cmd.c_str() << "\n";
        }
    }

    static void loadImports(llvm::ArrayRef<ImportPtr> imports, JitContext* ctx)
    {
        for (size_t i = 0; i < imports.size(); ++i) {
            ctx->module->imports.push_back(imports[i]);
        }
        for (size_t i = 0; i < imports.size(); ++i) {
            loadDependent(ctx->module, NULL, imports[i], false);
        }
        for (size_t i = 0; i < imports.size(); ++i) {
            initModule(imports[i]->module);
        }
    }

    static void jitTopLevel(llvm::ArrayRef<TopLevelItemPtr> toplevels, 
                            JitContext* ctx)
    {
        if (toplevels.empty()) {
            return;
        }
        if (printAST) {
            for (size_t i = 0; i < toplevels.size(); ++i) {
                llvm::errs() << i << ": " << toplevels[i] << "\n";
            }
        }
        addGlobals(ctx->module, toplevels);
    }

    void jitStatements(llvm::ArrayRef<StatementPtr> statements,
                       JitContext* ctx)
    {
        CompilerState* cst = ctx->module->cst;
        if (statements.empty()) {
            return;
        }

        if (printAST) {
            for (size_t i = 0; i < statements.size(); ++i) {
                llvm::errs() << statements[i] << "\n";
            }
        }

        IdentifierPtr fun = Identifier::get(newFunctionName());

        BlockPtr funBody = new Block(statements);
        ExternalProcedurePtr entryProc =
                new ExternalProcedure(ctx->module.ptr(),
                                      fun,
                                      PRIVATE,
                                      vector<ExternalArgPtr>(),
                                      false,
                                      NULL,
                                      funBody.ptr(),
                                      new ExprList(), ctx->cst);

        entryProc->env = ctx->module->env;

        codegenBeforeRepl(ctx->module);
        try {
            codegenExternalProcedure(entryProc, true);
        }
        catch (std::exception) {
            return;
        }

        llvm::Function* ctor;
        llvm::Function* dtor;
        codegenAfterRepl(ctor, dtor, cst);

        ctx->engine->runFunction(ctor, std::vector<llvm::GenericValue>());

        void* dtorLlvmFun = ctx->engine->getPointerToFunction(dtor);
        typedef void (*PFN)();
        atexit((PFN)(uintptr_t)dtorLlvmFun);
        ctx->engine->runFunction(entryProc->llvmFunc, 
                                 std::vector<llvm::GenericValue>());
    }

    static void jitAndPrintExpr(ExprPtr expr, JitContext* ctx) {
        //expr -> println(expr);
        NameRefPtr println = new NameRef(Identifier::get("println"));
        ExprPtr call = new Call(println.ptr(), new ExprList(expr));
        ExprStatementPtr callStmt = new ExprStatement(call);
        jitStatements(vector<StatementPtr>(1, callStmt.ptr()), ctx);
    }

    static void eval(llvm::StringRef line, JitContext* ctx) {
        SourcePtr source = new Source(line, 0);
        try {
            ReplItem x = parseInteractive(source, 0, source->size(), addTokens, 
                                          ctx->cst);
            if (x.isExprSet) {
                jitAndPrintExpr(x.expr, ctx);
            } else {
                loadImports(x.imports, ctx);
                jitTopLevel(x.toplevels, ctx);
                jitStatements(x.stmts, ctx);
            }
        }
        catch (CompilerError) {
            return;
        }        
    }

    static void interactiveLoop(JitContext* ctx)
    {
        setjmp(recovery);
        string line;
        while(true) {
            llvm::errs().flush();
            llvm::errs() << "clay>";
            char buf[255];
            line = fgets(buf, 255, stdin);
            line = stripSpaces(line);
            if (line[0] == ':') {
                replCommand(line.substr(1, line.size() - 1), ctx);
            } else {
                eval(line, ctx);
            }
        }
        ctx->engine->runStaticConstructorsDestructors(true);
    }

    static void exceptionHandler(int i)
    {
        llvm::errs() << "SIGABRT called\n";
        longjmp(recovery, 1);
    }



    void runInteractive(JitContext* ctx)
    {
        signal(SIGABRT, exceptionHandler);

        llvm::errs() << "Clay interpreter\n";
        llvm::errs() << ":q to exit\n";
        llvm::errs() << ":print {identifier} to print an identifier\n";
        llvm::errs() << ":modules to list global modules\n";
        llvm::errs() << ":globals to list globals\n";
        llvm::errs() << "In multi-line mode empty line to exit\n";

        interactiveLoop(ctx);
    }

}
