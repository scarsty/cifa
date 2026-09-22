#include "../Cifa.h"
#include <filesystem>
#include <cstdio>
#include <print>
#include <numeric>
#include <format>
#include "../CifaBytecode.h"
#include "test_process.h"

namespace cifa {
// A test-only factory runs the same suite with either explicit resource.
static memory::Resource test_resource = memory::default_resource();
class TestBytecode : public CifaBytecode {
public:
    TestBytecode() : CifaBytecode(test_resource) {}
};
}
using namespace cifa;
using DirectCifa = Cifa;

static double template_square(double x)
{
    return x * x;
}

static double template_add(double a, double b)
{
    return a + b;
}

static int template_trunc(double x)
{
    return int(x);
}

static void template_set_flag(Object flag)
{
    (void)flag;
}

static double template_menu(double x, double y, Object choices, double count)
{
    return x + y + count + (choices.hasValue() ? 0.0 : 1.0);
}

template <typename Backend>
struct BackendTests
{
#define Cifa Backend
bool local_array_store_test()
{
    Cifa c;
    const auto result = c.run_script(R"(
        int write_values() {
            int values[0];
            values[3] = 3.9;
            values[1] = -2.9;
            return values[3] * 10 + values[1];
        }
        return write_values();
    )");
    return result.hasValue() && result.toInt() == 28 && !c.has_runtime_error();
}

bool method_receiver_error_order_test()
{
    const auto run_case = [](Cifa& interpreter)
    {
        interpreter.set_output_error(false);
        int touches = 0;
        if constexpr (std::same_as<Cifa, TestBytecode>)
        {
            interpreter.register_native_function("touch", [&touches](TestBytecode::NativeCallContext& context)
                {
                    ++touches;
                    context.set_result(std::int64_t{0});
                });
        }
        else
        {
            interpreter.register_function("touch", [&touches](ObjectVector&) -> Object
                {
                    ++touches;
                    return 0;
                });
        }
        const auto result = interpreter.run_script("value = 1; value.insert(touch(), touch());");
        return result.getSpecialType() == "Error" && interpreter.has_runtime_error() && touches == 0;
    };
    Cifa interpreter;
    return run_case(interpreter);
}

bool register_function_test()
{
    Cifa c1;
    if constexpr (std::same_as<Cifa, TestBytecode>)
        c1.register_native_function("sin", [](TestBytecode::NativeCallContext& context)
            { context.set_result(std::sin(context.to_number(0))); });
    else
        c1.register_function("sin", [](ObjectVector& d) { return std::sin(d[0].toDouble()); });

    const auto o = c1.run_script(R"(
        double PI = 3.141592653589793238462643383279;
        double return_val = 0;
        return_val += sin(0);
        return_val += sin(PI * 0.5);
        return_val += sin(PI);
        return return_val;
    )");
    return o.hasValue() && o.isNumber() && o.isType<double>()
        && std::fabs(o.ref<double>() - 1.0) <= std::numeric_limits<double>::epsilon();
}

bool register_function_template_test()
{
    Cifa c;
    if constexpr (std::same_as<Cifa, TestBytecode>)
    {
        c.register_native_function("square", [](TestBytecode::NativeCallContext& context)
            { const double value = context.to_number(0); context.set_result(value * value); });
        c.register_native_function("add", [](TestBytecode::NativeCallContext& context)
            { context.set_result(context.to_number(0) + context.to_number(1)); });
        c.register_native_function("trunc", [](TestBytecode::NativeCallContext& context)
            { context.set_result(static_cast<std::int64_t>(context.to_number(0))); });
        c.register_native_function("set_flag", [](TestBytecode::NativeCallContext& context)
            { (void)context.argument_count(); context.set_empty_result(); });
    }
    else
    {
        c.register_function("square", template_square);
        c.register_function("add", template_add);
        c.register_function("trunc", template_trunc);
        c.register_function("set_flag", template_set_flag);
    }

    const auto o = c.run_script(R"(
        set_flag(1);
        return square(3) + add(2, 4) + trunc(1.8);
    )");
    return o.isNumber() && o.toDouble() == 16.0;
}

bool registration_name_validation_test()
{
    Cifa c;
    c.set_output_error(false);
    int context = 0;
    const bool valid_function = [&]()
    {
        if constexpr (std::same_as<Cifa, TestBytecode>)
            return c.register_native_function("valid_function", [](TestBytecode::NativeCallContext& context)
                { context.set_result(context.to_number(0)); });
        else return c.register_function("valid_function", template_square);
    }();
    if (!valid_function || !c.register_parameter("valid_parameter", 1)
        || !c.register_vector("valid_vector", std::vector<int>{1, 2})
        || !c.register_user_data("valid_context", &context))
    {
        return false;
    }

    Cifa invalid;
    invalid.set_output_error(false);
    const bool template_registration_failed = [&]()
    {
        if constexpr (std::same_as<Cifa, TestBytecode>)
            return !invalid.register_native_function("1bad", [](TestBytecode::NativeCallContext&) {});
        else return !invalid.register_function("1bad", template_square);
    }()
        && invalid.has_runtime_error()
        && invalid.get_runtime_error().find("invalid registration name '1bad'") != std::string::npos;

    Cifa default_output;
    const bool standard_registration_failed = !default_output.register_parameter("bad-key", 1)
        && default_output.has_runtime_error()
        && default_output.get_runtime_error().find("invalid registration name 'bad-key'") != std::string::npos;

    return Cifa::is_valid_key("valid_key") && Cifa::is_valid_key("_value2")
        && !Cifa::is_valid_key("1bad") && !Cifa::is_valid_key("bad-key") && !Cifa::is_valid_key("return")
        && Cifa::revise_key("1bad-key") == "_bad_key" && Cifa::revise_key("return") == "return_"
        && Cifa::revise_key("") == "_" && template_registration_failed && standard_registration_failed;
}

bool exit_function_test()
{
    {
        Cifa c;
        c.run_script("value = 1; exit(); value = 2;");
        const auto result = c.run_script("return value;");
        if (!result.isNumber() || result.toDouble() != 1.0) return false;
    }
    {
        Cifa c;
        c.run_script("count = 0; running = 1; while (running) { count++; exit(); count++; }");
        const auto result = c.run_script("return count;");
        if (!result.isNumber() || result.toDouble() != 1.0) return false;
    }
    return true;
}

bool runtime_error_abort_test()
{
    const std::vector<std::string> scripts = {
        "sum = 0; for (int i = 0; i < 10; i++) { sum += missing_value(i); } return sum;",
        "for (int i = !missing_value(); i < 10; i++) { touch(); }", "for (int i = 0; missing_value(); i++) { touch(); }",
        "for (int i = 0; i < 10; i += !missing_value()) { }", "while (missing_value()) { touch(); }",
        "int i = 0; while (i < 10) { sum += !missing_value(); i++; }", "do { sum += !missing_value(); } while (sum < 10);",
        "do { } while (missing_value());", "for (item : missing_value()) { touch(); }",
        "values = {1, 2}; for (item : values) { sum += !missing_value(); touch(); }", "bad() { sum += !missing_value(); touch(); } bad();",
        "touch(!missing_value());", "sum = !missing_value();", "run_string(\"bad = {1}; return !bad;\");",
        "for (int i = 0; i < 10; i++) { run_string(\"bad = {1}; return !bad;\"); touch(); }",
        "int i = 0; while (i < 10) { run_string(\"bad = {1}; return !bad;\"); i++; touch(); }",
        "do { run_string(\"bad = {1}; return !bad;\"); touch(); } while (1);",
        "for (; run_string(\"bad = {1}; return !bad;\");) { touch(); }", "while (run_string(\"bad = {1}; return !bad;\")) { touch(); }",
        "do {} while (run_string(\"bad = {1}; return !bad;\"));", "convert({1});"};
    for (const auto& script : scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        int calls = 0;
        if constexpr (std::same_as<Cifa, TestBytecode>)
        {
            interpreter.register_native_function("touch", [&calls](TestBytecode::NativeCallContext& context) { ++calls; context.set_result(std::int64_t{0}); });
            interpreter.register_native_function("missing_value", [](TestBytecode::NativeCallContext& context) { context.set_empty_result(); });
            interpreter.register_native_function("convert", [&calls](TestBytecode::NativeCallContext& context)
                { const auto value = context.to_number(0); if (!context.is_number(0)) return; ++calls; context.set_result(value); });
        }
        else
        {
            interpreter.register_function("touch", [&calls](ObjectVector&) -> Object { ++calls; return 0; });
            interpreter.register_function("missing_value", [](ObjectVector&) -> Object { return Object(); });
            interpreter.register_function("convert", [&calls, &interpreter](ObjectVector& arguments) -> Object
                { const auto value = arguments[0].toDouble(); if (interpreter.has_runtime_error()) return Object(); ++calls; return value; });
        }
        auto result = interpreter.run_script("sum = 7; " + script + " touch();");
        if (result.getSpecialType() != "Error" || !interpreter.has_runtime_error() || calls != 0)
        {
            std::println(stderr, "Runtime abort failed: {} (calls={}, result={})\n{}{}",
                script, calls, result.getSpecialType(), interpreter.get_errors_str(), interpreter.get_runtime_error());
            return false;
        }
        if (script.find("run_string(") != std::string::npos && interpreter.is_exit_requested())
        {
            std::println(stderr, "Nested exit flag leaked: {}", script);
            return false;
        }
        result = interpreter.run_script("touch(); return 42;");
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != 42 || calls != 1)
        {
            return false;
        }
    }

    Cifa interpreter;
    interpreter.set_output_error(false);
    const auto exit_result = interpreter.run_script("total = 0; for (int i = 0; i < 3; i++) { run_string(\"exit();\"); total++; } return total;");
    if (interpreter.has_error() || interpreter.has_runtime_error() || interpreter.is_exit_requested()
        || !exit_result.isNumber() || exit_result.toInt() != 3)
    {
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        auto result = interpreter.run_script("stored = 7; bad = {1}; !bad; stored = 9;");
        if (result.getSpecialType() != "Error" || !interpreter.has_runtime_error()
            || interpreter.get_runtime_error().find("type conversion failed") == std::string::npos)
        {
            return false;
        }
        result = interpreter.run_script("return stored;");
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != 7)
        {
            return false;
        }
    }
    return true;
}

bool object_conversion_fallback_test()
{
    Object value(std::string("not a number"));
    auto& invalid = value.ref<ObjectMap>();
    if (!invalid.empty())
    {
        return false;
    }
    invalid["discarded"] = 1;
    const Object& constant = value;
    return constant.ref<ObjectMap>().empty() && value.toString() == "not a number";
}

