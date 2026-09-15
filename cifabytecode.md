# CifaBytecode

`CifaBytecode` 是 Cifa 的独立编译字节码后端。它在编译时将脚本转换为 `Module`，运行时由 VM 执行，不回退到 AST 直接解释器。编译后的对象可重复运行，也可通过顶层标签选择入口。

## 基本使用

```cpp
#include "CifaBytecode.h"
#include <iostream>

using namespace cifa;

int main()
{
    CifaBytecode code;
    if (!code.compile_script(R"(
entry_start: return 1;
entry_resume: return 2;
)"))
    {
        std::cerr << code.get_errors_str();
        return 1;
    }

    const auto first = code.run("entry_start");
    const auto second = code.run("entry_resume");
}
```

- `compile_script()` 和 `compile_file()` 生成当前脚本的 Module。
- `run()` 从第一个顶层节点执行；`run(label)` 从指定顶层标签执行。
- 编译后的 Module 可重复运行。已发布的脚本函数、struct 和宿主全局状态保留在同一 `CifaBytecode` 实例中。
- `bytecode_statistics()` 返回只读的指令、寄存器、常量和调用点统计，不暴露私有指令布局。

## 内存资源

`CifaBytecode` 可接收共享的 `std::pmr::memory_resource`。默认构造使用 `new_delete_resource`；需要复用分配时，可显式传入标准内存池：

```cpp
auto pool = std::make_shared<std::pmr::unsynchronized_pool_resource>();
CifaBytecode code(pool);
if (code.compile_script("values={1,2,3}; return values;"))
{
    auto result = code.run();
}
```

`unsynchronized_pool_resource` 只能由一个线程同时使用。如果为内存池提供自定义上游资源，上游必须比内存池存活更久；标准内存池不拥有其上游资源。

VM 内部容器使用标准 PMR 分配器，资源通过构造参数显式传入，不使用自定义分配器模板或线程局部的分配上下文。VM、编译模块、寄存器存储及数组/map 的 COW 存储保留资源的共享所有权，容器本身只持有标准分配器中的资源指针。COW 分离时显式使用原存储的资源；存储对象和共享指针控制块仍通过普通 `make_shared` / `make_unique` 分配。

脚本字符串由 variant 中的 `VmString` 保存，内部为 `std::pmr::string` 和资源所有权，不再装入 `std::any`。复制字符串时显式保留资源，避免普通 PMR 字符串复制退回全局默认资源；移动时一起转移字符串及资源所有权。VM map 的键也使用 PMR 字符串。拼接、格式化和 native 字符串返回直接写入 PMR 存储；公共 `Object`、`std::string` 接口处进行导入/导出转换。

只在局部读取中借用 `string_view`：作用域名称查找、map 键查找、诊断源文本及格式串解析。格式解析在写入结果槽之前完成且不会调用宿主；视图不存入返回值或持久槽。`run_string` / `run_file` 在重入前仍复制脚本文本，因为嵌套执行可能替换源寄存器。

临时存储按实际 C++ 生命周期使用栈缓冲区：验证器使用 8 KiB，单次执行的全局链接表及可复用作用域描述等使用 8 KiB，宿主/方法调用的临时参数源码信息使用 2 KiB。缓冲区不足时从 VM 资源取得空间，离开对应作用域时释放；容器先析构，随后释放 arena。高频创建和销毁的脚本调用帧使用持久资源或可选内存池，避免在单次执行的单调资源中持续累积分配。返回值和持久寄存器不借用这些栈缓冲区。

标准 PMR 容器复制构造可能选择全局默认资源，移动赋值也不会传播分配器，因此需要存储的复制路径显式指定资源。公开的解析器/AST、`Object`、`ObjectVector`、`ObjectMap`、`std::any` 不透明内容及接口字符串保留现有类型；部分编译器名称表和类型/诊断元数据仍使用普通字符串。资源计数不等于整个进程的堆分配计数。导出的公开结果拥有独立的宿主存储，可以在 VM 销毁后继续使用。

### 带内联存储的 PmrAny

`CifaPmrAny.h` 提供独立的 `cifa::memory::PmrAny`。它是后续 VM 载荷迁移的基础组件，当前 `CompactValue` 的不透明载荷仍使用 `std::any`，尚未接入此类型。

```cpp
std::pmr::unsynchronized_pool_resource pool;
cifa::memory::PmrAny value(&pool);
value.emplace<std::pmr::string>(200, 'x');
auto& text = cifa::memory::any_cast<std::pmr::string&>(value);
```

