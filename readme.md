# Cifa

## 简介

一个简易类C语法脚本。语法是C的子集，有少量C++风格的扩展。

用于嵌入一些只需要简单计算，且不想引入较复杂的外部库的C++程序。

例如某些情况下，需要在外部配置文件中执行一个简单的C++过程，且希望程序内部的代码可以不修改直接放到配置文件。

项目名就是“词法”。实际上本垃圾一开始理解错了，以为词法分析要生成语法树，就这样吧。

执行模式为直接对语法树求值，并处理分支和循环。

千万不要用它完成过于复杂的任务，正如描述所说，这只是一个非常简单的语法解析器和执行器。如果你想做更复杂的事，请选用Python或者Lua。如果你喜欢C风格的语法，请选用AngelScript或者ChaiScript，但是这两个库都不小，而且在很多Linux发行版上没有预编译包可用。

有大佬做的js版：<https://github.com/whyb/cifa.js>，线上演示为：<https://whyb.github.io/cifa.js/playground/web>.

ps：目前经实测，Cifa已经能在一定程度上替换Lua。

Cifa曾经单独开发，后来为方便改成了mlcc的一部分，目前因为AI的发展，可以迅速为其添加功能，逐渐可以实用化，因此再次转为独立的项目。

独立编译字节码后端的用法及高性能宿主函数接口见 [cifabytecode.md](cifabytecode.md)。优化内容及前后实测时间见 [vm-optimization.md](vm-optimization.md)。

## 使用方法

### 基本用法

在自己的工程中加入Cifa.h和Cifa.cpp即可。例程如下：

```c++
#include "Cifa.h"
#include <fstream>
#include <iostream>

using namespace cifa;

int main()
{
    Cifa c1;
    std::ifstream ifs;
    ifs.open("1.c");
    std::string str;
    getline(ifs, str, '\0');
    auto o = c1.run_script(str);
    if(o.hasValue() && o.isNumber() && o.isType<double>())
    {
        std::cout << "Cifa value is: " << o.ref<double>() << "\n";
    }
}
```

`register_function`、`register_parameter`、`register_vector` 和 `register_user_data` 在注册成功时返回 `true`。变量、函数及用户数据的名称遵循相同规则：首字符必须是英文字母、下划线 `_` 或 UTF-8 字符；后续字符还可使用数字；不能包含空白或运算符，且不能使用关键字、类型名或替代运算符词。注册失败时返回 `false`，并可通过 `get_runtime_error()` 获取原因。

可通过 `Cifa::is_valid_key(key)` 检查名称是否符合此规则。`Cifa::revise_key(key)` 会把不合规字符替换为 `_`；空名称会变为 `_`，关键字等保留名称会追加 `_`，使返回值始终是可用名称，例如 `1bad-key` 会修订为 `_bad_key`。

其中1.c文件即为脚本内容，一个例子为：

```c++
int sum = 1;
for (int i = 1; i <= 10; i++)
{
    for (int j = 1; j <= 10; j++)
    {
        int x = 0;
        if ((i + j) % 2 == 0)
        {
            x = -1;
        }
        else
            x = 1;
        sum += (i * j) * x;
    }
}
return sum;
```

计算的结果为-24。

若脚本只有一个表达式，则结果就是表达式的求值。若脚本包含多行，则需用return来指定返回值，否则返回值是`<empty>`（强行当成数值则是NaN）。

数值字面量支持十进制整数和浮点数（包括科学计数法），以及 C 风格的整数进制：十六进制 `0xFF`、二进制 `0b1010`、八进制 `077`。非十进制字面量只用于整数，不能包含小数点或指数。

### 运行规则

1. **`Cifa` 解析后直接求值**  
    `run_script(source)` 与 `run_file(filename)` 都在当前 `Cifa` 实例中完成解析、静态检查和直接求值；不生成可保存、可传递的中间脚本对象。

2. **执行会更新实例全局状态**  
    顶层函数和 struct 会在脚本执行时注册到当前 `Cifa` 实例。脚本最外层执行的变量创建和赋值直接作用于实例全局变量表。这三张全局表会跨后续脚本和文件执行保留；同名同参数个数的脚本函数由后执行的定义覆盖。

4. **只有大括号和函数调用产生局部变量层**  
    `{}` 代码块进入时压入局部变量层，离开时弹出；函数参数和函数体变量也属于函数调用的局部层。局部变量不会在代码块、函数或本次执行结束后保留。变量查找从最内层局部层向外进行，最后查找实例全局变量表。

4. **函数和 struct 只能在全局空间定义**  
    脚本函数和 struct 只允许出现在脚本最外层。它们若出现在 `if`、循环、函数体或其他任意大括号内部，会产生静态错误。

5. **嵌套脚本从新的全局执行开始**  
    在宿主回调或脚本内置 `run_string` / `run_file` 中再次执行脚本时，子脚本共享同一 `Cifa` 实例的全局变量、全局函数和全局 struct，但看不到外层代码块或函数的局部变量。子脚本产生的全局状态会保留。

6. **`return` 和 `exit()` 按当前执行上下文处理**  
    函数中的 `return` 只返回当前函数；脚本顶层 `return` 产生本次执行结果；子脚本的 `return` 只返回到调用子脚本的位置。`exit()` 结束当前脚本执行，子脚本的 `exit()` 不会结束外层脚本。子脚本的静态错误和运行时错误仍会向外传播。

7. **文件读取使用统一实现**  
    顶层脚本文件和递归 include 文件最终都通过同一个内部 `read_text_file()` 读取；include 预处理器只额外负责候选路径搜索、相对目录处理和重复包含检测。

### 脚本函数与重载

脚本可以直接定义函数，并可按**参数个数**使用同名重载：

```c++
label() { return "empty"; }
label(value) { return "one"; }
label(left, right) { return "many"; }

return label(1, 2);
```

调用时必须存在参数个数完全相同的版本；例如只定义了 `label(value)` 时，调用 `label()` 或 `label(1, 2)` 会报告可用的参数个数。参数类型不参与重载选择；同名且参数个数相同的定义采用**后定义覆盖前定义**的规则。