bool typed_function_argument_error_test()
{
    Cifa c;
    c.set_output_error(false);
    if constexpr (std::same_as<Cifa, TestBytecode>)
        c.register_native_function("menu", [](TestBytecode::NativeCallContext& context) { context.set_result(context.to_number(3)); });
    else
        c.register_function("menu", template_menu);
    auto o = c.run_script("strs = {1, 2}; menu(85, 100, strs, strs);");
    return o.getSpecialType() == "Error"
        && c.get_runtime_error().find("variable 'strs'") != std::string::npos
        && c.get_runtime_error().find("to double") != std::string::npos;
}

bool object_vector_argument_error_test()
{
    if constexpr (std::same_as<Cifa, TestBytecode>)
    {
        const auto expect_conversion_error = [](bool string_conversion)
            {
                Cifa c;
                c.set_output_error(false);
                c.register_native_function("menu", [string_conversion](TestBytecode::NativeCallContext& context)
                    {
                        if (string_conversion) context.to_string(3);
                        else context.to_number(3);
                    });
                const auto result = c.run_script("strs = {1, 2}; menu(85, 100, strs, strs);");
                return result.getSpecialType() == "Error"
                    && c.get_runtime_error().find("variable 'strs'") != std::string::npos;
            };
        return expect_conversion_error(false) && expect_conversion_error(true);
    }
    else
    {
    const auto expect_conversion_error = [](const typename Backend::func_type& menu, const std::string& target_type)
        {
            Cifa c;
            c.set_output_error(false);
            c.register_function("menu", menu);
            auto result = c.run_script("strs = {1, 2}; menu(85, 100, strs, strs);");
            return result.getSpecialType() == "Error"
                && c.get_runtime_error().find("variable 'strs'") != std::string::npos
                && c.get_runtime_error().find(target_type) != std::string::npos;
        };

    return expect_conversion_error([](ObjectVector& args) -> Object
        {
            return Object(args[3].toDouble());
        }, "to double")
        && expect_conversion_error([](ObjectVector& args) -> Object
        {
            Object value = args[3];
            return Object(value.toDouble());
        }, "to double")
        && expect_conversion_error([](ObjectVector& args) -> Object
        {
            return Object(args[3].toString());
        }, "to string")
        && expect_conversion_error([](ObjectVector& args) -> Object
        {
            return Object(args[3].ref<ObjectMap>().size());
        }, typeid(ObjectMap).name());
    }
}

bool builtin_math_function_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        double total = 0;
        total += cbrt(27);
        total += log2(8);
        total += atan2(0, -1) > 3;
        total += hypot(3, 4);
        total += fmod(7, 4);
        total += remainder(7, 4);
        total += trunc(1.8);
        total += copysign(2, -1);
        total += fdim(5, 3);
        total += fmax(2, 5);
        total += fmin(2, 5);
        return total;
    )");
    return o.isNumber() && std::fabs(o.toDouble() - 22.0) < 1e-9;
}

bool builtin_type_function_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_script("auto pending_value;");
    if (!c.has_error() || c.get_errors_str().find("auto variable 'pending_value' requires an initializer") == std::string::npos)
    {
        return false;
    }
    auto o = c.run_script(R"(
        int empty_value;
        auto pending_value = 1;
        arr = {1, 2};
        m["x"] = 1;
        return type(empty_value) == "int"
            && type(pending_value) == "int"
            && type(1) == "int"
            && type(1.0f) == "double"
            && type(1.0) == "double"
            && type(true) == "bool"
            && type("abc") == "string"
            && type(arr) == "array"
            && type(m) == "map";
    )");
    return o.isNumber() && o.toDouble() == 1.0;
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

bool control_state_test()
{
    Cifa c;
    c.set_output_error(false);
    auto result = c.run_script(R"(
        int sum = 0;
        for (int i = 0; i < 5; i++) {
            switch (i) {
                case 1: continue;
                case 3: break;
                default: sum += i;
            }
            sum += 10;
        }
        {
            goto done;
            sum = 999;
        }
    done:
        return sum;
    )");
    if (c.has_error() || c.has_runtime_error() || result.toInt() != 46) return false;

    const auto rejects = [](const std::string& script, const std::string& message)
        {
            Cifa invalid;
            invalid.set_output_error(false);
            invalid.run_script(script);
            const auto error = invalid.get_errors_str();
            return invalid.has_error() && error.find(message) != std::string::npos
                && error.find(script) != std::string::npos && error.find('^') != std::string::npos;
        };
    if (!rejects("break;", "break statement is not within a loop or switch")
        || !rejects("continue;", "continue statement is not within a loop")
        || !rejects("switch (1) { case 1: continue; }", "continue statement is not within a loop"))
    {
        return false;
    }

    Cifa switch_break;
    switch_break.set_output_error(false);
    const auto switch_result = switch_break.run_script("int value = 1; switch (value) { case 1: value = 7; break; default: value = 9; } return value;");
    return !switch_break.has_error() && !switch_break.has_runtime_error()
        && switch_result.isNumber() && switch_result.toInt() == 7;
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

bool logical_short_circuit_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int a = 0, b = 0, c = 0, d = 0;
        int first = (0 && a++) || (1 && (b++ == 0)) || c++;
        int second = (1 || d++) && (0 || (++d == 1));
        return a + b * 10 + c * 100 + d * 1000 + first * 10000 + second * 100000;
    )");
    return o.isNumber() && o.toInt() == 111010;
}

bool numeric_literal_radix_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        return 42 + 1.5e2 + 0xFF + 0X10 + 0b1010 + 0B11 + 077;
    )");
    return o.isNumber() && o.toInt() == 539;
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

bool script_void_function_test()
{
    Cifa c;
    const std::string script = R"(
        total = 0;
        empty();
        increment();
        add(4);
        void empty() {}
        void increment() { total += 1; }
        void add(int amount) { total += amount; }
        return total;
    )";
    auto result = c.run_script(script);
    if (c.has_error() || c.has_runtime_error() || !result.isNumber() || result.toInt() != 5)
    {
        return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        result = c.run_script(script);
        if (c.has_error() || c.has_runtime_error() || !result.isNumber() || result.toInt() != 5)
        {
            return false;
        }
    }
    result = c.run_script("increment(); add(3); return total;");
    return !c.has_error() && !c.has_runtime_error() && result.isNumber() && result.toInt() == 9;
}

bool script_function_return_check_test()
{
    const std::vector<std::string> invalid_scripts = {
        R"(int sum = 0;
int myrandom(int a) {
}
for (int i = 0; i < 10; i++) {
    sum += myrandom(i);
}
return sum;)",
        "empty() {} empty() + 1;",
        "empty() {} value = empty(); return value + 1;",
        "empty() {} if (empty()) return 1;",
        "empty() {} while (empty()) {}",
        "empty() {} do {} while (empty());",
        "empty() {} for (int index = 0; empty(); index++) {}",
        "empty() {} for (item : empty()) {}",
        "empty() {} values = {1, 2}; return values[empty()];",
        "empty() {} values = {empty()}; return values[0] + 1;",
        "empty() {} return to_number(empty());",
        "empty() {} wrapper() { return empty(); } return wrapper() + 1;",
        "empty() { 42; } return empty() + 1;",
        "empty() { return; } return empty() + 1;",
        "empty() {} empty(value) { return value; } return empty() + 1;",
        "value(number) { if (number > 0) return number; } return value(-1) + 1;",
        "empty() {} addone(value) { return value + 1; } return addone(empty());"
    };
    for (const auto& script : invalid_scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_script(script);
        const auto error = interpreter.get_runtime_error();
        if (result.getSpecialType() != "Error" || !interpreter.has_runtime_error() || interpreter.has_error()
            || error.find("has no return value") == std::string::npos || error.find("^") == std::string::npos)
        {
            std::println(stderr, "NoValue use did not fail: {}\n{}", script, error);
            return false;
        }
        if (&script == &invalid_scripts.front())
        {
            const std::string header = "<script>:5, col 12: ";
            const std::string expected = header + "    sum += myrandom(i);\n"
                + std::string(header.size() + 11, ' ') + "^\n";
            if (error.find(expected) == std::string::npos)
            {
                std::print(stderr, "NoValue call position mismatch:\n{}Expected:\n{}", error, expected);
                return false;
            }
        }
    }

    for (const bool delayed : {false, true})
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        const std::string script = delayed
            ? "empty() {}\nsaved = empty();\nreturn abs(saved);"
            : "empty() {}\nreturn abs(empty());";
        auto result = interpreter.run_script(script);
        const auto error = interpreter.get_runtime_error();
        const std::string origin = delayed ? "<script>:2, col 9:" : "<script>:2, col 12:";
        const std::string source_line = delayed ? "saved = empty();" : "return abs(empty());";
        const std::string expected_source = "No return value originated at:\n" + origin + " " + source_line
            + "\n" + std::string(origin.size() + 1 + (delayed ? 8 : 11), ' ') + "^\n";
        const auto position = error.find(origin);
        if (result.getSpecialType() != "Error" || interpreter.has_error() || !interpreter.has_runtime_error()
            || error.find("function 'empty' has no return value") == std::string::npos || position == std::string::npos
            || error.find(expected_source) == std::string::npos
            || error.find("Call Stack (most recent call first):") == std::string::npos
            || error.find(origin, position + origin.size()) != std::string::npos
            || (delayed && error.find("<script>:3, col 12:") == std::string::npos))
        {
            std::println(stderr, "Invalid NoValue diagnostic frames:\n{}", error);
            return false;
        }
    }

    const std::vector<std::string> valid_scripts = {
        "empty() {} empty(); return 42;",
        "void empty() { return; } empty(); return 42;",
        "empty() {} if (true) empty(); else empty(); return 42;",
        "empty() {} for (int index = 0; index < 2; index++) empty(); return 42;",
        "empty() {} for (empty(); 0; empty()) {} return 42;",
        "empty() {} true ? empty() : empty(); return 42;",
        "empty() {} (empty()); return 42;",
        "empty() {} empty(), empty(); return 42;",
        "empty() {} value = empty(); return 42;",
        "empty() {} values = {empty()}; return 42;",
        "empty() {} ignore(value) { return 42; } return ignore(empty());",
        "empty() {} if (0) return empty() + 1; return 42;",
        "empty() {} 0 && empty(); 1 || empty(); return 42;",
        "empty() {} int i = 0; for (empty(); i < 2; empty()) { i++; } return 42;",
        "value() {} value(number) { return number; } value(); return value(42);",
        "return value(42); value(number) { return number; }",
        "void value() { return 42; } return value();",
        "value(number) { if (number > 0) return number; } value(-1); return value(42);"
    };
    for (const auto& script : valid_scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_script(script);
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != 42)
        {
            std::println(stderr, "Valid return use rejected: {}\n{}{}", script,
                interpreter.get_errors_str(), interpreter.get_runtime_error());
            return false;
        }
    }

    const std::vector<std::string> no_value_scripts = {
        "empty() {} return empty();",
        "empty() { 42; } return empty();",
        "empty() { return; } return empty();",
        "empty() {} value = empty(); return value;",
        "empty() {} values = {empty()}; return values[0];",
        "empty() {} wrapper() { return empty(); } return wrapper();",
        "inner() { return 42; } outer() { inner(); } return outer();",
        "empty() {} return true ? empty() : 1;",
        "empty() {} empty(value) { return value; } return empty();",
        "value(number) { if (number > 0) return number; } return value(-1);"
    };
    for (const auto& script : no_value_scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_script(script);
        if (interpreter.has_error() || interpreter.has_runtime_error() || result.getSpecialType() != "NoValue"
            || result.isNumber() || result.isType<std::string>())
        {
            std::println(stderr, "Missing NoValue result: {}\n{}{}", script,
                interpreter.get_errors_str(), interpreter.get_runtime_error());
            return false;
        }
    }

    Cifa interpreter;
    interpreter.set_output_error(false);
    interpreter.run_script("saved() {}");
    auto result = interpreter.run_script("return saved();");
    if (interpreter.has_error() || interpreter.has_runtime_error() || result.getSpecialType() != "NoValue")
    {
        return false;
    }
    result = interpreter.run_script("return type(saved());");
    if (interpreter.has_error() || interpreter.has_runtime_error()
        || !result.isType<std::string>() || result.toString() != "NoValue")
    {
        return false;
    }
    result = interpreter.run_script("value = saved(); return value + 1;");
    if (result.getSpecialType() != "Error"
        || interpreter.get_runtime_error().find("function 'saved' has no return value") == std::string::npos)
    {
        return false;
    }
    result = interpreter.run_script("saved(); return 42;");
    if (interpreter.has_error() || interpreter.has_runtime_error()
        || !result.isNumber() || result.toInt() != 42)
    {
        std::println(stderr, "Saved function call failed: {}{}", interpreter.get_errors_str(), interpreter.get_runtime_error());
        return false;
    }
    result = interpreter.run_script("saved() { return 42; } return saved();");
    if (interpreter.has_error() || interpreter.has_runtime_error() || !result.isNumber() || result.toInt() != 42)
    {
        return false;
    }
    interpreter.register_parameter("argument", 42);
    for (int argument : {42, -1, 7})
    {
        interpreter.register_parameter("argument", argument);
        result = interpreter.run_script("branch(number) { if (number > 0) return number; } return branch(argument);");
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || (argument > 0 && (!result.isNumber() || result.toInt() != argument))
            || (argument <= 0 && result.getSpecialType() != "NoValue"))
        {
            return false;
        }
    }
    result = interpreter.run_script("stop() { exit(); } return stop() + 1;");
    return !interpreter.has_error() && !interpreter.has_runtime_error() && interpreter.is_exit_requested();
}

