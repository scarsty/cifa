#include "Cifa.h"
#include <algorithm>
#include <bit>
#include <cerrno>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <limits>
#include <print>
#include <sstream>

namespace cifa
{

template <typename F>
class RaiiGuard
{
public:
    explicit RaiiGuard(F finish) : finish_(std::move(finish)) { }
    ~RaiiGuard() { finish_(); }

    RaiiGuard(const RaiiGuard&) = delete;
    RaiiGuard& operator=(const RaiiGuard&) = delete;

private:
    F finish_;
};

template <typename F>
RaiiGuard(F) -> RaiiGuard<F>;

static std::string normalize_path(const std::string& path);

static std::unordered_set<std::string> make_token_set(const std::vector<std::string>& tokens)
{
    return std::unordered_set<std::string>(tokens.begin(), tokens.end());
}

static std::unordered_set<std::string> make_token_set(const std::vector<std::vector<std::string>>& token_groups)
{
    std::unordered_set<std::string> tokens;
    for (const auto& group : token_groups)
    {
        tokens.insert(group.begin(), group.end());
    }
    return tokens;
}

bool Cifa::parse_number_literal(const std::string& text, Object& value)
{
    std::string normalized = text;
    const bool has_hex_prefix = normalized.size() > 2 && normalized[0] == '0'
        && (normalized[1] == 'x' || normalized[1] == 'X');
    const bool is_float_literal = (normalized.ends_with('f') || normalized.ends_with('F'))
        && (!has_hex_prefix || normalized.find_first_of(".pP") != std::string::npos);
    if (is_float_literal)
    {
        normalized.pop_back();
    }

    const bool is_hex = normalized.size() > 2 && normalized[0] == '0' && (normalized[1] == 'x' || normalized[1] == 'X');
    const bool is_binary = normalized.size() > 2 && normalized[0] == '0' && (normalized[1] == 'b' || normalized[1] == 'B');
    const bool is_octal = normalized.size() > 1 && normalized[0] == '0'
        && normalized.find_first_of(".eE") == std::string::npos;

    if (is_hex || is_binary || is_octal)
    {
        const char* digits = normalized.c_str() + (is_hex || is_binary ? 2 : 0);
        const int base = is_hex ? 16 : (is_binary ? 2 : 8);
        char* end = nullptr;
        errno = 0;
        const auto integer = std::strtoull(digits, &end, base);
        if (errno == ERANGE || end == digits || *end != '\0')
        {
            return false;
        }
        if (integer > static_cast<unsigned long long>(std::numeric_limits<std::int64_t>::max())) { return false; }
        value = Object(static_cast<std::int64_t>(integer));
        return true;
    }

    const bool has_fraction_or_exponent = normalized.find_first_of(".eE") != std::string::npos;
    if (!has_fraction_or_exponent)
    {
        char* end = nullptr;
        errno = 0;
        const auto integer = std::strtoll(normalized.c_str(), &end, 10);
        if (errno == ERANGE || end == normalized.c_str() || *end != '\0')
        {
            return false;
        }
        value = is_float_literal ? Object(static_cast<double>(integer)) : Object(static_cast<std::int64_t>(integer));
        return true;
    }

    char* end = nullptr;
    errno = 0;
    const double parsed = std::strtod(normalized.c_str(), &end);
    if (errno == ERANGE || end == normalized.c_str() || *end != '\0')
    {
        return false;
    }
    value = Object(parsed);
    return true;
}

static std::string object_to_display_string(const Object& value)
{
    if (value.isType<bool>()) { return value.toBool() ? "true" : "false"; }
    if (value.isInteger()) { return std::format("{}", value.toInt64()); }
    if (value.isType<double>()) { return std::format("{}", value.toDouble()); }
    if (value.isType<std::string>()) { return value.toString(); }
    return {};
}

static std::int64_t wrap_int64(std::uint64_t value)
{
    return std::bit_cast<std::int64_t>(value);
}

static bool numeric_less(const Object& left, const Object& right)
{
    return left.isInteger() && right.isInteger()
        ? left.toInt64() < right.toInt64() : left.toDouble() < right.toDouble();
}

//构造函数：注册内置函数（print, println, 数学函数等）
Cifa::Cifa()
{
    register_type<std::int64_t>("int");
    register_type<double>("double");
    register_type<bool>("bool");
    register_type<std::string>("string");
    type_names.emplace(typeid(ObjectVector), "array");
    type_names.emplace(typeid(ObjectMap), "map");
    register_type<double>("float");
    register_type<std::int64_t>("char");
    //输出辅助：先检查数字再检查字符串，不可输出类型调 toString 使报错显示 "<empty> to string"
    //返回 false 表示遇到了不可输出的类型（已触发 runtime error）
    auto print_object = [](const Object& d1) -> bool
    {
        if (d1.isNumber())
        {
            std::print("{}", object_to_display_string(d1));
            return true;
        }
        if (d1.isType<std::string>())
        {
            std::print("{}", d1.toString());
            return true;
        }
        //不可输出类型：触发运行时错误，不输出值
        d1.toString();
        return false;
    };
    register_function("print", [print_object](ObjectVector& d)
        {
            for (auto& d1 : d)
            {
                if (!print_object(d1)) { break; }
            }
            return Object(double(d.size()));
        });
    register_function("println", [print_object](ObjectVector& d)
        {
            bool ok = true;
            for (auto& d1 : d)
            {
                if (!print_object(d1))
                {
                    ok = false;
                    break;
                }
            }
            //只有全部成功才输出换行，避免出错后多出空行
            if (ok) { std::print("\n"); }
            return Object(double(d.size()));
        });
    register_function("to_string", [](ObjectVector& d)
        {
            if (d.empty())
            {
                return Object("");
            }
            std::ostringstream stream;
            if (d[0].isNumber())
            {
                stream << object_to_display_string(d[0]);
            }
            else
            {
                stream << d[0].toString();
            }
            return Object(stream.str());
        });
    register_function("to_number", [](ObjectVector& d)
        {
            if (d.empty())
            {
                return Object();
            }
            return Object(atof(d[0].toString().c_str()));
        });
    register_function("type", [this](ObjectVector& d)
        {
            if (d.empty())
            {
                return Object(std::string("empty"));
            }
            if (!d[0].hasValue())
            {
                if (d[0].isTyped() && d[0].getDeclaredTypeName() != "auto")
                {
                    return Object(d[0].getDeclaredTypeName());
                }
                return Object(std::string("empty"));
            }
            if (find_struct_definition(d[0].getDeclaredTypeName()) != nullptr)
            {
                return Object(d[0].getDeclaredTypeName());
            }
            if (!d[0].getSpecialType().empty())
            {
                return Object(d[0].getSpecialType());
            }
            if (d[0].isNumber())
            {
                return Object(registered_type_name(d[0]));
            }
            if (d[0].isType<std::string>())
            {
                return Object(std::string("string"));
            }
            if (d[0].isType<std::vector<Object>>())
            {
                return Object(std::string("array"));
            }
            if (d[0].isType<ObjectMap>())
            {
                return Object(std::string("map"));
            }
            return Object(registered_type_name(d[0]));
        });
    register_function("run_string", [this](ObjectVector& d) -> Object
        {
            if (d.size() != 1)
            {
                set_runtime_error("function 'run_string' expects 1 argument, got " + std::to_string(d.size()));
                return Object();
            }
            return run_script(d[0].toString());
        });
    register_function("run_file", [this](ObjectVector& d) -> Object
        {
            if (d.size() != 1)
            {
                set_runtime_error("function 'run_file' expects 1 argument, got " + std::to_string(d.size()));
                return Object();
            }
            return run_file(d[0].toString());
        });
    register_function("exit", [this](ObjectVector&) -> Object
        {
            request_exit();
            return Object();
        });
    auto ifv = [](ObjectVector& x) -> Object
    {
        if (x.size() != 3) { return cifa::Object(); }
        return x[0].toBool() ? x[1] : x[2];
    };
    register_function("ifv", ifv);
    register_function("ifvalue", ifv);

    const auto extremum = []<bool maximum>(ObjectVector& arguments) -> Object
        {
            if (arguments.empty()) return Object();
            if (arguments.size() == 1) return arguments[0];
            if (!arguments[0].isNumber()) return arguments[0].to<double>();
            bool floating = arguments[0].isType<double>();
            size_t best = 0;
            for (size_t index = 1; index < arguments.size(); ++index)
            {
                if (!arguments[index].isNumber()) return arguments[index].to<double>();
                floating = floating || arguments[index].isType<double>();
                if constexpr (maximum)
                {
                    if (numeric_less(arguments[best], arguments[index])) best = index;
                }
                else
                {
                    if (numeric_less(arguments[index], arguments[best])) best = index;
                }
            }
            return floating ? Object(arguments[best].toDouble()) : Object(arguments[best].toInt64());
        };
    register_function("max", [extremum](ObjectVector& arguments)
        { return extremum.operator()<true>(arguments); });
    register_function("min", [extremum](ObjectVector& arguments)
        { return extremum.operator()<false>(arguments); });
    register_function("random", [this](ObjectVector& x) -> Object
        {
            if (x.size() == 0) { return Object(double(rand()) / RAND_MAX); }
            if (x.size() == 1)
            {
                return Object(double(rand()) / RAND_MAX * x[0].toDouble());
            }
            if (x.size() == 2)
            {
                double min_val = x[0].toDouble();
                double max_val = x[1].toDouble();
                return Object(min_val + double(rand()) / RAND_MAX * (max_val - min_val));
            }
            set_runtime_error("function 'random' expects 0 to 2 arguments, got " + std::to_string(x.size()));
            return Object();
        });
    register_function("size", [this](ObjectVector& x) -> Object
        {
            if (x.size() == 0) { return 0; }
            if (x.size() == 1)
            {
                if (x[0].isType<std::string>())
                {
                    return Object(double(x[0].toString().size()));
                }
                if (x[0].isType<std::vector<Object>>())
                {
                    return Object(double(x[0].ref<std::vector<Object>>().size()));
                }
                if (x[0].isType<ObjectMap>())
                {
                    return Object(double(x[0].ref<ObjectMap>().size()));
                }
                set_runtime_error("function 'size' requires a string, array, or map", &x[0]);
                return Object();
            }
            set_runtime_error("function 'size' expects 0 or 1 arguments, got " + std::to_string(x.size()));
            return Object();
        });
    // 按 printf 格式说明符将一个 Object 转为字符串
    // spec 含用户原始长度修饰符（如 %llu），此处统一剥离后按类型重新添加
    auto sprintf_sub = [](const Object& arg, const std::string& spec, char tc) -> std::string
    {
        char buf[512] = { };
        // 剥离用户写的长度修饰符（hlLzjtq），保留 %、flags、width、.prec
        static const std::string len_mods = "hlLzjtq";
        std::string base;    // '%' + flags + width + .prec，不含长度修饰符和类型字符
        base += spec[0];     // '%'
        for (size_t k = 1; k + 1 < spec.size(); ++k)
        {
            if (len_mods.find(spec[k]) == std::string::npos)
            {
                base += spec[k];
            }
        }
        if (tc == 's')
        {
            snprintf(buf, sizeof(buf), (base + 's').c_str(), arg.toString().c_str());
        }
        else if (tc == 'd' || tc == 'i')
        {
            snprintf(buf, sizeof(buf), (base + (tc == 'd' ? "lld" : "lli")).c_str(), static_cast<long long>(arg.toInt64()));
        }
        else if (tc == 'u' || tc == 'o' || tc == 'x' || tc == 'X')
        {
            snprintf(buf, sizeof(buf), (base + "ll" + tc).c_str(), static_cast<unsigned long long>(arg.toInt64()));
        }
        else    // %f %e %E %g %G %a；整数类型和 float 会按 C 的默认提升传给 printf
        {
            snprintf(buf, sizeof(buf), (base + tc).c_str(), arg.toDouble());
        }
        return buf;
    };
    register_function("sprintf", [sprintf_sub](ObjectVector& x) -> Object
        {
            if (x.empty())
            {
                return Object(std::string(""));
            }
            std::string fmt = x[0].toString();
            std::string result;
            size_t arg_idx = 1;
            for (size_t i = 0; i < fmt.size();)
            {
                if (fmt[i] != '%')
                {
                    result += fmt[i++];
                    continue;
                }
                // %% -> 字面 %
                if (i + 1 < fmt.size() && fmt[i + 1] == '%')
                {
                    result += '%';
                    i += 2;
                    continue;
                }
                // 解析: %[flags][width][.prec]type
                size_t spec_start = i++;
                while (i < fmt.size() && std::string("-+ #0").find(fmt[i]) != std::string::npos)
                {
                    ++i;    // flags
                }
                while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9')
                {
                    ++i;    // width
                }
                if (i < fmt.size() && fmt[i] == '.')
                {
                    ++i;
                    while (i < fmt.size() && fmt[i] >= '0' && fmt[i] <= '9')
                    {
                        ++i;
                    }
                }    // .prec
                // 跳过长度修饰符（h hh l ll L z j t q），tc 取真正的转换字符
                while (i < fmt.size() && std::string("hlLzjtq").find(fmt[i]) != std::string::npos)
                {
                    ++i;
                }
                if (i >= fmt.size())
                {
                    break;
                }
                char tc = fmt[i++];
                if (arg_idx < x.size())
                {
                    result += sprintf_sub(x[arg_idx++], fmt.substr(spec_start, i - spec_start), tc);
                }
            }
            return Object(result);
        });

    // 将 Object 转为字符串
    // fspec 为空：按对象真实数值类型格式化
    // fspec 非空：格式字串是运行时値，必须用 std::vformat；按末尾字符选择传入的原生类型
    auto format_sub = [](const Object& arg, const std::string& fspec = { }) -> std::string
    {
        if (fspec.empty())
        {
            if (arg.isNumber())
            {
                if (arg.isType<std::int64_t>())
                {
                    return std::format("{}", arg.toInt64());
                }
                if (arg.isType<bool>())
                {
                    return arg.toBool() ? "true" : "false";
                }
                return std::format("{}", arg.toDouble());
            }
            std::string s = arg.toString();
            return std::format("{}", s);
        }
        std::string fmt_str = "{:" + fspec + "}";
        char last = fspec.back();
        if (!arg.isNumber() || last == 's')
        {
            std::string sv = arg.toString();
            return std::vformat(fmt_str, std::make_format_args(sv));
        }
        if (std::string_view("diouxXbB").find(last) != std::string_view::npos)
        {
            long long iv = arg.toInt64();
            return std::vformat(fmt_str, std::make_format_args(iv));
        }
        double dv = arg.toDouble();
        return std::vformat(fmt_str, std::make_format_args(dv));
    };
    register_function("format", [format_sub](ObjectVector& x) -> Object
        {
            if (x.empty())
            {
                return Object(std::string(""));
            }
            std::string fmt = x[0].toString();
            std::string result;
            size_t auto_idx = 0;
            for (size_t i = 0; i < fmt.size();)
            {
                if (fmt[i] == '{')
                {
                    // {{ -> 字面 {
                    if (i + 1 < fmt.size() && fmt[i + 1] == '{')
                    {
                        result += '{';
                        i += 2;
                        continue;
                    }
                    size_t end = fmt.find('}', i + 1);
                    if (end == std::string::npos)
                    {
                        result += fmt[i++];
                        continue;
                    }
                    std::string inner = fmt.substr(i + 1, end - i - 1);
                    // 分离 [index][:format_spec]
                    std::string idx_part = inner;
                    std::string fspec;
                    size_t colon = inner.find(':');
                    if (colon != std::string::npos)
                    {
                        idx_part = inner.substr(0, colon);
                        fspec = inner.substr(colon + 1);
                    }
                    size_t idx;
                    if (idx_part.empty())
                    {
                        idx = auto_idx++;
                    }
                    else
                    {
                        bool is_num = true;
                        for (char c : idx_part)
                        {
                            if (c < '0' || c > '9')
                            {
                                is_num = false;
                                break;
                            }
                        }
                        if (!is_num)
                        {
                            result += fmt[i++];
                            continue;
                        }    // 未知说明符，逐字输出
                        idx = (size_t)std::stoul(idx_part);
                    }
                    size_t arg_pos = idx + 1;
                    if (arg_pos < x.size())
                    {
                        result += format_sub(x[arg_pos], fspec);
                    }
                    i = end + 1;
                }
                else if (fmt[i] == '}' && i + 1 < fmt.size() && fmt[i + 1] == '}')
                {
                    // }} -> 字面 }
                    result += '}';
                    i += 2;
                }
                else
                {
                    result += fmt[i++];
                }
            }
            return Object(result);
        });

    register_function("abs", [](ObjectVector& x) -> Object
        {
            if (x.size() != 1) { return Object(); }
            if (x[0].isInteger())
            {
                const auto value = x[0].toInt64();
                if (value == std::numeric_limits<std::int64_t>::min())
                {
                    return Object();
                }
                return Object(value < 0 ? -value : value);
            }
            if (x[0].isType<double>())
            {
                return Object(std::fabs(x[0].toDouble()));
            }
            return x[0].to<double>();
        });

#define REGISTER_MATH1(func) register_function(#func, static_cast<double (*)(double)>(&std::func))
#define REGISTER_MATH2(func) register_function(#func, static_cast<double (*)(double, double)>(&std::func))
    REGISTER_MATH1(sqrt);
    REGISTER_MATH1(cbrt);
    REGISTER_MATH1(round);
    REGISTER_MATH1(trunc);
    REGISTER_MATH1(nearbyint);
    REGISTER_MATH1(rint);
    REGISTER_MATH1(ceil);
    REGISTER_MATH1(floor);
    REGISTER_MATH1(sin);
    REGISTER_MATH1(cos);
    REGISTER_MATH1(tan);
    REGISTER_MATH1(asin);
    REGISTER_MATH1(acos);
    REGISTER_MATH1(atan);
    REGISTER_MATH2(atan2);
    REGISTER_MATH1(sinh);
    REGISTER_MATH1(cosh);
    REGISTER_MATH1(tanh);
    REGISTER_MATH1(exp);
    REGISTER_MATH1(log);
    REGISTER_MATH1(log2);
    REGISTER_MATH1(log10);
    REGISTER_MATH2(pow);
    REGISTER_MATH2(hypot);
    REGISTER_MATH2(fmod);
    REGISTER_MATH2(remainder);
    REGISTER_MATH1(erf);
    REGISTER_MATH1(erfc);
    REGISTER_MATH1(tgamma);
    REGISTER_MATH1(lgamma);
    REGISTER_MATH2(copysign);
    REGISTER_MATH2(fdim);
    REGISTER_MATH2(fmax);
    REGISTER_MATH2(fmin);
#undef REGISTER_MATH2
#undef REGISTER_MATH1
    builtin_function_generations = function_generations;
}

const std::unordered_set<std::string>& Cifa::keyword_tokens()
{
    static const auto tokens = []()
        {
            std::unordered_set<std::string> result;
            for (const auto& group : keys)
            {
                result.insert(group.begin(), group.end());
            }
            return result;
        }();
    return tokens;
}

const std::unordered_set<std::string>& Cifa::operator_tokens()
{
    static const auto tokens = make_token_set(ops);
    return tokens;
}

const std::vector<std::unordered_set<std::string>>& Cifa::operator_precedence_token_groups()
{
    static const auto groups = []()
        {
            std::vector<std::unordered_set<std::string>> result;
            result.reserve(ops.size());
            for (const auto& group : ops)
            {
                result.push_back(make_token_set(group));
            }
            return result;
        }();
    return groups;
}

Cifa::ErrorSet& Cifa::active_errors()
{
    return execution_contexts.empty() ? errors : execution_contexts.back().errors;
}

const Cifa::ErrorSet& Cifa::active_errors() const
{
    return execution_contexts.empty() ? errors : execution_contexts.back().errors;
}

const std::vector<SourceLineInfo>& Cifa::active_source_line_infos() const
{
    static const std::vector<SourceLineInfo> empty;
    if (compiling)
    {
        return compilation_source_line_infos;
    }
    if (execution_contexts.empty())
    {
        return empty;
    }
    return execution_contexts.back().source_line_infos;
}

void Cifa::record_error(ErrorMessage error)
{
    active_errors().emplace(std::move(error));
    if (compiling)
    {
        compile_failed = true;
    }
}

FunctionOverloads* Cifa::find_script_function(const std::string& name)
{
    if (compiling)
    {
        auto function = compilation_functions.find(name);
        if (function != compilation_functions.end())
        {
            return &function->second;
        }
        if (compile_visible_functions != nullptr)
        {
            const auto visible = compile_visible_functions->find(name);
            if (visible != compile_visible_functions->end()) return const_cast<FunctionOverloads*>(&visible->second);
        }
    }
    else if (!execution_contexts.empty())
    {
        auto& context = execution_contexts.back();
        auto function = context.functions.find(name);
        if (function != context.functions.end())
        {
            return &function->second;
        }
    }
    auto function = functions2.find(name);
    return function != functions2.end() ? &function->second : nullptr;
}

const std::vector<StructField>* Cifa::find_struct_definition(const std::string& name) const
{
    if (compiling)
    {
        auto definition = compilation_struct_defs.find(name);
        if (definition != compilation_struct_defs.end())
        {
            return &definition->second;
        }
        if (compile_visible_struct_defs != nullptr)
        {
            definition = compile_visible_struct_defs->find(name);
            if (definition != compile_visible_struct_defs->end()) return &definition->second;
        }
    }
    else if (!execution_contexts.empty())
    {
        const auto& context = execution_contexts.back();
        auto definition = context.struct_defs.find(name);
        if (definition != context.struct_defs.end())
        {
            return &definition->second;
        }
    }
    auto definition = struct_defs.find(name);
    return definition != struct_defs.end() ? &definition->second : nullptr;
}

bool Cifa::has_error() const
{
    return !active_errors().empty();
}

void Cifa::request_exit()
{
    if (!execution_contexts.empty())
    {
        execution_contexts.back().exit_requested = true;
    }
    else
    {
        last_exit_requested = true;
    }
}

bool Cifa::is_exit_requested() const
{
    return execution_contexts.empty() ? last_exit_requested : execution_contexts.back().exit_requested;
}

bool Cifa::has_runtime_error() const
{
    return execution_contexts.empty() ? !runtime_error_message.empty() : !execution_contexts.back().runtime_error_message.empty();
}

//从局部作用域栈的最内层向外查找变量，最后查找实例全局变量表
Object* Cifa::find_object_from_inner(ScopeStack& scopes, const std::string& name)
{
    for (auto it = scopes.rbegin(); it != scopes.rend(); ++it)
    {
        auto it_obj = it->find(name);
        if (it_obj != it->end())
        {
            return &it_obj->second;
        }
    }
    auto global = global_variables.find(name);
    if (global != global_variables.end())
    {
        return &global->second;
    }
    return nullptr;
}

//返回值属于执行/函数调用控制状态，不存放在变量作用域栈中
bool Cifa::has_return_value() const
{
    const auto& return_states = execution_contexts.back().return_states;
    return !return_states.empty() && return_states.back().has_value;
}

void Cifa::set_control_flow(ControlFlow flow, std::string label)
{
    auto& context = execution_contexts.back();
    context.control_flow = flow;
    context.goto_label = std::move(label);
}

bool Cifa::consume_control_flow(ControlFlow flow)
{
    auto& context = execution_contexts.back();
    if (context.control_flow != flow) return false;
    context.control_flow = ControlFlow::None;
    context.goto_label.clear();
    return true;
}

//获取当前执行或函数调用的返回值引用
Object& Cifa::return_value()
{
    auto& return_states = execution_contexts.back().return_states;
    if (return_states.empty())
    {
        return_states.emplace_back();
    }
    return_states.back().has_value = true;
    return return_states.back().value;
}

//对数组或 map 对象执行内置方法（push_back / erase / contains 等）
//obj 必须是对原始变量的引用；args 是已展开的参数列表（CalUnit）
Object Cifa::eval_builtin_method(const CalUnit& method, Object& obj, std::vector<CalUnit>& args, ScopeStack& scopes)
{
    const auto& method_name = method.str;
    if (obj.isType<std::vector<Object>>())
    {
        auto& arr = obj.ref<std::vector<Object>>();
        if (method_name == "push_back")
        {
            for (auto& a : args)
            {
                Object value = eval_scoped(a, scopes);
                if (!obj.element_type_name.empty())
                {
                    value = convert_object_type(value, obj.element_type_name, &a);
                    if (has_runtime_error()) { return Object(); }
                }
                arr.push_back(std::move(value));
            }
            return Object(double(arr.size()));
        }
        if (method_name == "pop_back")
        {
            if (!arr.empty()) { arr.pop_back(); }
            return Object(double(arr.size()));
        }
        if (method_name == "resize")
        {
            if (!args.empty())
            {
                arr.resize(size_t(eval_scoped(args[0], scopes).toInt()));
                if (!obj.element_type_name.empty()) { set_array_element_type(obj, obj.element_type_name); }
            }
            return Object(double(arr.size()));
        }
        if (method_name == "reserve")
        {
            if (!args.empty()) { arr.reserve(size_t(eval_scoped(args[0], scopes).toInt())); }
            return Object(double(arr.size()));
        }
        if (method_name == "insert")
        {
            if (args.size() >= 2)
            {
                int idx = eval_scoped(args[0], scopes).toInt();
                if (idx < 0)
                {
                    idx = 0;
                }
                if (idx > (int)arr.size())
                {
                    idx = (int)arr.size();
                }
                Object value = eval_scoped(args[1], scopes);
                if (!obj.element_type_name.empty())
                {
                    value = convert_object_type(value, obj.element_type_name, &args[1]);
                    if (has_runtime_error()) { return Object(); }
                }
                arr.insert(arr.begin() + idx, std::move(value));
            }
            return Object(double(arr.size()));
        }
        if (method_name == "erase")
        {
            if (!args.empty())
            {
                int idx = eval_scoped(args[0], scopes).toInt();
                if (idx >= 0 && idx < (int)arr.size())
                {
                    arr.erase(arr.begin() + idx);
                }
            }
            return Object(double(arr.size()));
        }
        if (method_name == "clear")
        {
            arr.clear();
            return Object(0.0);
        }
        if (method_name == "contains")
        {
            if (!args.empty())
            {
                auto val = eval_scoped(args[0], scopes);
                for (auto& e : arr)
                {
                    if (equal(e, val)) { return Object(1.0); }
                }
            }
            return Object(0.0);
        }
        if (method_name == "keys")
        {
            set_runtime_error("keys() is not supported on arrays", nullptr, &method);
            return Object();
        }
    }
    else if (obj.isType<ObjectMap>())
    {
        auto& m = obj.ref<ObjectMap>();
        if (method_name == "erase")
        {
            if (!args.empty())
            {
                auto key = eval_scoped(args[0], scopes).toString();
                m.erase(key);
            }
            return Object(double(m.size()));
        }
        if (method_name == "clear")
        {
            m.clear();
            return Object(0.0);
        }
        if (method_name == "contains")
        {
            if (!args.empty())
            {
                auto key = eval_scoped(args[0], scopes).toString();
                return Object(m.count(key) ? 1.0 : 0.0);
            }
            return Object(0.0);
        }
        if (method_name == "keys")
        {
            std::vector<Object> keys;
            for (auto& [k, v] : m)
            {
                keys.push_back(Object(k));
            }
            return Object(std::move(keys));
        }
        if (method_name == "push_back" || method_name == "pop_back"
            || method_name == "resize" || method_name == "reserve" || method_name == "insert")
        {
            set_runtime_error(method_name + "() is not supported on maps", nullptr, &method);
            return Object();
        }
    }
    else
    {
        set_runtime_error(method_name + "() requires an array or map", nullptr, &method);
    }
    return Object();
}

//核心求值函数：递归遍历语法树节点并执行对应操作
bool Cifa::eval_condition(CalUnit& c, ScopeStack& scopes)
{
    auto value = eval_scoped(c, scopes);
    if (should_stop_execution()) { return false; }
    Object::set_runtime_error_reporter([this, &c](const std::string& message, const Object* source)
        { set_runtime_error(message, source, &c); });
    RaiiGuard reporter_guard([]() { Object::clear_runtime_error_reporter(); });
    const bool result = value.toBool();
    return !should_stop_execution() && result;
}

Object Cifa::eval_scoped(CalUnit& c, ScopeStack& scopes)
{
    if (should_stop_execution())
    {
        return Object("RuntimeError", "Error");
    }

    //Union（代码块/数组字面量）不入调用栈，避免根块的行号污染错误报告
    const bool push_frame = (c.type != CalUnitType::Union);
    auto& runtime_stack = execution_contexts.back().runtime_call_stack;
    if (push_frame)
    {
        runtime_stack.push_back({ &c, &execution_contexts.back().source_line_infos, {} });
    }
    RaiiGuard frame_guard([&runtime_stack, push_frame]()
        {
            if (push_frame)
            {
                runtime_stack.pop_back();
            }
        });

    if (has_return_value())
    {
        return return_value();
    }
    else if (c.type == CalUnitType::Operator)
    {
        if (c.v.size() == 1)
        {
            if (c.str == "+")
            {
                auto value = eval_scoped(c.v[0], scopes);
                if (value.isType<bool>())
                {
                    return Object(value.toInt64());
                }
                return value;
            }
            if (c.str == "-") { return sub(Object(0), eval_scoped(c.v[0], scopes)); }
            if (c.str == "~") { return Object(~eval_scoped(c.v[0], scopes).toInt64()); }
            if (c.str == "!") { return Object(!eval_scoped(c.v[0], scopes).toBool()); }
            if (c.str == "++")
            {
                auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                auto value = add(target, Object(1));
                return assign_object_value(target, std::move(value), c.v[0], &c);
            }
            if (c.str == "--")
            {
                auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                auto value = sub(target, Object(1));
                return assign_object_value(target, std::move(value), c.v[0], &c);
            }
            if (c.str == "()++")
            {
                auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                auto v = target;
                auto value = add(target, Object(1));
                assign_object_value(target, std::move(value), c.v[0], &c);
                return v;
            }
            if (c.str == "()--")
            {
                auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                auto v = target;
                auto value = sub(target, Object(1));
                assign_object_value(target, std::move(value), c.v[0], &c);
                return v;
            }
        }
        if (c.v.size() == 2)
        {
            if (c.str == "." && c.v[0].can_cal())
            {
                if (c.v[1].type == CalUnitType::Function)
                {
                    //内置的数组/map方法：需要引用修改原始对象
                    auto& method_name = c.v[1].str;
                    if (method_name == "push_back" || method_name == "pop_back" || method_name == "resize" || method_name == "reserve"
                        || method_name == "clear" || method_name == "insert" || method_name == "erase"
                        || method_name == "contains" || method_name == "keys")
                    {
                        std::vector<CalUnit> args;
                        if (c.v[1].v.size() > 0 && c.v[1].v[0].type != CalUnitType::None)
                        {
                            expand_comma(c.v[1].v[0], args);
                        }
                        auto& obj = get_parameter_for_assign(c.v[0], scopes);
                        return eval_builtin_method(c.v[1], obj, args, scopes);
                    }
                    std::vector<CalUnit> v = { c.v[0] };
                    if (c.v[1].v[0].type != CalUnitType::None)
                    {
                        expand_comma(c.v[1].v[0], v);
                    }
                    return run_function(c.v[1], v, scopes);
                }
                if (c.v[1].type == CalUnitType::Parameter)
                {
                    auto* base = find_object_from_inner(scopes, c.v[0].str);
                    if (base != nullptr && base->isType<ObjectMap>())
                    {
                        auto& m = base->ref<ObjectMap>();
                        auto& elem = m[c.v[1].str];
                        elem.name = c.v[0].str + "." + c.v[1].str;
                        return elem;
                    }
                    return get_or_create_parameter(c.v[0].str + "::" + c.v[1].str, scopes);
                }
            }
            //.和::作为取成员运算符时，目前只保证一层
            if (c.str == "::")
            {
                const auto flat_name = c.v[0].str + "::" + c.v[1].str;
                auto* flat = find_object_from_inner(scopes, flat_name);
                if (flat != nullptr) { return *flat; }
                //回退到 ObjectMap 访问（register_parameter 注册的 map）
                auto* base = find_object_from_inner(scopes, c.v[0].str);
                if (base != nullptr && base->isType<ObjectMap>())
                {
                    auto& m = base->ref<ObjectMap>();
                    auto& elem = m[c.v[1].str];
                    elem.name = flat_name;
                    return elem;
                }
                return get_or_create_parameter(flat_name, scopes);
            }
            if (c.str == "*") { return mul(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "/") { return div(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "%") { return mod(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "+") { return add(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "-") { return sub(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == ">") { return more(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "<") { return less(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == ">=") { return more_equal(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "<=") { return less_equal(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "==") { return equal(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "!=") { return not_equal(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "&") { return bit_and(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "^") { return bit_xor(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "|") { return bit_or(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "&&")
            {
                auto left = eval_scoped(c.v[0], scopes);
                if (!left.toBool())
                {
                    return Object(0);
                }
                return logic_and(left, eval_scoped(c.v[1], scopes));
            }
            if (c.str == "||")
            {
                auto left = eval_scoped(c.v[0], scopes);
                if (left.toBool())
                {
                    return Object(1);
                }
                return logic_or(left, eval_scoped(c.v[1], scopes));
            }
            if (c.str == "<<") { return shift_left(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == ">>") { return shift_right(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "=")
            {
                //空花括号 {} 在赋值右侧视为空数组字面量
                if (c.v[1].type == CalUnitType::Union && c.v[1].str == "{}" && c.v[1].v.empty())
                {
                    auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                    return assign_object_value(target, Object(std::vector<Object>{ }), c.v[0], &c);
                }
                auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                return assign_object_value(target, eval_scoped(c.v[1], scopes), c.v[0], &c);
            }
            if (c.str == "+=" || c.str == "-=" || c.str == "*=" || c.str == "/=" || c.str == "%="
                || c.str == "<<=" || c.str == ">>=" || c.str == "&=" || c.str == "|=" || c.str == "^=")
            {
                auto& target = get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type);
                const auto right = eval_scoped(c.v[1], scopes);
                Object value;
                if (c.str == "+=") { value = add(target, right); }
                else if (c.str == "-=") { value = sub(target, right); }
                else if (c.str == "*=") { value = mul(target, right); }
                else if (c.str == "/=") { value = div(target, right); }
                else if (c.str == "%=") { value = mod(target, right); }
                else if (c.str == "<<=") { value = shift_left(target, right); }
                else if (c.str == ">>=") { value = shift_right(target, right); }
                else if (c.str == "&=") { value = bit_and(target, right); }
                else if (c.str == "|=") { value = bit_or(target, right); }
                else { value = bit_xor(target, right); }
                return assign_object_value(target, std::move(value), c.v[0], &c);
            }
            if (c.str == ",")
            {
                eval_scoped(c.v[0], scopes);
                eval_scoped(c.v[1], scopes);
                return Object();
            }
            if (c.str == "?")    //条件1 ? 语句1 : 语句2;
            {
                if (eval_condition(c.v[0], scopes))    //比较?运算符左侧的 [条件1]
                {
                    return eval_scoped(c.v[1].v[0], scopes);    //取:运算符左侧 [语句1] 的结果
                }
                else
                {
                    return eval_scoped(c.v[1].v[1], scopes);    //取:运算符右侧 [语句2] 的结果
                }
            }
        }
        return Object();
    }
    else if (c.type == CalUnitType::Constant)
    {
        Object value;
        if (!parse_number_literal(c.str, value))
        {
            set_runtime_error("invalid numeric literal '" + c.str + "'");
            return Object();
        }
        return value;
    }
    else if (c.type == CalUnitType::Cast)
    {
        if (c.v.size() != 1)
        {
            set_runtime_error("type cast '" + c.type_name + "' has invalid operand count", nullptr, &c);
            return Object();
        }
        return convert_object_type(eval_scoped(c.v[0], scopes), c.type_name, &c);
    }
    else if (c.type == CalUnitType::String)
    {
        return Object(c.str);
    }
    else if (c.type == CalUnitType::Parameter)
    {
        // struct 类型声明：初始化为含所有字段的 ObjectMap
        const auto* struct_definition = find_struct_definition(c.type_name);
        if (c.with_type && struct_definition != nullptr)
        {
            auto* existing = find_object_from_inner(scopes, c.str);
            if (existing == nullptr || !existing->isType<ObjectMap>())
            {
                return get_parameter_for_assign(c, scopes, true);
            }
        }
        return get_parameter(c, scopes);
    }
    else if (c.type == CalUnitType::Function)
    {
        std::vector<CalUnit> v;
        if (!c.v.empty())
        {
            expand_comma(c.v[0], v);
        }
        return run_function(c, v, scopes);
    }
    else if (c.type == CalUnitType::Goto)
    {
        set_control_flow(ControlFlow::Goto, c.str);
        return Object();
    }
    else if (c.type == CalUnitType::Key)
    {
        auto& context = execution_contexts.back();
        if (c.str == "if")    //if(条件1){语句1}else{语句2}
        {
            if (eval_condition(c.v[0], scopes))    //判断 [条件1]
            {
                return eval_scoped(c.v[1], scopes);    //取: [语句1] 执行结果
            }
            else if (c.v.size() >= 3)
            {
                return eval_scoped(c.v[2], scopes);    //取: [语句2] 执行结果
            }
            return Object(0);
        }
        if (c.str == "for")    //for(语句1;条件1;语句2){语句3}
        {
            //范围 for：for (item : array)，循环变量每轮都是数组元素的值副本。
            CalUnit* range_clause = c.v[0].type == CalUnitType::Operator && c.v[0].str == ":" ? &c.v[0] : nullptr;
            if (range_clause == nullptr && c.v[0].type == CalUnitType::Union && c.v[0].str == "()" && c.v[0].v.size() == 1
                && c.v[0].v[0].type == CalUnitType::Operator && c.v[0].v[0].str == ":")
            {
                range_clause = &c.v[0].v[0];
            }
            if (range_clause != nullptr && range_clause->v.size() == 2
                && range_clause->v[0].type == CalUnitType::Parameter)
            {
                const auto& loop_var = range_clause->v[0].str;
                const auto& loop_type = range_clause->v[0].type_name;
                Object range = eval_scoped(range_clause->v[1], scopes);
                if (!range.isType<std::vector<Object>>())
                {
                    if (!range.report_no_value())
                    {
                        set_runtime_error("range for requires an array", nullptr, &range_clause->v[1]);
                    }
                    return Object();
                }

                const auto values = range.ref<std::vector<Object>>();
                Object o;
                for (const auto& value : values)
                {
                    scopes.emplace_back();
                    Object loop_value = value;
                    loop_value.bound_type = typeid(void);
                    loop_value.declared_type_name.clear();
                    if (!loop_type.empty())
                    {
                        if (loop_type == "auto")
                        {
                            apply_declared_type(loop_value, "auto", &range_clause->v[0], true);
                        }
                        else
                        {
                            loop_value = convert_object_type(loop_value, loop_type, &range_clause->v[0]);
                            if (has_runtime_error()) { scopes.pop_back(); return Object(); }
                            apply_declared_type(loop_value, loop_type, &range_clause->v[0], false);
                        }
                    }
                    scopes.back()[loop_var] = std::move(loop_value);
                    scopes.back()[loop_var].name = loop_var;
                    o = eval_scoped(c.v[1], scopes);
                    scopes.pop_back();
                    if (should_stop_execution()) { return o; }
                    if (context.control_flow == ControlFlow::Goto) { return o; }
                    if (consume_control_flow(ControlFlow::Break)) { break; }
                    if (consume_control_flow(ControlFlow::Continue)) { continue; }
                    if (has_return_value()) { return return_value(); }
                }
                return Object(0);
            }

            Object o;
            for (
                eval_scoped(c.v[0].v[0], scopes);    //执行 [语句1]
                !is_exit_requested() && eval_condition(c.v[0].v[1], scopes);    //判断 [条件1]
                eval_scoped(c.v[0].v[2], scopes)     //执行 [语句2]
            )
            {
                o = eval_scoped(c.v[1], scopes);    //执行 [语句3] 并 取执行结果
                if (should_stop_execution()) { return o; }
                if (context.control_flow == ControlFlow::Goto) { return o; }
                if (consume_control_flow(ControlFlow::Break)) { break; }
                if (consume_control_flow(ControlFlow::Continue)) { continue; }
                if (has_return_value()) { return return_value(); }
            }
            return Object(0);
        }
        if (c.str == "while")    //while (条件1) {语句1}
        {
            Object o;
            while (!is_exit_requested() && eval_condition(c.v[0], scopes))    //判断 [条件1]
            {
                o = eval_scoped(c.v[1], scopes);    //执行 [语句1] 并 取执行结果
                if (should_stop_execution()) { return o; }
                if (context.control_flow == ControlFlow::Goto) { return o; }
                if (consume_control_flow(ControlFlow::Break)) { break; }
                if (consume_control_flow(ControlFlow::Continue)) { continue; }
                if (has_return_value()) { return return_value(); }
            }
            return Object(0);
        }
        if (c.str == "do")    //do {语句1} while (条件1);
        {
            Object o;
            do
            {
                o = eval_scoped(c.v[0], scopes);    //执行 [语句1] 并 取执行结果
                if (should_stop_execution()) { return o; }
                if (context.control_flow == ControlFlow::Goto) { return o; }
                if (consume_control_flow(ControlFlow::Break)) { break; }
                consume_control_flow(ControlFlow::Continue);
                if (has_return_value()) { return return_value(); }
            } while (!is_exit_requested() && eval_condition(c.v[1].v[0], scopes));    //判断 [条件1]
            return Object(0);
        }
        if (c.str == "switch")
        {
            auto cond = eval_scoped(c.v[0], scopes);
            bool skip = true;
            scopes.emplace_back();
            for (auto& c1 : c.v[1].v)
            {
                if (should_stop_execution()) { break; }
                if (c1.str == "case")
                {
                    if (skip)
                    {
                        if (equal(cond, eval_scoped(c1.v[0], scopes)))
                        {
                            skip = false;
                        }
                    }
                }
                else if (c1.str == "default")
                {
                    if (skip)
                    {
                        skip = false;
                    }
                }
                else if (!skip)
                {
                    auto o = eval_scoped(c1, scopes);
                    if (should_stop_execution())
                    {
                        scopes.pop_back();
                        return o;
                    }
                    if (context.control_flow == ControlFlow::Goto)
                    {
                        scopes.pop_back();
                        return o;
                    }
                    if (context.control_flow == ControlFlow::Continue)
                    {
                        scopes.pop_back();
                        return o;
                    }
                    if (consume_control_flow(ControlFlow::Break)) { break; }
                    if (has_return_value())
                    {
                        auto ret = return_value();
                        scopes.pop_back();
                        return ret;
                    }
                }
            }
            scopes.pop_back();
            return 0;
        }
        if (c.str == "return")
        {
            Object value = eval_scoped(c.v[0], scopes);
            const auto& return_states = execution_contexts.back().return_states;
            const std::string return_type = return_states.empty() ? std::string() : return_states.back().return_type;
            if (!return_type.empty() && return_type != "void")
            {
                value = convert_object_type(value, return_type, &c);
                if (has_runtime_error()) { return Object(); }
            }
            return_value() = std::move(value);
            return return_value();
        }
        if (c.str == "break")
        {
            set_control_flow(ControlFlow::Break);
            return Object();
        }
        if (c.str == "continue")
        {
            set_control_flow(ControlFlow::Continue);
            return Object();
        }
        if (c.str == "goto" && c.v.size() == 1 && c.v[0].type == CalUnitType::Parameter)
        {
            set_control_flow(ControlFlow::Goto, c.v[0].str);
            return Object();
        }
        if (c.str == "true")
        {
            return Object(true);
        }
        if (c.str == "false")
        {
            return Object(false);
        }
    }
    else if (c.type == CalUnitType::Union)
    {
        Object array_literal;
        if (try_eval_array_literal(c, scopes, array_literal))
        {
            return array_literal;
        }

        const bool is_block_scope = c.str == "{}";
        if (is_block_scope)
        {
            scopes.emplace_back();
        }
        auto& context = execution_contexts.back();
        std::unordered_map<std::string, size_t> local_labels;
        for (size_t index = 0; index < c.v.size(); ++index)
        {
            if (c.v[index].type == CalUnitType::Label)
            {
                local_labels[c.v[index].str] = index;
            }
        }
        Object o;
        size_t index = 0;
        for (; index < c.v.size(); ++index)
        {
            auto& c1 = c.v[index];
            if (c1.type == CalUnitType::Label)
            {
                continue;
            }
            o = eval_scoped(c1, scopes);
            if (should_stop_execution())
            {
                if (is_block_scope)
                {
                    scopes.pop_back();
                }
                return o;
            }
            if (context.control_flow == ControlFlow::Goto)
            {
                auto target = local_labels.find(context.goto_label);
                if (target != local_labels.end())
                {
                    consume_control_flow(ControlFlow::Goto);
                    index = target->second;
                    continue;
                }
                if (is_block_scope)
                {
                    scopes.pop_back();
                }
                return o;
            }
            if (context.control_flow == ControlFlow::Break || context.control_flow == ControlFlow::Continue) { break; }
            if (has_return_value())
            {
                auto ret = return_value();
                if (is_block_scope)
                {
                    scopes.pop_back();
                }
                return ret;
            }
        }
        if (is_block_scope)
        {
            scopes.pop_back();
        }
        return o;
    }
    return Object();
}

//判断一个 {} 节点是否为数组字面量（而非代码块）
bool Cifa::is_array_literal_candidate(CalUnit& c) const
{
    if (c.type != CalUnitType::Union || c.str != "{}")
    {
        return false;
    }
    if (c.v.empty())
    {
        // Keep empty block behavior unchanged.
        return false;
    }
    for (auto& c1 : c.v)
    {
        // Statements imply block semantics, not array-literal semantics.
        if (c1.is_statement())
        {
            return false;
        }
    }
    return true;
}

//尝试将 {} 节点按数组字面量求值，成功则写入 out 并返回 true
bool Cifa::try_eval_array_literal(CalUnit& c, ScopeStack& scopes, Object& out)
{
    if (!is_array_literal_candidate(c))
    {
        return false;
    }

    std::vector<Object> arr;
    for (auto& c1 : c.v)
    {
        std::vector<CalUnit> items;
        expand_comma(c1, items);
        for (auto& item : items)
        {
            arr.emplace_back(eval_scoped(item, scopes));
        }
    }
    out = Object(arr);
    return true;
}

//递归展开逗号表达式为参数列表
void Cifa::expand_comma(CalUnit& c1, std::vector<CalUnit>& v)
{
    if (c1.str == ",")
    {
        for (auto& c2 : c1.v)
        {
            expand_comma(c2, v);
        }
    }
    else
    {
        if (c1.type != CalUnitType::None)
        {
            v.push_back(c1);
        }
    }
}

//根据字符推断词法类型（数字、运算符、标识符、分隔符、字符串）
CalUnitType Cifa::guess_char(char c)
{
    if (std::string("0123456789").find(c) != std::string::npos)
    {
        return CalUnitType::Constant;
    }
    if (std::string("+-*/%=.!<>&|,?:^").find(c) != std::string::npos)
    {
        return CalUnitType::Operator;
    }
    if (std::string("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").find(c) != std::string::npos)
    {
        return CalUnitType::Parameter;
    }
    if (std::string("()[]{};").find(c) != std::string::npos)
    {
        return CalUnitType::Split;
    }
    if (std::string("\"\'").find(c) != std::string::npos)
    {
        return CalUnitType::String;
    }
    //support utf-8
    if (c < 0)
    {
        return CalUnitType::Parameter;
    }
    return CalUnitType::None;
}

//分割语法
std::list<CalUnit> Cifa::split(std::string& str)
{
    std::string r;
    std::list<CalUnit> rv;

    CalUnitType stat = CalUnitType::None;
    char in_string = 0;
    char c0 = 0;
    size_t line = 1, col = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
        if (i > 1) { c0 = str[i - 1]; }
        auto c = str[i];
        col++;
        auto pre_stat = stat;
        auto g = guess_char(c);
        if (in_string)
        {
            if (stat == CalUnitType::String && in_string == c)
            {
                in_string = 0;
                stat = CalUnitType::None;
            }
            else
            {
                stat = CalUnitType::String;
            }
        }
        else if (g == CalUnitType::String)
        {
            if (in_string == 0)
            {
                in_string = c;
                stat = CalUnitType::String;
            }
        }
        else if (g == CalUnitType::Constant)
        {
            if (stat == CalUnitType::Constant || stat == CalUnitType::Operator
                || stat == CalUnitType::Split || stat == CalUnitType::None)
            {
                stat = CalUnitType::Constant;
            }
            else if (stat == CalUnitType::Parameter)
            {
                stat = CalUnitType::Parameter;
            }
        }
        else if (g == CalUnitType::Operator)
        {
            if (c == '.' && stat == CalUnitType::Constant)
            {
            }
            else if ((c == '+' || c == '-') && (c0 == 'E' || c0 == 'e') && stat == CalUnitType::Constant)
            {
            }
            else
            {
                stat = CalUnitType::Operator;
            }
            //"/"开头时特别处理注释
            if (c == '/')
            {
                if (i < str.size() - 1)
                {
                    auto c1 = str[i + 1];
                    if (c1 == '*')
                    {
                        stat = CalUnitType::None;
                        while (i < str.size() - 1)
                        {
                            if (str[i] == '*' && str[i + 1] == '/')
                            {
                                i++;
                                break;
                            }
                            if (str[i] == '\n')
                            {
                                line++;
                                col = 0;
                            }
                            i++;
                        }
                    }
                    else if (c1 == '/')
                    {
                        stat = CalUnitType::None;
                        while (i < str.size() - 1)
                        {
                            if (str[i] == '\n')
                            {
                                line++;
                                col = 0;
                                break;
                            }
                            i++;
                        }
                    }
                }
            }
        }
        else if (g == CalUnitType::Split)
        {
            stat = CalUnitType::Split;
        }
        else if (g == CalUnitType::Parameter)
        {
            if ((c == 'E' || c == 'e') && stat == CalUnitType::Constant)
            {
            }
            else if ((c == 'f' || c == 'F') && stat == CalUnitType::Constant
                && r.find_first_of(".eE") != std::string::npos)
            {
            }
            else if (stat == CalUnitType::Constant && r == "0" && (c == 'x' || c == 'X' || c == 'b' || c == 'B'))
            {
            }
            else if (stat == CalUnitType::Constant && r.size() >= 2 && r[0] == '0'
                && (r[1] == 'x' || r[1] == 'X') && std::isxdigit(static_cast<unsigned char>(c)))
            {
            }
            else
            {
                stat = CalUnitType::Parameter;
            }
        }
        else if (g == CalUnitType::None)
        {
            if (!std::isspace(static_cast<unsigned char>(c)))
            {
                add_error(line, col, "unexpected character '{}'", c);
            }
            stat = CalUnitType::None;
        }
        if (pre_stat != stat || stat == CalUnitType::Operator || stat == CalUnitType::Split)
        {
            if (pre_stat != CalUnitType::None)
            {
                //操作符替代词
                if (pre_stat == CalUnitType::Parameter && op_representations.contains(r))
                {
                    r = op_representations.at(r);
                    pre_stat = CalUnitType::Operator;
                }
                CalUnit c(pre_stat, r);
                c.line = line;
                c.col = col - r.size();
                rv.emplace_back(std::move(c));
            }
            r.clear();
            if (g != CalUnitType::String)
            {
                r = c;
            }
        }
        else
        {
            r += c;
        }
        if (c == '\n')
        {
            col = 0;
            line++;
        }
    }

    if (stat != CalUnitType::None)
    {
        CalUnit c(stat, r);
        c.line = line;
        c.col = col - r.size();
        rv.emplace_back(std::move(c));
    }

    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        //括号前的变量视为函数
        if (it->str == "(" && it != rv.begin() && std::prev(it)->type == CalUnitType::Parameter)
        {
            std::prev(it)->type = CalUnitType::Function;
        }
        if (keyword_tokens().contains(it->str))
        {
            it->type = CalUnitType::Key;
        }
        if (it->type == CalUnitType::Parameter && (it->str == "auto" || it->str == "void" || registered_types.contains(it->str)))
        {
            it->type = CalUnitType::Type;
        }
    }

    //合并多字节运算符
    for (auto it = rv.begin(); it != rv.end();)
    {
        auto itr = std::next(it);
        if (itr != rv.end() && it->type == CalUnitType::Operator && itr->type == CalUnitType::Operator
            && it->line == itr->line && it->col == itr->col - 1)
        {
            std::string op = it->str + itr->str;
            if (op.size() == 2 && operator_tokens().contains(op))
            {
                it->str = std::move(op);
                it = rv.erase(itr);
                continue;
            }
        }
        ++it;
    }

    // 不把类型符号留在归约结果中，但保留类型名供声明转换使用。
    // 同时把 int a = 1, b = 2; 中的类型传播到后续声明符。
    {
        int round_depth = 0;
        int square_depth = 0;
        int curly_depth = 0;
        bool pending_declaration = false;
        bool expect_declarator_after_comma = false;
        int pending_round_depth = 0;
        int pending_square_depth = 0;
        int pending_curly_depth = 0;
        std::string pending_type;

        for (auto it = rv.begin(); it != rv.end();)
        {
            const int before_round = round_depth;
            const int before_square = square_depth;
            const int before_curly = curly_depth;
            if (it->str == "(") { ++round_depth; }
            else if (it->str == ")") { --round_depth; }
            else if (it->str == "[") { ++square_depth; }
            else if (it->str == "]") { --square_depth; }
            else if (it->str == "{") { ++curly_depth; }
            else if (it->str == "}") { --curly_depth; }

            if (it->type == CalUnitType::Type)
            {
                auto next = std::next(it);
                if (next != rv.end()
                    && (next->type == CalUnitType::Parameter || next->type == CalUnitType::Function)
                    && !next->with_type)
                {
                    next->with_type = true;
                    next->type_name = it->str;
                    const bool is_function_declaration = next->type == CalUnitType::Function;
                    it = rv.erase(it);
                    if (is_function_declaration)
                    {
                        pending_declaration = false;
                        pending_type.clear();
                    }
                    else
                    {
                        pending_declaration = true;
                        expect_declarator_after_comma = false;
                        pending_type = next->type_name;
                        pending_round_depth = round_depth;
                        pending_square_depth = square_depth;
                        pending_curly_depth = curly_depth;
                    }
                    continue;
                }
                ++it;
                continue;
            }

            if (pending_declaration
                && round_depth == pending_round_depth
                && square_depth == pending_square_depth
                && curly_depth == pending_curly_depth)
            {
                if (it->str == ";")
                {
                    pending_declaration = false;
                    pending_type.clear();
                    expect_declarator_after_comma = false;
                }
                else if (it->str == ",")
                {
                    expect_declarator_after_comma = true;
                }
                else if (expect_declarator_after_comma
                    && (it->type == CalUnitType::Parameter || it->type == CalUnitType::Function)
                    && !it->with_type)
                {
                    it->with_type = true;
                    it->type_name = pending_type;
                    expect_declarator_after_comma = false;
                }
            }

            // 离开声明所在的括号层级后，内层逗号不再传播类型。
            (void)before_round;
            (void)before_square;
            (void)before_curly;
            ++it;
        }
    }

    // 预扫描：只注册全局 struct 类型名（此时 {} 尚未合并，使用花括号深度判断）
    int brace_depth = 0;
    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        if (it->str == "}")
        {
            --brace_depth;
        }
        if (brace_depth == 0 && it->str == "struct" && it->type == CalUnitType::Parameter)
        {
            auto it1 = std::next(it);
            if (it1 != rv.end() && it1->type == CalUnitType::Parameter)
            {
                auto it2 = std::next(it1);
                if (it2 != rv.end() && it2->str == "{")
                {
                    compilation_struct_defs.emplace(it1->str, std::vector<StructField>{ });
                }
            }
        }
        if (it->str == "{")
        {
            ++brace_depth;
        }
    }

    // 将已知 struct 类型名作为类型标记：擦除类型名并在后续变量上设置 with_type/type_name
    for (auto it = rv.begin(); it != rv.end();)
    {
        if (it->type == CalUnitType::Parameter && find_struct_definition(it->str) != nullptr)
        {
            auto itr = std::next(it);
            if (itr != rv.end() && (itr->type == CalUnitType::Parameter || itr->type == CalUnitType::Function))
            {
                itr->with_type = true;
                itr->type_name = it->str;
                it = rv.erase(it);
                continue;
            }
        }
        ++it;
    }

    return rv;
}

//表达式语法树
//参数含义：是否合并{}，是否合并[]，是否合并()
CalUnit Cifa::combine_all_cal(std::list<CalUnit>& ppp, bool curly, bool square, bool round, bool allow_labels, bool global_scope)
{
    //合并{}
    if (curly) { combine_curly_bracket(ppp); }
    //合并[]
    if (square) { combine_square_bracket(ppp); }
    //合并()
    if (round) { combine_round_bracket(ppp); }

    //提取并移除 struct 定义
    combine_structs(ppp, global_scope);

    //合并关键字
    deal_special_keys(ppp);

    //标签必须在运算符合并前处理，避免其冒号被当作普通二元运算符。
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (allow_labels && it->type == CalUnitType::Operator && it->str == ":" && !it->un_combine && it->v.empty() && it != ppp.begin()
            && std::prev(it)->type == CalUnitType::Parameter && !std::prev(it)->with_type
            && (std::prev(it) == ppp.begin() || std::prev(std::prev(it))->str == ";"
                || std::prev(std::prev(it))->type == CalUnitType::Label
                || std::prev(std::prev(it))->type == CalUnitType::Union && std::prev(std::prev(it))->str == "{}"))
        {
            auto label = std::prev(it);
            label->type = CalUnitType::Label;
            label->suffix = true;
            it = ppp.erase(it);
        }
        else
        {
            ++it;
        }
    }

    //合并算符
    combine_ops(ppp);

    //检查分号正确性并去除
    //方括号或圆括号内不准许分号
    combine_semi(ppp);

    //合并关键字
    combine_keys(ppp);

    combine_functions2(ppp, global_scope);

    //到此处应仅剩余单独分号（空语句）、语句、语句组，只需简单合并即可
    //即使只有一条语句也必须返回Union！
    CalUnit c;
    c.type = CalUnitType::Union;
    if (ppp.size() == 0)
    {
        return c;
    }
    else
    {
        c.line = ppp.front().line;
        c.col = ppp.front().col;
        for (auto it = ppp.begin(); it != ppp.end(); ++it)
        {
            //if (it->type != CalUnitType::Split)
            {
                c.v.emplace_back(std::move(*it));
            }
        }
        return c;
    }
}

//合并花括号 {} 为语法树节点
void Cifa::combine_curly_bracket(std::list<CalUnit>& ppp)
{
    std::vector<std::list<CalUnit>::iterator> stack;
    for (auto it = ppp.begin(); it != ppp.end(); ++it)
    {
        if (it->str == "{")
        {
            stack.push_back(it);
        }
        else if (it->str == "}")
        {
            if (stack.empty())
            {
                add_error(*it, "unpaired right bracket {}", it->str);
                it = ppp.erase(it);
                if (it == ppp.begin())
                {
                    continue;
                }
                --it;
                continue;
            }
            auto left = stack.back();
            stack.pop_back();
            std::list<CalUnit> ppp2;
            ppp2.splice(ppp2.begin(), ppp, std::next(left), it);
            auto c1 = combine_all_cal(ppp2, false, true, true, true, false);    //此处合并多行
            c1.str = "{}";
            c1.line = left->line;
            c1.col = left->col;
            auto next = ppp.erase(it);
            *left = std::move(c1);
            it = left;
        }
    }
    while (!stack.empty())
    {
        auto left = stack.back();
        stack.pop_back();
        add_error(*left, "unpaired left bracket {}", left->str);
        ppp.erase(left);
    }
}

//合并方括号 [] 为语法树节点，并关联到前置变量名
void Cifa::combine_square_bracket(std::list<CalUnit>& ppp)
{
    std::vector<std::list<CalUnit>::iterator> left_brackets;
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->str == "[")
        {
            left_brackets.push_back(it);
            ++it;
            continue;
        }
        if (it->str != "]")
        {
            ++it;
            continue;
        }

        if (left_brackets.empty())
        {
            add_error(*it, "unpaired right bracket {}", it->str);
            it = ppp.erase(it);
            continue;
        }

        auto left = left_brackets.back();
        left_brackets.pop_back();
        std::list<CalUnit> ppp2;
        ppp2.splice(ppp2.begin(), ppp, std::next(left), it);
        auto c1 = combine_all_cal(ppp2, true, false, true, true, false);
        c1.str = "[]";
        c1.line = left->line;
        c1.col = left->col;

        auto right = it;
        *right = std::move(c1);
        ppp.erase(left);
        it = std::next(right);
        if (right != ppp.begin())
        {
            if (std::prev(right)->type == CalUnitType::Parameter)
            {
                //多维数组：如果前置变量已有 [] 子节点，追加而非覆盖
                std::prev(right)->v.push_back(std::move(*right));
                ppp.erase(right);
            }
        }
    }
    while (!left_brackets.empty())
    {
        auto left = left_brackets.back();
        left_brackets.pop_back();
        add_error(*left, "unpaired left bracket {}", left->str);
        ppp.erase(left);
    }
}

//合并圆括号 () 为语法树节点，并关联到前置函数名或关键字
void Cifa::combine_round_bracket(std::list<CalUnit>& ppp)
{
    std::vector<std::list<CalUnit>::iterator> left_brackets;
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->str == "(")
        {
            left_brackets.push_back(it);
            ++it;
            continue;
        }
        if (it->str != ")")
        {
            ++it;
            continue;
        }

        if (left_brackets.empty())
        {
            add_error(*it, "unpaired right bracket {}", it->str);
            it = ppp.erase(it);
            continue;
        }

        auto left = left_brackets.back();
        left_brackets.pop_back();
        const bool is_for_header = left != ppp.begin() && std::prev(left)->type == CalUnitType::Key && std::prev(left)->str == "for";

        std::list<CalUnit> ppp2;
        ppp2.splice(ppp2.begin(), ppp, std::next(left), it);
        auto c1 = combine_all_cal(ppp2, true, true, false, !is_for_header, false);
        c1.str = "()";
        c1.line = left->line;
        c1.col = left->col;

        const bool is_cast = c1.type == CalUnitType::Union && c1.v.size() == 1
            && c1.v[0].type == CalUnitType::Type;

        auto right = it;
        if (is_cast)
        {
            CalUnit cast;
            cast.type = CalUnitType::Cast;
            cast.type_name = c1.v[0].str;
            cast.line = left->line;
            cast.col = left->col;
            *right = std::move(cast);
        }
        else if (c1.v.empty())
        {
            right->type = CalUnitType::None;
            right->str.clear();
            right->v.clear();
            right->line = c1.line;
            right->col = c1.col;
        }
        else if (c1.v.size() == 1)
        {
            *right = std::move(c1.v[0]);
        }
        else
        {
            *right = std::move(c1);
        }
        ppp.erase(left);

        if (right != ppp.begin())
        {
            auto itl = std::prev(right);
            if (itl->type == CalUnitType::Function || itl->type == CalUnitType::Parameter || itl->type == CalUnitType::Constant)
            {
                itl->v = { std::move(*right) };
                it = ppp.erase(right);
                continue;
            }
            else if (itl->type == CalUnitType::Key && keys[2].contains(itl->str))
            {
                itl->v = { std::move(*right) };
                it = ppp.erase(right);
                continue;
            }
        }
        it = std::next(right);
    }

    for (auto left = left_brackets.rbegin(); left != left_brackets.rend(); ++left)
    {
        add_error(**left, "unpaired left bracket {}", (*left)->str);
        ppp.erase(*left);
    }
}

//按优先级合并运算符到语法树，处理左结合和右结合
void Cifa::combine_ops(std::list<CalUnit>& ppp)
{
    // C 风格类型转换：(type)expr。转换优先级高于二元运算符。
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->type != CalUnitType::Cast || !it->v.empty())
        {
            ++it;
            continue;
        }
        auto value = std::next(it);
        if (value == ppp.end())
        {
            add_error(*it, "type cast '{}' has no operand", it->type_name);
            ++it;
            continue;
        }
        if (value->can_cal() || value->type == CalUnitType::Union)
        {
            it->v = { std::move(*value) };
            ppp.erase(value);
            ++it;
            continue;
        }
        if (value->type == CalUnitType::Operator && value->v.empty()
            && (value->str == "+" || value->str == "-" || value->str == "!"
                || value->str == "~" || value->str == "++" || value->str == "--"))
        {
            auto operand = std::next(value);
            if (operand != ppp.end() && (operand->can_cal() || operand->type == CalUnitType::Union))
            {
                value->v = { std::move(*operand) };
                ppp.erase(operand);
                it->v = { std::move(*value) };
                ppp.erase(value);
                ++it;
                continue;
            }
        }
        add_error(*it, "type cast '{}' has invalid operand", it->type_name);
        ++it;
    }

    const auto& op_groups = operator_precedence_token_groups();
    for (size_t group_index = 0; group_index < ops.size(); ++group_index)
    {
        const auto& ops1 = ops[group_index];
        const auto& ops1_tokens = op_groups[group_index];
        for (auto& op : ops1)
        {
            bool is_right = false;
            if (ops_single.contains(op) || ops_right.contains(op) || op == "+" || op == "-")    //右结合
            {
                auto it = ppp.end();
                for (; it != ppp.begin();)
                {
                    --it;
                    if (it->un_combine)
                    {
                        continue;
                    }
                    if (it->type == CalUnitType::Operator && it->str == op && it->v.size() == 0)
                    {
                        if (it == ppp.begin() || ops_single.contains(it->str)
                            || !std::prev(it)->can_cal() && (op == "+" || op == "-"))    //+-退化为单目运算的情况
                        {
                            is_right = true;
                            auto itr = std::next(it);
                            bool is_single = false;
                            if (itr != ppp.end())
                            {
                                if ((it->str == "+" || it->str == "-" || it->str == "!")
                                        && (itr->can_cal() || itr->type == CalUnitType::Union)
                                    || (it->str == "++" || it->str == "--")
                                        && itr->type == CalUnitType::Parameter)
                                {
                                    it->v = { std::move(*itr) };
                                    it = ppp.erase(itr);
                                    is_single = true;
                                }
                            }
                            if (!is_single && it != ppp.begin() && (it->str == "++" || it->str == "--"))
                            {
                                auto previous = std::prev(it);
                                const bool assignable = previous->type == CalUnitType::Parameter
                                    || (previous->type == CalUnitType::Operator && previous->str == "."
                                        && previous->v.size() == 2 && previous->v[1].type == CalUnitType::Parameter)
                                    || (!previous->v.empty() && previous->v[0].str == "[]");
                                if (assignable)
                                {
                                    it->v = { std::move(*previous) };
                                    it->str = "()" + it->str;
                                    it = ppp.erase(previous);
                                }
                            }
                        }
                        else
                        {
                            if (it->str != "+" && it->str != "-")
                            {
                                is_right = true;
                                auto itr = std::next(it);
                                if (itr != ppp.end())
                                {
                                    it->v = { std::move(*std::prev(it)), std::move(*itr) };
                                    ppp.erase(itr);
                                    it = ppp.erase(std::prev(it));
                                }
                            }
                        }
                    }
                }
            }
            if (!is_right && ops1_tokens.contains("?"))    //三目运算符组需按固定顺序（:先?后）逐符号合并
            {
                for (auto it = ppp.begin(); it != ppp.end();)
                {
                    if (it->un_combine)
                    {
                        ++it;
                        continue;
                    }
                    if (it->type == CalUnitType::Operator && it->str == op && it->v.size() == 0 && it != ppp.begin())
                    {
                        auto itr = std::next(it);
                        if (itr != ppp.end())
                        {
                            it->v = { std::move(*std::prev(it)), std::move(*itr) };
                            ppp.erase(itr);
                            it = ppp.erase(std::prev(it));
                        }
                    }
                    ++it;
                }
            }
        }
        //同优先级运算符按源码出现顺序从左到右合并，确保左结合性（如 a/b*c 解析为 (a/b)*c 而非 a/(b*c)）
        //三目运算符组 {":","?"} 已在上方用逐符号方式处理，此处跳过
        if (!ops1_tokens.contains("?"))
        {
            for (auto it = ppp.begin(); it != ppp.end();)
            {
                if (it->un_combine)
                {
                    ++it;
                    continue;
                }
                if (it->type == CalUnitType::Operator && it->v.size() == 0 && it != ppp.begin()
                    && ops1_tokens.contains(it->str)
                    && !ops_single.contains(it->str)
                    && !ops_right.contains(it->str))
                {
                    auto prev_it = std::prev(it);
                    auto itr = std::next(it);
                    //左侧必须是值（Constant/String/Parameter/Function/已合并运算符/Union块）
                    bool prev_is_val = prev_it->can_cal() || prev_it->type == CalUnitType::Union;
                    //右侧也必须是值；若右侧是尚未合并的一元 +/−，先做前瞻合并（处理 2*-3 等情形）
                    bool itr_is_val = itr != ppp.end() && (itr->can_cal() || itr->type == CalUnitType::Union);
                    if (prev_is_val && !itr_is_val && itr != ppp.end()
                        && itr->type == CalUnitType::Operator && itr->v.size() == 0
                        && (itr->str == "+" || itr->str == "-"))
                    {
                        auto itr2 = std::next(itr);
                        if (itr2 != ppp.end() && (itr2->can_cal() || itr2->type == CalUnitType::Union))
                        {
                            itr->v = { std::move(*itr2) };
                            ppp.erase(itr2);
                            itr_is_val = true;    //前瞻合并后，itr 已有子节点，can_cal() 为 true
                        }
                    }
                    if (prev_is_val && itr_is_val)
                    {
                        it->v = { std::move(*prev_it), std::move(*itr) };
                        ppp.erase(itr);
                        it = ppp.erase(prev_it);
                        continue;
                    }
                }
                ++it;
            }
        }
    }
}

//处理分号：将分号前的表达式标记为语句并移除分号节点
void Cifa::combine_semi(std::list<CalUnit>& ppp)
{
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->can_cal())
        {
            auto itr = std::next(it);
            if (itr != ppp.end() && itr->str == ";")
            {
                it->suffix = true;
                it = ppp.erase(itr);
            }
            else
            {
                ++it;
            }
        }
        else
        {
            ++it;
        }
    }
}

//处理 case/default 后的冒号，以及 for 语句的特殊结构
void Cifa::deal_special_keys(std::list<CalUnit>& ppp)
{
    //实际上仅处理case, default的冒号
    auto it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        if (it->str == "case")
        {
            for (auto it1 = std::next(it); it1 != ppp.end(); ++it1)
            {
                if (it1->str == ":")
                {
                    it1->un_combine = true;
                    break;
                }
            }
        }
        if (it->str == "default")
        {
            auto it1 = std::next(it);
            if (it1 != ppp.end() && it1->str == ":")
            {
                it1->un_combine = true;
            }
        }
        if (it->str == "for")
        {
            if (it->v.size() == 1 && it->v[0].str == "()")
            {
                auto& v = it->v[0].v;
                if (v.size() == 2)
                {
                    CalUnit c3;
                    c3.type = CalUnitType::None;
                    v.emplace_back(std::move(c3));
                }
                if (v.size() == 3)
                {
                    //将空的 for 子句（裸分号）转换为合适的节点：
                    //  条件为空时替换为常量 1（永真），初始化和步进为空时替换为 None
                    if (v[1].type == CalUnitType::Split)
                    {
                        v[1].str = "1";
                        v[1].type = CalUnitType::Constant;
                        v[1].suffix = true;
                    }
                    if (v[0].type == CalUnitType::Split)
                    {
                        v[0].type = CalUnitType::None;
                        v[0].str.clear();
                        v[0].suffix = true;
                    }
                    if (v[2].type == CalUnitType::Split)
                    {
                        v[2].type = CalUnitType::None;
                        v[2].str.clear();
                    }
                }
            }
        }
    }

}

//合并关键字（if/for/while/do/switch/case/else 等）及其子节点
void Cifa::combine_keys(std::list<CalUnit>& ppp)
{
    //需注意此时的ppp中已经没有()，因此if, for, while, switch等关键字后面的括号已经被合并
    //处理不能单独存在的关键字
    auto it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        //do while
        if (it->str == "do" && it->v.empty() && std::next(it) != ppp.end())
        {
            auto itr1 = std::next(it);
            auto itr2 = std::next(itr1);
            if (itr1->str == "{}" && itr2->str == "while")    //必须后面接 {} 和 while
            {
                it->v.emplace_back(std::move(*itr1));
                it->v.emplace_back(std::move(*itr2));
                ppp.erase(itr1);
                ppp.erase(itr2);
            }
        }
        //case
        if (it->str == "case" && it->v.empty() && std::next(it) != ppp.end())
        {
            auto itr1 = std::next(it);
            auto itr2 = std::next(itr1);
            if (itr2->str == ":")
            {
                it->v.emplace_back(std::move(*itr1));
                it->v.emplace_back(std::move(*itr2));
                ppp.erase(itr1);
                ppp.erase(itr2);
            }
        }
        //default
        if (it->str == "default" && it->v.empty() && std::next(it) != ppp.end())
        {
            auto itr1 = std::next(it);
            if (itr1->str == ":")
            {
                it->v.emplace_back(std::move(*itr1));
                ppp.erase(itr1);
            }
        }
    }
    it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        for (size_t para_count = 1; para_count < keys.size(); para_count++)
        {
            if (it->type == CalUnitType::Key && it->v.size() < para_count && keys[para_count].contains(it->str))
            {
                while (it->v.size() < para_count)
                {
                    auto itr = std::next(it);
                    if (itr != ppp.end())
                    {
                        it->v.emplace_back(std::move(*itr));
                        itr = ppp.erase(itr);
                    }
                    else
                    {
                        break;    //这里应该是语法错误，缺少关键字参数，何时报错待定
                    }
                }
            }
        }
    }

    const auto find_open_else_if = [&](auto&& self, CalUnit& unit) -> CalUnit*
        {
            if (unit.type != CalUnitType::Key || unit.str != "if")
            {
                return nullptr;
            }
            if (unit.v.size() >= 3)
            {
                if (auto* nested = self(self, unit.v[2]))
                {
                    return nested;
                }
                return nullptr;
            }
            return unit.v.size() == 2 ? &unit : nullptr;
        };

    for (auto it = ppp.begin(); it != ppp.end(); ++it)
    {
        if (it->type != CalUnitType::Key || it->str != "if")
        {
            continue;
        }
        auto else_it = std::next(it);
        while (else_it != ppp.end() && else_it->type == CalUnitType::Key && else_it->str == "else")
        {
            auto* target_if = find_open_else_if(find_open_else_if, *it);
            if (target_if == nullptr || else_it->v.empty())
            {
                break;
            }
            target_if->v.emplace_back(std::move(else_it->v[0]));
            else_it = ppp.erase(else_it);
        }
    }

    for (auto& unit : ppp)
    {
        if (unit.type == CalUnitType::Key && unit.str == "goto" && unit.v.size() == 1)
        {
            if (unit.v[0].type == CalUnitType::Parameter)
            {
                unit.type = CalUnitType::Goto;
                unit.str = unit.v[0].str;
                unit.v.clear();
                unit.suffix = true;
            }
        }
    }
}

void Cifa::check_goto_targets(CalUnit& root)
{
    struct LabelInfo
    {
        std::vector<CalUnit*> blocks;
    };
    std::unordered_map<std::string, LabelInfo> labels;
    std::vector<CalUnit*> blocks;

    const auto collect_labels = [&](auto&& self, CalUnit& unit) -> void
        {
            const bool is_block = unit.type == CalUnitType::Union;
            if (is_block)
            {
                blocks.push_back(&unit);
            }
            if (unit.type == CalUnitType::Label)
            {
                if (labels.contains(unit.str))
                {
                    add_error(unit, "duplicate label '{}'", unit.str);
                }
                else
                {
                    labels.emplace(unit.str, LabelInfo{ blocks });
                }
            }
            for (auto& child : unit.v)
            {
                self(self, child);
            }
            if (is_block)
            {
                blocks.pop_back();
            }
        };
    collect_labels(collect_labels, root);

    const auto check_gotos = [&](auto&& self, CalUnit& unit) -> void
        {
            const bool is_block = unit.type == CalUnitType::Union;
            if (is_block)
            {
                blocks.push_back(&unit);
            }
            if (unit.type == CalUnitType::Goto || unit.type == CalUnitType::Key && unit.str == "goto"
                && unit.v.size() == 1 && unit.v[0].type == CalUnitType::Parameter)
            {
                const std::string& target_name = unit.type == CalUnitType::Goto ? unit.str : unit.v[0].str;
                auto target = labels.find(target_name);
                if (target == labels.end())
                {
                    add_error(unit, "goto target '{}' is not defined", target_name);
                }
                else if (target->second.blocks.size() > blocks.size()
                    || !std::equal(target->second.blocks.begin(), target->second.blocks.end(), blocks.begin()))
                {
                    add_error(unit, "goto '{}' jumps into a nested or sibling block", target_name);
                }
                if (is_block)
                {
                    blocks.pop_back();
                }
                return;
            }
            for (auto& child : unit.v)
            {
                self(self, child);
            }
            if (is_block)
            {
                blocks.pop_back();
            }
        };
    check_gotos(check_gotos, root);
}

//合并脚本中定义的函数：将函数名、参数和函数体存入当前编译状态。
void Cifa::combine_functions2(std::list<CalUnit>& ppp, bool global_scope)
{
    //合并关键字，从右向左
    std::unordered_map<std::string, std::set<size_t>> definitions_in_current_script;
    auto it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        if (it->type == CalUnitType::Function && !it->suffix)
        {
            auto itr = std::next(it);
            if (itr != ppp.end() && itr->type == CalUnitType::Union && itr->str == "{}")
            {
                Function2 f;
                f.body = std::move(*itr);
                for (auto& c : it->v)
                {
                    std::vector<CalUnit> arguments;
                    expand_comma(c, arguments);
                    for (auto& argument : arguments)
                    {
                        Function2::Argument parameter;
                        parameter.name = std::move(argument.str);
                        parameter.type_name = argument.type_name;
                        f.arguments.emplace_back(std::move(parameter));
                    }
                }
                f.return_type = it->type_name;
                const std::string name = it->str;
                const size_t argument_count = f.arguments.size();
                if (!global_scope)
                {
                    add_error(*it, "script function '{}' is only allowed in global scope", name);
                }
                else if (functions.contains(name)
                    || (compile_visible_host_functions != nullptr && compile_visible_host_functions->contains(name)))
                {
                    add_error(*it, "script function '{}' conflicts with a host function", name);
                }
                else
                {
                    // 函数定义从右向左归约，已登记的同 arity 版本来自源码更靠后的位置。
                    // 它应覆盖此前执行留下的版本；更早定义则忽略。
                    if (definitions_in_current_script[name].insert(argument_count).second)
                    {
                        compilation_functions[name][argument_count] = std::move(f);
                    }
                }
                ppp.erase(itr);
                it = ppp.erase(it);
            }
        }
    }
}

