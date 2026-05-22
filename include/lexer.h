#pragma once
#include "common.h"

/*
 * lexer.hpp
 * 词法分析模块。
 *
 * 主要作用：
 *   - 从源程序字符串中一个个读出单词
 *   - 判断它是关键字、标识符、常量还是运算符
 *   - 生成 Token 序列
 *   - 顺便维护词法分析阶段用到的一些表
 */

// =============================================================
// 词法分析
// =============================================================

/*
 * TokenType
 * 词法单元类型枚举。
 *
 * 基本上程序里出现的关键字、运算符、界符、字面量，
 * 都会被转换成这里面的某一种。
 */
enum class TokenType {
    PROGRAM, VAR, CONST_KW, BEGIN_, END_, INTEGER_KW, REAL_KW, CHAR_KW, STRING_KW, BOOLEAN_KW,
    IF, THEN, ELSE, WHILE, DO, WRITE, READ, RETURN, FUNCTION, PROCEDURE,
    STRUCT, ARRAY, OF, INT_KW, FLOAT_KW, VOID_KW, TRUE_KW, FALSE_KW,
    ID, INT_LITERAL, REAL_LITERAL, CHAR_LITERAL, STRING_LITERAL,
    PLUS, MINUS, STAR, SLASH, LT, GT, LE, GE, EQ, NE, AND, OR, NOT,
    ASSIGN, COLON, SEMI, COMMA, DOT, ARROW,
    LPAREN, RPAREN, LBRACKET, RBRACKET, LBRACE, RBRACE,
    END_OF_FILE
};

/*
 * Token
 * 一个 Token 就是词法分析后的结果。
 * 保存：
 *   - 类型
 *   - 原始字符串
 *   - 所在行列号
 */
struct Token {
    TokenType type;
    string lexeme;
    int line;
    int col;
};

/*
 * tokenName
 * 功能：把 TokenType 转成字符串，便于报错和调试时输出。
 */
static inline string tokenName(TokenType t) {
    switch (t) {
        case TokenType::PROGRAM: return "program";
        case TokenType::VAR: return "var";
        case TokenType::CONST_KW: return "const";
        case TokenType::BEGIN_: return "begin";
        case TokenType::END_: return "end";
        case TokenType::INTEGER_KW: return "integer";
        case TokenType::REAL_KW: return "real";
        case TokenType::CHAR_KW: return "char";
        case TokenType::STRING_KW: return "string";
        case TokenType::BOOLEAN_KW: return "boolean";
        case TokenType::IF: return "if";
        case TokenType::THEN: return "then";
        case TokenType::ELSE: return "else";
        case TokenType::WHILE: return "while";
        case TokenType::DO: return "do";
        case TokenType::WRITE: return "write";
        case TokenType::READ: return "read";
        case TokenType::RETURN: return "return";
        case TokenType::FUNCTION: return "function";
        case TokenType::PROCEDURE: return "procedure";
        case TokenType::STRUCT: return "struct";
        case TokenType::ARRAY: return "array";
        case TokenType::OF: return "of";
        case TokenType::INT_KW: return "int";
        case TokenType::FLOAT_KW: return "float";
        case TokenType::VOID_KW: return "void";
        case TokenType::TRUE_KW: return "true";
        case TokenType::FALSE_KW: return "false";
        case TokenType::ID: return "ID";
        case TokenType::INT_LITERAL: return "INT_LITERAL";
        case TokenType::REAL_LITERAL: return "REAL_LITERAL";
        case TokenType::CHAR_LITERAL: return "CHAR_LITERAL";
        case TokenType::STRING_LITERAL: return "STRING_LITERAL";
        case TokenType::PLUS: return "+";
        case TokenType::MINUS: return "-";
        case TokenType::STAR: return "*";
        case TokenType::SLASH: return "/";
        case TokenType::LT: return "<";
        case TokenType::GT: return ">";
        case TokenType::LE: return "<=";
        case TokenType::GE: return ">=";
        case TokenType::EQ: return "=";
        case TokenType::NE: return "<>";
        case TokenType::AND: return "&&";
        case TokenType::OR: return "||";
        case TokenType::NOT: return "!";
        case TokenType::ASSIGN: return ":=";
        case TokenType::COLON: return ":";
        case TokenType::SEMI: return ";";
        case TokenType::COMMA: return ",";
        case TokenType::DOT: return ".";
        case TokenType::ARROW: return "->";
        case TokenType::LPAREN: return "(";
        case TokenType::RPAREN: return ")";
        case TokenType::LBRACKET: return "[";
        case TokenType::RBRACKET: return "]";
        case TokenType::LBRACE: return "{";
        case TokenType::RBRACE: return "}";
        case TokenType::END_OF_FILE: return "EOF";
    }
    return "?";
}

