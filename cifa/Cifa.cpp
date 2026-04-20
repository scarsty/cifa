#include "Cifa.h"
#include <algorithm>
#include <iostream>
#include <sstream>

namespace cifa
{

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

    register_function("pow", [](ObjectVector& x) -> Object
        {
            if (x.size() <= 1) { return cifa::Object(); }
            double x0 = x[0];
            double x1 = x[1];
            return pow(x0, x1);
        });

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
#define REGISTER_FUNCTION(func) \
    register_function(#func, [](ObjectVector& x) -> Object \
        { \
            if (x.size() == 0) { return cifa::Object(); } \
            double x0 = x[0]; \
            return func(x0); \
        });
    REGISTER_FUNCTION(abs);
    REGISTER_FUNCTION(sqrt);
    REGISTER_FUNCTION(round);
    REGISTER_FUNCTION(sin);
    REGISTER_FUNCTION(cos);
    REGISTER_FUNCTION(tan);
    REGISTER_FUNCTION(asin);
    REGISTER_FUNCTION(acos);
    REGISTER_FUNCTION(atan);
    REGISTER_FUNCTION(sinh);
    REGISTER_FUNCTION(cosh);
    REGISTER_FUNCTION(tanh);
    REGISTER_FUNCTION(exp);
    REGISTER_FUNCTION(log);
    REGISTER_FUNCTION(log10);
    REGISTER_FUNCTION(ceil);
    REGISTER_FUNCTION(floor);
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

//求值适配器：将平面变量表包装为作用域栈后调用 eval_scoped
Object Cifa::eval(CalUnit& c, std::unordered_map<std::string, Object>& p)
{
    ScopeStack scopes;
    scopes.emplace_back(p);
    auto o = eval_scoped(c, scopes);
    p = std::move(scopes.front());
    return o;
}

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
                if (idx < 0) idx = 0;
                if (idx > (int)arr.size()) idx = (int)arr.size();
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
                    return get_parameter(c.v[0].str + "::" + c.v[1].str, scopes);
                }
            }
            //.和::作为取成员运算符时，目前只保证一层
            if (c.str == "::") { return get_parameter(c.v[0].str + "::" + c.v[1].str, scopes); }
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
                if (++loop_count > max_loop_iterations) { set_runtime_error("for loop exceeded max iterations"); break; }
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
                if (++loop_count > max_loop_iterations) { set_runtime_error("while loop exceeded max iterations"); break; }
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
                if (++loop_count > max_loop_iterations) { set_runtime_error("do-while loop exceeded max iterations"); break; }
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
                                        && (itr->type == CalUnitType::Constant || itr->type == CalUnitType::Function || itr->type == CalUnitType::Parameter)
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
            if (!is_right)    //未能成功右结合则判断为左结合
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

//注册宿主程序中的 C++ 函数
void Cifa::register_function(const std::string& name, func_type func)
{
    functions[name] = func;
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
        auto p1 = parameters;    //新的变量表
        for (int i = 0; i < std::min(vc.size(), f.arguments.size()); i++)
        {
            p1[f.arguments[i]] = eval_scoped(vc[i], scopes);
        }
        return eval(f.body, p1);
    }
    else
    {
        set_runtime_error("function " + name + " is not defined");
        return Object();
    }
}

//从平面变量表中获取变量引用（用于语法检查阶段）
Object& Cifa::get_parameter(CalUnit& c, std::unordered_map<std::string, Object>& p, bool only_check)
{
    //return p[convert_parameter_name(c, p)];
    auto name = convert_parameter_name(c, p, only_check);
    auto& o = p[name];
    o.name = name;
    return o;
}

//从作用域栈中获取变量引用（用于运行时求值）
Object& Cifa::get_parameter(CalUnit& c, ScopeStack& scopes, bool only_check)
{
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        return resolve_indexed_parameter(c, scopes, only_check, false, true);
    }

    auto name = convert_parameter_name(c, scopes, only_check);
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

