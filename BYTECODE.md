# 独立字节码后端

## 当前实现

`CifaBytecode` 是独立的编译后 VM：执行时不回退到 AST 或直接解释器。运行时值存放在单一 `Object` 寄存器池中，局部槽、表达式暂存和调用窗口共享该池；脚本调用只保存并恢复调用者的窗口边界，不复制整套寄存器文件。

验证器以控制流数据流维护每个 PC 的活动寄存器上界，并为每条指令写入显式的 `Instruction::input_base`、`input_count` 和 `destination`。输入是连续寄存器窗口 `R(input_base)..R(input_base + input_count - 1)`，不再使用操作数旁表。基本块合流必须拥有相同的保守连续寄存器布局；语句结果通过编译期 `discard_result` 生命周期标记回收，不生成运行期清理 opcode。

已经完成的过渡：

- 诊断边界存放在冷的 `diagnostic_frames` 表；不存在 `Drop` opcode、执行镜像或根/普通块的空值哨兵。
- 每个 `Instructions` 记录最大嵌套作用域数；执行和脚本调用入口预留 `ScopeStack` 容量。
- 局部 `ObjectVector` 的一维读、写和 `push_back` 走直接 opcode 路径，同时保留元素类型转换、扩容和错误语义。
- 保持原始内建身份的数值数学函数会生成 `MathUnary` / `MathBinary`。数值参数直接调用 `std::` 数学函数；非数值参数和被用户覆盖的内建仍走常规宿主调用。

### 寄存器约定

- 每条普通值指令都有显式结果槽 `A = destination`；`B = input_base` 与 `C = input_count` 描述连续输入窗口。文档使用 `R0`、`R1` 表示这些槽。
- 二元指令在语义反汇编中写为 `OP R(A), R(B), R(C)`，一元指令写为 `OP R(A), R(B)`；常量写为 `LoadK R(A), K(Bx)`。这与 Lua 的 A/B/C 可读形式一致。
- 实际热码还不是 Lua 5.4 的固定 32 位 `iABC`，但输入已经是 Lua 式连续寄存器窗口：`A = destination`、`B = input_base`、`C = input_count`。`Call`、`GetTable`、`NewTable` 可通过连续窗口携带超过两个输入，无需操作数旁表。
- `register_capacity` 是模块所需的最大活动 `Object` 寄存器数，不是操作数栈容量。
- Machine 在构造时直接物理 `resize` 256 槽，逻辑活动窗口仍从 `R0` 开始；超过 256 槽时再按需扩容。共享槽池记录物理槽总数、当前逻辑活动上界、高水位、扩容次数与已清理槽数，可由 `CIFA_VM_PROFILE` 输出验证常见工作负载是否发生扩容。
- 所有容器方法先执行零输入、零输出的 `CheckMethodReceiver`，再按源序求值连续参数窗口并执行 `MethodCall R(A), R(B), C`（单参数 `push_back` 可走直接快路径）。这是 Cifa AST 的真实顺序：非法 receiver 或不支持的方法必须在任何参数副作用之前报错。`insert` 不再需要专用 opcode、暂存参数向量或额外寄存器窗口。
- 条件、短路、循环、`switch` 和 `goto` 的所有跳转目标必须合并到相同的活动寄存器上界；语句上下文的分支值在编译期标记为不可见。
- 当前分配器保守地使用连续槽位；跨基本块 phi 寄存器和非连续槽位复用仍是独立的后续优化，不影响执行器的显式操作数合同。

---

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

编译成功后可调用 `bytecode_statistics()` 取得只读统计，包括根模块和各脚本函数的指令数、显式寄存器操作数数、
寄存器容量、常量数及调用点数。该接口不暴露内部 `Instruction` 布局，适合建立优化前后的字节码长度基线。

## 可选优化

优化默认开启；如需在字节码编译后动态替换宿主函数或脚本函数，在编译前调用 `set_optimization_enabled(false)` 使用兼容模式。优化后的 Module 允许改变内部指令布局和运行时调用栈，但保持脚本结果与运行时错误语义。