脚本函数名也不能与宿主程序已经注册的函数同名，例如内置 `sqrt` 或通过 `register_function` 注册的名称；这种冲突会产生静态错误。

#### 全局函数表

直接执行脚本时，定义会注册到当前 `Cifa` 实例的全局脚本函数表，因此后续独立脚本也可以调用：

```c++
 Cifa c;
auto first = c.run_script("add_one(value) { return value + 1; } return add_one(41);");
auto second = c.run_script("return add_one(9);"); // 10
```

脚本函数和 struct 只允许在脚本最外层定义；任何大括号内部的函数或 struct 定义都会产生静态错误。同名、同参数个数的全局函数采用后执行的定义覆盖旧定义；同一脚本中则以源码靠后的定义为准。

嵌套执行的脚本仍从新的全局空间开始：它共享 `Cifa` 的全局变量、全局函数和全局 struct，但不会读取外层代码块或函数的局部变量。子脚本执行后产生的全局定义会保留；子脚本自己的 `exit()` 只结束子脚本，不会结束外层脚本。

#### 函数变量作用域

脚本函数可以访问 `Cifa` 实例的全局变量。函数对全局变量的修改会保留到函数返回之后，并可被同一实例后续执行的脚本读取：

```c++
b = 304;

add_one_to_b()
{
    b = b + 1;
    return b;
}

return add_one_to_b();    // 305，同时顶层 b 也变为 305
```

函数参数和函数体局部变量位于更内层的作用域，可以遮蔽同名顶层变量，但不会写回顶层：

```c++
b = 10;

add_one(b)
{
    b = b + 1;
    return b;
}

result = add_one(20);    // result 为 21，顶层 b 仍为 10
```

函数采用词法作用域规则，只访问脚本顶层变量、函数参数和函数自身的局部变量，不访问调用位置所在代码块的局部变量。

脚本顶层变量绑定在 `Cifa` 实例上，并跨 `run_script` / `run_file` 保留。由 `register_parameter`、`register_vector` 等接口注册的变量与脚本定义的全局变量使用同一张表，可以互相读取和修改：

```c++
Cifa c;
c.register_parameter("value", 10);
c.run_script("value = value + 2; script_value = 30;");
auto result = c.run_script("return value * 100 + script_value;");    // 1230
```

变量作用域栈不包含全局变量层，只管理 `{}` 代码块和函数参数/局部变量的 RAII 生命周期。变量查找先从最内层局部作用域向外进行，最后查找实例全局变量表；根级新变量直接写入全局表。

宿主函数回调需要调用另一段脚本时，可以再次调用 `run_script` 或 `run_file`。每次调用都会在当前 `Cifa` 实例的执行上下文栈上压入一个新上下文：

```c++
Cifa c;
c.register_function("run_child", [&c](ObjectVector&) -> Object
    {
        return c.run_file("child.cifa");
    });

c.run_script("shared = 10; run_child(); return shared;");
```

嵌套脚本从新的全局执行空间开始，共享同一实例的全局变量、全局脚本函数和全局 struct，但不会看到调用者代码块或函数的局部变量。嵌套脚本自己的 `return` 只返回到宿主函数调用处，`exit()` 只结束嵌套脚本；两者都不会终止外层脚本。子脚本的静态错误或运行时错误仍会向外传播，使本次外层求值停止，并保留子脚本的源码位置用于错误报告。

脚本中也可以直接使用内置函数 `run_string(script)` 和 `run_file(filename)` 执行嵌套脚本。两者各接收一个字符串参数，返回子脚本的执行结果；`run_file` 支持 `#include`，并以被执行文件的目录解析相对 include：

```c
total = 1;
run_string("total += 2; return total;");
run_file("scripts/child.cifa");
return total;
```

它们与宿主回调中再次调用 `run_script` / `run_file` 具有相同的独立 `return` / `exit` 语义。执行上下文通过压栈和出栈管理，不会影响外层源码映射或控制状态。

#### 函数错误检查

- 函数名是否存在、是否与宿主函数重名，以及新函数定义时可确定的未初始化变量，属于静态检查；同名同参数数的脚本函数定义按后定义覆盖，不视为错误。
- 脚本函数本次调用没有实际返回值时产生特殊值 `NoValue`，不会因此产生静态错误。后续需要具体数值、字符串等类型时，运行时报告 `function '函数名' has no return value`。
- 脚本函数调用时没有匹配参数个数的重载，属于运行时错误。
- 宿主函数参数数量、参数值转换、函数实现主动报告的错误，属于运行时错误。
- 通过 `ObjectVector&` 注册的可变参数宿主函数没有固定签名元数据，其参数规则由函数实现自行检查。

### include 指令

Cifa 支持在脚本文件中使用 `#include` 引入其他脚本文件。include 在词法分析前预处理展开，语法形式支持双引号和尖括号两种写法：

```c++
#include "lib_math.cifa"
#include <lib_greet.cifa>

return square(3) + cube(2);
```

被包含文件中的变量和脚本函数会进入同一次脚本执行流程。例如 `lib_math.cifa` 可以定义：

```c++
square(x) { return x * x; }
cube(x) { return x * x * x; }
```

然后主脚本中即可直接调用 `square(3)`、`cube(2)`。

include 文件路径相对于当前脚本所在目录解析，也可以包含子目录：

```c++
#include "subdir/subdir_lib.cifa"
```

从文件运行脚本时，推荐使用 `run_file`，这样 include 的相对路径会以该脚本文件所在目录为基准：

```c++
Cifa c;
auto o = c.run_file("scripts/main.cifa");
```

如果脚本内容来自字符串，但仍希望指定 include 的搜索目录，可以先调用 `set_include_dirs`。搜索目录可以设置多个，会按顺序查找：

```c++
Cifa c;
c.set_include_dirs({ "scripts", "libs" });
std::string script = R"(
#include "lib_math.cifa"
return square(3) + cube(2);
)";
auto o = c.run_script(script);
```

