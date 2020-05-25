﻿#include "Cifa.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <sstream>

namespace cifa
{

std::function<Object(const Object&, const Object&)> add, sub, mul, div, assign;

Object print(ObjectVector& d)
{
    for (auto& d1 : d)
    {
        if (d1.type == "string")
        {
            std::cout << d1.content;
        }
        else
        {
            std::cout << d1.value;
        }
    }
    std::cout << "\n";
    return Object(double(d.size()));
}

Object to_string(ObjectVector& d)
{
    if (d.empty())
    {
        return Object("");
    }
    std::ostringstream stream;
    stream << d[0].value;
    return Object(stream.str());
}

Object to_number(ObjectVector& d)
{
    if (d.empty())
    {
        return Object();
    }
    return Object(atof(d[0].content.c_str()));
}

Object Cifa::eval(CalUnit& c)
{
    if (force_return)
    {
        return Object();
    }
    else if (c.type == CalUnitType::Operator)
    {
        if (c.v.size() == 1)
        {
            if (c.str == "+") { return eval(c.v[0]); }
            if (c.str == "-") { return Object(0) - eval(c.v[0]); }
            if (c.str == "!") { return !eval(c.v[0]); }
            if (c.str == "++") { return parameters[c.v[0].str] += 1; }
            if (c.str == "--") { return parameters[c.v[0].str] -= 1; }
            if (c.str == "()++")
            {
                auto v = parameters[c.str];
                parameters[c.v[0].str] += 1;
                return v;
            }
            if (c.str == "()--")
            {
                auto v = parameters[c.str];
                parameters[c.v[0].str] -= 1;
                return v;
            }
        }
        if (c.v.size() == 2)
        {
            if (c.str == "." && c.v[0].can_cal())
            {
                if (c.v[1].type == CalUnitType::Function)
                {
                    if (c.v[1].v[0].type != CalUnitType::None)
                    {
                        CalUnit c1;
                        c1.type = CalUnitType::Operator;
                        c1.str = ",";
                        c1.v = { std::move(c.v[0]), std::move(c.v[1].v[0]) };
                        c.v[1].v = { std::move(c1) };
                    }
                    else
                    {
                        c.v[1].v = { std::move(c.v[0]) };
                    }
                    return eval(c.v[1]);
                }
            }
            if (c.str == "*") { return eval(c.v[0]) * eval(c.v[1]); }
            if (c.str == "/") { return eval(c.v[0]) / eval(c.v[1]); }
            if (c.str == "+") { return eval(c.v[0]) + eval(c.v[1]); }
            if (c.str == "-") { return eval(c.v[0]) - eval(c.v[1]); }
            if (c.str == ">") { return eval(c.v[0]) > eval(c.v[1]); }
            if (c.str == "<") { return eval(c.v[0]) < eval(c.v[1]); }
            if (c.str == ">=") { return eval(c.v[0]) >= eval(c.v[1]); }
            if (c.str == "<=") { return eval(c.v[0]) <= eval(c.v[1]); }
            if (c.str == "==") { return eval(c.v[0]) == eval(c.v[1]); }
            if (c.str == "!=") { return eval(c.v[0]) != eval(c.v[1]); }
            if (c.str == "&") { return eval(c.v[0]) & eval(c.v[1]); }
            if (c.str == "|") { return eval(c.v[0]) | eval(c.v[1]); }
            if (c.str == "&&") { return eval(c.v[0]) && eval(c.v[1]); }
            if (c.str == "||") { return eval(c.v[0]) || eval(c.v[1]); }
            if (c.str == "=") { return parameters[c.v[0].str] = eval(c.v[1]); }
            if (c.str == "+=") { return parameters[c.v[0].str] += eval(c.v[1]); }
            if (c.str == "-=") { return parameters[c.v[0].str] -= eval(c.v[1]); }
            if (c.str == "*=") { return parameters[c.v[0].str] *= eval(c.v[1]); }
            if (c.str == "/=") { return parameters[c.v[0].str] /= eval(c.v[1]); }
            if (c.str == ",") { return eval(c.v[0]), eval(c.v[1]); }
        }
        add_error("Error (%zu, %zu): Unknown operator or wrong using %s", c.line, c.col, c.str.c_str());
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
        if (parameters.count(c.str))
        {
            return parameters[c.str];
        }
        else
        {
            //parameters[c.str] = Object(0);
            return Object(nan(""));
        }
    }
    else if (c.type == CalUnitType::Function)
    {
        std::vector<CalUnit> v;
        if (!c.v.empty())
        {
            expand_comma(c.v[0], v);
        }
        return run_function(c.str, v);
    }
    else if (c.type == CalUnitType::Key)
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
            return Object(0);
        }
        if (c.str == "for")
        {
            Object o;
            //此处v[0]应是一个组合语句
            for (eval(c.v[0].v[0]); eval(c.v[0].v[1]); eval(c.v[0].v[2]))
            {
                o = eval(c.v[1]);
                if (o.type == "break") { break; }
                if (o.type == "continue") { continue; }
                if (force_return) { return Object(); }
            }
            o.type = "";
            return o;
        }
        if (c.str == "while")
        {
            Object o;
            while (eval(c.v[0]))
            {
                o = eval(c.v[1]);
                if (o.type == "break") { break; }
                if (o.type == "continue") { continue; }
                if (force_return) { return Object(); }
            }
            o.type = "";
            return o;
        }
        if (c.str == "break")
        {
            Object o;
            o.type = "break";
            return o;
        }
        if (c.str == "continue")
        {
            Object o;
            o.type = "continue";
            return o;
        }
        if (c.str == "return")
        {
            result = eval(c.v[0]);
            force_return = true;
            return result;
        }
    }
    else if (c.type == CalUnitType::Union)
    {
        Object o;
        for (auto& c1 : c.v)
        {
            o = eval(c1);
            if (o.type == "break") { break; }
            if (o.type == "continue") { break; }
            if (force_return) { return Object(); }
        }
        return o;
    }
    return Object();
}

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
};