//提取并注册 struct 定义，并将其从语法树中移除
void Cifa::combine_structs(std::list<CalUnit>& ppp, bool global_scope)
{
    auto it = ppp.begin();
    while (it != ppp.end())
    {
        if (it->type == CalUnitType::Parameter && it->str == "struct")
        {
            auto it1 = std::next(it);
            if (it1 != ppp.end() && it1->type == CalUnitType::Parameter)
            {
                auto it2 = std::next(it1);
                if (it2 != ppp.end() && it2->type == CalUnitType::Union && it2->str == "{}")
                {
                    std::string struct_name = it1->str;
                    std::vector<StructField> fields;
                    for (auto& c : it2->v)
                    {
                        if (c.type == CalUnitType::Parameter && c.with_type)
                        {
                            fields.push_back(StructField{ c.str, c.type_name });
                        }
                    }
                    if (!global_scope)
                    {
                        add_error(*it, "struct '{}' is only allowed in global scope", struct_name);
                    }
                    else
                    {
                        compilation_struct_defs[struct_name] = std::move(fields);
                    }
                    it = ppp.erase(it);    // erase "struct"
                    it = ppp.erase(it);    // erase struct name
                    it = ppp.erase(it);    // erase {} body
                    if (it != ppp.end() && it->str == ";")
                    {
                        it = ppp.erase(it);    // 吃掉结尾分号
                    }
                    continue;
                }
            }
        }
        ++it;
    }
}

