#include "../include/compiler.h"

/*
 * parser.cpp
 * ----------------------------------
 * 这里写语法分析和语义动作。
 *
 * 我采用的是递归下降方法：
 *   一个语法成分一般写成一个函数。
 *   例如 parseIfStatement 负责 if 语句，parseWhileStatement 负责 while 语句。
 *
 * 分析过程中同时做语义动作：
 *   1. 遇到声明就填符号表。
 *   2. 遇到表达式就产生临时变量和四元式。
 *   3. 遇到 if/while 就产生 label、ifFalse、goto 四元式。
 */

/* parseType：分析类型，如 integer、real、array[5] of integer、struct Point。 */
int Compiler::parseType() {
    // 类型分析的返回值不是字符串，而是 TYPEL 类型表中的编号。
    // 后面变量声明、函数返回值、形参类型都会保存这个编号。
    if (accept(TokenType::INTEGER_KW) || accept(TokenType::INT_KW)) return intType;
    if (accept(TokenType::REAL_KW) || accept(TokenType::FLOAT_KW)) return realType;
    if (accept(TokenType::CHAR_KW)) return charType;
    if (accept(TokenType::STRING_KW)) return stringType;
    if (accept(TokenType::BOOLEAN_KW)) return boolType;
    if (accept(TokenType::VOID_KW)) return voidType;
    if (accept(TokenType::ARRAY)) {
        expect(TokenType::LBRACKET, "[");
        if (!check(TokenType::INT_LITERAL)) semanticError("array length must be an integer constant");
        int len = stoi(cur.lexeme);
        advance();
        expect(TokenType::RBRACKET, "]");
        expect(TokenType::OF, "of");
        int elem = parseType();
        return addArrayType(elem, len);
    }
    if (accept(TokenType::STRUCT)) {
        if (!check(TokenType::ID)) semanticError("expected struct name");
        string name = cur.lexeme;
        advance();
        int idx = lookup(name);
        if (idx == -1 || symbolTable[idx].kind != SymbolKind::TYPE || typeTable[symbolTable[idx].typeId].kind != TypeKind::STRUCT)
            semanticError("unknown struct type: " + name);
        return symbolTable[idx].typeId;
    }
    if (check(TokenType::ID)) {
        string name = cur.lexeme;
        int idx = lookup(name);
        if (idx != -1 && symbolTable[idx].kind == SymbolKind::TYPE) {
            advance();
            return symbolTable[idx].typeId;
        }
    }
    semanticError("expected a type specifier");
}

/* parseArraySuffix：处理 C 风格数组后缀，例如 a[10]。 */
int Compiler::parseArraySuffix(int baseType) {
    int typeId = baseType;
    while (accept(TokenType::LBRACKET)) {
        if (!check(TokenType::INT_LITERAL)) semanticError("array length must be an integer constant");
        int len = stoi(cur.lexeme);
        advance();
        expect(TokenType::RBRACKET, "]");
        typeId = addArrayType(typeId, len);
    }
    return typeId;
}

/* parseProgram：分析整个程序 program id; ... begin ... end. */
void Compiler::parseProgram() {
    // 程序整体格式大致是：program 名字; 声明部分 begin 语句 end.
    // 声明部分可以包含 const、var、struct、function、procedure。
    if (accept(TokenType::PROGRAM)) {
        if (!check(TokenType::ID)) semanticError("expected program name");
        programName = cur.lexeme;
        addSymbol(programName, voidType, SymbolKind::TYPE, -1);
        advance();
        accept(TokenType::SEMI);
    } else {
        programName = "anonymous";
    }

    while (!check(TokenType::BEGIN_) && !check(TokenType::LBRACE) && !check(TokenType::END_OF_FILE)) {
        parseTopLevelDeclaration();
    }

    if (check(TokenType::BEGIN_) || check(TokenType::LBRACE)) {
        emit("main", "_", "_", "_");
        parseCompoundStatement(true);
        emit("endmain", "_", "_", "_");
    } else {
        semanticError("expected main compound statement: begin ... end");
    }
    accept(TokenType::DOT);
    if (!check(TokenType::END_OF_FILE)) semanticError("extra text after program end");
}

