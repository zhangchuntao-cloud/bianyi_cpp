#include "../include/backend.h"

// =============================================================
// 后端处理：基本块、优化、活跃信息和目标代码
// =============================================================
/*
 * 本文件负责处理四元式后面的工作。
 * 前端已经把源程序翻译成四元式，后端就在四元式基础上继续处理。
 *
 * 主要步骤按课本上的顺序写：
 * 1. 基本块划分：找每个基本块的入口 leader。
 * 2. 局部优化：在一个基本块内部做常量折叠、代数化简、公共子表达式消除。
 * 3. 循环优化：识别 goto 回到前面 label 的简单循环，尝试外提循环不变式。
 * 4. 活跃信息：从后往前迭代计算 USE、DEF、IN、OUT。
 * 5. 删除无用赋值：如果临时变量算出来以后再也不用，就把这条四元式删掉。
 * 6. 目标代码：生成 8086 风格的目标代码文本。
 *
 * 注意：这里输出的是教学用目标代码，不是完整汇编器。
 */
struct BasicBlockInfo {
    int id = 0;   // 块编号
    int start = 0;// 起始四元式下标
    int end = 0;
    vector<int> succ;  // 后继块（跳转去哪里）
    string reason;// 为什么这里是块入口
};

// 单条四元式 活跃变量数据流信息
struct LiveInfo {
    set<string> use;   // 使用集
    set<string> def;   // 定义集
    set<string> in;    // 入口活跃集
    set<string> out;   // 出口活跃集
};

/* Backend：后端处理类，输入四元式，输出优化结果和目标代码。 */
class Backend {
    const Compiler &c;
    vector<Quad> original;//原始4元式
    vector<Quad> afterLocalOpt;//局部优化的
    vector<Quad> afterLoopOpt;//局部优化基础上, 循环优化后的4元式
    vector<Quad> optimized;//最终
    vector<BasicBlockInfo> originalBlocks;
    vector<BasicBlockInfo> optimizedBlocks;
    vector<LiveInfo> live; //活跃变量数据流信息
    vector<string> optimizationLog;   //局部优化log
    vector<string> loopLog;            //循环优化log
//目标代码生成专用映射表
    map<string, int> targetOffset;       //- 变量 → 内存偏移地址映射。
    map<string, string> stringLiteralLabel;    //- 字符串常量 → 汇编标号映射。- 例如 "hello" → STR0、STR1。

    /*
     * 判断字符串是不是数字常量。
     * 例如 "10"、"3.14" 返回 true，变量名 a、t1 返回 false。
     * 后面常量折叠会用到这个判断。
     */
    static bool isNumberLiteral(const string &s) {
        if (s.empty()) return false;
        char *endp = nullptr;
        strtod(s.c_str(), &endp);
        return endp && *endp == '\0';
    }
    /* 判断是不是字符串字面量，例如 "hello"。 */
    static bool isQuotedString(const string &s) {
        return s.size() >= 2 && s.front() == '"' && s.back() == '"';
    }
    static bool isQuotedChar(const string &s) {
        return s.size() >= 2 && s.front() == '\'' && s.back() == '\'';
    }
    static bool isBoolLiteral(const string &s) {
        return s == "true" || s == "false";
    }
    static bool isLiteral(const string &s) {
        return s == "_" || isNumberLiteral(s) || isQuotedString(s) || isQuotedChar(s) || isBoolLiteral(s);
    }
    /*
     * 判断一个字符串能不能当变量处理。
     * 常数、下划线、标签 L1 都不算变量。
     * 活跃信息分析时，只把变量加入 USE/DEF 集。
     */
    static bool isVariableName(const string &s) {
        if (s.empty() || s == "_") return false;
        if (isLiteral(s)) return false;
        if (s[0] == 'L' && s.size() > 1 && all_of(s.begin() + 1, s.end(), ::isdigit)) return false;
        return true;
    }
    static bool isTempName(const string &s) {
        return s.size() >= 2 && s[0] == 't' && all_of(s.begin() + 1, s.end(), ::isdigit);
    }
//isbinary判断传入的四元式操作符是否为「二元运算符」。
二元运算符 = 需要两个操作数参与运算的运算符。
    static bool isBinaryOp(const string &op) {
        static set<string> ops = {"+", "-", "*", "/", "<", ">", "<=", ">=", "=", "<>", "&&", "||"};
        return ops.count(op) > 0;
    }
//一元运算符判断:
    static bool isUnaryOp(const string &op) { return op == "neg" || op == "!"; }
    //纯计算语句,
用于删除无用赋值优化：只有纯计算语句，且临时变量后续不活跃，才允许删除。
    static bool isPureCompute(const Quad &q) { return isBinaryOp(q.op) || isUnaryOp(q.op) || q.op == ":="; }
    /*包含语句
- goto：无条件跳转
- ifFalse：条件分支跳转
- return：函数返回
- end / endmain：程序结束*/
    static bool isControl(const Quad &q) { return q.op == "goto" || q.op == "ifFalse" || q.op == "return" || q.op == "end" || q.op == "endmain"; }
    //可交换运算符判断, 交换律
    static bool isCommutative(const string &op) { return op == "+" || op == "*" || op == "=" || op == "<>" || op == "&&" || op == "||"; }

