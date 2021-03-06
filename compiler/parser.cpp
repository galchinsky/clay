#include "clay.hpp"
#include "parser.hpp"
#include "codegen.hpp"

namespace clay {

map<llvm::StringRef, IdentifierPtr> Identifier::freeIdentifiers;

struct ParserImpl {
CompilerState* currentCompiler;

vector<Token> *tokens;
unsigned position;
unsigned maxPosition;
bool parserOptionKeepDocumentation;

AddTokensCallback addTokens;

bool inRepl;

ParserImpl(CompilerState* cst, AddTokensCallback f = NULL) :
    currentCompiler(cst), addTokens(f), inRepl(false),  
    parserOptionKeepDocumentation(false) {
}

bool next(Token *&x) {
    if (position == tokens->size()) {
        if (inRepl) {
            assert(addTokens != NULL);
            vector<Token> toks = addTokens();
            if (toks.size() == 0) {
                inRepl = false;
                return false;
            }
            tokens->insert(tokens->end(), toks.begin(), toks.end());
        } else {
            return false;
        }
    }
    x = &(*tokens)[position];
    if (position > maxPosition)
        maxPosition = position;
    ++position;
    return true;
}

unsigned save() {
    return position;
}

void restore(unsigned p) {
    position = p;
}

Location currentLocation() {
    if (position == tokens->size())
        return Location();
    return (*tokens)[position].location;
}



//
// symbol, keyword
//

bool opstring(llvm::StringRef &op) {
    Token *t;
    if (!next(t) || (t->tokenKind != T_OPSTRING))
        return false;
    op = llvm::StringRef(t->str);
    return true;
}

bool uopstring(llvm::StringRef &op) {
    Token* t;
    if (!next(t) || (t->tokenKind != T_UOPSTRING))
        return false;
    op = llvm::StringRef(t->str);
    return true;
}

bool opsymbol(const char *s) {
    Token* t;
    if (!next(t) || (t->tokenKind != T_OPSTRING))
        return false;
    return t->str == s;
}

bool symbol(const char *s) {
    Token* t;
    if (!next(t) || (t->tokenKind != T_SYMBOL))
        return false;
    return t->str == s;
}

bool keyword(const char *s) {
    Token* t;
    if (!next(t) || (t->tokenKind != T_KEYWORD))
        return false;
    return t->str == s;
}

bool ellipsis() {
    return symbol("..");
}


//
// identifier, identifierList,
// identifierListNoTail, dottedName
//

bool identifier(IdentifierPtr &x) {
    Location location = currentLocation();
    Token* t;
    if (!next(t) || (t->tokenKind != T_IDENTIFIER))
        return false;
    x = Identifier::get(t->str, location);
    return true;
}

bool identifierList(vector<IdentifierPtr> &x) {
    IdentifierPtr y;
    if (!identifier(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",")) {
            restore(p);
            break;
        }
        p = save();
        if (!identifier(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool identifierListNoTail(vector<IdentifierPtr> &x) {
    IdentifierPtr y;
    if (!identifier(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !identifier(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool dottedName(DottedNamePtr &x) {
    Location location = currentLocation();
    DottedNamePtr y = new DottedName();
    IdentifierPtr ident;
    if (!identifier(ident)) return false;
    while (true) {
        y->parts.push_back(ident);
        unsigned p = save();
        if (!symbol(".") || !identifier(ident)) {
            restore(p);
            break;
        }
    }
    x = y;
    x->location = location;
    return true;
}



//
// literals
//

bool boolLiteral(ExprPtr &x) {
    Location location = currentLocation();
    unsigned p = save();
    if (keyword("true"))
        x = new BoolLiteral(true);
    else if (restore(p), keyword("false"))
        x = new BoolLiteral(false);
    else
        return false;
    x->location = location;
    return true;
}

string cleanNumericSeparator(llvm::StringRef op, llvm::StringRef s) {
    string out;
    if (op == "-")
        out.push_back('-');
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] != '_')
            out.push_back(s[i]);
    }
    return out;
}

bool intLiteral(llvm::StringRef op, ExprPtr &x) {
    Location location = currentLocation();
    Token* t;
    if (!next(t) || (t->tokenKind != T_INT_LITERAL))
        return false;
    Token* t2;
    unsigned p = save();
    if (next(t2) && (t2->tokenKind == T_IDENTIFIER)) {
        x = new IntLiteral(cleanNumericSeparator(op, t->str), t2->str);
    }
    else {
        restore(p);
        x = new IntLiteral(cleanNumericSeparator(op, t->str));
    }
    x->location = location;
    return true;
}

bool floatLiteral(llvm::StringRef op, ExprPtr &x) {
    Location location = currentLocation();
    Token* t;
    if (!next(t) || (t->tokenKind != T_FLOAT_LITERAL))
        return false;
    Token* t2;
    unsigned p = save();
    if (next(t2) && (t2->tokenKind == T_IDENTIFIER)) {
        x = new FloatLiteral(cleanNumericSeparator(op, t->str), t2->str);
    }
    else {
        restore(p);
        x = new FloatLiteral(cleanNumericSeparator(op, t->str));
    }
    x->location = location;
    return true;
}


bool charLiteral(ExprPtr &x) {
    Location location = currentLocation();
    Token* t;
    if (!next(t) || (t->tokenKind != T_CHAR_LITERAL))
        return false;
    x = new CharLiteral(t->str[0]);
    x->location = location;
    return true;
}

bool stringLiteral(ExprPtr &x) {
    Location location = currentLocation();
    Token* t;
    if (!next(t) || (t->tokenKind != T_STRING_LITERAL))
        return false;
    IdentifierPtr id = Identifier::get(t->str, location);
    x = new StringLiteral(id);
    x->location = location;
    return true;
}

bool literal(ExprPtr &x) {
    unsigned p = save();
    if (boolLiteral(x)) return true;
    if (restore(p), intLiteral("+", x)) return true;
    if (restore(p), floatLiteral("+", x)) return true;
    if (restore(p), charLiteral(x)) return true;
    if (restore(p), stringLiteral(x)) return true;
    return false;
}



//
// expression misc
//

bool optExpression(ExprPtr &x) {
    unsigned p = save();
    if (!expression(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool expressionList(ExprListPtr &x, bool = false) {
    ExprListPtr a;
    ExprPtr b;
    if (!expression(b)) return false;
    a = new ExprList(b);
    while (true) {
        unsigned p = save();
        if (!symbol(",")) {
            restore(p);
            break;
        }
        p = save();
        if (!expression(b)) {
            restore(p);
            break;
        }
        a->add(b);
    }
    x = a;
    return true;
}

bool optExpressionList(ExprListPtr &x) {
    unsigned p = save();
    if (!expressionList(x)) {
        restore(p);
        x = new ExprList();
    }
    return true;
}



//
// atomic expr
//

bool tupleExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("[")) return false;
    ExprListPtr args;
    if (!optExpressionList(args)) return false;
    if (!symbol("]")) return false;
    x = new Tuple(args);
    x->location = location;
    return true;
}

bool parenExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("(")) return false;
    ExprListPtr args;
    if (!optExpressionList(args)) return false;
    if (!symbol(")")) return false;
    x = new Paren(args);
    x->location = location;
    return true;
}

bool nameRef(ExprPtr &x) {
    Location location = currentLocation();
    IdentifierPtr a;
    if (!identifier(a)) return false;
    x = new NameRef(a);
    x->location = location;
    return true;
}

bool fileExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!keyword("__FILE__")) return false;
    x = new FILEExpr();
    x->location = location;
    return true;
}

bool lineExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!keyword("__LINE__")) return false;
    x = new LINEExpr();
    x->location = location;
    return true;
}

bool columnExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!keyword("__COLUMN__")) return false;
    x = new COLUMNExpr();
    x->location = location;
    return true;
}

bool argExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!keyword("__ARG__")) return false;
    IdentifierPtr name;
    if (!identifier(name)) return false;
    x = new ARGExpr(name);
    x->location = location;
    return true;
}

bool evalExpr(ExprPtr &ev) {
    Location location = currentLocation();
    if (!keyword("eval")) return false;
    ExprPtr args;
    if (!expression(args)) return false;
    ev = new EvalExpr(args);
    ev->location = location;
    return true;
}

bool atomicExpr(ExprPtr &x) {
    unsigned p = save();
    if (nameRef(x)) return true;
    if (restore(p), parenExpr(x)) return true;
    if (restore(p), literal(x)) return true;
    if (restore(p), tupleExpr(x)) return true;
    if (restore(p), fileExpr(x)) return true;
    if (restore(p), lineExpr(x)) return true;
    if (restore(p), columnExpr(x)) return true;
    if (restore(p), argExpr(x)) return true;
    if (restore(p), evalExpr(x)) return true;
    return false;
}



//
// suffix expr
//

bool stringLiteralSuffix(ExprPtr &x) {
    Location location = currentLocation();
    ExprPtr str;
    if (!stringLiteral(str)) return false;
    ExprListPtr strArgs = new ExprList(str);
    x = new Call(NULL, strArgs);
    x->location = location;
    return true;
}

bool indexingSuffix(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("[")) return false;
    ExprListPtr args;
    if (!optExpressionList(args)) return false;
    if (!symbol("]")) return false;
    x = new Indexing(NULL, args);
    x->location = location;
    return true;
}

bool callSuffix(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("(")) return false;
    ExprListPtr args;
    if (!optExpressionList(args)) return false;
    if (!symbol(")")) return false;
    x = new Call(NULL, args);
    x->location = location;
    return true;
}

bool fieldRefSuffix(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol(".")) return false;
    IdentifierPtr a;
    if (!identifier(a)) return false;
    x = new FieldRef(NULL, a);
    x->location = location;
    return true;
}

bool staticIndexingSuffix(ExprPtr &x) {
    Location location = currentLocation();
    Token* t;
    if (!next(t) || (t->tokenKind != T_STATIC_INDEX))
        return false;
    char *b = const_cast<char *>(t->str.c_str());
    char *end = b;
    unsigned long c = strtoul(b, &end, 0);
    if (*end != 0)
        error(t, "invalid static index value");
    x = new StaticIndexing(NULL, (size_t)c);
    x->location = location;
    return true;
}