bool Cifa::is_valid_key(const std::string& key)
{
    const auto is_identifier_start = [](unsigned char c)
        {
            return c == '_' || std::isalpha(c) || c >= 0x80;
        };
    const auto is_identifier_char = [&](unsigned char c)
        {
            return is_identifier_start(c) || std::isdigit(c);
        };

    const bool is_identifier = !key.empty()
        && is_identifier_start(static_cast<unsigned char>(key.front()))
        && std::all_of(key.begin() + 1, key.end(), [&](char c)
            {
                return is_identifier_char(static_cast<unsigned char>(c));
            });
    return is_identifier && key != "struct" && key != "auto" && key != "void" && !keyword_tokens().contains(key)
        && !op_representations.contains(key);
}

std::string Cifa::revise_key(const std::string& key)
{
    const auto is_identifier_start = [](unsigned char c)
        {
            return c == '_' || std::isalpha(c) || c >= 0x80;
        };
    const auto is_identifier_char = [&](unsigned char c)
        {
            return is_identifier_start(c) || std::isdigit(c);
        };

    std::string revised = key.empty() ? "_" : key;
    for (size_t index = 0; index < revised.size(); ++index)
    {
        const unsigned char c = static_cast<unsigned char>(revised[index]);
        const bool valid = index == 0 ? is_identifier_start(c) : is_identifier_char(c);
        if (!valid)
        {
            revised[index] = '_';
        }
    }
    if (!is_valid_key(revised))
    {
        revised += '_';
    }
    return revised;
}