/*
 * Lexer
 * 词法分析器。
 *
 * 它会保存源代码字符串，并不断往前读字符，
 * 然后把字符组织成一个个 Token。
 */
class Lexer {
    string src;      // 源程序全文
    size_t pos = 0;  // 当前读到的位置
    int line = 1, col = 1;   // 当前行号和列号
    unordered_map<string, TokenType> keywords; // 关键字映射表

    /*
     * peek
     * 功能：只看当前位置后面的字符，不真正移动指针。
     * off 表示偏移量，默认看当前位置。
     */
    char peek(int off = 0) const {
        size_t p = pos + off;
        return p < src.size() ? src[p] : '\0';
    }

    /*
     * get
     * 功能：读走一个字符，并更新行号、列号。
     * 如果读到换行，就把行号加一，列号重置成 1。
     */
    char get() {
        char c = peek();
        if (pos < src.size()) {
            ++pos;
            if (c == '\n') {
                ++line;
                col = 1;
            } else {
                ++col;
            }
        }
        return c;
    }

    /*
     * lexicalError
     * 功能：直接抛出词法错误。
     * 这里加了行列号，方便定位出错位置。
     */
    [[noreturn]] void lexicalError(const string &msg) const {
        throw runtime_error("Lexical error at line " + to_string(line) + ", column " + to_string(col) + ": " + msg);
    }

    /*
     * skipBlankAndComments
     * 功能：跳过空白字符和注释。
     *
     * 支持两种注释：
     *   1. 单行注释
     *   2. 多行注释
     */
    void skipBlankAndComments() {
        while (true) {
            while (isspace((unsigned char)peek())) get();

            if (peek() == '/' && peek(1) == '/') {
                while (peek() && peek() != '\n') get();
                continue;
            }

            if (peek() == '/' && peek(1) == '*') {
                get();
                get();
                while (peek()) {
                    if (peek() == '*' && peek(1) == '/') {
                        get();
                        get();
                        break;
                    }
                    get();
                }
                continue;
            }

            break;
        }
    }

public:
    vector<string> keywordTable = {
        "program", "var", "const", "begin", "end", "integer", "real", "char", "string", "boolean",
        "if", "then", "else", "while", "do", "write", "read", "return", "function", "procedure",
        "struct", "array", "of", "int", "float", "void", "true", "false"
    };

    vector<string> delimiterTable = {
        "+", "-", "*", "/", "<", ">", "<=", ">=", "=", "==", "<>", "!=",
        "&&", "||", "!", ":=", ":", ";", ",", ".", "->", "(", ")", "[", "]", "{", "}"
    };

    // 标识符表：保存所有出现过的标识符
    vector<string> identifierTable;

    // 常量表：保存所有出现过的常量
    vector<string> constantTable;

    // Token 序列的简写表示
    vector<pair<char, int>> tokenSequence;

    // 所有 Token 的完整记录
    vector<Token> allTokens;

    /*
     * 构造函数
     * 传入源代码字符串后，先把关键字表初始化好。
     */
    explicit Lexer(string s) : src(std::move(s)) {
        vector<pair<string, TokenType>> ks = {
            {"program", TokenType::PROGRAM}, {"var", TokenType::VAR}, {"const", TokenType::CONST_KW}, {"begin", TokenType::BEGIN_},
            {"end", TokenType::END_}, {"integer", TokenType::INTEGER_KW}, {"real", TokenType::REAL_KW},
            {"char", TokenType::CHAR_KW}, {"string", TokenType::STRING_KW}, {"boolean", TokenType::BOOLEAN_KW},
            {"if", TokenType::IF}, {"then", TokenType::THEN}, {"else", TokenType::ELSE},
            {"while", TokenType::WHILE}, {"do", TokenType::DO}, {"write", TokenType::WRITE},
            {"read", TokenType::READ}, {"return", TokenType::RETURN}, {"function", TokenType::FUNCTION},
            {"procedure", TokenType::PROCEDURE}, {"struct", TokenType::STRUCT}, {"array", TokenType::ARRAY},
            {"of", TokenType::OF}, {"int", TokenType::INT_KW}, {"float", TokenType::FLOAT_KW},
            {"void", TokenType::VOID_KW}, {"true", TokenType::TRUE_KW}, {"false", TokenType::FALSE_KW}
        };
        for (auto &p : ks) keywords[p.first] = p.second;
    }

