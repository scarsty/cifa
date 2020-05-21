#include "../../common/convert.h"
#include <iostream>
#include <map>
#include <string>
#include <vector>

enum Stat
{
    None = -1,
    Number,
    Op,
    Para,
    Calc,
};

struct Cal
{
    Stat type;
    std::vector<Cal> v;
    char op_;
    std::string str;
    double eval()
    {
        if (type == Op)
        {
            switch (op_)
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
            default:
                break;
            }
        }
        else if (type == Number)
        {
            return atof(str.c_str());
        }
    }

    Cal(std::string s) { str = s; }
    Cal(Stat s, std::string s1)
    {
        type = s;
        str = s1;
        op_ = s1[0];
    }
    Cal() {}
};

Stat guess(char c)
{
    if (c >= '0' && c <= '9')
    {
        return Number;
    }
    if (c == '+' || c == '-' || c == '*' || c == '/')
    {
        return Op;
    }
    return Para;
}

std::vector<Cal> split(std::string str)
{
    convert::replaceAllSubStringRef(str, " ", "");
    Stat stat = guess(str[0]);
    std::string r;
    std::vector<Cal> rv;
    for (size_t i = 0; i < str.size(); i++)
    {
        auto pre_stat = stat;
        auto c = str[i];
        if (guess(c) == Number && (stat == Number || stat == Op))
        {
            stat = Number;
        }
        else if (guess(c) == Op)
        {
            stat = Op;
        }
        else if (guess(c) == Para)
        {
            if (c == '.' || c == 'E' || c == 'e' && stat == Number)
            {
            }
            else
            {
                stat = Para;
            }
        }
        if (pre_stat == stat)
        {
            r += c;
        }
        else
        {
            rv.push_back({ pre_stat, r });
            r = c;
        }
    }
    rv.push_back({ stat, r });
    return rv;
}

Cal uniform_cal(std::vector<Cal> ppp)
{
    std::vector<std::string> ops = { "*/", "+-" };
    for (auto& op : ops)
    {
        for (auto it = ppp.begin(); it != ppp.end();)
        {
            auto& p = it;
            if (it->type == Op && op.find(it->str) != std::string::npos)
            {
                it->v = { *(it - 1), *(it + 1) };
                ppp.erase(it + 1);
                it = ppp.erase(it - 1);
            }
            ++it;
        }
    }
    return ppp[0];  //should only one
}

int main()
{
    std::cout << "Hello World!\n";
    std::string str = "1.6 + 6e3 * 6";
    auto rv = split(str);
    for (auto s : rv)
    {
        std::cout << s.str << '\n';
    }
    auto c = uniform_cal(rv);
    auto v = c.eval();
    std::cout << str << " = " << v << '\n';
}