bool dereferenceSuffix(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("^")) return false;
    x = new VariadicOp(DEREFERENCE, new ExprList());
    x->location = location;
    return true;
}

bool suffix(ExprPtr &x) {
    unsigned p = save();
    if (stringLiteralSuffix(x)) return true;
    if (restore(p), indexingSuffix(x)) return true;
    if (restore(p), callSuffix(x)) return true;
    if (restore(p), fieldRefSuffix(x)) return true;
    if (restore(p), staticIndexingSuffix(x)) return true;
    if (restore(p), dereferenceSuffix(x)) return true;
    return false;
}

void setSuffixBase(Expr *a, ExprPtr base) {
    switch (a->exprKind) {
    case INDEXING : {
        Indexing *b = (Indexing *)a;
        b->expr = base;
        break;
    }
    case CALL : {
        Call *b = (Call *)a;
        b->expr = base;
        break;
    }
    case FIELD_REF : {
        FieldRef *b = (FieldRef *)a;
        b->expr = base;
        break;
    }
    case STATIC_INDEXING : {
        StaticIndexing *b = (StaticIndexing *)a;
        b->expr = base;
        break;
    }
    case VARIADIC_OP : {
        VariadicOp *b = (VariadicOp *)a;
        assert(b->op == DEREFERENCE);
        b->exprs->add(base);
        break;
    }
    default :
        assert(false);
    }
}

bool suffixExpr(ExprPtr &x) {
    if (!atomicExpr(x)) return false;
    while (true) {
        unsigned p = save();
        ExprPtr y;
        if (!suffix(y)) {
            restore(p);
            break;
        }
        setSuffixBase(y.ptr(), x);
        x = y;
    }
    return true;
}



//
// prefix expr
//

bool addressOfExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("@")) return false;
    ExprPtr a;
    if (!prefixExpr(a)) return false;
    x = new VariadicOp(ADDRESS_OF, new ExprList(a));
    x->location = location;
    return true;
}

bool plusOrMinus(llvm::StringRef &op) {
    unsigned p = save();
    if (opsymbol("+")) {
        op = llvm::StringRef("+");
        return true;
    } else if (restore(p), opsymbol("-")) {
        op = llvm::StringRef("-");
        return true;
    } else
        return false;
}

bool signedLiteral(llvm::StringRef op, ExprPtr &x) {
    unsigned p = save();
    if (restore(p), intLiteral(op, x)) return true;
    if (restore(p), floatLiteral(op, x)) return true;
    return false;
}

bool dispatchExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!opsymbol("*")) return false;
    ExprPtr a;
    if (!prefixExpr(a)) return false;
    x = new DispatchExpr(a);
    x->location = location;
    return true;
}

bool staticExpr(ExprPtr &x) {
    Location location = currentLocation();
    if (!symbol("#")) return false;
    ExprPtr y;
    if (!prefixExpr(y)) return false;
    x = new StaticExpr(y);
    x->location = location;
    return true;
}


bool operatorOp(llvm::StringRef &op) {
    unsigned p = save();

    const char *s[] = {
        "<--", "-->", "=>", "->", "~>", "=", NULL
    };
    for (const char **a = s; *a; ++a) {
        if (opsymbol(*a)) return false;
        restore(p);
    }
    if (!opstring(op)) return false;
    return true;
}

bool preopExpr(ExprPtr &x) {
    Location location = currentLocation();
    llvm::StringRef op;
    unsigned p = save();
    if (plusOrMinus(op)) {
        if (signedLiteral(op, x)) return true;
    }
    restore(p);
    if (!operatorOp(op)) return false;
    ExprPtr y;
    if (!prefixExpr(y)) return false;
    ExprListPtr exprs = new ExprList(new NameRef(Identifier::get(op)));
    exprs->add(y);
    x = new VariadicOp(PREFIX_OP, exprs);
    x->location = location;
    return true;
}

bool prefixExpr(ExprPtr &x) {
    unsigned p = save();
    if (addressOfExpr(x)) return true;
    if (restore(p), dispatchExpr(x)) return true;
    if (restore(p), preopExpr(x)) return true;
    if (restore(p), staticExpr(x)) return true;
    if (restore(p), suffixExpr(x)) return true;
    return false;
}


//
// infix binary operator expr
//

bool operatorTail(VariadicOpPtr &x) {
    Location location = currentLocation();
    ExprListPtr exprs = new ExprList();
    ExprPtr b;
    llvm::StringRef op;
    if (!operatorOp(op)) return false;
    unsigned p = save();
    while (true) {
        if (!prefixExpr(b)) return false;
        exprs->add(new NameRef(Identifier::get(op)));
        exprs->add(b);
        p = save();
        if (!operatorOp(op)) {
            restore(p);
            break;
        }
    }
    x = new VariadicOp(INFIX_OP, exprs);
    x->location = location;
    return true;
}

bool operatorExpr(ExprPtr &x) {
    if (!prefixExpr(x)) return false;
    while (true) {
        unsigned p = save();
        VariadicOpPtr y;
        if (!operatorTail(y)) {
            restore(p);
            break;
        }
        y->exprs->insert(x);
        x = y.ptr();
    }
    return true;
}



//
// not, and, or
//

bool notExpr(ExprPtr &x) {
    Location location = currentLocation();
    unsigned p = save();
    if (!keyword("not")) {
        restore(p);
        return operatorExpr(x);
    }
    ExprPtr y;
    if (!operatorExpr(y)) return false;
    x = new VariadicOp(NOT, new ExprList(y));
    x->location = location;
    return true;
}

bool andExprTail(AndPtr &x) {
    Location location = currentLocation();
    if (!keyword("and")) return false;
    ExprPtr y;
    if (!notExpr(y)) return false;
    x = new And(NULL, y);
    x->location = location;
    return true;
}