    static string boolToNum(const string &s) {
        if (s == "true") return "1";
        if (s == "false") return "0";
        return s;
    }

    /*
     * 对二元运算做常量计算。
     * 如果两个操作数都是常数，就直接算出结果，完成常值表达式节省。
     */
    //运算符op、两个操作数a/b、输出参数res（存储运算结果）
    static bool evalBinary(const string &op, const string &a, const string &b, string &res) {
        string aa = boolToNum(a), bb = boolToNum(b);
        // 校验转换后的值是否为合法数字字面量
        if (!isNumberLiteral(aa) || !isNumberLiteral(bb)) return false;
        // 字符串转双精度浮点数
        double x = stod(aa), y = stod(bb), r = 0;

        //
        if (op == "+") r = x + y;
        else if (op == "-") r = x - y;
        else if (op == "*") r = x * y;
        // 除零保护：判断除数绝对值是否接近0（浮点精度判断）
        else if (op == "/") { if (fabs(y) < 1e-12) return false; r = x / y; }
        
        else if (op == "<") r = x < y;
        else if (op == ">") r = x > y;
        else if (op == "<=") r = x <= y;
        else if (op == ">=") r = x >= y;
        
        else if (op == "=") r = fabs(x - y) < 1e-12;//浮点相等判断
        else if (op == "<>") r = fabs(x - y) >= 1e-12;
        // 逻辑运算：非0视为true，0视为false
        else if (op == "&&") r = (fabs(x) >= 1e-12) && (fabs(y) >= 1e-12);
        else if (op == "||") r = (fabs(x) >= 1e-12) || (fabs(y) >= 1e-12);
        else return false;//不支持的
        
        res = trimNumber(r);// 格式化数字结果（去除末尾0、小数点等）
        return true;
    }

    static bool evalUnary(const string &op, const string &a, string &res) {
        //输入普通数字（"5"、"-3.14"、"0"）→ 原样不变
        string aa = boolToNum(a);
        if (!isNumberLiteral(aa)) return false;//合法
        //转浮点数
        double x = stod(aa), r = 0;
        if (op == "neg") r = -x;
        else if (op == "!") r = !(fabs(x) >= 1e-12);
        else return false;
        res = trimNumber(r);
        return true;
    }

    /*
     * 做简单代数化简。
     * 例如 x+0=x，x*1=x，x*0=0。
     * 这里属于常值表达式节省的一种情况。
     */
    static bool algebraicSimplify(const string &op, const string &a, const string &b, string &res) {
        string aa = boolToNum(a), bb = boolToNum(b);
        auto isZero = [](const string &x) { return isNumberLiteral(x) && fabs(stod(x)) < 1e-12; };
        auto isOne = [](const string &x) { return isNumberLiteral(x) && fabs(stod(x) - 1.0) < 1e-12; };
        if (op == "+") {
            if (isZero(aa)) { res = b; return true; }
            if (isZero(bb)) { res = a; return true; }
        } else if (op == "-") {
            if (isZero(bb)) { res = a; return true; }
        } else if (op == "*") {
            if (isZero(aa) || isZero(bb)) { res = "0"; return true; }
            if (isOne(aa)) { res = b; return true; }
            if (isOne(bb)) { res = a; return true; }
        } else if (op == "/") {
            if (isZero(aa)) { res = "0"; return true; }
            if (isOne(bb)) { res = a; return true; }
        } else if (op == "&&") {
            if (isZero(aa) || isZero(bb)) { res = "0"; return true; }
            if (isOne(aa)) { res = b; return true; }
            if (isOne(bb)) { res = a; return true; }
        } else if (op == "||") {
            if (isOne(aa) || isOne(bb)) { res = "1"; return true; }
            if (isZero(aa)) { res = b; return true; }
            if (isZero(bb)) { res = a; return true; }
        }
        return false;
    }
    //给二元表达式生成一个唯一的「缓存 key」，让 a+b 和 b+a 生成同一个 key
    static string exprKey(string op, string a, string b) {
        if (isCommutative(op) && b < a) swap(a, b);
        return op + "|" + a + "|" + b;
    }
    //解析缓存键key → [op, a, b]
    static vector<string> splitExprKey(const string &key) {
        vector<string> out;
        string cur;
        for (char ch : key) {
            if (ch == '|') { out.push_back(cur); cur.clear(); }
            else cur.push_back(ch);
        }
        out.push_back(cur);
        return out;
    }