std::string Cifa::registered_type_name(const Object& object) const
{
    auto name = type_names.find(object.getType());
    return name == type_names.end() ? object.getType().name() : name->second;
}

Object Cifa::make_declared_default(const std::string& type_name) const
{
    Object result;
    result.declared_type_name = type_name;
    auto registered = registered_types.find(type_name);
    if (registered != registered_types.end()) { result.bound_type = registered->second.identity; }
    else if (find_struct_definition(type_name) != nullptr) { result.bound_type = typeid(ObjectMap); }
    return result;
}

bool Cifa::apply_declared_type(Object& object, const std::string& type_name, const CalUnit* location, bool infer_auto)
{
    if (type_name.empty())
    {
        return true;
    }

    if (type_name == "void")
    {
        set_runtime_error("variable cannot have type void", &object, location);
        return false;
    }

    auto registered = registered_types.find(type_name);
    if (type_name != "auto" && registered == registered_types.end() && find_struct_definition(type_name) == nullptr)
    {
        set_runtime_error("unknown type '" + type_name + "'", &object, location);
        return false;
    }
    object.declared_type_name = type_name;
    object.bound_type = registered != registered_types.end() ? registered->second.identity
        : std::type_index(type_name == "auto" ? typeid(void) : typeid(ObjectMap));
    if (type_name == "auto" && infer_auto && object.hasValue() && object.type1 != "NoValue")
    {
        object.bound_type = object.getType();
        object.declared_type_name = registered_type_name(object);
    }
    return true;
}

