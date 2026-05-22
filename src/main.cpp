#include "../include/compiler.h"
#include <windows.h>



/*
 * main.cpp
 * ----------------------------------
 * 这个文件只做程序入口，不做具体编译工作。
 * 具体的词法分析、语法分析、符号表、四元式、优化和目标代码生成
 * 都写在其他 cpp 文件中。
 *
 * 运行方法示例：
 *   .\compiler.exe tests\test1_basic.txt
 */

int main(int argc, char **argv) {
	// ���ÿ���̨���Ϊ UTF?8 ����ҳ
	SetConsoleOutputCP(CP_UTF8);
	
#ifdef _WIN32
    // Windows 控制台默认编码可能不是 UTF-8。
    // 这里切到 65001，可以减少中文输出乱码问题。
    system("chcp 65001 > nul");
#endif

    // argc 表示命令行参数个数。
    // argv[1] 是用户输入的源程序文件名。
    // 如果用户没有输入文件名，就默认读 test.txt。
    string filename = argc >= 2 ? argv[1] : "test.txt";

    // 打开 Pascal 风格源程序。
    ifstream in(filename);
    if (!in) {
        cerr << "源程序文件打开失败: " << filename << "\n";
        cerr << "请检查文件名是否写对，例如 tests\\test1_basic.txt\n";
        return 1;
    }

    // 把整个源程序文件读入字符串。
    // 后面的 Lexer 直接从这个字符串中一个字符一个字符扫描。
    stringstream buffer;
    buffer << in.rdbuf();

    try {
        // 创建编译器对象。
        // 构造函数中会初始化词法分析器、基本类型表等。
        Compiler compiler(buffer.str());

        // 编译过程：词法分析、语法分析、语义动作、四元式生成、解释执行。
        compiler.compile();

        // 输出各个阶段的结果。
        compiler.printAll();
    } catch (const exception &e) {
        // 语法错误、语义错误、词法错误都会抛出异常。
        // 这里统一输出错误原因，方便调试源程序。
        cerr << "编译出错: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