当前优化会折叠只由数值/布尔字面量、内建数值 cast 以及内建算术、比较、逻辑和位运算组成的纯常量子树，也会折叠参数全为常量的 `abs`、`min`、`max`、`ifv`、`ifvalue` 和内建数学函数。对于返回 `int`、`double` 或 `bool`、参数未声明类型、函数体只有一个纯 `return` 的脚本函数，也会在调用实参全为常量时递归折叠。递归函数检测到调用环后保留普通 VM 调用。整数除零、整数除法溢出、非法移位和浮点 `%` 不折叠，继续在运行时通过原指令报告错误。

在脚本函数目录冻结的优化 Module 中，还会实际内联更小的一类调用：函数必须是单个纯表达式 `return`，形参在该表达式中各恰好出现一次，实参只能是字面量、字符串/布尔值或简单变量读取，且不存在直接递归。替换后的表达式仍按声明的返回类型插入 cast，保持普通 `Return` 的转换语义。任何不符合条件的调用（包括循环体、数组操作、重复使用形参、带副作用的实参或递归）继续使用标准 VM 帧调用。

优化 Module 还会对保持构造期原始身份的 `size` 和数值数学内建使用直达路径，对整数二元运算使用 VM 内部实现，并缓存脚本函数调用点的目标；新 Module 发布函数后会自动失效该缓存。函数局部、非别名的一维 `ObjectVector` 读、写和 `push_back` 会直接使用当前帧局部槽位，保留数组扩容、元素类型绑定、未初始化元素和越界错误语义。map、string、多维索引、别名和全局数组继续走通用路径。

默认优化模式下，宿主函数注册表必须保持不变。每个优化 Module 会在编译时记录宿主函数版本；后续任意 `register_function`（包括覆盖已有名称）都会令它拒绝执行并报告运行时错误。应在注册全部宿主函数后编译；需要增删宿主函数时，重新创建并编译 `CifaBytecode`，或显式关闭优化。即使是在编译前覆盖了同名内建函数，该名称也不再参与内建折叠，仍正常调用宿主实现。

默认优化模式也会冻结 VM 脚本函数目录：同一 Module 可重复执行，后续独立脚本可调用或发布持久函数，但活动优化 Module 执行期间不能用 `run_string`、`run_file` 或宿主回调发布新的脚本函数。遇到替换会报告 `script functions changed during optimized bytecode execution`。需要执行期间动态替换函数时，应在编译前显式关闭优化。

`Object` 使用单一 `std::variant<std::monostate, int64_t, double, bool, std::any>` 存储：`int64_t`、`double` 和 `bool` 以内联 variant 分支保存，不进入 `std::any`；字符串、数组、map、struct 和宿主注册 C++ 类型存入 `std::any` 分支。直接解释器与字节码 VM 共享该表示，公开的 `Object` 构造、转换和引用 API 保持不变。

## 边界

`CalUnit` 只在 `CifaBytecode` 编译期用于语法归约。构造完成后，运行时不保存、解释或重新编译解析节点。

- `CifaBytecode` 是编译门面：读取 Cifa 的短生命周期解析结果，生成 `Module`，随后释放编译期源节点映射。
- `Module` 持有指令、常量、名称表、源码行、函数代码、结构体定义、标签入口和调用点元数据。
- `Machine` 持有作用域、局部槽位、显式调用帧、函数 Module 注册表、结构定义、退出状态和运行时诊断。
- 函数查找由 `Machine` 的 `(name, arity) -> shared_ptr<const Module>` 注册表完成。活动帧保留旧 Module；后续调用可见重定义后的 Module。
- `Cifa` 只提供既有宿主函数、宿主全局值、类型注册、解析和直接求值。除 `friend class CifaBytecode` 外，不添加 VM 注册表、执行器或 VM 错误状态接口。

运行时不能调用 Cifa 的直接求值器，也不能读取 `functions2`、`execution_contexts` 或解析函数体作为回退。

## 翻译示例

下面的清单是便于阅读的语义化反汇编，不是当前对外 API：`K(N)` 表示 `Module::constants` 中的常量，`R(N)` 表示值寄存器，`L(N)` 表示函数帧局部槽位，`@N` 表示绝对 PC。它展示 `A, B, C` 风格的值流；实际 `Instruction` 还保存名称/调用点编号、变长输入切片和诊断帧。