bool script_function_argument_count_test()
{
    const auto expect_runtime_error = [](const char* label, const std::string& script, const std::string& expected)
        {
            Cifa c;
            c.set_output_error(false);
            const auto result = c.run_script(script);
            const std::string error = c.get_runtime_error();
            if (result.getSpecialType() != "Error" || error.find(expected) == std::string::npos)
            {
                std::println(stderr, "  {} failed: expected runtime error: {}\n    actual: {}", label, expected, error);
                return false;
            }
            std::println(stderr, "  {}: {}", label, error);
            return true;
        };
    {
        Cifa c;
        auto result = c.run_script(
            "describe() { return 0; } "
            "describe(value) { return value; } "
            "describe(left, right) { return left + right; } "
            "return describe() + describe(2) + describe(3, 4);");
        if (!result.isNumber() || result.toDouble() != 9.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script("same(value) { return value; } same(other) { return other + 10; } return same(1);");
        if (!result.isNumber() || result.toDouble() != 11.0 || c.has_error())
        {
            return false;
        }
        result = c.run_script("same(number) { return number + 100; } return same(1);");
        if (!result.isNumber() || result.toDouble() != 101.0 || c.has_error())
        {
            return false;
        }
    }
    return expect_runtime_error("missing overload", "add(a, b) { return a + b; } return add(2);",
        "no overload for 1 arguments; available: 2")
        && expect_runtime_error("extra argument overload", "add(a, b) { return a + b; } return add(2, 3, 4);",
            "no overload for 3 arguments; available: 2")
        && [&]()
        {
            Cifa c;
            c.set_output_error(false);
            c.run_script("sqrt(value) { return value; }");
            const std::string errors = c.get_errors_str();
            return c.has_error() && errors.find("script function 'sqrt' conflicts with a host function") != std::string::npos
                && errors.find("^") != std::string::npos;
        }();
}

bool script_function_global_scope_test()
{
    Cifa c;
    c.set_output_error(false);
    int captured_value = 0;
    if constexpr (std::same_as<Cifa, TestBytecode>)
        c.register_native_function("capture", [&captured_value](TestBytecode::NativeCallContext& context)
            {
                captured_value = context.argument_count() == 0 ? 0 : static_cast<int>(context.to_integer(0));
                context.set_empty_result();
            });
    else
        c.register_function("capture", [&captured_value](ObjectVector& args) -> Object
            {
                captured_value = args.empty() ? 0 : args[0].toInt();
                return Object();
            });
    auto global_result = c.run_script(R"(
        b = 304;
        update_b() {
            capture(b);
            b = b + 1;
            return b;
        }
        result = update_b();
        return b * 1000 + result;
    )");
    if (!global_result.isNumber() || global_result.toInt() != 305305 || captured_value != 304)
    {
        return false;
    }

    auto next_script_result = c.run_script("return 7;");
    if (!next_script_result.isNumber() || next_script_result.toInt() != 7 || c.has_error())
    {
        return false;
    }

    auto persisted_function_result = c.run_script("b = 40; return update_b();");
    if (!persisted_function_result.isNumber() || persisted_function_result.toInt() != 41 || c.has_error())
    {
        return false;
    }

    c.run_script("broken() { return missing_function(); }");
    if (!c.has_error())
    {
        return false;
    }
    auto after_failed_definition = c.run_script("return 8;");
    if (!after_failed_definition.isNumber() || after_failed_definition.toInt() != 8 || c.has_error())
    {
        return false;
    }

    auto shadow_result = c.run_script(R"(
        b = 10;
        add_one(b) {
            b = b + 1;
            return b;
        }
        result = add_one(20);
        return b * 100 + result;
    )");
    return shadow_result.isNumber() && shadow_result.toInt() == 1021;
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

bool string_compare_test()
{    // 字符串比较与跨行逻辑运算测试
    Cifa c;
    std::string script = R"(
        return "abc" == "abc"
            && "abc" != "def";
    )";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 1.0;
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
        int inner = 0;
        {
            int x = 15 + 5;
            inner = x;
            if (x == 20) {
                int x = 30;
            }
        }
        return x * 100 + inner;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 1020;    // 融合表达式应写入内层声明，外部作用域不应受影响
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

bool same_precedence_left_assoc_test()
{    // 同优先级运算符应按 C++ 规则左结合，即 a/b*c == (a/b)*c，而非 a/(b*c)
    Cifa c;
    bool ok = true;
    // 除后乘：100/10*2 == 20
    ok = ok && c.run_script("return 100/10*2;").toInt() == 20;
    // 乘后除：100*10/2 == 500
    ok = ok && c.run_script("return 100*10/2;").toInt() == 500;
    // 除后模：100/10%3 == 1
    ok = ok && c.run_script("return 100/10%3;").toInt() == 1;
    // 模后除：10%4/2 == 1
    ok = ok && c.run_script("return 10%4/2;").toInt() == 1;
    // 减后加：10-3+1 == 8
    ok = ok && c.run_script("return 10-3+1;").toInt() == 8;
    // lW/2*lH/2 应等于 (lW/2*lH)/2，即 8/2*6/2==12
    ok = ok && c.run_script("double lW=8; double lH=6; return lW/2*lH/2;").toInt() == 12;
    return ok;
}

bool unary_minus_test()
{
    Cifa c;
    bool ok = true;
    ok = ok && c.run_script("return -5;").toDouble() == -5;           // 前置负号 + 常量
    ok = ok && c.run_script("return -(2+3);").toDouble() == -5;       // 前置负号 + 括号表达式
    ok = ok && c.run_script("return -(-3);").toDouble() == 3;         // 双重前置负号
    ok = ok && c.run_script("return 1 - -2;").toDouble() == 3;        // 二元减 + 前置负号
    ok = ok && c.run_script("return 10 - 3 - 2;").toDouble() == 5;    // 二元减左结合
    ok = ok && c.run_script("return -3 + 5;").toDouble() == 2;        // 前置负号 + 加法
    ok = ok && c.run_script("return 2 * -3;").toDouble() == -6;       // 乘以前置负号
    ok = ok && c.run_script("x = 7; return x * -1;").toDouble() == -7; // 变量乘以前置负号
    ok = ok && c.run_script("x = 7; x = x * -1; return x;").toDouble() == -7; // 赋值语境中的前置负号
    ok = ok && c.run_script("x = 7; return x * -1 + x * -2;").toDouble() == -21; // 连续乘法项
    ok = ok && c.run_script("return -(2*3);").toDouble() == -6;       // 前置负号 + 乘法括号
    ok = ok && c.run_script("return -2 + -3;").toDouble() == -5;      // 两个前置负号相加
    ok = ok && c.run_script("return +5;").toDouble() == 5;            // 前置正号 + 常量
    ok = ok && c.run_script("return +(2+3);").toDouble() == 5;        // 前置正号 + 括号表达式
    ok = ok && c.run_script("return 2 * +3;").toDouble() == 6;        // 乘以前置正号
    ok = ok && c.run_script("return 1 + +2;").toDouble() == 3;        // 二元加 + 前置正号
    return ok;
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
    // int/int 保持整数除法，混合 double 后提升为 double。
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

bool typed_numeric_storage_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int i = 1.9;
        float f = 1.9;
        double d = 1.9f;
        bool t = 0.5;
        bool u = 0.0;
        return i == 1
            && type(i) == "int"
            && type(f) == "double"
            && type(d) == "double"
            && type(t) == "bool"
            && type(abs(-3)) == "int"
            && type(max(1, 2)) == "int"
            && type(max(1, 2.0)) == "double"
            && t && !u;
    )");
    if (!o.hasValue() || !o.toBool())
    {
        std::println(stderr, "typed array and struct: result={}, error={}", o.toString(), c.get_runtime_error());
        return false;
    }
    return true;
}

bool auto_type_inference_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_script("auto pending;");
    if (!c.has_error() || c.get_errors_str().find("auto variable 'pending' requires an initializer") == std::string::npos)
    {
        return false;
    }
    c.run_script("empty() {} auto pending = empty();");
    if (!c.has_runtime_error() || c.get_runtime_error().find("cannot infer type for auto variable from NoValue") == std::string::npos)
    {
        return false;
    }
    const std::pair<const char*, std::int64_t> cases[] = {
        { "auto value = 1; value = 3.9; return type(value) == \"int\" && value == 3;", 1 },
        { "auto value = 1.5; value = 3; return type(value) == \"double\" && value == 3.0;", 1 },
        { "auto value = true; value = 0.0; return type(value) == \"bool\" && !value;", 1 },
        { "auto value = \"abc\"; return type(value) == \"string\" && value + \"d\" == \"abcd\";", 1 },
        { "auto values = {1.5, 2.5}; double sum = 0.0; for (auto value : values) sum += value; return sum == 4.0;", 1 },
        { "int source = 7; value = source; value = \"changed\"; return value == \"changed\";", 1 },
    };
    for (const auto& [script, expected] : cases)
    {
        Cifa scenario;
        scenario.set_output_error(false);
        const auto result = scenario.run_script(script);
        if (!result.hasValue() || result.toInt() != expected || scenario.has_runtime_error()) return false;
    }
    return true;
}

