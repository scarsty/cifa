#include "Cifa.h"
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
    return Object(d.size());
}

cifa::Object to_string(ObjectVector& d)
{
    std::ostringstream stream;
    stream << d[0].value;
    return Object(stream.str());
}

cifa::Object to_number(ObjectVector& d)
{
    return Object(atof(d[0].content.c_str()));
}

Object Cifa::eval(CalUnit& c)
{
    if (c.type == Operator)
    {
        if (c.v.size() == 1)
        {
            if (c.str == "+") { return eval(c.v[0]); }
            if (c.str == "-") { return Object(0) - eval(c.v[0]); }
            if (c.str == "!") { return !eval(c.v[0]); }
            //if (c.str == "++") { return eval(c.v[0]); }
            //if (c.str == "--") { return -eval(c.v[0]); }
        }
        if (c.v.size() == 2)
        {
            if (c.str == "+") { return eval(c.v[0]) + eval(c.v[1]); }
            if (c.str == "-") { return eval(c.v[0]) - eval(c.v[1]); }
            if (c.str == "*") { return eval(c.v[0]) * eval(c.v[1]); }
            if (c.str == "/") { return eval(c.v[0]) / eval(c.v[1]); }
            if (c.str == "+=") { return parameters[c.v[0].str] += eval(c.v[1]); }
            if (c.str == "-=") { return parameters[c.v[0].str] -= eval(c.v[1]); }
            if (c.str == "*=") { return parameters[c.v[0].str] *= eval(c.v[1]); }
            if (c.str == "/=") { return parameters[c.v[0].str] /= eval(c.v[1]); }
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
        fprintf(stderr, "Error: Unknown operator or wrong using %s, line %d, col %d\n", c.str.c_str(), c.line, c.col);
        return Object();
    }
    else if (c.type == Constant)
    {
        return Object(atof(c.str.c_str()));
    }
    else if (c.type == String)
    {
        return Object(c.str);
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
        if (!c.v.empty())
        {
            get(c.v[0]);
        }
        if (functions.count(c.str) == 0)
        {
            fprintf(stderr, "Error: %s is not a function, line %d, col %d\n", c.str.c_str(), c.line, c.col);
        }
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
    }
    else if (c.type == Union)
    {
        Object o;
        for (auto& c1 : c.v)
        {
            o = eval(c1);
            if (o.type == "break") { break; }
            if (o.type == "continue") { break; }
        }
        return o;
    }
    return Object();
}

CalUnitType Cifa::guess_char(char c)
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
    if (std::string("\"\'").find(c) != std::string::npos)
    {
        return String;
    }
    return None;
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

    CalUnitType stat = None;
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
            if (stat == String && in_string == c)
            {
                in_string = 0;
                stat = None;
            }
            else
            {
                stat = String;
            }
        }
        else if (g == String)
        {
            if (in_string == 0)
            {
                in_string = c;
                stat = String;
            }
        }
        else if (g == Constant)
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
        else if (g == None)
        {
            stat = None;
        }
        if (pre_stat != stat || stat == Operator || stat == Split)
        {
            if (pre_stat != None)
            {
                CalUnit c(pre_stat, r);
                c.line = line;
                c.col = col - r.size();
                rv.emplace_back(std::move(c));
            }
            r.clear();
            if (g != String)
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
    if (stat != None)
    {
        rv.push_back({ stat, r });
    }

    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        //括号前的变量视为函数
        if (it->str == "(")
        {
            if (it != rv.begin() && std::prev(it)->type == Parameter && functions.count(std::prev(it)->str) > 0)
            {
                std::prev(it)->type = Function;
            }
        }
        if (vector_have(keys, it->str) || vector_have(keys_single, it->str))
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

    //不处理类型符号
    for (auto it = rv.begin(); it != rv.end();)
    {
        if (it->type == Type)
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
    if (parse_result == OK) { combine_curly_backet(ppp); }
    //合并()
    if (parse_result == OK) { combine_round_backet(ppp); }
    //合并算符
    if (parse_result == OK) { combine_ops(ppp); }
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
    if (parse_result == OK) { combine_keys(ppp); }

    if (ppp.size() == 0 || parse_result != OK)
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
        c.type = Union;
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
    c.type = Union;
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
    if (!need_end_semicolon && !ppp.empty())    //{}内的必须一个分号结尾
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
std::list<CalUnit>::iterator Cifa::inside_bracket(std::list<CalUnit>& ppp, std::list<CalUnit>& ppp2, const std::string bl, const std::string br)
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
        fprintf(stderr, "Error: Unpaired right bracket %s, line %d, col %d\n", it->str.c_str(), it->line, it->col);
        parse_result = Error;
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
        fprintf(stderr, "Error: Unpaired left bracket %s, line %d, col %d\n", it->str.c_str(), it->line, it->col);
        parse_result = Error;
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
        auto c1 = combine_all_cal(ppp2);
        it = ppp.erase(it);
        *it = std::move(c1);
        //括号前是函数
        if (it != ppp.begin())
        {
            if (std::prev(it)->type == Function)
            {
                std::prev(it)->v = { *it };
                ppp.erase(it);
            }
        }
    }
}

