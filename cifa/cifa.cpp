#include "cifa.h"

std::map<std::string, object> parameters;
std::map<std::string, void*> functions;

object CalUnit::eval()
{
    if (str.size() > 2 && str.find("()") == str.size() - 2)
    {
        str.resize(str.size() - 2);
    }
    if (type == Operator)
    {
        if (str == "+") { return v[0].eval() + v[1].eval(); }
        if (str == "-") { return v[0].eval() - v[1].eval(); }
        if (str == "*") { return v[0].eval() * v[1].eval(); }
        if (str == "/") { return v[0].eval() / v[1].eval(); }
        if (str == "=") { return parameters[v[0].str] = v[1].eval(); }
        if (str == ",") { return v[0].eval(), v[1].eval(); }
    }
    else if (type == Constant)
    {
        return atof(str.c_str());
    }
    else if (type == Parameter)
    {
        if (parameters.count(str))
        {
            return parameters[str];
        }
        else
        {
            return 0;
        }
    }
    else if (type == Function)
    {
    }
}

Stat guess_char(char c)
{
    if (std::string("0123456789").find(c) != std::string::npos)
    {
        return Constant;
    }
    if (std::string("+-*/=().,").find(c) != std::string::npos)
    {
        return Operator;
    }
    if (std::string("_abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ").find(c) != std::string::npos)
    {
        return Parameter;
    }
    return None;
}

//分割语法
std::vector<CalUnit> split(std::string str)
{
    std::string r;
    std::vector<CalUnit> rv;

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
        if (pre_stat != stat || stat == Operator)
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

    std::vector<std::string> keys = { "if", "for", "while" };
    std::vector<std::string> types = { "auto" };
    for (auto it = rv.begin(); it != rv.end(); ++it)
    {
        //括号前的变量视为函数
        if (it->str == "(")
        {
            if (it != rv.begin() && (it - 1)->type == Parameter)
            {
                (it - 1)->type = Function;
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

    //合并多字节运算符，未完成
    return rv;
}

auto replace_cal(std::vector<CalUnit>& ppp, int i, int len, const CalUnit& c)
{
    auto it = ppp.erase(ppp.begin() + i, ppp.begin() + i + len);
    return ppp.insert(it, c);
}

//表达式语法树
CalUnit combine_cal_unit(std::vector<CalUnit> ppp)
{
    //清除括号
    while (true)
    {
        bool have = false;
        for (auto it = ppp.begin(); it != ppp.end(); ++it)
        {
            if (it->str == "(" || it->str == ")")
            {
                have = true;
                break;
            }
        }
        if (!have)
        {
            break;
        }
        auto itl0 = ppp.begin(), itr0 = ppp.end();
        for (auto itr = ppp.begin(); itr != ppp.end(); ++itr)
        {
            if (itr->str == ")")
            {
                itr0 = itr;
                for (auto itl = itr - 1; itl != ppp.begin(); --itl)
                {
                    if (itl->str == "(")
                    {
                        itl0 = itl;
                        break;
                    }
                }
                break;
            }
        }

        itl0 = replace_cal(ppp, itl0 - ppp.begin(), itr0 - itl0 + 1, combine_cal_unit(std::vector<CalUnit>{ itl0 + 1, itr0 }));
        itl0->str += "()";
        //如果括号前是函数则合并
        if (itl0 != ppp.begin())
        {
            if ((itl0 - 1)->type == Function)
            {
                (itl0 - 1)->v = itl0->v;
                ppp.erase(itl0);
            }
        }
        //auto c = uniform_cal(std::vector<CalUnit>{ itl0 + 1, itr0 });
        //auto it = ppp.erase(itl0, itr0 + 1);
        //ppp.insert(it, c);
    }

    //双目运算符，要求左右皆有操作数，此处的顺序即优先级
    std::vector<std::vector<std::string>> ops = { { "*", "/" }, { "+", "-" }, { "=" }, { "," } };
    for (auto& op : ops)
    {
        for (auto it = ppp.begin(); it != ppp.end();)
        {
            if (it->type == Operator && vector_have(op, it->str))
            {
                it->v = { *(it - 1), *(it + 1) };
                //replace_cal(ppp, it - 1 - ppp.begin(), 3, *it);
                ppp.erase(it + 1);
                it = ppp.erase(it - 1);
            }
            ++it;
        }
    }
    return ppp[0];    //应该只剩一个
}

object run(std::string str)
{
    auto rv = split(str);
    auto c = combine_cal_unit(rv);
    return c.eval();
}
