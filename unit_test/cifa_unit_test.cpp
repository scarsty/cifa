#include "../Cifa.h"
#include <iostream>
#include <numeric>

using namespace cifa;

bool register_function_test()
{
    Cifa c1;
    c1.register_function("sin", [](ObjectVector& d)
        {
            return sin(d[0]);
        });

    std::string script_code = R"(
    double PI = 3.141592653589793238462643383279;
    double return_val = 0;
    return_val += sin(0);
    return_val += sin(PI * 0.5);
    return_val += sin(PI);
    return return_val;
    )";

    auto o = c1.run_script(script_code);
    if (o.hasValue() && o.isNumber() && o.isType<double>())
    {
        return (std::fabs(o.ref<double>() - 1.0) <= std::numeric_limits<double>::epsilon());
    }
    else
    {
        return false;
    }
}

bool loop_math_test()
{
    Cifa c1;
    std::string script_code = R"(
    int i;
    double sum = 0.0, product = 1.0, division = 100.0, difference = 50.0;
    double total_result = 0.0;
    for (i = 1; i <= 5; i++) {
        sum += i;
    }
    while (i <= 6) {
        product *= i;
        i++;
    }
    do {
        difference -= i;
        i++;
    } while (difference > 0);
    division /= 5;
    total_result = sum + product + division + difference;
    return total_result;
    )";

    auto o = c1.run_script(script_code);
    if (o.hasValue() && o.isNumber() && o.isType<double>())
    {
        return (std::fabs(o.ref<double>() - 34) <= std::numeric_limits<double>::epsilon());
    }
    else
    {
        return false;
    }
}

bool loop_control_test()
{
    Cifa c;
    std::string script = R"(
        int sum = 0;
        for (int i = 0; i < 10; i++) {
            if (i % 2 == 0) continue; // 跳过偶数
            if (i > 7) break;         // 遇到 9 跳出
            sum += i;
        }
        return sum; // 1 + 3 + 5 + 7 = 16
    )";
    auto o = c.run_script(script);
    return o.toInt() == 16;
}

bool ternary_operator_test()
{    // 测试嵌套三目运算
    Cifa c;
    std::string script = R"(
        int a = 1, b = 0;
        return a > b ? (b > a ? 10 : 20) : 30;
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 20;
}

bool switch_case_test()
{    // Switch-Case 完备性测试
    Cifa c;
    std::string script = R"(
        int x = 2;
        int res = 0;
        switch(x) {
            case 1: res = 10; break;
            case 2: res = 20; // 故意不写break看看？
            case 3: res = 30; break;
            default: res = 40;
        }
        return res;
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 30;
}

bool recursion_test()
{    // 递归函数测试
    Cifa c;
    std::string script = R"(
        double factorial(double n) {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        }
        return factorial(5);
    )";
    auto o = c.run_script(script);
    return o.hasValue() && std::fabs(o.toDouble() - 120.0) < 1e-9;
}

bool string_operation_test()
{    // 字符串操作与拼接测试
    Cifa c;
    std::string script = R"(
        string s1 = "Hello ";
        string s2 = "World";
        return s1 + s2;
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.isType<std::string>() && o.toString() == "Hello World";
}

bool bitwise_operator_test()
{    // 位运算测试
    Cifa c;
    std::string script = R"(
        int a = 5;      // 0101
        int b = 3;      // 0011
        int res1 = a & b;  // 0001 (1)
        int res2 = a | b;  // 0111 (7)
        int res3 = a ^ b;  // 0110 (6)
        int res4 = a << 1; // 1010 (10)
        return res1 + res2 + res3 + res4; // 1 + 7 + 6 + 10 = 24
    )";
    auto o = c.run_script(script);
    return o.toInt() == 24;
}

bool scope_shadowing_test()
{    // 变量作用域遮蔽测试
    Cifa c;
    std::string script = R"(
        int x = 10;
        {
            int x = 20;
            if (x == 20) {
                int x = 30;
            }
        }
        return x;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 10;    // 外部作用域不应受内部干扰
}

