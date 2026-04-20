#include "../../Cifa.h"
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
    std::vector<double> v = {1.2, 1.45, 77.3};
    Cifa c;
    c.register_vector("v", v);
    std::string script = R"(
        return v[0] + v[1] + v[2];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toDouble() == std::accumulate(v.begin(), v.end(), 0.0);
}

bool type_promotion_test()
{
    Cifa c;
    std::string script = R"(
        int a = 5;
        int b = 2;
        double res = a / b;       // 整数除法，结果可能是 2.0
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
            if (d.empty() || !d[0].isType<std::string>()) return 0;
            return (double)d[0].toString().length();
        });

    // 2. 注册 strcmp: 比较字符串
    c1.register_function("strcmp", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2) return 0;
            int res = d[0].toString().compare(d[1].toString());
            // 标准化为 C 风格的 -1, 0, 1
            return (double)((res > 0) - (res < 0));
        });

    // 3. 注册 strcat: 拼接字符串
    c1.register_function("strcat", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2) return "";
            return d[0].toString() + d[1].toString();
        });

    // 4. 注册 strcpy: 模拟赋值
    c1.register_function("strcpy", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2) return "";
            return d[1]; // 将第二个参数赋值给第一个
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

int main()
{
    auto run_test = [](std::string name, bool (*func)())
    {
        if (func())
        {
            std::cout << "✅ " << name << " success\n";
        }
        else
        {
            std::cerr << "❌ " << name << " failed\n";
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
    run_test("type_promotion_test", type_promotion_test);
    run_test("empty_statement_test", empty_statement_test);
    run_test("multi_dimensional_array_test", multi_dimensional_array_test);
    run_test("compound_assignment_test", compound_assignment_test);
    run_test("c_string_library_test", c_string_library_test);
    run_test("runtime_error_stack_test", runtime_error_stack_test);
    return 0;
}