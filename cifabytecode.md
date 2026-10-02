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

### 自定义 C++ 类型、`std::any` 与性能边界

Bytecode 的原生值集合不是任意 C++ 类型的通用运行时。它只对自身定义的基础值、字符串、table、闭包、宿主闭包以及 `NoValue` 建立了固定的标签、访问和指令语义；任意用户自定义 C++ 类型不能直接作为 bytecode 的高效原生值参与寄存器运算、比较、索引和生命周期管理。需要把自定义 C++ 对象暴露给脚本时，应通过明确的宿主函数/窄边界接口转换为 bytecode 支持的值，或由宿主保存不透明句柄，而不是把 C++ 对象整体塞进每个 VM 槽位。

`std::any` 可以承载几乎任意 C++ 类型，但这只是通用边界容器，不是适合 bytecode 热路径的值表示。它会擦除静态类型，使 VM 难以使用紧凑的标签和直接访问；读取通常需要类型检查或 `any_cast`，跨宿主边界还可能触发包装、解包和重新物化。对数组、map 和自定义对象而言，若值按值传递，容器和其元素还可能被递归复制。因而“`std::any` 能承载任何 C++ 类型”不能推出“bytecode 能以低成本执行任何 C++ 类型”。

C++ 的复制语义同样是重要的性能约束。一个看似普通的参数传递、寄存器赋值、返回值同步或宿主调用，可能复制字符串、容器、`std::any` 内部对象以及它们拥有的子对象；这些复制发生在 VM 指令之外时尤其容易被误认为只是一次轻量的 `TValue` 搬运。若改为引用或共享所有权，又必须明确别名、写时分离、失效和宿主可观察状态，否则会改变 Cifa 的按值语义。

RAII 适合表达冷态资源的所有权和异常安全，但 `unique_ptr`、`shared_ptr`、容器析构以及自定义析构函数并不会因为放入 bytecode 就消失。若它们位于寄存器值、临时结果、参数窗口或每轮循环都会创建/销毁的对象中，构造、引用计数、析构和间接寻址都会进入脚本热路径，造成严重性能问题；资源所有权也可能扩大对象尺寸并破坏连续值布局。Bytecode 应尽量让热路径使用紧凑的带标签值和稳定的 VM 容器，把 RAII 资源管理限制在宿主边界、程序冻结/销毁等冷路径。任何把 `std::any`、深复制或复杂 RAII 对象引入值槽的设计，都必须以相同脚本、相同结果校验和交错基准证明没有热路径回退，不能只凭接口泛化能力判断其适合 VM。

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

## 调试和回归测试

字节码后端的回归测试位于 `unit_test/cifa_unit_test.cpp`，覆盖 Cifa 的语法/静态检查以及字节码后端的运行时错误 parity。修改 lowering 或 VM dispatch 后，应使用当前源码重新构建 `cifa_tests` 并运行完整回归，不要依赖历史 benchmark 输出判断当前行为。

## 相关文件

| 文件 | 内容 |
| --- | --- |
| `CifaBytecode.h` | 后端公开接口和类声明 |
| `CifaBytecode.cpp` | Proto、chunk 编译、私有 LuaVm 和运行时实现 |
| `unit_test/cifa_unit_test.cpp` | Direct Cifa 与 Lua backend 的统一回归和 parity 测试 |
| `vm-optimization.md` | 字节码后端的优化实验和历史数据 |