bool complex_math_priority_test()
{    // 复杂算术优先级测试
    Cifa c;
    std::string script = R"(
        return 2 + 3 * 4 / (1 + 1) - 5 % 2; // 2 + 12 / 2 - 1 = 2 + 6 - 1 = 7
    )";
    auto o = c.run_script(script);
    return o.toInt() == 7;
}

bool array_access_test()
{    // 数组/集合模拟测试 (假设Cifa支持类似[]的操作)
    Cifa c;
    std::string script = R"(
        //int arr[3];
        arr[0] = 10;
        {arr[1] = 20;}
        arr[2] = arr[0] + arr[1];
        return arr[2];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 30;
}

bool array_literal_assignment_test()
{    // 数组字面量赋值测试
    Cifa c;
    std::string script = R"(
        array = {1,2,3,4,5};
        return array[0] + array[4];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 6;
}

bool size_of_array_test()
{    // 数组大小测试
    Cifa c;
    std::string script = R"(
        array = {1,2,3,4,5};
        return size(array);
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 5;
}

bool register_vector_test()
{
    std::vector<double> v = { 1.2, 1.45, 77.3 };
    Cifa c;
    c.register_vector("v", v);
    std::string script = R"(
        return v[0] + v[1] + v[2];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toDouble() == std::accumulate(v.begin(), v.end(), 0.0);
}

bool register_map_test()
{
    Cifa c;
    c.register_parameter("cfg", std::map<std::string, double>{ { "width", 640 }, { "height", 480 } });
    // m["key"] syntax
    {
        auto o = c.run_script(R"( return cfg["width"] * cfg["height"]; )");
        if (!o.hasValue() || o.toDouble() != 640 * 480)
        {
            return false;
        }
    }
    // m::key syntax
    {
        auto o = c.run_script(R"( return cfg::width + cfg::height; )");
        if (!o.hasValue() || o.toDouble() != 640 + 480)
        {
            return false;
        }
    }
    return true;
}

bool type_promotion_test()
{
    //不区分整数和浮点数，此测试不增加特别处理不可能通过
    Cifa c;
    std::string script = R"(
        int a = 5;
        int b = 2;
        double res = floor(a / b);       // 整数除法，结果可能是 2.0
        double res2 = a / 2.0;    // 提升为浮点，结果应该是 2.5
        return res + res2;        // 4.5
    )";
    auto o = c.run_script(script);
    return std::fabs(o.toDouble() - 4.5) < 1e-9;
}

bool empty_statement_test()
{
    Cifa c;
    std::string script = R"(
        int x = 10;;;  // 多重分号
        if (x > 5) {}
        else ;
        while(false){;}
        for(;false;);
        return x;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 10;
}

bool multi_dimensional_array_test()
{
    Cifa c;
    // 假设你的引擎支持这种嵌套方式
    std::string script = R"(
        grid = {{1, 2}, {3, 4}};
        return grid[1][0]; // 应该返回 3
    )";
    auto o = c.run_script(script);
    return o.toInt() == 3;
}

bool compound_assignment_test()
{
    Cifa c;
    std::string script = R"(
        int x = 10;
        x *= 2 + 3; // 应该是 10 * (2 + 3) = 50，而不是 10 * 2 + 3 = 23
        x %= 7;     // 50 % 7 = 1
        return x;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 1;
}

bool c_string_library_test()
{
    Cifa c1;

    // 1. 注册 strlen: 返回字符串长度
    c1.register_function("strlen", [](ObjectVector& d) -> Object
        {
            if (d.empty() || !d[0].isType<std::string>())
            {
                return 0;
            }
            return (double)d[0].toString().length();
        });

    // 2. 注册 strcmp: 比较字符串
    c1.register_function("strcmp", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2)
            {
                return 0;
            }
            int res = d[0].toString().compare(d[1].toString());
            // 标准化为 C 风格的 -1, 0, 1
            return (double)((res > 0) - (res < 0));
        });

    // 3. 注册 strcat: 拼接字符串
    c1.register_function("strcat", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2)
            {
                return "";
            }
            return d[0].toString() + d[1].toString();
        });

    // 4. 注册 strcpy: 模拟赋值
    c1.register_function("strcpy", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2)
            {
                return "";
            }
            return d[1];    // 将第二个参数赋值给第一个
        });

    std::string script_code = R"(
        string s1 = "Cifa";
        string s2 = "Language";
        // 测试 strlen
        int len = strlen(s1); // 4
        // 测试 strcmp
        int cmp_res = strcmp(s1, "Cifa"); // 0
        // 测试 strcat
        string combined = strcat(s1, s2); // "CifaLanguage"
        // 测试 strcpy 逻辑
        string target = "old";
        target = strcpy(target, "new");
        // 期望: 4 + 0 + 12 (combined长度) + 3 (new长度) = 19
        return len + cmp_res + strlen(combined) + strlen(target);
    )";

    auto o = c1.run_script(script_code);

    if (o.hasValue() && o.isNumber())
    {
        // 预期 4 + 0 + 12 + 3 = 19
        return o.toInt() == 19;
    }

    return false;
}

