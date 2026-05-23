# 编译器后端讲解：从四元式到目标代码

本文档详细讲解给出的 `Backend` 类实现。它接收前端生成的四元式（三地址码），依次完成**基本块划分**、**局部优化**（常量折叠/传播、公共子表达式消除）、**简单循环优化**（不变式外提）、**活跃变量分析**、**无用赋值删除**以及**目标代码生成**（模拟 8086 汇编）。以下按处理流程逐模块说明。

---

## 一、整体处理流程

整个后端的入口是 `Backend::run()`：

```cpp
void run() {
    originalBlocks = buildBlocks(original);
    afterLocalOpt = localOptimize(original);
    afterLoopOpt = loopOptimize(afterLocalOpt);
    optimized = removeDeadAssignments(afterLoopOpt);
    optimizedBlocks = buildBlocks(optimized);
    live = computeLive(optimized);
}
```

1. **原始四元式** → 基本块划分（仅用于输出展示）
2. **局部优化**（在基本块内部）
3. **循环优化**（简单不变式外提）
4. **删除无用赋值**（基于活跃信息）
5. 再次划分优化后的基本块（供输出）
6. 计算最终四元式的活跃信息

最后通过 `printReport()` 输出所有中间结果和目标代码。

---

## 二、基本块划分 (`buildBlocks`)

### 2.1 Leader 确定规则

- 第一条四元式一定是 Leader
- 任何 `label`、`function`、`main` 四元式所在位置
- 跳转语句（`goto`、`ifFalse`）的下一条四元式
- 跳转语句的目标地址（查 `labelIndex` 映射表）

这些规则确保控制流图的每个基本块只有一个入口。

### 2.2 构造基本块

将 Leader 排序后，每个 Leader 到下一个 Leader 前一条指令构成一个块。记录每个块的后继（`succ`）：

- 若最后一条是 `goto L`，后继为 `L` 所在块
- 若最后一条是 `ifFalse L`，后继为 `L` 所在块 **和** 下一个块
- 普通顺序执行，后继为下一个块
- `return`/`end`/`endmain` 无后继

### 2.3 代码示例

```cpp
BasicBlockInfo b;
b.start = leaders[i];
b.end = (i+1 < leaders.size() ? leaders[i+1]-1 : qs.size()-1);
```

---

## 三、局部优化 (`localOptimize`)

在 **每个基本块内部** 进行下列优化，不跨越块边界，确保控制流安全。

### 3.1 常量传播

维护一个 **常量环境** `constEnv`（变量 → 已知常量值）。遇到二元/一元运算或赋值时，将操作数替换为常量。

```cpp
q.arg1 = replaceByConst(q.arg1);
q.arg2 = replaceByConst(q.arg2);
```

当变量被重新定义时，调用 `kill(d)` 清除该变量在常量环境和表达式缓存中的条目。

### 3.2 常量折叠

对二元/一元运算，若所有操作数都是数字字面量，直接计算并替换为 `:=` 赋值。

```cpp
if (evalBinary(op, a, b, folded)) {
    q = {":=", folded, "_", q.result};
}
```

支持 `+ - * / < > <= >= = <> && ||` 以及一元 `neg` 和 `!`。

### 3.3 代数化简

处理常见恒等/零等运算，例如：
- `x + 0 → x`
- `x * 1 → x`
- `x * 0 → 0`
- `true && x → x` 等

### 3.4 公共子表达式消除

使用一个哈希表 `exprResult`：键是 `op|arg1|arg2`（可交换运算符自动排序 arg1/arg2），值是存放结果的临时变量名。

当再次遇到相同表达式时，直接替换为已计算的临时变量：

```cpp
if (exprResult.count(key) && q.result != exprResult[key]) {
    q = {":=", exprResult[key], "_", q.result};
}
```

### 3.5 条件跳转常量优化

若 `ifFalse` 的条件是常量：
- 恒假（0） → 改为 `goto`
- 恒真（非0） → 删除该跳转（设为 `nop`）

### 3.6 常量环境更新

仅对 `:=` 且源操作数是字面量、目标是临时变量时，才加入常量环境。这避免对普通用户变量过度优化，使最终目标代码更贴近课本示例。

---

## 四、简单循环优化 (`loopOptimize`)

该函数寻找形如 `goto label` 且 `label` 出现在当前指令 **之前** 的结构，从而识别循环。

### 4.1 循环不变式识别

对循环体（`[start, end]`）内的每条指令：
- 仅考虑二元/一元运算
- 结果必须是临时变量 `t...`
- 该指令的所有使用变量在循环内没有被赋值（即不在 `assigned` 集合中）

```cpp
bool invariant = true;
for (auto &u : usesOf(q)) 
    if (assigned.count(u)) invariant = false;
```

### 4.2 外提

将不变式指令移动到循环入口（`start` 位置）之前，并标记原位置为跳过。

实现时使用 `insertBefore[start]` 收集需要插入的指令，最后重新拼接四元式序列。

---

## 五、活跃变量分析 (`computeLive`)

### 5.1 数据流方程

对于每条四元式 i，定义：

