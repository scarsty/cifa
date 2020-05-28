# Cifa

一个简易类C语法脚本。语法是C的子集，有少量C++风格的扩展。

用于嵌入一些只需要简单计算，且不想引入较复杂的外部库的C++程序。例如某些情况下简单的C过程可以不修改放到外部配置文件。

一些注意事项

- auto, int, float等类型名会被忽略，若想无脑从C++复制可以使用auto
- 未经初始化即出现在赋值号右侧的变量值为nan，相当于强制要求变量显式初始化
- 函数调用时，a.func(c)等价于func(a, c)
- 若脚本只有一行，返回值为本行的计算结果，否则由return决定，实际是在原始脚本后面加一个分号
- 自加算符不支持++++或----这种写法，请不要瞎折腾
- 没有switch
- 没有do while
- 没有goto
- 没有?:
- 可以在程序而非脚本中自定义函数

一些已知问题

- 一切皆表达式，执行模式为直接对语法树求值。
- 类型很简单，如需自定义类型请自行扩展。
- 生成语法树时的检查不太严格，例如if和while后面条件其实可以不写括号，但是最好要写全（此处若严格处理需要将括号多归约一层，略微影响效率）。
- 报错一般是语法树生成错误，所以有时可能报错在差几个字节的地方。
- 为调试方便，在生成语法树时检测错误，实际可以在最后检查整个语法树。
- 报错位置有时不太准确。例如括号被归约后，在语法树上就不再存在，所以会相差一个或多个括号。要解决需要在语法归约时记录最终位置，比较复杂且意义不是很大，不再处理。
- 遍历最终语法树可以生成执行码，不再处理。
- 千万不要用它完成过于复杂的任务，正如描述所说，这只是一个非常简单的语法解析器和执行器。如果你想做更复杂的事，请选用Python或者Lua。如果你喜欢C风格的语法，请选用AngelScript或者ChaiScript，但是这两个库都不小，而且在很多Linux发行版上没有预编译包可用。

有可能要加的

- 变量的括号初始化。
- []算符和数组，目前可以产生语法树，还不能正确执行，而且很可能语法树也不正确。
- 若支持自定义函数，则需为每个函数都构造语法树，且必须类似C语言在使用之前声明或定义，暂时搁置。
