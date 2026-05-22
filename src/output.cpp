#include "../include/compiler.h"

/*
 * output.cpp
 * ----------------------------------
 * 本文件负责打印各阶段结果。
 * 编译过程中的表很多，如果不集中写在这里，其他文件会比较乱。
 *
 * 输出内容分为下面几部分：
 * 1. 词法分析结果：Token 序列、关键字表、界符表、标识符表、常量表。
 * 2. 符号表系统：SYNBL、TYPEL、AINFL、RINFL、PFINFL、CONSL、VALUNIT。
 * 3. 原始四元式。
 * 4. 对四元式做简单解释执行，检查 write 输出是否符合源程序。
 */

// =============================================================
// 输出：词法、符号表、四元式、解释运行
// =============================================================
/* printLexicalTables：输出 Token 序列、关键字表、界符表、标识符表和常量表。 */
void Compiler::printLexicalTables() {
    // tokenSequence 中保存的是 (类别,编号)，例如 (k,1)、(i,2)。
    // 这里按词法分析课设常见格式输出。
    cout << "\n一、词法分析结果" << endl;
    cout << "Token 序列：" << endl;
    for (size_t i = 0; i < lexer.tokenSequence.size(); ++i) {
        if (i) cout << ",";
        cout << "(" << lexer.tokenSequence[i].first << "," << lexer.tokenSequence[i].second << ")";
    }
    cout << "\n关键字表：" << endl;
    for (size_t i = 0; i < lexer.keywordTable.size(); ++i) cout << setw(3) << i + 1 << "  " << lexer.keywordTable[i] << "\n";
    cout << "\n界符表：" << endl;
    for (size_t i = 0; i < lexer.delimiterTable.size(); ++i) cout << setw(3) << i + 1 << "  " << lexer.delimiterTable[i] << "\n";
    cout << "\n标识符表：" << endl;
    for (size_t i = 0; i < lexer.identifierTable.size(); ++i) cout << setw(3) << i + 1 << "  " << lexer.identifierTable[i] << "\n";
    cout << "\n常量表：" << endl;
    for (size_t i = 0; i < lexer.constantTable.size(); ++i) cout << setw(3) << i + 1 << "  " << lexer.constantTable[i] << "\n";
}