bool c_style_cast_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        double a = 1.34;
        int b = (int)a;
        int c = (int)-3.9;
        int e = (int)1.9 + 2;
        int g = (int)(1.9 + 2.1);
        float f = (float)a;
        double d = (double)3;
        bool t = (bool)2.5;
        bool u = (bool)0.0;
        float precision = 0.1;
        return b == 1 && c == -3 && e == 3 && g == 4 && t && !u
            && precision == 0.1
            && type(b) == "int" && type(f) == "double"
            && type(d) == "double" && type(t) == "bool";
    )");
    return o.hasValue() && o.toBool();
}

bool integer_arithmetic_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        return 5 / 2 == 2
            && 5 % 2 == 1
            && 5 / 2.0 == 2.5
            && 1.0f / 2.0f == 0.5f
            && type(5 / 2) == "int"
            && type(5 / 2.0) == "double"
            && type(1.0f / 2.0f) == "double";
    )");
    return o.hasValue() && o.toBool();
}

bool typed_function_conversion_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int truncate(double x) { return x; }
        double half(int x) { return x / 2; }
        int add_int(double a, int b) { return a + b; }
        return truncate(3.9) == 3
            && half(3) == 1.0
            && add_int(1.9, 2.9) == 3
            && type(truncate(3.9)) == "int"
            && type(half(3)) == "double";
    )");
    return o.hasValue() && o.toBool();
}

bool typed_array_and_struct_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int a[2];
        a[0] = 3.9;
        a[1] = -2.9;
        int matrix[2][2];
        matrix[0][0] = 3.9;
        struct S { int i; float f; bool b; };
        S s;
        s.i = 1.9;
        s.f = 1.9;
        s.b = 2;
        return a[0] == 3 && a[1] == -2
            && matrix[0][0] == 3
            && s.i == 1 && s.b
            && type(a[0]) == "int"
            && type(matrix[0][0]) == "int"
            && type(s) == "S"
            && type(s.i) == "int"
            && type(s.f) == "double"
            && type(s.b) == "bool";
    )");
    return o.hasValue() && o.toBool();
}

