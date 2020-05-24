#include "Cifa.h"
#include <functional>

#include <iostream>

namespace cifa
{

Object Cifa::eval(CalUnit& c)
{
    if (c.type == Operator)
    {
        if (c.v.size() == 1)
        {
            if (c.str == "+") { return eval(c.v[0]); }
            if (c.str == "-") { return -eval(c.v[0]); }
            if (c.str == "!") { return !eval(c.v[0]); }
        }
        if (c.v.size() == 2)
        {
            if (c.str == "+") { return eval(c.v[0]) + eval(c.v[1]); }
            if (c.str == "-") { return eval(c.v[0]) - eval(c.v[1]); }
            if (c.str == "*") { return eval(c.v[0]) * eval(c.v[1]); }
            if (c.str == "/") { return eval(c.v[0]) / eval(c.v[1]); }
            if (c.str == "=") { return parameters[c.v[0].str] = eval(c.v[1]); }
            if (c.str == ",") { return eval(c.v[0]), eval(c.v[1]); }
            if (c.str == ">") { return eval(c.v[0]) > eval(c.v[1]); }
            if (c.str == "<") { return eval(c.v[0]) < eval(c.v[1]); }
            if (c.str == "==") { return eval(c.v[0]) == eval(c.v[1]); }
            if (c.str == "!=") { return eval(c.v[0]) != eval(c.v[1]); }
            if (c.str == ">=") { return eval(c.v[0]) >= eval(c.v[1]); }
            if (c.str == "<=") { return eval(c.v[0]) <= eval(c.v[1]); }
            //if (c.str == "&") { return eval(c.v[0]) & eval(c.v[1]); }
            //if (c.str == "|") { return eval(c.v[0]) | eval(c.v[1]); }
            if (c.str == "&&") { return eval(c.v[0]) && eval(c.v[1]); }
            if (c.str == "||") { return eval(c.v[0]) || eval(c.v[1]); }
        }
        fprintf(stderr, "Unknown operator %s\n", c.str.c_str());
        //return std::nan(c.str.c_str());
    }
    else if (c.type == Constant)
    {
        return atof(c.str.c_str());
    }
    else if (c.type == Parameter)
    {
        if (parameters.count(c.str))
        {
            return parameters[c.str];
        }
        else
        {
            return 0;
        }
    }
    else if (c.type == Function)
    {
        std::vector<CalUnit> v;
        std::function<void(CalUnit&)> get = [&v, &get](CalUnit& c1)
        {
            if (c1.str == ",")
            {
                for (auto& c2 : c1.v)
                {
                    get(c2);
                }
            }
            else
            {
                v.push_back(c1);
            }
        };
        get(c.v[0]);
        return run_function(c.str, v);
    }
    else if (c.type == Key)
    {
        if (c.str == "if")
        {
            if (eval(c.v[0]))
            {
                return eval(c.v[1]);
            }
            else if (c.v.size() >= 3)
            {
                return eval(c.v[2]);
            }
        }
        if (c.str == "for")
        {
            if (eval(c.v[0]))
            {
                return eval(c.v[1]);
            }
        }
    }
    else if (c.type == Union)
    {
        for (auto& c1 : c.v)
        {
            std::cout << eval(c1) << std::endl;
        }
        return c.v.size();
    }
}

Stat Cifa::guess_char(char c)
{
    if (std::string("0123456789").find(c) != std::string::npos)
    {
        return Constant;
    }
    if (std::string("+-*/=().!<>&|,").find(c) != std::string::npos)
    {
        return Operator;
    }
    if (std::string("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").find(c) != std::string::npos)
    {
        return Parameter;
    }
    if (std::string("{};").find(c) != std::string::npos)
    {
        return Split;
    }
    return None;
}

//分割语法
std::list<CalUnit> Cifa::split(std::string str)
{
    std::string r;
    std::list<CalUnit> rv;

    Stat stat = None;
    for (size_t i = 0; i < str.size(); i++)
    {
        auto c = str[i];
        auto pre_stat = stat;
        auto g = guess_char(c);
        if (g == Constant)
        {
            if (stat == Constant || stat == Operator || stat == None)
            {
                stat = Constant;
            }
            else if (stat == Parameter)
            {
                stat = Parameter;
            }
        }
        else if (g == Operator)
        {
            if (c == '.' && stat == Constant)
            {
            }
            else
            {
                stat = Operator;
            }
        }
        else if (g == Split)
        {
            stat = Split;
        }
        else if (g == Parameter)
        {
            if (c == 'E' || c == 'e' && stat == Constant)
            {
            }
            else
            {
                stat = Parameter;
            }
        }
        if (g == None)
        {
            stat = None;
        }
        if (pre_stat != stat || stat == Operator || stat == Split)
        {
            if (pre_stat != None)
            {
                rv.push_back({ pre_stat, r });
            }
            r = c;
        }
        else
        {
            r += c;
        }
    }
    rv.push_back({ stat, r });

    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        //括号前的变量视为函数
        if (it->str == "(")
        {
            if (it != rv.begin() && std::prev(it)->type == Parameter)
            {
                std::prev(it)->type = Function;
            }
        }
        if (vector_have(keys, it->str))
        {
            it->type = Key;
        }
        if (vector_have(types, it->str))
        {
            it->type = Type;
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
                    if (it->str == std::string(1, op[0]) && std::next(it)->str == std::string(1, op[1]))
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
    return rv;
}

auto Cifa::replace_cal(std::list<CalUnit>& ppp, std::list<CalUnit>::iterator i0, std::list<CalUnit>::iterator i1, const CalUnit& c)
{
    auto it = ppp.erase(i0, i1);
    return ppp.insert(it, c);
}

//提取最内层括号
std::list<CalUnit>::iterator inside_bracket(std::list<CalUnit>& ppp, std::list<CalUnit>& ppp2, std::string bl, std::string br)
{
    bool have = false;
    for (auto it = ppp.begin(); it != ppp.end(); ++it)
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
    auto itl0 = ppp.begin(), itr0 = ppp.end();
    for (auto itr = ppp.begin(); itr != ppp.end(); ++itr)
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
    ppp2.splice(ppp2.begin(), ppp, std::next(itl0), itr0);
    return itl0;
}

CalUnit Cifa::combine_multi_line(std::list<CalUnit> ppp)
{
    CalUnit c;
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->str == ";")
        {
            std::list<CalUnit> ppp2;
            ppp2.splice(ppp2.begin(), ppp, ppp.begin(), it);
            c.v.push_back(std::move(combine_all_cal(ppp2)));
            it = ppp.erase(ppp.begin());
        }
        else
        {
            ++it;
        }
    }
    c.type = Union;
    return c;
}

