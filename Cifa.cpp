#include "Cifa.h"
#include <algorithm>
#include <cstdarg>
#include <fstream>
#include <format>
#include <iostream>
#include <sstream>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace cifa
{

static std::string normalize_path(const std::string& path);

//构造函数：注册内置函数（print, println, 数学函数等）
Cifa::Cifa()
{
    //输出辅助：先检查数字再检查字符串，不可输出类型调 toString 使报错显示 "<empty> to string"
    //返回 false 表示遇到了不可输出的类型（已触发 runtime error）
    auto print_object = [](const Object& d1) -> bool
    {
        if (d1.isNumber())
        {
            std::cout << d1.toDouble();
            return true;
        }
        if (d1.isType<std::string>())
        {
            std::cout << d1.toString();
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
            if (ok) { std::cout << "\n"; }
            return Object(double(d.size()));
        });
    register_function("to_string", [](ObjectVector& d)
        {
            if (d.empty())
            {
                return Object("");
            }
            std::ostringstream stream;
            stream << d[0].toDouble();
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
    register_function("import", [this](ObjectVector& d) -> Object
        {
            if (d.size() != 1)
            {
                set_runtime_error("function 'import' expects 1 arguments, got " + std::to_string(d.size()));
                return Object();
            }
            return Object(import_module(d[0].toString()));
        });
    //parameters["true"] = Object(1, "__");
    //parameters["false"] = Object(0, "__");
    //parameters["break"] = Object("break", "__");
    //parameters["continue"] = Object("continue", "__");
    auto ifv = [](ObjectVector& x) -> Object
    {
        if (x.size() != 3) { return cifa::Object(); }
        int x0 = x[0];
        double x1 = x[1];
        double x2 = x[2];
        return (x0) ? x1 : x2;
    };
    register_function("ifv", ifv);
    register_function("ifvalue", ifv);

    register_function("max", [](ObjectVector& x) -> Object
        {
            if (x.size() == 0) { return cifa::Object(); }
            if (x.size() == 1)
            {
                return x[0];
            }
            double max_val = x[0];
            for (int i = 1; i < x.size(); i++)
            {
                double v = x[i];
                if (max_val < v)
                {
                    max_val = v;
                }
            }
            return max_val;
        });

    register_function("min", [](ObjectVector& x) -> Object
        {
            if (x.size() == 0) { return cifa::Object(); }
            if (x.size() == 1)
            {
                return x[0];
            }
            double min_val = x[0];
            for (int i = 1; i < x.size(); i++)
            {
                double v = x[i];
                if (min_val > v)
                {
                    min_val = v;
                }
            }
            return min_val;
        });
    register_function("random", [](ObjectVector& x) -> Object
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
        });
    register_function("size", [](ObjectVector& x) -> Object
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
            }
        });
    // 按 printf 格式说明符将一个 Object 转为字符串
    // spec 含用户原始长度修饰符（如 %llu），此处统一剥离后按类型重新添加
    auto sprintf_sub = [](const Object& arg, const std::string& spec, char tc) -> std::string
    {
        char buf[512] = {};
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
            snprintf(buf, sizeof(buf), (base + (tc == 'd' ? "lld" : "lli")).c_str(), (long long)arg.toDouble());
        }
        else if (tc == 'u' || tc == 'o' || tc == 'x' || tc == 'X')
        {
            snprintf(buf, sizeof(buf), (base + "ll" + tc).c_str(), (unsigned long long)arg.toDouble());
        }
        else    // %f %e %E %g %G %a（cifa 数值均为 double，忽略 L 修饰符）
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
    // fspec 为空：用 std::format "{}"（编译期字面量），double 自动省略多余零（如 42.0→4 2）
    // fspec 非空：格式字串是运行时値，必须用 std::vformat；按末尾字符选择传入的原生类型
    auto format_sub = [](const Object& arg, const std::string& fspec = {}) -> std::string
    {
        if (fspec.empty())
        {
            if (arg.isNumber())
            {
                double v = arg.toDouble();
                return std::format("{}", v);
            }
            std::string s = arg.toString();
            return std::format("{}", s);
        }
        std::string fmt_str = "{:" + fspec + "}";
        try
        {
            char last = fspec.back();
            if (!arg.isNumber() || last == 's')
            {
                std::string sv = arg.toString();
                return std::vformat(fmt_str, std::make_format_args(sv));
            }
            if (std::string_view("diouxXbB").find(last) != std::string_view::npos)
            {
                long long iv = (long long)arg.toDouble();
                return std::vformat(fmt_str, std::make_format_args(iv));
            }
            double dv = arg.toDouble();
            return std::vformat(fmt_str, std::make_format_args(dv));
        } catch (const std::format_error&)
        {
            if (arg.isNumber())
            {
                double v = arg.toDouble();
                return std::format("{}", v);
            }
            std::string s = arg.toString();
            return std::format("{}", s);
        }
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

#define REGISTER_MATH1(func) register_function(#func, static_cast<double(*)(double)>(&std::func))
#define REGISTER_MATH2(func) register_function(#func, static_cast<double(*)(double, double)>(&std::func))
    REGISTER_MATH1(abs);
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
}

Cifa::~Cifa()
{
    functions.clear();
    for (void* module : imported_modules)
    {
        if (module != nullptr)
        {
#ifdef _WIN32
            FreeLibrary(static_cast<HMODULE>(module));
#else
            dlclose(module);
#endif
        }
    }
}

//从作用域栈的最内层向外查找变量，找到则返回指针，否则返回 nullptr
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
    return nullptr;
}

//检查作用域栈中是否已存在返回值
bool Cifa::has_return_value(const ScopeStack& scopes) const
{
    if (scopes.empty())
    {
        return false;
    }
    return scopes.front().count("return") > 0;
}

//获取作用域栈中的返回值引用
Object& Cifa::return_value(ScopeStack& scopes)
{
    if (scopes.empty())
    {
        scopes.emplace_back();
    }
    return scopes.front()["return"];
}

//RAII 守卫：在作用域退出时自动弹出运行时调用栈的栈帧
struct RuntimeFrameGuard
{
    std::vector<std::string>& stack;
    RuntimeFrameGuard(std::vector<std::string>& s) :
        stack(s) {}
    ~RuntimeFrameGuard()
    {
        if (!stack.empty())
        {
            stack.pop_back();
        }
    }
};

//对数组或 map 对象执行内置方法（push_back / erase / contains 等）
//obj 必须是对原始变量的引用；args 是已展开的参数列表（CalUnit）
Object Cifa::eval_builtin_method(const std::string& method_name, Object& obj, std::vector<CalUnit>& args, ScopeStack& scopes)
{
    if (obj.isType<std::vector<Object>>())
    {
        auto& arr = obj.ref<std::vector<Object>>();
        if (method_name == "push_back")
        {
            for (auto& a : args) { arr.push_back(eval_scoped(a, scopes)); }
            return Object(double(arr.size()));
        }
        if (method_name == "pop_back")
        {
            if (!arr.empty()) { arr.pop_back(); }
            return Object(double(arr.size()));
        }
        if (method_name == "resize")
        {
            if (!args.empty()) { arr.resize(size_t(eval_scoped(args[0], scopes).toInt())); }
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
                arr.insert(arr.begin() + idx, eval_scoped(args[1], scopes));
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
            set_runtime_error("keys() is not supported on arrays");
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
            || method_name == "resize" || method_name == "insert")
        {
            set_runtime_error(method_name + "() is not supported on maps");
            return Object();
        }
    }
    else
    {
        set_runtime_error(method_name + "() requires an array or map");
    }
    return Object();
}