/* printSymbolSystem：按老师图里的 SYNBL/TYPEL/ADDR 关系输出完整符号表。 */
void Compiler::printSymbolSystem() {
    // 这个函数只是输出符号表，不改变符号表内容。
    // addrInfo 负责把 extra 字段解释成 CONSL、PFINFL、RINFL 等子表编号。
    cout << "\n" << string(110, '=') << "\n";
    cout << "二、符号表\n";
    cout << string(110, '=') << "\n";

    cout << "\n【0. 单词分类】\n";
    cout << "  K 关键字  -> 存在关键字表中\n";
    cout << "  P 界符    -> 存在界符表中\n";
    cout << "  I 标识符  -> 进入主符号表 SYNBL\n";
    cout << "  C 常量    -> 进入常量表 CONSL\n";

    cout << "\n【1. CAT 种类码说明】\n";
    cout << "  v  : 普通变量标识符，ADDR -> 活动记录/值单元分配表\n";
    cout << "  vf : 赋值形参/传值形参，ADDR -> 活动记录/值单元分配表\n";
    cout << "  vn : 指针形参/地址形参，ADDR -> 活动记录/值单元分配表\n";
    cout << "  c  : 常量标识符或字面量常量，ADDR -> CONSL 常量表\n";
    cout << "  f  : 函数标识符，ADDR -> PFINFL 函数信息表\n";
    cout << "  t  : 类型标识符，TYPE -> TYPEL；结构体类型的 TYPE.TPOINT -> RINFL\n";
    cout << "  d  : 域名标识符，ADDR -> RINFL 结构体信息表，offset 为成员偏移\n";

    auto addrInfo = [&](const SymbolEntry &s) -> string {
        switch (s.kind) {
            case SymbolKind::VAR:
            case SymbolKind::PARAM:
                return s.extra >= 0 ? "VALUNIT#" + to_string(s.extra) : "VALUNIT";
            case SymbolKind::CONST_:
                return s.extra >= 0 ? "CONSL#" + to_string(s.extra) : "CONSL";
            case SymbolKind::FUNC:
                return s.extra >= 0 ? "PFINFL#" + to_string(s.extra) : "PFINFL";
            case SymbolKind::TYPE:
                if (typeTable[s.typeId].kind == TypeKind::STRUCT) return "RINFL#" + to_string(typeTable[s.typeId].structIndex);
                if (typeTable[s.typeId].kind == TypeKind::ARRAY) return "AINFL";
                return "TYPEL#" + to_string(s.typeId);
            case SymbolKind::FIELD:
                return s.extra >= 0 ? "RINFL#" + to_string(s.extra) : "RINFL";
        }
        return "-";
    };

    cout << "\n【2. SYNBL 主符号表：NAME / CAT / TYPE / ADDR】\n";
    cout << left << setw(5) << "No" << setw(18) << "NAME" << setw(6) << "CAT"
         << setw(20) << "种类说明" << setw(8) << "TYPE" << setw(24) << "类型名"
         << setw(16) << "ADDR" << setw(8) << "Scope" << "Offset\n";
    cout << string(110, '-') << "\n";
    for (size_t i = 0; i < symbolTable.size(); ++i) {
        const auto &s = symbolTable[i];
        cout << left << setw(5) << i
             << setw(18) << s.name
             << setw(6) << catCode(s.kind, s.paramMode)
             << setw(20) << (s.kind == SymbolKind::PARAM ? paramModeName(s.paramMode) : symbolKindName(s.kind))
             << setw(8) << s.typeId
             << setw(24) << typeTable[s.typeId].name
             << setw(16) << addrInfo(s)
             << setw(8) << s.scopeId
             << s.offset << "\n";
    }

    cout << "\n【3. TYPEL 类型表：系统类型 + 用户类型】\n";
    cout << left << setw(6) << "TYPE" << setw(14) << "TVAL" << setw(32) << "类型名" << setw(10) << "宽度" << "TPOINT/说明\n";
    cout << string(92, '-') << "\n";
    for (auto &t : typeTable) {
        string ptr = "系统定义基本类型";
        if (t.kind == TypeKind::ARRAY) ptr = "TPOINT -> AINFL，elem=TYPE#" + to_string(t.elemType) + "，len=" + to_string(t.arrayLength);
        else if (t.kind == TypeKind::STRUCT) ptr = "TPOINT -> RINFL#" + to_string(t.structIndex);
        else if (t.kind == TypeKind::FUNCTION) ptr = "TPOINT -> PFINFL#" + to_string(t.funcIndex);
        cout << left << setw(6) << t.id << setw(14) << typeKindName(t.kind) << setw(32) << t.name
             << setw(10) << t.width << ptr << "\n";
    }

    cout << "\n【4. 类型长度表 / 类型体积信息】\n";
    cout << left << setw(8) << "TYPE" << setw(32) << "类型名" << "长度/宽度(byte)\n";
    cout << string(58, '-') << "\n";
    for (auto &t : typeTable) cout << left << setw(8) << t.id << setw(32) << t.name << t.width << "\n";

    cout << "\n【5. AINFL 数组信息表】\n";
    if (arrayTable.empty()) cout << "  <空>\n";
    else {
        cout << left << setw(8) << "No" << setw(8) << "TYPE" << setw(16) << "上下界" << setw(12) << "元素TYPE" << "总大小\n";
        cout << string(58, '-') << "\n";
        for (size_t i = 0; i < arrayTable.size(); ++i) {
            auto &a = arrayTable[i];
            cout << left << setw(8) << i << setw(8) << a.typeId
                 << setw(16) << ("[" + to_string(a.low) + "," + to_string(a.high) + "]")
                 << setw(12) << a.elemType << a.totalSize << "\n";
        }
    }

    cout << "\n【6. RINFL 结构体信息表】\n";
    if (structTable.empty()) cout << "  <空>\n";
    for (size_t i = 0; i < structTable.size(); ++i) {
        auto &st = structTable[i];
        cout << "  RINFL#" << i << "  struct " << st.name << "  totalSize=" << st.totalSize << "\n";
        cout << "      " << left << setw(18) << "域名" << setw(8) << "CAT" << setw(8) << "TYPE" << setw(24) << "类型名" << "偏移\n";
        for (auto &m : st.members) {
            cout << "      " << setw(18) << m.name << setw(8) << "d" << setw(8) << m.typeId << setw(24) << typeTable[m.typeId].name << m.offset << "\n";
        }
    }

    cout << "\n【7. PFINFL 函数信息表】\n";
    if (funcTable.empty()) cout << "  <空>\n";
    for (size_t i = 0; i < funcTable.size(); ++i) {
        auto &f = funcTable[i];
        vector<string> ps;
        for (size_t j = 0; j < f.paramNames.size(); ++j) {
            ParamMode m = j < f.paramModes.size() ? f.paramModes[j] : ParamMode::VALUE;
            ps.push_back(catCode(SymbolKind::PARAM, m) + string(":") + typeTable[f.paramTypes[j]].name + " " + f.paramNames[j]);
        }
        cout << "  PFINFL#" << i << "  " << f.name << "(" << join(ps, ", ") << ") : "
             << typeTable[f.retType].name << "  entryQuad=" << f.entryQuad << "  scope=" << f.scopeId << "\n";
    }

    cout << "\n【8. CONSL 常量表】\n";
    if (constTable.empty()) cout << "  <空>\n";
    else {
        cout << left << setw(8) << "No" << setw(22) << "常量值" << setw(8) << "TYPE" << "类型名\n";
        cout << string(58, '-') << "\n";
        for (size_t i = 0; i < constTable.size(); ++i)
            cout << left << setw(8) << i << setw(22) << constTable[i].value << setw(8) << constTable[i].typeId << typeTable[constTable[i].typeId].name << "\n";
    }

    cout << "\n【9. 活动记录 / 值单元分配表 VALUNIT】\n";
    if (actRecords.empty()) cout << "  <空>\n";
    else {
        cout << left << setw(8) << "No" << setw(18) << "Owner" << setw(16) << "Name" << setw(8) << "CAT"
             << setw(8) << "TYPE" << setw(24) << "类型名" << setw(8) << "Offset" << "Width\n";
        cout << string(96, '-') << "\n";
        for (size_t i = 0; i < actRecords.size(); ++i) {
            auto &a = actRecords[i];
            cout << left << setw(8) << i << setw(18) << a.owner << setw(16) << a.name << setw(8) << a.catCode
                 << setw(8) << a.typeId << setw(24) << typeTable[a.typeId].name << setw(8) << a.offset << a.width << "\n";
        }
    }
}

