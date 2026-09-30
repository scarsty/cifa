# CifaBytecode

`CifaBytecode` 是 Cifa 的 Lua 风格字节码后端。它不是另一个独立的脚本语言，也不是官方 Lua 的完整实现，而是**从 Lua 5.4 的字节码格式和寄存器虚拟机思路修改而来**的 Cifa 私有实现。

## 当前定位

- Cifa 负责词法分析、语法分析和静态检查。
- `CifaBytecode` 把检查后的 Cifa AST lowering 为 Lua 5.4 风格的寄存器指令。
- 运行时由项目内部的 `LuaVm` 执行，不调用官方 Lua 解释器，也不回退到 AST 解释执行。
- 可以通过 `emitted_chunk()` 取得当前生成的 Lua 风格 chunk 字节序列；该 chunk 主要用于调试、分析和基准测试。
- chunk 的执行语义由 Cifa 的私有 VM 决定，不能假定它可以直接交给任意版本的官方 Lua 运行。

后端保留 Cifa 自己的语法和语义，包括 `NoValue`、宿主函数、Cifa 全局状态同步、按值容器语义以及 Cifa 的静态检查和运行时错误信息。

字节码后端不取代 Cifa 的语法检查：语法错误、静态类型/作用域错误仍由 `Cifa` 在编译阶段负责。后端测试只要求对已经通过静态检查的脚本保持运行时语义一致，包括运行时错误、结果值和可观察的全局状态。

## 基本用法

```cpp
#include "CifaBytecode.h"
#include <iostream>

int main()
{
    cifa::CifaBytecode vm;
    if (!vm.compile_script("return 6 * 7;")) {
        std::cerr << vm.get_translation_error() << '\n';
        return 1;
    }

    const cifa::Object result = vm.run();
    if (vm.has_runtime_error()) {
        std::cerr << vm.get_runtime_error() << '\n';
        return 1;
    }
    std::cout << result.toInt64() << '\n';
}
```

当前公开入口：

| 接口 | 作用 |
| --- | --- |
| `compile_script(std::string)` | 编译脚本文本并保存当前字节码程序 |
| `compile_file(const std::string&)` | 读取并编译脚本文件 |
| `run()` | 执行最近一次成功编译的程序 |
| `run_script(std::string)` | 编译并执行脚本，适合一次性调用 |
| `run_file(const std::string&)` | 读取、编译并执行脚本文件 |
| `valid()` | 检查最近一次编译是否成功 |
| `get_translation_error()` | 取得字节码 lowering 错误 |
| `get_runtime_error()` | 取得最近一次 VM 执行错误 |
| `emitted_chunk()` | 取得生成的 chunk 字节序列 |

`run()` 没有标签参数。入口是当前编译程序的根 Proto；旧文档中关于 `run(label)`、命名 entry 或独立 `Module` 的示例已经不适用。

## 与 Lua 5.4 的关系

实现参考并修改了 Lua 5.4 的几个核心概念：

- 32 位指令编码和 Lua 风格 opcode 表；
- A/B/C、A/Bx、Ax 等寄存器指令格式；
- `TValue` 类型标签和值载荷；
- `Proto`、常量表、子 Proto 和 upvalue 描述；
- `CallInfo` 调用帧、closure 和 upvalue；
- table 的数组部分与哈希部分；
- Lua 风格的寄存器窗口和 PC 分派循环。

这些是实现基础，不表示项目声称兼容完整 Lua 5.4。Cifa 的语法、类型转换、未初始化值、宿主桥接、全局持久化和错误传播规则仍由本项目代码控制。

## 值和运行时语义

VM 中的值使用带类型标签的 `TValue`。当前支持的主要值包括整数、浮点数、布尔值、字符串、table、Lua 风格闭包、Cifa 宿主闭包、`nil` 和 `NoValue`。

`NoValue` 是 Cifa 特有的运行时值，用于表示函数没有返回值等情况。它不能被简单当作 Lua 的 `nil`：在算术、条件、类型转换、范围计算或需要具体参数的宿主调用中，VM 会生成相应的 Cifa 运行时错误。

table 同时支持数组和 map 行为。脚本函数调用、宿主函数调用和全局变量同步仍遵循 Cifa 的既有语义；字节码后端只改变执行路径，不改变这些可观察规则。

## 编译结果的组织

编译期使用 `Proto` 保存可变的构建结果：