//核心求值函数：递归遍历语法树节点并执行对应操作
Object Cifa::eval_scoped(CalUnit& c, ScopeStack& scopes)
{
    if (has_runtime_error())
    {
        return Object("RuntimeError", "Error");
    }

    //Union（代码块/数组字面量）不入调用栈，避免根块的行号污染错误报告
    const bool push_frame = (c.type != CalUnitType::Union);
    if (push_frame)
    {
        runtime_call_stack.push_back(format_runtime_frame(c));
    }
    struct ConditionalFrameGuard
    {
        std::vector<std::string>& stack;
        bool active;
        ConditionalFrameGuard(std::vector<std::string>& s, bool a) :
            stack(s), active(a) {}
        ~ConditionalFrameGuard()
        {
            if (active && !stack.empty())
            {
                stack.pop_back();
            }
        }
    } frame_guard(runtime_call_stack, push_frame);

    if (has_return_value(scopes))
    {
        return return_value(scopes);
    }
    else if (c.type == CalUnitType::Operator)
    {
        if (c.v.size() == 1)
        {
            if (c.str == "+") { return eval_scoped(c.v[0], scopes); }
            if (c.str == "-") { return sub(Object(0.0), eval_scoped(c.v[0], scopes)); }
            if (c.str == "~") { return double(~int(eval_scoped(c.v[0], scopes))); }
            if (c.str == "!") { return !eval_scoped(c.v[0], scopes); }
            if (c.str == "++") { return get_parameter_for_assign(c.v[0], scopes) = add(get_parameter(c.v[0], scopes), Object(1)); }
            if (c.str == "--") { return get_parameter_for_assign(c.v[0], scopes) = add(get_parameter(c.v[0], scopes), Object(-1)); }
            if (c.str == "()++")
            {
                auto v = get_parameter(c.v[0], scopes);
                get_parameter_for_assign(c.v[0], scopes) = add(get_parameter(c.v[0], scopes), Object(1));
                return v;
            }
            if (c.str == "()--")
            {
                auto v = get_parameter(c.v[0], scopes);
                get_parameter_for_assign(c.v[0], scopes) = add(get_parameter(c.v[0], scopes), Object(-1));
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
                    if (method_name == "push_back" || method_name == "pop_back" || method_name == "resize"
                        || method_name == "clear" || method_name == "insert" || method_name == "erase"
                        || method_name == "contains" || method_name == "keys")
                    {
                        std::vector<CalUnit> args;
                        if (c.v[1].v.size() > 0 && c.v[1].v[0].type != CalUnitType::None)
                        {
                            expand_comma(c.v[1].v[0], args);
                        }
                        auto& obj = get_parameter_for_assign(c.v[0], scopes);
                        return eval_builtin_method(method_name, obj, args, scopes);
                    }
                    if (c.v[1].v[0].type != CalUnitType::None)
                    {
                        std::vector<CalUnit> v = { c.v[0] };
                        expand_comma(c.v[1].v[0], v);
                        return run_function(c.v[1].str, v, scopes);
                    }
                    else
                    {
                        std::vector<CalUnit> v = { c.v[0] };
                        return run_function(c.v[1].str, v, scopes);
                    }
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
                    return get_parameter(c.v[0].str + "::" + c.v[1].str, scopes);
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
                return get_parameter(flat_name, scopes);
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
            if (c.str == "&&") { return logic_and(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "||") { return logic_or(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "<<") { return shift_left(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == ">>") { return shift_right(eval_scoped(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "=")
            {
                //空花括号 {} 在赋值右侧视为空数组字面量
                if (c.v[1].type == CalUnitType::Union && c.v[1].str == "{}" && c.v[1].v.empty())
                {
                    return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = Object(std::vector<Object>{});
                }
                return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = eval_scoped(c.v[1], scopes);
            }
            if (c.str == "+=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = add(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "-=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = sub(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "*=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = mul(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "/=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = div(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "%=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = mod(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "<<=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = shift_left(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == ">>=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = shift_right(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "&=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = bit_and(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "|=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = bit_or(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == "^=") { return get_parameter_for_assign(c.v[0], scopes, c.v[0].with_type) = bit_xor(get_parameter(c.v[0], scopes), eval_scoped(c.v[1], scopes)); }
            if (c.str == ",")
            {
                Object o;
                o.v.emplace_back(eval_scoped(c.v[0], scopes));
                o.v.emplace_back(eval_scoped(c.v[1], scopes));
                return o;
            }
            if (c.str == "?")    //条件1 ? 语句1 : 语句2;
            {
                if (eval_scoped(c.v[0], scopes))    //比较?运算符左侧的 [条件1]
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
        return Object(atof(c.str.c_str()));
    }
    else if (c.type == CalUnitType::String)
    {
        return Object(c.str);
    }
    else if (c.type == CalUnitType::Parameter)
    {
        // struct 类型声明：初始化为含所有字段的 ObjectMap
        if (c.with_type && !c.type_name.empty() && struct_defs.count(c.type_name))
        {
            auto* existing = find_object_from_inner(scopes, c.str);
            if (existing == nullptr || !existing->isType<ObjectMap>())
            {
                auto& o = get_parameter_for_assign(c, scopes, true);
                ObjectMap m;
                for (auto& field : struct_defs.at(c.type_name))
                {
                    m[field] = Object();
                }
                o = Object(std::move(m));
                o.name = c.str;
                return o;
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
        return run_function(c.str, v, scopes);
    }
    else if (c.type == CalUnitType::Key)
    {
        if (c.str == "if")    //if(条件1){语句1}else{语句2}
        {
            if (eval_scoped(c.v[0], scopes))    //判断 [条件1]
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
            Object o;
            int loop_count = 0;
            for (
                eval_scoped(c.v[0].v[0], scopes);    //执行 [语句1]
                eval_scoped(c.v[0].v[1], scopes);    //判断 [条件1]
                eval_scoped(c.v[0].v[2], scopes)     //执行 [语句2]
            )
            {
                if (++loop_count > max_loop_iterations)
                {
                    set_runtime_error("for loop exceeded max iterations");
                    break;
                }
                o = eval_scoped(c.v[1], scopes);    //执行 [语句3] 并 取执行结果
                if (o.type1 == "__" && o.toString() == "break") { break; }
                if (o.type1 == "__" && o.toString() == "continue") { continue; }
                if (has_return_value(scopes)) { return return_value(scopes); }
            }
            return Object(0);
        }
        if (c.str == "while")    //while (条件1) {语句1}
        {
            Object o;
            int loop_count = 0;
            while (eval_scoped(c.v[0], scopes))    //判断 [条件1]
            {
                if (++loop_count > max_loop_iterations)
                {
                    set_runtime_error("while loop exceeded max iterations");
                    break;
                }
                o = eval_scoped(c.v[1], scopes);    //执行 [语句1] 并 取执行结果
                if (o.type1 == "__" && o.toString() == "break") { break; }
                if (o.type1 == "__" && o.toString() == "continue") { continue; }
                if (has_return_value(scopes)) { return return_value(scopes); }
            }
            return o;
        }
        if (c.str == "do")    //do {语句1} while (条件1);
        {
            Object o;
            int loop_count = 0;
            do
            {
                if (++loop_count > max_loop_iterations)
                {
                    set_runtime_error("do-while loop exceeded max iterations");
                    break;
                }
                o = eval_scoped(c.v[0], scopes);    //执行 [语句1] 并 取执行结果
                if (o.type1 == "__" && o.toString() == "break") { break; }
                if (o.type1 == "__" && o.toString() == "continue") { continue; }
                if (has_return_value(scopes)) { return return_value(scopes); }
            } while (eval_scoped(c.v[1].v[0], scopes));    //判断 [条件1]
            return o;
        }
        if (c.str == "switch")
        {
            auto cond = eval_scoped(c.v[0], scopes);
            bool skip = true;
            scopes.emplace_back();
            for (auto& c1 : c.v[1].v)
            {
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
                    if (o.type1 == "__" && o.toString() == "break") { break; }
                    if (has_return_value(scopes))
                    {
                        auto ret = return_value(scopes);
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
            return_value(scopes) = eval_scoped(c.v[0], scopes);
            return return_value(scopes);
        }
        if (c.str == "break")
        {
            return Object("break", "__");
        }
        if (c.str == "continue")
        {
            return Object("continue", "__");
        }
        if (c.str == "true")
        {
            return Object(1, "__");
        }
        if (c.str == "false")
        {
            return Object(0, "__");
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
        Object o;
        for (auto& c1 : c.v)
        {
            o = eval_scoped(c1, scopes);
            if (o.type1 == "__" && o.toString() == "break") { break; }
            if (o.type1 == "__" && o.toString() == "continue") { break; }
            if (has_return_value(scopes))
            {
                auto ret = return_value(scopes);
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

//查找语法树最右侧叶节点
CalUnit& Cifa::find_right_side(CalUnit& c1)
{
    CalUnit* p = &c1;
    while (p->v.size() > 0)
    {
        p = &(p->v.back());
    }
    return *p;
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
            else
            {
                stat = CalUnitType::Parameter;
            }
        }
        else if (g == CalUnitType::None)
        {
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
        if (it->str == "(")
        {
            if (it != rv.begin() && std::prev(it)->type == CalUnitType::Parameter)
            {
                std::prev(it)->type = CalUnitType::Function;
                if (functions.count(std::prev(it)->str))
                {
                }
                else
                {
                    //脚本中的自定义函数
                    functions2[std::prev(it)->str] = {};
                }
            }
        }
        for (auto& keys1 : keys)
        {
            if (vector_have(keys1, it->str))
            {
                it->type = CalUnitType::Key;
            }
        }
        if (vector_have(types, it->str))
        {
            it->type = CalUnitType::Type;
        }
    }

    //合并多字节运算符
    for (auto& ops1 : ops)
    {
        for (auto& op : ops1)
        {
            if (op.size() == 2)
            {
                for (auto it = rv.begin(); it != rv.end();)
                {
                    auto itr = std::next(it);
                    if (itr != rv.end()
                        && it->str == std::string(1, op[0]) && itr->str == std::string(1, op[1])
                        && it->line == itr->line && it->col == itr->col - 1)    //合并的两个字符在同一行，列相邻
                    {
                        it->str = op;
                        it = rv.erase(std::next(it));
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }
    }

    //不处理类型符号
    for (auto it = rv.begin(); it != rv.end();)
    {
        if (it->type == CalUnitType::Type)
        {
            //记录下类型符号曾经存在的位置
            auto itr = std::next(it);
            if (itr != rv.end()
                && (itr->type == CalUnitType::Parameter || itr->type == CalUnitType::Function))
            {
                itr->with_type = true;
                it = rv.erase(it);
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

    // 预扫描：注册 struct 类型名（此时 {} 尚未合并，只需检测 struct Name { 模式）
    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        if (it->str == "struct" && it->type == CalUnitType::Parameter)
        {
            auto it1 = std::next(it);
            if (it1 != rv.end() && it1->type == CalUnitType::Parameter)
            {
                auto it2 = std::next(it1);
                if (it2 != rv.end() && it2->str == "{")
                {
                    struct_defs.emplace(it1->str, std::vector<std::string>{});
                }
            }
        }
    }

    // 将已知 struct 类型名作为类型标记：擦除类型名并在后续变量上设置 with_type/type_name
    for (auto it = rv.begin(); it != rv.end();)
    {
        if (it->type == CalUnitType::Parameter && struct_defs.count(it->str))
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
CalUnit Cifa::combine_all_cal(std::list<CalUnit>& ppp, bool curly, bool square, bool round)
{
    //合并{}
    if (curly) { combine_curly_bracket(ppp); }
    //合并[]
    if (square) { combine_square_bracket(ppp); }
    //合并()
    if (round) { combine_round_bracket(ppp); }

    //提取并移除 struct 定义
    combine_structs(ppp);

    //合并关键字
    deal_special_keys(ppp);

    //合并算符
    combine_ops(ppp);

    //检查分号正确性并去除
    //方括号或圆括号内不准许分号
    combine_semi(ppp);

    //合并关键字
    combine_keys(ppp);

    combine_functions2(ppp);

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

//查找现有最内层括号，并返回位置
std::list<CalUnit>::iterator Cifa::inside_bracket(std::list<CalUnit>& ppp, std::list<CalUnit>& ppp2, const std::string& bl, const std::string& br)
{
    bool have = false;
    auto it = ppp.begin();
    for (; it != ppp.end(); ++it)
    {
        if (it->str == bl || it->str == br)
        {
            have = true;
            break;
        }
    }
    if (!have)
    {
        return ppp.end();
    }
    if (it->str == br)
    {
        add_error(*it, "unpaired right bracket %s", it->str.c_str());
        ppp.erase(it);
        return ppp.end();
    }
    auto itl0 = it, itr0 = ppp.end();
    for (auto itr = it; itr != ppp.end(); ++itr)
    {
        if (itr->str == br)
        {
            itr0 = itr;
            for (auto itl = std::prev(itr); itl != ppp.begin(); --itl)
            {
                if (itl->str == bl)
                {
                    itl0 = itl;
                    break;
                }
            }
            break;
        }
    }
    if (itr0 == ppp.end())
    {
        add_error(*it, "unpaired left bracket %s", it->str.c_str());
        ppp.erase(it);
        return ppp.end();
    }
    ppp2.splice(ppp2.begin(), ppp, std::next(itl0), itr0);
    return itl0;
}

//合并花括号 {} 为语法树节点
void Cifa::combine_curly_bracket(std::list<CalUnit>& ppp)
{
    while (true)
    {
        std::list<CalUnit> ppp2;
        auto it = inside_bracket(ppp, ppp2, "{", "}");
        if (it == ppp.end())
        {
            break;
        }
        auto c1 = combine_all_cal(ppp2, false, true, true);    //此处合并多行
        c1.str = "{}";
        c1.line = it->line;
        c1.col = it->col;
        it = ppp.erase(it);
        *it = std::move(c1);
    }
}

//合并方括号 [] 为语法树节点，并关联到前置变量名
void Cifa::combine_square_bracket(std::list<CalUnit>& ppp)
{
    while (true)
    {
        std::list<CalUnit> ppp2;
        auto it = inside_bracket(ppp, ppp2, "[", "]");
        if (it == ppp.end())
        {
            break;
        }
        auto c1 = combine_all_cal(ppp2, true, false, true);
        c1.str = "[]";
        c1.line = it->line;
        c1.col = it->col;
        it = ppp.erase(it);
        *it = std::move(c1);
        if (it != ppp.begin())
        {
            if (std::prev(it)->type == CalUnitType::Parameter)
            {
                //多维数组：如果前置变量已有 [] 子节点，追加而非覆盖
                std::prev(it)->v.push_back(*it);
                ppp.erase(it);
            }
        }
    }
}

//合并圆括号 () 为语法树节点，并关联到前置函数名或关键字
void Cifa::combine_round_bracket(std::list<CalUnit>& ppp)
{
    while (true)
    {
        std::list<CalUnit> ppp2;
        //auto size = ppp.size();
        auto it = inside_bracket(ppp, ppp2, "(", ")");
        if (it == ppp.end())
        {
            break;
        }
        it = ppp.erase(it);
        auto c1 = combine_all_cal(ppp2, true, true, false);
        c1.str = "()";
        c1.line = it->line;
        c1.col = it->col;
        if (c1.v.size() == 0)
        {
            it->type = CalUnitType::None;
        }
        else if (c1.v.size() == 1)
        {
            *it = std::move(c1.v[0]);
        }
        else
        {
            *it = std::move(c1);
        }
        //括号前
        if (it != ppp.begin())
        {
            auto itl = std::prev(it);
            if (itl->type == CalUnitType::Function || itl->type == CalUnitType::Parameter || itl->type == CalUnitType::Constant)
            {
                itl->v = { std::move(*it) };
                ppp.erase(it);
            }
            else if (itl->type == CalUnitType::Key && vector_have(keys[2], itl->str))
            {
                itl->v = { std::move(*it) };
                ppp.erase(it);
            }
        }
    }
}

//按优先级合并运算符到语法树，处理左结合和右结合
void Cifa::combine_ops(std::list<CalUnit>& ppp)
{
    for (const auto& ops1 : ops)
    {
        for (auto& op : ops1)
        {
            bool is_right = false;
            if (vector_have(ops_single, op) || vector_have(ops_right, op) || op == "+" || op == "-")    //右结合
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
                        if (it == ppp.begin() || vector_have(ops_single, it->str)
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
                            if (!is_single && it != ppp.begin() && (it->str == "++" || it->str == "--") && std::prev(it)->type == CalUnitType::Parameter)
                            {
                                it->v = { std::move(*std::prev(it)) };
                                it->str = "()" + it->str;
                                it = ppp.erase(std::prev(it));
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
            if (!is_right && vector_have(ops1, std::string("?")))    //三目运算符组需按固定顺序（:先?后）逐符号合并
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
        if (!vector_have(ops1, std::string("?")))
        {
            for (auto it = ppp.begin(); it != ppp.end();)
            {
                if (it->un_combine)
                {
                    ++it;
                    continue;
                }
                if (it->type == CalUnitType::Operator && it->v.size() == 0 && it != ppp.begin()
                    && vector_have(ops1, it->str)
                    && !vector_have(ops_single, it->str)
                    && !vector_have(ops_right, it->str))
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
            auto& keys1 = keys[para_count];
            if (it->type == CalUnitType::Key && it->v.size() < para_count && vector_have(keys1, it->str))
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

    it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        if (it->str == "if" && it->v.size() == 2 && std::next(it) != ppp.end())
        {
            auto itr = std::next(it);
            if (itr->str == "else")
            {
                it->v.emplace_back(std::move(*itr));
                ppp.erase(itr);
                if (!it->v[2].v.empty())
                {
                    auto it_else = std::move(it->v[2].v[0]);
                    it->v[2] = std::move(it_else);    //cannot assign directly when debug
                    //it->v[2] = std::move(it->v[2].v[0]);
                }
            }
        }
    }
}

//合并脚本中定义的函数：将函数名+参数+函数体合为 Function2 并注册到 functions2
void Cifa::combine_functions2(std::list<CalUnit>& ppp)
{
    //合并关键字，从右向左
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
                    f.arguments.emplace_back(std::move(c.str));
                }
                functions2[it->str] = std::move(f);
                ppp.erase(itr);
                it = ppp.erase(it);
            }
        }
    }
}

//提取并注册 struct 定义，并将其从语法树中移除
void Cifa::combine_structs(std::list<CalUnit>& ppp)
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
                    std::vector<std::string> fields;
                    for (auto& c : it2->v)
                    {
                        if (c.type == CalUnitType::Parameter && c.with_type)
                        {
                            fields.push_back(c.str);
                        }
                    }
                    struct_defs[struct_name] = std::move(fields);
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

//注册宿主程序中的 C++ 函数
void Cifa::register_function(const std::string& name, func_type func)
{
    functions[name] = func;
}

bool Cifa::import_module(const std::string& path)
{
    if (path.empty())
    {
        set_runtime_error("import path is empty");
        return false;
    }

    if (std::find(imported_module_paths.begin(), imported_module_paths.end(), path) != imported_module_paths.end())
    {
        return true;
    }

#ifdef _WIN32
    HMODULE module = LoadLibraryA(path.c_str());
    if (module == nullptr)
    {
        set_runtime_error("import failed: cannot load '" + path + "'");
        return false;
    }

    auto import_func = reinterpret_cast<import_func_type>(GetProcAddress(module, "cifa_import"));
    if (import_func == nullptr)
    {
        FreeLibrary(module);
        set_runtime_error("import failed: '" + path + "' does not export cifa_import");
        return false;
    }

    if (!import_func(this))
    {
        FreeLibrary(module);
        set_runtime_error("import failed: cifa_import returned false for '" + path + "'");
        return false;
    }

    imported_modules.push_back(module);
    imported_module_paths.push_back(path);
    return true;
#else
    void* module = dlopen(path.c_str(), RTLD_NOW);
    if (module == nullptr)
    {
        const char* err = dlerror();
        set_runtime_error("import failed: cannot load '" + path + "'" + (err != nullptr ? std::string(": ") + err : std::string()));
        return false;
    }

    dlerror();
    auto import_func = reinterpret_cast<import_func_type>(dlsym(module, "cifa_import"));
    const char* symbol_error = dlerror();
    if (symbol_error != nullptr)
    {
        dlclose(module);
        set_runtime_error("import failed: '" + path + "' does not export cifa_import");
        return false;
    }

    if (!import_func(this))
    {
        dlclose(module);
        set_runtime_error("import failed: cifa_import returned false for '" + path + "'");
        return false;
    }

    imported_modules.push_back(module);
    imported_module_paths.push_back(path);
    return true;
#endif
}

void Cifa::import_literal_modules(CalUnit& c)
{
    if (c.type == CalUnitType::Function && c.str == "import" && !c.v.empty())
    {
        std::vector<CalUnit> args;
        expand_comma(c.v[0], args);
        if (args.size() == 1 && args[0].type == CalUnitType::String)
        {
            import_module(args[0].str);
        }
    }

    for (auto& child : c.v)
    {
        import_literal_modules(child);
    }
}

//注册用户自定义数据指针
void Cifa::register_user_data(const std::string& name, void* p)
{
    user_data[name] = p;
}

//注册一个全局参数变量
void Cifa::register_parameter(const std::string& name, Object o)
{
    parameters[name] = o;
}

void Cifa::set_include_dirs(const std::vector<std::string>& dirs)
{
    include_dirs = dirs;
}

//获取用户自定义数据指针
void* Cifa::get_user_data(const std::string& name)
{
    return user_data[name];
}

//执行函数调用：查找已注册函数或脚本定义函数并执行
Object Cifa::run_function(const std::string& name, std::vector<CalUnit>& vc, ScopeStack& scopes)
{
    if ((int)runtime_call_stack.size() > max_call_depth)
    {
        set_runtime_error("max call depth exceeded (possible infinite recursion)");
        return Object();
    }
    runtime_call_stack.push_back("func " + name + "()");
    RuntimeFrameGuard frame_guard(runtime_call_stack);

    if (functions.count(name))
    {
        auto f = functions[name];
        std::vector<Object> v;
        for (auto& c : vc)
        {
            v.emplace_back(eval_scoped(c, scopes));
        }
        return f(v);
    }
    else if (functions2.count(name))
    {
        auto& f = functions2[name];
        ScopeStack fn_scopes;
        fn_scopes.emplace_back(parameters);
        for (size_t i = 0; i < std::min(vc.size(), f.arguments.size()); i++)
        {
            fn_scopes.back()[f.arguments[i]] = eval_scoped(vc[i], scopes);
        }
        return eval_scoped(f.body, fn_scopes);
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
        return get_parameter(c.v[0].str + "::" + c.v[1].str, scopes);
    }
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        return resolve_indexed_parameter(c, scopes, only_check, false, true);
    }
    auto* o = find_object_from_inner(scopes, c.str);
    if (o == nullptr)
    {
        if (scopes.empty())
        {
            scopes.emplace_back();
        }
        o = &scopes.back()[c.str];
    }
    o->name = c.str;
    return *o;
}

//按名称从作用域栈获取变量引用
Object& Cifa::get_parameter(const std::string& name, ScopeStack& scopes)
{
    auto* o = find_object_from_inner(scopes, name);
    if (o == nullptr)
    {
        if (scopes.empty())
        {
            scopes.emplace_back();
        }
        o = &scopes.back()[name];
    }
    o->name = name;
    return *o;
}

//按名称检查变量是否存在于作用域栈中
bool Cifa::check_parameter(const std::string& name, ScopeStack& scopes)
{
    return find_object_from_inner(scopes, name) != nullptr;
}

//获取赋值目标的变量引用，必要时在当前作用域创建新变量
Object& Cifa::get_parameter_for_assign(CalUnit& c, ScopeStack& scopes, bool declare_current)
{
    // . 操作符节点：写入 ObjectMap 字段
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
        return get_parameter(c.v[0].str + "::" + c.v[1].str, scopes);
    }
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        return resolve_indexed_parameter(c, scopes, false, declare_current, false);
    }

    const auto& name = c.str;
    if (scopes.empty())
    {
        scopes.emplace_back();
    }
    Object* o = nullptr;
    // struct 类型声明：直接创建并初始化 ObjectMap
    if (declare_current && !c.type_name.empty() && struct_defs.count(c.type_name))
    {
        o = &scopes.back()[name];
        ObjectMap m;
        for (auto& field : struct_defs.at(c.type_name))
        {
            m[field] = Object();
        }
        *o = Object(std::move(m));
        o->name = name;
        return *o;
    }
    if (declare_current)
    {
        o = &scopes.back()[name];
    }
    else
    {
        o = find_object_from_inner(scopes, name);
        if (o == nullptr)
        {
            o = &scopes.back()[name];
        }
    }
    o->name = name;
    return *o;
}

//多维数组递归索引：从 element 开始，继续按 c.v[dim_index] 及后续维度进行下标访问
Object& Cifa::resolve_nested_index(Object& element, CalUnit& c, size_t dim_index, ScopeStack& scopes, bool only_check)
{
    int idx = 0;
    if (!only_check && c.v[dim_index].v.size() > 0)
    {
        idx = eval_scoped(c.v[dim_index].v[0], scopes).toInt();
        if (idx < 0)
        {
            idx = 0;
        }
    }

    if (!element.isType<std::vector<Object>>())
    {
        //元素还不是数组，创建一个
        std::vector<Object> arr(size_t(idx + 1));
        element = Object(arr);
    }

    auto& arr = element.ref<std::vector<Object>>();
    if (idx >= int(arr.size()))
    {
        arr.resize(size_t(idx + 1));
    }
    auto& sub = arr[size_t(idx)];

    if (dim_index + 1 < c.v.size() && c.v[dim_index + 1].str == "[]")
    {
        return resolve_nested_index(sub, c, dim_index + 1, scopes, only_check);
    }
    return sub;
}

//解析字符串下标访问（map 语义），如 dict["name"]，返回元素引用
Object& Cifa::resolve_string_indexed_parameter(CalUnit& c, ScopeStack& scopes, const std::string& key, bool only_check, bool declare_current)
{
    if (scopes.empty())
    {
        scopes.emplace_back();
    }

    Object* base = nullptr;
    if (declare_current)
    {
        base = &scopes.back()[c.str];
        base->name = c.str;
    }
    else
    {
        base = find_object_from_inner(scopes, c.str);
    }

    //已有 map 对象
    if (base != nullptr && base->isType<ObjectMap>())
    {
        auto& m = base->ref<ObjectMap>();
        auto& element = m[key];
        element.name = c.str + "[\"" + key + "\"]";
        return element;
    }

    //不存在则创建新的 map
    if (base == nullptr)
    {
        base = &scopes.back()[c.str];
        base->name = c.str;
    }

    if (!base->isType<ObjectMap>())
    {
        *base = Object(ObjectMap());
        base->name = c.str;
    }

    auto& m = base->ref<ObjectMap>();
    auto& element = m[key];
    element.name = c.str + "[\"" + key + "\"]";
    return element;
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
        return resolve_string_indexed_parameter(c, scopes, index_val.toString(), only_check, declare_current);
    }

    int index = 0;
    if (index_val.hasValue())
    {
        index = index_val.toInt();
        if (index < 0)
        {
            index = 0;
        }
    }
    const bool is_decl_array = declaration_as_array && c.with_type && c.suffix;

    if (scopes.empty())
    {
        scopes.emplace_back();
    }

    Object* base = nullptr;
    if (declare_current)
    {
        base = &scopes.back()[c.str];
        base->name = c.str;
    }
    else
    {
        base = find_object_from_inner(scopes, c.str);
    }

    if (base != nullptr && base->isType<std::vector<Object>>())
    {
        auto& arr = base->ref<std::vector<Object>>();
        if (is_decl_array)
        {
            if (!only_check)
            {
                arr.resize(size_t(index));
            }
            base->name = c.str;
            return *base;
        }
        if (index >= int(arr.size()))
        {
            arr.resize(size_t(index + 1));
        }
        auto& element = arr[size_t(index)];
        element.name = c.str + "[" + std::to_string(index) + "]";
        //多维数组：如果还有后续的 [] 下标，递归索引子数组
        if (c.v.size() > 1)
        {
            return resolve_nested_index(element, c, 1, scopes, only_check);
        }
        return element;
    }

    if (base == nullptr)
    {
        base = &scopes.back()[c.str];
        base->name = c.str;
    }

    if (!base->isType<std::vector<Object>>())
    {
        std::vector<Object> arr;
        size_t initial_size = is_decl_array ? size_t(index) : size_t(index + 1);
        arr.resize(initial_size);
        *base = Object(arr);
        base->name = c.str;
    }

    if (is_decl_array)
    {
        return *base;
    }

    auto& arr = base->ref<std::vector<Object>>();
    if (index >= int(arr.size()))
    {
        arr.resize(size_t(index + 1));
    }
    auto& element = arr[size_t(index)];
    element.name = c.str + "[" + std::to_string(index) + "]";
    //多维数组：如果还有后续的 [] 下标，递归索引子数组
    if (c.v.size() > 1)
    {
        return resolve_nested_index(element, c, 1, scopes, only_check);
    }
    return element;
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
void Cifa::check_cal_unit(CalUnit& c, CalUnit* father, std::unordered_map<std::string, Object>& p)
{
    //若提前return，表示不再检查其下的结构
    if (c.type == CalUnitType::Operator && c.un_combine == false)
    {
        if (vector_have(ops_single, c.str))
        {
            if (c.v.size() != 1)
            {
                add_error(c, "operator %s has wrong operands", c.str.c_str());
            }
        }
        else if (vector_have(ops, c.str) && !vector_have(ops_single, c.str))
        {
            if (c.str == "=")
            {
                if (c.v.size() != 2)
                {
                    add_error(c, "operator = has wrong operands");
                }
                else
                {
                    if (c.v[0].type == CalUnitType::Parameter)
                    {
                        check_cal_unit(c.v[1], &c, p);    //here make sure no undefined parameters at right of "="
                        p[c.v[0].str].name = c.v[0].str;
                        //赋值左侧的下标表达式也需要递归检查
                        for (auto& sub : c.v[0].v)
                        {
                            check_cal_unit(sub, &c.v[0], p);
                        }
                    }
                    else
                    {
                        //左侧不是参数（如常量、字符串），仍需递归检查右侧
                        check_cal_unit(c.v[1], &c, p);
                    }
                    if (c.v[0].type == CalUnitType::Parameter && p[c.v[0].str].type1 == "__"
                        || c.v[0].type != CalUnitType::Parameter
                            && !(c.v[0].type == CalUnitType::Operator && c.v[0].str == "."))
                    {
                        add_error(c.v[0], "'%s' cannot be assigned", c.v[0].str.c_str());
                    }
                }
            }
            if (c.str == "::" || c.str == ".")
            {
                if (c.v.size() == 2)
                {
                    if (c.v[0].type == CalUnitType::Parameter && !p.count(c.v[0].str))
                    {
                        add_error(c.v[0], "parameter '%s' is at right of = but not been initialized", c.v[0].str.c_str());
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
                            add_error(c.v[0], "parameter '%s' in '%s' is at right of = but not been initialized", c.v[1].str.c_str(), c.v[0].str.c_str());
                        }
                    }
                }
                else
                {
                    add_error(c, "operator %s has wrong operands", c.str.c_str());
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
                add_error(c, "operator %s has wrong operands", c.str.c_str());
            }
        }
        else
        {
            add_error(c, "unknown operator %s with %zu operands", c.str.c_str(), c.v.size());
        }
    }
    else if (c.type == CalUnitType::Constant || c.type == CalUnitType::String)
    {
        if (c.v.size() > 0)
        {
            add_error(c, "cannot calculate constant %s with operands", c.str.c_str());
        };
    }
    else if (c.type == CalUnitType::Parameter)
    {
        if (c.v.size() > 0 && c.v[0].str != "[]")
        {
            add_error(c, "cannot calculate parameter '%s' with operands", c.str.c_str());
        }
        //带类型前缀的独立声明（如 int i;），注册变量到作用域
        if (c.with_type)
        {
            if (!c.type_name.empty() && struct_defs.count(c.type_name))
            {
                ObjectMap m;
                for (auto& field : struct_defs.at(c.type_name))
                {
                    m[field] = Object();
                }
                p[c.str] = Object(std::move(m));
            }
            p[c.str].name = c.str;
        }
        else if (father && father->type == CalUnitType::Operator)
        {
            if (father->str == "::" || father->str == ".")
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
                    add_error(c, "parameter '%s' is at right of = but not been initialized", c.str.c_str());
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
                    add_error(c, "parameter '%s' is at right of = but not been initialized", c.str.c_str());
                }
            }
        }
    }
    else if (c.type == CalUnitType::Function)
    {
        if (c.v.size() == 0)
        {
            add_error(c, "function '%s' has no operands", c.str.c_str());
        }
        //内置方法名不视为未定义函数
        if (!functions.contains(c.str) && !functions2.contains(c.str))
        {
            if (!builtin_methods.contains(c.str))
            {
                add_error(c, "function '%s' is not defined", c.str.c_str());
            }
        }
        else if (!functions.contains(c.str) && functions2.contains(c.str) && functions2.at(c.str).body.type == CalUnitType::None)
        {
            if (!builtin_methods.contains(c.str))
            {
                add_error(c, "function '%s' is not defined", c.str.c_str());
            }
        }
    }
    else if (c.type == CalUnitType::Key)
    {
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
            if (c.v[0].type != CalUnitType::Union || c.v[0].str != "()" || c.v[0].v.size() != 3
                || !c.v[0].v[0].is_statement() || !c.v[0].v[1].is_statement() || (c.v[0].v[2].is_statement() && c.v[0].v[2].type != CalUnitType::None))
            {
                add_error(c, "for loop condition is not right");
            }
            //检测 for(;; ) 和 for(; true/1; ) 形式的潜在死循环
            if (c.v[0].type == CalUnitType::Union && c.v[0].str == "()" && c.v[0].v.size() == 3)
            {
                auto& cond = c.v[0].v[1];
                bool is_always_true = (cond.type == CalUnitType::None)
                    || (cond.type == CalUnitType::Constant && cond.str == "1")
                    || (cond.type == CalUnitType::Key && cond.str == "true");
                if (is_always_true)
                {
                    add_error(c, "for loop may cause infinite loop");
                }
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
            //检测 while(1)/while(true) 形式的潜在死循环
            if (c.v.size() >= 1 && !(father && father->str == "do"))
            {
                auto& cond = c.v[0];
                if ((cond.type == CalUnitType::Constant && cond.str == "1")
                    || (cond.type == CalUnitType::Key && cond.str == "true"))
                {
                    add_error(c, "while has constant true condition, may cause infinite loop");
                }
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
                add_error(c, "%s missing ;", c.str.c_str());
            }
        }
        if (c.str == "break" || c.str == "continue")
        {
            if (c.v.size() == 0 || c.v[0].str != ";")
            {
                add_error(c, "%s missing ;", c.str.c_str());
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
        add_error(c, "type %s has operands", c.str.c_str());
    }
    for (auto& c1 : c.v)
    {
        //=的子节点已在上方显式处理过，跳过避免重复检查
        if (c.type == CalUnitType::Operator && c.str == "=" && c.un_combine == false)
        {
            continue;
        }
        check_cal_unit(c1, &c, p);
    }
}

//运行脚本，使用独立变量表；按当前目录和include搜索目录处理#include
Object Cifa::run_script(std::string script)
{
    errors.clear();
    clear_runtime_error();
    std::unordered_map<std::string, Object> local_params;
    std::set<std::string> visited;
    script = preprocess_includes(script, "<script>", ".", include_dirs, visited);
    return run_pipeline(std::move(script), local_params);
}

//运行脚本，使用外部变量表；按当前目录和include搜索目录处理#include
Object Cifa::run_script(std::string script, std::unordered_map<std::string, Object>& p)
{
    errors.clear();
    clear_runtime_error();
    std::set<std::string> visited;
    script = preprocess_includes(script, "<script>", ".", include_dirs, visited);
    return run_pipeline(std::move(script), p);
}

//从文件运行脚本（简化版，使用空变量表）
Object Cifa::run_file(const std::string& filename)
{
    std::unordered_map<std::string, Object> local_params;
    return run_file(filename, local_params);
}

//从文件运行脚本（使用外部变量表）
Object Cifa::run_file(const std::string& filename, std::unordered_map<std::string, Object>& p)
{
    errors.clear();
    clear_runtime_error();
    std::ifstream ifs(filename);
    if (!ifs.is_open())
    {
        add_error(1, 1, "cannot open file: %s", filename.c_str());
        Object result = std::string("");
        result.type1 = "Error";
        if (output_error)
        {
            print_errors();
        }
        return result;
    }
    std::string str((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
    std::set<std::string> visited;
    visited.insert(normalize_path(filename));
    std::string dir = get_directory(filename);
    str = preprocess_includes(str, normalize_path(filename), dir, include_dirs, visited);
    return run_pipeline(std::move(str), p);
}

//脚本执行管线：词法分析→语法树构建→语法检查→求值执行
Object Cifa::run_pipeline(std::string str, std::unordered_map<std::string, Object>& p)
{
    Object result;

    {
        std::stringstream source_stream(str);
        std::string source_line;
        size_t line_index = 0;
        while (std::getline(source_stream, source_line))
        {
            if (line_index >= runtime_source_line_infos.size())
            {
                runtime_source_line_infos.push_back({ "<script>", line_index + 1, source_line });
            }
            runtime_source_lines.emplace_back(runtime_source_line_infos[line_index].text);
            ++line_index;
        }
    }

    str += ";";    //方便处理仅有一行的情况
    auto rv = split(str);
    auto c = combine_all_cal(rv);    //结果必定是一个Union
    import_literal_modules(c);
    //此处设定为在语法树检查不正确时，仍然尝试运行并检查执行时的错误
    //if (errors.empty())
    {
        auto p1 = parameters;
        for (auto& [name, o] : p)
        {
            p1[name] = o;
        }
        check_cal_unit(c, nullptr, p1);
        for (auto& [name, func2] : functions2)
        {
            auto p1 = parameters;
            for (auto& a : func2.arguments)
            {
                p1[a] = Object();
            }
            check_cal_unit(func2.body, nullptr, p1);
        }
    }
    if (errors.empty())
    {
        struct RuntimeReporterGuard
        {
            RuntimeReporterGuard(Cifa* owner)
            {
                Object::set_runtime_error_reporter([owner](const std::string& message)
                    {
                        owner->set_runtime_error(message);
                    });
            }
            ~RuntimeReporterGuard()
            {
                Object::clear_runtime_error_reporter();
            }
        } runtime_reporter_guard(this);

        for (auto& [name, o] : parameters)
        {
            p[name] = o;
        }
        ScopeStack run_scopes;
        run_scopes.emplace_back(p);
        auto o = eval_scoped(c, run_scopes);
        p = std::move(run_scopes.front());
        if (has_runtime_error())
        {
            result = std::string("");
            result.type1 = "Error";
            return result;
        }
        return o;
    }
    else
    {
        result = std::string("");
        result.type1 = "Error";
        if (output_error)
        {
            print_errors();
        }
    }
    return result;
}

//获取文件路径中的目录部分
std::string Cifa::get_directory(const std::string& filepath)
{
    auto pos1 = filepath.find_last_of('/');
    auto pos2 = filepath.find_last_of('\\');
    size_t pos = std::string::npos;
    if (pos1 != std::string::npos) pos = pos1;
    if (pos2 != std::string::npos && (pos == std::string::npos || pos2 > pos)) pos = pos2;
    if (pos != std::string::npos)
    {
        return filepath.substr(0, pos);
    }
    return ".";
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

//在语法树构建之前报告错误（无CalUnit信息）
void Cifa::add_error(size_t line, size_t col, const char* fmt, ...)
{
    ErrorMessage e;
    e.line = line;
    e.col = col;
    char buffer[1024] = { '\0' };
    va_list args;
    va_start(args, fmt);
    vsnprintf(buffer, 1024, fmt, args);
    va_end(args);
    e.message = buffer;
    errors.emplace(std::move(e));
}

//规范化路径：将\替换为/，解析.和..
static std::string normalize_path(const std::string& path)
{
    std::string normalized = path;
    for (auto& c : normalized)
    {
        if (c == '\\') c = '/';
    }
    std::vector<std::string> parts;
    std::stringstream ss(normalized);
    std::string part;
    while (std::getline(ss, part, '/'))
    {
        if (part.empty() || part == ".") continue;
        if (part == "..")
        {
            if (!parts.empty()) parts.pop_back();
        }
        else
        {
            parts.push_back(part);
        }
    }
    if (parts.empty()) return ".";
    std::string result;
    for (size_t i = 0; i < parts.size(); ++i)
    {
        if (i > 0) result += "/";
        result += parts[i];
    }
    return result;
}

//预处理#include指令：递归展开所有包含的文件
std::string Cifa::preprocess_includes(const std::string& source, const std::string& current_file, const std::string& current_dir, const std::vector<std::string>& extra_include_dirs, std::set<std::string>& visited)
{
    std::stringstream source_stream(source);
    std::string line;
    std::string result;
    size_t line_num = 0;

    while (std::getline(source_stream, line))
    {
        ++line_num;
        //查找行首的#include指令（允许前导空白）
        std::string trimmed = line;
        size_t first_non_space = trimmed.find_first_not_of(" \t");
        if (first_non_space == std::string::npos || trimmed[first_non_space] != '#')
        {
            result += line + "\n";
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            continue;
        }
        std::string directive = trimmed.substr(first_non_space);
        if (directive.substr(0, 8) != "#include")
        {
            result += line + "\n";
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            continue;
        }
        //解析文件名
        std::string rest = directive.substr(8);
        size_t filename_start = rest.find_first_not_of(" \t");
        if (filename_start == std::string::npos)
        {
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            add_error(line_num, first_non_space + 1, "#include: missing filename");
            result += "\n";
            continue;
        }
        char open_char = rest[filename_start];
        char close_char = 0;
        if (open_char == '"') close_char = '"';
        else if (open_char == '<') close_char = '>';
        else
        {
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            add_error(line_num, first_non_space + 1, "#include: invalid syntax, expected '\"' or '<'");
            result += "\n";
            continue;
        }
        size_t filename_end = rest.find(close_char, filename_start + 1);
        if (filename_end == std::string::npos)
        {
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            add_error(line_num, first_non_space + 1, "#include: missing closing '%c'", close_char);
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

        std::ifstream ifs;
        std::string full_path;
        std::string normalized;
        for (const auto& candidate : candidates)
        {
            std::string candidate_normalized = normalize_path(candidate);
            if (visited.count(candidate_normalized))
            {
                full_path = candidate;
                normalized = std::move(candidate_normalized);
                break;
            }
            ifs.open(candidate);
            if (!ifs.is_open())
            {
                ifs.clear();
                ifs.open(candidate_normalized);
            }
            if (ifs.is_open())
            {
                full_path = candidate;
                normalized = std::move(candidate_normalized);
                break;
            }
            ifs.clear();
        }

        if (!normalized.empty() && visited.count(normalized))
        {
            result += "\n";    //跳过已包含的文件，插入空行保持行号
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            continue;
        }
        if (!ifs.is_open())
        {
            runtime_source_line_infos.push_back({ current_file, line_num, line });
            add_error(line_num, first_non_space + 1, "#include: cannot open file '%s'", include_filename.c_str());
            result += "\n";
            continue;
        }
        visited.insert(normalized);
        std::string included_content((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());
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
    for (auto& e : errors)
    {
        str += "Syntax Error: " + e.message + "\n";
        if (e.line > 0 && e.line <= runtime_source_line_infos.size())
        {
            const auto& source_line = runtime_source_line_infos[e.line - 1];
            const std::string& line_text = source_line.text;
            std::string header = "  at " + source_line.filename + ":" + std::to_string(source_line.line) + ", col " + std::to_string(e.col) + ": ";
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
            str += "  at line " + std::to_string(e.line) + ", col " + std::to_string(e.col) + "\n";
        }
    }
    return str;
}

//将编译期错误信息输出到 stderr（委托 get_errors_str() 避免重复逻辑）
void Cifa::print_errors() const
{
    fprintf(stderr, "%s", get_errors_str().c_str());
}

//获取所有编译期错误的列表，建议优先使用 get_errors_str() 或 print_errors()
std::vector<Cifa::ErrorMessage> Cifa::get_errors() const
{
    return std::vector<ErrorMessage>(errors.begin(), errors.end());
}

//格式化一个运行时调用栈帧：显示行号、源码行和插入符位置
std::string Cifa::format_runtime_frame(const CalUnit& c) const
{
    std::string label = c.str.empty() ? "<none>" : c.str;
    std::string line_text;
    std::string filename = "<script>";
    size_t line = c.line;
    if (c.line > 0 && c.line <= runtime_source_line_infos.size())
    {
        const auto& source_line = runtime_source_line_infos[c.line - 1];
        filename = source_line.filename;
        line = source_line.line;
        line_text = source_line.text;
    }
    else if (c.line > 0 && c.line <= runtime_source_lines.size())
    {
        line_text = runtime_source_lines[c.line - 1];
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

//设置运行时错误消息（仅记录第一个错误，后续错误忽略）
void Cifa::set_runtime_error(const std::string& message)
{
    if (has_runtime_error())
    {
        return;
    }
    runtime_error_message = message.empty() ? "runtime error" : message;
    if (output_error && !runtime_error_reported)
    {
        print_runtime_error();
        runtime_error_reported = true;
    }
}

//清除运行时错误状态（调用栈、源码行缓存、错误消息）
void Cifa::clear_runtime_error()
{
    runtime_call_stack.clear();
    runtime_source_lines.clear();
    runtime_source_line_infos.clear();
    runtime_error_message.clear();
    runtime_error_reported = false;
}

//输出运行时错误信息和调用栈到 stderr（相同源码行的栈帧会去重）
void Cifa::print_runtime_error() const
{
    fprintf(stderr, "Runtime Error: %s\n", runtime_error_message.c_str());
    if (runtime_call_stack.empty())
    {
        return;
    }
    fprintf(stderr, "Call Stack (most recent call last):\n");
    std::string last_source_line;
    for (auto it = runtime_call_stack.rbegin(); it != runtime_call_stack.rend(); ++it)
    {
        const std::string& frame = *it;
        const size_t line_pos = frame.find("line ");
        const size_t colon_pos = frame.find(": ");
        if (line_pos == 0 && colon_pos != std::string::npos)
        {
            size_t line_end = frame.find('\n', colon_pos + 2);
            std::string source_line = frame.substr(colon_pos + 2, line_end == std::string::npos ? std::string::npos : line_end - (colon_pos + 2));
            if (!source_line.empty() && source_line == last_source_line)
            {
                continue;
            }
            last_source_line = std::move(source_line);
        }
        size_t newline_pos = frame.find('\n');
        if (newline_pos == std::string::npos)
        {
            fprintf(stderr, "  at %s\n", frame.c_str());
        }
        else
        {
            std::string first_line = frame.substr(0, newline_pos);
            std::string rest = frame.substr(newline_pos + 1);
            fprintf(stderr, "  at %s\n", first_line.c_str());

            size_t start = 0;
            while (start <= rest.size())
            {
                size_t pos = rest.find('\n', start);
                std::string continuation = (pos == std::string::npos) ? rest.substr(start) : rest.substr(start, pos - start);
                if (!continuation.empty())
                {
                    fprintf(stderr, "     %s\n", continuation.c_str());
                }
                if (pos == std::string::npos)
                {
                    break;
                }
                start = pos + 1;
            }
        }
    }
}
}    // namespace cifa