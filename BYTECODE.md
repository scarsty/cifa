# 独立字节码后端

`CifaBytecode` 是独立的可编译脚本对象，适合保存编译结果、重复运行脚本，或从顶层标签进入执行。

```c++
#include "CifaBytecode.h"

using namespace cifa;

CifaBytecode code;
code.register_function("record", [](ObjectVector& arguments) -> Object
	{
		return Object();
	});

if (!code.compile_script(R"(
entry_start: record(1); exit();
entry_resume: record(2); exit();
)"))
{
	std::cerr << code.get_errors_str();
}
else
{
	code.run("entry_start");
	code.run("entry_resume");
}
```

`run()` 从第一个顶层节点执行；`run("label")` 从指定的顶层标签执行。标签不存在时产生运行时错误。每次执行拥有独立的局部作用域、返回状态和 `exit()` 状态；宿主回调以及已发布的字节码函数和 struct 定义在同一 `CifaBytecode` 对象内保留。

## 边界

`CalUnit` 只在 `CifaBytecode` 编译期用于语法归约。构造完成后，运行时不保存、解释或重新编译解析节点。

- `CifaBytecode` 是编译门面：读取 Cifa 的短生命周期解析结果，生成 `Module`，随后释放编译期源节点映射。
- `Module` 持有指令、常量、名称表、源码行、函数代码、结构体定义、标签入口和调用点元数据。
- `Machine` 持有作用域、局部槽位、显式调用帧、函数 Module 注册表、结构定义、退出状态和运行时诊断。
- 函数查找由 `Machine` 的 `(name, arity) -> shared_ptr<const Module>` 注册表完成。活动帧保留旧 Module；后续调用可见重定义后的 Module。
- `Cifa` 只提供既有宿主函数、宿主全局值、类型注册、解析和直接求值。除 `friend class CifaBytecode` 外，不添加 VM 注册表、执行器或 VM 错误状态接口。

运行时不能调用 Cifa 的直接求值器，也不能读取 `functions2`、`execution_contexts` 或解析函数体作为回退。

## 翻译示例

下面的清单是便于阅读的语义化反汇编，不是当前对外 API：`#N` 表示 `Module::constants` 中的常量编号，`@N` 表示指令绝对 PC，`$N` 表示函数帧局部槽位。实际 `Instruction` 还保存 `SourceRef`、名称表/调用点编号和诊断帧，本文省略这些内部编号。

### 1. 常量、算术与赋值

源码：

```cifa
int total = 2;
total += 3 * 4;
return total;
```

字节码：

```text
Constant       #0 (2)
PrepareStore   total : int
Store          total : int, Assign
Drop

Constant       #1 (3)
Constant       #2 (4)
Multiply
PrepareStore   total : int
Store          total : int, Add
Drop

Load           total
Return
```

`PrepareStore` 先建立带类型绑定的目标，再计算并写回 RHS；`Store` 的 `WriteOperation::Add` 等价于读取旧值、执行 `Add`、再依照目标类型写回。因此复合赋值不会重复求值左值。

### 2. 条件与跳转

源码：

```cifa
int value = 7;
if (value > 3) return 1;
return 0;
```

字节码：

```text
Constant       #0 (7)
PrepareStore   value : int
Store          value : int, Assign
Drop

Load           value
Constant       #1 (3)
Greater
Branch         @12              ; 假时跳到下一条 return
Constant       #2 (1)
Return

@12: Constant  #3 (0)
Return
```

`Branch` 同时带有条件表达式自己的 `SourceRef`，所以 `value > 3` 的运行时错误会定位到条件，而不是整个 `if`。真实指令流还会在表达式边界插入 `Enter` / `Leave`，用于建立诊断调用链。

### 3. 已编译脚本函数调用

源码：

```cifa
int twice(int number) { return number * 2; }
return twice(21);
```

函数 `twice/1` 的独立 `FunctionCode`：

```text
LoadLocal      $0 (number)
Constant       #0 (2)
Multiply
Return
```

根指令流：

```text
CallBegin      twice/1
Constant       #1 (21)
BindArgument   #0 -> twice/1.number
Call           twice/1
CallEnd        twice/1
Return
```

`Call` 通过 Machine 的函数 Module 注册表查找 `(twice, 1)`，创建显式 Frame 和局部槽位 `$0`，然后切换到 `FunctionCode` 的指令流。它不会解释函数解析节点，也不会在调用时编译函数。

### 4. 类型数组与方法实参

源码：

```cifa
int values[];
values.push_back(9);
return values[0];
```

字节码：

```text
Empty
Index          values : int[], declaration
Drop

MethodBegin    values.push_back
Constant       #0 (9)
MethodValue    argument #0
Drop

Constant       #1 (0)
Index          values[0]
Return
```

声明中的 `Empty` 表示未指定数组长度，`Index(... declaration)` 将它规范化为长度 0 的 `ObjectVector`，并记录元素类型 `int`。`MethodValue` 先只计算一次 `9`，`call_method` 再按照数组元素类型转换并追加；索引扩容或方法重入不会保存数组元素的裸指针。

