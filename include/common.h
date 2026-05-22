#pragma once

/*
 * common.hpp
 * 这是整个项目共用的头文件。
 * 这里主要放一些标准库头文件、using namespace std，
 * 还有一些大家都会用到的小工具函数。
 *
 * 这样做的好处是：其他文件不用每次都重复写一大堆 include。
 */

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stack>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace std;

// =============================================================
// 一些通用的小工具函数
// =============================================================

/*
 * join
 * 功能：把字符串数组按指定分隔符拼接起来。
 *
 * 例如：
 *   vector<string> a = {"x", "y", "z"};
 *   join(a, ",") 结果就是 "x,y,z"
 */
static inline string join(const vector<string> &xs, const string &sep) {
    string r;
    for (size_t i = 0; i < xs.size(); ++i) {
        if (i) r += sep;
        r += xs[i];
    }
    return r;
}

/*
 * trimNumber
 * 功能：把一个 double 变成更适合输出的字符串。
 *
 * 例如：
 *   3.0000000000 -> "3"
 *   3.1400000000 -> "3.14"
 *
 * 主要是为了输出好看一点，不要一串多余的 0。
 */
static inline string trimNumber(double x) {
    // 如果这个数其实就是整数，就直接按整数输出。
    if (fabs(x - llround(x)) < 1e-9) return to_string((long long)llround(x));

    ostringstream os;
    os << fixed << setprecision(10) << x;
    string s = os.str();

    // 去掉后面多余的 0
    while (!s.empty() && s.back() == '0') s.pop_back();

    // 如果最后只剩一个小数点，也去掉
    if (!s.empty() && s.back() == '.') s.pop_back();

    return s.empty() ? "0" : s;
}
