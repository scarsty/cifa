#pragma once
#include <list>
#include <map>
#include <string>
#include <vector>

namespace cifa
{

using Object = double;
using ObjectVector = std::vector<Object>;

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
    Union,
};

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
    bool canCal()
    {
        return type == Constant || type == Parameter || type == Function || type == Operator && v.size() > 0;
    }
};

class Cifa
{
    std::vector<std::vector<std::string>> ops = { { "*", "/" }, { "+", "-" }, { "!", ">", "<", "==", "!=", ">=", "<=" }, { "&&", "||" }, { "=" }, { "," } };
    std::vector<std::string> keys = { "if", "for", "while" };
    std::vector<std::string> types = { "auto" };

    std::map<std::string, Object> parameters;
    using func_type = Object (*)(std::vector<Object>&);
    std::map<std::string, func_type> functions;

public:
    Object eval(CalUnit& c);

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

    CalUnit combine_all_cal(std::list<CalUnit>& ppp);
    CalUnit combine_multi_line(std::list<CalUnit> ppp);

    void register_function(std::string name, func_type func);

    Object run_function(std::string name, std::vector<CalUnit> vc);

    Object run_line(std::string str);
};

}    // namespace cifa