    /*
     * 求一条四元式使用了哪些变量，得到 USE 集。
     * 例如 (+,a,b,t1) 的 USE 是 {a,b}。
     */
    static set<string> usesOf(const Quad &q) {
        set<string> u;  // 存放：这条四元式用到的所有变量（自动去重）

        // 工具：如果 x 是变量名，就加入集合
        auto add = [&](const string &x) { if (isVariableName(x)) u.insert(x); };

        // 👇 根据不同指令，收集用到的变量
        if (isBinaryOp(q.op)) {          // 二元运算：+ - * / && || == < > ...
            add(q.arg1); add(q.arg2);   // 用到 arg1, arg2
        }
        else if (isUnaryOp(q.op)) {     // 一元运算：neg !
            add(q.arg1);                // 用到 arg1
        }
        else if (q.op == ":=") {        // 赋值：a := b
            add(q.arg1);                // 用到 b
        }
        else if (q.op == "ifFalse") {   // 条件跳转：ifFalse cond
            add(q.arg1);                // 用到 cond
        }
        else if (q.op == "write") {     // 输出：write x
            add(q.arg1);                // 用到 x
        }
        else if (q.op == "param") {     // 函数参数：param x
            add(q.arg1);                // 用到 x
        }
        else if (q.op == "return") {    // 返回：return x
            add(q.arg1);                // 用到 x
        }
        else if (q.op == "[]=") {       // 数组赋值：a[i] = x
            add(q.arg1); add(q.arg2); add(q.result); // 用到 a, i, x
        }
        else if (q.op == "=[]") {       // 数组取值：x = a[i]
            add(q.arg1); add(q.arg2);   // 用到 a, i
        }
        else if (q.op == ".=" || q.op == "->=") { // 成员赋值：a.b = x
            add(q.arg1); add(q.result); // 用到 a, x
        }
        else if (q.op == "." || q.op == "->") {   // 成员访问：x = a.b
            add(q.arg1);                // 用到 a
        }

        return u; // 返回所有用到的变量
    }

    /*
     * 求一条四元式定义了哪些变量，得到 DEF 集。
     * 例如 (+,a,b,t1) 的 DEF 是 {t1}。
     */
    static set<string> defsOf(const Quad &q) {
        set<string> d;
        auto add = [&](const string &x) { if (isVariableName(x)) d.insert(x); };
        if (isBinaryOp(q.op) || isUnaryOp(q.op) || q.op == ":=" || q.op == "=[]" || q.op == "." || q.op == "->") add(q.result);
        else if (q.op == "call" && q.result != "_") add(q.result);
        return d;
    }
    //转字符串
    static string setToString(const set<string> &s) {
        if (s.empty()) return "{}";
        vector<string> xs(s.begin(), s.end());
        return "{" + join(xs, ",") + "}";
    }

    static string quadToString(const Quad &q) {
        return "(" + q.op + "," + q.arg1 + "," + q.arg2 + "," + q.result + ")";
    }

    /*
     * 扫描四元式，记录 label/function/main 所在的位置。
     * 基本块划分和跳转目标查找都要用。
     */
    //遍历所有四元式指令，记录「标签名 / 函数名 → 指令行号」的映射表。
    //以后跳转、调用函数时，直接查表知道跳去第几行
    map<string, int> labelIndex(const vector<Quad> &qs) const {
        map<string, int> mp;
        for (int i = 0; i < (int)qs.size(); ++i) {
            if (qs[i].op == "label") mp[qs[i].result] = i;
            if (qs[i].op == "function") mp[qs[i].arg1] = i;
            if (qs[i].op == "main") mp["main"] = i;
        }
        return mp;
    }

    /*
     * 基本块划分。
     * 常用规则：第一条四元式、跳转目标、跳转语句的下一条都作为 leader。
     * 每个 leader 到下一个 leader 前一条就是一个基本块。
     */
    vector<BasicBlockInfo> buildBlocks(const vector<Quad> &qs) const {
        vector<BasicBlockInfo> blocks;//基本块列表
        if (qs.empty()) return blocks;
        //1. 先建立标签/函数 → 指令行号的映射表
        map<string, int> lbl = labelIndex(qs);
        map<int, string> leaderReason;
        // 工具lambda：标记 idx 行为基本块入口，并记录原因
        auto mark = [&](int idx, const string &why) {
            if (idx >= 0 && idx < (int)qs.size()) {
                if (leaderReason[idx].empty()) leaderReason[idx] = why;
                else leaderReason[idx] += ";" + why;
            }
        };
        // 2. 标记所有基本块入口（leader）
        mark(0, "第一条四元式");// 程序第一行一定是入口
        
        // 遍历每一条四元式，寻找所有入口点
        for (int i = 0; i < (int)qs.size(); ++i) {
            const Quad &q = qs[i];
            if (q.op == "label" || q.op == "function" || q.op == "main") mark(i, q.op + "入口");
            if (q.op == "goto" || q.op == "ifFalse" || q.op == "return" || q.op == "end" || q.op == "endmain") mark(i + 1, "跳转/出口的后继");
            if ((q.op == "goto" || q.op == "ifFalse") && lbl.count(q.result)) mark(lbl[q.result], "跳转目标" + q.result);
        }
        // 3. 收集所有入口行号，并排序、去重
        vector<int> leaders;
        for (auto &p : leaderReason) leaders.push_back(p.first);
        sort(leaders.begin(), leaders.end());
        leaders.erase(unique(leaders.begin(), leaders.end()), leaders.end());
        // 4. 根据入口划分基本块
        map<int, int> blockOfStart;    //块起始行 → 块ID
        for (size_t i = 0; i < leaders.size(); ++i) {
            BasicBlockInfo b;
            b.id = (int)i;//块id
            b.start = leaders[i];//块起始指令行
            //
            b.end = (i + 1 < leaders.size() ? leaders[i + 1] - 1 : (int)qs.size() - 1);
            b.reason = leaderReason[b.start];
            blockOfStart[b.start] = b.id;
            blocks.push_back(b);
        }
        /// 5. 建立映射：指令行号 → 所属基本块ID
        map<int, int> instToBlock;
        for (auto &b : blocks) for (int i = b.start; i <= b.end; ++i) instToBlock[i] = b.id;
        // 6. 建立映射：标签名 → 所在基本块ID
        map<string, int> labelBlock;
        for (auto &p : lbl) if (instToBlock.count(p.second)) labelBlock[p.first] = instToBlock[p.second];
        // 7. 计算每个基本块的【后继块】succ（控制流）
        for (auto &b : blocks) {
            const Quad &last = qs[b.end];//
            set<int> ss;
            if (last.op == "goto") {
                if (labelBlock.count(last.result)) ss.insert(labelBlock[last.result]);
            } else if (last.op == "ifFalse") {
                if (labelBlock.count(last.result)) ss.insert(labelBlock[last.result]);
                if (b.id + 1 < (int)blocks.size()) ss.insert(b.id + 1);
            } else if (last.op == "return" || last.op == "end" || last.op == "endmain") {
                // 出口基本块没有后继。
            } else {
                //普通顺序执行 ->后继是下一个块
                if (b.id + 1 < (int)blocks.size()) ss.insert(b.id + 1);
            }
            //后继存入succ
            b.succ.assign(ss.begin(), ss.end());
        }
        return blocks;
    }

