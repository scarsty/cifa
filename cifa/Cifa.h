#pragma once
#include <list>
#include <map>
#include <string>
#include <vector>

using object = double;
using object_vector = std::vector<object>;

enum Stat
{
    None = -1,
    Constant,
    Operator,
    Split,
    Parameter,
    Function,
    Key,
    Type,
};

class Cifa
{

    std::map<std::string, object> parameters;
    using func_type = object (*)(std::vector<object>&);
    std::map<std::string, func_type> functions;

public:
    struct CalUnit
    {
        Stat type;
        std::vector<CalUnit> v;
        std::string str;
        CalUnit(Stat s, std::string s1)
        {
            type = s;
            str = s1;
        }
        CalUnit() {}
        void simple()
        {
            if (str.size() > 2 && str.find("()") == str.size() - 2)
            {
                str.resize(str.size() - 2);
            }
        }
        bool canCal()
        {
            return type == Constant || type == Parameter || type == Function || type == Operator && v.size() > 0;
        }
    };

    object eval(CalUnit& c);

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
    std::list<CalUnit> split(std::string str);
    auto replace_cal(std::list<CalUnit>& ppp, std::list<CalUnit>::iterator i0, std::list<CalUnit>::iterator i1, const CalUnit& c);
    CalUnit combine_cal_unit(std::list<CalUnit>& ppp);

    void register_function(std::string name, func_type func);

    object run_function(std::string name, std::vector<CalUnit> vc);

    object run_line(std::string str);
};