- `use[i]`：该指令读取的所有变量
- `def[i]`：该指令写入的所有变量
- `in[i]`：进入该指令时活跃的变量
- `out[i]`：离开该指令时活跃的变量

迭代方程：

```
out[i] = ∪_{s∈succ[i]} in[s]
in[i]  = use[i] ∪ (out[i] - def[i])
```

### 5.2 后继关系

与基本块不同，这里计算 **四元式级别** 的后继：
- `goto L` → 后继为 `L` 的索引
- `ifFalse L` → 后继为 `L` 的索引 和 i+1
- 普通指令 → 后继为 i+1
- 终止指令无后继

### 5.3 迭代求解

从后向前反复计算，直到 `in`/`out` 集合不再变化。`while (changed)` 保证不动点。

---

## 六、删除无用赋值 (`removeDeadAssignments`)

根据活跃信息，删除满足以下条件的四元式：
- 是“纯计算”指令（二元/一元运算、赋值、数组/结构体取值等）
- 只定义一个变量
- 该变量是临时变量（形如 `t1`, `t2`）
- 该变量在 `out[i]` 中不活跃（即后续不再使用）

```cpp
bool dead = pure && d.size()==1 && isTempName(*d.begin()) && !li[i].out.count(*d.begin());
```

普通用户变量即使无用也保留，避免破坏预期行为。

---

## 七、目标代码生成 (`buildTarget`)

### 7.1 内存布局

- 调用 `buildTargetMemory()` 为所有变量（`SymbolKind::VAR` / `PARAM`）分配偏移地址，按 4 字节对齐。
- 字符串字面量生成独立标号（`STR0`, `STR1`…），并存储为 `DB` 定义。

### 7.2 汇编指令映射

| 四元式               | 生成的汇编风格                                |
|----------------------|---------------------------------------------|
| `:= a, _, t`         | `MOV AX, a`<br>`MOV [offset_t], AX`         |
| `+ a, b, t`          | `MOV AX, a`<br>`ADD AX, b`<br>`MOV [t], AX` |
| `* a, b, t`          | `MOV AX, a`<br>`IMUL AX, b`<br>`MOV [t], AX`|
| `/ a, b, t`          | `MOV AX, a`<br>`MOV BX, b`<br>`CWD`<br>`IDIV BX`<br>`MOV [t], AX` |
| `< a, b, t`          | 生成比较分支，结果 0/1 存入 t                |
| `neg a, t`           | `NEG AX`                                    |
| `! a, t`             | 分支生成逻辑非                              |
| `goto L`             | `JMP L`                                     |
| `ifFalse a, L`       | `CMP AX,0`<br>`JE L`                        |
| `write a`            | 调用 `WRITE_VALUE` 或 `WRITE_STRING`        |
| `param a`            | `PUSH [offset_a]`                           |
| `call f`             | `CALL f`                                    |
| `return a`           | `MOV AX, a`<br>`RET`                        |
| `endmain`            | `INT 21H`<br>`RET`                          |
| `[]=`, `=[]`, 结构体访问 | 生成注释，指出需要地址计算                |

### 7.3 辅助函数

- `asmOperand(x)`：将变量转为 `[偏移]` 或直接数值，字符串转为 `OFFSET STRn`。
- `hex4(v)`：将整数格式化为 4 位十六进制数（如 `0004H`）。
- `setwString(off)`：生成 `[0000H]` 形式的内存引用。

---

## 八、关键数据结构

### 8.1 Quad（四元式）

在头文件 `backend.h` 中定义（虽未给出，但可推测）：

```cpp
struct Quad {
    string op;      // 操作符
    string arg1;    // 第一操作数
    string arg2;    // 第二操作数
    string result;  // 结果变量或跳转目标
};
```

### 8.2 BasicBlockInfo

```cpp
struct BasicBlockInfo {
    int id;
    int start, end;     // 四元式下标范围
    vector<int> succ;   // 后继块id
    string reason;      // 块入口原因（调试用）
};
```

### 8.3 LiveInfo

```cpp
struct LiveInfo {
    set<string> use, def, in, out;
};
```

---

## 九、运行示例输出

调用 `printBackendReport(compiler)` 会依次输出：

1. **指令集说明**（MOV, ADD, JMP 等含义）
2. **原始四元式的基本块划分**
3. **优化处理记录**（常量折叠、公共子表达式消除、循环外提等）
4. **优化后的四元式列表**
5. **优化后代码的基本块划分**
6. **活跃信息表**（每行的 USE/DEF/IN/OUT）
7. **生成的目标汇编代码**（含数据段和代码段）

---

## 十、局限性 & 改进方向

- **循环优化**仅识别简单的 `goto` 后向跳转，不支持 `while`/`for` 结构识别。
- **数组/结构体**的目标代码只生成注释，未实现真实地址计算（如 `base + index * width`）。
- **临时变量分配**当前直接映射到内存偏移，未做寄存器分配。
- **条件跳转**生成较多临时标号，可优化为直接使用标志位。

尽管如此，本后端完整演示了编译原理课本中经典的数据流分析和优化技术，适合教学和理解编译器后端的核心流程。

---

*文档结束*