需要从宿主程序向脚本提供变量时，使用 `register_parameter` 注册到实例全局变量表：

```c++
Cifa c;
c.register_parameter("base", 100);
auto o = c.run_file("scripts/main.cifa");
auto o2 = c.run_script(script);
```

几点限制和行为说明：

- `run_script(script)` 默认先按当前目录 `.` 解析 include，然后按 `set_include_dirs` 设置的目录继续查找。
- `run_file(filename)` 会额外将脚本文件所在目录作为当前搜索路径，因此文件内相对 include 会以该文件所在目录为基准。
- include 中写绝对路径时，会直接使用该路径，不会再拼接当前目录或搜索目录。
- 重复 include 或循环 include 会被跳过，避免无限递归展开。
- include 失败会产生语法错误，例如 `#include: cannot open file 'missing.cifa'`。
- include 指令必须写在行首或只带前导空白；其他位置不会作为 include 处理。

### 宿主程序添加自定义函数

自定义函数必须可以转化为`std::function<cifa::Object(cifa::ObjectVector&)>`，其中`cifa::ObjectVector`即`std::vector<cifa::Object>`。

#### 方式一

以下自定义3个数学函数（省略了检测越界）：
```c++
using namespace cifa;

Object sin1(ObjectVector& d) { return sin(d[0]); }
Object cos1(ObjectVector& d) { return cos(d[0]); }
Object pow1(ObjectVector& d) { return pow(d[0].value, d[1].value); }

int main()
{
    Cifa c1;
    c1.register_function("sin", sin1);
    c1.register_function("cos", cos1);
    c1.register_function("pow", pow1);
    //....
}
```
这里函数原型写成了xxx1的形式，只是为了避免与cmath中的数学函数同名，造成一些麻烦。

#### 方式二

其实更推荐lambda表达式的形式，例如将上面正弦函数的注册修改为：

```c++
    c1.register_function("sin", [](ObjectVector& d) { return sin(d[0]); });
```
这样也不必再定义sin1这个函数。

如果宿主函数本身就是普通 C++ 函数指针，也可以直接注册，Cifa 会通过模板自动判断参数个数并完成常见类型转换。无重载的函数不需要手动写类型：

```c++
double square(double x) { return x * x; }
double add(double a, double b) { return a + b; }

c1.register_function("square", square);
c1.register_function("add", add);
```

对于有重载的标准库函数，通常需要用 `static_cast` 指明具体签名：

```c++
c1.register_function("sin", static_cast<double(*)(double)>(&std::sin));
c1.register_function("pow", static_cast<double(*)(double, double)>(&std::pow));
```

函数名仍需显式传入，因为 C++ 函数指针本身不携带源码中的名字。

此时再运行如下脚本：
```c++
auto pi = 3.1415927;
print(sin(pi / 6));
print(cos(pi / 6));
print(pow(2, 10));
```
输出应是：
```
0.5
0.866025
1024
```
需注意语言已经内置了一些函数，如下面的表格所示。如果想覆盖掉内置函数，可以直接注册同名函数即可。

### 脚本中的自定义函数

例如脚本为：

```c++
myfun(i)
{
    return i*i*i+i*i+i+1;
}

print(myfun(3));
```
可以得到输出为40。

函数返回类型前缀可写也可省略，例如 `void notify() { println("done"); }`。注册类型前缀会在返回时执行类型转换；省略类型或使用 `auto` 时保留返回值的真实类型。`void` 保持兼容行为，不强制函数无返回值。

脚本函数当前按参数个数重载，不按参数类型重载，也不进行完整的静态类型检查。参数和返回值仍使用动态 `Object`，不兼容的数值/字符串转换会在运行时报告错误。

脚本函数的结果由本次实际执行决定：空函数、执行到末尾没有返回、分支未返回，以及 `return;` 都会产生特殊值 `NoValue`，不会把函数体最后一条语句的计算结果当成返回值。

`NoValue` 可以被忽略、赋给变量、放入数组，或作为实参和返回值继续传递，这些操作本身不报错。实际转换为数值、字符串等类型，或参与需要具体值的运算时才会产生运行时错误。例如 `value = notify();` 可以保存无值结果，随后 `value + 1` 会报告 `function 'notify' has no return value`。脚本可用 `type(value)` 识别它，宿主可检查 `getSpecialType() == "NoValue"`。

### 预定义变量

通过预定义变量可以模拟一些外置函数的效果。下面这个例子中，将pi预先定义好，并将degree视作一个C++送到Cifa的参数:
```c++
    c1.register_parameter("degree", 30);
    c1.register_parameter("pi", 3.14159265358979323846);
```
脚本为：
```c++
print(sin(degree*pi/180));
```
输出应为0.5。

### 逻辑运算与短路求值

逻辑与 `&&` 和逻辑或 `||` 按 C/C++ 风格从左到右求值，并使用短路规则：

- `false && expression` 不会执行 `expression`，结果为 `0`。
- `true || expression` 不会执行 `expression`，结果为 `1`。
- 只有左侧无法决定最终结果时，才会计算右侧表达式。

因此可以安全地把有副作用或可能出错的操作放在右侧：

```c++
int value = 0;
0 && value++;      // value 仍为 0
1 || value++;      // value 仍为 0

int ok = value != 0 && 10 / value > 1;
```

复杂表达式仍遵循运算符优先级和括号结构。例如 `a && b || c && d` 等价于 `(a && b) || (c && d)`；每一个 `&&` 或 `||` 节点都会独立应用短路规则。逻辑运算结果使用 Cifa 的数值真值 `0` 或 `1`。

### 变量的可见范围

一对大括号 `{}` 内声明的变量只在该代码块及其内层代码块中可见。变量查找从最内层作用域向外进行。

脚本最外层定义的变量进入 `Cifa` 实例的全局变量表。脚本函数可以访问和修改这一层；函数参数和函数局部变量可以遮蔽同名全局变量。函数不会读取调用者代码块的局部变量，因此不会形成动态作用域。

