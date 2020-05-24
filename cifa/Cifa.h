#pragma once
#include <list>
#include <map>
#include <string>
#include <vector>

namespace cifa
{

struct Object
{
    Object() {}
    Object(double v) { value = v; }
    double value = 0;
    std::string type;
    std::string content;
    operator bool() { return value; }
    operator double() { return value; }
};

inline Object operator+(Object& o) { return Object(o.value); }
inline Object operator!(Object& o) { return Object(!o.value); }

inline Object operator+(const Object& o1, const Object& o2) { return Object(o1.value + o2.value); }
inline Object operator-(const Object& o1, const Object& o2) { return Object(o1.value - o2.value); }
inline Object operator*(const Object& o1, const Object& o2) { return Object(o1.value * o2.value); }
inline Object operator/(const Object& o1, const Object& o2) { return Object(o1.value / o2.value); }
inline Object operator+=(Object& o1, const Object& o2) { return Object(o1.value += o2.value); }
inline Object operator-=(Object& o1, const Object& o2) { return Object(o1.value -= o2.value); }
inline Object operator*=(Object& o1, const Object& o2) { return Object(o1.value *= o2.value); }
inline Object operator/=(Object& o1, const Object& o2) { return Object(o1.value /= o2.value); }

inline Object operator>(const Object& o1, const Object& o2) { return Object(o1.value > o2.value); }
inline Object operator<(const Object& o1, const Object& o2) { return Object(o1.value < o2.value); }
inline Object operator==(const Object& o1, const Object& o2) { return Object(o1.value == o2.value); }
inline Object operator!=(const Object& o1, const Object& o2) { return Object(o1.value != o2.value); }
inline Object operator>=(const Object& o1, const Object& o2) { return Object(o1.value >= o2.value); }
inline Object operator<=(const Object& o1, const Object& o2) { return Object(o1.value <= o2.value); }

inline Object operator&&(const Object& o1, const Object& o2) { return Object(o1.value && o2.value); }
inline Object operator||(const Object& o1, const Object& o2) { return Object(o1.value || o2.value); }

using ObjectVector = std::vector<Object>;

enum CalUnitType
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
    CalUnitType type = None;
    std::vector<CalUnit> v;
    std::string str;
    CalUnit(CalUnitType s, std::string s1)
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
/// <summary>
///
/// </summary>
class Cifa
{
    //运算符，大部分为双目，此处的顺序即优先级
    std::vector<std::vector<std::string>> ops = { { "*", "/" }, { "+", "-" }, { "*=", "/=", "+=", "-=" }, { "!", ">", "<", "==", "!=", ">=", "<=" }, { "&&", "||" }, { "=" }, { "," } };
    std::vector<std::string> ops1 = { "++", "--", "!" };
    std::vector<std::string> keys = { "if", "else", "for", "while", "break", "continue" };
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

    CalUnitType guess_char(char c);
    std::list<CalUnit> split(std::string str);
    auto replace_cal(std::list<CalUnit>& ppp, std::list<CalUnit>::iterator i0, std::list<CalUnit>::iterator i1, const CalUnit& c);

    CalUnit combine_all_cal(std::list<CalUnit>& ppp);
    CalUnit combine_multi_line(std::list<CalUnit>& ppp, bool need_end_semicolon);
    void combine_ops(std::list<CalUnit>& ppp);
    void combine_keys(std::list<CalUnit>& ppp);

    void register_function(std::string name, func_type func);

    Object run_function(std::string name, std::vector<CalUnit> vc);

    Object run_line(std::string str);
};

}    // namespace cifa