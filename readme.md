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

注意这种函数不能做类型检查。

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

### 变量的可见范围

一对大括号{}内定义的变量在该大括号内可见，外部不可见。函数参数在函数体内可见。与C++规则相同。

### 用户的数据类型

Cifa中Object的实现其实是std::any，故可以容纳任何类型。但目前数值相关的类型实际都是double，另内置了std::string的支持。

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

### 内置函数汇总

| 函数 | 说明 |
|------|------|
| `print(...)` | 输出一个或多个值，不换行 |
| `println(...)` | 输出一个或多个值，最后换行 |
| `to_string(x)` | 将数值转为字符串 |
| `to_number(s)` | 将字符串转为数值 |
| `size(x)` | 返回数组、map 或字符串的大小 |
| `pow(x, y)` | x 的 y 次方 |
| `max(a, b, ...)` | 多个数中的最大值 |
| `min(a, b, ...)` | 多个数中的最小值 |
| `random()` | `[0, 1)` 均匀随机数；`random(n)` 返回 `[0, n)`；`random(a, b)` 返回 `[a, b)` |
| `ifv(cond, a, b)` | 三元选择，等价于 `cond ? a : b` |
| `abs / sqrt / round / floor / ceil` | 常见数学函数 |
| `sin / cos / tan / asin / acos / atan` | 三角函数 |
| `sinh / cosh / tanh` | 双曲函数 |
| `exp / log / log10` | 指数对数函数 |

### 错误处理

Cifa 将错误分为两类：**语法错误**（静态检查阶段）和**运行时错误**（执行阶段）。

#### 语法错误

```c++
if (c.has_error())
{
    // 返回带源码行和位置指示的错误字符串
    std::string err = c.get_errors_str();
    std::cerr << err;

    // 或者直接打印到 stderr
    // c.print_errors();

    // 也可逐条访问
    for (auto& e : c.get_errors())
    {
        // e.line, e.col, e.message
    }
}
```

错误输出示例：
```
Syntax Error: parameter arr is at right of = but not been initialized
  at line 3, col 5:     arr[0] = 1;
                        ^
```

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
| while has constant true condition, may cause infinite loop | `while(1)` / `while(true)` 静态检测到潜在死循环 |
| for loop may cause infinite loop | `for(;;)` 等无终止条件 |
| for loop condition is not right | `for` 循环条件格式不正确 |
| while/do while has no statement/condition | 循环缺少必要部分 |
| switch has no condition/statement | `switch` 缺少条件或语句体 |
| case has no condition / case missing : | `case` 格式不正确 |
| default missing : | `default` 后缺少冒号 |
| missing ; | 语句末尾缺少分号 |
| no parameters inside [] | 下标表达式为空（数组声明 `int a[];` 除外） |
| wrong parameters inside [] / () | 括号内参数格式不正确 |

#### 运行时错误

执行阶段的错误（如类型不兼容、访问不存在的 map 键等），`run_script` 的返回值会携带错误标记：

```c++
auto result = c.run_script(script);
if (result.getSpecialType() == "Error")
{
    // 运行时错误，可打印调用栈
    c.print_runtime_error();

    // 或者获取错误字符串
    std::string err = c.get_runtime_error_str();
}
```

运行时错误输出示例（含调用栈）：
```
Runtime Error: type conversion failed: variable 'bad' from <empty> to double
Call Stack (most recent call last):
  at func sqrt()
  at line 3, col 12:     return sqrt(bad);
                                ^
```

运行时错误列表：

| 错误 | 说明 |
|------|------|
| type conversion failed: variable '...' from ... to double | 将非数值类型（空值、字符串等）转换为 double 时失败 |
| type conversion failed: variable '...' from ... to string | 将非字符串类型转换为 string 时失败 |
| max call depth exceeded (possible infinite recursion) | 函数递归调用过深，超过最大调用深度 |
| for/while/do-while loop exceeded max iterations | 循环次数超过最大限制 |
| function ... is not defined | 调用了运行时未找到的函数 |
| ...() is not supported on arrays/maps | 对数组或 map 调用了不支持的内置方法 |
| ...() requires an array or map | 对非数组、非 map 的变量调用了内置方法 |

### 语法上的注意事项

- Cifa的变量定义其实不需要指定类型，但是为了实现“简单C（C++）代码可以直接被Cifa运行”这一目的，auto、int、float、double等类型名会被忽略。
- 未经初始化即出现在赋值号右侧的变量值为空，即std::any的`<empty>`，相当于强制要求显式初始化。
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
- 报错位置有时不太准确。例如括号被归约后，在语法树上就不再存在，所以会相差一个或多个括号。要解决需要在归约时记录最终位置，比较复杂且意义不是很大，不再处理。

### 有可能会加的

- 变量的括号初始化。
- 遍历最终语法树可以生成执行码，不再处理。
