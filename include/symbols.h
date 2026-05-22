#pragma once
#include "common.h"

/*
 * symbols.hpp
 * 这个头文件里放的是编译器前后端都要用的数据结构。
 *
 * 你可以把它理解成“符号系统的基础定义”：
 *   - 类型表 TYPEL
 *   - 主符号表 SYNBL
 *   - 常量表 CONSL
 *   - 数组信息表 AINFL
 *   - 结构体信息表 RINFL
 *   - 函数信息表 PFINFL
 *   - 活动记录表
 *
 * 这些名字基本都对应老师课件里的图。
 */

// =============================================================
// 类型、符号表、常量表、活动记录
// =============================================================

/*
 * TypeKind
 * 表示一个“类型”的大类。
 * 比如 integer、real、char、array、struct、function 等。
 */
enum class TypeKind {
    INT, REAL, CHAR, STRING, BOOL, VOID, ARRAY, STRUCT, FUNCTION
};

/*
 * SymbolKind
 * 表示符号表里一个标识符属于哪种类别。
 * 比如是变量、常量、函数、类型名、结构体域名、形参等。
 */
enum class SymbolKind {
    VAR, CONST_, FUNC, TYPE, FIELD, PARAM
};

/*
 * ParamMode
 * 形参的传递方式。
 * VALUE 代表传值，对应课件里的 vf
 * ADDRESS 代表传地址，对应课件里的 vn
 */
enum class ParamMode {
    NONE, VALUE, ADDRESS
};

/*
 * TypeEntry
 * 类型表中的一项。
 *
 * 这些字段大概可以这样理解：
 *   - id          : 这个类型在类型表里的编号
 *   - kind        : 类型的大类
 *   - name        : 类型名字，方便输出
 *   - width       : 这个类型占多少字节
 *   - elemType    : 数组元素类型编号
 *   - arrayLength : 数组长度
 *   - structIndex  : 如果是结构体，指向结构体表
 *   - funcIndex    : 如果是函数类型，指向函数表
 */
struct TypeEntry {
    int id = -1;
    TypeKind kind = TypeKind::VOID;
    string name;
    int width = 0;
    int elemType = -1;
    int arrayLength = 0;
    int structIndex = -1;
    int funcIndex = -1;
};

/*
 * ArrayInfo
 * 数组附加信息表 AINFL。
 * 数组类型比较特殊，除了类型本身，还要记录下界、上界、元素类型等信息。
 */
struct ArrayInfo {
    int typeId;
    int low;
    int high;
    int elemType;
    int totalSize;
};

/*
 * StructMember
 * 表示结构体里的一个成员。
 */
struct StructMember {
    string name;
    int typeId;
    int offset;
};

/*
 * StructInfo
 * 结构体信息表 RINFL。
 * 一个结构体类型会有自己的成员列表和总大小。
 */
struct StructInfo {
    string name;
    vector<StructMember> members;
    int totalSize = 0;
};

/*
 * FuncInfo
 * 函数信息表 PFINFL。
 *
 * 这里保存函数的：
 *   - 函数名
 *   - 返回值类型
 *   - 形参类型、形参名、传参方式
 *   - 入口四元式位置
 *   - 函数所属作用域编号
 */
struct FuncInfo {
    string name;
    int retType;
    vector<int> paramTypes;
    vector<string> paramNames;
    vector<ParamMode> paramModes;
    int entryQuad = 0;
    int scopeId = -1;
};

/*
 * ConstEntry
 * 常量表 CONSL 中的一项。
 * 保存常量的字面值和类型编号。
 */
struct ConstEntry {
    string value;
    int typeId;
};

/*
 * SymbolEntry
 * 主符号表 SYNBL 中的一项。
 *
 * 字段说明：
 *   - name      : 标识符名字
 *   - typeId    : 类型表编号
 *   - kind      : 符号类别
 *   - scopeId   : 这个符号属于哪个作用域
 *   - offset    : 偏移地址，主要给变量/形参用
 *   - extra     : 附加信息，按符号种类指向不同子表
 *   - paramMode : 形参的传递方式
 */
struct SymbolEntry {
    string name;
    int typeId;
    SymbolKind kind;
    int scopeId;
    int offset;
    int extra;
    ParamMode paramMode = ParamMode::NONE;
};

/*
 * ActRecordEntry
 * 活动记录/值单元分配表。
 * 主要记录变量、形参、临时变量在运行时的存储信息。
 */
struct ActRecordEntry {
    int funcIndex;
    string owner;
    string name;
    string catCode;
    int typeId;
    int offset;
    int width;
};

/*
 * typeKindName
 * 功能：把内部的 TypeKind 枚举转成更容易读的类型名字。
 */
static inline string typeKindName(TypeKind k) {
    switch (k) {
        case TypeKind::INT: return "integer";
        case TypeKind::REAL: return "real";
        case TypeKind::CHAR: return "char";
        case TypeKind::STRING: return "string";
        case TypeKind::BOOL: return "boolean";
        case TypeKind::VOID: return "void";
        case TypeKind::ARRAY: return "array";
        case TypeKind::STRUCT: return "struct";
        case TypeKind::FUNCTION: return "function";
    }
    return "?";
}

/*
 * symbolKindName
 * 功能：把 SymbolKind 转成中文说明，方便打印符号表时看懂。
 */
static inline string symbolKindName(SymbolKind k) {
    switch (k) {
        case SymbolKind::VAR: return "变量标识符";
        case SymbolKind::CONST_: return "常量标识符";
        case SymbolKind::FUNC: return "函数标识符";
        case SymbolKind::TYPE: return "类型标识符";
        case SymbolKind::FIELD: return "域名标识符";
        case SymbolKind::PARAM: return "形参标识符";
    }
    return "?";
}

/*
 * paramModeName
 * 功能：把形参传递方式转成说明文字。
 */
static inline string paramModeName(ParamMode m) {
    switch (m) {
        case ParamMode::VALUE: return "vf(赋值/传值形参)";
        case ParamMode::ADDRESS: return "vn(指针/地址形参)";
        case ParamMode::NONE: return "-";
    }
    return "-";
}

/*
 * catCode
 * 功能：把符号种类转成老师图里用的 CAT 编码。
 *
 * 例如：
 *   变量 -> v
 *   常量 -> c
 *   函数 -> f
 *   类型 -> t
 *   域名 -> d
 *   形参 -> vf / vn
 */
static inline string catCode(SymbolKind k, ParamMode m = ParamMode::NONE) {
    switch (k) {
        case SymbolKind::VAR: return "v";
        case SymbolKind::CONST_: return "c";
        case SymbolKind::FUNC: return "f";
        case SymbolKind::TYPE: return "t";
        case SymbolKind::FIELD: return "d";
        case SymbolKind::PARAM: return m == ParamMode::ADDRESS ? "vn" : "vf";
    }
    return "?";
}

// =============================================================
// 四元式与运行期值
// =============================================================

/*
 * Quad
 * 四元式结构：(op, arg1, arg2, result)
 * 是后端处理中最常见的一种中间表示。
 */
struct Quad {
    string op, arg1, arg2, result;
};

/*
 * RuntimeValue
 * 解释执行四元式时，运行时保存的值。
 * 这里用 number + text 两种形式，方便处理数字和字符串。
 */
struct RuntimeValue {
    TypeKind kind = TypeKind::INT;
    double number = 0;
    string text;
};

// =============================================================
// 编译器类声明
// =============================================================

// 后端报告函数：在 Compiler 完整定义之后实现。
// 它负责基本块划分、优化、活跃信息分析和目标代码生成。
class Compiler;