bool runtime_error_stack_test()
{    // 运行时错误触发测试（类型转换失败）
    Cifa c;
    std::string script = R"(
        string bad = "abc";
        return sqrt(bad);
    )";
    auto o = c.run_script(script);
    return o.getSpecialType() == "Error";
}

bool mixed_array_literal_test()
{    // 混合类型数组字面量：数字、字符串混存
    Cifa c;
    std::string script = R"(
        arr = {1, "hello", 3.14, "world"};
        int i = 2;
        double n = arr[0];
        string s = arr[1];
        double f = arr[i];
        string s2 = arr[3];
        println(s + " " + s2, " ", to_string(f)); // 输出 "hello world 3.14"
        return n + f;  // 1 + 3.14 = 4.14
    )";
    auto o = c.run_script(script);
    return o.hasValue() && std::fabs(o.toDouble() - 4.14) < 1e-9;
}

bool static_syntax_error_test()
{
    // 辅助 lambda：运行脚本并返回错误字符串（禁止 stderr 输出避免干扰测试输出）
    auto get_errors = [](const std::string& script) -> std::string
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script(script);
        return c.get_errors_str();
    };

    // 检查 get_errors() 返回的错误列表是否包含某关键词
    auto get_error_messages = [](const std::string& script) -> std::vector<Cifa::ErrorMessage>
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script(script);
        return c.get_errors();
    };

    auto expect_error = [&](const std::string& label, const std::string& script, const std::string& keyword) -> bool
    {
        std::string err = get_errors(script);
        std::cerr << "  [" << label << "]:\n";
        if (err.find("Syntax Error:") == std::string::npos || err.find(keyword) == std::string::npos)
        {
            std::cerr << "    FAIL: expected keyword \"" << keyword << "\" in error output\n";
            if (!err.empty()) { std::cerr << "    Got:\n"
                                          << err; }
            else { std::cerr << "    (no error produced)\n"; }
            return false;
        }
        // 确认错误字符串包含源码行指示 (^ caret)
        if (err.find("^") == std::string::npos)
        {
            std::cerr << "    FAIL: missing caret (^) in error output\n";
            return false;
        }
        std::cerr << err;
        return true;
    };

    auto expect_no_error = [&](const std::string& label, const std::string& script) -> bool
    {
        std::string err = get_errors(script);
        std::cerr << "  [" << label << "]: ";
        if (!err.empty())
        {
            std::cerr << "FAIL unexpected error:\n"
                      << err;
            return false;
        }
        std::cerr << "OK\n";
        return true;
    };

    bool ok = true;

    // ==== 应触发静态错误的场景 ====

    // 1. 赋值右侧使用未初始化变量
    ok &= expect_error("uninitialized var in assign",
        R"(int y = undef;)",
        "not been initialized");

    // 2. 赋值右侧使用未初始化变量（多行，验证行号定位）
    ok &= expect_error("uninitialized var multiline",
        "int x = 10;\nint y = undef;\n",
        "not been initialized");

    // 3. 调用未定义的函数
    ok &= expect_error("undefined function",
        R"(return foo(1, 2);)",
        "not defined");

    // 4. 括号不匹配（右括号多余）
    ok &= expect_error("unpaired right paren",
        R"(int x = (1 + 2));)",
        "unpaired");

    // 5. 括号不匹配（左括号多余）
    ok &= expect_error("unpaired left paren",
        R"(int x = ((1 + 2);)",
        "unpaired");

    // 6. 赋值给常量
    ok &= expect_error("assign to constant",
        R"(123 = 5;)",
        "cannot be assigned");

    // 7. 赋值给字符串字面量
    ok &= expect_error("assign to string literal",
        "\"hello\" = 5;",
        "cannot be assigned");

    // 8. 独立的 else（没有对应的 if）
    ok &= expect_error("else without if",
        R"(else { int x = 1; })",
        "else has no if");

    // 9. 三元运算符缺少 :
    ok &= expect_error("ternary missing colon",
        R"(int x = 1; int y = x ? 10;)",
        "no :");

    // 10. 错误定位验证：检查 ErrorMessage 的行列号
    {
        // 第2行第9列（"y = undef" 的 "undef"）
        auto errs = get_error_messages("int x = 10;\nint y = undef;\n");
        bool found_line2 = false;
        for (auto& e : errs)
        {
            if (e.line == 2 && e.message.find("undef") != std::string::npos)
            {
                found_line2 = true;
                break;
            }
        }
        if (!found_line2)
        {
            std::cerr << "  FAIL [error line number]: expected error at line 2 mentioning 'undef'\n";
            for (auto& e : errs)
            {
                std::cerr << "    Got line " << e.line << ", col " << e.col << ": " << e.message << "\n";
            }
            ok = false;
        }
    }

    // ==== 不应触发静态错误的场景 ====

    // 11. 正常脚本
    ok &= expect_no_error("valid script",
        R"(int x = 10; int y = x + 5; return y;)");

    // 12. 正常 for 循环
    ok &= expect_no_error("valid for loop",
        R"(int s = 0; for (int i = 0; i < 5; i++) { s += i; } return s;)");

    // 13. 正常函数调用
    ok &= expect_no_error("valid function call",
        R"(return abs(-5);)");

    // 14. 正常数组操作
    ok &= expect_no_error("valid array",
        R"(arr = {1,2,3}; return arr[0] + arr[2];)");

    // 15. 正常字符串 Map
    ok &= expect_no_error("valid map",
        R"(dict["k"] = 42; return dict["k"];)");

    // 16. 正常三元运算符
    ok &= expect_no_error("valid ternary",
        R"(int x = 1; return x ? 10 : 20;)");

    // 17. 正常自定义函数
    ok &= expect_no_error("valid user function",
        "myfun(i) { return i * i; }\nreturn myfun(5);");

    // 18. 正常嵌套块
    ok &= expect_no_error("valid nested block",
        R"(int x = 1; { int y = x + 1; x = y; } return x;)");

    // ==== 深层未初始化变量检测 ====

    // 19. 表达式中嵌套使用未初始化变量（加法右侧）
    ok &= expect_error("uninitialized var in expr",
        R"(int x = 1; int y = x + undef;)",
        "not been initialized");

    // 20. 函数调用参数中使用未初始化变量
    ok &= expect_error("uninitialized var in func arg",
        R"(return abs(undef);)",
        "not been initialized");

    // 21. return 语句中使用未初始化变量
    ok &= expect_error("uninitialized var in return",
        R"(return undef;)",
        "not been initialized");

    // 22. 条件表达式中使用未初始化变量
    ok &= expect_error("uninitialized var in condition",
        R"(if (undef) { int x = 1; })",
        "not been initialized");

    // 23. 复合表达式中使用未初始化变量
    ok &= expect_error("uninitialized var in complex expr",
        R"(int x = 1; int y = (x * 2) + undef;)",
        "not been initialized");

    // 24. 正常：已初始化变量在表达式中使用不应报错
    ok &= expect_no_error("valid var in expr",
        R"(int x = 1; int y = 2; int z = x + y; return z;)");

    // 25. 正常：数组下标中使用已初始化变量不应报错
    ok &= expect_no_error("valid var in subscript",
        R"(arr = {10, 20, 30}; int i = 1; return arr[i];)");

    // ==== 空条件与死循环检测 ====

    // 26. if 空条件
    ok &= expect_error("if empty condition",
        R"(if () { int x = 1; })",
        "empty condition");

    // 27. while 空条件
    ok &= expect_error("while empty condition",
        R"(while () { int x = 1; })",
        "empty condition");

    // 28. while(1) 死循环
    ok &= expect_error("while(1) infinite loop",
        R"(while (1) { int x = 1; })",
        "infinite loop");

    // 29. while(true) 死循环
    ok &= expect_error("while(true) infinite loop",
        R"(while (true) { int x = 1; })",
        "infinite loop");

    // 30. for(;;) 死循环
    ok &= expect_error("for(;;) infinite loop",
        R"(for (;;) { int x = 1; })",
        "infinite loop");

    // 31. for(;1;) 死循环
    ok &= expect_error("for(;1;) infinite loop",
        R"(int i = 0; for (; 1; i++) { int x = 1; })",
        "infinite loop");

    // 32. 正常 while 不应报死循环
    ok &= expect_no_error("valid while loop",
        R"(int i = 0; while (i < 5) { i++; } return i;)");

    // 33. 正常 for 不应报死循环
    ok &= expect_no_error("valid for loop no warning",
        R"(int s = 0; for (int i = 0; i < 10; i++) { s += i; } return s;)");

    return ok;
}