Object Cifa::convert_object_type(const Object& source, const std::string& type_name, const CalUnit* location)
{
    if (type_name.empty() || type_name == "auto")
    {
        return source;
    }
    if (source.type1 == "NoValue")
    {
        source.toDouble();
        return Object();
    }
    if (!source.hasValue())
    {
        set_runtime_error("cannot convert an empty value to '" + type_name + "'", &source, location);
        return Object();
    }

    if (type_name == "void")
    {
        return Object();
    }
    auto registered = registered_types.find(type_name);
    if (registered != registered_types.end())
    {
        const auto identity = registered->second.identity;
        if (identity != source.getType() && !(source.isNumber()
            && (identity == typeid(std::int64_t) || identity == typeid(double) || identity == typeid(bool))))
        {
            set_runtime_error("cannot convert value to '" + type_name + "'", &source, location);
            return Object();
        }
        return registered->second.convert(source);
    }
    if (find_struct_definition(type_name) != nullptr)
    {
        if (!source.isType<ObjectMap>() || (!source.declared_type_name.empty()
            && find_struct_definition(source.declared_type_name) != nullptr && source.declared_type_name != type_name))
        {
            set_runtime_error("cannot convert value to struct type '" + type_name + "'", &source, location);
            return Object();
        }
        return source;
    }

    set_runtime_error("unknown conversion type '" + type_name + "'", &source, location);
    return Object();
}

Object& Cifa::assign_object_value(Object& target, Object value, const CalUnit& lhs, const CalUnit* location)
{
    if (lhs.with_type && !apply_declared_type(target, lhs.type_name, location, false))
    {
        return target;
    }

    if (target.declared_type_name == "auto" && value.type1 == "NoValue")
    {
        set_runtime_error("cannot infer type for auto variable from NoValue", &value, location);
        return target;
    }

    if (target.isTyped() && target.bound_type == typeid(void) && value.hasValue() && value.type1 != "NoValue")
    {
        target.bound_type = value.getType();
        target.declared_type_name = find_struct_definition(value.declared_type_name) != nullptr
            ? value.declared_type_name : registered_type_name(value);
    }

    if (target.isTyped() && target.bound_type != typeid(void))
    {
        if (registered_types.contains(target.declared_type_name) || find_struct_definition(target.declared_type_name) != nullptr)
        {
            value = convert_object_type(value, target.declared_type_name, location);
        }
        else if (target.bound_type != value.getType())
        {
            set_runtime_error("cannot change the type of a static variable", &value, location);
        }
        if (has_runtime_error())
        {
            return target;
        }
    }

    target.value = std::move(value.value);
    target.element_type_name = value.element_type_name;
    target.type1 = std::move(value.type1);
    return target;
}