    /*
     * 基本块内局部优化。
     * 这里只在同一个基本块内处理，避免跨基本块导致控制流错误。
     */
    vector<Quad> localOptimize(const vector<Quad> &qs) {
        vector<Quad> out = qs;
        // 1. 构建基本块，只在块内优化（局部优化）
        auto blocks = buildBlocks(out);
        //
        for (auto &b : blocks) {
            //
            map<string, string> constEnv;       // var -> constant
            map<string, string> exprResult;     // expression key -> 计算结果
            // 工具lambda：如果变量是已知常量，就替换成常量值
            auto replaceByConst = [&](string x) {
                if (constEnv.count(x)) return constEnv[x];
                return x;
            };
            // 工具lambda：当变量d被重新赋值时，杀死相关常量与表达式
            auto kill = [&](const string &d) {
                // 1. 常量环境中删除d（因为被重新赋值，不再是常量）
                constEnv.erase(d);
                 // 2. 删除所有用到d的表达式缓存（表达式失效）
                for (auto it = exprResult.begin(); it != exprResult.end();) {
                    vector<string> parts = splitExprKey(it->first);
                    bool hit = false;
                    // 只要表达式的操作数被重新赋值，该表达式失效；
                    // 表达式的结果一般是唯一临时变量，刚生成时不应被自己杀掉。
                    for (auto &p : parts) if (p == d) hit = true;
                    if (hit) it = exprResult.erase(it);
                    else ++it;
                }
            };
            //遍历当前基本块内的每一条四元式
            for (int i = b.start; i <= b.end; ++i) {
                Quad q = out[i];
                //
                if (q.op == "nop") continue;
                // ===================== 常量传播：把变量替换成已知常量 =====================
                if (isBinaryOp(q.op)) { q.arg1 = replaceByConst(q.arg1); q.arg2 = replaceByConst(q.arg2); }
                else if (isUnaryOp(q.op) || q.op == ":=" || q.op == "ifFalse" || q.op == "write" || q.op == "param" || q.op == "return") q.arg1 = replaceByConst(q.arg1);
                else if (q.op == "[]=") { q.arg1 = replaceByConst(q.arg1); q.arg2 = replaceByConst(q.arg2); }
                else if (q.op == "=[]") { q.arg2 = replaceByConst(q.arg2); }
                else if (q.op == ".=" || q.op == "->=") q.arg1 = replaceByConst(q.arg1);
                // ===================== 常量折叠 / 代数化简 =====================
                string folded;
                //二元
                if (isBinaryOp(q.op) && evalBinary(q.op, q.arg1, q.arg2, folded)) {
                    optimizationLog.push_back("常值表达式节省：" + quadToString(out[i]) + "  ==>  (:=," + folded + ",_," + q.result + ")");
                    q = {":=", folded, "_", q.result};
                } //一元
                else if (isUnaryOp(q.op) && evalUnary(q.op, q.arg1, folded)) {
                    optimizationLog.push_back("常值表达式节省：" + quadToString(out[i]) + "  ==>  (:=," + folded + ",_," + q.result + ")");
                    q = {":=", folded, "_", q.result};
                } //代数化简
                else if (isBinaryOp(q.op) && algebraicSimplify(q.op, q.arg1, q.arg2, folded)) {
                    optimizationLog.push_back("常值/代数表达式节省：" + quadToString(q) + "  ==>  (:=," + folded + ",_," + q.result + ")");
                    q = {":=", folded, "_", q.result};
                }// ===================== 公共子表达式消除（核心优化） =====================
                // 仅对 二元运算 / 一元运算 做重复表达式检测
                else if (isBinaryOp(q.op) || isUnaryOp(q.op)) {
                    // 生成表达式唯一键（满足交换律的运算会自动排序，保证a+b和b+a键相同）
                    string key = exprKey(q.op, q.arg1, q.arg2);
                    // 如果该表达式已经计算过，并且结果变量不同，则直接复用之前的结果
                    if (exprResult.count(key) && q.result != exprResult[key]) {
                        optimizationLog.push_back("公共子表达式节省：" + quadToString(q) + "  复用 " + exprResult[key]);
                        // 把计算指令 替换成 直接赋值（复用已有结果）
                        q = {":=", exprResult[key], "_", q.result};
                    } else {
                        // 第一次计算，将表达式与结果存入缓存，供后续复用
                        exprResult[key] = q.result;
                    }
                }

                // 常量条件跳转优化。
                if (q.op == "ifFalse") {
                    string cond = boolToNum(q.arg1);
                    if (isNumberLiteral(cond)) {
                        if (fabs(stod(cond)) < 1e-12) {
                            optimizationLog.push_back("条件常量化：" + quadToString(out[i]) + "  恒假，改为 goto");
                            q = {"goto", "_", "_", q.result};
                        } else {
                            optimizationLog.push_back("条件常量化：" + quadToString(out[i]) + "  恒真，删除条件跳转");
                            q = {"nop", "_", "_", "_"};
                        }
                    }
                }

                // 更新常量环境和公共表达式环境。
                set<string> defs = defsOf(q);
                for (auto &d : defs) kill(d);
                // 为了贴近老师实例，这里只把“临时变量 = 常量”加入常量环境。
                // 普通变量虽然可能被赋常量，但后续仍按内存变量处理，避免把 a:=2; b:=10+a 过度折叠成 b:=12。
                if (q.op == ":=" && isLiteral(q.arg1) && isTempName(q.result)) constEnv[q.result] = boolToNum(q.arg1);
                if (q.op == "call" || q.op == "[]=" || q.op == ".=" || q.op == "->=") { constEnv.clear(); exprResult.clear(); }
                out[i] = q;
            }
        }
        vector<Quad> compact;
        for (auto &q : out) if (q.op != "nop") compact.push_back(q);
        return compact;
    }