CalUnitType Cifa::guess_char(char c)
{
    if (std::string("0123456789").find(c) != std::string::npos)
    {
        return CalUnitType::Constant;
    }
    if (std::string("+-*/=().!<>&|,").find(c) != std::string::npos)
    {
        return CalUnitType::Operator;
    }
    if (std::string("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").find(c) != std::string::npos)
    {
        return CalUnitType::Parameter;
    }
    if (std::string("{};").find(c) != std::string::npos)
    {
        return CalUnitType::Split;
    }
    if (std::string("\"\'").find(c) != std::string::npos)
    {
        return CalUnitType::String;
    }
    return CalUnitType::None;
}

//分割语法
std::list<CalUnit> Cifa::split(std::string str)
{
    std::string r;
    std::list<CalUnit> rv;

    //删除注释
    size_t pos = 0;
    while (pos != std::string::npos)
    {
        if ((pos = str.find("/*", pos)) != std::string::npos)
        {
            auto pos1 = str.find("*/", pos + 2);
            if (pos1 == std::string::npos)
            {
                pos1 = str.size();
            }
            else
            {
                pos1 += 2;
            }
            for (size_t i = pos; i <= pos1; i++)
            {
                if (str[i] != '\n') { str[i] = ' '; }
            }
        }
    }
    pos = 0;
    while (pos != std::string::npos)
    {
        if ((pos = str.find("//", pos)) != std::string::npos)
        {
            auto pos1 = str.find("\n", pos + 2);
            if (pos1 == std::string::npos)
            {
                pos1 = str.size();
            }
            std::fill(str.begin() + pos, str.begin() + pos1, ' ');
        }
    }

    CalUnitType stat = CalUnitType::None;
    char in_string = 0;
    size_t line = 1, col = 0;
    for (size_t i = 0; i < str.size(); i++)
    {
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
            if (stat == CalUnitType::Constant || stat == CalUnitType::Operator || stat == CalUnitType::None)
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
            else
            {
                stat = CalUnitType::Operator;
            }
        }
        else if (g == CalUnitType::Split)
        {
            stat = CalUnitType::Split;
        }
        else if (g == CalUnitType::Parameter)
        {
            if (c == 'E' || c == 'e' && stat == CalUnitType::Constant)
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
        rv.push_back({ stat, r });
    }

    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        //括号前的变量视为函数
        if (it->str == "(")
        {
            if (it != rv.begin() && std::prev(it)->type == CalUnitType::Parameter && functions.count(std::prev(it)->str) > 0)
            {
                std::prev(it)->type = CalUnitType::Function;
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

    //不处理类型符号
    for (auto it = rv.begin(); it != rv.end();)
    {
        if (it->type == CalUnitType::Type)
        {
            it = rv.erase(it);
        }
        else
        {
            ++it;
        }
    }

    return rv;
}

//表达式语法树
CalUnit Cifa::combine_all_cal(std::list<CalUnit>& ppp)
{
    //合并{}
    combine_curly_backet(ppp);
    //合并()
    combine_round_backet(ppp);
    //合并算符
    combine_ops(ppp);
    //删除剩余的所有分号
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->str == ";")
        {
            it = ppp.erase(it);
        }
        else
        {
            ++it;
        }
    }
    //合并关键字
    combine_keys(ppp);

    if (ppp.size() == 0)
    {
        return CalUnit();
    }
    if (ppp.size() == 1)
    {
        return ppp.front();
    }
    else
    {
        CalUnit c;
        c.type = CalUnitType::Union;
        for (auto it = ppp.begin(); it != ppp.end(); ++it)
        {
            c.v.emplace_back(std::move(*it));
        }
        return c;
    }
}