void Cifa::set_array_element_type(Object& array, const std::string& type_name)
{
    if (type_name.empty() || type_name == "void")
    {
        return;
    }
    array.element_type_name = type_name;
    if (!array.isType<std::vector<Object>>())
    {
        return;
    }
    for (auto& element : array.ref<std::vector<Object>>())
    {
        if (!element.isTyped())
        {
            element = make_declared_default(type_name);
        }
    }
}

template <typename Compare>
static Object compare_objects(const Object& o1, const Object& o2, Compare compare)
{
    if (o1.isType<std::string>() && o2.isType<std::string>())
    {
        return Object(compare(o1.ref<std::string>(), o2.ref<std::string>()));
    }
    if (o1.isNumber() && o2.isNumber())
    {
        if (o1.isInteger() && o2.isInteger()) { return Object(compare(o1.toInt64(), o2.toInt64())); }
        return Object(compare(o1.toDouble(), o2.toDouble()));
    }
    return Object();
}

template <char Symbol, bool Extended>
Object Cifa::evaluate_binary(const Object& o1, const Object& o2, OperatorCallbacks& callbacks)
{
    if (o1.type1 == "NoValue" || o2.type1 == "NoValue")
    {
        (o1.type1 == "NoValue" ? o1 : o2).toDouble();
        return Object();
    }
    if constexpr (Symbol == '+')
    {
        if (o1.isType<std::string>() && o2.isType<std::string>()) { return Object(o1.toString() + o2.toString()); }
    }
    if constexpr (Symbol == '<' || Symbol == '>' || Symbol == '=' || Symbol == '!')
    {
        auto result = compare_objects(o1, o2, [](const auto& left, const auto& right)
            {
                if constexpr (Symbol == '<' && Extended) { return left <= right; }
                else if constexpr (Symbol == '>' && Extended) { return left >= right; }
                else if constexpr (Symbol == '<') { return left < right; }
                else if constexpr (Symbol == '>') { return left > right; }
                else if constexpr (Symbol == '=') { return left == right; }
                else { return left != right; }
            });
        if (result.hasValue()) { return result; }
    }
    else if (o1.isNumber() && o2.isNumber())
    {
        if constexpr (Extended && (Symbol == '&' || Symbol == '|'))
        {
            if constexpr (Symbol == '&') { return Object(o1.toBool() && o2.toBool()); }
            else { return Object(o1.toBool() || o2.toBool()); }
        }
        else
        {
            if (!o1.isInteger() || !o2.isInteger())
            {
                if constexpr (Symbol == '+') { return Object(o1.toDouble() + o2.toDouble()); }
                else if constexpr (Symbol == '-') { return Object(o1.toDouble() - o2.toDouble()); }
                else if constexpr (Symbol == '*') { return Object(o1.toDouble() * o2.toDouble()); }
                else if constexpr (Symbol == '/') { return Object(o1.toDouble() / o2.toDouble()); }
                else
                {
                    const std::string symbol = Symbol == 'L' ? "<<" : Symbol == 'R' ? ">>" : std::string(1, Symbol);
                    set_runtime_error("operator " + symbol + " requires integer operands");
                    return Object();
                }
            }
            const auto left = o1.toInt64();
            const auto right = o2.toInt64();
            if constexpr (Symbol == '+' || Symbol == '-' || Symbol == '*')
            {
                const auto unsigned_left = static_cast<std::uint64_t>(left);
                const auto unsigned_right = static_cast<std::uint64_t>(right);
                if constexpr (Symbol == '+') { return Object(wrap_int64(unsigned_left + unsigned_right)); }
                else if constexpr (Symbol == '-') { return Object(wrap_int64(unsigned_left - unsigned_right)); }
                else { return Object(wrap_int64(unsigned_left * unsigned_right)); }
            }
            else if constexpr (Symbol == '/' || Symbol == '%')
            {
                if (right == 0)
                {
                    set_runtime_error(Symbol == '/' ? "integer division by zero" : "integer modulo by zero", &o2);
                    return Object();
                }
                if (left == std::numeric_limits<std::int64_t>::min() && right == -1)
                {
                    if constexpr (Symbol == '%') { return Object(0); }
                    set_runtime_error("integer division overflow", &o1);
                    return Object();
                }
                if constexpr (Symbol == '/') { return Object(left / right); }
                else { return Object(left % right); }
            }
            else if constexpr (Symbol == '&') { return Object(left & right); }
            else if constexpr (Symbol == '|') { return Object(left | right); }
            else if constexpr (Symbol == '^') { return Object(left ^ right); }
            else if constexpr (Symbol == 'L' || Symbol == 'R')
            {
                if (right < 0 || right >= 64)
                {
                    set_runtime_error(Symbol == 'L' ? "left shift count is out of range" : "right shift count is out of range", &o2);
                    return Object();
                }
                if constexpr (Symbol == 'L') { return Object(wrap_int64(static_cast<std::uint64_t>(left) << right)); }
                else { return Object(left >> right); }
            }
        }
    }
    for (auto& callback : callbacks)
    {
        auto result = callback(o1, o2);
        if (has_runtime_error()) { return Object(); }
        if (result.hasValue()) { return result; }
    }
    return Object();
}

Object Cifa::add(const Object& o1, const Object& o2) { return evaluate_binary<'+'>(o1, o2, user_add); }
Object Cifa::sub(const Object& o1, const Object& o2) { return evaluate_binary<'-'>(o1, o2, user_sub); }
Object Cifa::mul(const Object& o1, const Object& o2) { return evaluate_binary<'*'>(o1, o2, user_mul); }
Object Cifa::div(const Object& o1, const Object& o2) { return evaluate_binary<'/'>(o1, o2, user_div); }
Object Cifa::mod(const Object& o1, const Object& o2) { return evaluate_binary<'%'>(o1, o2, user_mod); }
Object Cifa::less(const Object& o1, const Object& o2) { return evaluate_binary<'<'>(o1, o2, user_less); }
Object Cifa::more(const Object& o1, const Object& o2) { return evaluate_binary<'>'>(o1, o2, user_more); }
Object Cifa::less_equal(const Object& o1, const Object& o2) { return evaluate_binary<'<', true>(o1, o2, user_less_equal); }
Object Cifa::more_equal(const Object& o1, const Object& o2) { return evaluate_binary<'>', true>(o1, o2, user_more_equal); }
Object Cifa::equal(const Object& o1, const Object& o2) { return evaluate_binary<'='>(o1, o2, user_equal); }
Object Cifa::not_equal(const Object& o1, const Object& o2) { return evaluate_binary<'!'>(o1, o2, user_not_equal); }
Object Cifa::bit_and(const Object& o1, const Object& o2) { return evaluate_binary<'&'>(o1, o2, user_bit_and); }
Object Cifa::bit_or(const Object& o1, const Object& o2) { return evaluate_binary<'|'>(o1, o2, user_bit_or); }
Object Cifa::bit_xor(const Object& o1, const Object& o2) { return evaluate_binary<'^'>(o1, o2, user_bit_xor); }
Object Cifa::logic_and(const Object& o1, const Object& o2) { return evaluate_binary<'&', true>(o1, o2, user_logic_and); }
Object Cifa::logic_or(const Object& o1, const Object& o2) { return evaluate_binary<'|', true>(o1, o2, user_logic_or); }
Object Cifa::shift_left(const Object& o1, const Object& o2) { return evaluate_binary<'L'>(o1, o2, user_shift_left); }
Object Cifa::shift_right(const Object& o1, const Object& o2) { return evaluate_binary<'R'>(o1, o2, user_shift_right); }

bool Cifa::validate_registration_name(const std::string& name)
{
    if (is_valid_key(name) && !registered_types.contains(name))
    {
        return true;
    }
    set_runtime_error("invalid registration name '" + name + "'");
    return false;
}

//注册宿主程序中的 C++ 函数
bool Cifa::register_function(const std::string& name, func_type func)
{
    if (!validate_registration_name(name)) { return false; }
    functions[name] = std::move(func);
    ++function_version;
    ++function_generations[name];
    return true;
}

//注册用户自定义数据指针
bool Cifa::register_user_data(const std::string& name, void* p)
{
    if (!validate_registration_name(name)) { return false; }
    user_data[name] = p;
    return true;
}

//注册一个全局参数变量
bool Cifa::register_parameter(const std::string& name, Object o)
{
    if (!validate_registration_name(name)) { return false; }
    global_variables[name] = std::move(o);
    return true;
}

void Cifa::set_include_dirs(const std::vector<std::string>& dirs)
{
    include_dirs = dirs;
}

//获取用户自定义数据指针
void* Cifa::get_user_data(const std::string& name)
{
    auto value = user_data.find(name);
    return value != user_data.end() ? value->second : nullptr;
}

//执行函数调用：查找已注册函数或脚本定义函数并执行
Object Cifa::run_function(const CalUnit& call_site, std::vector<CalUnit>& vc, ScopeStack& scopes)
{
    const auto& name = call_site.str;
    auto& context = execution_contexts.back();
    auto& runtime_stack = context.runtime_call_stack;
    runtime_stack.push_back({ nullptr, nullptr, name });
    RaiiGuard frame_guard([&runtime_stack]() { runtime_stack.pop_back(); });

    auto host_function = functions.find(name);
    if (host_function != functions.end())
    {
        std::vector<Object> v;
        for (auto& c : vc)
        {
            if (name == "type" && c.type == CalUnitType::Parameter)
            {
                v.emplace_back(get_parameter(c, scopes, true));
            }
            else
            {
                v.emplace_back(eval_scoped(c, scopes));
            }
            if (has_runtime_error()) { return make_error_result(); }
        }
        for (auto& value : v)
        {
            value.argument_origin = &value;
        }
        const auto* previous_arguments = context.active_function_arguments;
        const auto* previous_values = context.active_function_values;
        context.active_function_arguments = &vc;
        context.active_function_values = &v;
        RaiiGuard arguments_guard([&context, previous_arguments, previous_values]()
            {
                context.active_function_arguments = previous_arguments;
                context.active_function_values = previous_values;
            });
        auto result = host_function->second(v);
        if (has_runtime_error())
        {
            return make_error_result();
        }
        return result;
    }
    auto* overloads = find_script_function(name);
    if (overloads != nullptr)
    {
        auto overload = overloads->find(vc.size());
        if (overload == overloads->end())
        {
            std::vector<size_t> arities;
            arities.reserve(overloads->size());
            for (const auto& [arity, function] : *overloads)
            {
                arities.push_back(arity);
            }
            std::sort(arities.begin(), arities.end());
            std::string available;
            for (size_t index = 0; index < arities.size(); ++index)
            {
                if (index > 0)
                {
                    available += ", ";
                }
                available += std::to_string(arities[index]);
            }
            set_runtime_error("function '" + name + "' has no overload for " + std::to_string(vc.size())
                + " arguments; available: " + available);
            return Object();
        }
        auto& function = overload->second;
        ScopeStack fn_scopes;
        fn_scopes.emplace_back();
        for (size_t i = 0; i < function.arguments.size(); i++)
        {
            const auto& parameter = function.arguments[i];
            Object argument = eval_scoped(vc[i], scopes);
            const auto element_type = argument.element_type_name;
            argument.bound_type = typeid(void);
            argument.declared_type_name.clear();
            if (!parameter.type_name.empty())
            {
                argument = convert_object_type(argument, parameter.type_name, &vc[i]);
                if (!apply_declared_type(argument, parameter.type_name, &vc[i], parameter.type_name == "auto"))
                {
                    return make_error_result();
                }
            }
            argument.element_type_name = element_type;
            fn_scopes.back()[parameter.name] = std::move(argument);
            if (has_runtime_error()) { return make_error_result(); }
        }
        context.return_states.emplace_back();
        context.return_states.back().return_type = function.return_type;
        auto result = eval_scoped(function.body, fn_scopes);
        const auto& state = context.return_states.back();
        if (!has_runtime_error() && !is_exit_requested() && (!state.has_value || !state.value.hasValue()))
        {
            result = Object::make_no_value(name, format_runtime_frame(call_site));
        }
        context.return_states.pop_back();
        return has_runtime_error() ? make_error_result() : result;
    }
    else
    {
        set_runtime_error("function '" + name + "' is not defined");
        return Object();
    }
}

//从作用域栈中获取变量引用（用于运行时求值）
Object& Cifa::get_parameter(CalUnit& c, ScopeStack& scopes, bool only_check)
{
    // . 操作符节点：小数点读取 ObjectMap 字段
    if (c.type == CalUnitType::Operator && c.str == "." && c.v.size() == 2 && c.v[1].type == CalUnitType::Parameter)
    {
        auto* base = find_object_from_inner(scopes, c.v[0].str);
        if (base != nullptr && base->isType<ObjectMap>())
        {
            auto& m = base->ref<ObjectMap>();
            auto& elem = m[c.v[1].str];
            elem.name = c.v[0].str + "." + c.v[1].str;
            return elem;
        }
        return get_or_create_parameter(c.v[0].str + "::" + c.v[1].str, scopes);
    }
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        auto* base = find_object_from_inner(scopes, c.str);
        const bool is_map_access = (base != nullptr && base->isType<ObjectMap>())
            || (!c.v[0].v.empty() && c.v[0].v[0].type == CalUnitType::String);
        auto& element = resolve_indexed_parameter(c, scopes, only_check, false, true);
        if (!only_check && !is_map_access && !element.hasValue())
        {
            set_runtime_error("array element '" + element.name + "' has not been initialized");
        }
        return element;
    }
    auto* existing = find_object_from_inner(scopes, c.str);
    const bool existed = existing != nullptr;
    auto& object = existed ? *existing : get_or_create_parameter(c.str, scopes, true);
    object.name = c.str;
    if (c.with_type)
    {
        apply_declared_type(object, c.type_name, &c, false);
    }
    if (!only_check && existed && !c.with_type && !object.hasValue())
    {
        set_runtime_error("variable '" + object.name + "' has not been initialized");
    }
    return object;
}

//查找变量；未找到时在当前作用域创建
Object& Cifa::get_or_create_parameter(const std::string& name, ScopeStack& scopes, bool current_scope_only)
{
    auto& current_variables = scopes.empty() ? global_variables : scopes.back();
    Object* object = current_scope_only ? nullptr : find_object_from_inner(scopes, name);
    if (object == nullptr)
    {
        object = &current_variables[name];
    }
    object->name = name;
    return *object;
}

//获取赋值目标的变量引用，必要时在当前作用域创建新变量
Object& Cifa::get_parameter_for_assign(CalUnit& c, ScopeStack& scopes, bool declare_current)
{
    // . 操作符的读写目标解析相同
    if (c.type == CalUnitType::Operator && c.str == "." && c.v.size() == 2 && c.v[1].type == CalUnitType::Parameter)
    {
        return get_parameter(c, scopes, true);
    }
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        return resolve_indexed_parameter(c, scopes, false, declare_current, false);
    }

    auto& object = get_or_create_parameter(c.str, scopes, declare_current);
    if (c.with_type && !object.isTyped())
    {
        apply_declared_type(object, c.type_name, &c, false);
    }
    // struct 类型声明：直接创建并初始化 ObjectMap
    const auto* struct_definition = find_struct_definition(c.type_name);
    if (declare_current && struct_definition != nullptr)
    {
        ObjectMap m;
        for (const auto& field : *struct_definition)
        {
            m[field.name] = make_declared_default(field.type_name);
        }
        object = Object(std::move(m));
        object.name = c.str;
        object.bound_type = typeid(ObjectMap);
        object.declared_type_name = c.type_name;
    }
    return object;
}

//解析数组下标访问（如 a[i] 或多维 a[i][j]），返回元素引用，必要时自动扩展数组大小
Object& Cifa::resolve_indexed_parameter(CalUnit& c, ScopeStack& scopes, bool only_check, bool declare_current, bool declaration_as_array)
{
    //先求值下标表达式，判断是整数下标（数组）还是字符串下标（map）
    Object index_val;
    if (!only_check && c.v[0].v.size() > 0)
    {
        index_val = eval_scoped(c.v[0].v[0], scopes);
    }

    //字符串下标：使用 map<string, Object> 语义
    if (index_val.isType<std::string>())
    {
        const auto key = index_val.toString();
        auto& base = get_or_create_parameter(c.str, scopes, declare_current);
        if (!base.isType<ObjectMap>())
        {
            base = Object(ObjectMap());
            base.name = c.str;
        }
        auto& element = base.ref<ObjectMap>()[key];
        element.name = c.str + "[\"" + key + "\"]";
        return element;
    }

    int first_index = 0;
    if (index_val.hasValue())
    {
        first_index = index_val.toInt();
        if (first_index < 0)
        {
            first_index = 0;
        }
    }
    const bool is_decl_array = declaration_as_array && c.with_type && c.suffix;

    auto& base = get_or_create_parameter(c.str, scopes, declare_current);
    if (is_decl_array)
    {
        if (!base.isType<std::vector<Object>>())
        {
            base = Object(std::vector<Object>(size_t(first_index)));
            set_array_element_type(base, c.type_name);
        }
        else if (!only_check)
        {
            base.ref<std::vector<Object>>().resize(size_t(first_index));
            set_array_element_type(base, c.type_name);
        }
        base.name = c.str;
        return base;
    }

    Object* element = &base;
    std::string element_name = c.str;
    for (size_t dimension = 0; dimension < c.v.size(); ++dimension)
    {
        int index = dimension == 0 ? first_index : 0;
        if (dimension > 0 && !only_check && !c.v[dimension].v.empty())
        {
            index = eval_scoped(c.v[dimension].v[0], scopes).toInt();
            if (index < 0)
            {
                index = 0;
            }
        }
        if (!element->isType<std::vector<Object>>())
        {
            *element = Object(std::vector<Object>(size_t(index + 1)));
            element->name = element_name;
            element->element_type_name = base.element_type_name;
        }
        auto& array = element->ref<std::vector<Object>>();
        if (index >= int(array.size()))
        {
            array.resize(size_t(index + 1));
        }
        element = &array[size_t(index)];
        element_name += "[" + std::to_string(index) + "]";
        element->name = element_name;
        if (dimension + 1 == c.v.size() && !element->isTyped() && !base.element_type_name.empty())
        {
            *element = make_declared_default(base.element_type_name);
            element->name = element_name;
        }
    }
    return *element;
}