void Cifa::combine_ops(std::list<CalUnit>& ppp)
{
    //合并运算符
    for (auto& op : ops)
    {
        for (auto it = ppp.begin(); it != ppp.end();)
        {
            if (it->type == Operator && it->v.size() == 0 && vector_have(op, it->str))    //已经能计算的不需再算
            {
                if (it == ppp.begin() || !std::prev(it)->canCal() || vector_have(ops1, it->str))    //退化为单目运算
                {
                    auto itr = std::next(it);
                    if (itr != ppp.end() && itr->canCal())
                    {
                        it->v = { *itr };
                        it = ppp.erase(itr);
                    }
                    else
                    {
                        parse_result = Error;
                    }
                }
                else
                {
                    auto itr = std::next(it);
                    if (itr != ppp.end() && itr->canCal())
                    {
                        it->v = { *std::prev(it), *itr };
                        ppp.erase(itr);
                        it = ppp.erase(std::prev(it));
                        ++it;
                    }
                    else
                    {
                        parse_result = Error;
                    }
                }
                if (parse_result != OK)
                {
                    fprintf(stderr, "Error: Operator %s has no parameter, line %d, col %d\n", it->str.c_str(), it->line, it->col);
                    return;
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
    //合并关键词，从右向左
    auto it = ppp.end();
    while (it != ppp.begin())
    {
        --it;
        if (it->type == Key && it->v.size() == 0 && vector_have(keys, it->str))
        {
            auto itr = std::next(it);
            if (itr == ppp.end())
            {
                parse_result = Error;
                break;
            }
            auto key = it->str;
            it->v.emplace_back(std::move(*std::next(it)));
            auto it1 = ppp.erase(std::next(it));
            if (it1 != ppp.end())
            {
                it->v.emplace_back(std::move(*it1));
                it1 = ppp.erase(it1);
            }
            else
            {
                it->v.emplace_back();
            }
            if (it->str == "if" && it1 != ppp.end() && it1->str == "else")
            {
                auto it1r = std::next(it1);
                if (it1r == ppp.end())
                {
                    it = it1;
                    parse_result = Error;
                    break;
                }
                it->v.emplace_back(std::move(*std::next(it1)));
                it1 = ppp.erase(it1);
                it1 = ppp.erase(it1);
            }
        }
    }
    if (parse_result != OK)
    {
        fprintf(stderr, "Error: %s has no content, line %d, col %d\n", it->str.c_str(), it->line, it->col);
        return;
    }
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
            v.emplace_back(eval(c));
        }
        return p(v);
    }
    else
    {
        return Object();
    }
}

Object Cifa::run_script(std::string str)
{
    parse_result = OK;
    auto rv = split(str);
    auto c = combine_all_cal(rv);
    if (parse_result == OK)
    {
        return eval(c);
    }
    else
    {
        //fprintf(stderr, "Error: Parse error!\n");
        return Object();
    }
}

}    // namespace cifa