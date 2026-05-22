#include "../include/backend.h"

/*
 * demos.cpp
 * ----------------------------------
 * 这里放三个语法分析相关模块：LL(1)、LR(0)、算符优先分析。
 * 主编译流程用的是递归下降，这几个模块主要用于展示课设要求中的
 * FIRST/FOLLOW/SELECT、分析表自动生成和优先关系矩阵。
 */

// =============================================================
/* LL1Demo：计算表达式文法的 FIRST、FOLLOW、SELECT 和 LL(1) 分析表。 */
class LL1Demo {
public:
    struct Prod { string lhs; vector<string> rhs; };
    vector<Prod> prods;
    set<string> VN, VT;
    map<string, set<string>> first, follow;
    map<int, set<string>> select;
    map<pair<string,string>, int> table;

    LL1Demo() {
        prods = {
            {"E", {"T", "E'"}}, {"E'", {"+", "T", "E'"}}, {"E'", {"-", "T", "E'"}}, {"E'", {"ε"}},
            {"T", {"F", "T'"}}, {"T'", {"*", "F", "T'"}}, {"T'", {"/", "F", "T'"}}, {"T'", {"ε"}},
            {"F", {"(", "E", ")"}}, {"F", {"id"}}, {"F", {"num"}}
        };
        for (auto &p : prods) VN.insert(p.lhs);
        for (auto &p : prods) for (auto &s : p.rhs) if (s != "ε" && !VN.count(s)) VT.insert(s);
        VT.insert("$");
        computeFirst(); computeFollow(); computeSelectAndTable();
    }
    set<string> firstOfSeq(const vector<string> &seq) {
        set<string> ans; bool nullable = true;
        for (auto &s : seq) {
            if (s == "ε") { ans.insert("ε"); nullable = false; break; }
            if (!VN.count(s)) { ans.insert(s); nullable = false; break; }
            for (auto &x : first[s]) if (x != "ε") ans.insert(x);
            if (!first[s].count("ε")) { nullable = false; break; }
        }
        if (nullable) ans.insert("ε");
        return ans;
    }
    void computeFirst() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto &p : prods) {
                auto fs = firstOfSeq(p.rhs);
                for (auto &x : fs) if (!first[p.lhs].count(x)) { first[p.lhs].insert(x); changed = true; }
            }
        }
    }
    void computeFollow() {
        follow["E"].insert("$");
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto &p : prods) {
                for (size_t i = 0; i < p.rhs.size(); ++i) if (VN.count(p.rhs[i])) {
                    vector<string> beta(p.rhs.begin() + i + 1, p.rhs.end());
                    auto fb = firstOfSeq(beta.empty() ? vector<string>{"ε"} : beta);
                    for (auto &x : fb) if (x != "ε" && !follow[p.rhs[i]].count(x)) { follow[p.rhs[i]].insert(x); changed = true; }
                    if (fb.count("ε")) for (auto &x : follow[p.lhs]) if (!follow[p.rhs[i]].count(x)) { follow[p.rhs[i]].insert(x); changed = true; }
                }
            }
        }
    }
    void computeSelectAndTable() {
        for (int i = 0; i < (int)prods.size(); ++i) {
            auto fs = firstOfSeq(prods[i].rhs);
            for (auto &x : fs) if (x != "ε") select[i].insert(x);
            if (fs.count("ε")) for (auto &x : follow[prods[i].lhs]) select[i].insert(x);
            for (auto &a : select[i]) table[{prods[i].lhs, a}] = i;
        }
    }
    void print() {
        cout << "\n九、LL(1) 分析表" << endl;
        for (int i = 0; i < (int)prods.size(); ++i) cout << setw(2) << i << ": " << prods[i].lhs << " -> " << join(prods[i].rhs, " ") << "\n";
        cout << "\nFIRST 集：\n";
        for (auto &nt : VN) cout << "  FIRST(" << nt << ") = { " << join(vector<string>(first[nt].begin(), first[nt].end()), ", ") << " }\n";
        cout << "\nFOLLOW 集：\n";
        for (auto &nt : VN) cout << "  FOLLOW(" << nt << ") = { " << join(vector<string>(follow[nt].begin(), follow[nt].end()), ", ") << " }\n";
        cout << "\nSELECT 集：\n";
        for (int i = 0; i < (int)prods.size(); ++i) cout << "  SELECT(" << prods[i].lhs << " -> " << join(prods[i].rhs, " ") << ") = { " << join(vector<string>(select[i].begin(), select[i].end()), ", ") << " }\n";
        vector<string> terms = {"id", "num", "+", "-", "*", "/", "(", ")", "$"};
        cout << "\n预测分析表 M[A,a]：\n" << setw(8) << "";
        for (auto &t : terms) cout << setw(12) << t;
        cout << "\n";
        for (auto &nt : VN) {
            cout << setw(8) << nt;
            for (auto &t : terms) {
                auto it = table.find({nt, t});
                if (it == table.end()) cout << setw(12) << "";
                else cout << setw(12) << (prods[it->second].lhs + "->" + join(prods[it->second].rhs, ""));
            }
            cout << "\n";
        }
    }
};