内联容量为四个指针的大小（x64 为 32 字节），对齐为 `max_align_t`。只有尺寸、对齐满足要求且普通移动构造不抛异常的类型才使用内联存储；其他类型从指定资源分配。内联仅免除载荷对象本身的分配，载荷内部的字符串或容器仍可能分配。

构造与复制通过标准 uses-allocator 机制传递分配器，适用于 PMR 容器及遵循该协议的自定义类型；普通类型内部的分配不会因此自动改道。支持 `emplace`、`reset`、`type`、`any_cast`、`clone(resource)` 和 `swap`，以及标准 PMR 容器所需的分配器构造协议。

资源指针为借用关系，调用者必须保证资源存活。普通复制和移动构造保留源资源；赋值保留目标资源。显式指定其他资源的复制/移动会使用目标资源重新构造载荷，跨资源移动成功后才清空源。同资源移动不分配；跨资源移动和交换可能分配或抛异常，不承诺 `noexcept`。赋值和交换失败保留原值，`emplace` 失败后为空。

把值从栈 arena 转移到更长寿命资源时，载荷本身也必须支持正确的分配器复制语义；包装器无法修复任意类型内部的裸指针、`string_view` 或隐藏的共享所有权。已装入公共 `std::any` 的未知类型也无法通用地重新注入分配器，需要在知道类型的入口处理或保留为兼容边界。

`pmr_any` CTest 覆盖内联/堆存储、超对齐、析构、嵌套分配器传递、PMR 容器中的复制移动、跨资源转移、栈 arena 退出后的值、交换和异常清理。该测试遵循测试进程的 stderr 错误报告策略，不弹出 CRT 断言窗口。

## 宿主函数

`CifaBytecode` 继承 Cifa 的传统宿主 API，也提供直接操作 VM 寄存器的 native API：

```cpp
CifaBytecode code;
code.register_native_function("twice", [](CifaBytecode::NativeCallContext& context)
    {
        context.set_result(context.to_integer(0) * 2);
    });
```

native 回调避免构造 `ObjectVector`，适合高频内建和受控的 C++ 扩展。传统 `register_function(ObjectVector&)` 仍可用于兼容既有 Cifa 宿主代码。

## 优化模式

优化默认开启。编译前调用：

```cpp
code.set_optimization_enabled(false);
```

关闭后使用兼容模式，适用于执行期间需要通过宿主回调、`run_string` 或 `run_file` 发布或替换宿主函数、脚本函数的场景。

开启优化时，Module 会冻结宿主函数与脚本函数目录；执行期间发生变化会报告运行时错误，而不会在旧 Module 中混用新的调用目标。优化保持脚本结果、值隔离和运行时错误语义。

优化只改变内部实现；未满足静态条件的类型、别名、容器、全局和宿主调用继续使用通用路径。

优化模式会省略可证明不产生名称绑定或局部生命周期的块作用域。例如
`for (...) { value++; }` 在 `value` 已有局部槽时不再发射 `ScopeEnter/ScopeLeave`；块内没有标签时也移除
无用的 `LoopMark`。编译期块边界和标签可见性仍保留。
独立的 `analyze_binding_scopes()` 在发射指令之前使用词法作用域栈记录所有确定存在的名称，
进入块时压栈、退出时弹栈；无类型赋值查找已有绑定，否则在当前层记录新名称。
因此 `a=1; b=2; { a=9; b=8; }` 不要求名称已有静态局部槽，也不再只记住最近一次赋值。
分支和短路表达式在汇合时取确定事实的交集；普通循环对入口与回边反复取交集直到稳定，
不把可能只执行一次的绑定当成必然存在的绑定。内层块自己拥有的声明和清理不要求外层再创建空作用域。
数值类型事实支持已有数值绑定的赋值、自增/自减、算术、比较和逻辑表达式；未知类型的操作仍按可能调用宿主处理。
标签和跳转、调用、转换、容器访问及特殊绑定结构保守地使事实失效；注册自定义类型时，可能触发自定义转换的赋值也使事实失效。
声明、未证明目标已绑定的赋值、未知名称和容器访问仍保留其所在块的作用域。分析不分配隐式变量的静态槽，不改变运行时名称解析。
无类型赋值仍可创建局部名称；函数入口、switch 和范围绑定的作用域不通过此规则删除。