/* printQuads：输出前端生成的原始四元式。 */
void Compiler::printQuads() {
    // quads 是语义动作生成的原始四元式序列。
    cout << "\n三、三、四元式" << endl;
    cout << left << setw(6) << "No" << setw(12) << "Op" << setw(16) << "Arg1" << setw(16) << "Arg2" << "Result\n";
    cout << string(62, '-') << "\n";
    for (size_t i = 0; i < quads.size(); ++i) {
        cout << left << setw(6) << i << setw(12) << quads[i].op << setw(16) << quads[i].arg1 << setw(16) << quads[i].arg2 << quads[i].result << "\n";
    }
}

/* executeQuads：简单解释执行四元式，只为检查 write 输出是否正确。 */
void Compiler::executeQuads() {
    // 这里用一个简单解释器检查 write 输出。
    // 解释器只实现本课设用到的四元式，不追求完整虚拟机功能。
    unordered_map<string, RuntimeValue> baseMem;
    unordered_map<string, TypeKind> varKind;
    for (auto &s : symbolTable) {
        if (s.kind == SymbolKind::VAR || s.kind == SymbolKind::PARAM) {
            RuntimeValue v; v.kind = typeTable[s.typeId].kind; v.number = 0; v.text = "";
            baseMem[s.name] = v;
            varKind[s.name] = v.kind;
        }
    }

    unordered_map<string, int> labelPc, funcBegin, funcEnd;
    int mainBegin = 0, mainEnd = (int)quads.size();
    for (size_t i = 0; i < quads.size(); ++i) {
        if (quads[i].op == "label") labelPc[quads[i].result] = (int)i;
        if (quads[i].op == "function") funcBegin[quads[i].arg1] = (int)i + 1;
        if (quads[i].op == "end") funcEnd[quads[i].arg1] = (int)i;
        if (quads[i].op == "main") mainBegin = (int)i + 1;
        if (quads[i].op == "endmain") mainEnd = (int)i;
    }

    auto unquote = [](string s) {
        if (s.size() >= 2 && s.front() == '"' && s.back() == '"') return s.substr(1, s.size() - 2);
        if (s.size() >= 2 && s.front() == '\'' && s.back() == '\'') {
            string mid = s.substr(1, s.size() - 2);
            if (mid == "\\n") return string("\n");
            if (mid == "\\t") return string("\t");
            if (mid == "\\r") return string("\r");
            return mid;
        }
        return s;
    };
    auto isNumberText = [](const string &s) {
        if (s.empty()) return false;
        char *end = nullptr;
        strtod(s.c_str(), &end);
        return end && *end == '\0';
    };

    unordered_map<string, map<int, RuntimeValue>> arrayMem;
    unordered_map<string, RuntimeValue> fieldMem;
    runtimeOutput.clear();

    function<RuntimeValue(int,int,unordered_map<string,RuntimeValue>&)> runRange;
    function<RuntimeValue(const string&, const vector<RuntimeValue>&)> callFunction;

    auto putVal = [&](unordered_map<string, RuntimeValue> &mem, const string &name, RuntimeValue v) {
        if (varKind.count(name)) v.kind = varKind[name];
        mem[name] = v;
    };
    auto valueToString = [&](const RuntimeValue &v) {
        if (v.kind == TypeKind::STRING || v.kind == TypeKind::CHAR || !v.text.empty()) return v.text;
        return trimNumber(v.number);
    };
    auto truth = [&](const RuntimeValue &v) { return fabs(v.number) > 1e-12 || !v.text.empty(); };

    function<RuntimeValue(unordered_map<string,RuntimeValue>&, const string&)> getVal =
        [&](unordered_map<string,RuntimeValue> &mem, const string &x) -> RuntimeValue {
            if (x == "_" || x.empty()) return {};
            if (mem.count(x)) return mem[x];
            if (baseMem.count(x)) return baseMem[x];
            if (x.size() >= 2 && x.front() == '"') { RuntimeValue v; v.kind = TypeKind::STRING; v.text = unquote(x); return v; }
            if (x.size() >= 2 && x.front() == '\'') { RuntimeValue v; v.kind = TypeKind::CHAR; v.text = unquote(x); if (!v.text.empty()) v.number = (unsigned char)v.text[0]; return v; }
            if (isNumberText(x)) { RuntimeValue v; v.kind = (x.find('.') != string::npos ? TypeKind::REAL : TypeKind::INT); v.number = stod(x); return v; }
            return {};
        };

    runRange = [&](int begin, int end, unordered_map<string,RuntimeValue> &mem) -> RuntimeValue {
        vector<RuntimeValue> pendingParams;
        RuntimeValue ret; ret.kind = TypeKind::VOID;
        for (int pc = begin; pc >= 0 && pc < end; ++pc) {
            const Quad &q = quads[pc];
            if (q.op == "label" || q.op == "main" || q.op == "endmain" || q.op == "function" || q.op == "end") continue;
            if (q.op == ":=") {
                putVal(mem, q.result, getVal(mem, q.arg1));
            } else if (q.op == "+" || q.op == "-" || q.op == "*" || q.op == "/" || q.op == "<" || q.op == ">" || q.op == "<=" || q.op == ">=" || q.op == "=" || q.op == "<>" || q.op == "&&" || q.op == "||") {
                RuntimeValue a = getVal(mem, q.arg1), b = getVal(mem, q.arg2), r; r.kind = TypeKind::REAL;
                if (q.op == "+") r.number = a.number + b.number;
                else if (q.op == "-") r.number = a.number - b.number;
                else if (q.op == "*") r.number = a.number * b.number;
                else if (q.op == "/") r.number = fabs(b.number) < 1e-12 ? 0 : a.number / b.number;
                else if (q.op == "<") r.number = a.number < b.number;
                else if (q.op == ">") r.number = a.number > b.number;
                else if (q.op == "<=") r.number = a.number <= b.number;
                else if (q.op == ">=") r.number = a.number >= b.number;
                else if (q.op == "=") r.number = fabs(a.number - b.number) < 1e-12;
                else if (q.op == "<>") r.number = fabs(a.number - b.number) >= 1e-12;
                else if (q.op == "&&") r.number = truth(a) && truth(b);
                else if (q.op == "||") r.number = truth(a) || truth(b);
                putVal(mem, q.result, r);
            } else if (q.op == "neg") {
                RuntimeValue r = getVal(mem, q.arg1); r.number = -r.number; putVal(mem, q.result, r);
            } else if (q.op == "!") {
                RuntimeValue r; r.kind = TypeKind::BOOL; r.number = !truth(getVal(mem, q.arg1)); putVal(mem, q.result, r);
            } else if (q.op == "ifFalse") {
                if (!truth(getVal(mem, q.arg1))) pc = labelPc[q.result] - 1;
            } else if (q.op == "goto") {
                pc = labelPc[q.result] - 1;
            } else if (q.op == "write") {
                runtimeOutput.push_back(valueToString(getVal(mem, q.arg1)));
            } else if (q.op == "param") {
                pendingParams.push_back(getVal(mem, q.arg1));
            } else if (q.op == "call") {
                RuntimeValue r = callFunction(q.arg1, pendingParams);
                pendingParams.clear();
                if (q.result != "_") putVal(mem, q.result, r);
            } else if (q.op == "return") {
                ret = q.arg1 == "_" ? RuntimeValue{} : getVal(mem, q.arg1);
                return ret;
            } else if (q.op == "[]=") {
                int idx = (int)llround(getVal(mem, q.arg2).number);
                arrayMem[q.result][idx] = getVal(mem, q.arg1);
            } else if (q.op == "=[]") {
                int idx = (int)llround(getVal(mem, q.arg2).number);
                RuntimeValue v = arrayMem[q.arg1].count(idx) ? arrayMem[q.arg1][idx] : RuntimeValue{};
                putVal(mem, q.result, v);
            } else if (q.op == ".=" || q.op == "->=") {
                fieldMem[q.result + "." + q.arg2] = getVal(mem, q.arg1);
            } else if (q.op == "." || q.op == "->") {
                RuntimeValue v = fieldMem.count(q.arg1 + "." + q.arg2) ? fieldMem[q.arg1 + "." + q.arg2] : RuntimeValue{};
                putVal(mem, q.result, v);
            }
        }
        return ret;
    };

    callFunction = [&](const string &name, const vector<RuntimeValue> &args) -> RuntimeValue {
        int symIdx = -1;
        for (size_t i = 0; i < symbolTable.size(); ++i) if (symbolTable[i].name == name && symbolTable[i].kind == SymbolKind::FUNC) { symIdx = (int)i; break; }
        if (symIdx == -1 || !funcBegin.count(name) || !funcEnd.count(name)) return {};
        int fidx = symbolTable[symIdx].extra;
        unordered_map<string,RuntimeValue> local = baseMem;
        for (size_t i = 0; i < args.size() && i < funcTable[fidx].paramNames.size(); ++i) local[funcTable[fidx].paramNames[i]] = args[i];
        return runRange(funcBegin[name], funcEnd[name], local);
    };

    unordered_map<string,RuntimeValue> mainMem = baseMem;
    runRange(mainBegin, mainEnd, mainMem);
}