bool string_key_map_test()
{    // 字符串下标（map 语义）测试
    Cifa c;
    std::string script = R"(
        dict["name"] = "Alice";
        dict["age"] = 30;
        dict["score"] = 95.5;
        string name = "name";
        string n = dict[name];
        string age = "age";
        double a = dict[age];
        string score = "score";
        double s = dict[score];
        println("Name: ", n, ", Age: ", a, ", Score: ", s);
        return a + s;  // 30 + 95.5 = 125.5
    )";
    auto o = c.run_script(script);
    if (!o.hasValue() || std::fabs(o.toDouble() - 125.5) > 1e-9)
    {
        return false;
    }

    // 访问不存在的 key 后尝试输出，应触发 runtime error
    Cifa c2;
    std::string script2 = R"(
        dict["name"] = "Alice";
        n1 = dict["name1"];
        println("Non-existent key: ", n1);
        return 0;
    )";
    auto o2 = c2.run_script(script2);
    return o2.getSpecialType() == "Error";
}

bool infinite_loop_protection_test()
{
    // while loop with variable condition (checker can't detect as infinite)
    {
        Cifa c1;
        c1.set_output_error(false);
        c1.max_loop_iterations = 100;
        auto o = c1.run_script("int x = 0; int go = 1; while(go) { x++; } return x;");
        if (o.getSpecialType() != "Error")
        {
            return false;
        }
    }
    // for loop with variable condition
    {
        Cifa c1;
        c1.set_output_error(false);
        c1.max_loop_iterations = 100;
        auto o = c1.run_script("int x = 0; int go = 1; for(; go;) { x++; } return x;");
        if (o.getSpecialType() != "Error")
        {
            return false;
        }
    }
    // do-while loop with variable condition
    {
        Cifa c1;
        c1.set_output_error(false);
        c1.max_loop_iterations = 100;
        auto o = c1.run_script("int x = 0; int go = 1; do { x++; } while(go); return x;");
        if (o.getSpecialType() != "Error")
        {
            return false;
        }
    }
    // infinite recursion should be stopped
    {
        Cifa c1;
        c1.max_call_depth = 10;
        auto o = c1.run_script("f(n) { return f(n); } return f(1);");
        if (o.getSpecialType() != "Error")
        {
            return false;
        }
    }
    // normal loops within limit should work fine
    {
        Cifa c1;
        c1.max_loop_iterations = 1000;
        auto o = c1.run_script("int s = 0; for(int i = 0; i < 500; i++) { s += i; } return s;");
        if (!o.hasValue() || o.toInt() != 124750)
        {
            return false;
        }
    }
    return true;
}