全局变量、全局脚本函数和全局 struct 表都绑定在 `Cifa` 实例上，可以跨脚本执行保留。变量作用域栈只保存大括号代码块和函数调用产生的局部层，并在离开对应范围时自动弹出；未被大括号包裹的脚本最外层变量直接进入全局变量表。

### 用户的数据类型

Cifa 的 `Object` 使用 `std::any` 保存实际值，可以容纳宿主自定义类型。普通整数保存为 `std::int64_t`，浮点数保存为 `double`，另外内置支持 `bool` 和 `std::string`。变量的静态或动态绑定规则见下文“静态与动态类型”。

如果用户希望使用自己的类型，需要增加一些功能函数和对应的运算符重载。

例如，增加以下几个函数支持OpenCV中cv::Mat相关的一些功能：

```c++
    c.register_function("imread", [](cifa::ObjectVector& v) -> cifa::Object
        {
            int flag = -1;
            if (v.size() >= 2)
            {
                flag = int(v[1]);
            }
            return cv::Mat(cv::imread(v[0].toString(), flag));
        });
    c.register_function("imshow", [](cifa::ObjectVector& v) -> cifa::Object
        {
            cv::imshow(v[0].toString(), v[1].to<cv::Mat>());
            return cifa::Object();
        });
    c.register_function("imwrite", [](cifa::ObjectVector& v) -> cifa::Object
        {
            cv::imwrite(v[0].toString(), v[1].to<cv::Mat>());
            return cifa::Object();
        });
```
除此之外，也可以支持用户自定义某些运算符的重载，但是需注意应进行类型检查。下面以增加加号和减号的重载为例：
```c++
    c.user_add.push_back([](const cifa::Object& l, const cifa::Object& r) -> cifa::Object
        {
            if (l.isType<cv::Mat>() && r.isType<cv::Mat>())
            {
                return cv::Mat(l.to<cv::Mat>() + r.to<cv::Mat>());
            }
            return cifa::Object();
        });
    c.user_sub.push_back([](const cifa::Object& l, const cifa::Object& r) -> cifa::Object
        {
            if (l.isType<cv::Mat>() && r.isType<cv::Mat>())
            {
                return cv::Mat(l.to<cv::Mat>() - r.to<cv::Mat>());
            }
            return cifa::Object();
        });
```

因为变量作用域的关系，用户自定义类型会按照RAII的原则进行管理，无需手动释放资源，即没有必要进行垃圾收集。但是用户原则上不应使用非RAII的类型。

### 数组

#### 空数组初始化

有两种方式创建空数组：

```c++
int a[];        // 类型声明方式
b = {};         // 赋值方式
```

两种写法均会产生一个长度为 0 的数组，之后可以用 `push_back` 等方法添加元素。

#### 数组越界行为

- **写入越界**：自动扩展数组大小，中间元素为空值。
- **读取越界**：同样自动扩展，返回的空值在后续数值运算中可能产生运行时错误。

#### 数组字面量

使用 `{}` 花括号构造数组，元素之间用逗号分隔。元素类型可以混合（数字、字符串等）：

```c++
arr = {1, 2, 3, 4, 5};
mixed = {1, "hello", 3.14, "world"};
```

#### 数组下标访问

使用整数下标访问元素，下标从 0 开始：

```c++
arr = {10, 20, 30};
int x = arr[0];    // x = 10
arr[1] = 99;       // 修改元素

int i = 2;
double v = arr[i]; // 变量作为下标
```

#### 多维数组

数组元素本身也可以是数组（嵌套）：

```c++
grid = { {1, 2}, {3, 4} };
int v = grid[1][0];    // v = 3
```

#### 数组大小

使用内置函数 `size()` 获取数组元素个数：

```c++
arr = {1, 2, 3, 4, 5};
int n = size(arr);    // n = 5
```

#### 范围 for 循环

数组支持类 C++ 的范围循环：

```c++
arr = {1, 2, 3};
int sum = 0;
for (int value : arr) {
    sum += value;
}

for (auto value : arr) {
    println(value);
}
```

首版范围循环仅支持数组。循环变量是每个数组元素的**值副本**，在循环体中修改它不会回写原数组；暂不支持 `auto&`、引用遍历、map 直接遍历或结构化绑定。范围表达式只在进入循环时求值一次，随后按当时的数组元素副本依次执行。

#### 受限 goto

脚本支持标签和 `goto`，可用于同一代码块内的跳转，或从嵌套代码块跳到其外层代码块的标签：

```c++
int value = 0;
again:
value += 1;
if (value < 3) goto again;
return value; // 3
```

标签写作 `label:`，跳转写作 `goto label;`。为保持作用域与控制流可预测，目标标签必须位于当前代码块或其祖先代码块中：不能跳入嵌套块或兄弟块，也不能跨函数或跨嵌套脚本跳转。标签名在同一脚本/函数中必须唯一。

静态检查会报告 `duplicate label '...'`、`goto target '...' is not defined` 或 `goto '...' jumps into a nested or sibling block`。运行时不限制跳转次数。

#### 从宿主注册向量

宿主程序可以将 `std::vector<double>` 注册为脚本内的数组变量：

```c++
c1.register_vector("v", std::vector<double>{1.2, 1.45, 77.3});
```

脚本中即可 `v[0]`、`v[1]` 访问。

### 字符串键映射（Map）

变量可通过字符串下标使用，此时它会变成一个 `string → Object` 的映射。

```c++
dict["name"] = "Alice";
dict["age"]  = 30;

string key = "name";
string n = dict[key];    // n = "Alice"
double a = dict["age"];  // a = 30
```

`size(dict)` 返回 map 中的元素个数。

访问不存在的 key 后使用该值会触发运行时错误。

#### Map 越界行为

访问不存在的 key 会自动创建该 key 并赋空值（与 C++ `std::map::operator[]` 行为一致）。

### 数组和 Map 的内置方法

数组和 Map 支持通过 `.` 语法调用内置方法，对自身进行原地修改。