// =============================================================
// LR(0) 项目集族与分析表示例
// =============================================================
/* LR0Demo：构造 LR(0) 项目集族，并输出 ACTION/GOTO 表。 */
class LR0Demo {
public:
    struct Prod { string lhs; vector<string> rhs; };
    struct Item { int p; int dot; bool operator<(const Item &o) const { return p != o.p ? p < o.p : dot < o.dot; } };
    vector<Prod> prods;
    set<string> VN, VT;
    vector<set<Item>> states;
    map<set<Item>, int> stateId;
    map<pair<int,string>, int> trans;

    LR0Demo() {
        prods = {{"S'", {"E"}}, {"E", {"E", "+", "T"}}, {"E", {"T"}}, {"T", {"T", "*", "F"}}, {"T", {"F"}}, {"F", {"(", "E", ")"}}, {"F", {"id"}}, {"F", {"num"}}};
        for (auto &p : prods) VN.insert(p.lhs);
        for (auto &p : prods) for (auto &s : p.rhs) if (!VN.count(s)) VT.insert(s);
        VT.insert("$"); build();
    }
    set<Item> closure(set<Item> I) {
        bool changed = true;
        while (changed) {
            changed = false;
            set<Item> add = I;
            for (auto &it : I) if (it.dot < (int)prods[it.p].rhs.size()) {
                string x = prods[it.p].rhs[it.dot];
                if (VN.count(x)) for (int i = 0; i < (int)prods.size(); ++i) if (prods[i].lhs == x) {
                    Item ni{i,0}; if (!add.count(ni)) { add.insert(ni); changed = true; }
                }
            }
            I = add;
        }
        return I;
    }
    set<Item> goTo(const set<Item> &I, const string &x) {
        set<Item> J;
        for (auto &it : I) if (it.dot < (int)prods[it.p].rhs.size() && prods[it.p].rhs[it.dot] == x) J.insert({it.p, it.dot + 1});
        return closure(J);
    }
    void build() {
        set<Item> start = closure({{0,0}});
        states.push_back(start); stateId[start] = 0;
        for (size_t i = 0; i < states.size(); ++i) {
            set<string> symbols;
            for (auto &it : states[i]) if (it.dot < (int)prods[it.p].rhs.size()) symbols.insert(prods[it.p].rhs[it.dot]);
            for (auto &x : symbols) {
                auto J = goTo(states[i], x); if (J.empty()) continue;
                if (!stateId.count(J)) { int id = (int)states.size(); stateId[J] = id; states.push_back(J); }
                trans[{(int)i, x}] = stateId[J];
            }
        }
    }
    string itemText(Item it) const {
        vector<string> rhs = prods[it.p].rhs; rhs.insert(rhs.begin() + it.dot, "·");
        return prods[it.p].lhs + " -> " + join(rhs, " ");
    }
    void print() {
        cout << "\n十、LR(0) 项目集族和分析表" << endl;
        for (size_t i = 0; i < states.size(); ++i) {
            cout << "I" << i << ":\n";
            for (auto &it : states[i]) cout << "  " << itemText(it) << "\n";
        }
        cout << "\nGOTO 转换：\n";
        for (auto &tr : trans) cout << "  goto(I" << tr.first.first << ", " << tr.first.second << ") = I" << tr.second << "\n";
        cout << "\nLR(0) ACTION/GOTO 表（表达式左递归文法可能出现移进/归约冲突，表中以 r 表示归约）：\n";
        vector<string> terms = {"id", "num", "+", "*", "(", ")", "$"};
        vector<string> nts = {"E", "T", "F"};
        cout << setw(6) << "State"; for (auto &t : terms) cout << setw(10) << t; for (auto &n : nts) cout << setw(10) << n; cout << "\n";
        for (size_t i = 0; i < states.size(); ++i) {
            cout << setw(6) << i;
            for (auto &t : terms) {
                string cell;
                if (trans.count({(int)i, t})) cell = "s" + to_string(trans[{(int)i, t}]);
                for (auto &it : states[i]) if (it.dot == (int)prods[it.p].rhs.size()) {
                    if (it.p == 0 && t == "$") cell = cell.empty()?"acc":cell+"/acc";
                    else if (it.p != 0) cell = cell.empty() ? ("r" + to_string(it.p)) : (cell + "/r" + to_string(it.p));
                }
                cout << setw(10) << cell;
            }
            for (auto &n : nts) cout << setw(10) << (trans.count({(int)i, n}) ? to_string(trans[{(int)i, n}]) : "");
            cout << "\n";
        }
    }
};