    /*
     * 简单循环优化。
     * 如果发现 goto 跳回前面的 label，就把中间区域看作一个循环。
     */
    vector<Quad> loopOptimize(const vector<Quad> &qs) {
        vector<Quad> out;
        map<string, int> lbl = labelIndex(qs);
        map<int, vector<Quad>> insertBefore;
        set<int> skip;
        for (int i = 0; i < (int)qs.size(); ++i) {
            if (qs[i].op != "goto" || !lbl.count(qs[i].result)) continue;
            int start = lbl[qs[i].result];
            if (start >= i) continue;
            int end = i;
            loopLog.push_back("发现循环：入口 label=" + qs[i].result + "，四元式范围 [" + to_string(start) + "," + to_string(end) + "]");
            set<string> assigned;
            for (int j = start; j <= end; ++j) for (auto &d : defsOf(qs[j])) assigned.insert(d);
            for (int j = start + 1; j < end; ++j) {
                const Quad &q = qs[j];
                if (!(isBinaryOp(q.op) || isUnaryOp(q.op))) continue;
                if (!isTempName(q.result)) continue;
                bool invariant = true;
                for (auto &u : usesOf(q)) if (assigned.count(u)) invariant = false;
                if (invariant) {
                    insertBefore[start].push_back(q);
                    skip.insert(j);
                    loopLog.push_back("  循环不变式外提：" + quadToString(q) + "  移到循环入口前");
                }
            }
        }
        for (int i = 0; i < (int)qs.size(); ++i) {
            if (insertBefore.count(i)) for (auto &q : insertBefore[i]) out.push_back(q);
            if (!skip.count(i)) out.push_back(qs[i]);
        }
        if (loopLog.empty()) loopLog.push_back("没有发现可以外提的循环不变式。");
        return out;
    }

    /*
     * 活跃变量分析。
     * 从后往前反复计算 IN/OUT，直到集合不再变化。
     */
    vector<LiveInfo> computeLive(const vector<Quad> &qs) const {
        int n = (int)qs.size();
        vector<LiveInfo> info(n);
        for (int i = 0; i < n; ++i) { info[i].use = usesOf(qs[i]); info[i].def = defsOf(qs[i]); }
        map<string, int> lbl = labelIndex(qs);
        vector<vector<int>> succ(n);
        for (int i = 0; i < n; ++i) {
            const Quad &q = qs[i];
            if (q.op == "goto") {
                if (lbl.count(q.result)) succ[i].push_back(lbl[q.result]);
            } else if (q.op == "ifFalse") {
                if (lbl.count(q.result)) succ[i].push_back(lbl[q.result]);
                if (i + 1 < n) succ[i].push_back(i + 1);
            } else if (q.op == "return" || q.op == "end" || q.op == "endmain") {
                // 无后继。
            } else if (i + 1 < n) succ[i].push_back(i + 1);
        }
        bool changed = true;
        while (changed) {
            changed = false;
            for (int i = n - 1; i >= 0; --i) {
                set<string> newOut;
                for (int s : succ[i]) newOut.insert(info[s].in.begin(), info[s].in.end());
                set<string> newIn = info[i].use;
                for (auto &x : newOut) if (!info[i].def.count(x)) newIn.insert(x);
                if (newOut != info[i].out || newIn != info[i].in) {
                    info[i].out = newOut;
                    info[i].in = newIn;
                    changed = true;
                }
            }
        }
        return info;
    }