CalUnit Cifa::combine_multi_line(std::list<CalUnit>& ppp, bool need_end_semicolon)
{
    CalUnit c;
    c.type = CalUnitType::Union;
    for (auto it = ppp.begin(); it != ppp.end();)
    {
        if (it->str == ";")
        {
            std::list<CalUnit> ppp2;
            ppp2.splice(ppp2.begin(), ppp, ppp.begin(), it);
            c.v.emplace_back(std::move(combine_all_cal(ppp2)));
            it = ppp.erase(ppp.begin());
        }
        else
        {
            ++it;
        }
    }
    if (!need_end_semicolon && !ppp.empty())
    {
        c.v.emplace_back(std::move(combine_all_cal(ppp)));
    }
    if (c.v.size() == 0)
    {
        return CalUnit();
    }
    if (c.v.size() == 1)
    {
        return c.v.front();
    }
    return c;
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
        add_error("Error (%zu, %zu): Unpaired right bracket %s", it->line, it->col, it->str.c_str());
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
        add_error("Error (%zu, %zu): Unpaired left bracket %s", it->line, it->col, it->str.c_str());
        return ppp.end();
    }
    ppp2.splice(ppp2.begin(), ppp, std::next(itl0), itr0);
    return itl0;
}

void Cifa::combine_curly_backet(std::list<CalUnit>& ppp)
{
    while (true)
    {
        std::list<CalUnit> ppp2;
        //auto size = ppp.size();
        auto it = inside_bracket(ppp, ppp2, "{", "}");
        if (it == ppp.end())
        {
            break;
        }
        auto c1 = combine_multi_line(ppp2, false);    //此处合并多行
        it = ppp.erase(it);
        *it = std::move(c1);
    }
}

void Cifa::combine_round_backet(std::list<CalUnit>& ppp)
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
        auto c1 = combine_multi_line(ppp2, false);
        it = ppp.erase(it);
        *it = std::move(c1);
        //括号前是函数
        if (it != ppp.begin())
        {
            if (std::prev(it)->type == CalUnitType::Function)
            {
                std::prev(it)->v = { *it };
                ppp.erase(it);
            }
            else if (std::prev(it)->type == CalUnitType::Parameter)
            {
                auto itl = std::prev(it);
                add_error("Error (%zu, %zu): %s is not a function", itl->line, itl->col, itl->str.c_str());
                std::prev(it)->v = { *it };
                ppp.erase(it);
            }
            else if (std::prev(it)->type == CalUnitType::Constant)
            {
                auto itl = std::prev(it);
                add_error("Error (%zu, %zu): %s is not a function", itl->line, itl->col, itl->str.c_str());
                std::prev(it)->v = { *it };
                ppp.erase(it);
            }
        }
    }
}

