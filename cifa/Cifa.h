#pragma once
#include <functional>
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
    Object(const std::string& str)
    {
        type = "string";
        content = str;
    }
    double value = nan("");
    std::string type;
    std::string content;
    operator bool() { return value; }
    operator double() { return value; }
};

using ObjectVector = std::vector<Object>;

extern std::function<Object(const Object&, const Object&)> add, sub, mul, div, assign;

#define OPERATOR(o1, o2, op, op2) \
    if (o1.type == "" && o2.type == "") \
    { \
        return Object(o1.value op o2.value); \
    } \
    if (op2) \
    { \
        return op2(o1, o2); \
    } \
    return Object(o1.value op o2.value);

inline Object operator!(Object& o)
{
    return Object(!o.value);
}
inline Object operator*(const Object& o1, const Object& o2) { OPERATOR(o1, o2, *, sub); }
inline Object operator/(const Object& o1, const Object& o2) { OPERATOR(o1, o2, /, sub); }
inline Object operator+(const Object& o1, const Object& o2)
{
    if (o1.type == "string" && o2.type == "string")
    {
        return Object(o1.content + o2.content);
    }
    OPERATOR(o1, o2, +, add);
}
inline Object operator-(const Object& o1, const Object& o2) { OPERATOR(o1, o2, -, sub); }
inline Object operator>(const Object& o1, const Object& o2) { return Object(o1.value > o2.value); }
inline Object operator<(const Object& o1, const Object& o2) { return Object(o1.value < o2.value); }
inline Object operator>=(const Object& o1, const Object& o2) { return Object(o1.value >= o2.value); }
inline Object operator<=(const Object& o1, const Object& o2) { return Object(o1.value <= o2.value); }
inline Object operator==(const Object& o1, const Object& o2) { return Object(o1.value == o2.value); }
inline Object operator!=(const Object& o1, const Object& o2) { return Object(o1.value != o2.value); }
inline Object operator&&(const Object& o1, const Object& o2) { return Object(o1.value && o2.value); }
inline Object operator||(const Object& o1, const Object& o2) { return Object(o1.value || o2.value); }
inline Object operator+=(Object& o1, const Object& o2)
{
    if (o1.type == "" && o2.type == "")
    {
        return Object(o1.value += o2.value);
    }
    else if (o1.type == "string" && o2.type == "string")
    {
        return Object(o1.content += o2.content);
    }
    return Object();
}
inline Object operator-=(Object& o1, const Object& o2) { return Object(o1.value -= o2.value); }
inline Object operator*=(Object& o1, const Object& o2) { return Object(o1.value *= o2.value); }
inline Object operator/=(Object& o1, const Object& o2) { return Object(o1.value /= o2.value); }

enum class CalUnitType
{
    None = 0,
    Constant,
    String,
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
    CalUnitType type = CalUnitType::None;
    std::vector<CalUnit> v;
    std::string str;
    size_t line = 0, col = 0;
    CalUnit(CalUnitType s, std::string s1)
    {
        type = s;
        str = s1;
    }
    CalUnit() {}
    bool can_cal()
    {
        return type == CalUnitType::Constant || type == CalUnitType::String || type == CalUnitType::Parameter || type == CalUnitType::Function || type == CalUnitType::Operator && v.size() > 0;
    }
};

Object print(ObjectVector& d);
Object to_string(ObjectVector& d);
Object to_number(ObjectVector& d);

class Cifa
{
    //运算符，大部分为双目，此处的顺序即优先级
    std::vector<std::vector<std::string>> ops = { { ".", "++", "--" }, { "!" }, { "*", "/" }, { "+", "-" }, { ">", "<", ">=", "<=" }, { "==", "!=" }, { "&" }, { "|" }, { "&&" }, { "||" }, { "=", "*=", "/=", "+=", "-=" }, { "," } };
    std::vector<std::string> ops1 = { "++", "--", "!" };    //单目
    //关键字，在表中的位置为其所需参数个数
    std::vector<std::vector<std::string>> keys = { { "else", "break", "continue" }, { "return" }, { "if", "for", "while" } };
    std::vector<std::string> types = { "auto", "int", "float", "double" };

    std::map<std::string, Object> parameters;
    using func_type = Object (*)(std::vector<Object>&);
    std::map<std::string, func_type> functions;

    bool force_return = false;
    Object result;

    std::vector<std::string> errors;

public:
    Cifa()
    {
        register_function("print", print);
        register_function("to_string", to_string);
        register_function("to_number", to_number);
    }
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

    CalUnit combine_all_cal(std::list<CalUnit>& ppp);
    CalUnit combine_multi_line(std::list<CalUnit>& ppp, bool need_end_semicolon);
    std::list<CalUnit>::iterator inside_bracket(std::list<CalUnit>& ppp, std::list<CalUnit>& ppp2, const std::string bl, const std::string br);
    void combine_curly_backet(std::list<CalUnit>& ppp);
    void combine_round_backet(std::list<CalUnit>& ppp);
    void combine_ops(std::list<CalUnit>& ppp);
    void combine_keys(std::list<CalUnit>& ppp);

    void register_function(const std::string& name, func_type func);
    Object run_function(const std::string& name, std::vector<CalUnit> vc);

    Object run_script(const std::string& str);

    template <typename...Args> void add_error(Args... args)
    {
        char buffer[1024];
        sprintf(buffer, args...);
        errors.emplace_back(buffer);
    }
};

}    // namespace cifa