    /*
     * 删除无用赋值。
     * 如果一条纯计算四元式只给临时变量赋值，而这个临时变量后面不再使用，就可以删除。
     */
    vector<Quad> removeDeadAssignments(const vector<Quad> &qs) {
        vector<LiveInfo> li = computeLive(qs);
        vector<Quad> out;
        for (int i = 0; i < (int)qs.size(); ++i) {
            const Quad &q = qs[i];
            set<string> d = defsOf(q);
            bool pure = isPureCompute(q) || q.op == "=[]" || q.op == "." || q.op == "->";
            bool dead = pure && d.size() == 1 && isTempName(*d.begin()) && !li[i].out.count(*d.begin());
            // 保守处理：只删除临时变量 t 的死赋值，普通用户变量即使后续不读也保留，
            // 这样生成的目标代码更接近老师实例中的“最终变量存储结果”。
            if (dead) {
                optimizationLog.push_back("删除无用赋值：" + quadToString(q) + "，定义的 " + *d.begin() + " 在后续不活跃");
                continue;
            }
            out.push_back(q);
        }
        return out;
    }

    static string hex4(int v) {
        stringstream ss;
        ss << uppercase << hex << setw(4) << setfill('0') << v << "H";
        return ss.str();
    }
    static string asmName(string s) {
        for (char &ch : s) if (!isalnum((unsigned char)ch) && ch != '_') ch = '_';
        if (s.empty()) return "anonymous";
        if (isdigit((unsigned char)s[0])) s = "_" + s;
        return s;
    }
    string asmOperand(const string &x) {
        if (x == "_" || x.empty()) return "";
        if (x == "true") return "1";
        if (x == "false") return "0";
        if (isNumberLiteral(x)) return x;
        if (isQuotedChar(x)) {
            if (x.size() >= 3) return to_string((int)(unsigned char)x[1]);
            return "0";
        }
        if (isQuotedString(x)) {
            if (!stringLiteralLabel.count(x)) {
                stringLiteralLabel[x] = "STR" + to_string(stringLiteralLabel.size());
            }
            return "OFFSET " + stringLiteralLabel[x];
        }
        if (targetOffset.count(x)) return "[" + hex4(targetOffset[x]) + "]";
        if (x.find('.') != string::npos) return "[" + asmName(x) + "]";
        return x;
    }

    /*
     * 为目标代码中的变量分配偏移地址。
     * 输出形式类似 [0000H]、[0004H]，贴近老师给的例子。
     */
    void buildTargetMemory() {
        targetOffset.clear();
        int off = 0;
        for (const auto &s : c.symbolTable) {
            if ((s.kind == SymbolKind::VAR || s.kind == SymbolKind::PARAM) && !targetOffset.count(s.name)) {
                int width = 4;
                if (s.typeId >= 0 && s.typeId < (int)c.typeTable.size()) width = max(1, c.typeTable[s.typeId].width);
                // 仿老师示例，整型按 4 字节域宽；小于 4 的 char/boolean 也按 4 对齐，便于 MOV AX 生成。
                if (width < 4) width = 4;
                targetOffset[s.name] = off;
                off += width;
            }
        }
    }

    static string setwString(int off) {
        return "[" + hex4(off) + "]";
    }

public:
    explicit Backend(const Compiler &compiler) : c(compiler), original(compiler.quads) {}

    // 运行完整后端流水线。
    /* run：后端总控，按顺序完成划分、优化、活跃分析和目标代码生成准备。 */
    void run() {
        originalBlocks = buildBlocks(original);
        afterLocalOpt = localOptimize(original);
        afterLoopOpt = loopOptimize(afterLocalOpt);
        optimized = removeDeadAssignments(afterLoopOpt);
        optimizedBlocks = buildBlocks(optimized);
        live = computeLive(optimized);
    }

    void printInstructionSet(ostream &os) {
        os << "\n四、目标代码指令集合" << endl;
        os << left << setw(12) << "指令" << "含义\n";
        os << string(68, '-') << "\n";
        os << setw(12) << "MOV" << "数据传送：MOV AX,src / MOV dst,AX\n";
        os << setw(12) << "ADD/SUB" << "加减运算，结果保存在 AX\n";
        os << setw(12) << "IMUL/IDIV" << "乘除运算，结果保存在 AX\n";
        os << setw(12) << "CMP" << "关系运算或条件跳转前的比较\n";
        os << setw(12) << "JE/JNE" << "相等/不等条件跳转\n";
        os << setw(12) << "JL/JG" << "小于/大于条件跳转\n";
        os << setw(12) << "JLE/JGE" << "小于等于/大于等于条件跳转\n";
        os << setw(12) << "JMP" << "无条件跳转，对应 goto 四元式\n";
        os << setw(12) << "PUSH/CALL" << "参数入栈与函数/过程调用\n";
        os << setw(12) << "RET" << "过程返回，对应 end/return\n";
        os << setw(12) << "INT 21H" << "程序结束中断\n";
    }

