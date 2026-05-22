#pragma once
#include "lexer.h"
#include "symbols.h"

/*
 * compiler.hpp
 * 递归下降编译器的类定义。
 *
 * 这个类把前端和后端需要的数据都串起来：
 *   1. 先调用 Lexer 做词法分析；
 *   2. 再用递归下降分析 Pascal 风格程序；
 *   3. 顺便建立符号表、类型表、常量表；
 *   4. 生成四元式；
 *   5. 最后解释四元式，检查 write 的输出结果。
 */

// =============================================================
// 递归下降编译器前端
// =============================================================
class Compiler {
    // 词法分析器
    Lexer lexer;

    // 当前正在看的那个 token
    Token cur;

    // 当前作用域编号
    int currentScope = 0;

    // 作用域栈：用于支持嵌套作用域
    vector<int> scopeStack = {0};

    // 每个作用域对应一个名字表
    vector<unordered_map<string, int>> scopedSymbols;

    // 每个作用域自己的偏移量，变量和形参分配空间时会用到
    vector<int> scopeOffset;

    // 当前所在函数的编号
    int currentFuncIndex = -1;

    // 临时变量编号、标号编号
    int tempId = 0, labelId = 0;

    // 基本类型在类型表里的编号
    int intType = -1, realType = -1, charType = -1, stringType = -1, boolType = -1, voidType = -1;

    /*
     * advance
     * 功能：读下一个 Token。
     * 递归下降分析里经常要调用它。
     */
    void advance() { cur = lexer.nextToken(); }

    /*
     * check
     * 功能：判断当前 token 是否是某种类型。
     * 不会改变当前读取位置。
     */
    bool check(TokenType t) const { return cur.type == t; }

    /*
     * accept
     * 功能：如果当前 token 正好匹配，就吃掉它并读下一个。
     * 常用于“可有可无”的语法部分。
     */
    bool accept(TokenType t) {
        if (check(t)) {
            advance();
            return true;
        }
        return false;
    }

    /*
     * expect
     * 功能：要求当前 token 必须是指定类型。
     * 如果不是，就抛出语法错误。
     */
    void expect(TokenType t, const string &what) {
        if (!check(t)) {
            throw runtime_error(
                "Syntax error at line " + to_string(cur.line) +
                ", column " + to_string(cur.col) +
                ": expected " + what +
                ", got '" + cur.lexeme + "' (" + tokenName(cur.type) + ")"
            );
        }
        advance();
    }

    /*
     * semanticError
     * 功能：抛出语义错误。
     * 比如重复定义、类型不匹配等问题。
     */
    [[noreturn]] void semanticError(const string &msg) const {
        throw runtime_error("Semantic error at line " + to_string(cur.line) +
                            ", column " + to_string(cur.col) + ": " + msg);
    }

public:
    // 类型表
    vector<TypeEntry> typeTable;

    // 数组表
    vector<ArrayInfo> arrayTable;

    // 结构体表
    vector<StructInfo> structTable;

    // 函数表
    vector<FuncInfo> funcTable;

    // 常量表
    vector<ConstEntry> constTable;

    // 主符号表
    vector<SymbolEntry> symbolTable;

    // 活动记录表
    vector<ActRecordEntry> actRecords;

    // 四元式序列
    vector<Quad> quads;

    // 运行时输出
    vector<string> runtimeOutput;

    // 程序名
    string programName = "";

    /*
     * 构造函数
     * 作用：
     *   1. 初始化词法分析器
     *   2. 创建全局作用域
     *   3. 初始化基本类型
     *   4. 读入第一个 token
     */
    explicit Compiler(const string &src) : lexer(src) {
        scopedSymbols.push_back({});
        scopeOffset.push_back(0);
        initBasicTypes();
        advance();
    }