局部整数自增/自减在结果被丢弃时直接更新整数 payload，保留原绑定元数据与 64 位回绕语义；
此路径不构造前/后置结果，也不读取诊断位置。非整数、别名、带声明的操作及需要表达式结果的情况仍使用原路径。
是否带声明在编译时存为指令内标志，不在每次自增时查找变量元数据；指令大小仍为 80 字节。
运行时局部别名标志使用字节数组，避免 `vector<bool>` 代理访问。
`compact()` 对 `NumericForNext` 指向的“同一局部变量 < 整数字面量”条件建立 `integer_loops` 描述符，
预先保存常量上界、循环体与退出 PC。运行时确认计数器仍是非别名整数后，增量、比较和跳转一次完成；
变量上界、其他比较形式、带副作用的条件和类型改变继续走完整条件求值路径。
`bytecode_statistics().integer_loop_count` 报告建立的描述符数量；实际使用仍受运行时类型检查约束。

## 行为说明

- 编译后对象可以反复 `run()`；顶层变量、已发布的脚本函数和 struct 在同一实例中保留。
- 数组与 map 保持按值隔离语义；函数内或副本上的修改不会改写原值。
- 嵌套 `run_string` / `run_file` 共享实例全局状态，但不读取外层函数或代码块局部变量；内层 `exit()` 不会结束外层执行。
- `NoValue` 可被保存或转发；在算术、条件、转换、范围及需要具体值的宿主调用中会报告运行时错误。

## 实现原理

编译复用 Cifa 的词法、语法与静态检查，再将根代码和脚本函数分别转换为指令流。`seal()` 分离执行指令与源码信息，`verify()` 检查控制流汇合处的寄存器和作用域状态并生成操作数映射，`compact()` 移除构建期标记并同步重映射跳转与诊断位置。运行时不再递归解释 AST。

| 部分 | 实现方式 |
| --- | --- |
| 寄存器槽 | `RegisterSlots` 用连续存储保存值，以窗口基址和长度划分参数、局部变量及临时值。脚本调用使用共享存储中的窗口；动态名称绑定和全局值另有对应槽存储。名称、类型和诊断来源放在并行元数据中。 |
| 值与容器 | `CompactValue` 使用 `std::variant`，内联保存整数、浮点数和布尔值；字符串由持有资源的 `VmString` 保存，内部使用 `std::pmr::string`；数组、map 使用 COW 容器，其他不透明资源由 `std::any` 持有。传统宿主接口和公开结果处才转换为 `Object`。 |
| 指令格式 | 当前 x64 实现中，构建期 `BuildInstruction` 为 88 字节，最终 `Instruction` 为 80 字节，包含操作码、目标槽、操作数索引及站点编号。输入槽编号保存在 `register_inputs` 表中；数值专用路径另外使用 16 字节 `RegisterOperation`。这是内部内存布局，不是稳定的文件或跨平台序列化格式。 |
| 控制流 | VM 通过 PC 和单一 `switch/case` 分派执行。条件与循环转换为比较、分支、跳转等指令，部分数值序列融合执行；脚本函数使用显式调用帧保存返回位置、窗口及控制状态。 |
| 作用域与 RAII | `ScopeEnter`、`ScopeLeave` 和 `Unwind` 维护词法作用域及提前跳转时的清理；退出窗口立即释放不再存活的值，但保留存储容量。不能把这些操作当成无用标记删除。 |
| 诊断与宿主调用 | 源码位置和诊断帧保存在与 PC 对齐的冷表，报错时还原调用链。native 回调直接通过 `NativeCallContext` 访问参数槽和结果槽；传统宿主调用在边界进行 Object 转换及必要的全局同步。 |

## 验证与基准

Debug 全套测试入口为：

```powershell
$msbuild = (& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -requires Microsoft.Component.MSBuild -find "MSBuild\**\Bin\MSBuild.exe")
& $msbuild unit_test\cifa_test.sln /m /t:Build /p:Configuration=Debug /p:Platform=x64
& .\unit_test\x64\Debug\cifa_test.exe
```

当前完整回归结果为 `Passed 88 out of 88 tests.`。

使用 CMake 构建并运行当前 Release PI 基准：

```powershell
./tools/build.ps1
./build/cmake/Release/cifa_benchmark.exe 15 pi
```

它比较 AST 与优化字节码的 500 位 PI 输出，预热后记录 15 次执行并逐次检查结果。CMake 还会运行直接分配、内存池及分配器生命周期测试。性能结论、计数器用法和 CPU 采样方法见 [vm-optimization.md](vm-optimization.md)。