    void printBlocks(ostream &os, const vector<BasicBlockInfo> &blocks, const vector<Quad> &qs, const string &title) {
        os << "\n========== " << title << " ==========" << endl;
        for (auto &b : blocks) {
            vector<string> ss; for (int x : b.succ) ss.push_back("B" + to_string(x));
            os << "B" << b.id << "  range=[" << b.start << "," << b.end << "]  succ={" << join(ss, ",") << "}  入口原因=" << b.reason << "\n";
            for (int i = b.start; i <= b.end && i < (int)qs.size(); ++i) os << "    " << setw(4) << i << quadToString(qs[i]) << "\n";
        }
    }

    void printOptimization(ostream &os) {
        os << "\n五、优化处理记录" << endl;
        os << "下面列出本次编译中做过的优化处理。\n";
        if (optimizationLog.empty()) os << "  无局部优化记录。\n";
        else for (auto &s : optimizationLog) os << "  - " << s << "\n";
        os << "\n循环处理记录：\n";
        for (auto &s : loopLog) os << "  - " << s << "\n";
    }

    void printOptimizedQuads(ostream &os) {
        os << "\n六、优化后的四元式" << endl;
        os << left << setw(6) << "No" << setw(12) << "Op" << setw(16) << "Arg1" << setw(16) << "Arg2" << "Result\n";
        os << string(62, '-') << "\n";
        for (size_t i = 0; i < optimized.size(); ++i)
            os << left << setw(6) << i << setw(12) << optimized[i].op << setw(16) << optimized[i].arg1 << setw(16) << optimized[i].arg2 << optimized[i].result << "\n";
    }

    void printLiveInfo(ostream &os) {
        os << "\n七、活跃信息表" << endl;
        os << left << setw(5) << "No" << setw(28) << "Quad" << setw(22) << "USE" << setw(22) << "DEF" << setw(28) << "IN" << "OUT\n";
        os << string(112, '-') << "\n";
        for (size_t i = 0; i < optimized.size(); ++i) {
            os << left << setw(5) << i << setw(28) << quadToString(optimized[i]).substr(0, 27)
               << setw(22) << setToString(live[i].use)
               << setw(22) << setToString(live[i].def)
               << setw(28) << setToString(live[i].in)
               << setToString(live[i].out) << "\n";
        }
    }

    string memCommentLine(const string &name, int off) {
        return "        ; " + setwString(off) + "  " + name;
    }