/* parseTopLevelDeclaration：分析全局层的 const、var、struct、function、procedure。 */
void Compiler::parseTopLevelDeclaration() {
    // 全局声明的入口函数。
    // 根据当前 token 判断后面是哪一种声明。
    if (accept(TokenType::CONST_KW)) { parseConstSection(); return; }
    if (accept(TokenType::VAR)) { parseVarSection(); return; }
    if (check(TokenType::STRUCT)) { parseStructDefinition(); return; }
    if (accept(TokenType::FUNCTION)) { parseFunctionDefinition(false); return; }
    if (accept(TokenType::PROCEDURE)) { parseFunctionDefinition(true); return; }
    if (isTypeStart(cur.type)) {
        int typeId = parseType();
        if (!check(TokenType::ID)) semanticError("expected identifier after type");
        string name = cur.lexeme; advance();
        if (check(TokenType::LPAREN)) {
            // C 风格函数：int f(int a) { ... }
            FuncInfo fi; fi.name = name; fi.retType = typeId; fi.entryQuad = (int)quads.size();
            int fidx = (int)funcTable.size(); funcTable.push_back(fi);
            int ftype = addFuncType(fidx);
            addSymbol(name, ftype, SymbolKind::FUNC, fidx);
            int oldFunc = currentFuncIndex; currentFuncIndex = fidx;
            int fscope = enterScope(); funcTable[fidx].scopeId = fscope;
            expect(TokenType::LPAREN, "(");
            if (!check(TokenType::RPAREN)) {
                do {
                    int ptype = parseType();
                    ParamMode pm = ParamMode::VALUE;
                    // C 风格 int *p 记为 vn；这里只做符号表语义，不引入完整指针类型系统。
                    if (accept(TokenType::STAR)) pm = ParamMode::ADDRESS;
                    if (!check(TokenType::ID)) semanticError("expected parameter name");
                    string pname = cur.lexeme; advance();
                    ptype = parseArraySuffix(ptype);
                    funcTable[fidx].paramTypes.push_back(ptype);
                    funcTable[fidx].paramNames.push_back(pname);
                    funcTable[fidx].paramModes.push_back(pm);
                    addSymbol(pname, ptype, SymbolKind::PARAM, fidx, pm);
                } while (accept(TokenType::COMMA));
            }
            expect(TokenType::RPAREN, ")");
            emit("function", name, "_", "_");
            parseCompoundStatement(false);
            emit("end", name, "_", "_");
            leaveScope(); currentFuncIndex = oldFunc;
            return;
        }
        parseCStyleVarDeclaration(typeId, name);
        return;
    }
    semanticError("unexpected top-level declaration");
}

/* parseConstSection：分析 const 常量定义，并建立 CONSL 和 CAT=c 项。 */
void Compiler::parseConstSection() {
    // Pascal 风格常量定义：const PI = 3.14; MAX = 100;
    // 常量名进入 SYNBL，CAT=c，ADDR 指向 CONSL。
    while (check(TokenType::ID)) {
        string name = cur.lexeme;
        advance();
        if (!(accept(TokenType::EQ) || accept(TokenType::ASSIGN))) semanticError("expected '=' or ':=' in const declaration");
        Expr e = parseExpression();
        if (!e.isConst && typeTable[e.typeId].kind != TypeKind::CHAR && typeTable[e.typeId].kind != TypeKind::STRING)
            semanticError("const declaration requires a constant expression");
        string value = e.text.empty() ? e.place : e.text;
        int cidx = addConst(value, e.typeId);
        addSymbol(name, e.typeId, SymbolKind::CONST_, cidx);
        expect(TokenType::SEMI, ";");
    }
}

/* parseVarSection：分析 var 声明段，里面可以有多行变量声明。 */
void Compiler::parseVarSection() {
    while (check(TokenType::ID)) parseVarDeclarationLine();
}