    /*
     * initBasicTypes
     * 功能：把最基础的类型先放进类型表。
     * 包括 integer、real、char、string、boolean、void。
     */
    void initBasicTypes() {
        intType = addType(TypeKind::INT, "integer", 4);
        realType = addType(TypeKind::REAL, "real", 8);
        charType = addType(TypeKind::CHAR, "char", 1);
        stringType = addType(TypeKind::STRING, "string", 32);
        boolType = addType(TypeKind::BOOL, "boolean", 1);
        voidType = addType(TypeKind::VOID, "void", 0);
    }

    /*
     * addType
     * 功能：向类型表里添加一种类型。
     * 这里既能加基本类型，也能加后来生成的复合类型。
     */
    int addType(TypeKind k, const string &name, int width) {
        TypeEntry t;
        t.id = (int)typeTable.size();
        t.kind = k;
        t.name = name;
        t.width = width;
        typeTable.push_back(t);
        return t.id;
    }

    /*
     * addArrayType
     * 功能：创建数组类型，并把数组信息记到 AINFL 里。
     */
    int addArrayType(int elemType, int length) {
        if (length <= 0) semanticError("array length must be positive");

        TypeEntry t;
        t.id = (int)typeTable.size();
        t.kind = TypeKind::ARRAY;
        t.elemType = elemType;
        t.arrayLength = length;
        t.width = typeTable[elemType].width * length;
        t.name = "array[" + to_string(length) + "] of " + typeTable[elemType].name;

        int id = t.id;
        typeTable.push_back(t);
        arrayTable.push_back({id, 0, length - 1, elemType, t.width});
        return id;
    }

    /*
     * addStructType
     * 功能：创建结构体类型，并把成员信息记到 RINFL 里。
     */
    int addStructType(const string &name, const vector<StructMember> &members, int width) {
        StructInfo si;
        si.name = name;
        si.members = members;
        si.totalSize = width;

        int idx = (int)structTable.size();
        structTable.push_back(si);

        TypeEntry t;
        t.id = (int)typeTable.size();
        t.kind = TypeKind::STRUCT;
        t.name = "struct " + name;
        t.width = width;
        t.structIndex = idx;

        int typeId = t.id;
        typeTable.push_back(t);
        return typeId;
    }

    /*
     * addFuncType
     * 功能：给函数创建一个类型项。
     * 主要是为了让函数也能像其他类型一样被管理。
     */
    int addFuncType(int funcIndex) {
        TypeEntry t;
        t.id = (int)typeTable.size();
        t.kind = TypeKind::FUNCTION;
        t.name = "function";
        t.width = 0;
        t.funcIndex = funcIndex;

        int typeId = t.id;
        typeTable.push_back(t);
        return typeId;
    }

    /*
     * addConst
     * 功能：向常量表添加一项常量。
     * 如果同值同类型已经存在，就直接复用，不重复存。
     */
    int addConst(const string &value, int typeId) {
        for (size_t i = 0; i < constTable.size(); ++i) {
            if (constTable[i].value == value && constTable[i].typeId == typeId) return (int)i;
        }

        int cidx = (int)constTable.size();
        constTable.push_back({value, typeId});

        // 同时建立一个常量符号表项，方便统一展示
        SymbolEntry cs{"C" + to_string(cidx), typeId, SymbolKind::CONST_, 0, -1, cidx, ParamMode::NONE};
        symbolTable.push_back(cs);
        return cidx;
    }

    /*
     * enterScope
     * 功能：进入一个新的作用域。
     * 比如进入函数体、复合语句块、结构体定义内部等。
     */
    int enterScope() {
        int id = (int)scopedSymbols.size();
        scopedSymbols.push_back({});
        scopeOffset.push_back(0);
        currentScope = id;
        scopeStack.push_back(id);
        return id;
    }

    /*
     * leaveScope
     * 功能：离开当前作用域，回到外层作用域。
     */
    void leaveScope() {
        if (scopeStack.size() <= 1) return;
        scopeStack.pop_back();
        currentScope = scopeStack.back();
    }

