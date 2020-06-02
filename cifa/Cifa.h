#pragma once
#include <cstdio>
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
    Object(double v, const std::string ty = "")
    {
        value = v;
        type = ty;
    }
    Object(const std::string& str, const std::string ty = "string")
    {
        content = str;
        type = ty;
    }
    double value = nan("");
    std::string content;
    std::string type;
    std::vector<Object> v;
    operator bool() { return value; }
    operator int() { return value; }
    operator double() { return value; }
};

using ObjectVector = std::vector<Object>;

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
    //UnionRound,    //()合并模式，仅for语句使用
};

struct CalUnit
{
    CalUnitType type = CalUnitType::None;
    std::vector<CalUnit> v;
    std::string str;
    size_t line = 0, col = 0;
    bool suffix = false;
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
    bool is_statement()
    {
        return suffix || !can_cal();
    }
};

template <typename T>
bool vector_have(const std::vector<T>& ops, const T& op)
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
template <typename T>
bool vector_have(const std::vector<std::vector<T>>& ops, const T& op)
{
    for (auto& ops1 : ops)
    {
        for (auto& o : ops1)
        {
            if (op == o)
            {
                return true;
            }
        }
    }
    return false;
}

class Cifa
{
private:
    //运算符，大部分为双目，此处的顺序即优先级
    std::vector<std::vector<std::string>> ops = { { ".", "++", "--" }, { "!" }, { "*", "/", "%" }, { "+", "-" }, { ">", "<", ">=", "<=" }, { "==", "!=" }, { "&" }, { "|" }, { "&&" }, { "||" }, { "=", "*=", "/=", "+=", "-=" }, { "," } };
    std::vector<std::string> ops1 = { "++", "--", "!" };    //单目
    //关键字，在表中的位置为其所需参数个数
    std::vector<std::vector<std::string>> keys = { { "true", "false" }, { "break", "continue", "else", "return" }, { "if", "for", "while" } };
    std::vector<std::string> types = { "auto", "int", "float", "double" };

    std::map<std::string, void*> user_data;
    std::map<std::string, Object> parameters;
    using func_type = Object (*)(Cifa&, std::vector<Object>&);
    std::map<std::string, func_type> functions;

    bool force_return = false;
    Object result;

    struct ErrorMessage
    {
        size_t line, col;
        std::string message;
    };
    std::vector<ErrorMessage> errors;

public:
    Cifa();
    Object eval(CalUnit& c);
    void expand_comma(CalUnit& c1, std::vector<CalUnit>& v);
    CalUnit& find_right_side(CalUnit& c1);
    //bool need_suffix(CalUnit& c) { return c.can_cal() || vector_have(keys[0], c.str); }

    CalUnitType guess_char(char c);
    std::list<CalUnit> split(std::string& str);

    CalUnit combine_all_cal(std::list<CalUnit>& ppp, bool curly = true, bool square = true, bool round = true);
    std::list<CalUnit>::iterator inside_bracket(std::list<CalUnit>& ppp, std::list<CalUnit>& ppp2, const std::string& bl, const std::string& br);
    void combine_curly_backet(std::list<CalUnit>& ppp);
    void combine_square_backet(std::list<CalUnit>& ppp);
    void combine_round_backet(std::list<CalUnit>& ppp);
    void combine_ops(std::list<CalUnit>& ppp);
    void combine_semi(std::list<CalUnit>& ppp);
    void combine_keys(std::list<CalUnit>& ppp);
    void combine_types(std::list<CalUnit>& ppp);

    void register_function(const std::string& name, func_type func);
    void register_user_data(const std::string& name, void* p);
    void* get_user_data(const std::string& name);
    Object run_function(const std::string& name, std::vector<CalUnit>& vc);

    void check_cal_unit(CalUnit& c);

    Object run_script(std::string str);

    template <typename... Args>
    void add_error(CalUnit& c, Args... args)
    {
        ErrorMessage e;
        e.line = c.line;
        e.col = c.col;
        char buffer[1024];
        snprintf(buffer, 1024, args...);
        e.message = buffer;
        errors.emplace_back(std::move(e));
    }

    //四则运算准许用户增加自定义功能

#define OPERATOR(o1, o2, op, op2) \
    if (o1.type == "" && o2.type == "") \
    { \
        return Object(o1.value op o2.value); \
    } \
    if (op2) \
    { \
        return op2(*this, o1, o2); \
    } \
    return Object(o1.value op o2.value);

    std::function<Object(Cifa&c, const Object&, const Object&)> user_add, user_sub, user_mul, user_div;

    Object mul(const Object& o1, const Object& o2) { OPERATOR(o1, o2, *, user_mul); }
    Object div(const Object& o1, const Object& o2) { OPERATOR(o1, o2, /, user_div); }
    Object add(const Object& o1, const Object& o2)
    {
        if (o1.type == "string" && o2.type == "string")
        {
            return Object(o1.content + o2.content);
        }
        OPERATOR(o1, o2, +, user_add);
    }
    Object sub(const Object& o1, const Object& o2) { OPERATOR(o1, o2, -, user_sub); }
};

Object print(Cifa& c, ObjectVector& d);
Object to_string(Cifa& c, ObjectVector& d);
Object to_number(Cifa& c, ObjectVector& d);

}    // namespace cifa