bool typed_conversion_error_test()
{
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("int value = \"not a number\";");
        if (!c.has_runtime_error() || c.get_runtime_error().find("cannot convert value to 'int'") == std::string::npos)
        {
            return false;
        }
    }
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("double value = (double)\"not a number\";");
        if (!c.has_runtime_error() || c.get_runtime_error().find("cannot convert value to 'double'") == std::string::npos)
        {
            return false;
        }
    }
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("return 1 / 0;");
        if (!c.has_runtime_error() || c.get_runtime_error().find("integer division by zero") == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

struct RegisteredTestValue
{
    int number = 7;
};

bool registered_type_binding_test()
{
    Cifa c;
    c.set_output_error(false);
    if (!c.register_type<RegisteredTestValue>("Box") || !c.register_type<std::int64_t>("Index")) { return false; }
    c.register_parameter("original", RegisteredTestValue{});
    if constexpr (std::same_as<Cifa, TestBytecode>)
        c.register_native_function("inspect_box", [](TestBytecode::NativeCallContext& context)
            {
                const auto* value = context.resource<RegisteredTestValue>(0);
                if (!value) { context.report_error("inspect_box requires Box"); return; }
                context.set_result(static_cast<std::int64_t>(value->number));
            });
    else
        c.register_function("inspect_box", [](ObjectVector& args) { return Object(args[0].to<RegisteredTestValue>().number); });
    const auto result = c.run_script(R"(
        Box copy = original;
        auto inferred = original;
        Box identity(Box value) { return value; }
        fixed(int value) { value = 3.9; return value; }
        loose(value) { value = 3.9; return value; }
        int source = 1;
        dynamic = source;
        dynamic = "changed";
        Index index = 3.9;
        int checks = 0;
        checks += inspect_box(identity(copy)) == 7;
        checks += inspect_box(inferred) == 7;
        checks += type(inferred) == "Box";
        checks += type(index) == "int";
        checks += index == 3;
        checks += fixed(1) == 3;
        checks += loose(source) == 3.9;
        checks += dynamic == "changed";
        return checks == 8;
    )");
    if (!result.toBool() || c.has_runtime_error())
    {
        return false;
    }
    c.run_script("inferred = 1;");
    if (!c.has_runtime_error()) { return false; }
    Cifa named_type;
    named_type.set_output_error(false);
    if (!named_type.register_type<RegisteredTestValue>("dynamic")) { return false; }
    named_type.run_script("dynamic value = 1;");
    if (!named_type.has_runtime_error()
        || named_type.get_runtime_error().find("cannot convert value to 'dynamic'") == std::string::npos) { return false; }
    Cifa comma;
    auto comma_result = comma.run_script("count = 0; unused = (count = 1, count += 2); return count;");
    if (comma.has_runtime_error() || comma.has_error() || comma_result.toInt64() != 3) { return false; }
    Cifa invalid;
    invalid.set_output_error(false);
    return !invalid.register_type<int>("bad-name") && invalid.has_runtime_error();
}

bool int64_storage_test()
{
    Cifa c;
    c.register_parameter("wide", std::int64_t{9007199254740993LL});
    if constexpr (std::same_as<Cifa, TestBytecode>)
        c.register_native_function("identity64", [](TestBytecode::NativeCallContext& context) { context.set_result(context.to_integer(0)); });
    else
        c.register_function("identity64", +[](std::int64_t value) { return value; });
    auto result = c.run_script(R"(
        auto exact = 9007199254740993;
        int largest = 9223372036854775807;
        int smallest = -largest - 1;
        return exact == wide && identity64(exact) == wide && exact - 9007199254740992 == 1
            && max(exact, exact - 1) == exact && min(exact, exact - 1) == exact - 1
            && largest + 1 == smallest && smallest % -1 == 0
            && (1 << 40) == 1099511627776 && (1099511627776 >> 40) == 1
            && sprintf("%lld", exact) == "9007199254740993"
            && format("{}", exact) == "9007199254740993"
            && type(1.0f) == "double";
    )");
    if (!result.toBool() || c.has_runtime_error() || !Object(1).isType<std::int64_t>()
        || !Object(1.0f).isType<double>()) { return false; }
    c.set_output_error(false);
    c.run_script("return 1 << 64;");
    return c.has_runtime_error();
}

bool custom_operator_dispatch_test()
{
    using CallbackList = std::vector<std::function<Object(const Object&, const Object&)>>;
    const std::pair<const char*, CallbackList DirectCifa::*> operations[] = {
        {"+", &DirectCifa::user_add}, {"-", &DirectCifa::user_sub}, {"*", &DirectCifa::user_mul}, {"/", &DirectCifa::user_div},
        {"%", &DirectCifa::user_mod}, {"&", &DirectCifa::user_bit_and}, {"|", &DirectCifa::user_bit_or}, {"^", &DirectCifa::user_bit_xor},
        {"<<", &DirectCifa::user_shift_left}, {">>", &DirectCifa::user_shift_right}, {"==", &DirectCifa::user_equal},
        {"!=", &DirectCifa::user_not_equal}, {"<", &DirectCifa::user_less}, {">", &DirectCifa::user_more},
        {"<=", &DirectCifa::user_less_equal}, {">=", &DirectCifa::user_more_equal}
    };
    for (const auto& [symbol, callbacks] : operations)
    {
        DirectCifa c;
        c.set_output_error(false);
        c.register_parameter("host", RegisteredTestValue{});
        int calls = 0;
        (c.*callbacks).push_back([](const Object&, const Object&) { return Object(); });
        (c.*callbacks).push_back([&calls](const Object&, const Object&) { ++calls; return Object(true); });
        for (const auto& operands : {std::pair{"host", "2"}, std::pair{"2", "host"}})
        {
            auto result = c.run_script(std::format("return {} {} {};", operands.first, symbol, operands.second));
            if (!result.isType<bool>() || !result.toBool() || c.has_runtime_error()) { return false; }
        }
        if (calls != 2) { return false; }
        c.run_script(std::format("empty_function() {{}} unused = empty_function() {} 1; return 7;", symbol));
        if (!c.has_runtime_error() || c.get_runtime_error().find("has no return value") == std::string::npos) { return false; }
    }
    return true;
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

bool else_if_chain_test()
{
    Cifa c;
    const auto run_branch = [&c](int value)
        {
            return c.run_script(std::format(R"(
                int value = {};
                if (value == 3) {{ return 30; }}
                else if (value == 2) {{ return 20; }}
                else if (value == 1) {{ return 10; }}
                else {{ return 0; }}
            )", value));
        };

    return run_branch(3).toInt() == 30
        && run_branch(2).toInt() == 20
        && run_branch(1).toInt() == 10
        && run_branch(0).toInt() == 0;
}

bool multi_dimensional_array_test()
{
    Cifa c;
    std::string script = R"(
        grid = {{1, 2}, {3, 4}};
        grid[2][1] = 5;
        return grid[1][0] + grid[2][1];
    )";
    auto o = c.run_script(script);
    return o.toInt() == 8;
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

    if constexpr (std::same_as<Cifa, TestBytecode>)
    {
        c1.register_native_function("strlen", [](TestBytecode::NativeCallContext& context)
            {
                context.set_result(context.argument_count() == 0 || !context.is_string(0)
                    ? std::int64_t{0} : static_cast<std::int64_t>(context.to_string(0).size()));
            });
        c1.register_native_function("strcmp", [](TestBytecode::NativeCallContext& context)
            {
                if (context.argument_count() < 2) { context.set_result(std::int64_t{0}); return; }
                const int comparison = context.to_string(0).compare(context.to_string(1));
                context.set_result(static_cast<std::int64_t>((comparison > 0) - (comparison < 0)));
            });
        c1.register_native_function("strcat", [](TestBytecode::NativeCallContext& context)
            {
                context.set_result(context.argument_count() < 2
                    ? std::string{} : context.to_string(0) + context.to_string(1));
            });
        c1.register_native_function("strcpy", [](TestBytecode::NativeCallContext& context)
            {
                context.set_result(context.argument_count() < 2 ? std::string{} : context.to_string(1));
            });
    }
    else
    {
        c1.register_function("strlen", [](ObjectVector& d) -> Object
            {
                if (d.empty() || !d[0].isType<std::string>())
                {
                    return 0;
                }
                return (double)d[0].toString().length();
            });
        c1.register_function("strcmp", [](ObjectVector& d) -> Object
            {
                if (d.size() < 2)
                {
                    return 0;
                }
                int res = d[0].toString().compare(d[1].toString());
                return (double)((res > 0) - (res < 0));
            });
        c1.register_function("strcat", [](ObjectVector& d) -> Object
            {
                if (d.size() < 2)
                {
                    return "";
                }
                return d[0].toString() + d[1].toString();
            });
        c1.register_function("strcpy", [](ObjectVector& d) -> Object
            {
                if (d.size() < 2)
                {
                    return "";
                }
                return d[1];
            });
    }

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
{    // 多层脚本函数调用中的运行时错误应传播到顶层。
    Cifa c;
    c.set_output_error(false);
    std::string script = R"(
        string bad = "abc";
        inner(value) { return sqrt(value); }
        middle(value) { return inner(value); }
        outer(value) { return middle(value); }
        return outer(bad);
    )";
    auto o = c.run_script(script);
    const std::string error = c.get_runtime_error();
    return o.getSpecialType() == "Error"
        && error.find("type conversion failed") != std::string::npos
        && error.find("Call Stack (most recent call first):") != std::string::npos
        && error.find("func inner()") != std::string::npos
        && error.find("func middle()") != std::string::npos
        && error.find("func outer()") != std::string::npos;
}

bool uninitialized_variable_runtime_test()
{
    Cifa c;
    auto o = c.run_script("double x; return x * 2;");
    return o.getSpecialType() == "Error"
        && c.get_runtime_error().find("variable 'x' has not been initialized") != std::string::npos;
}

bool nested_execution_state_test()
{
    Cifa c;
    c.set_output_error(false);
    auto success = c.run_script("shared = 10; run_string(\"shared += 1; return shared;\"); run_file(\"unit_test/test_data/nested_increment.cifa\"); return shared;");
    if (!success.isNumber() || success.toInt() != 21 || c.has_error())
    {
        return false;
    }

    c.run_script("run_string(\"return missing_nested_value;\"); return 1;");
    if (!c.has_error() || c.get_errors_str().find("missing_nested_value") == std::string::npos)
    {
        return false;
    }

    c.run_script("run_file(\"unit_test/test_data/not_present_nested.cifa\"); double outer_value; return outer_value;");
    return c.has_error()
        && c.get_errors_str().find("cannot open file") != std::string::npos
        && !c.get_runtime_error().empty()
        && c.get_runtime_error().find("double outer_value; return outer_value;") != std::string::npos;
}

bool nested_error_preservation_test()
{
    Cifa c;
    c.set_output_error(false);
    if constexpr (std::same_as<Cifa, TestBytecode>)
    {
        c.register_native_function("run_first", [&c](TestBytecode::NativeCallContext& context)
            {
                c.run_script("first_missing_function();");
                context.set_empty_result();
            });
        c.register_native_function("run_second", [&c](TestBytecode::NativeCallContext& context)
            {
                c.run_script("second_missing_function();");
                context.set_empty_result();
            });
    }
    else
    {
        c.register_function("run_first", [&c](ObjectVector&) -> Object
            {
                return c.run_script("first_missing_function();");
            });
        c.register_function("run_second", [&c](ObjectVector&) -> Object
            {
                return c.run_script("second_missing_function();");
            });
    }
    c.run_script("run_first(); run_second();");
    const std::string errors = c.get_errors_str();
    return c.get_errors().size() == 2
        && errors.find("first_missing_function") != std::string::npos
        && errors.find("second_missing_function") != std::string::npos;
}

    bool nested_static_error_source_test()
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("run_string(\"missing_nested_function();\");");
        const auto errors = c.get_errors();
        return errors.size() == 1
        && errors.front().message == "function 'missing_nested_function' is not defined"
        && errors.front().source_text == "missing_nested_function();"
        && c.get_errors_str().find("missing_nested_function();") != std::string::npos;
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

// ---- 共用测试辅助函数 ----

// 断言脚本产生语法错误，且错误信息包含 keyword；同时验证输出带行号和 ^ 箭头
static bool expect_syntax_error(const std::string& label, const std::string& script, const std::string& keyword)
{
    Cifa c;
    c.set_output_error(false);
    c.run_script(script);
    std::string err = c.get_errors_str();
    std::println(stderr, "  [{}]:", label);
    if (err.find("Syntax Error:") == std::string::npos || err.find(keyword) == std::string::npos)
    {
        std::println(stderr, "    FAIL: expected keyword \"{}\" in error output", keyword);
        if (!err.empty()) { std::print(stderr, "    Got:\n{}", err); }
        else { std::println(stderr, "    (no error produced)"); }
        return false;
    }
    if (err.find("^") == std::string::npos)
    {
        std::println(stderr, "    FAIL: missing caret (^) in error output");
        return false;
    }
    std::print(stderr, "{}", err);
    return true;
}

// 断言脚本不产生任何语法错误
static bool expect_no_syntax_error(const std::string& label, const std::string& script)
{
    Cifa c;
    c.set_output_error(false);
    c.run_script(script);
    std::string err = c.get_errors_str();
    std::print(stderr, "  [{}]: ", label);
    if (!err.empty())
    {
        std::print(stderr, "FAIL unexpected error:\n{}", err);
        return false;
    }
    std::println(stderr, "OK");
    return true;
}

bool static_syntax_error_test()
{
    const auto& expect_error = expect_syntax_error;
    const auto& expect_no_error = expect_no_syntax_error;

    bool ok = true;

    // ==== 应触发静态错误的场景 ====

    // 1. 赋值右侧使用未初始化变量
    ok &= expect_error("uninitialized var in assign",
        R"(int y = undef;)",
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

    // 10. 非法字符不能被静默忽略，避免 #strs 被解释为 strs
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("return menu(85, 100, strs, #strs);");
        std::string err = c.get_errors_str();
        if (err.find("unexpected character '#'") == std::string::npos
            || err.find("col 29") == std::string::npos)
        {
            std::print(stderr, "  FAIL [unexpected character]: expected '#' at column 29\n    Got:\n{}", err);
            ok = false;
        }
    }

    // 多行错误同时检查正文、源码和插入符。
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("int x = 10;\nint y = undef;\n");
        std::string err = c.get_errors_str();
        const std::string header = "  at <script>:2, col 9: ";
        const std::string expected = header + "int y = undef;\n"
            + std::string(header.size() + 8, ' ') + "^\n";
        if (!c.has_error() || err.find("not been initialized") == std::string::npos
            || err.find(expected) == std::string::npos)
        {
            std::print(stderr, "  FAIL [error line number]: expected '<script>:2' and 'undef' in error output\n    Got:\n{}", err);
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

    // ==== 空条件检查 ====

    // 26. if 空条件
    ok &= expect_error("if empty condition",
        R"(if () { int x = 1; })",
        "empty condition");

    // 27. while 空条件
    ok &= expect_error("while empty condition",
        R"(while () { int x = 1; })",
        "empty condition");

    ok &= expect_no_error("while(1) with break",
        R"(while (1) { break; })");

    ok &= expect_no_error("while(true) with break",
        R"(while (true) { break; })");

    ok &= expect_no_error("for(;;) with break",
        R"(for (;;) { break; })");

    ok &= expect_no_error("for(;1;) with break",
        R"(int i = 0; for (; 1; i++) { break; })");

    ok &= expect_no_error("for(;true;) with return",
        R"(for (; true;) { return 7; })");

    ok &= expect_no_error("valid while loop",
        R"(int i = 0; while (i < 5) { i++; } return i;)");

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

bool loop_and_recursion_execution_test()
{
    const std::pair<const char*, int> cases[] = {
        { "value = 0; while (1) { value++; if (value == 5) break; } return value;", 5 },
        { "while (true) { return 7; }", 7 },
        { "value = 0; for (;;) { value++; if (value == 5) break; } return value;", 5 },
        { "value = 0; for (; 1; value++) { if (value == 5) break; } return value;", 5 },
        { "for (; true;) { return 7; }", 7 },
        { "value = 0; while (value < 500) { value++; } return value;", 500 },
        { "total = 0; for (int index = 0; index < 500; index++) { total += index; } return total;", 124750 },
        { "value = 0; do { value++; } while (value < 500); return value;", 500 },
        { "values = {1, 2, 3, 4, 5}; total = 0; for (value : values) { total += value; } return total;", 15 },
        { "value = 0; again: value++; if (value < 500) goto again; return value;", 500 },
        { "sum_to(depth) { if (depth <= 0) return 0; return depth + sum_to(depth - 1); } return sum_to(4);", 10 },
    };
    for (const auto& [script, expected] : cases)
    {
        Cifa interpreter;
        const auto result = interpreter.run_script(script);
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != expected)
        {
            std::println(stderr, "  loop/recursion execution failed: {}", script);
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
            std::println(stderr, "array push_back literal: {}", c1.get_runtime_error());
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
            std::println(stderr, "array push_back typed: {}", c1.get_runtime_error());
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
            std::println(stderr, "array pop_back: {}", c1.get_runtime_error());
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
            std::println(stderr, "array insert: {}", c1.get_runtime_error());
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
            std::println(stderr, "array erase: {}", c1.get_runtime_error());
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
            std::println(stderr, "array resize: {}", c1.get_runtime_error());
            return false;
        }
    }
    // reserve does not change length and supports subsequent writes
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {};
            int before = a.reserve(8);
            a.push_back(12);
            a.push_back(34);
            return before * 100 + size(a) * 10 + a[0] + a[1];
        )");
        if (!o.hasValue() || o.toInt() != 66)
        {
            std::println(stderr, "array reserve: {}", c1.get_runtime_error());
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
            std::println(stderr, "array clear: {}", c1.get_runtime_error());
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
            std::println(stderr, "array contains: {}", c1.get_runtime_error());
            return false;
        }
    }
    return true;
}

bool range_for_test()
{
    // 循环变量为值副本；修改不会回写数组元素。
    {
        Cifa c;
        auto o = c.run_script(R"(
            values = {1, 2, 3, 4};
            int sum = 0;
            for (int value : values) {
                value *= 10;
                if (value == 20) continue;
                if (value == 40) break;
                sum += value;
            }
            return sum + values[0] + values[1] + values[2] + values[3];
        )");
        if (!o.isNumber() || o.toDouble() != 50)
        {
            return false;
        }
    }
    // auto 形式与传统范围循环作用域。
    {
        Cifa c;
        auto o = c.run_script(R"(
            values = {2, 3, 5};
            int product = 1;
            for (auto value : values) { product *= value; }
            return product;
        )");
        if (!o.isNumber() || o.toDouble() != 30)
        {
            return false;
        }
    }
    return true;
}

bool goto_test()
{
    {
        Cifa c;
        auto result = c.run_script(R"(
            int value = 0;
        again:
            value += 1;
            if (value < 3) goto again;
            return value;
        )");
        if (!result.isNumber() || result.toDouble() != 3.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script(R"(
            int value = 0;
            if (value == 0) {
                value = 1;
            }
        label_after_if:
            value += 1;
            if (value < 3) { goto label_after_if; }
            return value;
        )");
        if (!result.isNumber() || result.toDouble() != 3.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script(R"(
            int value = 0;
            {
                value = 1;
                goto done;
            }
            value = 2;
        done:
            return value;
        )");
        if (!result.isNumber() || result.toDouble() != 1.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script(R"(
            count_to(limit) {
                int value = 0;
            again:
                value += 1;
                if (value < limit) goto again;
                return value;
            }
            return count_to(4);
        )");
        if (!result.isNumber() || result.toDouble() != 4.0)
        {
            return false;
        }
    }

    const auto expect_static_error = [](const std::string& script, const std::string& expected)
        {
            Cifa c;
            c.set_output_error(false);
            c.run_script(script);
            return c.has_error() && c.get_errors_str().find(expected) != std::string::npos;
        };
    return expect_static_error("goto missing;", "goto target 'missing' is not defined")
        && expect_static_error("first: first:", "duplicate label 'first'")
        && expect_static_error("goto inside; { inside: return 1; }", "goto 'inside' jumps into a nested or sibling block")
        && expect_static_error("{ left: goto right; } { right: return 1; }", "goto 'right' jumps into a nested or sibling block");
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

bool non_block_branch_declaration_test()
{
    // 委托到文件级辅助函数，硬编码搜索关键词 "non-block"
    auto expect_error = [](const std::string& label, const std::string& script) -> bool
    {
        return expect_syntax_error(label, script, "non-block");
    };
    const auto& expect_no_error = expect_no_syntax_error;

    bool ok = true;

    // ==== 应报错：非封闭体内定义变量（显式类型前缀）====

    // if 体内声明并初始化
    ok &= expect_error("if non-block init decl",
        R"(int a = 1; if (a) int x = 0;)");

    // if 体内仅声明（无初始化）
    ok &= expect_error("if non-block bare decl",
        R"(int a = 1; if (a) int x;)");

    // else 体内声明
    ok &= expect_error("else non-block decl",
        R"(int a = 1; if (a) a = 0; else int x = 1;)");

    // while 体内声明
    ok &= expect_error("while non-block decl",
        R"(int i = 5; while (i > 0) int x = i;)");

    // for 体内声明
    ok &= expect_error("for non-block decl",
        R"(for (int i = 0; i < 5; i++) int x = i;)");

    // ==== 应报错：非封闭体内引入新变量（无类型前缀，变量不在表中）====

    // if 体内引入全新变量（无 int 前缀，赋值形式）
    ok &= expect_error("if non-block new var no type",
        R"(int a = 1; if (a) newvar = 0;)");

    // if 体内裸引用未声明变量（x; 形式）
    ok &= expect_error("if non-block bare ref undeclared",
        R"(int a = 1; if (a) newvar;)");

    // while 体内引入全新变量
    ok &= expect_error("while non-block new var no type",
        R"(int i = 5; while (i > 0) newvar = i;)");

    // while 体内裸引用未声明变量
    ok &= expect_error("while non-block bare ref undeclared",
        R"(int i = 5; while (i > 0) newvar;)");

    // for 体内引入全新变量
    ok &= expect_error("for non-block new var no type",
        R"(int s = 0; for (int i = 0; i < 5; i++) newvar = i;)");

    // for 体内裸引用未声明变量
    ok &= expect_error("for non-block bare ref undeclared",
        R"(for (int i = 0; i < 5; i++) newvar;)");

    // ==== 应报错：switch case 体内引入新变量 ====

    ok &= expect_error("switch case non-block new var",
        R"(int x = 1; switch(x) { case 1: newvar = 10; break; })");

    ok &= expect_error("switch case non-block typed decl",
        R"(int x = 1; switch(x) { case 1: int y = 10; break; })");

    // ==== 不应报错：花括号体内定义变量合法 ====

    // if 加花括号
    ok &= expect_no_error("if block decl ok",
        R"(int a = 1; if (a) { int x = 0; })");

    // else 加花括号
    ok &= expect_no_error("else block decl ok",
        R"(int a = 1; if (a) { a = 0; } else { int x = 1; })");

    // while 加花括号
    ok &= expect_no_error("while block decl ok",
        R"(int i = 0; while (i < 3) { int x = i; i++; })");

    // for 加花括号
    ok &= expect_no_error("for block decl ok",
        R"(int s = 0; for (int i = 0; i < 5; i++) { int x = i; s += x; } return s;)");

    // switch case 内用 {} 包裹合法
    ok &= expect_no_error("switch case block decl ok",
        R"(int x = 1; switch(x) { case 1: { int y = 10; } break; })");

    // ==== 不应报错：非封闭体内赋值/引用已有变量合法 ====

    ok &= expect_no_error("if non-block assign ok",
        R"(int x = 0; int a = 1; if (a) x = 1; return x;)");

    // if 体内裸引用已声明变量：合法
    ok &= expect_no_error("if non-block bare ref declared ok",
        R"(int x = 0; int a = 1; if (a) x; return x;)");

    ok &= expect_no_error("else non-block assign ok",
        R"(int x = 0; int a = 0; if (a) x = 1; else x = 2; return x;)");

    ok &= expect_no_error("while non-block assign ok",
        R"(int i = 0; while (i < 3) i++; return i;)");

    ok &= expect_no_error("for non-block assign ok",
        R"(int s = 0; for (int i = 0; i < 5; i++) s += i; return s;)");

    // switch case 内赋值已有变量合法
    ok &= expect_no_error("switch case assign existing ok",
        R"(int x = 1; int r = 0; switch(x) { case 1: r = 10; break; default: r = 20; } return r;)");

    // else if 链（仅2层，无悬空 else）不应误报非封闭声明错误
    ok &= expect_no_error("else if chain ok",
        R"(int a = 2; int r = 0; if (a == 1) r = 10; else if (a == 2) r = 20; return r;)");

    return ok;
}

bool struct_test()
{
    // 基本字段读写
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Point { int x; int y; };
            Point p;
            p.x = 10;
            p.y = 20;
            return p.x + p.y;
        )");
        if (!o.isNumber() || o.toDouble() != 30)
        {
            return false;
        }
    }
    // struct 多字段
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Vec { int x; int y; int z; };
            Vec v;
            v.x = 1; v.y = 2; v.z = 3;
            return v.x * 100 + v.y * 10 + v.z;
        )");
        if (!o.isNumber() || o.toDouble() != 123)
        {
            return false;
        }
    }
    // struct 字段复合赋值
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Counter { int n; };
            Counter cnt;
            cnt.n = 5;
            cnt.n += 3;
            return cnt.n;
        )");
        if (!o.isNumber() || o.toDouble() != 8)
        {
            return false;
        }
    }
    // 函数中使用 struct
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Rect { int w; int h; };
            area(r) { return r.w * r.h; }
            Rect r;
            r.w = 4; r.h = 5;
            return area(r);
        )");
        if (!o.isNumber() || o.toDouble() != 20)
        {
            return false;
        }
    }
    // 全局 struct 定义在执行后注册到 Cifa，可供后续脚本使用
    {
        Cifa c;
        c.run_script("struct Point { int x; int y; };");
        auto o = c.run_script("Point p; p.x = 3; p.y = 7; return p.x + p.y;");
        if (!o.isNumber() || o.toDouble() != 10)
        {
            return false;
        }
    }
    return true;
}