    /*
     * lookupHere
     * 功能：只在当前作用域里查找名字。
     */
    int lookupHere(const string &name) const {
        auto it = scopedSymbols[currentScope].find(name);
        return it == scopedSymbols[currentScope].end() ? -1 : it->second;
    }

    /*
     * lookup
     * 功能：从当前作用域开始往外找标识符。
     * 这个逻辑支持“内层变量遮蔽外层变量”。
     */
    int lookup(const string &name) const {
        for (auto it = scopeStack.rbegin(); it != scopeStack.rend(); ++it) {
            auto f = scopedSymbols[*it].find(name);
            if (f != scopedSymbols[*it].end()) return f->second;
        }
        return -1;
    }

    /*
     * addSymbol
     * 功能：向主符号表 SYNBL 加入一个普通符号。
     *
     * 适用对象：
     *   - 变量
     *   - 形参
     *   - 函数名
     *   - 类型名
     *
     * extra 用来指向不同的附加表，比如活动记录表。
     */
    int addSymbol(const string &name, int typeId, SymbolKind kind, int extra = -1,
                  ParamMode pm = ParamMode::NONE) {
        if (lookupHere(name) != -1) semanticError("duplicate identifier in the same scope: " + name);

        if (kind == SymbolKind::PARAM && pm == ParamMode::NONE) pm = ParamMode::VALUE;

        int width = typeTable[typeId].width;
        int offset = scopeOffset[currentScope];

        // 变量和形参需要占用空间，所以要更新偏移量
        if (kind == SymbolKind::VAR || kind == SymbolKind::PARAM) scopeOffset[currentScope] += max(1, width);

        SymbolEntry s{name, typeId, kind, currentScope, offset, extra, pm};
        int idx = (int)symbolTable.size();
        symbolTable.push_back(s);
        scopedSymbols[currentScope][name] = idx;

        // 变量和形参要写入活动记录表，记录运行时存储信息
        if (kind == SymbolKind::VAR || kind == SymbolKind::PARAM) {
            string owner = currentFuncIndex >= 0 ? funcTable[currentFuncIndex].name : "<global/main>";
            int arIdx = (int)actRecords.size();
            actRecords.push_back({currentFuncIndex, owner, name, catCode(kind, pm), typeId, offset, width});
            symbolTable[idx].extra = arIdx;
        }

        return idx;
    }

    /*
     * addFieldSymbol
     * 功能：把结构体成员也登记到符号表里。
     *
     * 注意：
     *   结构体成员不放进普通作用域里，
     *   不然不同结构体里同名成员容易冲突。
     */
    int addFieldSymbol(const string &structName, const StructMember &m, int structIndex) {
        SymbolEntry fs{structName + "." + m.name, m.typeId, SymbolKind::FIELD, -1, m.offset, structIndex, ParamMode::NONE};
        int idx = (int)symbolTable.size();
        symbolTable.push_back(fs);
        return idx;
    }

    /*
     * newTemp
     * 功能：生成临时变量名，比如 t1、t2、t3。
     * 临时变量主要用来保存表达式中间结果。
     */
    string newTemp(int typeId) {
        string name = "t" + to_string(++tempId);
        if (lookupHere(name) == -1) addSymbol(name, typeId, SymbolKind::VAR, -1);
        return name;
    }

    /*
     * newLabel
     * 功能：生成跳转标签名，比如 L1、L2、L3。
     * 常用于 if 和 while 的四元式生成。
     */
    string newLabel() { return "L" + to_string(++labelId); }

    /*
     * emit
     * 功能：往四元式表里加入一条四元式。
     */
    void emit(const string &op, const string &a1, const string &a2, const string &res) {
        quads.push_back({op, a1, a2, res});
    }