/* parseVarDeclarationLine：分析一行 Pascal 变量声明，如 a,b: integer;。 */
void Compiler::parseVarDeclarationLine() {
    vector<string> names;
    if (!check(TokenType::ID)) semanticError("expected identifier in var declaration");
    names.push_back(cur.lexeme); advance();
    while (accept(TokenType::COMMA)) {
        if (!check(TokenType::ID)) semanticError("expected identifier after ','");
        names.push_back(cur.lexeme); advance();
    }
    expect(TokenType::COLON, ":");
    int typeId = parseType();
    expect(TokenType::SEMI, ";");
    for (const string &n : names) addSymbol(n, typeId, SymbolKind::VAR, -1);
}

/* parseCStyleVarDeclaration：兼容 C 风格局部变量声明。 */
void Compiler::parseCStyleVarDeclaration(int typeId, string firstName) {
    while (true) {
        int vtype = parseArraySuffix(typeId);
        addSymbol(firstName, vtype, SymbolKind::VAR, -1);
        if (accept(TokenType::ASSIGN)) {
            Expr e = parseExpression();
            if (!assignCompatible(vtype, e.typeId)) semanticError("cannot assign " + typeTable[e.typeId].name + " to " + typeTable[vtype].name);
            emit(":=", e.place, "_", firstName);
        }
        if (!accept(TokenType::COMMA)) break;
        if (!check(TokenType::ID)) semanticError("expected variable name after ','");
        firstName = cur.lexeme; advance();
    }
    expect(TokenType::SEMI, ";");
}

/* parseStructDefinition：分析结构体定义，填写 RINFL 和域名 CAT=d。 */
void Compiler::parseStructDefinition() {
    expect(TokenType::STRUCT, "struct");
    if (!check(TokenType::ID)) semanticError("expected struct name");
    string sname = cur.lexeme; advance();
    expect(TokenType::LBRACE, "{");
    vector<StructMember> members;
    int offset = 0;
    set<string> fieldNames;
    while (!check(TokenType::RBRACE)) {
        int ftype;
        vector<string> fnames;
        if (isTypeStart(cur.type)) {
            ftype = parseType();
            if (!check(TokenType::ID)) semanticError("expected field name");
            fnames.push_back(cur.lexeme); advance();
            while (accept(TokenType::COMMA)) {
                if (!check(TokenType::ID)) semanticError("expected field name after ','");
                fnames.push_back(cur.lexeme); advance();
            }
        } else if (check(TokenType::ID)) {
            // Pascal 风格域：x,y: integer;
            fnames.push_back(cur.lexeme); advance();
            while (accept(TokenType::COMMA)) { if (!check(TokenType::ID)) semanticError("expected field name"); fnames.push_back(cur.lexeme); advance(); }
            expect(TokenType::COLON, ":");
            ftype = parseType();
        } else semanticError("expected field declaration");
        expect(TokenType::SEMI, ";");
        for (auto &fn : fnames) {
            if (fieldNames.count(fn)) semanticError("duplicate field " + fn + " in struct " + sname);
            fieldNames.insert(fn);
            members.push_back({fn, ftype, offset});
            offset += max(1, typeTable[ftype].width);
        }
    }
    expect(TokenType::RBRACE, "}");
    accept(TokenType::SEMI);
    int typeId = addStructType(sname, members, offset);
    int stIdx = (int)structTable.size() - 1;
    addSymbol(sname, typeId, SymbolKind::TYPE, stIdx);
    for (const auto &m : members) addFieldSymbol(sname, m, stIdx);
}