bool sprintf_format_test()
{
    // --- sprintf ---
    // %s 字符串
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("Hello %s!", "World");)");
        if (!o.isType<std::string>() || o.toString() != "Hello World!")
        {
            return false;
        }
    }
    // %d 整数和 %.2f 浮点
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%d + %.2f = %.2f", 3, 1.5, 4.5);)");
        if (!o.isType<std::string>() || o.toString() != "3 + 1.50 = 4.50")
        {
            return false;
        }
    }
    // 连续说明符和 %% 混用：%% 不消耗参数，其他说明符按顺序各消耗一个参数
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%s%d%.1f%%-%x", "A", 2, 3.5, 255);)");
        if (!o.isType<std::string>() || o.toString() != "A23.5%-ff")
        {
            return false;
        }
    }
    // %% 转义
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("100%%");)");
        if (!o.isType<std::string>() || o.toString() != "100%")
        {
            return false;
        }
    }
    // 奇数个 %：前 12 个组成 6 个 %% ，末尾不完整的 % 被忽略；与 MSVC/UCRT 当前行为一致
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("100%%%%%%%%%%%%%");)");
        if (!o.isType<std::string>() || o.toString() != "100%%%%%%")
        {
            return false;
        }
    }
    // %x 十六进制
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%x", 255);)");
        if (!o.isType<std::string>() || o.toString() != "ff")
        {
            return false;
        }
    }
    // %llu 多字母长度修饰符（用户显式写 ll，应正常工作）
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%llu", 12345);)");
        if (!o.isType<std::string>() || o.toString() != "12345")
        {
            return false;
        }
    }
    // %05.1f 宽度+精度（浮点，无长度修饰符）
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%08.2f", 3.14);)");
        if (!o.isType<std::string>() || o.toString() != "00003.14")
        {
            return false;
        }
    }
    // --- format ---
    // 自动 {}
    {
        Cifa c;
        auto o = c.run_script(R"(return format("Hello {}!", "World");)");
        if (!o.isType<std::string>() || o.toString() != "Hello World!")
        {
            return false;
        }
    }
    // 显式索引 {N}
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{1} and {0}", "B", "A");)");
        if (!o.isType<std::string>() || o.toString() != "A and B")
        {
            return false;
        }
    }
    // 整数数字不带小数点
    {
        Cifa c;
        auto o = c.run_script(R"(return format("x = {}", 42);)");
        if (!o.isType<std::string>() || o.toString() != "x = 42")
        {
            return false;
        }
    }
    // 浮点数字
    {
        Cifa c;
        auto o = c.run_script(R"(return format("pi = {}", 3.14);)");
        if (!o.isType<std::string>() || o.toString() != "pi = 3.14")
        {
            return false;
        }
    }
    // {{ }} 转义
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{{{}}}", "ok");)");
        if (!o.isType<std::string>() || o.toString() != "{ok}")
        {
            return false;
        }
    }
    // {:格式说明符} 自动索引
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:.2f}", 3.14159);)");
        if (!o.isType<std::string>() || o.toString() != "3.14")
        {
            return false;
        }
    }
    // {N:格式说明符} 显式索引
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{0:d} / {1:.1f}", 7, 3.0);)");
        if (!o.isType<std::string>() || o.toString() != "7 / 3.0")
        {
            return false;
        }
    }
    // {:s} 字符串格式
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:s}", "hi");)");
        if (!o.isType<std::string>() || o.toString() != "hi")
        {
            return false;
        }
    }
    // {:.2} 无类型尾缀 — 数字按 %g 保留2位有效数字
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:.2}", 3.14159);)");
        if (!o.isType<std::string>() || o.toString() != "3.1")
        {
            return false;
        }
    }
    // {:>8} 无类型尾缀 — 字符串按 %s 右对齐（snprintf %s 不支持对齐，直接透传）
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:5}", 42);)");
        // 宽度5，数字补 g → "   42" 或 "42" 均可，只要不崩溃且包含 "42"
        if (!o.isType<std::string>() || o.toString().find("42") == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

bool include_file_cases_test()
{
    const std::pair<const char*, double> cases[] = {
        { "include_simple.cifa", 15 },
        { "include_multi.cifa", 17 },
        { "c.cifa", 201 },
        { "self.cifa", 1 },
        { "cycle_a.cifa", 1 },
        { "include_subdir.cifa", 42 },
        { "angle_bracket.cifa", 10 },
        { "include_in_function.cifa", 25 }
    };
    bool ok = true;
    for (const auto& [file, expected] : cases)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_file(std::string("unit_test/test_data/") + file);
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toDouble() != expected)
        {
            std::print(stderr, "Include file failed: {}\n{}{}", file,
                interpreter.get_errors_str(), interpreter.get_runtime_error());
            ok = false;
        }
    }
    return ok;
}