- `code` 保存指令流；
- `constants` 保存整数、浮点数、字符串和布尔常量；
- `children` 保存嵌套脚本函数的 Proto；
- `upvalues` 保存闭包捕获描述；
- 调试名称、调用位置和 Cifa 错误信息与指令位置关联。

程序执行前，`Program` 把 Proto 冻结为运行期的 `RuntimeProto`。冻结过程会复制指令、常量、子 Proto 和诊断表，并建立 VM 使用的连续运行时布局。这不是旧后端的“双重解释器”或两个独立的 IR；Proto 是编译期结构，RuntimeProto 是执行期结构。

## 指令削减原则

当前后端仍保持 Lua 5.4 风格的 opcode 编号和编码方式，但只生成 Cifa VM 真正需要的指令。

Lua 的二元运算指令通常会配合 metamethod 指令。Cifa 当前没有这套 Lua metamethod 调度，因此 `MMBIN`、`MMBINI` 和 `MMBINK` 在本 VM 中没有运行时副作用。编译器不会再为动态算术、索引、自增、自减、转换和循环路径生成这些空操作；对应算术指令也不会再跳过下一条真实指令。

这项削减不是把动态类型脚本静态化：动态值仍以 `TValue` 运行，运行时检查、数值表示、table 访问、宿主调用和错误语义仍然保留。后续优化只有在能够证明不改变这些语义时才适合进行。

`EXTRAARG` 仍保留 Lua 风格编码，并用于 `NEWTABLE` 的扩展数组大小。它不能因为 `MMBIN*` 被删除而一并移除。

## 调试和基准测试

`benchmarks/lua_backend_benchmark.cpp` 会：

1. 编译 `cifa/calc-pi-dynamic.c`；
2. 通过 VM 执行并校验 502 个字符的 PI 结果；
3. 校验 FNV-1a32 为 `1d4b4c2f`；
4. 重复执行并报告编译时间和执行时间；
5. 把生成的 chunk 写入 `build/cifa_lua_backend_benchmark.luac`。

修改字节码 lowering 或 VM dispatch 后，应使用当前源码重新构建 benchmark，再比较结果。不要使用旧的 benchmark 可执行文件推断当前指令分布或性能。

### MSVC PI 性能诊断

当前 MSVC x64 Release 的动态 PI 对照必须区分测量口径：

- `Cifa::run_script` 每个样本都包含词法、解析、静态检查和 AST 执行；本轮 31 样本中位数约为 `300 ms`，不能与 VM 热执行直接比较。
- `CifaBytecode` benchmark 的 `compile_ms` 单独测量编译，`median_execute_ms` 只测已经编译后的 VM 执行；本轮 MSVC x64 Release 为 `compile_ms=0.9623 ms`、执行中位数 `12.913 ms`，结果为 502 字符、FNV-1a32 `1d4b4c2f`。
- 带 opcode profile 的一次 PI 执行约有 4.4M 条动态指令，其中 `MOVE` 约 2.21M、`ADDI` 约 674K、`LOADI` 约 342K、`LEN` 约 280K、`GETTABLE` 约 252K、`SETTABLE` 约 220K。它说明当前主要嫌疑是寄存器 VM 的分派和中间值搬运，不能仅凭现有数据归因于 RAII。

RAII 也不是当前首要嫌疑：历史实验中把 `RuntimeProto` 热数组改为 `unique_ptr` 已在 MSVC 和 Clang 的整数 PI 上回退，随后已撤回。后续若要减少指令，应先对 `MOVE`、`LEN` 及其相邻 lowering 做静态 listing 和交错 A/B，不能把冷态所有权对象的析构成本当作热循环成本。

测试边界如下：`unit_test/cifa_unit_test.cpp` 保留 Cifa 的完整语法/静态检查测试，并额外执行字节码后端的运行时错误 parity。当前 parity 覆盖 `size`、除零、`NoValue` 和容器方法错误；宿主函数内部的复杂 Object 转换错误（例如 `to_number({1})`）仍需单独完成统一错误传播，不能用宽松断言掩盖差异。

## 相关文件

| 文件 | 内容 |
| --- | --- |
| `CifaBytecode.h` | 后端公开接口和类声明 |
| `CifaBytecode.cpp` | Proto、chunk 编译、私有 LuaVm 和运行时实现 |
| `benchmarks/lua_backend_benchmark.cpp` | 动态 PI 正确性和性能基准 |
| `unit_test/cifa_unit_test.cpp` | Direct Cifa 与 Lua backend 的统一回归和 parity 测试 |
| `history/optimization/vm-optimization.md` | 字节码后端的优化实验和历史数据 |