//将语法节点转换为变量名字符串（平面变量表版本，处理数组下标）
std::string Cifa::convert_parameter_name(CalUnit& c, std::unordered_map<std::string, Object>& p, bool only_check)
{
    std::string parameter_name = c.str;
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        if (c.v[0].v.size() > 0)
        {
            //检查语法树时不处理坐标
            if (!only_check)
            {
                auto e = eval(c.v[0].v[0], p);
                std::string str;
                if (e.isType<std::string>())
                {
                    str = e.toString();
                }
                else
                {
                    str = std::to_string(e.toInt());
                }
                parameter_name += "[" + str + "]";
            }
        }
    }
    return parameter_name;
}

//将语法节点转换为变量名字符串（作用域栈版本，处理数组下标）
std::string Cifa::convert_parameter_name(CalUnit& c, ScopeStack& scopes, bool only_check)
{
    std::string parameter_name = c.str;
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        if (c.v[0].v.size() > 0)
        {
            //检查语法树时不处理坐标
            if (!only_check)
            {
                auto e = eval_scoped(c.v[0].v[0], scopes);
                std::string str;
                if (e.isType<std::string>())
                {
                    str = e.toString();
                }
                else
                {
                    str = std::to_string(e.toInt());
                }
                parameter_name += "[" + str + "]";
            }
        }
    }
    return parameter_name;
}

//检查变量是否存在于平面变量表中
bool Cifa::check_parameter(CalUnit& c, std::unordered_map<std::string, Object>& p)
{
    return p.count(convert_parameter_name(c, p));
}

//检查变量是否存在于作用域栈中
bool Cifa::check_parameter(CalUnit& c, ScopeStack& scopes)
{
    return check_parameter(convert_parameter_name(c, scopes), scopes);
}