## 执行能力

- 单 PC 执行循环和显式 Frame：表达式、短路、赋值、局部槽位、函数调用、递归、return、exit、goto、break、continue、循环、range 与 switch。
- Module 自有源码映射。Machine 独立格式化运行时错误、调用栈、源码行及 caret；NoValue 保留原始调用位置。
- Module 自有函数和结构体定义；结构实例、`type()`、数组/map、内置容器方法和多维索引不依赖 Cifa 的直接求值状态。
- 类型语义：内置数值转换、注册类型、结构体、动态形参、auto 推导、NoValue 与重复执行。
- 宿主桥：Machine 管理 Object 错误报告器；宿主回调中的转换错误可协作中止副作用，回调完成后清理临时 Cifa 运行时状态。嵌套 VM `RuntimeError` 会传播；普通嵌套静态错误可累积。
- Module 生命周期：编译器对象销毁后，Machine 已发布函数仍可执行；同名函数重定义替换后续调用；失败函数仍使用所属 Module 的源码和 caret。

## 验证

已通过的 Debug 验证：

- `build\bytecode_test.exe --verifier-only`：字节码验证器通过。
- `build\bytecode_test.exe`：公开 API 的递归、重复执行、标签、嵌套脚本、函数持久化/重定义和原样 500 位 PI 均通过。
- `unit_test\cifa_unit_test.cpp`：Cifa 直接求值 Debug 模式为 `Passed 72 out of 72 tests.`；定义 `CIFA_TEST_BYTECODE` 的 Debug 模式为 `Passed 75 out of 75 tests.`。

`common_backend_conformance_test` 用一份模板化场景分别执行 `Cifa` 与 `CifaBytecode`。控制流和递归、数组/map、跨脚本函数和 struct、宿主回调、嵌套脚本与 include 必须在两个后端都通过，该测试才通过。新增共通行为时应优先加入这组共享场景，而不是复制一份 Cifa 测试和一份字节码测试。

只保留不能由另一后端表达的专有测试：直接求值器的源码状态与直接运算符回调测试，以及字节码的编译后重复运行、标签入口和独立 Module 生命周期测试。

持久字节码函数和 struct 在下一编译单元中通过明确的签名目录投影参与静态检查，运行期不回读 Cifa 的直接求值状态。

## PI 性能

基准脚本为 `cifa/calc-pi.c`，计算并返回 500 位 PI 字符串。`build/pi_benchmark.cpp` 关闭脚本输出，先预热 1 次，再记录 6 次样本；每轮都核对直接求值与字节码结果完全相同。

- `AST source to result`：每轮新建 `Cifa` 后调用 `run_file`，包含读取、解析、静态检查和直接执行。
- `Source to CifaBytecode`：每轮新建 `CifaBytecode` 后调用 `compile_file`，包含读取、解析、静态检查和字节码翻译。
- `CifaBytecode execute`：在同一份已编译模块上调用 `run`，表示可重复执行时的运行成本。

使用 `build\pi_benchmark.bat` 构建并执行 Release 基准。输出包含每项的中位数、均值及全部样本，下面记录以该脚本的实际输出为准。

2026-09-12 在 Windows x64、Visual Studio 2026 18.10.0、MSVC Release `/O2 /DNDEBUG` 下的结果为：

| 测量项 | 中位数 (ms) | 均值 (ms) | 六次样本 (ms) |
| --- | ---: | ---: | --- |
| AST source to result | 406.343 | 405.371 | 402.853, 403.460, 404.955, 406.343, 406.827, 407.786 |
| Source to CifaBytecode | 1.5766 | 1.50453 | 1.3138, 1.3360, 1.3572, 1.5766, 1.6855, 1.7581 |
| CifaBytecode execute | 363.414 | 363.464 | 362.732, 362.793, 363.146, 363.414, 363.861, 364.840 |

同一份脚本的字节码重复执行中位数比直接从源码执行低约 $10.6\%$；字节码编译成本单独计量，未计入重复执行时间。

## 语义约束

- 脚本函数只能访问自身局部和 Machine 全局层，不能访问调用者局部。
- 同一 Module 的函数声明必须自包含；Machine 持久函数通过编译前签名目录投影对后续编译单元可见。
- 赋值、下标和方法参数按既有求值顺序执行；下标只求值一次，扩容后按逻辑路径重新定位。
- `NoValue` 可被丢弃、保存或转发；在要求具体值的转换、算术、条件、范围和宿主调用中报告运行时错误。
- 内层 `exit()` 不泄漏到外层 Machine；第一个运行时错误停止后续字节码副作用，不回滚已发生副作用。
- 错误验收必须同时比较正文、源码行和 caret 对齐。

## 运行时边界

- 不将 Cifa 直接求值、惰性函数编译或 Cifa 执行上下文作为字节码回退。
- 不恢复 Cifa 内的 VM 函数注册、执行器或运行时错误状态。