bool array_methods_test()
{
    // empty array via {}
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {};
            a.push_back(10);
            a.push_back(20);
            a.push_back(30);
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 3)
        {
            return false;
        }
    }
    // empty array via int a[]
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            int a[];
            a.push_back(1);
            a.push_back(2);
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 2)
        {
            return false;
        }
    }
    // pop_back
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 2, 3, 4, 5};
            a.pop_back();
            a.pop_back();
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 3)
        {
            return false;
        }
    }
    // insert
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 3, 4};
            a.insert(1, 2);
            return a[0] * 1000 + a[1] * 100 + a[2] * 10 + a[3];
        )");
        if (!o.hasValue() || o.toInt() != 1234)
        {
            return false;
        }
    }
    // erase
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {10, 20, 30, 40};
            a.erase(1);
            return a[0] * 100 + a[1] * 10 + a[2];
        )");
        if (!o.hasValue() || o.toInt() != 1340)
        {
            return false;
        }
    }
    // resize
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 2, 3};
            a.resize(5);
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 5)
        {
            return false;
        }
    }
    // clear
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 2, 3};
            a.clear();
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 0)
        {
            return false;
        }
    }
    // contains
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {10, 20, 30};
            int r1 = a.contains(20);
            int r2 = a.contains(99);
            return r1 * 10 + r2;
        )");
        if (!o.hasValue() || o.toInt() != 10)
        {
            return false;
        }
    }
    return true;
}