### 1. 常量、算术与赋值

源码：

```cifa
int total = 2;
total += 3 * 4;
return total;
```

字节码：

```text
LoadK          R0, K0 (2)
PrepareGlobalStore   total : int
SetGlobal      total : int, R0, Assign

LoadK          R0, K1 (3)
LoadK          R1, K2 (4)
Multiply       R0, R0, R1
PrepareGlobalStore   total : int
SetGlobal      total : int, R0, Add

GetGlobal      R0, total
Return         R0
```

`PrepareGlobalStore`、`PrepareFieldStore` 和 `PrepareIndexStore` 会先冻结对应的全局、字段或索引左值，再计算并写回 RHS。`SetGlobal`、`SetField`、`SetIndex` 的 `WriteOperation::Add` 等价于读取旧值、执行 `Add`、再依照目标类型写回。因此复合赋值不会重复求值左值。

### 2. 条件与跳转

源码：

```cifa
int value = 7;
if (value > 3) return 1;
return 0;
```

字节码：

```text
LoadK          R0, K0 (7)
PrepareGlobalStore   value : int
SetGlobal      value : int, R0, Assign

GetGlobal      R0, value
LoadK          R1, K1 (3)
Greater        R0, R0, R1
Test           R0, @12          ; 假时跳到下一条 return
LoadK          R0, K2 (1)
Return         R0

@12: LoadK      R0, K3 (0)
Return          R0
```

`Test` 同时带有条件表达式自己的 `SourceRef`，所以 `value > 3` 的运行时错误会定位到条件，而不是整个 `if`。表达式边界的诊断调用链由冷的 `diagnostic_frames` 表建立，不额外发射执行期边界 opcode。

### 3. 已编译脚本函数调用

源码：

```cifa
int twice(int number) { return number * 2; }
return twice(21);
```

函数 `twice/1` 的独立 `FunctionCode`：

```text
Move           R0, L0 (number)
LoadK          R1, K0 (2)
Multiply       R0, R0, R1
Return         R0
```

根指令流：

```text
LoadK          R0, K1 (21)
Cast           R0, R0, int
Call           R0, twice/1, R0
Return         R0
```

`Call` 通过 Machine 的函数 Module 注册表查找 `(twice, 1)`，把 `R0` 作为实参输入与返回槽，创建显式 Frame 和局部槽位 `L0`，然后切换到 `FunctionCode` 的指令流。带具体类型的参数在 `Call` 前由 `Cast` 转换，因此失败会阻止后续实参求值。它不会解释函数解析节点，也不会在调用时编译函数。

### 4. 类型数组与方法实参

源码：

```cifa
int values[];
values.push_back(9);
return values[0];
```

字节码：

```text
LoadNil        R0
GetTable       R0, values : int[], declaration

MethodCall     values.push_back, R0, 1
LoadK          R0, K0 (9)

LoadK          R1, K1 (0)
GetTable       R0, values, R1
Return         R0
```

声明中的 `LoadNil` 表示未指定数组长度，`GetTable(... declaration)` 将它规范化为长度 0 的 `ObjectVector`，并记录元素类型 `int`。参数 `9` 先写入连续输入窗口，`MethodCall` 再按照数组元素类型转换并追加；索引扩容或方法重入不会保存数组元素的裸指针。数组字面量使用 `NewTable`，局部索引快路使用 `GetTableLocal`。

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
- `unit_test\cifa_unit_test.cpp`：每个共通测试项都会在 `Cifa` 与 `CifaBytecode` 上执行，两个后端都通过才算通过。

`common_backend_conformance_test` 用一份模板化场景分别执行 `Cifa` 与 `CifaBytecode`。控制流和递归、数组/map、跨脚本函数和 struct、宿主回调、嵌套脚本与 include 必须在两个后端都通过，该测试才通过。新增共通行为时应优先加入这组共享场景，而不是复制一份 Cifa 测试和一份字节码测试。

只保留不能由另一后端表达的专有测试：直接求值器的源码状态与直接运算符回调测试，以及字节码的编译后重复运行、标签入口和独立 Module 生命周期测试。

持久字节码函数和 struct 在下一编译单元中通过明确的签名目录投影参与静态检查，运行期不回读 Cifa 的直接求值状态。