#### 数组方法

| 方法 | 说明 | 返回值 |
|------|------|--------|
| `arr.push_back(x)` | 在末尾追加元素 `x`（支持多个参数） | 新的数组大小 |
| `arr.pop_back()` | 移除最后一个元素 | 新的数组大小 |
| `arr.resize(n)` | 将数组大小调整为 `n` | 新的数组大小 |
| `arr.insert(i, x)` | 在下标 `i` 处插入元素 `x` | 新的数组大小 |
| `arr.erase(i)` | 删除下标 `i` 处的元素 | 新的数组大小 |
| `arr.clear()` | 清空所有元素 | 0 |
| `arr.contains(x)` | 检查数组中是否存在值 `x` | 1（存在）或 0（不存在） |

示例：

```c++
a = {};
a.push_back(10);
a.push_back(20);
a.push_back(30);
a.insert(1, 15);    // a = {10, 15, 20, 30}
a.erase(0);          // a = {15, 20, 30}
int has = a.contains(20);  // has = 1
a.clear();           // a = {}
```

#### Map 方法

| 方法 | 说明 | 返回值 |
|------|------|--------|
| `m.erase("key")` | 删除指定 key | 新的 map 大小 |
| `m.clear()` | 清空所有键值对 | 0 |
| `m.contains("key")` | 检查 key 是否存在 | 1（存在）或 0（不存在） |
| `m.keys()` | 返回所有 key 组成的数组 | 字符串数组 |

示例：

```c++
m["name"] = "Alice";
m["age"] = 30;
int has = m.contains("name");  // has = 1
k = m.keys();                  // k = {"age", "name"}（按字典序）
m.erase("age");
int n = size(m);               // n = 1
```

### 结构体（struct）

Cifa 支持 C 风格的结构体定义，用于将多个字段组合成一个命名类型。

#### 定义与声明

```c++
struct Point { int x; int y; };
Point p;
```

- `struct` 定义声明字段列表，字段类型名（如 `int`）会被忽略，仅字段名有效。
- 结构体名本身作为类型关键字使用，声明变量时自动将其初始化为含所有字段的对象。

#### 字段读写

```c++
struct Point { int x; int y; };
Point p;
p.x = 10;
p.y = 20;
return p.x + p.y;    // 30
```

#### 复合赋值

```c++
struct Counter { int n; };
Counter cnt;
cnt.n = 5;
cnt.n += 3;    // cnt.n == 8
```

#### 作为函数参数

结构体实例可以作为参数传入脚本定义的函数：

```c++
struct Rect { int w; int h; };
area(r) { return r.w * r.h; }
Rect r;
r.w = 4; r.h = 5;
return area(r);    // 20
```

> **注意**：传递时为值拷贝，函数内修改字段不影响外部变量。

#### 跨调用复用

`struct` 定义绑定在 `Cifa` 实例上，**只需定义一次**，后续对同一实例的 `run_script` 调用均可直接使用该类型，无需重复写定义：

```c++
Cifa c;
c.run_script("struct Point { int x; int y; };");   // 第一次：定义结构体

auto o = c.run_script(R"(
    Point p;
    p.x = 3; p.y = 7;
    return p.x + p.y;
)");    // 第二次：直接使用，结果为 10
```

#### 限制

- 不支持嵌套 struct 定义（字段类型只能是基本类型）。
- 不支持构造函数、成员函数、继承等 C++ OOP 特性。

### 字符串

字符串长度可用 `size()` 获取：

```c++
string s = "hello";
int n = size(s);    // n = 5
```

字符串拼接用 `+`：

```c++
string s = "hello" + " " + "world";
```

类型转换：
- `to_string(3.14)` → 将数值转为字符串
- `to_number("3.14")` → 将字符串转为数值

类型检查：
- `type(x)` → 返回变量或表达式的类型字符串。内置值返回 `empty`、`int`、`double`、`bool`、`string`、`array`、`map`；注册对象返回规范注册名，未注册对象返回 C++ 类型名。

### 内置函数汇总

| 函数 | 说明 |
|------|------|
| `print(...)` | 输出一个或多个值，不换行 |
| `println(...)` | 输出一个或多个值，最后换行 |
| `to_string(x)` | 将数值转为字符串 |
| `to_number(s)` | 将字符串转为数值 |
| `type(x)` | 返回变量或表达式的类型字符串，如 `empty`、`int`、`double`、`bool`、`string`、`array`、`map` |
| `size(x)` | 返回数组、map 或字符串的大小 |
| `pow(x, y)` | x 的 y 次方 |
| `max(a, b, ...)` | 多个数中的最大值 |
| `min(a, b, ...)` | 多个数中的最小值 |
| `random()` | `[0, 1)` 均匀随机数；`random(n)` 返回 `[0, n)`；`random(a, b)` 返回 `[a, b)` |
| `ifv(cond, a, b)` | 三元选择，等价于 `cond ? a : b` |
| `abs / sqrt / cbrt / round / trunc / nearbyint / rint / floor / ceil` | 常见数学函数 |
| `fmod / remainder / copysign / fdim / fmax / fmin` | 常见双参数数学函数 |
| `sin / cos / tan / asin / acos / atan / atan2` | 三角函数 |
| `sinh / cosh / tanh` | 双曲函数 |
| `exp / log / log2 / log10` | 指数对数函数 |
| `hypot(x, y)` | 计算直角边为 x、y 的斜边长度 |
| `erf / erfc / tgamma / lgamma` | 误差函数与 Gamma 相关函数 |
| `sprintf(fmt, ...)` | C printf 风格格式化，支持 `%s %d %f %g %x` 等；`%%` 输出字面 `%` |
| `format(fmt, ...)` | `{}` / `{N}` 占位符风格格式化；整数不带小数点，`{{` / `}}` 转义为字面括号 |

### 错误处理

Cifa 将错误分为两类：**语法错误**（静态检查阶段）和**运行时错误**（执行阶段）。

#### 语法错误