//检查非花括号分支/循环/switch case 体内不允许引入新变量，并直接报告错误
//合并了原 contains_new_var_assign 的检测逻辑与错误报告
void Cifa::check_non_block_body(CalUnit& body, const std::unordered_map<std::string, Object>& p)
{
    //花括号块内定义变量合法，跳过
    if (body.type == CalUnitType::Union && body.str == "{}")
    {
        return;
    }
    //显式声明：int x; 或 int a[];
    if (body.type == CalUnitType::Parameter && body.with_type)
    {
        add_error(body, "variable declaration not allowed in non-block body");
        return;
    }
    //赋值到新变量：int x = val（有类型前缀） 或 x = val（x 不在变量表中）
    if (body.type == CalUnitType::Operator && body.str == "=")
    {
        if (!body.v.empty() && body.v[0].type == CalUnitType::Parameter)
        {
            if (body.v[0].with_type || !p.count(body.v[0].str))
            {
                add_error(body, "variable declaration not allowed in non-block body");
                return;
            }
        }
    }
    //裸引用未声明变量：x;
    if (body.type == CalUnitType::Parameter && !body.with_type && !p.count(body.str))
    {
        add_error(body, "variable declaration not allowed in non-block body");
        return;
    }
    //逗号表达式：递归检查每一子项
    if (body.type == CalUnitType::Operator && body.str == ",")
    {
        for (auto& sub : body.v)
        {
            check_non_block_body(sub, p);
        }
    }
}

//语法检查：递归检查语法树节点的合法性（运算符、变量、函数、关键字等）
void Cifa::check_cal_unit(CalUnit& c, CalUnit* father, std::unordered_map<std::string, Object>& p,
    size_t loop_depth, size_t switch_depth)
{
    //若提前return，表示不再检查其下的结构
    if (c.type == CalUnitType::Operator && c.un_combine == false)
    {
        if (ops_single.contains(c.str))
        {
            if (c.v.size() != 1)
            {
                add_error(c, "operator {} has wrong operands", c.str);
            }
        }
        else if (operator_tokens().contains(c.str) && !ops_single.contains(c.str))
        {
            if (c.str == "=")
            {
                if (c.v.size() != 2)
                {
                    add_error(c, "operator = has wrong operands");
                }
                else
                {
                    check_cal_unit(c.v[1], &c, p, loop_depth, switch_depth);    //here make sure no undefined parameters at right of "="
                    p[c.v[0].str].name = c.v[0].str;
                    //赋值左侧的下标表达式也需要递归检查
                    for (auto& sub : c.v[0].v)
                    {
                        check_cal_unit(sub, &c.v[0], p, loop_depth, switch_depth);
                    }
                    if (c.v[0].type != CalUnitType::Parameter
                        && !(c.v[0].type == CalUnitType::Operator && c.v[0].str == "."))
                    {
                        add_error(c.v[0], "'{}' cannot be assigned", c.v[0].str);
                    }
                }
            }
            if (c.str == "::" || c.str == ".")
            {
                if (c.v.size() == 2)
                {
                    if (c.v[0].type == CalUnitType::Parameter && !p.count(c.v[0].str))
                    {
                        add_error(c.v[0], "parameter '{}' is at right of = but not been initialized", c.v[0].str);
                    }
                    else if (c.v[1].type == CalUnitType::Parameter)
                    {
                        bool ok = p.count(c.v[0].str + "::" + c.v[1].str);
                        if (!ok)
                        {
                            //若基变量是 ObjectMap，检查 key 是否存在于 map 中
                            auto& base_obj = p[c.v[0].str];
                            if (base_obj.isType<ObjectMap>() && base_obj.ref<ObjectMap>().count(c.v[1].str))
                            {
                                ok = true;
                            }
                            else if (!base_obj.isType<ObjectMap>())
                            {
                                // 基变量类型在静态分析阶段未知（如函数参数），无法验证字段，放行
                                ok = true;
                            }
                        }
                        if (!ok)
                        {
                            add_error(c.v[0], "parameter '{}' in '{}' is at right of = but not been initialized", c.v[1].str, c.v[0].str);
                        }
                    }
                }
                else
                {
                    add_error(c, "operator {} has wrong operands", c.str);
                }
            }
            if (c.str == "?")
            {
                if (c.v.size() != 2)
                {
                    add_error(c, "operator ?(:) has wrong operands");
                }
                else if (c.v[1].type != CalUnitType::Operator || c.v[1].str != ":")
                {
                    add_error(c, "operator ? has no :");
                }
                else
                {
                    if (c.v[1].v.size() != 2)
                    {
                        add_error(c.v[1], "operator : followed ? has wrong operands");
                    }
                }
            }
            if (c.v.size() == 1 && (c.str == "+" || c.str == "-"))
            {
            }
            else if (c.v.size() != 2)
            {
                add_error(c, "operator {} has wrong operands", c.str);
            }
        }
        else
        {
            add_error(c, "unknown operator {} with {} operands", c.str, c.v.size());
        }
    }
    else if (c.type == CalUnitType::Cast)
    {
        if (c.v.size() != 1)
        {
            add_error(c, "type cast '{}' has wrong operands", c.type_name);
        }
        if (c.type_name == "auto" || c.type_name == "void")
        {
            add_error(c, "cannot cast to type '{}'", c.type_name);
        }
        else if (!registered_types.contains(c.type_name) && find_struct_definition(c.type_name) == nullptr)
        {
            add_error(c, "unknown type '{}' in cast", c.type_name);
        }
    }
    else if (c.type == CalUnitType::Constant || c.type == CalUnitType::String)
    {
        if (c.v.size() > 0)
        {
            add_error(c, "cannot calculate constant {} with operands", c.str);
        };
    }
    else if (c.type == CalUnitType::Parameter)
    {
        if (c.v.size() > 0 && c.v[0].str != "[]")
        {
            add_error(c, "cannot calculate parameter '{}' with operands", c.str);
        }
        //带类型前缀的独立声明（如 int i;），注册变量到作用域
        if (c.with_type)
        {
            if (c.type_name == "auto" && father != nullptr && father->type == CalUnitType::Union)
            {
                add_error(c, "auto variable '{}' requires an initializer", c.str);
                return;
            }
            const auto* struct_definition = find_struct_definition(c.type_name);
            if (struct_definition != nullptr)
            {
                ObjectMap m;
                for (const auto& field : *struct_definition)
                {
                    m[field.name] = make_declared_default(field.type_name);
                }
                p[c.str] = Object(std::move(m));
                p[c.str].bound_type = typeid(ObjectMap);
                p[c.str].declared_type_name = c.type_name;
            }
            p[c.str].name = c.str;
        }
        else if (father && father->type == CalUnitType::Operator)
        {
            if (father->str == "::" || father->str == "."
                || (father->str == ":" && father->v.size() >= 1 && &father->v[0] == &c))
            {
                // do nothings
            }
            else if (father->str == "=" && father->v.size() >= 1 && &father->v[0] == &c)
            {
                //赋值左侧不需要初始化检查（这是定义/写入）
            }
            else
            {
                //所有表达式上下文中的参数都需要初始化检查
                if (!p.count(c.str))
                {
                    add_error(c, "parameter '{}' has not been initialized", c.str);
                }
            }
        }
        //非运算符子节点中的参数（如 return、函数调用参数等）也检查
        else if (father && father->type != CalUnitType::Operator && !c.with_type)
        {
            if (father->type == CalUnitType::Function
                || father->type == CalUnitType::Key
                || father->type == CalUnitType::Union)
            {
                if (!p.count(c.str))
                {
                    add_error(c, "parameter '{}' has not been initialized", c.str);
                }
            }
        }
    }
    else if (c.type == CalUnitType::Function)
    {
        const auto* script_overloads = find_script_function(c.str);
        const bool has_zero_argument_script_overload = script_overloads != nullptr && script_overloads->contains(0);
        if (c.v.size() == 0 && c.str != "exit" && !has_zero_argument_script_overload)
        {
            add_error(c, "function '{}' has no operands", c.str);
        }
        //内置方法名不视为未定义函数
        if (!functions.contains(c.str)
            && (compile_visible_host_functions == nullptr || !compile_visible_host_functions->contains(c.str))
            && !builtin_methods.contains(c.str)
            && (script_overloads == nullptr || script_overloads->empty()))
        {
            add_error(c, "function '{}' is not defined", c.str);
        }
    }
    else if (c.type == CalUnitType::Key)
    {
        if (c.str == "goto")
        {
            return;
        }
        if (c.str == "if")
        {
            if (c.v.size() == 0)
            {
                add_error(c, "if has no condition");
            }
            if (c.v.size() >= 1 && c.v[0].type == CalUnitType::None)
            {
                add_error(c, "if has empty condition");
            }
            if (c.v.size() == 1)
            {
                add_error(c, "if has no statement");
            }
            if (c.v.size() >= 2 && !c.v[1].is_statement())
            {
                add_error(c.v[1], "missing ;");
            }
            if (c.v.size() >= 3)
            {
                if (c.v[2].str == "else")
                {
                    add_error(c.v[2], "else has no if");
                }
                else if (!c.v[2].is_statement())
                {
                    add_error(c.v[2], "missing ;");
                }
            }
            //分支体为非花括号语句时，不允许引入新变量
            if (c.v.size() >= 2)
            {
                check_non_block_body(c.v[1], p);
            }
            if (c.v.size() >= 3 && c.v[2].type != CalUnitType::Key)    //else if(...) 不需要检查
            {
                check_non_block_body(c.v[2], p);
            }
        }
        if (c.str == "else")    //语法树合并后不应有单独的else
        {
            add_error(c, "else has no if");
        }
        if (c.str == "for")
        {
            CalUnit* range_clause = c.v[0].type == CalUnitType::Operator && c.v[0].str == ":" ? &c.v[0] : nullptr;
            if (range_clause == nullptr && c.v[0].type == CalUnitType::Union && c.v[0].str == "()" && c.v[0].v.size() == 1
                && c.v[0].v[0].type == CalUnitType::Operator && c.v[0].v[0].str == ":")
            {
                range_clause = &c.v[0].v[0];
            }
            const bool is_range_for = range_clause != nullptr && range_clause->v.size() == 2
                && range_clause->v[0].type == CalUnitType::Parameter;
            if (!is_range_for && (c.v[0].type != CalUnitType::Union || c.v[0].str != "()" || c.v[0].v.size() != 3
                || !c.v[0].v[0].is_statement() || !c.v[0].v[1].is_statement() || (c.v[0].v[2].is_statement() && c.v[0].v[2].type != CalUnitType::None)))
            {
                add_error(c, "for loop condition is not right");
            }
            if (is_range_for)
            {
                p[range_clause->v[0].str].name = range_clause->v[0].str;
            }
            if (c.v.size() >= 2 && !c.v[1].is_statement())
            {
                add_error(c.v[1], "missing ;");
            }
            //分支体为非花括号语句时，不允许引入新变量
            if (c.v.size() >= 2)
            {
                check_non_block_body(c.v[1], p);
            }
        }
        if (c.str == "while")
        {
            if (c.v.size() == 0)
            {
                add_error(c, "while has no condition");
            }
            if (c.v.size() >= 1 && c.v[0].type == CalUnitType::None)
            {
                add_error(c, "while has empty condition");
            }
            if (c.v.size() == 1 && !(father && father->str == "do"))
            {
                add_error(c, "while has no statement");
            }
            if (c.v.size() >= 2 && !c.v[1].is_statement())
            {
                add_error(c.v[1], "missing ;");
            }
            //分支体为非花括号语句时，不允许引入新变量（do-while 的 while 子句无体，跳过）
            if (c.v.size() >= 2 && !(father && father->str == "do"))
            {
                check_non_block_body(c.v[1], p);
            }
        }
        if (c.str == "do")
        {
            if (c.v.size() == 0)
            {
                add_error(c, "do while has no statement and condition");
            }
            if (c.v.size() == 1)
            {
                if (c.v[0].str != "while")
                {
                    add_error(c, "do while has no while keyword");
                }
                else
                {
                    add_error(c, "do while has no statement");
                }
            }
            if (c.v.size() == 2)
            {
                if (c.v[1].v.size() < 1)
                {
                    add_error(c, "do while has no condition");
                }
            }
        }
        if (c.str == "switch")
        {
            if (c.v.size() == 0)
            {
                add_error(c, "switch has no condition");
            }
            if (c.v.size() == 1)
            {
                add_error(c, "switch has no statement");
            }
            //switch case 体内（非 {} 包裹的语句）不允许引入新变量
            if (c.v.size() >= 2 && c.v[1].type == CalUnitType::Union && c.v[1].str == "{}")
            {
                for (auto& c1 : c.v[1].v)
                {
                    //跳过 case/default 标签本身
                    if (c1.type == CalUnitType::Key && (c1.str == "case" || c1.str == "default"))
                    {
                        continue;
                    }
                    check_non_block_body(c1, p);
                }
            }
        }
        if (c.str == "case")
        {
            if (c.v.size() == 0)
            {
                add_error(c, "case has no condition");
            }
            if (c.v.size() < 2 || c.v.size() == 2 && c.v[1].str != ":")
            {
                add_error(c, "case missing :");
            }
        }
        if (c.str == "default")
        {
            if (c.v.size() < 1 || c.v.size() == 1 && c.v[0].str != ":")
            {
                add_error(c, "default missing :");
            }
        }
        if (c.str == "return")
        {
            if (c.v.size() == 0 || !c.v[0].is_statement())
            {
                add_error(c, "{} missing ;", c.str);
            }
        }
        if (c.str == "break" || c.str == "continue")
        {
            if (c.v.size() == 0 || c.v[0].str != ";")
            {
                add_error(c, "{} missing ;", c.str);
            }
            if (c.str == "break" && loop_depth == 0 && switch_depth == 0)
            {
                add_error(c, "break statement is not within a loop or switch");
            }
            if (c.str == "continue" && loop_depth == 0)
            {
                add_error(c, "continue statement is not within a loop");
            }
        }
    }
    else if (c.type == CalUnitType::Union)
    {
        if (father == nullptr || c.str == "{}" && (father->type == CalUnitType::Union || father->type == CalUnitType::Key))
        {
            for (auto& c1 : c.v)
            {
                if (!c1.is_statement())
                {
                    add_error(c1, "missing ;");
                }
            }
        }
        if (c.str == "[]")
        {
            if (c.v.size() == 0)
            {
                //int a[]; 声明空数组时允许空下标
                if (!(father && father->with_type))
                {
                    add_error(c, "no parameters inside []");
                }
            }
            else if (c.v[0].str == ",")
            {
                add_error(c, "wrong parameters inside []");
            }
            else
            {
                for (auto& c1 : c.v)
                {
                    if (c1.is_statement())
                    {
                        add_error(c1, "semicolon inside []");
                    }
                }
            }
        }
        if (c.str == "()")
        {
            //如果为空则是圆括号，除了for之外，里面不应出现语句或多个参数
            bool is_for = father && father->str == "for";
            if ((father == nullptr || !is_for) && c.v.size() > 1)
            {
                add_error(c, "wrong parameters inside ()");
            }
            if (father && !is_for)
            {
                for (auto& c1 : c.v)
                {
                    if (c1.is_statement())
                    {
                        add_error(c1, "semicolon inside ()");
                    }
                }
            }
        }
    }
    else if (c.type == CalUnitType::Type)
    {
        //不应存在类型符号
        add_error(c, "type {} has operands", c.str);
    }
    const bool enters_loop = c.type == CalUnitType::Key
        && (c.str == "for" || c.str == "while" || c.str == "do");
    const bool enters_switch = c.type == CalUnitType::Key && c.str == "switch";
    for (auto& c1 : c.v)
    {
        //=的子节点已在上方显式处理过，跳过避免重复检查
        if (c.type == CalUnitType::Operator && c.str == "=" && c.un_combine == false)
        {
            continue;
        }
        check_cal_unit(c1, &c, p, loop_depth + enters_loop, switch_depth + enters_switch);
    }
}

//运行脚本，使用实例全局变量表；按当前目录和include搜索目录处理#include
Object Cifa::run_script(std::string script)
{
    return compile_script_internal(std::move(script)) ? run_compilation_result() : make_error_result();
}

Object Cifa::make_error_result() const
{
    return Object("", "Error");
}

bool Cifa::compile_script_internal(std::string script)
{
    run_compilation([this, script = std::move(script)]() mutable
        {
            std::set<std::string> visited;
            script = preprocess_includes(script, "<script>", ".", include_dirs, visited);
            compile_pipeline(std::move(script));
        });
    return compiled && !compile_failed;
}

//从文件运行脚本，使用实例全局变量表
Object Cifa::run_file(const std::string& filename)
{
    return compile_file_internal(filename) ? run_compilation_result() : make_error_result();
}