bool include_missing_file_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_file("unit_test/test_data/missing.cifa");
    return c.has_error();
}

bool include_with_parameters_test()
{
    Cifa c;
    c.register_parameter("base", Object(100.0));
    auto o = c.run_file("unit_test/test_data/with_params.cifa");
    return o.isNumber() && o.toDouble() == 110.0;
}

bool include_run_script_include_dir_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data" });
    std::string script = "#include \"simple.cifa\"\nint y = x + 5;\nreturn y;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 15.0;
}

bool include_run_script_default_dir_test()
{
    Cifa c;
    std::string script = "#include \"unit_test/test_data/simple.cifa\"\nreturn x + 5;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 15.0;
}

bool include_run_script_include_dir_multi_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data" });
    std::string script = "#include \"lib_math.cifa\"\nreturn square(3) + cube(2);\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 17.0;
}

bool include_run_script_include_dir_with_params_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data" });
    c.register_parameter("base", Object(100.0));
    std::string script = "#include \"simple.cifa\"\nreturn base + x;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 110.0;
}

bool include_multi_search_dirs_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data/missing_dir", "unit_test/test_data" });
    std::string script = "#include \"simple.cifa\"\nreturn x + 7;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 17.0;
}

bool include_absolute_path_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data/missing_dir" });
    std::filesystem::path path = std::filesystem::absolute("unit_test/test_data/simple.cifa");
    std::string path_str = path.generic_string();
    std::string script = "#include \"" + path_str + "\"\nreturn x + 8;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 18.0;
}

bool include_error_location_in_included_file_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_file("unit_test/test_data/include_bad_syntax.cifa");
    std::string errors = c.get_errors_str();
    auto error_list = c.get_errors();
    return errors.find("unit_test/test_data/bad_syntax_include.cifa:2") != std::string::npos
        && errors.find("int y = undef_from_include;") != std::string::npos
        && error_list.size() == 1
        && error_list[0].filename == "unit_test/test_data/bad_syntax_include.cifa"
        && error_list[0].line == 2;
}

bool include_error_location_after_include_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_file("unit_test/test_data/include_then_bad_main.cifa");
    std::string errors = c.get_errors_str();
    auto error_list = c.get_errors();
    return errors.find("unit_test/test_data/include_then_bad_main.cifa:3") != std::string::npos
        && errors.find("int b = undef_after_include;") != std::string::npos
        && error_list.size() == 1
        && error_list[0].filename == "unit_test/test_data/include_then_bad_main.cifa"
        && error_list[0].line == 3;
}

bool include_test()
{
    struct IncludeCase
    {
        const char* name;
        bool (BackendTests::*test)();
    };
    const IncludeCase cases[] = {
        { "file cases", &BackendTests::include_file_cases_test },
        { "missing file", &BackendTests::include_missing_file_test },
        { "with parameters", &BackendTests::include_with_parameters_test },
        { "run_script include dir", &BackendTests::include_run_script_include_dir_test },
        { "run_script default dir", &BackendTests::include_run_script_default_dir_test },
        { "run_script include dir multi", &BackendTests::include_run_script_include_dir_multi_test },
        { "run_script include dir with parameters", &BackendTests::include_run_script_include_dir_with_params_test },
        { "multiple search directories", &BackendTests::include_multi_search_dirs_test },
        { "absolute path", &BackendTests::include_absolute_path_test },
        { "error location in included file", &BackendTests::include_error_location_in_included_file_test },
        { "error location after include", &BackendTests::include_error_location_after_include_test },
    };

    bool ok = true;
    for (const auto& include_case : cases)
    {
        if (!(this->*include_case.test)())
        {
            std::println(stderr, "  include case failed: {}", include_case.name);
            ok = false;
        }
    }
    return ok;
}

#undef Cifa
};

using DirectTests = BackendTests<DirectCifa>;
using BytecodeTests = BackendTests<TestBytecode>;

bool direct_source_map_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_script("return 1;");
    c.run_script("double cached_value; return cached_value * 2;");
    const std::string error = c.get_runtime_error();
    return error.find("variable 'cached_value' has not been initialized") != std::string::npos
        && error.find("double cached_value; return cached_value * 2;") != std::string::npos;
}

bool nested_ast_function_lookup_test()
{
    Cifa c;
    c.set_output_error(false);
    c.register_function("run_child", [&c](ObjectVector&) -> Object
        {
            return c.run_script("return value(5);");
        });
    auto result = c.run_script("value(n) { return n + 1; } return run_child();");
    return result.isNumber() && result.toInt() == 6 && !c.has_runtime_error();
}

bool global_definition_scope_test()
{
    Cifa c;
    c.set_output_error(false);

    c.run_script("if (1) { nested() { return 1; } }");
    if (!c.has_error() || c.get_errors_str().find("script function 'nested' is only allowed in global scope") == std::string::npos)
    {
        return false;
    }

    c.run_script("if (1) { struct Local { int value; }; }");
    if (!c.has_error() || c.get_errors_str().find("struct 'Local' is only allowed in global scope") == std::string::npos)
    {
        return false;
    }

    c.run_script("global_fn(value) { return value + 1; } struct Global { int value; }; return 0;");
    if (c.has_error() || c.has_runtime_error()) return false;
    auto global_definitions = c.run_script("Global item; item.value = 5; return global_fn(item.value);");
    if (!global_definitions.isNumber() || global_definitions.toInt() != 6)
    {
        return false;
    }

    c.run_script("global_value = 1; { local_value = 2; global_value = 3; }");
    auto global_value = c.run_script("return global_value;");
    if (!global_value.isNumber() || global_value.toInt() != 3)
    {
        return false;
    }
    c.run_script("return local_value;");
    return c.has_error() && c.get_errors_str().find("local_value") != std::string::npos;
}

bool nested_script_global_scope_test()
{
    Cifa c;
    c.set_output_error(false);
    c.register_function("run_child", [&c](ObjectVector&) -> Object
        {
            return c.run_script("child_global = 5; child_function() { return child_global + 1; } exit(); child_global = 99;");
        });

    auto outer_result = c.run_script("outer_global = 10; run_child(); outer_global += 1; return outer_global;");
    if (!outer_result.isNumber() || outer_result.toInt() != 11)
    {
        return false;
    }
    auto persisted_child = c.run_script("return child_global * 100 + child_function();");
    if (!persisted_child.isNumber() || persisted_child.toInt() != 506)
    {
        return false;
    }

    c.register_function("read_outer_local", [&c](ObjectVector&) -> Object
        {
            return c.run_script("return outer_local;");
        });
    c.run_script("{ outer_local = 7; read_outer_local(); }");
    return c.has_error() && c.get_errors_str().find("outer_local") != std::string::npos;
}