    /*
     * 把优化后的四元式翻译成目标代码文本。
     * 这里只模拟常见指令，不追求真正可由 MASM 直接汇编。
     */
    vector<string> buildTarget() {
        buildTargetMemory();
        stringLiteralLabel.clear();
        // 先扫描一遍，给字符串字面量编号。
        for (auto &q : optimized) {
            for (auto &x : {q.arg1, q.arg2, q.result}) if (isQuotedString(x) && !stringLiteralLabel.count(x)) stringLiteralLabel[x] = "STR" + to_string(stringLiteralLabel.size());
        }
        vector<string> lines;
        string seg = asmName(c.programName.empty() ? "example" : c.programName);
        int total = 0;
        for (auto &p : targetOffset) total = max(total, p.second + 4);
        lines.push_back(seg + " SEGMENT");
        lines.push_back("        DB " + to_string(max(1, total)) + " DUP(0)");
        lines.push_back("        ; 变量、形参、临时变量偏移表");
        vector<pair<int,string>> byOff;
        for (auto &p : targetOffset) byOff.push_back({p.second, p.first});
        sort(byOff.begin(), byOff.end());
        for (auto &p : byOff) lines.push_back(memCommentLine(p.second, p.first));
        for (auto &p : stringLiteralLabel) {
            string lit = p.first.substr(1, p.first.size() - 2);
            lines.push_back("        " + p.second + " DB '" + lit + "','$'");
        }
        lines.push_back("        ASSUME CS:" + seg + ", DS:" + seg);

        int cmpId = 0;
        auto emitLoadAX = [&](vector<string> &ls, const string &src) { ls.push_back("        MOV AX," + asmOperand(src)); };
        auto emitStoreAX = [&](vector<string> &ls, const string &dst) { ls.push_back("        MOV " + asmOperand(dst) + ",AX"); };
        auto relJump = [&](const string &op) {
            if (op == "<") return string("JL");
            if (op == ">") return string("JG");
            if (op == "<=") return string("JLE");
            if (op == ">=") return string("JGE");
            if (op == "=") return string("JE");
            if (op == "<>") return string("JNE");
            return string("JE");
        };
        for (size_t i = 0; i < optimized.size(); ++i) {
            const Quad &q = optimized[i];
            lines.push_back("        ; " + to_string(i) + " " + quadToString(q));
            if (q.op == "main") lines.push_back("MAIN:");
            else if (q.op == "function") lines.push_back(asmName(q.arg1) + ":");
            else if (q.op == "label") lines.push_back(q.result + ":");
            else if (q.op == "end") lines.push_back("        RET");
            else if (q.op == "endmain") { lines.push_back("        INT 21H"); lines.push_back("        RET"); }
            else if (q.op == ":=") { emitLoadAX(lines, q.arg1); emitStoreAX(lines, q.result); }
            else if (q.op == "+" || q.op == "-" || q.op == "*" || q.op == "/" || q.op == "&&" || q.op == "||") {
                emitLoadAX(lines, q.arg1);
                if (q.op == "+") lines.push_back("        ADD AX," + asmOperand(q.arg2));
                else if (q.op == "-") lines.push_back("        SUB AX," + asmOperand(q.arg2));
                else if (q.op == "*") lines.push_back("        IMUL AX," + asmOperand(q.arg2));
                else if (q.op == "/") { lines.push_back("        MOV BX," + asmOperand(q.arg2)); lines.push_back("        CWD"); lines.push_back("        IDIV BX"); }
                else if (q.op == "&&") { lines.push_back("        AND AX," + asmOperand(q.arg2)); }
                else if (q.op == "||") { lines.push_back("        OR AX," + asmOperand(q.arg2)); }
                emitStoreAX(lines, q.result);
            } else if (q.op == "<" || q.op == ">" || q.op == "<=" || q.op == ">=" || q.op == "=" || q.op == "<>") {
                string t = "CMP_TRUE_" + to_string(++cmpId), e = "CMP_END_" + to_string(cmpId);
                emitLoadAX(lines, q.arg1);
                lines.push_back("        CMP AX," + asmOperand(q.arg2));
                lines.push_back("        MOV AX,0");
                lines.push_back("        " + relJump(q.op) + " " + t);
                lines.push_back("        JMP " + e);
                lines.push_back(t + ":");
                lines.push_back("        MOV AX,1");
                lines.push_back(e + ":");
                emitStoreAX(lines, q.result);
            } else if (q.op == "neg") { emitLoadAX(lines, q.arg1); lines.push_back("        NEG AX"); emitStoreAX(lines, q.result); }
            else if (q.op == "!") { emitLoadAX(lines, q.arg1); lines.push_back("        CMP AX,0"); lines.push_back("        MOV AX,0"); lines.push_back("        JE NOT_TRUE_" + to_string(++cmpId)); lines.push_back("        JMP NOT_END_" + to_string(cmpId)); lines.push_back("NOT_TRUE_" + to_string(cmpId) + ":"); lines.push_back("        MOV AX,1"); lines.push_back("NOT_END_" + to_string(cmpId) + ":"); emitStoreAX(lines, q.result); }
            else if (q.op == "goto") lines.push_back("        JMP " + q.result);
            else if (q.op == "ifFalse") { emitLoadAX(lines, q.arg1); lines.push_back("        CMP AX,0"); lines.push_back("        JE " + q.result); }
            else if (q.op == "write") {
                if (isQuotedString(q.arg1)) { lines.push_back("        MOV DX," + asmOperand(q.arg1)); lines.push_back("        CALL WRITE_STRING"); }
                else { emitLoadAX(lines, q.arg1); lines.push_back("        CALL WRITE_VALUE"); }
            } else if (q.op == "param") lines.push_back("        PUSH " + asmOperand(q.arg1) + "        ; " + q.arg2);
            else if (q.op == "call") { lines.push_back("        CALL " + asmName(q.arg1)); if (q.result != "_") emitStoreAX(lines, q.result); }
            else if (q.op == "return") { if (q.arg1 != "_") emitLoadAX(lines, q.arg1); lines.push_back("        RET"); }
            else if (q.op == "[]=") { lines.push_back("        ; 数组赋值：" + q.result + "[" + q.arg2 + "] := " + q.arg1); emitLoadAX(lines, q.arg1); lines.push_back("        ; 实际机器实现需计算 base+index*width 后存储"); }
            else if (q.op == "=[]") { lines.push_back("        ; 数组取值：" + q.result + " := " + q.arg1 + "[" + q.arg2 + "]"); lines.push_back("        ; 实际机器实现需计算 base+index*width 后读取"); }
            else if (q.op == ".=" || q.op == "->=") { lines.push_back("        ; 结构体域赋值：" + q.result + "." + q.arg2 + " := " + q.arg1); emitLoadAX(lines, q.arg1); }
            else if (q.op == "." || q.op == "->") { lines.push_back("        ; 结构体域取值：" + q.result + " := " + q.arg1 + "." + q.arg2); }
            else lines.push_back("        ; 未展开的四元式，保留注释");
        }
        lines.push_back(seg + " ENDS");
        return lines;
    }

    void printTarget(ostream &os) {
        os << "\n八、目标代码" << endl;
        vector<string> lines = buildTarget();
        for (auto &line : lines) os << line << "\n";
    }

    void printReport(ostream &os) {
        printInstructionSet(os);
        printBlocks(os, originalBlocks, original, "四、基本块划分（原四元式）");
        printOptimization(os);
        printOptimizedQuads(os);
        printBlocks(os, optimizedBlocks, optimized, "基本块划分（优化后）");
        printLiveInfo(os);
        printTarget(os);
    }
};

void printBackendReport(const Compiler &compiler) {
    Backend backend(compiler);
    backend.run();
    backend.printReport(cout);
}


// backend.cpp 到这里结束。LL(1)、LR(0)、优先分析代码在 demos.cpp 中。