    /*
     * isTypeStart
     * 功能：判断当前 token 能不能作为“类型”的开头。
     */
    bool isTypeStart(TokenType t) const {
        return t == TokenType::INTEGER_KW || t == TokenType::REAL_KW || t == TokenType::CHAR_KW ||
               t == TokenType::STRING_KW || t == TokenType::BOOLEAN_KW || t == TokenType::INT_KW ||
               t == TokenType::FLOAT_KW || t == TokenType::VOID_KW || t == TokenType::STRUCT ||
               t == TokenType::ARRAY;
    }

    /*
     * isNumeric
     * 功能：判断某个类型是不是数值类型。
     * 这里把 int、real、char、bool 都当成可以参与某些运算的类型。
     */
    bool isNumeric(int typeId) const {
        TypeKind k = typeTable[typeId].kind;
        return k == TypeKind::INT || k == TypeKind::REAL || k == TypeKind::CHAR || k == TypeKind::BOOL;
    }

    /*
     * assignCompatible
     * 功能：判断右边的类型能不能赋给左边的变量。
     */
    bool assignCompatible(int dst, int src) const {
        if (dst == src) return true;
        TypeKind dk = typeTable[dst].kind, sk = typeTable[src].kind;

        if ((dk == TypeKind::REAL || dk == TypeKind::INT) &&
            (sk == TypeKind::REAL || sk == TypeKind::INT || sk == TypeKind::CHAR || sk == TypeKind::BOOL))
            return true;

        if (dk == TypeKind::CHAR && sk == TypeKind::CHAR) return true;
        if (dk == TypeKind::BOOL && (sk == TypeKind::BOOL || sk == TypeKind::INT)) return true;

        return false;
    }

    /*
     * arithmeticResult
     * 功能：计算算术运算的结果类型。
     * 只要有一个操作数是 real，结果通常就是 real。
     */
    int arithmeticResult(int a, int b) const {
        if (!isNumeric(a) || !isNumeric(b)) throw runtime_error("arithmetic operands must be numeric");
        return (typeTable[a].kind == TypeKind::REAL || typeTable[b].kind == TypeKind::REAL) ? realType : intType;
    }

    /*
     * Expr
     * 这是表达式分析时临时用的小结构。
     *
     * place   : 表达式结果放在哪个名字里
     * typeId  : 这个表达式的类型
     * isConst : 这个表达式是不是常量
     * num/text: 常量值，按需要保存数字或字符串
     */
    struct Expr {
        string place;
        int typeId;
        bool isConst = false;
        double num = 0;
        string text;
    };

    // =========================
    // 下面这些函数都在 .cpp 里实现
    // =========================
    int parseType();
    int parseArraySuffix(int baseType);
    void parseProgram();
    void parseTopLevelDeclaration();
    void parseConstSection();
    void parseVarSection();
    void parseVarDeclarationLine();
    void parseCStyleVarDeclaration(int typeId, string firstName);
    void parseStructDefinition();
    void parseFunctionDefinition(bool procedure);
    void parseCompoundStatement(bool createScope = true);
    void parseStatement();
    void parseIfStatement();
    void parseWhileStatement();
    void parseWriteStatement();
    void parseReturnStatement();
    void parseAssignmentOrCall();
    Expr parseExpression();
    Expr parseLogicalOr();
    Expr parseLogicalAnd();
    Expr parseEquality();
    Expr parseRelational();
    Expr parseAdditive();
    Expr parseMultiplicative();
    Expr parseUnary();
    Expr parsePrimary();
    Expr parseDesignatorFromName(const string &name, bool loadValue);

    /*
     * compile
     * 功能：编译器总入口。
     * 流程就是：
     *   1. 先语法/语义分析整个程序
     *   2. 再执行四元式，看看输出对不对
     */
    void compile() {
        parseProgram();
        executeQuads();
    }

    // 下面这些函数负责把结果打印出来，方便调试和提交作业
    void printAll();
    void printLexicalTables();
    void printSymbolSystem();
    void printQuads();
    void executeQuads();
};
