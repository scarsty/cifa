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
| 值与容器 | `CompactValue` 使用 `std::variant`，内联保存整数、浮点数和布尔值；数组、map 使用 VM 内部的 COW 容器，写入时分离，其他资源由 `std::any` 持有。传统宿主接口和公开结果处才转换为 `Object`。 |
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

Release PI 基准入口：

```powershell
cmd /d /c build\bytecode_benchmark.bat
```

它比较 AST 与优化字节码的 500 位 PI 输出，并要求 14 个输出完全一致。性能结论和前后时间记录见 [vm-optimization.md](vm-optimization.md)。