```c++
if (c.has_error())
{
    // 推荐：返回带源码行和位置指示的错误字符串
    std::string err = c.get_errors_str();
    std::cerr << err;

    // 或者直接打印到 stderr
    // c.print_errors();

    // 也可逐条访问（建议优先使用上面的字符串接口）
    for (auto& e : c.get_errors())
    {
        // e.filename, e.line, e.col, e.message
    }
}
```

错误输出示例：

**未初始化变量**（脚本：`int y = undef;`）：
```
Syntax Error: parameter undef is at right of = but not been initialized
  at line 2, col 9: int y = undef;
                            ^
```

**未定义函数**（脚本：`return foo(1, 2);`）：
```
Syntax Error: function foo is not defined
  at line 1, col 8: return foo(1, 2);
                           ^
```

**不可赋值的左值**（脚本：`123 = 5;`）：
```
Syntax Error: 123 cannot be assigned
  at line 1, col 1: 123 = 5;
                    ^
```

**括号不匹配**（脚本：`int x = (1 + 2));`）：
```
Syntax Error: unpaired right bracket )
  at line 1, col 16: int x = (1 + 2));
                                    ^
```

**else 无对应 if**（脚本：`int x = 1; else { x = 2; }`）：
```
Syntax Error: else has no if
  at line 2, col 1: else { x = 2; }
                    ^
```

**位置信息**：下表所有语法错误均记录了精确的行列位置，`get_errors_str()` / `print_errors()` 输出时均会附带原始源码行与 `^` 位置指示。

> 若某个错误节点未能记录位置（内部极少见情况），则仅输出 `at line 0, col 0`，不附原始行文本。

语法错误在 `run_script` 返回前会自动打印到 stderr（`output_error` 为 `true` 时，这是默认行为），也可手动调用 `print_errors()` 或 `get_errors_str()` 获取。

Cifa 不根据恒真条件判断死循环，`while(1)`、`while(true)`、`for(;;)` 等写法合法。循环是否终止由使用者负责；`while()` 空条件和循环结构错误仍会报告。

静态检查可检出的语法错误列表：

| 错误 | 说明 |
|------|------|
| unpaired right bracket | 右括号 `)` / `]` / `}` 无对应左括号 |
| unpaired left bracket | 左括号 `(` / `[` / `{` 无对应右括号 |
| parameter ... is at right of = but not been initialized | 使用了未经赋值的变量 |
| ... cannot be assigned | 赋值左侧是常量或字符串字面量 |
| function ... is not defined | 调用了未注册/未定义的函数 |
| function ... has no operands | 函数缺少参数列表 |
| operator ? has no : | 三元运算符 `?` 缺少 `:` 分支 |
| if/while has empty condition | `if()` 或 `while()` 条件为空 |
| if has no condition/statement | `if` 缺少条件或语句体 |
| else has no if | `else` 无对应 `if` |
| for loop condition is not right | `for` 循环条件格式不正确 |
| while/do while has no statement/condition | 循环缺少必要部分 |
| switch has no condition/statement | `switch` 缺少条件或语句体 |
| case has no condition / case missing : | `case` 格式不正确 |
| default missing : | `default` 后缺少冒号 |
| missing ; | 语句末尾缺少分号 |
| no parameters inside [] | 下标表达式为空（数组声明 `int a[];` 除外） |
| wrong parameters inside [] / () | 括号内参数格式不正确 |
| variable declaration not allowed in non-block body | 在无 `{}` 的分支/循环/case 体中定义或引入了新变量 |

#### 运行时错误

执行阶段的错误（如类型不兼容、访问不存在的 map 键等）：

- 运行时错误在**触发时立即自动打印**到 stderr（`output_error` 为 `true` 时，这是默认行为）。
- 解释器报告运行时错误后通过 `request_exit()` 设置当前脚本的退出标记，复用语句块和循环的检查结束执行。退出标记不向外层脚本传递；嵌套脚本的错误信息仍按原有规则向外传递。
- 保留首个运行时错误及其调用栈。错误前已经完成的全局变量修改和宿主副作用不会回滚，但错误后的脚本语句不再执行；本次执行结束后仍可启动新脚本。
- 若要关闭自动输出，可在 `run_script` 前调用 `c.set_output_error(false)`。
- `run_script`、`run_file` 和 `run` 的返回值会携带 `Error` 标记，可用于程序逻辑判断：

```c++
// 运行时错误发生时会自动打印到 stderr
// 若要关闭自动输出：c.set_output_error(false);

auto result = c.run_script(script);
if (result.getSpecialType() == "Error")
{
    // 运行时发生了错误（已自动打印到 stderr）
}
```

**位置信息**：自动输出的运行时错误包含完整调用栈，每个调用帧均附带原始源码行与 `^` 位置指示。

错误中断不使用异常，也不保证出错语句内的修改回滚。已进入的 C++ 宿主回调需要在转换后检查 `has_runtime_error()` 并自行返回，解释器不会强制跳出回调。

`Object::ref<T>()` 类型不匹配时记录错误，返回线程局部占位对象的引用。占位对象在每次失败时重置为 `T{}`，因此 `T` 需要可默认构造、可赋值；此引用不代表转换成功，调用方应优先用 `isType<T>()` 检查类型。

运行时错误输出示例（含调用栈）：

**类型转换失败**（脚本：`int bad; return sqrt(bad);`）：
```
Runtime Error: type conversion failed: variable 'bad' from <empty> to double
Call Stack (most recent call last):
  at func sqrt()
  at line 2, col 8: return sqrt(bad);
                           ^
```

Cifa 不设置运行时循环次数、`goto` 跳转次数或函数调用深度上限，也不提供相应的限制参数。脚本应自行保证终止；递归仍受宿主进程的栈空间和可用内存约束。

运行时错误列表：