    /*
     * addIdentifier
     * 功能：把一个标识符加入标识符表。
     * 如果之前已经出现过，就直接返回原来的编号。
     */
    int addIdentifier(const string &s) {
        auto it = find(identifierTable.begin(), identifierTable.end(), s);
        if (it != identifierTable.end()) return int(it - identifierTable.begin()) + 1;
        identifierTable.push_back(s);
        return (int)identifierTable.size();
    }

    /*
     * addConstant
     * 功能：把一个常量加入常量表。
     * 如果之前已经存在，就复用原编号。
     */
    int addConstant(const string &s) {
        auto it = find(constantTable.begin(), constantTable.end(), s);
        if (it != constantTable.end()) return int(it - constantTable.begin()) + 1;
        constantTable.push_back(s);
        return (int)constantTable.size();
    }

    /*
     * nextToken
     * 词法分析的核心函数。
     *
     * 每调用一次，就从源代码里读出下一个 Token。
     * 读完后会把 Token 记录到 allTokens、tokenSequence 等表里。
     */
    Token nextToken() {
        skipBlankAndComments();

        int startLine = line, startCol = col;
        char c = peek();
        if (!c) return {TokenType::END_OF_FILE, "", line, col};

        // 1. 先处理标识符和关键字
        if (isalpha((unsigned char)c) || c == '_') {
            string s;
            while (isalnum((unsigned char)peek()) || peek() == '_') s += get();

            // 统一转成小写，方便和关键字表匹配
            auto low = s;
            transform(low.begin(), low.end(), low.begin(), [](unsigned char ch) { return (char)tolower(ch); });

            auto it = keywords.find(low);
            if (it != keywords.end()) {
                int index = int(find(keywordTable.begin(), keywordTable.end(), low) - keywordTable.begin()) + 1;
                tokenSequence.push_back({'k', index});
                Token t{it->second, low, startLine, startCol};
                allTokens.push_back(t);
                return t;
            }

            // 普通标识符
            int index = addIdentifier(s);
            tokenSequence.push_back({'i', index});
            Token t{TokenType::ID, s, startLine, startCol};
            allTokens.push_back(t);
            return t;
        }

        // 2. 处理数字常量（整数 / 实数）
        if (isdigit((unsigned char)c)) {
            string s;
            bool real = false;

            while (isdigit((unsigned char)peek())) s += get();

            // 小数部分
            if (peek() == '.' && isdigit((unsigned char)peek(1))) {
                real = true;
                s += get();
                while (isdigit((unsigned char)peek())) s += get();
            }

            // 科学计数法
            if (peek() == 'e' || peek() == 'E') {
                real = true;
                s += get();
                if (peek() == '+' || peek() == '-') s += get();
                if (!isdigit((unsigned char)peek())) lexicalError("bad exponent in number");
                while (isdigit((unsigned char)peek())) s += get();
            }

            int index = addConstant(s);
            tokenSequence.push_back({'c', index});
            Token t{real ? TokenType::REAL_LITERAL : TokenType::INT_LITERAL, s, startLine, startCol};
            allTokens.push_back(t);
            return t;
        }

        // 3. 处理字符常量
        if (c == '\'') {
            string raw;
            raw += get();

            char value;
            if (peek() == '\\') {
                raw += get();
                char e = get();
                raw += e;
                switch (e) {
                    case 'n': value = '\n'; break;
                    case 't': value = '\t'; break;
                    case 'r': value = '\r'; break;
                    case '\\': value = '\\'; break;
                    case '\'': value = '\''; break;
                    default: value = e; break;
                }
            } else {
                if (!peek() || peek() == '\n') lexicalError("unclosed character literal");
                value = get();
                raw += value;
            }

            if (peek() != '\'') lexicalError("character literal must contain exactly one character");
            raw += get();

            string saved = string("'") + (value == '\n' ? "\\n" : value == '\t' ? "\\t" : string(1, value)) + "'";
            int index = addConstant(saved);
            tokenSequence.push_back({'c', index});
            Token t{TokenType::CHAR_LITERAL, saved, startLine, startCol};
            allTokens.push_back(t);
            return t;
        }

        // 4. 处理字符串常量
        if (c == '"') {
            get();
            string val, display = "\"";

            while (peek() && peek() != '"') {
                if (peek() == '\n') lexicalError("unclosed string literal");

                if (peek() == '\\') {
                    get();
                    char e = get();
                    switch (e) {
                        case 'n': val += '\n'; display += "\\n"; break;
                        case 't': val += '\t'; display += "\\t"; break;
                        case 'r': val += '\r'; display += "\\r"; break;
                        case '"': val += '"'; display += "\\\""; break;
                        case '\\': val += '\\'; display += "\\\\"; break;
                        default: val += e; display += e; break;
                    }
                } else {
                    char ch = get();
                    val += ch;
                    display += ch;
                }
            }

            if (peek() != '"') lexicalError("unclosed string literal");
            get();
            display += "\"";

            int index = addConstant(display);
            tokenSequence.push_back({'c', index});
            Token t{TokenType::STRING_LITERAL, display, startLine, startCol};
            allTokens.push_back(t);
            return t;
        }

        // 5. 先处理双字符运算符 / 界符
        auto emitDelimiter = [&](TokenType ty, const string &lex) -> Token {
            int index = int(find(delimiterTable.begin(), delimiterTable.end(), lex) - delimiterTable.begin()) + 1;
            tokenSequence.push_back({'p', index});
            Token t{ty, lex, startLine, startCol};
            allTokens.push_back(t);
            return t;
        };

        if (c == ':' && peek(1) == '=') { get(); get(); return emitDelimiter(TokenType::ASSIGN, ":="); }
        if (c == '<' && peek(1) == '=') { get(); get(); return emitDelimiter(TokenType::LE, "<="); }
        if (c == '>' && peek(1) == '=') { get(); get(); return emitDelimiter(TokenType::GE, ">="); }
        if (c == '=' && peek(1) == '=') { get(); get(); return emitDelimiter(TokenType::EQ, "=="); }
        if (c == '<' && peek(1) == '>') { get(); get(); return emitDelimiter(TokenType::NE, "<>"); }
        if (c == '!' && peek(1) == '=') { get(); get(); return emitDelimiter(TokenType::NE, "!="); }
        if (c == '&' && peek(1) == '&') { get(); get(); return emitDelimiter(TokenType::AND, "&&"); }
        if (c == '|' && peek(1) == '|') { get(); get(); return emitDelimiter(TokenType::OR, "||"); }
        if (c == '-' && peek(1) == '>') { get(); get(); return emitDelimiter(TokenType::ARROW, "->"); }

        // 6. 单字符运算符 / 界符
        get();
        switch (c) {
            case '+': return emitDelimiter(TokenType::PLUS, "+");
            case '-': return emitDelimiter(TokenType::MINUS, "-");
            case '*': return emitDelimiter(TokenType::STAR, "*");
            case '/': return emitDelimiter(TokenType::SLASH, "/");
            case '<': return emitDelimiter(TokenType::LT, "<");
            case '>': return emitDelimiter(TokenType::GT, ">");
            case '=': return emitDelimiter(TokenType::EQ, "=");
            case '!': return emitDelimiter(TokenType::NOT, "!");
            case ':': return emitDelimiter(TokenType::COLON, ":");
            case ';': return emitDelimiter(TokenType::SEMI, ";");
            case ',': return emitDelimiter(TokenType::COMMA, ",");
            case '.': return emitDelimiter(TokenType::DOT, ".");
            case '(': return emitDelimiter(TokenType::LPAREN, "(");
            case ')': return emitDelimiter(TokenType::RPAREN, ")");
            case '[': return emitDelimiter(TokenType::LBRACKET, "[");
            case ']': return emitDelimiter(TokenType::RBRACKET, "]");
            case '{': return emitDelimiter(TokenType::LBRACE, "{");
            case '}': return emitDelimiter(TokenType::RBRACE, "}");
            default: lexicalError(string("unknown character '") + c + "'");
        }
    }
};