//按名称从平面变量表获取变量引用
Object& Cifa::get_parameter(const std::string& name, std::unordered_map<std::string, Object>& p)
{
    return p[name];
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

//按名称检查变量是否存在于平面变量表中
bool Cifa::check_parameter(const std::string& name, std::unordered_map<std::string, Object>& p)
{
    return p.count(name);
}

//按名称检查变量是否存在于作用域栈中
bool Cifa::check_parameter(const std::string& name, ScopeStack& scopes)
{
    return find_object_from_inner(scopes, name) != nullptr;
}

//获取赋值目标的变量引用，必要时在当前作用域创建新变量
Object& Cifa::get_parameter_for_assign(CalUnit& c, ScopeStack& scopes, bool declare_current)
{
    if (c.v.size() > 0 && c.v[0].str == "[]")
    {
        return resolve_indexed_parameter(c, scopes, false, declare_current, false);
    }

    auto name = convert_parameter_name(c, scopes);
    if (scopes.empty())
    {
        scopes.emplace_back();
    }
    Object* o = nullptr;
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
    const std::string legacy_name = c.str + "[" + std::to_string(index) + "]";
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
        element.name = legacy_name;
        //多维数组：如果还有后续的 [] 下标，递归索引子数组
        if (c.v.size() > 1)
        {
            return resolve_nested_index(element, c, 1, scopes, only_check);
        }
        return element;
    }

    if (!declare_current)
    {
        auto* legacy = find_object_from_inner(scopes, legacy_name);
        if (legacy != nullptr)
        {
            legacy->name = legacy_name;
            return *legacy;
        }
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
    element.name = legacy_name;
    //多维数组：如果还有后续的 [] 下标，递归索引子数组
    if (c.v.size() > 1)
    {
        return resolve_nested_index(element, c, 1, scopes, only_check);
    }
    return element;
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
                        //带下标的参数只注册基名，下标在运行时解析
                        if (c.v[0].v.size() > 0 && c.v[0].v[0].str == "[]")
                        {
                            p[c.v[0].str];
                        }
                        else
                        {
                            get_parameter(c.v[0], p);
                        }
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
                    if (c.v[0].type == CalUnitType::Parameter && get_parameter(c.v[0].str, p).type1 == "__"
                        || c.v[0].type != CalUnitType::Parameter)
                    {
                        add_error(c.v[0], "%s cannot be assigned", c.v[0].str.c_str());
                    }
                }
            }
            if (c.str == "::" || c.str == ".")
            {
                if (c.v.size() == 2)
                {
                    if (c.v[0].type == CalUnitType::Parameter && !check_parameter(c.v[0], p))
                    {
                        add_error(c.v[0], "parameter %s is at right of = but not been initialized", c.v[0].str.c_str());
                    }
                    else if (c.v[1].type == CalUnitType::Parameter)
                    {
                        if (!check_parameter(c.v[0].str + "::" + c.v[1].str, p))
                        {
                            add_error(c.v[0], "parameter %s in %s is at right of = but not been initialized", c.v[1].str.c_str(), c.v[0].str.c_str());
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
            add_error(c, "cannot calculate parameter %s with operands", c.str.c_str());
        }
        //带类型前缀的独立声明（如 int i;），注册变量到作用域
        if (c.with_type)
        {
            if (c.v.size() > 0 && c.v[0].str == "[]")
            {
                p[c.str];
            }
            else
            {
                get_parameter(c, p);
            }
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
                bool ok;
                if (c.v.size() > 0 && c.v[0].str == "[]")
                {
                    ok = check_parameter(c.str, p);
                }
                else
                {
                    ok = check_parameter(c, p);
                }
                if (!ok)
                {
                    add_error(c, "parameter %s is at right of = but not been initialized", c.str.c_str());
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
                bool ok;
                if (c.v.size() > 0 && c.v[0].str == "[]")
                {
                    ok = check_parameter(c.str, p);
                }
                else
                {
                    ok = check_parameter(c, p);
                }
                if (!ok)
                {
                    add_error(c, "parameter %s is at right of = but not been initialized", c.str.c_str());
                }
            }
        }
    }
    else if (c.type == CalUnitType::Function)
    {
        if (c.v.size() == 0)
        {
            add_error(c, "function %s has no operands", c.str.c_str());
        }
        //内置方法名不视为未定义函数
        if (!functions.contains(c.str) && !functions2.contains(c.str))
        {
            if (!builtin_methods.contains(c.str))
            {
                add_error(c, "function %s is not defined", c.str.c_str());
            }
        }
        else if (!functions.contains(c.str) && functions2.contains(c.str) && functions2.at(c.str).body.type == CalUnitType::None)
        {
            if (!builtin_methods.contains(c.str))
            {
                add_error(c, "function %s is not defined", c.str.c_str());
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

//运行脚本（简化版，使用空变量表）
Object Cifa::run_script(std::string str)
{
    std::unordered_map<std::string, Object> p;
    return run_script(str, p);
}

//运行脚本主入口：词法分析→语法树构建→语法检查→求值执行
Object Cifa::run_script(std::string str, std::unordered_map<std::string, Object>& p)
{
    errors.clear();
    clear_runtime_error();
    Object result;

    {
        std::stringstream source_stream(str);
        std::string source_line;
        while (std::getline(source_stream, source_line))
        {
            runtime_source_lines.emplace_back(std::move(source_line));
        }
    }

    str += ";";    //方便处理仅有一行的情况
    auto rv = split(str);
    auto c = combine_all_cal(rv);    //结果必定是一个Union
    //此处设定为在语法树检查不正确时，仍然尝试运行并检查执行时的错误
    //if (errors.empty())
    {
        auto p1 = parameters;
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
        auto o = eval(c, p);
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

//获取所有编译期错误的列表
std::vector<Cifa::ErrorMessage> Cifa::get_errors() const
{
    std::vector<Cifa::ErrorMessage> es;
    for (auto& e : errors)
    {
        es.push_back(e);
    }
    return es;
}

//将所有错误格式化为字符串（带源码行和插入符）
std::string Cifa::get_errors_str() const
{
    std::string str;
    for (auto& e : errors)
    {
        str += "Syntax Error: " + e.message + "\n";
        if (e.line > 0 && e.line <= runtime_source_lines.size())
        {
            const std::string& line_text = runtime_source_lines[e.line - 1];
            std::string header = "  at line " + std::to_string(e.line) + ", col " + std::to_string(e.col) + ": ";
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

//格式化一个运行时调用栈帧：显示行号、源码行和插入符位置
std::string Cifa::format_runtime_frame(const CalUnit& c) const
{
    std::string label = c.str.empty() ? "<none>" : c.str;
    std::string line_text;
    if (c.line > 0 && c.line <= runtime_source_lines.size())
    {
        line_text = runtime_source_lines[c.line - 1];
    }
    if (line_text.empty())
    {
        line_text = label;
    }

    std::string header = "line " + std::to_string(c.line) + ", col " + std::to_string(c.col) + ": ";
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