bool Cifa::compile_file_internal(const std::string& filename)
{
    run_compilation([this, filename]()
        {
            std::string str;
            if (!read_text_file(filename, str))
            {
                add_error(filename, 1, 1, "cannot open file: {}", filename);
                if (output_error)
                {
                    print_errors();
                }
                return;
            }
            std::set<std::string> visited;
            visited.insert(normalize_path(filename));
            std::string dir = get_directory(filename);
            str = preprocess_includes(str, normalize_path(filename), dir, include_dirs, visited);
            compile_pipeline(std::move(str));
        });
    return compiled && !compile_failed;
}

Object Cifa::run_compilation_result()
{
    const bool is_root = execution_contexts.empty();
    if (is_root)
    {
        errors.clear();
        clear_runtime_error();
    }

    execution_contexts.emplace_back();
    auto& context = execution_contexts.back();
    context.root = std::move(compilation_root);
    context.functions = std::move(compilation_functions);
    context.struct_defs = std::move(compilation_struct_defs);
    context.source_line_infos = std::move(compilation_source_line_infos);
    if (!is_root)
    {
        context.runtime_call_stack = execution_contexts[execution_contexts.size() - 2].runtime_call_stack;
    }

    RaiiGuard context_guard([this]()
        {
            auto& completed = execution_contexts.back();
            if (execution_contexts.size() > 1)
            {
                auto& parent = execution_contexts[execution_contexts.size() - 2];
                parent.errors.insert(std::make_move_iterator(completed.errors.begin()),
                    std::make_move_iterator(completed.errors.end()));
                if (parent.runtime_error_message.empty() && !completed.runtime_error_message.empty())
                {
                    parent.runtime_error_message = std::move(completed.runtime_error_message);
                    parent.runtime_error_call_stack = std::move(completed.runtime_error_call_stack);
                }
            }
            else
            {
                errors = std::move(completed.errors);
                runtime_error_call_stack = std::move(completed.runtime_error_call_stack);
                runtime_error_message = std::move(completed.runtime_error_message);
                last_exit_requested = completed.exit_requested;
            }
            execution_contexts.pop_back();
        });

    for (const auto& [name, overloads] : context.functions)
    {
        for (const auto& [argument_count, function] : overloads)
        {
            functions2[name][argument_count] = function;
        }
    }
    for (const auto& [name, fields] : context.struct_defs)
    {
        struct_defs[name] = fields;
    }

    Object::set_runtime_error_reporter([this](const std::string& message, const Object* source)
        {
            set_runtime_error(message, source);
        });
    RaiiGuard reporter_guard([]() { Object::clear_runtime_error_reporter(); });
    context.return_states.emplace_back();

    ScopeStack run_scopes;
    auto result = eval_scoped(context.root, run_scopes);
    context.return_states.pop_back();
    if (has_runtime_error())
    {
        return make_error_result();
    }
    return result;
}

void Cifa::run_compilation(const std::function<void()>& action)
{
    if (execution_contexts.empty())
    {
        errors.clear();
        clear_runtime_error();
    }
    compilation_root = CalUnit{};
    compilation_functions.clear();
    compilation_struct_defs.clear();
    compilation_source_line_infos.clear();
    compile_failed = false;
    compiled = false;
    compiling = true;
    action();
    compiling = false;
}

//脚本编译管线：完成词法分析、语法树构建和静态检查。
void Cifa::compile_pipeline(std::string str)
{
    str += ";";    //方便处理仅有一行的情况
    auto rv = split(str);
    auto c = combine_all_cal(rv);    //结果必定是一个Union
    check_goto_targets(c);
    //即使解析阶段已发现错误，仍继续静态检查，以便一次报告尽可能多的错误
    {
        auto p1 = global_variables;
        check_cal_unit(c, nullptr, p1);
        for (auto& [name, overloads] : compilation_functions)
        {
            for (auto& [argument_count, func2] : overloads)
            {
                auto function_parameters = p1;
                for (auto& argument : func2.arguments)
                {
                    function_parameters[argument.name] = make_declared_default(argument.type_name);
                }
                check_goto_targets(func2.body);
                check_cal_unit(func2.body, nullptr, function_parameters);
            }
        }
    }
    if (!compile_failed)
    {
        compilation_root = std::move(c);
        compiled = true;
        return;
    }

    if (output_error)
    {
        print_errors();
    }
}

//获取文件路径中的目录部分
std::string Cifa::get_directory(const std::string& filepath)
{
    const auto separator = filepath.find_last_of("/\\");
    return separator == std::string::npos ? "." : filepath.substr(0, separator);
}

bool Cifa::is_absolute_path(const std::string& filepath)
{
    if (filepath.empty())
    {
        return false;
    }
    if (filepath[0] == '/' || filepath[0] == '\\')
    {
        return true;
    }
    return filepath.size() >= 3 && ((filepath[0] >= 'A' && filepath[0] <= 'Z') || (filepath[0] >= 'a' && filepath[0] <= 'z')) && filepath[1] == ':' && (filepath[2] == '/' || filepath[2] == '\\');
}

bool Cifa::read_text_file(const std::string& filename, std::string& content)
{
    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        return false;
    }
    content.assign(std::istreambuf_iterator<char>(ifs), std::istreambuf_iterator<char>());
    return true;
}

//规范化路径：将\替换为/，解析.和..
static std::string normalize_path(const std::string& path)
{
    std::string normalized = path;
    for (auto& c : normalized)
    {
        if (c == '\\')
        {
            c = '/';
        }
    }
    std::vector<std::string> parts;
    std::stringstream ss(normalized);
    std::string part;
    while (std::getline(ss, part, '/'))
    {
        if (part.empty() || part == ".")
        {
            continue;
        }
        if (part == "..")
        {
            if (!parts.empty())
            {
                parts.pop_back();
            }
        }
        else
        {
            parts.push_back(part);
        }
    }
    if (parts.empty())
    {
        return ".";
    }
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0)
        {
            result += "/";
        }
        result += parts[i];
    }
    return result;
}

//预处理#include指令：递归展开所有包含的文件
std::string Cifa::preprocess_includes(const std::string& source, const std::string& current_file, const std::string& current_dir, const std::vector<std::string>& extra_include_dirs, std::set<std::string>& visited)
{
    auto& source_line_infos = compilation_source_line_infos;
    std::stringstream source_stream(source);
    std::string line;
    std::string result;
    size_t line_num = 0;

    while (std::getline(source_stream, line))
    {
        ++line_num;
        //查找行首的#include指令（允许前导空白）
        size_t first_non_space = line.find_first_not_of(" \t");
        if (first_non_space == std::string::npos || line.compare(first_non_space, 8, "#include") != 0)
        {
            result += line + "\n";
            source_line_infos.push_back({ current_file, line_num, line });
            continue;
        }
        //解析文件名
        std::string rest = line.substr(first_non_space + 8);
        size_t filename_start = rest.find_first_not_of(" \t");
        if (filename_start == std::string::npos)
        {
            source_line_infos.push_back({ current_file, line_num, line });
            add_error(current_file, line_num, first_non_space + 1, "#include: missing filename");
            result += "\n";
            continue;
        }
        char open_char = rest[filename_start];
        char close_char = 0;
        if (open_char == '"')
        {
            close_char = '"';
        }
        else if (open_char == '<')
        {
            close_char = '>';
        }
        else
        {
            source_line_infos.push_back({ current_file, line_num, line });
            add_error(current_file, line_num, first_non_space + 1, "#include: invalid syntax, expected '\"' or '<'");
            result += "\n";
            continue;
        }
        size_t filename_end = rest.find(close_char, filename_start + 1);
        if (filename_end == std::string::npos)
        {
            source_line_infos.push_back({ current_file, line_num, line });
            add_error(current_file, line_num, first_non_space + 1, "#include: missing closing '{}'", close_char);
            result += "\n";
            continue;
        }
        std::string include_filename = rest.substr(filename_start + 1, filename_end - filename_start - 1);

        std::vector<std::string> candidates;
        if (is_absolute_path(include_filename))
        {
            candidates.push_back(include_filename);
        }
        else
        {
            candidates.push_back((current_dir.empty() ? "." : current_dir) + "/" + include_filename);
            for (const auto& dir : extra_include_dirs)
            {
                candidates.push_back((dir.empty() ? "." : dir) + "/" + include_filename);
            }
        }

        std::string full_path;
        std::string normalized;
        std::string included_content;
        bool found = false;
        for (const auto& candidate : candidates)
        {
            std::string candidate_normalized = normalize_path(candidate);
            if (visited.count(candidate_normalized))
            {
                full_path = candidate;
                normalized = std::move(candidate_normalized);
                break;
            }
            if (read_text_file(candidate, included_content)
                || candidate_normalized != candidate && read_text_file(candidate_normalized, included_content))
            {
                found = true;
                full_path = candidate;
                normalized = std::move(candidate_normalized);
                break;
            }
        }

        if (!normalized.empty() && visited.count(normalized))
        {
            result += "\n";    //跳过已包含的文件，插入空行保持行号
            source_line_infos.push_back({ current_file, line_num, line });
            continue;
        }
        if (!found)
        {
            source_line_infos.push_back({ current_file, line_num, line });
            add_error(current_file, line_num, first_non_space + 1, "#include: cannot open file '{}'", include_filename);
            result += "\n";
            continue;
        }
        visited.insert(normalized);
        //递归预处理被包含的文件
        std::string included_dir = get_directory(full_path);
        std::string processed = preprocess_includes(included_content, normalized, included_dir, extra_include_dirs, visited);
        result += processed;
    }
    return result;
}

//将所有错误格式化为字符串（带源码行和插入符）
std::string Cifa::get_errors_str() const
{
    std::string str;
    for (const auto& e : active_errors())
    {
        str += "Syntax Error: " + e.message + "\n";
        if (e.has_source_text)
        {
            const std::string& line_text = e.source_text;
            std::string header = "  at " + e.filename + ":" + std::to_string(e.line) + ", col " + std::to_string(e.col) + ": ";
            str += header + line_text + "\n";
            size_t arrow_col = e.col > 0 ? (e.col - 1) : 0;
            std::string caret_line(header.size(), ' ');
            const size_t prefix_len = std::min(arrow_col, line_text.size());
            for (size_t i = 0; i < prefix_len; ++i)
            {
                caret_line += (line_text[i] == '\t') ? '\t' : ' ';
            }
            caret_line += "^";
            str += caret_line + "\n";
        }
        else
        {
            if (!e.filename.empty())
            {
                str += "  at " + e.filename + ":" + std::to_string(e.line) + ", col " + std::to_string(e.col) + "\n";
            }
            else
            {
                str += "  at line " + std::to_string(e.line) + ", col " + std::to_string(e.col) + "\n";
            }
        }
    }
    return str;
}

//将编译期错误信息输出到 stderr（委托 get_errors_str() 避免重复逻辑）
void Cifa::print_errors() const
{
    std::print(stderr, "{}", get_errors_str());
}

//获取所有编译期错误的列表，建议优先使用 get_errors_str() 或 print_errors()
std::vector<Cifa::ErrorMessage> Cifa::get_errors() const
{
    const auto& current_errors = active_errors();
    return std::vector<ErrorMessage>(current_errors.begin(), current_errors.end());
}

//格式化一个运行时调用栈帧：显示行号、源码行和插入符位置
std::string Cifa::format_runtime_frame(const CalUnit& c) const
{
    return format_runtime_frame(c, active_source_line_infos());
}

std::string Cifa::format_runtime_frame(const CalUnit& c, const std::vector<SourceLineInfo>& source_line_infos)
{
    std::string label = c.str.empty() ? "<none>" : c.str;
    std::string line_text;
    std::string filename = "<script>";
    size_t line = c.line;
    if (c.line > 0 && c.line <= source_line_infos.size())
    {
        const auto& source_line = source_line_infos[c.line - 1];
        filename = source_line.filename;
        line = source_line.line;
        line_text = source_line.text;
    }
    if (line_text.empty())
    {
        line_text = label;
    }

    std::string header = filename + ":" + std::to_string(line) + ", col " + std::to_string(c.col) + ": ";
    size_t arrow_col = c.col > 0 ? (c.col - 1) : 0;
    std::string caret_line(header.size(), ' ');
    const size_t prefix_len = std::min(arrow_col, line_text.size());
    for (size_t i = 0; i < prefix_len; ++i)
    {
        caret_line += (line_text[i] == '\t') ? '\t' : ' ';
    }
    if (arrow_col > prefix_len)
    {
        caret_line.append(arrow_col - prefix_len, ' ');
    }
    caret_line += "^";
    return header + line_text + "\n" + caret_line;
}

std::string Cifa::format_runtime_frame(const RuntimeFrame& frame)
{
    if (!frame.function_name.empty()) return "func " + frame.function_name + "()";
    if (frame.node == nullptr || frame.source_lines == nullptr) return "<unknown>";
    return format_runtime_frame(*frame.node, *frame.source_lines);
}

//设置运行时错误消息（仅记录第一个错误，后续错误忽略）
void Cifa::set_runtime_error(const std::string& message, const Object* source, const CalUnit* location)
{
    if (has_runtime_error())
    {
        return;
    }
    if (execution_contexts.empty())
    {
        runtime_error_message = message.empty() ? "runtime error" : message;
        runtime_error_call_stack.clear();
        if (output_error)
        {
            print_runtime_error();
        }
        return;
    }
    request_exit();
    auto& context = execution_contexts.back();
    auto& error_call_stack = context.runtime_error_call_stack;
    error_call_stack.clear();
    error_call_stack.reserve(context.runtime_call_stack.size() + (location == nullptr ? 0 : 1));
    for (const auto& frame : context.runtime_call_stack)
    {
        error_call_stack.push_back(format_runtime_frame(frame));
    }
    if (location != nullptr)
    {
        error_call_stack.push_back(format_runtime_frame(*location));
    }
    auto& error_message = context.runtime_error_message;
    const auto* function_arguments = context.active_function_arguments;
    const auto* function_values = context.active_function_values;
    if (source != nullptr && function_arguments != nullptr && function_values != nullptr)
    {
        const Object* origin = source->argument_origin != nullptr ? source->argument_origin : source;
        const size_t count = std::min(function_arguments->size(), function_values->size());
        for (size_t index = 0; index < count; ++index)
        {
            if (&(*function_values)[index] == origin)
            {
                error_call_stack.push_back(format_runtime_frame((*function_arguments)[index]));
                break;
            }
        }
    }
    error_message = message.empty() ? "runtime error" : message;
    if (source != nullptr && source->getSpecialType() == "NoValue")
    {
        const auto* object = std::get_if<std::any>(&source->value);
        const auto* no_value = object == nullptr ? nullptr : std::any_cast<Object::NoValue>(object);
        if (no_value != nullptr && !no_value->call_frame.empty())
        {
            error_message += "\nNo return value originated at:\n" + no_value->call_frame;
            if (!error_call_stack.empty() && error_call_stack.back() == no_value->call_frame)
            {
                error_call_stack.pop_back();
            }
        }
    }
    if (output_error)
    {
        print_runtime_error();
    }
}

//清除运行时错误状态（调用栈、源码行缓存、错误消息）
void Cifa::clear_runtime_error()
{
    if (!execution_contexts.empty())
    {
        auto& context = execution_contexts.back();
        context.runtime_call_stack.clear();
        context.runtime_error_call_stack.clear();
        context.runtime_error_message.clear();
        context.exit_requested = false;
    }
    else
    {
        runtime_error_call_stack.clear();
        runtime_error_message.clear();
        last_exit_requested = false;
    }
}

//格式化运行时错误和调用栈（仅去除相邻且内容完全相同的栈帧）
std::string Cifa::format_runtime_error() const
{
    const auto& error_message = execution_contexts.empty() ? runtime_error_message : execution_contexts.back().runtime_error_message;
    const auto& error_call_stack = execution_contexts.empty() ? runtime_error_call_stack : execution_contexts.back().runtime_error_call_stack;
    if (error_message.empty())
    {
        return "";
    }
    std::string result = "Runtime Error: " + error_message + "\n";
    if (error_call_stack.empty())
    {
        return result;
    }
    result += "Call Stack (most recent call first):\n";
    // Keep distinct columns from a shared source line; only remove exact duplicates.
    const std::string* last_frame = nullptr;
    for (auto it = error_call_stack.rbegin(); it != error_call_stack.rend(); ++it)
    {
        const std::string& frame = *it;
        if (last_frame != nullptr && frame == *last_frame)
        {
            continue;
        }
        last_frame = &frame;
        size_t newline_pos = frame.find('\n');
        if (newline_pos == std::string::npos)
        {
            result += "  at " + frame + "\n";
        }
        else
        {
            std::string first_line = frame.substr(0, newline_pos);
            std::string rest = frame.substr(newline_pos + 1);
            result += "  at " + first_line + "\n";

            size_t start = 0;
            while (start <= rest.size())
            {
                size_t pos = rest.find('\n', start);
                std::string continuation = (pos == std::string::npos) ? rest.substr(start) : rest.substr(start, pos - start);
                if (!continuation.empty())
                {
                    result += "     " + continuation + "\n";
                }
                if (pos == std::string::npos)
                {
                    break;
                }
                start = pos + 1;
            }
        }
    }
    return result;
}

std::string Cifa::get_runtime_error() const
{
    return format_runtime_error();
}

//输出运行时错误信息和调用栈到 stderr
void Cifa::print_runtime_error() const
{
    std::print(stderr, "{}", format_runtime_error());
}
}    // namespace cifa