bool andExpr(ExprPtr &x) {
    if (!notExpr(x)) return false;
    while (true) {
        unsigned p = save();
        AndPtr y;
        if (!andExprTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}

bool orExprTail(OrPtr &x) {
    Location location = currentLocation();
    if (!keyword("or")) return false;
    ExprPtr y;
    if (!andExpr(y)) return false;
    x = new Or(NULL, y);
    x->location = location;
    return true;
}

bool orExpr(ExprPtr &x) {
    if (!andExpr(x)) return false;
    while (true) {
        unsigned p = save();
        OrPtr y;
        if (!orExprTail(y)) {
            restore(p);
            break;
        }
        y->expr1 = x;
        x = y.ptr();
    }
    return true;
}



//
// ifExpr
//

bool ifExpr(ExprPtr &x) {
    Location location = currentLocation();
    ExprPtr expr;
    if (!keyword("if")) return false;
    if (!symbol("(")) return false;
    if (!expression(expr)) return false;
    ExprListPtr exprs = new ExprList(expr);
    if (!symbol(")")) return false;
    if (!expression(expr)) return false;
    exprs->add(expr);
    if (!keyword("else")) return false;
    if (!expression(expr)) return false;
    exprs->add(expr);
    x = new VariadicOp(IF_EXPR, exprs);
    x->location = location;
    return true;
}



//
// returnKind, returnExprList, returnExpr
//

bool returnKind(ReturnKind &x) {
    unsigned p = save();
    if (keyword("ref")) {
        x = RETURN_REF;
    }
    else if (restore(p), keyword("forward")) {
        x = RETURN_FORWARD;
    }
    else {
        restore(p);
        x = RETURN_VALUE;
    }
    return true;
}

bool returnExprList(ReturnKind &rkind, ExprListPtr &exprs) {
    if (!returnKind(rkind)) return false;
    if (!optExpressionList(exprs)) return false;
    return true;
}

bool returnExpr(ReturnKind &rkind, ExprPtr &expr) {
    if (!returnKind(rkind)) return false;
    if (!expression(expr)) return false;
    return true;
}



//
// lambda
//

bool lambdaArgs(vector<FormalArgPtr> &formalArgs,
                       bool &hasVarArg, bool &hasAsConversion) {
    unsigned p = save();
    IdentifierPtr name;
    if (identifier(name)) {
        formalArgs.clear();
        FormalArgPtr arg = new FormalArg(name, NULL, TEMPNESS_DONTCARE);
        arg->location = name->location;
        formalArgs.push_back(arg);
        return true;
    }
    restore(p);
    if (arguments(formalArgs, hasVarArg, hasAsConversion)) return true;
    restore(p);
    return true;
}

bool lambdaExprBody(StatementPtr &x) {
    Location location = currentLocation();
    ReturnKind rkind;
    ExprPtr expr;
    if (!returnExpr(rkind, expr)) return false;
    x = new Return(rkind, new ExprList(expr));
    x->location = location;
    return true;
}

bool lambdaArrow(LambdaCapture &captureBy) {
    unsigned p = save();
    if (opsymbol("->")) {
        captureBy = REF_CAPTURE;
        return true;
    } 
    else if (restore(p), opsymbol("=>")) {
        captureBy = VALUE_CAPTURE;
        return true;
    }
    else if (restore(p), opsymbol("~>")) {
        captureBy = STATELESS;
        return true;
    }
    return false;
}

bool lambdaBody(StatementPtr &x) {
    unsigned p = save();
    if (lambdaExprBody(x)) return true;
    if (restore(p), block(x)) return true;
    return false;
}

bool lambda(ExprPtr &x) {
    Location location = currentLocation();
    vector<FormalArgPtr> formalArgs;
    bool hasVarArg = false;
    bool hasAsConversion = false;
    LambdaCapture captureBy;
    StatementPtr body;
    if (!lambdaArgs(formalArgs, hasVarArg, hasAsConversion)) return false;
    if (!lambdaArrow(captureBy)) return false;
    if (!lambdaBody(body)) return false;
    x = new Lambda(captureBy, formalArgs, hasVarArg, hasAsConversion, body);
    x->location = location;
    return true;
}



//
// unpack
//

bool unpack(ExprPtr &x) {
    Location location = currentLocation();
    if (!ellipsis()) return false;
    ExprPtr y;
    if (!expression(y)) return false;
    x = new Unpack(y);
    x->location = location;
    return true;
}



//
// pairExpr
//

bool pairExpr(ExprPtr &x) {
    Location location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    if (!symbol(":")) return false;
    ExprPtr z;
    if (!expression(z)) return false;
    ExprPtr ident = new StringLiteral(y);
    ident->location = location;
    ExprListPtr args = new ExprList();
    args->add(ident);
    args->add(z);
    x = new Tuple(args);
    x->location = location;
    return true;
}


//
// expression
//

bool expression(ExprPtr &x, bool = false) {
    Location startLocation = currentLocation();
    unsigned p = save();
    if (restore(p), lambda(x)) goto success;
    if (restore(p), pairExpr(x)) goto success;
    if (restore(p), orExpr(x)) goto success;
    if (restore(p), ifExpr(x)) goto success;
    if (restore(p), unpack(x)) goto success;
    return false;

success:
    x->startLocation = startLocation;
    x->endLocation = currentLocation();
    return true;
}


//
// pattern
//

bool dottedNameRef(ExprPtr &x) {
    if (!nameRef(x)) return false;
    while (true) {
        unsigned p = save();
        ExprPtr y;
        if (!fieldRefSuffix(y)) {
            restore(p);
            break;
        }
        setSuffixBase(y.ptr(), x);
        x = y;
    }
    return true;
}

bool atomicPattern(ExprPtr &x) {
    unsigned p = save();
    if (dottedNameRef(x)) return true;
    if (restore(p), intLiteral("+", x)) return true;
    return false;
}

bool patternSuffix(IndexingPtr &x) {
    Location location = currentLocation();
    if (!symbol("[")) return false;
    ExprListPtr args;
    if (!optExpressionList(args)) return false;
    if (!symbol("]")) return false;
    x = new Indexing(NULL, args);
    x->location = location;
    return true;
}

bool pattern(ExprPtr &x) {
    Location start = currentLocation();
    if (!atomicPattern(x)) return false;
    unsigned p = save();
    IndexingPtr y;
    if (!patternSuffix(y)) {
        restore(p);
        x->startLocation = start;
        x->endLocation = currentLocation();
        return true;
    }
    y->expr = x;
    x = y.ptr();
    x->startLocation = start;
    x->endLocation = currentLocation();
    return true;
}



//
// typeSpec, optTypeSpec, exprTypeSpec, optExprTypeSpec
//

bool typeSpec(ExprPtr &x) {
    if (!symbol(":")) return false;
    return pattern(x);
}

bool optTypeSpec(ExprPtr &x) {
    unsigned p = save();
    if (!typeSpec(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool exprTypeSpec(ExprPtr &x) {
    if (!symbol(":")) return false;
    return expression(x);
}

bool optExprTypeSpec(ExprPtr &x) {
    unsigned p = save();
    if (!exprTypeSpec(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}



//
// statements
//

bool labelDef(StatementPtr &x) {
    Location location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    if (!symbol(":")) return false;
    x = new Label(y);
    x->location = location;
    return true;
}

bool bindingKind(BindingKind &bindingKind) {
    unsigned p = save();
    if (keyword("var"))
        bindingKind = VAR;
    else if (restore(p), keyword("ref"))
        bindingKind = REF;
    else if (restore(p), keyword("alias"))
        bindingKind = ALIAS;
    else if (restore(p), keyword("forward"))
        bindingKind = FORWARD;
    else
        return false;
    return true;
}

bool localBinding(StatementPtr &x) {
    Location location = currentLocation();
    BindingKind bk;
    if (!bindingKind(bk)) return false;
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;
    vector<FormalArgPtr> args;
    bool hasVarArg = false;
    if (!bindingsBody(args, hasVarArg)) return false;
    unsigned p = save();
    if (!symbol(",")) restore(p); 
    if (!opsymbol("=")) return false;
    ExprListPtr z;
    if (!expressionList(z)) return false;
    if (!symbol(";")) return false;
    vector<ObjectPtr> patternTypes;
    x = new Binding(bk, patternVars, patternTypes, predicate, args, z, hasVarArg);
    x->location = location;
    return true;
}

bool blockItem(StatementPtr &x) {
    unsigned p = save();
    if (labelDef(x)) return true;
    if (restore(p), localBinding(x)) return true;
    if (restore(p), statement(x)) return true;
    return false;
}

bool blockItems(vector<StatementPtr> &stmts, bool) {
    while (true) {
        unsigned p = save();
        StatementPtr z;
        if (!blockItem(z)) {
            restore(p);
            break;
        }
        stmts.push_back(z);
    }
    return true;
}

bool statementExpression(vector<StatementPtr> &stmts, ExprPtr &expr) {
    ExprPtr tailExpr;
    StatementPtr stmt;
    while (true) {
        unsigned p = save();
        if (statementExprStatement(stmt)) {
            stmts.push_back(stmt);
        } else if (restore(p), expression(tailExpr)) {
            unsigned q = save();
            if (symbol(";")) {
                stmts.push_back(new ExprStatement(tailExpr));
            } else {
                restore(q);
                expr = tailExpr;
                return true;
            }
        } else {
            return false;
        }
    }
}

bool block(StatementPtr &x) {
    Location location = currentLocation();
    if (!symbol("{")) return false;
    BlockPtr y = new Block();
    if (!blockItems(y->statements, false)) return false;
    if (!symbol("}")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

bool assignment(StatementPtr &x) {
    Location location = currentLocation();
    ExprListPtr y, z;
    if (!expressionList(y)) return false;
    if (!opsymbol("=")) return false;
    if (!expressionList(z)) return false;
    if (!symbol(";")) return false;
    x = new Assignment(y, z);
    x->location = location;
    return true;
}

bool initAssignment(StatementPtr &x) {
    Location location = currentLocation();
    ExprListPtr y, z;
    if (!expressionList(y)) return false;
    if (!opsymbol("<--")) return false;
    if (!expressionList(z)) return false;
    if (!symbol(";")) return false;
    x = new InitAssignment(y, z);
    x->location = location;
    return true;
}

bool prefixUpdate(StatementPtr &x) {
    Location location = currentLocation();
    ExprPtr z;
    llvm::StringRef op;
    if (!uopstring(op)) return false;
    if (!expression(z)) return false;
    if (!symbol(";")) return false;
    ExprListPtr exprs = new ExprList(new NameRef(Identifier::get(op)));
    if (z->exprKind == VARIADIC_OP) {
        VariadicOp *y = (VariadicOp *)z.ptr();
        exprs->add(y->exprs);
    } else {
        exprs->add(z);
    }
    x = new VariadicAssignment(PREFIX_OP, exprs);
    x->location = location;
    return true;
}

bool updateAssignment(StatementPtr &x) {
    Location location = currentLocation();
    ExprPtr y,z;
    if (!expression(y)) return false;
    llvm::StringRef op;
    if (!uopstring(op)) return false;
    if (!expression(z)) return false;
    if (!symbol(";")) return false;
    ExprListPtr exprs = new ExprList(new NameRef(Identifier::get(op)));
    exprs->add(y);
    if (z->exprKind == VARIADIC_OP) {
        VariadicOp *y = (VariadicOp *)z.ptr();
        exprs->add(y->exprs);
    } else {
        exprs->add(z);
    }
    x = new VariadicAssignment(INFIX_OP, exprs);
    x->location = location;
    return true;
}


bool gotoStatement(StatementPtr &x) {
    Location location = currentLocation();
    IdentifierPtr y;
    if (!keyword("goto")) return false;
    if (!identifier(y)) return false;
    if (!symbol(";")) return false;
    x = new Goto(y);
    x->location = location;
    return true;
}

bool returnStatement(StatementPtr &x) {
    Location location = currentLocation();
    if (!keyword("return")) return false;
    ReturnKind rkind;
    ExprListPtr exprs;
    if (!returnExprList(rkind, exprs)) return false;
    if (!symbol(";")) return false;
    x = new Return(rkind, exprs);
    x->location = location;
    return true;
}

bool optElse(StatementPtr &x) {
    unsigned p = save();
    if (!keyword("else")) {
        restore(p);
        return true;
    }
    return statement(x);
}

bool ifStatement(StatementPtr &x) {
    Location location = currentLocation();
    vector<StatementPtr> condStmts;
    ExprPtr condition;
    StatementPtr thenPart, elsePart;
    if (!keyword("if")) return false;
    if (!symbol("(")) return false;
    if (!statementExpression(condStmts, condition)) return false;
    if (!symbol(")")) return false;
    if (!statement(thenPart)) return false;
    if (!optElse(elsePart)) return false;
    x = new If(condStmts, condition, thenPart, elsePart);
    x->location = location;
    return true;
}

bool caseList(ExprListPtr &x) {
    if (!keyword("case")) return false;
    if (!symbol("(")) return false;
    if (!expressionList(x)) return false;
    if (!symbol(")")) return false;
    return true;
}

bool caseBlock(CaseBlockPtr &x) {
    Location location = currentLocation();
    ExprListPtr caseLabels;
    StatementPtr body;
    if (!caseList(caseLabels)) return false;
    if (!statement(body)) return false;
    x = new CaseBlock(caseLabels, body);
    x->location = location;
    return true;
}

bool caseBlockList(vector<CaseBlockPtr> &x) {
    CaseBlockPtr a;
    if (!caseBlock(a)) return false;
    x.clear();
    while (true) {
        x.push_back(a);
        unsigned p = save();
        if (!caseBlock(a)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool switchStatement(StatementPtr &x) {
    Location location = currentLocation();
    vector<StatementPtr> exprStmts;
    ExprPtr expr;
    if (!keyword("switch")) return false;
    if (!symbol("(")) return false;
    if (!statementExpression(exprStmts, expr)) return false;
    if (!symbol(")")) return false;
    vector<CaseBlockPtr> caseBlocks;
    if (!caseBlockList(caseBlocks)) return false;
    StatementPtr defaultCase;
    if (!optElse(defaultCase)) return false;
    x = new Switch(exprStmts, expr, caseBlocks, defaultCase);
    x->location = location;
    return true;
}

bool exprStatement(StatementPtr &x) {
    Location location = currentLocation();
    ExprPtr y;
    if (!expression(y)) return false;
    if (!symbol(";")) return false;
    x = new ExprStatement(y);
    x->location = location;
    return true;
}

bool whileStatement(StatementPtr &x) {
    Location location = currentLocation();
    vector<StatementPtr> condStmts;
    ExprPtr cond;
    StatementPtr body;
    if (!keyword("while")) return false;
    if (!symbol("(")) return false;
    if (!statementExpression(condStmts, cond)) return false;
    if (!symbol(")")) return false;
    if (!statement(body)) return false;
    x = new While(condStmts, cond, body);
    x->location = location;
    return true;
}

bool breakStatement(StatementPtr &x) {
    Location location = currentLocation();
    if (!keyword("break")) return false;
    if (!symbol(";")) return false;
    x = new Break();
    x->location = location;
    return true;
}

bool continueStatement(StatementPtr &x) {
    Location location = currentLocation();
    if (!keyword("continue")) return false;
    if (!symbol(";")) return false;
    x = new Continue();
    x->location = location;
    return true;
}

bool forStatement(StatementPtr &x) {
    Location location = currentLocation();
    vector<IdentifierPtr> a;
    ExprPtr b;
    StatementPtr c;
    if (!keyword("for")) return false;
    if (!symbol("(")) return false;
    if (!identifierList(a)) return false;
    if (!keyword("in")) return false;
    if (!expression(b)) return false;
    if (!symbol(")")) return false;
    if (!statement(c)) return false;
    x = new For(a, b, c);
    x->location = location;
    return true;
}

bool catchBlock(CatchPtr &x) {
    Location location = currentLocation();
    if (!keyword("catch")) return false;
    if (!symbol("(")) return false;
    IdentifierPtr evar;
    if (!identifier(evar)) return false;
    ExprPtr etype;
    if (!optExprTypeSpec(etype)) return false;
    IdentifierPtr contextVar;
    unsigned p = save();
    if (keyword("in")) {
        if (!identifier(contextVar)) return false;
    } else {
        restore(p);
    }
    if (!symbol(")")) return false;
    StatementPtr body;
    if (!block(body)) return false;
    x = new Catch(evar, etype, contextVar, body);
    x->location = location;
    return true;
}

bool catchBlockList(vector<CatchPtr> &x) {
    CatchPtr a;
    if (!catchBlock(a)) return false;
    x.clear();
    while (true) {
        x.push_back(a);
        unsigned p = save();
        if (!catchBlock(a)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool tryStatement(StatementPtr &x) {
    Location location = currentLocation();
    StatementPtr a;
    if (!keyword("try")) return false;
    if (!block(a)) return false;
    vector<CatchPtr> b;
    if (!catchBlockList(b)) return false;
    x = new Try(a, b);
    x->location = location;
    return true;
}

bool throwStatement(StatementPtr &x) {
    Location location = currentLocation();
    ExprPtr a;
    ExprPtr context;
    if (!keyword("throw")) return false;
    unsigned p = save();
    if (!symbol(";")) {
        restore(p);
        if (!optExpression(a)) return false;
        unsigned q = save();
        if (keyword("in")) {
            if (!optExpression(context)) return false;
        } else {
            restore(q);
        }
    } else {
        restore(p);
    }
    if (!symbol(";")) return false;
    x = new Throw(a, context);
    x->location = location;
    return true;
}

bool staticFor(StatementPtr &x) {
    Location location = currentLocation();
    if (!ellipsis()) return false;
    if (!keyword("for")) return false;
    if (!symbol("(")) return false;
    IdentifierPtr a;
    if (!identifier(a)) return false;
    if (!keyword("in")) return false;
    ExprListPtr b;
    if (!expressionList(b)) return false;
    if (!symbol(")")) return false;
    StatementPtr c;
    if (!statement(c)) return false;
    x = new StaticFor(a, b, c);
    x->location = location;
    return true;
}

bool finallyStatement(StatementPtr &x) {
    Location location = currentLocation();

    if (!keyword("finally")) return false;
    StatementPtr body;
    if (!statement(body)) return false;
    x = new Finally(body);
    x->location = location;
    return true;
}

bool onerrorStatement(StatementPtr &x) {
    Location location = currentLocation();

    if (!keyword("onerror")) return false;
    StatementPtr body;
    if (!statement(body)) return false;
    x = new OnError(body);
    x->location = location;
    return true;
}

bool evalStatement(StatementPtr &x) {
    Location location = currentLocation();

    if (!keyword("eval")) return false;
    ExprListPtr args;
    if (!expressionList(args)) return false;
    if (!symbol(";")) return false;
    x = new EvalStatement(args);
    x->location = location;
    return true;
}

// parse staticassert top level or statement
bool staticAssert(ExprPtr& cond, ExprListPtr& message, Location& location) {
    location = currentLocation();
    if (!keyword("staticassert")) return false;
    if (!symbol("(")) return false;
    if (!expression(cond)) return false;

    message = new ExprList();

    unsigned s = save();
    while (symbol(",")) {
        ExprPtr expr;
        if (!expression(expr)) return false;
        message->add(expr);
        s = save();
    }
    restore(s);

    if (!symbol(")")) return false;
    if (!symbol(";")) return false;
    return true;
}

bool staticAssertTopLevel(TopLevelItemPtr &x, Module *module) {
    ExprPtr cond;
    ExprListPtr message;
    Location location;
    if (!staticAssert(cond, message, location)) return false;
    x = new StaticAssertTopLevel(module, cond, message);
    x->location = location;
    return true;
}

bool staticAssertStatement(StatementPtr &x) {
    ExprPtr cond;
    ExprListPtr message;
    Location location;
    if (!staticAssert(cond, message, location)) return false;
    x = new StaticAssertStatement(cond, message);
    x->location = location;
    return true;
}


bool statement(StatementPtr &x, bool = false) {
    unsigned p = save();
    if (block(x)) return true;
    if (restore(p), assignment(x)) return true;
    if (restore(p), initAssignment(x)) return true;
    if (restore(p), updateAssignment(x)) return true;
    if (restore(p), prefixUpdate(x)) return true;
    if (restore(p), ifStatement(x)) return true;
    if (restore(p), switchStatement(x)) return true;
    if (restore(p), returnStatement(x)) return true;
    if (restore(p), evalStatement(x)) return true;
    if (restore(p), exprStatement(x)) return true;
    if (restore(p), whileStatement(x)) return true;
    if (restore(p), breakStatement(x)) return true;
    if (restore(p), continueStatement(x)) return true;
    if (restore(p), forStatement(x)) return true;
    if (restore(p), staticFor(x)) return true;
    if (restore(p), tryStatement(x)) return true;
    if (restore(p), throwStatement(x)) return true;
    if (restore(p), finallyStatement(x)) return true;
    if (restore(p), onerrorStatement(x)) return true;
    if (restore(p), staticAssertStatement(x)) return true;
    if (restore(p), gotoStatement(x)) return true;
    
    return false;
}

bool statementExprStatement(StatementPtr &stmt) {
    unsigned p = save();
    if (localBinding(stmt)) return true;
    if (restore(p), assignment(stmt)) return true;
    if (restore(p), initAssignment(stmt)) return true;
    if (restore(p), updateAssignment(stmt)) return true;
    return false;
}



//
// staticParams
//

bool staticVarParam(IdentifierPtr &varParam) {
    if (!ellipsis()) return false;
    if (!identifier(varParam)) return false;
    return true;
}

bool optStaticVarParam(IdentifierPtr &varParam) {
    unsigned p = save();
    if (!staticVarParam(varParam)) {
        restore(p);
        varParam = NULL;
    }
    return true;
}

bool trailingVarParam(IdentifierPtr &varParam) {
    if (!symbol(",")) return false;
    if (!staticVarParam(varParam)) return false;
    return true;
}

bool optTrailingVarParam(IdentifierPtr &varParam) {
    unsigned p = save();
    if (!trailingVarParam(varParam)) {
        restore(p);
        varParam = NULL;
    }
    return true;
}

bool paramsAndVarParam(vector<IdentifierPtr> &params,
                              IdentifierPtr &varParam) {
    if (!identifierListNoTail(params)) return false;
    if (!optTrailingVarParam(varParam)) return false;
    unsigned p = save();
    if (!symbol(","))
        restore(p);
    return true;
}

bool staticParamsInner(vector<IdentifierPtr> &params,
                              IdentifierPtr &varParam) {
    unsigned p = save();
    if (paramsAndVarParam(params, varParam)) return true;
    restore(p);
    params.clear(); varParam = NULL;
    return optStaticVarParam(varParam);
}

bool staticParams(vector<IdentifierPtr> &params,
                         IdentifierPtr &varParam) {
    if (!symbol("[")) return false;
    if (!staticParamsInner(params, varParam)) return false;
    if (!symbol("]")) return false;
    return true;
}

bool optStaticParams(vector<IdentifierPtr> &params,
                            IdentifierPtr &varParam) {
    unsigned p = save();
    if (!staticParams(params, varParam)) {
        restore(p);
        params.clear();
        varParam = NULL;
    }
    return true;
}


//
// code
//

bool varArgTypeSpec(ExprPtr &vargType) {
    if (!symbol(":")) return false;
    Location start = currentLocation();
    if (!nameRef(vargType)) return false;
    vargType->startLocation = start;
    vargType->endLocation = currentLocation();
    return true;
}

bool optVarArgTypeSpec(ExprPtr &vargType) {
    unsigned p = save();
    if (!varArgTypeSpec(vargType)) {
        restore(p);
        vargType = NULL;
    }
    return true;
}

bool optArgTempness(ValueTempness &tempness) {
    unsigned p = save();
    if (keyword("rvalue")) {
        tempness = TEMPNESS_RVALUE;
        return true;
    }
    restore(p);
    if (keyword("ref")) {
        tempness = TEMPNESS_LVALUE;
        return true;
    }
    restore(p);
    if (keyword("forward")) {
        tempness = TEMPNESS_FORWARD;
        return true;
    }
    restore(p);
    tempness = TEMPNESS_DONTCARE;
    return true;
}

bool valueFormalArg(FormalArgPtr &x, bool &hasVarArg, bool &hasAsConversion) {
    Location location = currentLocation();
    ValueTempness tempness;
    if (!optArgTempness(tempness)) return false;
    IdentifierPtr y;
    ExprPtr z;
    bool varArg = false;
    unsigned p = save();
    if (ellipsis()) {
        if (hasVarArg) 
            return false;    
        else
            hasVarArg = true;
        varArg = true;
    } else
        restore(p);
    if (!identifier(y)) return false;
    if (varArg) {
        if (!optVarArgTypeSpec(z)) return false;
    } else {
        if (!optTypeSpec(z)) return false;
    }
    p = save();
    if (keyword("as")) {
        if (varArg) return false;
        ExprPtr w;
        if (!pattern(w)) return false;
        hasAsConversion = true;
        x = new FormalArg(y, z, tempness, w);
    } else {
        restore(p);
        x = new FormalArg(y, z, tempness, varArg);
    } 
    x->location = location;
    return true;
}

FormalArgPtr makeStaticFormalArg(size_t index, ExprPtr expr, Location const & location) {
    // desugar static args
    llvm::SmallString<128> buf;
    llvm::raw_svector_ostream sout(buf);
    sout << "%arg" << index;
    IdentifierPtr argName = Identifier::get(sout.str());

    ExprPtr staticName =
        new ForeignExpr("prelude",
                        new NameRef(Identifier::get("Static")));

    IndexingPtr indexing = new Indexing(staticName, new ExprList(expr));
    indexing->startLocation = location;
    indexing->endLocation = currentLocation();

    FormalArgPtr arg = new FormalArg(argName, indexing.ptr());
    arg->location = location;
    return arg;
}

bool staticFormalArg(size_t index, FormalArgPtr &x) {
    Location location = currentLocation();
    ExprPtr expr;
    if (!symbol("#")) return false;
    if (!expression(expr)) return false;

    if (expr->exprKind == UNPACK) {
        error(expr, "#static variadic arguments are not yet supported");
    }

    x = makeStaticFormalArg(index, expr, location);
    return true;
}

bool stringFormalArg(size_t index, FormalArgPtr &x) {
    Location location = currentLocation();
    ExprPtr expr;
    if (!stringLiteral(expr)) return false;
    x = makeStaticFormalArg(index, expr, location);
    return true;
}

bool formalArg(size_t index, FormalArgPtr &x, bool &hasVarArg, bool &hasAsConversion) {
    unsigned p = save();
    if (valueFormalArg(x, hasVarArg, hasAsConversion)) return true;
    if (restore(p), staticFormalArg(index, x)) return true;
    if (restore(p), stringFormalArg(index, x)) return true;
    return false;
}

bool formalArgs(vector<FormalArgPtr> &x, bool &hasVarArg, bool &hasAsConversion) {
    FormalArgPtr y;
    if (!formalArg(x.size(), y, hasVarArg, hasAsConversion)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !formalArg(x.size(), y, hasVarArg, hasAsConversion)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool argumentsBody(vector<FormalArgPtr> &args, bool &hasVarArg, bool &hasAsConversion) {
    unsigned p = save();
    if (!formalArgs(args, hasVarArg, hasAsConversion)){
        restore(p);
        args.clear();
    }
    return true;
}

bool arguments(vector<FormalArgPtr> &args, bool &hasVarArg, bool &hasAsConversion) {
    if (!symbol("(")) return false;
    if (!argumentsBody(args, hasVarArg, hasAsConversion)) return false;
    if (!symbol(")")) return false;
    return true;
}


bool bindingArg(FormalArgPtr &x, bool &hasVarArg) {
    Location location = currentLocation();
    IdentifierPtr y;
    ExprPtr z;
    bool varArg = false;
    unsigned p = save();
    if (ellipsis()) {
        if (hasVarArg) 
            return false;    
        else
            hasVarArg = true;
        varArg = true;
    } else
        restore(p);
    if (!identifier(y)) return false;
    if (varArg) {
        if (!optVarArgTypeSpec(z)) return false;
    } else {
        if (!optTypeSpec(z)) return false;
    }
    x = new FormalArg(y, z);
    x->location = location;
    x->varArg = varArg;
    return true;
}

bool bindingsBody(vector<FormalArgPtr> &x, bool &hasVarArg) {
    FormalArgPtr y;
    if (!bindingArg(y, hasVarArg)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !bindingArg(y, hasVarArg)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool predicate(ExprPtr &x) {
    if (!keyword("when")) return false;
    return expression(x);
}

bool optPredicate(ExprPtr &x) {
    unsigned p = save();
    if (!predicate(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool patternVar(PatternVar &x) {
    unsigned p = save();
    x.isMulti = true;
    if (!ellipsis()) {
        restore(p);
        x.isMulti = false;
    }
    if (!identifier(x.name)) return false;
    return true;
}

bool patternVarList(vector<PatternVar> &x) {
    PatternVar y;
    if (!patternVar(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !patternVar(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool optPatternVarList(vector<PatternVar> &x) {
    unsigned p = save();
    if (!patternVarList(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

bool patternVarsWithCond(vector<PatternVar> &x, ExprPtr &y) {
    if (!symbol("[")) return false;
    if (!optPatternVarList(x)) return false;
    if (!optPredicate(y) || !symbol("]")) {
        x.clear();
        y = NULL;
        return false;
    }
    return true;
}

bool optPatternVarsWithCond(vector<PatternVar> &x, ExprPtr &y) {
    unsigned p = save();
    if (!patternVarsWithCond(x, y)) {
        restore(p);
        x.clear();
        y = NULL;
    }
    return true;
}

void skipOptPatternVar() {
    unsigned p = save();
    if (!symbol("[")) {
        restore(p);
    } else {
        int bracket = 1;
        while (bracket) {
            unsigned p = save();
            if (symbol("[")) {
                ++bracket;
                continue;
            }
            restore(p);
            if(symbol("]"))
                --bracket;
        }
    }
}

bool exprBody(StatementPtr &x) {
    if (!opsymbol("=")) return false;
    Location location = currentLocation();
    ReturnKind rkind;
    ExprListPtr exprs;
    if (!returnExprList(rkind, exprs)) return false;
    if (!symbol(";")) return false;
    x = new Return(rkind, exprs, true);
    x->location = location;
    return true;
}

bool body(StatementPtr &x) {
    unsigned p = save();
    if (exprBody(x)) return true;
    if (restore(p), block(x)) return true;
    return false;
}

bool optBody(StatementPtr &x) {
    unsigned p = save();
    if (body(x)) return true;
    restore(p);
    x = NULL;
    if (symbol(";")) return true;
    return false;
}



//
// topLevelVisibility, importVisibility
//

bool optVisibility(Visibility defaultVisibility, Visibility &x) {
    unsigned p = save();
    if (keyword("public")) {
        x = PUBLIC;
        return true;
    }
    restore(p);
    if (keyword("private")) {
        x = PRIVATE;
        return true;
    }
    restore(p);
    x = defaultVisibility;
    return true;
}

bool topLevelVisibility(Visibility &x) {
    return optVisibility(PUBLIC, x);
}

bool importVisibility(Visibility &x) {
    return optVisibility(VISIBILITY_UNDEFINED, x);
}



//
// records
//

bool recordField(RecordFieldPtr &x, bool &hasVarField) {
    Location location = currentLocation();
    IdentifierPtr y;
    ExprPtr z;
    bool varField = false;
    unsigned p = save();
    if (ellipsis()) {
        if (hasVarField) 
            return false;    
        else
            hasVarField = true;
        varField = true;
    } else
        restore(p);
    if (!identifier(y)) return false;
    if (varField) {
        if (!varArgTypeSpec(z)) return false;
    } else {
        if (!exprTypeSpec(z)) return false;
    }
    x = new RecordField(y, z);
    x->location = location;
    x->varField = varField;
    return true;
}

bool recordFields(vector<RecordFieldPtr> &x, bool &hasVarField) {
    RecordFieldPtr y;
    if (!recordField(y, hasVarField)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",")) {
            restore(p);
            break;
        }
        p = save();
        if (!recordField(y, hasVarField)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool optRecordFields(vector<RecordFieldPtr> &x, bool &hasVarField) {
    unsigned p = save();
    if (!recordFields(x, hasVarField)) {
        restore(p);
        x.clear();
    }
    return true;
}

bool recordBodyFields(RecordBodyPtr &x) {
    Location location = currentLocation();
    if (!symbol("(")) return false;
    vector<RecordFieldPtr> y;
    bool hasVarField = false;
    if (!optRecordFields(y, hasVarField)) return false;
    if (!symbol(")")) return false;
    if (!symbol(";")) return false;
    x = new RecordBody(y, hasVarField);
    x->location = location;
    return true;
}

bool recordBodyComputed(RecordBodyPtr &x) {
    Location location = currentLocation();
    if (!opsymbol("=")) return false;
    ExprListPtr y;
    if (!optExpressionList(y)) return false;
    if (!symbol(";")) return false;
    x = new RecordBody(y);
    x->location = location;
    return true;
}

bool recordBody(RecordBodyPtr &x) {
    unsigned p = save();
    if (recordBodyFields(x)) return true;
    if (restore(p), recordBodyComputed(x)) return true;
    return false;
}

bool record(TopLevelItemPtr &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("record")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;    
    restore(e);
    RecordDeclPtr y = new RecordDecl(module, vis, patternVars, predicate);
    if (!identifier(y->name)) return false;
    if (!optStaticParams(y->params, y->varParam)) return false;
    if (!recordBody(y->body)) return false;
    x = y.ptr();
    x->location = location;
    return true;
}



//
// variant, instance
//

bool instances(ExprListPtr &x) {
    return symbol("(") && optExpressionList(x) && symbol(")");
}

bool optInstances(ExprListPtr &x, bool &open) {
    unsigned p = save();
    if (symbol("(")) {
        open = false;
        return optExpressionList(x) && symbol(")");
    } else {
        open = true;
        restore(p);
        x = new ExprList();
        return true;
    }
}

bool variant(TopLevelItemPtr &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("variant")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;    
    restore(e);
    IdentifierPtr name;
    if (!identifier(name)) return false;
    vector<IdentifierPtr> params;
    IdentifierPtr varParam;
    if (!optStaticParams(params, varParam)) return false;
    ExprListPtr defaultInstances;
    bool open;
    if (!optInstances(defaultInstances, open)) return false;
    if (!symbol(";")) return false;
    x = new VariantDecl(module, name, vis, patternVars, predicate, params, varParam, open, defaultInstances);
    x->location = location;
    return true;
}

bool instance(TopLevelItemPtr &x, Module *module, unsigned s) {
    if (!keyword("instance")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;    
    restore(e);
    ExprPtr target;
    if (!pattern(target)) return false;
    ExprListPtr members;
    if (!instances(members)) return false;
    if (!symbol(";")) return false;
    x = new InstanceDecl(module, patternVars, predicate, target, members);
    x->location = location;
    return true;
}

bool newtype(TopLevelItemPtr &x, Module *module) {
    Location location = currentLocation();
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("newtype")) return false;
    IdentifierPtr name;
    if (!identifier(name)) return false;
    if (!opsymbol("=")) return false;
    ExprPtr expr;
    if (!expression(expr)) return false;
    if (!symbol(";")) return false;
    x = new NewTypeDecl(module, name, vis, expr);
    x->location = location;
    return true;
}


//
// returnSpec
//

bool namedReturnName(IdentifierPtr &x) {
    IdentifierPtr y;
    if (!identifier(y)) return false;
    if (!symbol(":")) return false;
    x = y;
    return true;
}

bool returnTypeExpression(ExprPtr &x) {
    return orExpr(x);
}

bool returnType(ReturnSpecPtr &x) {
    Location location = currentLocation();
    ExprPtr z;
    if (!returnTypeExpression(z)) return false;
    x = new ReturnSpec(z, NULL);
    x->location = location;
    return true;
}

bool returnTypeList(vector<ReturnSpecPtr> &x) {
    ReturnSpecPtr y;
    if (!returnType(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !returnType(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool optReturnTypeList(vector<ReturnSpecPtr> &x) {
    unsigned p = save();
    if (!returnTypeList(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

bool namedReturn(ReturnSpecPtr &x) {
    Location location = currentLocation();
    IdentifierPtr y;
    if (!namedReturnName(y)) return false;
    ExprPtr z;
    if (!returnTypeExpression(z)) return false;
    x = new ReturnSpec(z, y);
    x->location = location;
    return true;
}

bool namedReturnList(vector<ReturnSpecPtr> &x) {
    ReturnSpecPtr y;
    if (!namedReturn(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !namedReturn(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool optNamedReturnList(vector<ReturnSpecPtr> &x) {
    unsigned p = save();
    if (!namedReturnList(x)) {
        restore(p);
        x.clear();
    }
    return true;
}

bool varReturnType(ReturnSpecPtr &x) {
    Location location = currentLocation();
    if (!ellipsis()) return false;
    ExprPtr z;
    if (!returnTypeExpression(z)) return false;
    x = new ReturnSpec(z, NULL);
    x->location = location;
    return true;
}

bool optVarReturnType(ReturnSpecPtr &x) {
    unsigned p = save();
    if (!varReturnType(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool varNamedReturn(ReturnSpecPtr &x) {
    Location location = currentLocation();
    if (!ellipsis()) return false;
    IdentifierPtr y;
    if (!namedReturnName(y)) return false;
    ExprPtr z;
    if (!returnTypeExpression(z)) return false;
    x = new ReturnSpec(z, y);
    x->location = location;
    return true;
}

bool optVarNamedReturn(ReturnSpecPtr &x) {
    unsigned p = save();
    if (!varNamedReturn(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool allReturnSpecsWithFlag(vector<ReturnSpecPtr> &returnSpecs,
                           ReturnSpecPtr &varReturnSpec, bool &exprRetSpecs) {
    returnSpecs.clear();
    varReturnSpec = NULL;
    unsigned p = save();
    if (symbol(":")) {
        if (!optReturnTypeList(returnSpecs)) return false;
        if (!optVarReturnType(varReturnSpec)) return false;
        if (returnSpecs.size()>0 || varReturnSpec!=NULL)
            exprRetSpecs = true;
        return true;
    } else {
        restore(p);
        if (opsymbol("-->")) {
            if (!optNamedReturnList(returnSpecs)) return false;
            if (!optVarNamedReturn(varReturnSpec)) return false;
            return true;
        } else {
            restore(p);
            return false;
        }
    }
}

bool allReturnSpecs(vector<ReturnSpecPtr> &returnSpecs,
                           ReturnSpecPtr &varReturnSpec) {
    bool exprRetSpecs = false;
    return allReturnSpecsWithFlag(returnSpecs, varReturnSpec, exprRetSpecs);
}


//
// define, overload
//

bool isOverload(bool &isDefault) {
    int p = save();
    if (keyword("overload"))
        isDefault = false;
    else if (restore(p), keyword("default"))
        isDefault = true;
    else {
        return false;
    }
    return true;
}

bool optInline(InlineAttribute &isInline) {
    unsigned p = save();
    if (keyword("inline"))
        isInline = INLINE;
    else if (restore(p), keyword("forceinline"))
        isInline = FORCE_INLINE;
    else if (restore(p), keyword("noinline"))
        isInline = NEVER_INLINE;
    else {
        restore(p);
        isInline = IGNORE;
    }
    return true;
}

bool optCallByName(bool &callByName) {
    unsigned p = save();
    if (!keyword("alias")) {
        restore(p);
        callByName = false;
        return true;
    }
    callByName = true;
    return true;
}

bool optPrivateOverload(bool &privateOverload) {
    unsigned p = save();
    if (!keyword("private")) {
        restore(p);
        privateOverload = false;
        return true;
    }
    if (!keyword("overload")) {
        return false;
    }
    privateOverload = true;
    return true;
}

bool llvmCode(LLVMCodePtr &b) {
    Token* t;
    if (!next(t) || (t->tokenKind != T_LLVM))
        return false;
    b = new LLVMCode(t->str);
    b->location = t->location;
    return true;
}

bool llvmProcedure(vector<TopLevelItemPtr> &x, Module *module) {
    Location location = currentLocation();
    CodePtr y = new Code();
    if (!optPatternVarsWithCond(y->patternVars, y->predicate)) return false;
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    InlineAttribute isInline;
    if (!optInline(isInline)) return false;
    IdentifierPtr z;
    Location targetStartLocation = currentLocation();
    if (!identifier(z)) return false;
    Location targetEndLocation = currentLocation();
    bool hasVarArg = false;
    bool hasAsConversion = false;
    if (!arguments(y->formalArgs, hasVarArg, hasAsConversion)) return false;
    y->hasVarArg = hasVarArg;
    y->returnSpecsDeclared = allReturnSpecs(y->returnSpecs, y->varReturnSpec);
    if (!llvmCode(y->llvmBody)) return false;
    y->location = location;

    ProcedurePtr u = new Procedure(module, z, vis, true);
    u->location = location;
    x.push_back(u.ptr());

    ExprPtr target = new NameRef(z);
    target->location = location;
    target->startLocation = targetStartLocation;
    target->endLocation = targetEndLocation;
    OverloadPtr v = new Overload(module, target, y, false, isInline, hasAsConversion);
    v->location = location;
    x.push_back(v.ptr());

    u->singleOverload = v;

    return true;
}

bool procedureWithInterface(vector<TopLevelItemPtr> &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("define")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    CodePtr interfaceCode = new Code();
    if (!optPatternVarsWithCond(interfaceCode->patternVars, interfaceCode->predicate)) return false;
    restore(e);
    IdentifierPtr name;
    Location targetStartLocation = currentLocation();
    if (!identifier(name)) return false;
    Location targetEndLocation = currentLocation();
    bool hasVarArg = false;
    bool hasAsConversion = false;
    if (!arguments(interfaceCode->formalArgs, hasVarArg, hasAsConversion)) return false;
    interfaceCode->hasVarArg = hasVarArg;
    interfaceCode->returnSpecsDeclared = allReturnSpecs(interfaceCode->returnSpecs, interfaceCode->varReturnSpec);

    bool privateOverload;
    if (!optPrivateOverload(privateOverload)) return false;

    if (!symbol(";")) return false;
    interfaceCode->location = location;

    ExprPtr target = new NameRef(name);
    target->location = location;
    target->startLocation = targetStartLocation;
    target->endLocation = targetEndLocation;
    OverloadPtr interface = new Overload(module, target, interfaceCode, false, IGNORE);
    interface->location = location;

    ProcedurePtr proc = new Procedure(module, name, vis, privateOverload, interface);
    proc->location = location;
    x.push_back(proc.ptr());

    return true;
}

bool procedureWithBody(vector<TopLevelItemPtr> &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    InlineAttribute isInline;
    if (!optInline(isInline)) return false;
    bool callByName;
    if (!optCallByName(callByName)) return false;
    IdentifierPtr name;
    Location targetStartLocation = currentLocation();
    if (!identifier(name)) return false;
    Location targetEndLocation = currentLocation();
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    CodePtr code = new Code();
    if (!optPatternVarsWithCond(code->patternVars, code->predicate)) return false;
    restore(e);
    bool hasVarArg = false;
    bool hasAsConversion = false;
    if (!arguments(code->formalArgs, hasVarArg, hasAsConversion)) return false;
    code->hasVarArg = hasVarArg;
    bool exprRetSpecs = false;
    code->returnSpecsDeclared = allReturnSpecsWithFlag(code->returnSpecs, code->varReturnSpec, exprRetSpecs);
    if (!body(code->body)) return false;
    code->location = location;
    if(exprRetSpecs && code->body->stmtKind == RETURN){
        Return *x = (Return *)code->body.ptr();    
        if(x->isExprReturn)
            x->isReturnSpecs = true;
    }

    ProcedurePtr proc = new Procedure(module, name, vis, true);
    proc->location = location;
    x.push_back(proc.ptr());

    ExprPtr target = new NameRef(name);
    target->location = location;
    target->startLocation = targetStartLocation;
    target->endLocation = targetEndLocation;
    OverloadPtr oload = new Overload(module, target, code, callByName, isInline, hasAsConversion);
    oload->location = location;
    x.push_back(oload.ptr());

    proc->singleOverload = oload;

    return true;
}

bool procedure(TopLevelItemPtr &x, Module *module) {
    Location location = currentLocation();
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("define")) return false;
    IdentifierPtr name;
    if (!identifier(name)) return false;
    bool privateOverload;
    if (!optPrivateOverload(privateOverload)) return false;
    if (!symbol(";")) return false;
    x = new Procedure(module, name, vis, privateOverload);
    x->location = location;
    return true;
}

bool overload(TopLevelItemPtr &x, Module *module, unsigned s) {
    InlineAttribute isInline;
    if (!optInline(isInline)) return false;
    bool callByName;
    if (!optCallByName(callByName)) return false;
    bool isDefault;
    if (!isOverload(isDefault)) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    CodePtr code = new Code();
    if (!optPatternVarsWithCond(code->patternVars, code->predicate)) return false;
    restore(e);
    ExprPtr target;
    Location targetStartLocation = currentLocation();
    if (!pattern(target)) return false;
    Location targetEndLocation = currentLocation();
    bool hasVarArg = false;
    bool hasAsConversion = false;
    if (!arguments(code->formalArgs, hasVarArg, hasAsConversion)) return false;
    code->hasVarArg = hasVarArg;
    bool exprRetSpecs = false;
    code->returnSpecsDeclared = allReturnSpecsWithFlag(code->returnSpecs, code->varReturnSpec, exprRetSpecs);
    unsigned p = save();
    if (!optBody(code->body)) {
        restore(p);
        if (callByName) return false;
        if (!llvmCode(code->llvmBody)) return false;
    }
    if(exprRetSpecs && code->body->stmtKind == RETURN){
        Return *x = (Return *)code->body.ptr();    
        if(x->isExprReturn)
            x->isReturnSpecs = true;
    }
    target->location = location;
    target->startLocation = targetStartLocation;
    target->endLocation = targetEndLocation;
    code->location = location;
    OverloadPtr oload = new Overload(module, target, code, callByName, isInline, hasAsConversion);
    oload->location = location;
    oload->isDefault = isDefault;
    x = oload.ptr();
    return true;
}



//
// enumerations
//

bool enumMember(EnumMemberPtr &x) {
    Location location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    x = new EnumMember(y);
    x->location = location;
    return true;
}

bool enumMemberList(vector<EnumMemberPtr> &x) {
    EnumMemberPtr y;
    if (!enumMember(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",")) {
            restore(p);
            break;
        }
        p = save();
        if (!enumMember(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool enumeration(TopLevelItemPtr &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("enum")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;    
    restore(e);
    IdentifierPtr y;
    if (!identifier(y)) return false;
    EnumDeclPtr z = new EnumDecl(module, y, vis, patternVars, predicate);
    if (!symbol("(")) return false;
    if (!enumMemberList(z->members)) return false;
    if (!symbol(")")) return false;
    if (!symbol(";")) return false;
    x = z.ptr();
    x->location = location;
    return true;
}



//
// global variable
//

bool globalVariable(TopLevelItemPtr &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("var")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;    
    restore(e);
    IdentifierPtr name;
    if (!identifier(name)) return false;
    vector<IdentifierPtr> params;
    IdentifierPtr varParam;
    if (!optStaticParams(params, varParam)) return false;
    if (!opsymbol("=")) return false;
    ExprPtr expr;
    if (!expression(expr)) return false;
    if (!symbol(";")) return false;
    x = new GlobalVariable(module, name, vis, patternVars, predicate, params, varParam, expr);
    x->location = location;
    return true;
}



//
// external procedure, external variable
//

bool externalAttributes(ExprListPtr &x) {
    if (!symbol("(")) return false;
    if (!expressionList(x)) return false;
    if (!symbol(")")) return false;
    return true;
}

bool optExternalAttributes(ExprListPtr &x) {
    unsigned p = save();
    if (!externalAttributes(x)) {
        restore(p);
        x = new ExprList();
    }
    return true;
}

bool externalArg(ExternalArgPtr &x) {
    Location location = currentLocation();
    IdentifierPtr y;
    if (!identifier(y)) return false;
    ExprPtr z;
    if (!exprTypeSpec(z)) return false;
    x = new ExternalArg(y, z);
    x->location = location;
    return true;
}

bool externalArgs(vector<ExternalArgPtr> &x) {
    ExternalArgPtr y;
    if (!externalArg(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !externalArg(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool externalVarArgs(bool &hasVarArgs) {
    if (!ellipsis()) return false;
    hasVarArgs = true;
    return true;
}

bool optExternalVarArgs(bool &hasVarArgs) {
    unsigned p = save();
    if (!externalVarArgs(hasVarArgs)) {
        restore(p);
        hasVarArgs = false;
    }
    return true;
}

bool trailingExternalVarArgs(bool &hasVarArgs) {
    if (!symbol(",")) return false;
    if (!externalVarArgs(hasVarArgs)) return false;
    return true;
}

bool optTrailingExternalVarArgs(bool &hasVarArgs) {
    unsigned p = save();
    if (!trailingExternalVarArgs(hasVarArgs)) {
        restore(p);
        hasVarArgs = false;
    }
    return true;
}

bool externalArgsWithVArgs(vector<ExternalArgPtr> &x,
                                  bool &hasVarArgs) {
    if (!externalArgs(x)) return false;
    if (!optTrailingExternalVarArgs(hasVarArgs)) return false;
    return true;
}

bool externalArgsBody(vector<ExternalArgPtr> &x,
                             bool &hasVarArgs) {
    unsigned p = save();
    if (externalArgsWithVArgs(x, hasVarArgs)) return true;
    restore(p);
    x.clear();
    hasVarArgs = false;
    if (!optExternalVarArgs(hasVarArgs)) return false;
    return true;
}

bool externalBody(StatementPtr &x) {
    unsigned p = save();
    if (exprBody(x)) return true;
    if (restore(p), block(x)) return true;
    if (restore(p), symbol(";")) {
        x = NULL;
        return true;
    }
    return false;
}

bool optExternalReturn(ExprPtr &x) {
    unsigned p = save();
    if (!symbol(":")) {
        restore(p);
        return true;
    }
    return optExpression(x);
}

bool external(TopLevelItemPtr &x, Module *module) {
    Location location = currentLocation();
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("external")) return false;
    ExternalProcedurePtr y = new ExternalProcedure(module, vis);
    if (!optExternalAttributes(y->attributes)) return false;
    if (!identifier(y->name)) return false;
    if (!symbol("(")) return false;
    bool hasVarArg = false;
    if (!externalArgsBody(y->args, hasVarArg)) return false;
    y->hasVarArgs = hasVarArg;
    if (!symbol(")")) return false;
    if (!optExternalReturn(y->returnType)) return false;
    if (!externalBody(y->body)) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

bool externalVariable(TopLevelItemPtr &x, Module *module) {
    Location location = currentLocation();
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("external")) return false;
    ExternalVariablePtr y = new ExternalVariable(module, vis);
    if (!optExternalAttributes(y->attributes)) return false;
    if (!identifier(y->name)) return false;
    if (!exprTypeSpec(y->type)) return false;
    if (!symbol(";")) return false;
    x = y.ptr();
    x->location = location;
    return true;
}



//
// global alias
//

bool globalAlias(TopLevelItemPtr &x, Module *module, unsigned s) {
    Visibility vis;
    if (!topLevelVisibility(vis)) return false;
    if (!keyword("alias")) return false;
    unsigned e = save();
    restore(s);
    Location location = currentLocation();
    vector<PatternVar> patternVars;
    ExprPtr predicate;
    if (!optPatternVarsWithCond(patternVars, predicate)) return false;    
    restore(e);
    IdentifierPtr name;
    if (!identifier(name)) return false;
    vector<IdentifierPtr> params;
    IdentifierPtr varParam;
    if (!optStaticParams(params, varParam)) return false;
    if (!opsymbol("=")) return false;
    ExprPtr expr;
    if (!expression(expr)) return false;
    if (!symbol(";")) return false;
    x = new GlobalAlias(module, name, vis, patternVars, predicate, params, varParam, expr);
    x->location = location;
    return true;
}



//
// imports
//

bool importAlias(IdentifierPtr &x) {
    if (!keyword("as")) return false;
    if (!identifier(x)) return false;
    return true;
}

bool optImportAlias(IdentifierPtr &x) {
    unsigned p = save();
    if (!importAlias(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool importModule(ImportPtr &x) {
    Location location = currentLocation();
    DottedNamePtr y;
    if (!dottedName(y)) return false;
    IdentifierPtr z;
    if (!optImportAlias(z)) return false;
    x = new ImportModule(y, z);
    x->location = location;
    return true;
}

bool importStar(ImportPtr &x) {
    Location location = currentLocation();
    DottedNamePtr y;
    if (!dottedName(y)) return false;
    if (!symbol(".")) return false;
    if (!opsymbol("*")) return false;
    x = new ImportStar(y);
    x->location = location;
    return true;
}

bool importedMember(ImportedMember &x) {
    if (!topLevelVisibility(x.visibility)) return false;
    if (!identifier(x.name)) return false;
    if (!optImportAlias(x.alias)) return false;
    return true;
}

bool importedMemberList(vector<ImportedMember> &x) {
    ImportedMember y;
    if (!importedMember(y)) return false;
    x.clear();
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",")) {
            restore(p);
            break;
        }
        p = save();
        if (!importedMember(y)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool importMembers(ImportPtr &x) {
    Location location = currentLocation();
    DottedNamePtr y;
    if (!dottedName(y)) return false;
    if (!symbol(".")) return false;
    if (!symbol("(")) return false;
    ImportMembersPtr z = new ImportMembers(y);
    if (!importedMemberList(z->members)) return false;
    if (!symbol(")")) return false;
    x = z.ptr();
    x->location = location;
    return true;
}

bool import2(ImportPtr &x, Visibility visTop) {
    Visibility vis;
    if (!importVisibility(vis)) return false;
    unsigned p = save();
    if (importStar(x)) goto importsuccess;
    if (restore(p), importMembers(x)) goto importsuccess;
    if (restore(p), importModule(x)) goto importsuccess;
    return false;
importsuccess:
    if (vis == VISIBILITY_UNDEFINED) {
        if (visTop ==  VISIBILITY_UNDEFINED)
            x->visibility = PRIVATE;
        else
            x->visibility = visTop;
    } else
            x->visibility = vis;
    return true;
}

bool importList(vector<ImportPtr> &x, Visibility vis) {
    ImportPtr y;
    if (!import2(y, vis)) return false;
    while (true) {
        x.push_back(y);
        unsigned p = save();
        if (!symbol(",") || !import2(y, vis)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool import(vector<ImportPtr> &x) {
    Visibility vis;
    if (!importVisibility(vis)) return false;
    if (!keyword("import")) return false;
    if (!importList(x, vis)) return false;
    if (!symbol(";")) return false;
    return true;
}

bool imports(vector<ImportPtr> &x) {
    x.clear();
    while (true) {
        unsigned p = save();
        if (!import(x)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool evalTopLevel(TopLevelItemPtr &x, Module *module) {
    Location location = currentLocation();
    if (!keyword("eval")) return false;
    ExprListPtr args;
    if (!expressionList(args)) return false;
    if (!symbol(";")) return false;
    x = new EvalTopLevel(module, args);
    x->location = location;
    return true;
}

//
// documentation
//


bool documentationAnnotation(std::map<DocumentationAnnotation, string> &an)
{
    Location location = currentLocation();
    Token* t;
    if (!next(t) || t->tokenKind != T_DOC_PROPERTY) return false;
    llvm::StringRef key = llvm::StringRef(t->str);

    DocumentationAnnotation ano;
    if (key == "section") {
        ano = SectionAnnotation;
    } else if (key == "module") {
        ano = ModuleAnnotation;
    } else if (key == "overload" || key == "default") {
        ano = OverloadAnnotation;
    } else if (key == "record") {
        ano = RecordAnnotion;
    } else {
        ano = InvalidAnnotation;
        pushLocation(location);
        fmtError("invalid annotation '%s'\n", key.str().c_str());
    }

    if (!next(t) || t->tokenKind != T_DOC_TEXT) return false;
    llvm::StringRef value = llvm::StringRef(t->str);

    an.insert(std::pair<DocumentationAnnotation,string>(ano, value.str()));
    return true;
}

bool documentationText(std::string &text)
{
    Token* t;
    if (!next(t) || t->tokenKind != T_DOC_TEXT) return false;
    if (parserOptionKeepDocumentation)
        text.append(t->str.str());
        text.append("\n");
    return true;
}


bool documentation(TopLevelItemPtr &x, Module *module)
{
    Location location = currentLocation();
    std::map<DocumentationAnnotation, string> annotation;
    std::string text;

    bool success = false;
    bool hasAttachmentAnnotation = false;

    for (;;) {
        unsigned p = save();
        if (documentationAnnotation(annotation)) {
            if (hasAttachmentAnnotation) {
                restore (p);
                break;
            }
            hasAttachmentAnnotation = true;
            success = true;
            continue;
        }
        restore(p);
        if (documentationText(text)) {
            success = true;
            continue;
        }
        restore(p);
        break;
    }

    if (success) {
        x = new Documentation(module, annotation, text);
        return true;
    }
    return false;


}


//
// module
//

bool topLevelItem(vector<TopLevelItemPtr> &x, Module *module) {
    unsigned p = save();

    TopLevelItemPtr y;
    skipOptPatternVar();
    unsigned q = save();
    if (restore(q), overload(y, module, p)) goto success;
    if (restore(q), procedureWithInterface(x, module, p)) goto success2;
    if (restore(q), procedureWithBody(x, module, p)) goto success2;
    if (restore(q), record(y, module, p)) goto success;
    if (restore(q), variant(y, module, p)) goto success;
    if (restore(q), instance(y, module, p)) goto success;
    if (restore(q), enumeration(y, module, p)) goto success;
    if (restore(q), globalVariable(y, module, p)) goto success;
    if (restore(q), globalAlias(y, module, p)) goto success;
    if (restore(p), procedure(y, module)) goto success;
    if (restore(p), documentation(y, module)) goto success;
    if (restore(p), llvmProcedure(x, module)) goto success2;
    if (restore(p), external(y, module)) goto success;
    if (restore(p), externalVariable(y, module)) goto success;
    if (restore(p), newtype(y, module)) goto success;
    if (restore(p), evalTopLevel(y, module)) goto success;
    if (restore(p), staticAssertTopLevel(y, module)) goto success;
    return false;
success :
    assert(y.ptr());
    x.push_back(y);
success2 :
    return true;
}

bool topLevelItems(vector<TopLevelItemPtr> &x, Module *module) {
    x.clear();

    while (true) {
        unsigned p = save();
        if (!topLevelItem(x, module)) {
            restore(p);
            break;
        }
    }
    return true;
}

bool optModuleDeclarationAttributes(ExprListPtr &x) {
    unsigned p = save();
    if (!symbol("(")) {
        restore(p);
        x = NULL;
        return true;
    }
    if (!optExpressionList(x))
        return false;
    if (!symbol(")"))
        return false;
    return true;
}

bool optModuleDeclaration(ModuleDeclarationPtr &x) {
    Location location = currentLocation();

    unsigned p = save();
    if (!keyword("in")) {
        restore(p);
        x = NULL;
        return true;
    }
    DottedNamePtr name;
    ExprListPtr attributes;
    if (!dottedName(name))
        return false;
    if (!optModuleDeclarationAttributes(attributes))
        return false;
    if (!symbol(";"))
        return false;

    x = new ModuleDeclaration(name, attributes);
    x->location = location;
    return true;
}

bool optTopLevelLLVM(LLVMCodePtr &x) {
    unsigned p = save();
    if (!llvmCode(x)) {
        restore(p);
        x = NULL;
    }
    return true;
}

bool module(llvm::StringRef moduleName, ModulePtr &x) {
    Location location = currentLocation();
    ModulePtr y = new Module(currentCompiler, moduleName);
    if (!imports(y->imports)) return false;
    if (!optModuleDeclaration(y->declaration)) return false;
    if (!optTopLevelLLVM(y->topLevelLLVM)) return false;
    if (!topLevelItems(y->topLevelItems, y.ptr())) return false;
    x = y.ptr();
    x->location = location;
    return true;
}

//
// REPL
//

bool replItems(ReplItem& x, bool = false) {
    inRepl = false;
    unsigned p = save();
    if (expression(x.expr) && position == tokens->size()) {
        x.isExprSet = true;
        return true;
    }
    restore(p);

    inRepl = true;
    x.toplevels.clear();
    x.imports.clear();
    x.stmts.clear();
    StatementPtr stmtItem;

    while (true) {
        if (position == tokens->size()) {
            break;
        }

        unsigned p = save();

        if (!topLevelItem(x.toplevels, NULL)) {
            restore(p);
        } else {
            continue;
        }

        if (!import(x.imports)) {
            restore(p);
        } else {
            continue;
        }

        if (!blockItem(stmtItem)) {
            restore(p);
            break;
        } else {
            x.stmts.push_back(stmtItem);
        }
    }

    x.isExprSet = false;

    return true;
}


//
// parse
//

template<typename Parser, typename ParserParam, typename Node>
void applyParser(SourcePtr source, unsigned offset, size_t length, Parser parser, ParserParam parserParam, Node &node)
{
    vector<Token> t;
    tokenize(source, offset, length, t);

    tokens = &t;
    position = maxPosition = 0;

    if (!parser(this, node, parserParam) || (position < t.size())) {
        Location location;
        if (maxPosition == t.size())
            location = Location(source.ptr(), unsigned(source->size()));
        else
            location = t[maxPosition].location;
        pushLocation(location);
        error("parse error");
    }

    tokens = NULL;
    position = maxPosition = 0;
}

struct ModuleParser {
    llvm::StringRef moduleName;
    bool operator()(ParserImpl* parserImpl, ModulePtr &m, Module*) {
        return parserImpl->module(moduleName, m); 
    }
};

}; //ParserImpl

bool expressionParser(ParserImpl* self, ExprPtr& x, bool f) {
    return self->expression(x, f);
}

bool expressionListParser(ParserImpl* self, ExprListPtr& x, bool f) {
    return self->expressionList(x, f);
}

bool blockItemsParser(ParserImpl* self, vector<StatementPtr> &x, bool f) {
    return self->blockItems(x, f);
}

bool topLevelItemsParser(ParserImpl* self, vector<TopLevelItemPtr> &x, Module* m) {
    return self->topLevelItems(x, m);
}

bool replItemsParser(ParserImpl* self, ReplItem &x, bool f) {
    return self->replItems(x, f);
}

ModulePtr parse(llvm::StringRef moduleName, SourcePtr source, 
                CompilerState* cst, ParserFlags flags) {
    ParserImpl parserImpl(cst);
    if (flags && ParserKeepDocumentation)
        parserImpl.parserOptionKeepDocumentation = true;
    ModulePtr m;
    ParserImpl::ModuleParser p = { moduleName };
    parserImpl.applyParser(source, 0, source->size(), p, m.ptr(), m);
    m->source = source;
    return m;
}



//
// parseExpr
//

ExprPtr parseExpr(SourcePtr source, unsigned offset, size_t length,
                  CompilerState* cst) {
    ParserImpl parserImpl(cst);
    ExprPtr expr;
    parserImpl.applyParser(source, offset, length, 
                           expressionParser, false, expr);
    return expr;
}


//
// parseExprList
//

ExprListPtr parseExprList(SourcePtr source, unsigned offset, size_t length,
                          CompilerState* cst) {
    ParserImpl parserImpl(cst);
    ExprListPtr exprList;
    parserImpl.applyParser(source, offset, length, 
                           expressionListParser, false, exprList);
    return exprList;
}


//
// parseStatements
//

void parseStatements(SourcePtr source, unsigned offset, size_t length,
        vector<StatementPtr> &stmts, CompilerState* cst)
{
    ParserImpl parserImpl(cst);
    parserImpl.applyParser(source, offset, length, 
                           blockItemsParser, false, stmts);
}


//
// parseTopLevelItems
//

void parseTopLevelItems(SourcePtr source, unsigned offset, size_t length,
        vector<TopLevelItemPtr> &topLevels, Module *module,
                        CompilerState* cst)
{
    ParserImpl parserImpl(cst);
    parserImpl.applyParser(source, offset, length, 
                           topLevelItemsParser, module, topLevels);
}

//
// parseInteractive
//

ReplItem parseInteractive(SourcePtr source, unsigned offset, size_t length,
                          CompilerState* cst)
{
    ParserImpl parserImpl(cst);
    ReplItem x;
    parserImpl.applyParser(source, offset, length, 
                           replItemsParser, false, x);
    return x;
}

}