/* parseFunctionDefinition：分析 function/procedure，并识别 vf、vn 形参。 */
void Compiler::parseFunctionDefinition(bool procedure) {
    // procedure=true 表示过程，没有返回值；false 表示函数，有返回值。
    // 形参前面有 var 时按 vn 处理，没有 var 时按 vf 处理。
    if (!check(TokenType::ID)) semanticError("expected function/procedure name");
    string name = cur.lexeme; advance();
    FuncInfo fi; fi.name = name; fi.retType = procedure ? voidType : intType; fi.entryQuad = (int)quads.size();
    int fidx = (int)funcTable.size(); funcTable.push_back(fi);
    int ftype = addFuncType(fidx);
    addSymbol(name, ftype, SymbolKind::FUNC, fidx);

    int oldFunc = currentFuncIndex; currentFuncIndex = fidx;
    int fscope = enterScope(); funcTable[fidx].scopeId = fscope;

    expect(TokenType::LPAREN, "(");
    if (!check(TokenType::RPAREN)) {
        do {
            if (isTypeStart(cur.type)) {
                int ptype = parseType();
                ParamMode pm = ParamMode::VALUE;
                if (accept(TokenType::STAR)) pm = ParamMode::ADDRESS;
                if (!check(TokenType::ID)) semanticError("expected parameter name");
                string pname = cur.lexeme; advance();
                funcTable[fidx].paramTypes.push_back(ptype);
                funcTable[fidx].paramNames.push_back(pname);
                funcTable[fidx].paramModes.push_back(pm);
                addSymbol(pname, ptype, SymbolKind::PARAM, fidx, pm);
            } else {
                vector<string> names;
                ParamMode pm = ParamMode::VALUE;
                // Pascal 风格：var x: integer 表示地址形参 vn；无 var 表示赋值形参 vf。
                if (accept(TokenType::VAR)) pm = ParamMode::ADDRESS;
                if (!check(TokenType::ID)) semanticError("expected parameter name");
                names.push_back(cur.lexeme); advance();
                while (accept(TokenType::COMMA)) { if (!check(TokenType::ID)) semanticError("expected parameter name"); names.push_back(cur.lexeme); advance(); }
                expect(TokenType::COLON, ":");
                int ptype = parseType();
                for (auto &pname : names) {
                    funcTable[fidx].paramTypes.push_back(ptype);
                    funcTable[fidx].paramNames.push_back(pname);
                    funcTable[fidx].paramModes.push_back(pm);
                    addSymbol(pname, ptype, SymbolKind::PARAM, fidx, pm);
                }
            }
        } while (accept(TokenType::SEMI) || accept(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, ")");
    if (!procedure) {
        expect(TokenType::COLON, ":");
        funcTable[fidx].retType = parseType();
    }
    expect(TokenType::SEMI, ";");
    while (accept(TokenType::VAR)) parseVarSection();
    emit("function", name, "_", "_");
    parseCompoundStatement(false);
    emit("end", name, "_", "_");
    accept(TokenType::SEMI);
    leaveScope(); currentFuncIndex = oldFunc;
}

/* parseCompoundStatement：分析 begin...end 或 {...} 复合语句。 */
void Compiler::parseCompoundStatement(bool createScope) {
    bool pascal = false;
    if (accept(TokenType::BEGIN_)) pascal = true;
    else expect(TokenType::LBRACE, "{");
    if (createScope) enterScope();
    while (!(pascal ? check(TokenType::END_) : check(TokenType::RBRACE)) && !check(TokenType::END_OF_FILE)) {
        if (accept(TokenType::SEMI)) continue;
        parseStatement();
        while (accept(TokenType::SEMI)) {
            if (check(TokenType::ELSE) || (pascal ? check(TokenType::END_) : check(TokenType::RBRACE))) break;
        }
    }
    if (pascal) expect(TokenType::END_, "end"); else expect(TokenType::RBRACE, "}");
    if (createScope) leaveScope();
}

/* parseStatement：根据当前 Token 分派到 if、while、write、赋值等语句。 */
void Compiler::parseStatement() {
    if (check(TokenType::BEGIN_) || check(TokenType::LBRACE)) { parseCompoundStatement(true); return; }
    if (check(TokenType::IF)) { parseIfStatement(); return; }
    if (check(TokenType::WHILE)) { parseWhileStatement(); return; }
    if (check(TokenType::WRITE)) { parseWriteStatement(); return; }
    if (check(TokenType::RETURN)) { parseReturnStatement(); return; }
    if (accept(TokenType::VAR)) { parseVarSection(); return; }
    if (isTypeStart(cur.type)) {
        int typeId = parseType();
        if (!check(TokenType::ID)) semanticError("expected local variable name");
        string n = cur.lexeme; advance();
        parseCStyleVarDeclaration(typeId, n); return;
    }
    if (check(TokenType::ID)) { parseAssignmentOrCall(); return; }
    semanticError("unexpected statement");
}

/* parseIfStatement：分析 if 条件 then 语句 [else 语句]，生成跳转四元式。 */
void Compiler::parseIfStatement() {
    expect(TokenType::IF, "if");
    Expr cond;
    if (accept(TokenType::LPAREN)) { cond = parseExpression(); expect(TokenType::RPAREN, ")"); }
    else cond = parseExpression();
    expect(TokenType::THEN, "then");
    string lElse = newLabel(), lEnd = newLabel();
    emit("ifFalse", cond.place, "_", lElse);
    parseStatement();
    while (accept(TokenType::SEMI)) { if (check(TokenType::ELSE)) break; }
    if (accept(TokenType::ELSE)) {
        emit("goto", "_", "_", lEnd);
        emit("label", "_", "_", lElse);
        parseStatement();
        emit("label", "_", "_", lEnd);
    } else {
        emit("label", "_", "_", lElse);
    }
}

/* parseWhileStatement：分析 while 条件 do 语句，生成循环跳转四元式。 */
void Compiler::parseWhileStatement() {
    expect(TokenType::WHILE, "while");
    string lStart = newLabel(), lEnd = newLabel();
    emit("label", "_", "_", lStart);
    Expr cond;
    if (accept(TokenType::LPAREN)) { cond = parseExpression(); expect(TokenType::RPAREN, ")"); }
    else cond = parseExpression();
    expect(TokenType::DO, "do");
    emit("ifFalse", cond.place, "_", lEnd);
    parseStatement();
    emit("goto", "_", "_", lStart);
    emit("label", "_", "_", lEnd);
}

/* parseWriteStatement：分析 write(...)，同时生成 write 四元式。 */
void Compiler::parseWriteStatement() {
    expect(TokenType::WRITE, "write");
    expect(TokenType::LPAREN, "(");
    if (!check(TokenType::RPAREN)) {
        do {
            Expr e = parseExpression();
            emit("write", e.place, "_", "_");
        } while (accept(TokenType::COMMA));
    }
    expect(TokenType::RPAREN, ")");
}

/* parseReturnStatement：分析 return 语句，并检查返回值类型。 */
void Compiler::parseReturnStatement() {
    expect(TokenType::RETURN, "return");
    if (check(TokenType::SEMI) || check(TokenType::END_) || check(TokenType::RBRACE)) emit("return", "_", "_", "_");
    else {
        Expr e = parseExpression();
        if (currentFuncIndex >= 0 && !assignCompatible(funcTable[currentFuncIndex].retType, e.typeId)) semanticError("return type mismatch");
        emit("return", e.place, "_", "_");
    }
}

/* parseAssignmentOrCall：看到 ID 开头时，判断是赋值还是函数/过程调用。 */
void Compiler::parseAssignmentOrCall() {
    string name = cur.lexeme; advance();
    if (check(TokenType::LPAREN)) {
        int idx = lookup(name);
        if (idx == -1 || symbolTable[idx].kind != SymbolKind::FUNC) semanticError("undefined function: " + name);
        int fidx = symbolTable[idx].extra;
        expect(TokenType::LPAREN, "(");
        vector<Expr> args;
        if (!check(TokenType::RPAREN)) { do { args.push_back(parseExpression()); } while (accept(TokenType::COMMA)); }
        expect(TokenType::RPAREN, ")");
        if (args.size() != funcTable[fidx].paramTypes.size()) semanticError("argument count mismatch in call to " + name);
        for (size_t i = 0; i < args.size(); ++i) {
            if (!assignCompatible(funcTable[fidx].paramTypes[i], args[i].typeId)) semanticError("argument type mismatch in call to " + name);
            if (i < funcTable[fidx].paramModes.size() && funcTable[fidx].paramModes[i] == ParamMode::ADDRESS && args[i].isConst)
                semanticError("vn parameter requires a variable/designator, not a constant");
            emit("param", args[i].place, (i < funcTable[fidx].paramModes.size() ? catCode(SymbolKind::PARAM, funcTable[fidx].paramModes[i]) : "vf"), "_");
        }
        emit("call", name, to_string(args.size()), "_");
        return;
    }
    Expr lhs = parseDesignatorFromName(name, false);
    expect(TokenType::ASSIGN, ":=");
    Expr rhs = parseExpression();
    if (!assignCompatible(lhs.typeId, rhs.typeId)) semanticError("cannot assign " + typeTable[rhs.typeId].name + " to " + typeTable[lhs.typeId].name);
    // LHS place 可能是 arr[index] 或 obj.field，用统一字符串输出。
    if (lhs.place.find('[') != string::npos) {
        auto p = lhs.place.find('['); string base = lhs.place.substr(0, p); string idx = lhs.place.substr(p + 1, lhs.place.size() - p - 2);
        emit("[]=", rhs.place, idx, base);
    } else if (lhs.place.find('.') != string::npos) {
        auto p = lhs.place.find('.'); string base = lhs.place.substr(0, p); string field = lhs.place.substr(p + 1);
        emit(".=", rhs.place, field, base);
    } else emit(":=", rhs.place, "_", lhs.place);
}

/* parseExpression：表达式入口，从逻辑或开始，逐层处理优先级。 */
Compiler::Expr Compiler::parseExpression() {
    // 表达式入口。优先级最低的是逻辑或，所以先调用 parseLogicalOr。
    return parseLogicalOr();
}
/* parseLogicalOr：处理 || 运算。 */
Compiler::Expr Compiler::parseLogicalOr() {
    // 逻辑或 || 的优先级最低。
    Expr left = parseLogicalAnd();
    while (accept(TokenType::OR)) {
        Expr right = parseLogicalAnd();
        string t = newTemp(boolType);
        emit("||", left.place, right.place, t);
        left = {t, boolType, false, 0, ""};
    }
    return left;
}
/* parseLogicalAnd：处理 && 运算。 */
Compiler::Expr Compiler::parseLogicalAnd() {
    // 逻辑与 &&。
    Expr left = parseEquality();
    while (accept(TokenType::AND)) {
        Expr right = parseEquality();
        string t = newTemp(boolType);
        emit("&&", left.place, right.place, t);
        left = {t, boolType, false, 0, ""};
    }
    return left;
}
/* parseEquality：处理 =、<>、==、!= 比较。 */
Compiler::Expr Compiler::parseEquality() {
    // 等于/不等于关系。
    Expr left = parseRelational();
    while (check(TokenType::EQ) || check(TokenType::NE)) {
        string op = cur.lexeme == "!=" ? "<>" : cur.lexeme;
        advance();
        Expr right = parseRelational();
        string t = newTemp(boolType);
        emit(op == "==" ? "=" : op, left.place, right.place, t);
        left = {t, boolType, false, 0, ""};
    }
    return left;
}
/* parseRelational：处理 <、>、<=、>= 比较。 */
Compiler::Expr Compiler::parseRelational() {
    // 大小关系 < > <= >=。
    Expr left = parseAdditive();
    while (check(TokenType::LT) || check(TokenType::GT) || check(TokenType::LE) || check(TokenType::GE)) {
        string op = cur.lexeme; advance();
        Expr right = parseAdditive();
        if (!isNumeric(left.typeId) || !isNumeric(right.typeId)) semanticError("relational operands must be numeric");
        string t = newTemp(boolType);
        emit(op, left.place, right.place, t);
        left = {t, boolType, false, 0, ""};
    }
    return left;
}
/* parseAdditive：处理 +、-，并在纯常量时直接折叠。 */
Compiler::Expr Compiler::parseAdditive() {
    // 加减法，优先级低于乘除法。
    Expr left = parseMultiplicative();
    while (check(TokenType::PLUS) || check(TokenType::MINUS)) {
        string op = cur.lexeme; advance();
        Expr right = parseMultiplicative();
        int rtype = arithmeticResult(left.typeId, right.typeId);
        if (left.isConst && right.isConst) {
            double v = op == "+" ? left.num + right.num : left.num - right.num;
            string s = trimNumber(v); addConst(s, rtype);
            left = {s, rtype, true, v, s};
        } else {
            string t = newTemp(rtype);
            emit(op, left.place, right.place, t);
            left = {t, rtype, false, 0, ""};
        }
    }
    return left;
}
/* parseMultiplicative：处理 *、/，并在纯常量时直接折叠。 */
Compiler::Expr Compiler::parseMultiplicative() {
    // 乘除法。
    Expr left = parseUnary();
    while (check(TokenType::STAR) || check(TokenType::SLASH)) {
        string op = cur.lexeme; advance();
        Expr right = parseUnary();
        int rtype = op == "/" ? realType : arithmeticResult(left.typeId, right.typeId);
        if (left.isConst && right.isConst && !(op == "/" && fabs(right.num) < 1e-12)) {
            double v = op == "*" ? left.num * right.num : left.num / right.num;
            string s = trimNumber(v); addConst(s, rtype);
            left = {s, rtype, true, v, s};
        } else {
            string t = newTemp(rtype);
            emit(op, left.place, right.place, t);
            left = {t, rtype, false, 0, ""};
        }
    }
    return left;
}
/* parseUnary：处理一元负号和逻辑非。 */
Compiler::Expr Compiler::parseUnary() {
    // 一元运算，例如 -x 和 !x。
    if (accept(TokenType::MINUS)) {
        Expr e = parseUnary();
        if (!isNumeric(e.typeId)) semanticError("unary '-' requires numeric operand");
        if (e.isConst) {
            double v = -e.num; string s = trimNumber(v); addConst(s, e.typeId);
            return {s, e.typeId, true, v, s};
        }
        string t = newTemp(e.typeId); emit("neg", e.place, "_", t); return {t, e.typeId, false, 0, ""};
    }
    if (accept(TokenType::NOT)) {
        Expr e = parseUnary();
        string t = newTemp(boolType); emit("!", e.place, "_", t); return {t, boolType, false, 0, ""};
    }
    return parsePrimary();
}
/* parsePrimary：处理常量、标识符、函数调用、括号表达式。 */
Compiler::Expr Compiler::parsePrimary() {
    // 最基本的表达式单元：常量、变量、函数调用、括号表达式。
    if (check(TokenType::INT_LITERAL)) {
        string s = cur.lexeme; advance(); addConst(s, intType); return {s, intType, true, stod(s), s};
    }
    if (check(TokenType::REAL_LITERAL)) {
        string s = cur.lexeme; advance(); addConst(s, realType); return {s, realType, true, stod(s), s};
    }
    if (check(TokenType::CHAR_LITERAL)) {
        string s = cur.lexeme; advance(); addConst(s, charType); return {s, charType, false, 0, s};
    }
    if (check(TokenType::STRING_LITERAL)) {
        string s = cur.lexeme; advance(); addConst(s, stringType); return {s, stringType, false, 0, s};
    }
    if (accept(TokenType::TRUE_KW)) return {"1", boolType, true, 1, "true"};
    if (accept(TokenType::FALSE_KW)) return {"0", boolType, true, 0, "false"};
    if (accept(TokenType::LPAREN)) { Expr e = parseExpression(); expect(TokenType::RPAREN, ")"); return e; }
    if (check(TokenType::ID)) {
        string name = cur.lexeme; advance();
        if (check(TokenType::LPAREN)) {
            int idx = lookup(name);
            if (idx == -1 || symbolTable[idx].kind != SymbolKind::FUNC) semanticError("undefined function: " + name);
            int fidx = symbolTable[idx].extra;
            expect(TokenType::LPAREN, "(");
            vector<Expr> args;
            if (!check(TokenType::RPAREN)) { do { args.push_back(parseExpression()); } while (accept(TokenType::COMMA)); }
            expect(TokenType::RPAREN, ")");
            if (args.size() != funcTable[fidx].paramTypes.size()) semanticError("argument count mismatch in call to " + name);
            for (size_t i = 0; i < args.size(); ++i) {
                if (!assignCompatible(funcTable[fidx].paramTypes[i], args[i].typeId)) semanticError("argument type mismatch");
                if (i < funcTable[fidx].paramModes.size() && funcTable[fidx].paramModes[i] == ParamMode::ADDRESS && args[i].isConst)
                    semanticError("vn parameter requires a variable/designator, not a constant");
                emit("param", args[i].place, (i < funcTable[fidx].paramModes.size() ? catCode(SymbolKind::PARAM, funcTable[fidx].paramModes[i]) : "vf"), "_");
            }
            int rt = funcTable[fidx].retType;
            string t = newTemp(rt);
            emit("call", name, to_string(args.size()), t);
            return {t, rt, false, 0, ""};
        }
        return parseDesignatorFromName(name, true);
    }
    semanticError("expected expression");
}

/* parseDesignatorFromName：处理变量、数组元素、结构体成员访问。 */
Compiler::Expr Compiler::parseDesignatorFromName(const string &name, bool loadValue) {
    int idx = lookup(name);
    if (idx == -1) semanticError("undefined identifier: " + name);
    SymbolEntry &sym = symbolTable[idx];
    int typeId = sym.typeId;

    // 常量标识符 c 只能作为右值，不能作为赋值左部。
    if (sym.kind == SymbolKind::CONST_) {
        if (!loadValue) semanticError("constant cannot appear on the left side of assignment: " + name);
        string v = (sym.extra >= 0 && sym.extra < (int)constTable.size()) ? constTable[sym.extra].value : name;
        bool numeric = typeTable[typeId].kind == TypeKind::INT || typeTable[typeId].kind == TypeKind::REAL || typeTable[typeId].kind == TypeKind::BOOL;
        return {v, typeId, numeric, numeric ? stod(v) : 0, v};
    }
    if (sym.kind != SymbolKind::VAR && sym.kind != SymbolKind::PARAM) semanticError(name + " is not a value identifier");
    string place = name;
    while (check(TokenType::LBRACKET) || check(TokenType::DOT) || check(TokenType::ARROW)) {
        if (accept(TokenType::LBRACKET)) {
            if (typeTable[typeId].kind != TypeKind::ARRAY) semanticError(name + " is not an array");
            Expr sub = parseExpression();
            if (!isNumeric(sub.typeId)) semanticError("array index must be integer-compatible");
            expect(TokenType::RBRACKET, "]");
            int elem = typeTable[typeId].elemType;
            if (loadValue) {
                string t = newTemp(elem); emit("=[]", place, sub.place, t); place = t;
            } else place = place + "[" + sub.place + "]";
            typeId = elem;
        } else {
            bool arrow = accept(TokenType::ARROW);
            if (!arrow) expect(TokenType::DOT, ".");
            if (typeTable[typeId].kind != TypeKind::STRUCT) semanticError(name + " is not a struct");
            if (!check(TokenType::ID)) semanticError("expected field name");
            string field = cur.lexeme; advance();
            StructInfo &st = structTable[typeTable[typeId].structIndex];
            bool found = false; int ftype = -1;
            for (auto &m : st.members) if (m.name == field) { found = true; ftype = m.typeId; break; }
            if (!found) semanticError("struct " + st.name + " has no field " + field);
            if (loadValue) { string t = newTemp(ftype); emit(arrow ? "->" : ".", place, field, t); place = t; }
            else place += "." + field;
            typeId = ftype;
        }
    }
    return {place, typeId, false, 0, ""};
}

