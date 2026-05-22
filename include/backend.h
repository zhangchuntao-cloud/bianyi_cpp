#pragma once
#include "compiler.h"

/*
 * backend.hpp
 * 后端入口函数声明。
 *
 * 后端主要做这些事：
 *   - 基本块划分
 *   - 优化
 *   - 活跃变量分析
 *   - 目标代码生成
 *
 * 具体实现放在 backend.cpp 里面。
 */

/*
 * printBackendReport
 * 功能：打印后端分析结果。
 * 传入 Compiler 对象后，可以直接读取其中的四元式、符号表等信息。
 */
void printBackendReport(const Compiler &compiler);