| 错误 | 说明 |
|------|------|
| type conversion failed: variable '...' from ... to double | 将非数值类型（空值、字符串等）转换为 double 时失败 |
| type conversion failed: variable '...' from ... to string | 将非字符串类型转换为 string 时失败 |
| function ... is not defined | 调用了运行时未找到的函数 |
| ...() is not supported on arrays/maps | 对数组或 map 调用了不支持的内置方法 |
| ...() requires an array or map | 对非数组、非 map 的变量调用了内置方法 |

### 语法树解析方案

Cifa 没有使用 yacc/ANTLR 之类的生成器，也不是传统的递归下降解析器，而是用一个比较直接的“token 列表逐步归约”为语法树的方案。核心节点类型是 `CalUnit`，节点里保存类型、字符串、子节点、源码行列位置以及一些语句/类型标记。

整体流程如下：

1. **include 预处理**  
    `#include` 会在词法分析前展开。展开时会记录每一行来自哪个原始文件和原始行号，因此 include 文件内部的错误、以及 include 之后主文件中的错误，都可以映射回原文件位置。

2. **词法切分**  
    `split()` 按字符扫描源码，将数字、字符串、标识符、运算符、括号和分号切成 `CalUnit` 列表。此时还只是线性的 token 序列。括号前面的标识符会先标记成函数调用候选，类型名（如 `int`、`double`、`auto`）会被识别并在后续变量上留下 `with_type` 标记。

3. **括号归约**  
    `combine_all_cal()` 先处理 `{}`、`[]`、`()`。实现方式是用栈按源码顺序匹配括号，把括号中的 token 列表递归归约成一个 `Union` 节点，再挂回外层列表。圆括号会关联到前面的函数名或关键字，方括号会关联到前面的数组/map 访问对象。这里避免了大量括号时反复扫描整条 token 列表。

4. **声明和结构提取**  
    `combine_structs()` 会把全局 `struct` 定义及字段信息提取到当前编译状态。`combine_functions2()` 会把全局空间中形如 `foo(a, b) { ... }` 的函数定义提取到当前函数表，并从根归约结果中移除。两类定义若出现在任何大括号内部，会产生静态错误。直接执行脚本时，这些定义注册到 `Cifa` 实例的全局表。

5. **运算符归约**  
    `combine_ops()` 按 `ops` 表定义的优先级从高到低处理运算符。大多数二元运算符按从左到右归约；赋值和部分一元运算按右结合处理。前置正号/负号会在这里区分一元和二元场景。三元 `?:` 也作为特殊运算符组处理。

    未来可以考虑把这里改为 precedence climbing、Pratt parser 或 shunting-yard 这类一趟表达式解析方案，并继续生成相同形状的 `CalUnitType::Operator` 节点。不过当前解析管线是在平铺 token 列表上做后处理归约，旧方案不需要显式计算表达式范围；一趟表达式解析若只替换 `combine_ops()`，需要准确区分 `return` 后表达式、`for` 头、range-for 的 `:`、三元 `?:`、数组赋值左值等边界。这个改动更适合作为较大范围的语句/表达式解析重构，而不是局部性能补丁。

6. **语句和关键字归约**  
    `combine_semi()` 将分号前的表达式标记为语句。`combine_keys()` 处理 `if/else/for/while/do/switch/case/default` 等关键字，把它们需要的条件和语句体挂成子节点。花括号形式的 `if / else if / else` 支持任意长度的连续分支，归约为嵌套的 `if` 结构，执行时只会进入第一个条件成立的分支。

7. **静态检查**  
    `check_cal_unit()` 遍历归约后的语法树，检查括号、运算符参数个数、未初始化变量、未定义函数、缺失分号、无块语句中定义新变量、明显无限循环等问题。这里的检查不是完整 C/C++ 语义检查，而是为了在执行前拦住常见错误。

8. **直接解释执行**  
    通过 `eval_scoped()` 递归求值语法树，不生成字节码。分支、循环、函数调用、数组/map 方法和结构体字段访问都在求值阶段直接处理。运行时错误通过调用栈和源码映射输出位置。

这个方案的优点是代码量小、容易嵌入、扩展简单；缺点是语法边界不如标准解析器严格，错误恢复能力也比较有限。Cifa 的定位是运行简单脚本和配置逻辑，因此选择了这种更轻量的解析方式。

### 语法上的注意事项

- 未写类型的变量和形参是动态的，可以改变值的类型；写注册类型或 `auto` 的变量是静态的，后续赋值受绑定类型约束。赋值和传参不会继承源变量的静态约束。
- 普通整数统一保存为 `std::int64_t`，浮点数统一保存为 `double`，布尔值保存为 `bool`。`float` 是 `double` 的兼容别名，`char` 是 `int` 的兼容别名，不保留独立的 float/char 数值表示。
- `auto` 按右值真实类型绑定；`auto value;` 在首次有效赋值时推导，暂存 `NoValue` 不触发推导。未经初始化的变量在需要读取具体值时报告错误。
- 整数加减乘和左移按 64 位补码回绕，整数除零、除法溢出、非法移位数量和越界数值转换报告错误。整数之间的算术和比较不经过 double。

### 静态与动态类型

Cifa 区分**值的实际类型**和**变量的类型绑定**。每个值都有实际类型；动态变量也不例外。静态绑定表示变量后续赋值受类型约束，不表示采用 C++ 式的完整编译期类型检查，也不表示变量不可修改。类型转换和不兼容赋值主要在执行时检查。

| 声明方式 | 绑定规则 | 后续赋值 |
| --- | --- | --- |
| `value = 1;` | 不写类型，动态绑定 | 可以改为字符串、数组等其他类型 |
| `int value = 1;` | 显式类型，静态绑定 | 按声明类型转换；不兼容时报告运行时错误 |
| `auto value = 1;` | 从初始值推导为 `int`，静态绑定 | 与绑定为 `int` 的变量一样，不会随新值重新推导 |
| `auto value;` | 等待首次有效赋值 | 推导后固定；暂存 `NoValue` 不触发推导 |

```cpp
dynamic_value = 1;
dynamic_value = "text";

int fixed_value = 1;
fixed_value = 3.9;

auto inferred_value = 1;
inferred_value = 3.9;

copy = fixed_value;
copy = "independent";

auto delayed_value;
delayed_value = 2.5;
delayed_value = 3;
```

