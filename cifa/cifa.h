#pragma once
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

struct CalUnit
{
    Stat type;
    std::vector<CalUnit> v;
    std::string str;
    object eval();
    CalUnit(Stat s, std::string s1)
    {
        type = s;
        str = s1;
    }
    CalUnit() {}
};

template <typename T>
bool vector_have(std::vector<T>& ops, T& op)
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

Stat guess_char(char c);
std::vector<CalUnit> split(std::string str);
auto replace_cal(std::vector<CalUnit>& ppp, int i, int len, const CalUnit& c);
CalUnit combine_cal_unit(std::vector<CalUnit> ppp);

object run(std::string str);
template <typename F, Arg...args>
void register_function(std::string name, void*);