//表达式语法树
CalUnit Cifa::combine_all_cal(std::list<CalUnit>& ppp)
{
    //合并{}
    while (true)
    {
        std::list<CalUnit> ppp2;
        auto it = inside_bracket(ppp, ppp2, "{", "}");
        if (ppp2.empty())
        {
            break;
        }
        auto c1 = combine_multi_line(ppp2);    //此处合并多行
        it = ppp.erase(it);
        *it = std::move(c1);
    }
    //合并()
    while (true)
    {
        std::list<CalUnit> ppp2;
        auto it = inside_bracket(ppp, ppp2, "(", ")");
        if (ppp2.empty())
        {
            break;
        }
        auto c1 = combine_all_cal(ppp2);
        it = ppp.erase(it);
        *it = std::move(c1);
        //如果括号前是函数则合并
        if (it != ppp.begin())
        {
            if (std::prev(it)->type == Function)
            {
                std::prev(it)->v = { *it };
                ppp.erase(it);
            }
        }
    }

    //双目运算符，要求左右皆有操作数，此处的顺序即优先级
    for (auto& op : ops)
    {
        for (auto it = ppp.begin(); it != ppp.end();)
        {
            if (it->type == Operator && it->v.size() == 0 && vector_have(op, it->str))    //已经能计算的不需再算
            {
                if (it == ppp.begin() || !std::prev(it)->canCal() || it->str == "!")    //退化为单目运算
                {
                    it->v = { *std::next(it) };
                    it = ppp.erase(std::next(it));
                }
                else
                {
                    it->v = { *std::prev(it), *std::next(it) };
                    ppp.erase(std::next(it));
                    it = ppp.erase(std::prev(it));
                    ++it;
                }
            }
            else
            {
                ++it;
            }
        }
    }
    //合并关键词
    return ppp.front();    //如果语法正确应该只剩一个
}

void Cifa::register_function(std::string name, func_type func)
{
    functions[name] = func;
}

Object Cifa::run_function(std::string name, std::vector<CalUnit> vc)
{
    if (functions.count(name))
    {
        auto p = functions[name];
        std::vector<Object> v;
        for (auto& c : vc)
        {
            v.push_back(eval(c));
        }
        return p(v);
    }
}

Object Cifa::run_line(std::string str)
{
    auto rv = split(str);
    auto c = combine_all_cal(rv);
    return eval(c);
}

}    // namespace cifa