## PI 性能

基准脚本为 `cifa/calc-pi.c`，计算并返回 500 位 PI 字符串。`build/pi_benchmark.cpp` 关闭脚本输出，先预热 1 次，再记录 6 次样本；每轮都核对直接求值与字节码结果完全相同。它会在同一进程依次测量未优化与开启优化的字节码，避免将不同构建或机器负载下的数字误作优化收益。

- `AST source to result`：每轮新建 `Cifa` 后调用 `run_file`，包含读取、解析、静态检查和直接执行。
- `Source to CifaBytecode`：每轮新建 `CifaBytecode` 后调用 `compile_file`，包含读取、解析、静态检查和字节码翻译。
- `CifaBytecode execute`：在同一份已编译模块上调用 `run`，表示可重复执行时的运行成本；同时报告关闭和开启优化的版本。

使用 `build\pi_benchmark.bat` 构建并执行 Release 基准。输出包含每项的中位数、均值及全部样本，下面记录以该脚本的实际输出为准。

2026-09-12 在 Windows x64、Visual Studio 2026 18.10.0、MSVC Release `/O2 /DNDEBUG` 下的同进程 A/B 结果为：

| 测量项 | 中位数 (ms) | 均值 (ms) | 六次样本 (ms) |
| --- | ---: | ---: | --- |
| AST source to result | 383.565 | 384.044 | 382.417, 382.496, 382.994, 383.565, 384.948, 387.846 |
| Source to CifaBytecode | 1.2502 | 1.21590 | 1.0975, 1.1327, 1.1706, 1.2502, 1.2690, 1.3754 |
| CifaBytecode execute | 355.710 | 356.112 | 354.545, 355.495, 355.509, 355.710, 357.223, 358.189 |
| Source to optimized CifaBytecode | 1.2559 | 1.21792 | 1.0952, 1.1045, 1.1918, 1.2559, 1.2925, 1.3676 |
| Optimized CifaBytecode execute | 313.109 | 313.021 | 311.647, 312.432, 313.068, 313.109, 313.598, 314.270 |

当前 PI 负载的优化执行中位数相对未优化版本快约 $11.98\%$。该收益来自内联标量、动态整数/数学路径，以及局部一维数组的读取、写入和方法调用避免名称查找与临时索引向量。优化编译中位数为 $1.2559\,\mathrm{ms}$，与未优化版本相近；字节码编译成本单独计量，未计入重复执行时间。每次基准只在同一进程内比较未优化与优化结果，跨次运行的绝对时间会受机器负载影响。

`build/calc_pi.lua` 是与 `cifa/calc-pi.c` 调用图逐层对应的 Lua 5.4 对照脚本：除 `big_is_zero`、`big_add`、`big_sub`、`big_mul_int`、`big_div_int`、`calc_arctan` 与 `format_pi` 外，还显式包装 `size`、`floor`、`fmod`、`to_string`、`push_back` 与 `pop_back`。两端都预热 1 次；Lua 在单个进程中对每个样本重复 32 次再取单次均值，以避免 Windows `os.clock()` 的毫秒量化，并记录 6 个样本。两端均输出结果长度和 FNV-1a32；只有长度均为 $502$ 且校验值均为 `1d4b4c2f` 时才可比较性能。

2026-09-12 使用用户级 Lua 5.4.5，Lua 执行中位数为 $9.4375\,\mathrm{ms}$（样本为 $9.3750,9.3750,9.4375,9.4375,9.4375,9.4688\,\mathrm{ms}$）；同轮优化 CifaBytecode 的中位数为 $327.695\,\mathrm{ms}$，约为 Lua 的 $34.7\times$。Lua 数字采用双精度，但本基准每个 base $10^4$ 块及所有中间整数均低于 $2^{53}$，所以仍保持整数精确性。Lua 的原生 `0` 为真而 Cifa 的数值 `0` 为假，因此 Lua 脚本必须显式写作 `big_is_zero(term) == 1`，不能直接把返回值放入条件；否则会在第一次循环提前退出并产生错误的 PI。该对照只表示这份算法和运行时配置下的执行成本，不包含 Lua 进程启动和 Cifa 源码编译时间。

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
