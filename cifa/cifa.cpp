#include "../../common/convert.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>

using object = double;

enum Stat
{
    None = -1,
    Constant,
    Operator,
    Parameter,
    Function,
    Key,
    Type,
};

std::map<std::string, object> parameters;
std::map<std::string, void*> functions;

bool vector_have(std::vector<std::string>& ops, std::string& op)
{
    for (auto& o : ops)
    {
        if (op == o)
        {
            return true;
        }
    }
    return false;
}

struct CalUnit
{
    Stat type;
    std::vector<CalUnit> v;
    std::string str;
    object eval()
    {
        if (type == Operator)
        {
            switch (str[0])
            {
            case '+':
                return v[0].eval() + v[1].eval();
                break;
            case '-':
                return v[0].eval() - v[1].eval();
                break;
            case '*':
                return v[0].eval() * v[1].eval();
                break;
            case '/':
                return v[0].eval() / v[1].eval();
                break;
            case '=':
                return parameters[v[0].str] = v[1].eval();
            default:
                break;
            }
        }
        else if (type == Constant)
        {
            return atof(str.c_str());
        }
        else if (type == Parameter)
        {
            return parameters[str];
        }
        else if (type == Function)
        {
        }
    }
    CalUnit(Stat s, std::string s1)
    {
        type = s;
        str = s1;
    }
    CalUnit() {}
};

Stat guess(char c)
{
    if (std::string("0123456789").find(c) != std::string::npos)
    {
        return Constant;
    }
    if (std::string("+-*/=().").find(c) != std::string::npos)
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
        auto g = guess(c);
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
    return rv;
}

void replace_cal(std::vector<CalUnit>& ppp, int i, int len, const CalUnit& c)
{
    auto it = ppp.erase(ppp.begin() + i, ppp.begin() + i + len);
    ppp.insert(it, c);
}

//表达式语法树
CalUnit uniform_cal(std::vector<CalUnit> ppp)
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

        replace_cal(ppp, itl0 - ppp.begin(), itr0 - itl0 + 1, uniform_cal(std::vector<CalUnit>{ itl0 + 1, itr0 }));

        //auto c = uniform_cal(std::vector<CalUnit>{ itl0 + 1, itr0 });
        //auto it = ppp.erase(itl0, itr0 + 1);
        //ppp.insert(it, c);
    }

    std::vector<std::vector<std::string>> ops = { { "*", "/" }, { "+", "-" }, { "=" } };
    for (auto& op : ops)
    {
        for (auto it = ppp.begin(); it != ppp.end();)
        {
            auto& p = it;
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

int main()
{
    std::string str = "  (1.6 + 6) * 6 / (87  +90)";
    auto rv = split(str);

    auto c = uniform_cal(rv);
    auto v = c.eval();
    std::cout << str << " = " << v << '\n';
}