bool map_methods_test()
{
    // contains
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["a"] = 1;
            m["b"] = 2;
            int r1 = m.contains("a");
            int r2 = m.contains("z");
            return r1 * 10 + r2;
        )");
        if (!o.hasValue() || o.toInt() != 10)
        {
            return false;
        }
    }
    // erase
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["x"] = 10;
            m["y"] = 20;
            m["z"] = 30;
            m.erase("y");
            return size(m) * 100 + m.contains("x") * 10 + m.contains("y");
        )");
        if (!o.hasValue() || o.toInt() != 210)
        {
            return false;
        }
    }
    // clear
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["a"] = 1;
            m["b"] = 2;
            m.clear();
            return size(m);
        )");
        if (!o.hasValue() || o.toInt() != 0)
        {
            return false;
        }
    }
    // keys
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["alpha"] = 1;
            m["beta"] = 2;
            k = m.keys();
            return size(k);
        )");
        if (!o.hasValue() || o.toInt() != 2)
        {
            return false;
        }
    }
    return true;
}

int main()
{
    int total = 0, ok = 0;
    auto run_test = [&total, &ok](std::string name, bool (*func)())
    {
        total++;
        if (func())
        {
            ok++;
            std::cout << "\xe2\x9c\x85 " << total << ". " << name << " success" << std::endl;
        }
        else
        {
            std::cerr << "\xe2\x9d\x8c " << total << ". " << name << " failed" << std::endl;
        }
    };

    run_test("register_function_test", register_function_test);
    run_test("loop_math_test", loop_math_test);
    run_test("loop_control_test", loop_control_test);
    run_test("ternary_operator_test", ternary_operator_test);
    run_test("switch_case_test", switch_case_test);
    run_test("recursion_test", recursion_test);
    run_test("string_operation_test", string_operation_test);
    run_test("bitwise_operator_test", bitwise_operator_test);
    run_test("scope_shadowing_test", scope_shadowing_test);
    run_test("complex_math_priority_test", complex_math_priority_test);
    run_test("array_access_test", array_access_test);
    run_test("array_literal_assignment_test", array_literal_assignment_test);
    run_test("size_of_array_test", size_of_array_test);
    run_test("register_vector_test", register_vector_test);
    run_test("register_map_test", register_map_test);
    run_test("type_promotion_test", type_promotion_test);
    run_test("empty_statement_test", empty_statement_test);
    run_test("multi_dimensional_array_test", multi_dimensional_array_test);
    run_test("compound_assignment_test", compound_assignment_test);
    run_test("c_string_library_test", c_string_library_test);
    run_test("runtime_error_stack_test", runtime_error_stack_test);
    run_test("mixed_array_literal_test", mixed_array_literal_test);
    run_test("string_key_map_test", string_key_map_test);
    run_test("static_syntax_error_test", static_syntax_error_test);
    run_test("infinite_loop_protection_test", infinite_loop_protection_test);
    run_test("array_methods_test", array_methods_test);
    run_test("map_methods_test", map_methods_test);

    std::cout << "Passed " << ok << " out of " << total << " tests." << std::endl;
    return 0;
}