bool nested_runtime_reporter_test()
{
    Cifa outer;
    Cifa inner;
    outer.set_output_error(false);
    inner.set_output_error(false);
    outer.register_function("run_inner", [&inner](ObjectVector&) -> Object
        {
            inner.run_script("double inner_value; return inner_value * 2;");
            return Object();
        });

    outer.run_script("run_inner(); double outer_value; return outer_value * 2;");
    const std::string outer_error = outer.get_runtime_error();
    const std::string inner_error = inner.get_runtime_error();
    return outer_error.find("variable 'outer_value'") != std::string::npos
        && inner_error.find("variable 'inner_value'") != std::string::npos;
}

bool diagnostic_position_test()
{
    struct Case
    {
        std::string script;
        std::string token;
        bool syntax;
    };
    const Case cases[] = {
        { "bad = \"abc\"; if (bad) return 1;", "bad)", false },
        { "bad = \"abc\"; while (bad) {}", "bad)", false },
        { "bad = \"abc\"; for (;bad;) {}", "bad;", false },
        { "bad = \"abc\"; do {} while (bad);", "bad)", false },
        { "for (item : 42) {}", "42", false },
        { "values = {1}; values.keys();", "keys", false },
        { "value = 1; value.clear();", "clear", false },
        { "return size(42);", "42", false },
        { "return random(1, 2, 3);", "random", false },
        { "return missing;", "missing", true },
        { "return unknown(1);", "unknown", true },
        { "return #bad;", "#", true },
        { "int x = (1 + 2));", ");", true },
        { "123 = 5;", "123", true }
    };
    for (const auto& test : cases)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        interpreter.run_script(test.script);
        const auto error = test.syntax ? interpreter.get_errors_str() : interpreter.get_runtime_error();
        if (test.script == "return missing;"
            && error.find("parameter 'missing' has not been initialized") == std::string::npos)
        {
            return false;
        }
        const auto column = test.script.find(test.token) + 1;
        const std::string header = "  at <script>:1, col " + std::to_string(column) + ": ";
        const std::string expected = header + test.script + "\n"
            + std::string(header.size() + column - 1, ' ') + "^\n";
        if (error.find(expected) == std::string::npos
            || (test.syntax ? !interpreter.has_error() : !interpreter.has_runtime_error()))
        {
            std::print(stderr, "Diagnostic position mismatch:\n{}Expected:\n{}", error, expected);
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    configure_test_process();
    if (argc > 1 && std::string(argv[1]) == "--pool")
        cifa::test_resource = std::make_shared<std::pmr::unsynchronized_pool_resource>();
    struct ExplicitPmrResources {
        std::pmr::memory_resource* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
        ~ExplicitPmrResources() { std::pmr::set_default_resource(previous); }
    } explicit_pmr_resources;
    if (argc > 1 && std::string(argv[1]) == "--error-checks")
    {
        DirectTests direct;
        return direct.object_conversion_fallback_test() && direct.runtime_error_abort_test()
            && direct.object_vector_argument_error_test() && direct.script_function_return_check_test() ? 0 : 1;
    }

    int total = 0, ok = 0;
    DirectTests direct;
    BytecodeTests bytecode;
    auto run_direct_test = [&total, &ok](std::string name, bool (*test)())
    {
        total++;
        if (test())
        {
            ok++;
            std::println("[PASS] {}. {} success", total, name);
        }
        else
        {
            std::println("[FAIL] {}. {} failed", total, name);
        }
    };

    auto run_common_test = [&total, &ok, &direct, &bytecode](std::string name, auto direct_test, auto bytecode_test)
    {
        total++;
        const bool direct_passed = (direct.*direct_test)();
        const bool bytecode_passed = (bytecode.*bytecode_test)();
        if (direct_passed && bytecode_passed)
        {
            ok++;
            std::println("[PASS] {}. {} success", total, name);
        }
        else
        {
            std::println(stderr, "  backend results: Cifa={}, TestBytecode={}", direct_passed, bytecode_passed);
            std::println("[FAIL] {}. {} failed", total, name);
        }
    };
    auto run_runtime_error_parity_test = [&total, &ok](std::string name, const std::string& script, const std::string& expected)
    {
        total++;
        Cifa direct_backend;
        TestBytecode bytecode_backend;
        direct_backend.set_output_error(false);
        bytecode_backend.set_output_error(false);
        const auto direct_result = direct_backend.run_script(script);
        const auto bytecode_result = bytecode_backend.run_script(script);
        const bool failed = direct_result.getSpecialType() == "Error" && bytecode_result.getSpecialType() == "Error"
            && direct_backend.has_runtime_error() && bytecode_backend.has_runtime_error()
            && direct_backend.get_runtime_error().find(expected) != std::string::npos
            && bytecode_backend.get_runtime_error().find(expected) != std::string::npos;
        const auto direct_recovery = direct_backend.run_script("return 42;");
        const auto bytecode_recovery = bytecode_backend.run_script("return 42;");
        const bool recovered = !direct_backend.has_runtime_error() && !bytecode_backend.has_runtime_error()
            && direct_recovery.isNumber() && bytecode_recovery.isNumber()
            && direct_recovery.toInt() == 42 && bytecode_recovery.toInt() == 42;
        if (failed && recovered)
        {
            ok++;
            std::println("[PASS] {}. {} success", total, name);
        }
        else std::println("[FAIL] {}. {} failed", total, name);
    };
    auto run_backend_test = [&total, &ok](std::string name, auto& backend, auto test)
    {
        total++;
        if ((backend.*test)())
        {
            ok++;
            std::println("[PASS] {}. {} success", total, name);
        }
        else std::println("[FAIL] {}. {} failed", total, name);
    };
    #define RUN_COMMON(name) run_common_test(#name, &DirectTests::name, &BytecodeTests::name)
    #define RUN_CIFA(name) run_backend_test("cifa_" #name, direct, &DirectTests::name)

    std::println("[Common tests]");
    RUN_COMMON(exit_function_test);
    RUN_COMMON(builtin_math_function_test);
    RUN_COMMON(builtin_type_function_test);
    RUN_COMMON(range_for_test);
    RUN_COMMON(goto_test);
    RUN_COMMON(loop_math_test);
    RUN_COMMON(loop_control_test);
    RUN_COMMON(control_state_test);
    RUN_COMMON(ternary_operator_test);
    RUN_COMMON(logical_short_circuit_test);
    RUN_COMMON(numeric_literal_radix_test);
    RUN_COMMON(switch_case_test);
    RUN_COMMON(recursion_test);
    RUN_COMMON(script_void_function_test);
    RUN_COMMON(script_function_return_check_test);
    RUN_COMMON(script_function_argument_count_test);
    RUN_COMMON(string_operation_test);
    RUN_COMMON(string_compare_test);
    RUN_COMMON(bitwise_operator_test);
    RUN_COMMON(scope_shadowing_test);
    RUN_COMMON(complex_math_priority_test);
    RUN_COMMON(same_precedence_left_assoc_test);
    RUN_COMMON(unary_minus_test);
    RUN_COMMON(array_access_test);
    RUN_COMMON(array_literal_assignment_test);
    RUN_COMMON(size_of_array_test);
    RUN_COMMON(register_vector_test);
    RUN_COMMON(register_map_test);
    RUN_COMMON(type_promotion_test);
    RUN_COMMON(typed_numeric_storage_test);
    RUN_COMMON(auto_type_inference_test);
    RUN_COMMON(c_style_cast_test);
    RUN_COMMON(integer_arithmetic_test);
    RUN_COMMON(typed_function_conversion_test);
    RUN_COMMON(typed_array_and_struct_test);
    RUN_COMMON(typed_conversion_error_test);
    RUN_COMMON(empty_statement_test);
    RUN_COMMON(else_if_chain_test);
    RUN_COMMON(multi_dimensional_array_test);
    RUN_COMMON(compound_assignment_test);
    RUN_COMMON(runtime_error_stack_test);
    RUN_COMMON(uninitialized_variable_runtime_test);
    RUN_COMMON(nested_execution_state_test);
    RUN_COMMON(nested_static_error_source_test);
    RUN_COMMON(mixed_array_literal_test);
    RUN_COMMON(string_key_map_test);
    RUN_COMMON(static_syntax_error_test);
    RUN_COMMON(loop_and_recursion_execution_test);
    RUN_COMMON(array_methods_test);
    RUN_COMMON(map_methods_test);
    RUN_COMMON(non_block_branch_declaration_test);
    RUN_COMMON(sprintf_format_test);
    RUN_COMMON(struct_test);
    RUN_COMMON(include_test);
    #undef RUN_COMMON

    std::println("[Runtime error parity]");
    run_runtime_error_parity_test("division by zero", "return 1 / 0;", "integer division by zero");
    run_runtime_error_parity_test("uninitialized variable", "int value; return value + 1;", "has not been initialized");

    std::println("[Cifa tests]");
    run_direct_test("diagnostic_position_test", diagnostic_position_test);
    run_direct_test("direct_source_map_test", direct_source_map_test);
    run_direct_test("nested_ast_function_lookup_test", nested_ast_function_lookup_test);
    run_direct_test("global_definition_scope_test", global_definition_scope_test);
    run_direct_test("nested_script_global_scope_test", nested_script_global_scope_test);
    run_direct_test("nested_runtime_reporter_test", nested_runtime_reporter_test);
    run_direct_test("object_conversion_fallback_test", +[]() { DirectTests direct; return direct.object_conversion_fallback_test(); });
    run_direct_test("custom_operator_dispatch_test", +[]() { DirectTests direct; return direct.custom_operator_dispatch_test(); });
    RUN_CIFA(register_function_test);
    RUN_CIFA(register_function_template_test);
    RUN_CIFA(registration_name_validation_test);
    RUN_CIFA(typed_function_argument_error_test);
    RUN_CIFA(object_vector_argument_error_test);
    RUN_CIFA(script_function_global_scope_test);
    RUN_CIFA(registered_type_binding_test);
    RUN_CIFA(int64_storage_test);
    RUN_CIFA(c_string_library_test);
    RUN_CIFA(method_receiver_error_order_test);
    RUN_CIFA(runtime_error_abort_test);
    RUN_CIFA(nested_error_preservation_test);

    #undef RUN_CIFA

    std::println("Passed {} out of {} tests.", ok, total);
    return ok == total ? 0 : 1;
}