// =============================================================
// 简单优先分析 FIRSTVT/LASTVT/优先矩阵
// =============================================================
/* OperatorPrecedenceDemo：计算 FIRSTVT、LASTVT 和算符优先关系矩阵。 */
class OperatorPrecedenceDemo {
public:
    map<string, vector<vector<string>>> G;
    set<string> VN, VT;
    map<string, set<string>> firstvt, lastvt;
    map<pair<string,string>, string> prec;
    OperatorPrecedenceDemo() {
        G["E"] = {{"E", "+", "T"}, {"T"}};
        G["T"] = {{"T", "*", "F"}, {"F"}};
        G["F"] = {{"(", "E", ")"}, {"id"}, {"num"}};
        for (auto &p : G) VN.insert(p.first);
        for (auto &p : G) for (auto &rhs : p.second) for (auto &s : rhs) if (!VN.count(s)) VT.insert(s);
        VT.insert("$"); compute(); build();
    }
    void compute() {
        bool changed = true;
        while (changed) {
            changed = false;
            for (auto &p : G) for (auto &rhs : p.second) {
                if (!rhs.empty()) {
                    if (!VN.count(rhs[0])) { if (firstvt[p.first].insert(rhs[0]).second) changed = true; }
                    else {
                        for (auto &x : firstvt[rhs[0]]) if (firstvt[p.first].insert(x).second) changed = true;
                        if (rhs.size() > 1 && !VN.count(rhs[1])) if (firstvt[p.first].insert(rhs[1]).second) changed = true;
                    }
                    string last = rhs.back();
                    if (!VN.count(last)) { if (lastvt[p.first].insert(last).second) changed = true; }
                    else {
                        for (auto &x : lastvt[last]) if (lastvt[p.first].insert(x).second) changed = true;
                        if (rhs.size() > 1 && !VN.count(rhs[rhs.size()-2])) if (lastvt[p.first].insert(rhs[rhs.size()-2]).second) changed = true;
                    }
                }
            }
        }
    }
    void build() {
        for (auto &p : G) for (auto &rhs : p.second) for (size_t i = 0; i + 1 < rhs.size(); ++i) {
            string a = rhs[i], b = rhs[i+1];
            if (!VN.count(a) && !VN.count(b)) prec[{a,b}] = "=";
            if (!VN.count(a) && VN.count(b)) for (auto &x : firstvt[b]) prec[{a,x}] = "<";
            if (VN.count(a) && !VN.count(b)) for (auto &x : lastvt[a]) prec[{x,b}] = ">";
            if (i + 2 < rhs.size() && !VN.count(rhs[i]) && VN.count(rhs[i+1]) && !VN.count(rhs[i+2])) prec[{rhs[i], rhs[i+2]}] = "=";
        }
        for (auto &x : firstvt["E"]) prec[{"$", x}] = "<";
        for (auto &x : lastvt["E"]) prec[{x, "$"}] = ">";
    }
    void print() {
        cout << "\n十一、算符优先分析表" << endl;
        for (auto &nt : VN) cout << "  FIRSTVT(" << nt << ")={" << join(vector<string>(firstvt[nt].begin(), firstvt[nt].end()), ",") << "}  LASTVT(" << nt << ")={" << join(vector<string>(lastvt[nt].begin(), lastvt[nt].end()), ",") << "}\n";
        vector<string> symbols(VT.begin(), VT.end());
        cout << "\n" << setw(6) << ""; for (auto &b : symbols) cout << setw(6) << b; cout << "\n";
        for (auto &a : symbols) { cout << setw(6) << a; for (auto &b : symbols) cout << setw(6) << (prec.count({a,b}) ? prec[{a,b}] : ""); cout << "\n"; }
    }
};

/* printAll：总输出函数，依次输出前端、后端和分析表。 */
void Compiler::printAll() {
    // 总输出函数。按照“前端结果 -> 后端结果 -> 分析表”的顺序打印。
    printLexicalTables();
    printSymbolSystem();
    printQuads();
    printBackendReport(*this);
    cout << "\n========== 十二、源程序 write 输出 ==========" << endl;
    if (runtimeOutput.empty()) cout << "<无 write 输出>\n";
    else for (auto &s : runtimeOutput) cout << s << "\n";
    cout << "\n十三、翻译文法和语义动作" << endl;
    cout << "S -> id := E       { emit(':=', E.place, _, id.place) }\n";
    cout << "S -> if E then S1 else S2  { backpatch / ifFalse / goto / label }\n";
    cout << "S -> while E do S  { beginLabel, ifFalse, body, goto beginLabel, endLabel }\n";
    cout << "E -> E1 op E2      { temp=newtemp(); emit(op,E1.place,E2.place,temp); E.place=temp }\n";
    cout << "F -> id | num | call | array | struct-field { 查符号表、查类型表、生成取值四元式 }\n";

    LL1Demo().print();
    LR0Demo().print();
    OperatorPrecedenceDemo().print();
}