void Cifa::combine_ops(std::list<CalUnit>& ppp)
{
    for (auto& op : ops)
    {
        for (auto it = ppp.begin(); it != ppp.end();)
        {
            if (it->type == CalUnitType::Operator && it->v.size() == 0 && vector_have(op, it->str))    //已经能计算的不需再算
            {
                if (it == ppp.begin() || !std::prev(it)->can_cal() || vector_have(ops1, it->str))    //退化为单目运算
                {
                    auto itr = std::next(it);
                    if (itr != ppp.end() && itr->type == CalUnitType::Parameter)
                    {
                        it->v = { *itr };
                        it = ppp.erase(itr);
                    }
                    else if (it != ppp.begin() && (it->str == "++" || it->str == "--") && std::prev(it)->type == CalUnitType::Parameter)
                    {
                        it->v = { *std::prev(it) };
                        it->str = "()" + it->str;
                        it = ppp.erase(std::prev(it));
                    }
                    else
                    {
                        add_error("Error (%zu, %zu): Operator %s has no operands", it->line, it->col, it->str.c_str());
                        it = ppp.erase(it);
                    }
                }
                else
                {
                    auto itr = std::next(it);
                    if (itr != ppp.end() && itr->can_cal())
                    {
                        it->v = { *std::prev(it), *itr };
                        ppp.erase(itr);
                        it = ppp.erase(std::prev(it));
                        ++it;
                    }
                    else
                    {
                        add_error("Error (%zu, %zu): Operator %s has no operands", it->line, it->col, it->str.c_str());
                        it = ppp.erase(it);
                    }
                }
            }
            else
            {
                ++it;
            }
        }
    }
}

void Cifa::combine_keys(std::list<CalUnit>& ppp)
{
    //合并关键字，从右向左
    auto it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        for (size_t para_count = 1; para_count < keys.size(); para_count++)
        {
            auto& keys1 = keys[para_count];
            if (it->type == CalUnitType::Key && it->v.size() == 0 && vector_have(keys1, it->str))
            {
                auto itr = std::next(it);
                for (size_t i = 0; i < para_count; i++)
                {
                    if (itr != ppp.end())
                    {
                        it->v.emplace_back(std::move(*itr));
                        itr = ppp.erase(itr);
                    }
                    else if (i == para_count - 1 && itr == ppp.end())
                    {
                        it->v.emplace_back();
                    }
                    else
                    {
                        add_error("Error (%zu, %zu): %s has no content", it->line, it->col, it->str.c_str());
                        break;
                    }
                }
                if (it->str == "if" && itr != ppp.end() && itr->str == "else")
                {
                    auto it1r = std::next(itr);
                    if (it1r == ppp.end())
                    {
                        it = itr;
                        add_error("Error (%zu, %zu): else has no content", it->line, it->col);
                        break;
                    }
                    it->v.emplace_back(std::move(*std::next(itr)));
                    itr = ppp.erase(itr);
                    itr = ppp.erase(itr);
                }
            }
        }
    }
}

void Cifa::register_function(const std::string& name, func_type func)
{
    functions[name] = func;
}

Object Cifa::run_function(const std::string& name, std::vector<CalUnit>& vc)
{
    if (functions.count(name))
    {
        auto p = functions[name];
        std::vector<Object> v;
        for (auto& c : vc)
        {
            v.emplace_back(eval(c));
        }
        return p(v);
    }
    else
    {
        return Object();
    }
}

Object Cifa::run_script(const std::string& str)
{
    errors.clear();
    force_return = false;
    auto rv = split(str);
    auto c = combine_all_cal(rv);
    if (errors.empty())
    {
        auto o = eval(c);
        //如果只有一个表达式，则返回其值，否则为return返回的值
        if (c.type != CalUnitType::Union)
        {
            return o;
        }
    }
    else
    {
        std::sort(errors.begin(), errors.end());
        for (auto& e : errors)
        {
            std::cerr << e << "\n";
        }
    }
    return result;
}

}    // namespace cifa