上述代码中，`fixed_value` 和 `inferred_value` 最终都是整数 `3`；`delayed_value` 绑定为 `double`，最终保存 `3.0`。浮点数向整数转换在范围合法时向零截断，不是四舍五入。将 `fixed_value` 赋为字符串则会报错。`copy` 不会继承右值变量的静态约束，是否绑定由目标变量自身的声明决定。`dynamic_value` 只是普通变量名，并非使用了 `dynamic` 关键字。

函数形参同样遵循声明规则：未写类型的形参是动态的，即使实参来自静态变量；有类型的形参在每次调用时转换并绑定；`auto` 形参在每次调用中根据该次实参推导。普通传参仍是值语义，不能把动态类型理解成引用传递。

```cpp
fixed_parameter(int value) { value = 3.9; return value; }
dynamic_parameter(value) { value = 3.9; return value; }
```

两者分别返回整数 `3` 和浮点数 `3.9`。函数返回类型与局部变量绑定不同：`int func(...)` 在返回时转换为 `int`；省略返回类型或写 `auto` 保留本次返回值的实际类型，不会将第一次调用的返回类型固定到后续调用。

`type(value)` 查询的是值的实际类型，不是静态或动态绑定状态。例如动态变量保存整数时，`type()` 仍返回 `int`。绑定也不改变作用域、生命周期或数组的值复制规则；显式元素类型数组另有元素转换约束。

### 注册类型与自定义运算

类型应在编译脚本前注册，同名重复注册被拒绝：

```cpp
cifa::Cifa interpreter;
interpreter.register_type<std::int64_t>("Index");
interpreter.register_type<MyObject>("MyObject");
interpreter.register_parameter("source", MyObject{});
interpreter.run_script("MyObject value = source; auto copy = source; Index index = 3.9;");
```

类型注册保存 C++ 类型身份和转换函数，不需要扩展类型枚举。数值注册自动归一化为 int64_t/double；自定义类型默认只允许同类型赋值。同一 C++ 类型的第一个注册名是 `type()` 的规范名称，因此 `Index` 值的规范名仍为 `int`。数组和 map 默认不占用类型关键字，保留原有同名变量语法。

`user_add`、`user_mod`、`user_equal` 等扩展列表仍可使用。回调返回空 `Object()` 表示未匹配并继续尝试下一个回调；返回任何有值对象（包括数字和 bool）表示成功。旧回调若使用数值作为未匹配标记，需要改为空对象。自定义对象与数字的混合运算可以进入回调，内置纯数值运算优先。脚本 `&&`、`||` 先将左操作数转换为 bool 并进行短路判断；未短路时才进入对应二元运算分派，因此不是无条件调用自定义逻辑回调。

`Object::subV()` 及其逗号结果缓存已移除。逗号表达式仍按顺序求值左右操作数，保持原有空结果语义，不保存两个操作数的结果副本。
- 函数调用时，a.func(c)等价于func(a, c)。但对于内置数组/map方法（push_back、erase等），仅能通过 `.` 语法调用，不能写成 `push_back(arr, x)` 的形式——这是因为内置方法需要直接修改原始变量，而普通函数调用传的是值的副本，无法修改原始对象。
- 自加算符不支持++++或----这种写法，请不要瞎折腾。
- 没有goto。

### 一个完整的用例

以下是使用Cifa计算一个数值的完整用例，包含错误检查和结果处理：

```c++
    cifa::Cifa cifa;
    std::string str1 = "a1 = 2;\na3=a1;\nreturn 5+4*9*(a1+3)/23;";
    auto c = cifa.run_script(str1);
    if (cifa.has_error())    //检查语法错误
    {
        //可以选择输出语法错误字符串（已包含源码行和错误位置指示）
        std::string err_str = cifa.get_errors_str();
        std::cerr << err_str;
        //也可以直接打印到 stderr
        //cifa.print_errors();
    }
    //无语法错误，判断结果是否是一个数值
    if (c.isNumber())
    {
        std::print("{}\n", c.toDouble());
    }
    //若需正常继续计算，需要排除nan和inf
    if (c.isEffectNumber())
    {
        //do something
    }
```


## 其他

### 已知问题

- 生成语法树时的检查不太严格，例如if和while后面的条件其实可以不写括号，但是最好要写全（此处若严格处理需要将括号多归约一层，略微影响效率）。
- 复杂表达式中的某些错误位置仍可能落在归约后的代表节点上，而不是用户直觉中的最细 token；include 展开前后的文件名和行号已通过源码映射处理。

### 有可能会加的

- 已实现：数组的值语义范围循环 `for (int value : arr)` / `for (auto value : arr)`。
- 高优先级：`const` 只读变量，避免脚本误改配置常量或宿主传入的固定参数。
- 高优先级：字符串处理函数或方法，例如 `trim`、`split`、`join`、`find`、`substr`、`replace`、`starts_with`、`ends_with`。
- 高优先级：`assert(condition, message)` 与 `error(message)`，让脚本能主动校验配置并输出清晰的运行时错误。
- 中优先级：容器辅助函数，例如 `empty(x)`、`range(begin, end)`、`clamp(x, low, high)`；可与范围循环配合使用。
- 中优先级：简单 `enum` 或命名常量组，用于模式、状态码等可读性需求；首版可仅映射为数值常量。
- 中优先级：函数默认参数，例如 `greet(name, prefix = "hello")`。
- 中优先级：变量的括号初始化。
- 后续考虑：脚本模块/命名空间隔离，避免 `#include` 展开后变量与函数污染同一全局作用域。
- 后续考虑：范围循环的引用语义 `auto&`、map 遍历与结构化绑定；当前刻意只支持数组值遍历。
- 后续考虑：JSON/INI 文本与数组/map 的转换，或由宿主程序按需要注册对应函数。
- 后续考虑：遍历最终语法树生成执行码，不再重复处理语法树。
