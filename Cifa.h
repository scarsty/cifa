#pragma once
#include <any>
#include <array>
#include <cstdint>
#include <cmath>
#include <deque>
#include <format>
#include <functional>
#include <limits>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace cifa
{
struct CalUnit;
class Cifa;

enum class ValueType
{
    Empty,
    Auto,
    Void,
    Int,
    Float,
    Double,
    Bool,
    Char,
    String,
    Array,
    Map,
    Struct,
    NoValue,
    Dynamic,
};

struct Object
{
    friend CalUnit;
    friend Cifa;

    Object() {}

    Object(double v)
    {
        value = v;
        stored_type = ValueType::Double;
    }

    Object(double v, const std::string& t)
    {
        value = v;
        stored_type = ValueType::Double;
        type1 = t;
    }

    Object(float v)
    {
        value = v;
        stored_type = ValueType::Float;
    }

    Object(float v, const std::string& t)
    {
        value = v;
        stored_type = ValueType::Float;
        type1 = t;
    }

    Object(const std::string& str)
    {
        value = str;
        stored_type = ValueType::String;
    }

    Object(const std::string& str, const std::string& t)
    {
        value = str;
        stored_type = ValueType::String;
        type1 = t;
    }

    template <typename T, typename std::enable_if<std::is_arithmetic_v<std::decay_t<T>>
        && !std::is_same_v<std::decay_t<T>, double>
        && !std::is_same_v<std::decay_t<T>, float>
        && !std::is_same_v<std::decay_t<T>, int>
        && !std::is_same_v<std::decay_t<T>, bool>
        && !std::is_same_v<std::decay_t<T>, char>, int>::type = 0>
    Object(T v)
    {
        if constexpr (std::is_integral_v<std::decay_t<T>>)
        {
            value = static_cast<std::int32_t>(v);
            stored_type = ValueType::Int;
        }
        else
        {
            value = static_cast<double>(v);
            stored_type = ValueType::Double;
        }
    }

    template <typename T, typename std::enable_if<!std::is_same_v<std::decay_t<T>, Object>
        && !std::is_arithmetic_v<std::decay_t<T>>, int>::type = 0>
    Object(const T& v)
    {
        value = v;
    }

    Object(int v)
    {
        value = static_cast<std::int32_t>(v);
        stored_type = ValueType::Int;
    }

    Object(bool v)
    {
        value = v;
        stored_type = ValueType::Bool;
    }

    Object(char v)
    {
        value = v;
        stored_type = ValueType::Char;
    }

    operator bool() const { return toDouble() != 0; }

    operator int() const { return toInt(); }

    operator double() const { return toDouble(); }

    operator std::string() const { return toString(); }

    bool toBool() const
    {
        if (stored_type == ValueType::Bool)
        {
            return std::any_cast<bool>(value);
        }
        if (stored_type == ValueType::Int)
        {
            return std::any_cast<std::int32_t>(value) != 0;
        }
        if (stored_type == ValueType::Char)
        {
            return std::any_cast<char>(value) != 0;
        }
        if (stored_type == ValueType::Float)
        {
            return std::any_cast<float>(value) != 0.0f;
        }
        if (stored_type == ValueType::Double)
        {
            return std::any_cast<double>(value) != 0.0;
        }
        report_conversion_error("bool");
        return false;
    }

    int toInt() const { return static_cast<int>(toInt32()); }

    std::int32_t toInt32() const
    {
        if (stored_type == ValueType::Int)
        {
            return std::any_cast<std::int32_t>(value);
        }
        if (stored_type == ValueType::Bool)
        {
            return std::any_cast<bool>(value) ? 1 : 0;
        }
        if (stored_type == ValueType::Char)
        {
            return static_cast<std::int32_t>(std::any_cast<char>(value));
        }
        if (stored_type == ValueType::Float)
        {
            const float number = std::any_cast<float>(value);
            if (!std::isfinite(number) || number > std::numeric_limits<std::int32_t>::max()
                || number < std::numeric_limits<std::int32_t>::min())
            {
                report_conversion_error("int");
                return 0;
            }
            return static_cast<std::int32_t>(number);
        }
        if (stored_type == ValueType::Double)
        {
            const double number = std::any_cast<double>(value);
            if (!std::isfinite(number) || number > std::numeric_limits<std::int32_t>::max()
                || number < std::numeric_limits<std::int32_t>::min())
            {
                report_conversion_error("int");
                return 0;
            }
            return static_cast<std::int32_t>(number);
        }
        report_conversion_error("int");
        return 0;
    }

    float toFloat() const
    {
        if (stored_type == ValueType::Float)
        {
            return std::any_cast<float>(value);
        }
        return static_cast<float>(toDouble());
    }

    double toDouble() const
    {
        if (stored_type == ValueType::Double)
        {
            return std::any_cast<double>(value);
        }
        if (stored_type == ValueType::Float)
        {
            return static_cast<double>(std::any_cast<float>(value));
        }
        if (stored_type == ValueType::Int)
        {
            return static_cast<double>(std::any_cast<std::int32_t>(value));
        }
        if (stored_type == ValueType::Bool)
        {
            return std::any_cast<bool>(value) ? 1.0 : 0.0;
        }
        if (stored_type == ValueType::Char)
        {
            return static_cast<double>(std::any_cast<char>(value));
        }
        report_conversion_error("double");
        return NAN;
    }

    std::string toString() const
    {
        if (value.type() == typeid(std::string))
        {
            return std::any_cast<std::string>(value);
        }
        report_conversion_error("string");
        return "";
    }

    //复制，不会改变原来的值
    template <typename T>
    T to() const
    {
        if (value.type() == typeid(T))
        {
            return std::any_cast<T>(value);
        }
        report_conversion_error(typeid(T).name());
        return T();
    }

    //const与非const版本，按需使用
    //如果转换失败，后续使用时也不会正常，因此应谨慎使用，或者在isType()判断后使用
    template <typename T>
    const T& ref() const
    {
        if (value.type() == typeid(T))
        {
            return std::any_cast<const T&>(value);
        }
        report_conversion_error(typeid(T).name());
        return empty_reference<T>();
    }

    template <typename T>
    T& ref()
    {
        if (value.type() == typeid(T))
        {
            return std::any_cast<T&>(value);
        }
        report_conversion_error(typeid(T).name());
        return empty_reference<T>();
    }

    template <typename T>
    bool isType() const { return value.type() == typeid(T); }

    bool isNumber() const
    {
        return stored_type == ValueType::Int || stored_type == ValueType::Float
            || stored_type == ValueType::Double || stored_type == ValueType::Bool
            || stored_type == ValueType::Char;
    }

    bool isInteger() const
    {
        return stored_type == ValueType::Int || stored_type == ValueType::Bool
            || stored_type == ValueType::Char;
    }

    bool isEffectNumber() const { return isNumber() && !std::isnan(toDouble()) && !std::isinf(toDouble()); }

    bool hasValue() const { return value.has_value(); }

    const std::vector<Object>& subV() const { return v; }

    const std::string& getSpecialType() const { return type1; }

    ValueType getStoredType() const { return stored_type; }

    ValueType getDeclaredType() const { return declared_type; }

    const std::string& getDeclaredTypeName() const { return declared_type_name; }

    bool isTyped() const { return type_fixed; }

    std::type_info const& getType() const { return value.type(); }

private:
    struct NoValue
    {
        std::string function_name;
        std::string call_frame;
    };

    static Object make_no_value(const std::string& function_name, std::string call_frame)
    {
        Object result;
        result.value = NoValue{ function_name, std::move(call_frame) };
        result.stored_type = ValueType::NoValue;
        result.type1 = "NoValue";
        return result;
    }

    bool report_no_value() const
    {
        if (type1 != "NoValue")
        {
            return false;
        }
        const auto* no_value = std::any_cast<NoValue>(&value);
        const std::string function_name = no_value == nullptr ? "<unknown>" : no_value->function_name;
        report_runtime_error("function '" + function_name + "' has no return value", this);
        return true;
    }

    void report_conversion_error(const std::string& target_type) const
    {
        if (report_no_value()) { return; }
        const std::string object_name = name.empty() ? "<temporary>" : name;
        const std::string source_type = value.has_value() ? value.type().name() : "<empty>";
        report_runtime_error("type conversion failed: variable '" + object_name + "' from " + source_type + " to " + target_type, this);
    }

    template <typename T>
    static T& empty_reference()
    {
        static thread_local T empty{};
        empty = T{};
        return empty;
    }

    static void report_runtime_error(const std::string& message, const Object* source)
    {
        if (!runtime_error_reporters.empty())
        {
            runtime_error_reporters.back()(message, source);
        }
    }

    static void set_runtime_error_reporter(const std::function<void(const std::string&, const Object*)>& reporter)
    {
        runtime_error_reporters.push_back(reporter);
    }

    static void clear_runtime_error_reporter()
    {
        if (!runtime_error_reporters.empty())
        {
            runtime_error_reporters.pop_back();
        }
    }

    inline static thread_local std::vector<std::function<void(const std::string&, const Object*)>> runtime_error_reporters;

    std::any value;
    ValueType stored_type = ValueType::Empty;
    ValueType declared_type = ValueType::Dynamic;
    std::string declared_type_name;
    std::string element_type_name;
    bool type_fixed = false;
    std::string type1;        //特别的类型，用于Error、break、continue
    std::vector<Object> v;    //仅用于处理逗号表达式
    std::string name;
    const Object* argument_origin = nullptr;
};

using ObjectVector = std::vector<Object>;
using ObjectMap = std::map<std::string, Object>;

struct StructField
{
    std::string name;
    std::string type_name;
};

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
    Cast,
    Label,
    Goto,
    //UnionRound,    //()合并模式，仅for语句使用
};

struct CalUnit
{
    CalUnitType type = CalUnitType::None;
    std::vector<CalUnit> v;    //语法树的节点，v.size():[0,3]
    std::string str;
    size_t line = 0, col = 0;
    bool suffix = false;        //有后缀，可视为一个语句
    bool with_type = false;     //有前置的类型
    std::string type_name;      //前置类型名；内置类型或用户定义 struct 类型
    bool un_combine = false;    //是否合并到语法树，目前仅case和default后面的冒号使用

    CalUnit(CalUnitType s, std::string s1)
    {
        type = s;
        str = s1;
    }

    CalUnit() {}

    bool can_cal()
    {
        return type == CalUnitType::Constant || type == CalUnitType::String || type == CalUnitType::Parameter
            || type == CalUnitType::Function || type == CalUnitType::Cast
            || type == CalUnitType::Operator && v.size() > 0;
    }

    bool is_statement()
    {
        return suffix || !can_cal();
    }
};

struct Function2
{
    struct Argument
    {
        std::string name;
        std::string type_name;
    };

    std::vector<Argument> arguments;
    CalUnit body;
    std::string return_type;
};

using FunctionOverloads = std::unordered_map<size_t, Function2>;

struct SourceLineInfo
{
    std::string filename;
    size_t line = 0;
    std::string text;
};

class Ast
{
    friend class Cifa;

    CalUnit root;
    std::unordered_map<std::string, size_t> labels;
    std::unordered_map<std::string, FunctionOverloads> functions;
    std::unordered_map<std::string, std::vector<StructField>> struct_defs;
    std::vector<SourceLineInfo> source_line_infos;
    bool compiling = false;
    bool compiled = false;
    bool compile_failed = false;

public:
    Ast() = default;
    Ast(const Ast&) = delete;
    Ast& operator=(const Ast&) = delete;
    Ast(Ast&&) noexcept = default;
    Ast& operator=(Ast&&) noexcept = default;

    bool valid() const { return compiled && !compile_failed; }
    explicit operator bool() const { return valid(); }
};

class Cifa
{
public:
    using func_type = std::function<Object(ObjectVector&)>;
    using ScopeStack = std::vector<std::unordered_map<std::string, Object>>;

private:
    template <typename Arg>
    static decltype(auto) object_to_cpp_arg(Object& o)
    {
        using T = std::remove_cvref_t<Arg>;
        if constexpr (std::is_same_v<T, Object>)
        {
            if constexpr (std::is_lvalue_reference_v<Arg>)
            {
                return static_cast<Arg>(o);
            }
            else
            {
                return o;
            }
        }
        else if constexpr (std::is_same_v<T, std::string>)
        {
            return o.toString();
        }
        else if constexpr (std::is_same_v<T, bool>)
        {
            return o.toBool();
        }
        else if constexpr (std::is_integral_v<T>)
        {
            return static_cast<T>(o.toInt());
        }
        else if constexpr (std::is_floating_point_v<T>)
        {
            return static_cast<T>(o.toDouble());
        }
        else
        {
            return o.to<T>();
        }
    }

    template <typename R, typename... Args, size_t... I>
    Object call_registered_function(R (*func)(Args...), ObjectVector& args, std::index_sequence<I...>)
    {
        if constexpr (std::is_void_v<R>)
        {
            func(object_to_cpp_arg<Args>(args[I])...);
            return Object();
        }
        else if constexpr (std::is_same_v<std::remove_cvref_t<R>, Object>)
        {
            return func(object_to_cpp_arg<Args>(args[I])...);
        }
        else
        {
            return Object(func(object_to_cpp_arg<Args>(args[I])...));
        }
    }

    //运算符，此处的顺序即优先级，单目和右结合由下面的列表判断
    inline static const std::vector<std::vector<std::string>> ops = { { "::", ".", "++", "--" }, { "~", "!" }, { "*", "/", "%" }, { "+", "-" }, { "<<", ">>" }, { ">", "<", ">=", "<=" }, { "==", "!=" }, { "&" }, { "^" }, { "|" }, { "&&" }, { ":", "?" }, { "||" }, { "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "|=", "^=" }, { "," } };
    //单目运算符全部是右结合
    inline static const std::unordered_set<std::string> ops_single = { "++", "--", "~", "!", "()++", "()--" };
    //右结合的运算符，注意+-既有单目又有双目，因此不能简单地放在单目列表中
    inline static const std::unordered_set<std::string> ops_right = { "=", "*=", "/=", "%=", "+=", "-=", "<<=", ">>=", "&=", "|=", "^=" };
    //关键字，在表中的下标为其所需子节点个数
    inline static const std::array<std::unordered_set<std::string>, 3> keys = { {
        { "true", "false" },
        { "break", "continue", "else", "return", "default", "goto" },
        { "if", "for", "while", "do", "switch", "case" },
    } };
    //类型列表；auto 在初始化或首次赋值时推导，string 为 Cifa 独有类型
    inline static const std::unordered_set<std::string> types = { "auto", "void", "int", "float", "double", "bool", "string", "char" };
    //内置的运算符表示列表，用户可扩展运算符时会用到，注意这些运算符在语法分析阶段会被转换为对应的符号（如and转换为&&），因此用户扩展时也应使用符号形式的运算符
    inline static const std::map<std::string, std::string> op_representations = { { "and", "&&" }, { "and_eq", "&=" }, { "bitand", "&" }, { "bitor", "|" }, { "compl", "~" }, { "not", "!" }, { "not_eq", "!=" }, { "or", "||" }, { "or_eq", "|=" }, { "xor", "^" }, { "xor_eq", "^=" }, { "<%", "{" }, { "%>", "}" }, { "<:", "[" }, { ":>", "]" }, { "%:", "#" }, { "%:%:", "##" } };
    //内置的数组/map方法列表
    inline static const std::set<std::string> builtin_methods = { "push_back", "pop_back", "resize", "insert", "erase", "clear", "contains", "keys" };

    std::unordered_map<std::string, func_type> functions;     //在宿主程序中注册的函数
    std::unordered_map<std::string, FunctionOverloads> functions2;    //执行脚本后注册的全局脚本函数
    std::unordered_map<std::string, std::vector<StructField>> struct_defs;    //执行脚本后注册的全局 struct

    std::unordered_map<std::string, void*> user_data;
    std::unordered_map<std::string, Object> global_variables;    //C++ 注册变量与脚本顶层变量共用的实例全局表
    std::vector<std::string> include_dirs;                  //#include 搜索目录

    struct ErrorMessage
    {
        std::string filename;
        size_t line = 0, col = 0;
        std::string message;
        size_t expanded_line = 0;
        std::string source_text;
        bool has_source_text = false;
    };

    struct ErrorMessageComp
    {
        bool operator()(const ErrorMessage& l, const ErrorMessage& r) const
        {
            size_t left_line = l.expanded_line != 0 ? l.expanded_line : l.line;
            size_t right_line = r.expanded_line != 0 ? r.expanded_line : r.line;
            if (left_line != right_line)
            {
                return left_line < right_line;
            }
            if (l.col != r.col)
            {
                return l.col < r.col;
            }
            if (l.filename != r.filename)
            {
                return l.filename < r.filename;
            }
            return l.message < r.message;
        }
    };

    using ErrorSet = std::set<ErrorMessage, ErrorMessageComp>;

    struct ReturnState
    {
        bool has_value = false;
        Object value;
        std::string return_type;
    };

    struct ExecutionContext
    {
        explicit ExecutionContext(Ast& current_program) : program(current_program) { }

        Ast& program;
        size_t start_index = 0;
        std::vector<std::string> runtime_call_stack;
        std::vector<std::string> runtime_error_call_stack;
        const std::vector<CalUnit>* active_function_arguments = nullptr;
        const ObjectVector* active_function_values = nullptr;
        std::string runtime_error_message;
        std::vector<ReturnState> return_states;
        ErrorSet errors;
        bool exit_requested = false;
    };

    std::deque<ExecutionContext> execution_contexts;
    Ast compilation_ast;
    ErrorSet errors;
    std::vector<std::string> runtime_error_call_stack;
    std::string runtime_error_message;
    bool last_exit_requested = false;

    bool output_error = true;

public:
    Cifa();
    ~Cifa() = default;
    Cifa(const Cifa&) = delete;
    Cifa& operator=(const Cifa&) = delete;
    Cifa(Cifa&&) = delete;
    Cifa& operator=(Cifa&&) = delete;

    static bool is_valid_key(const std::string& key);
    static std::string revise_key(const std::string& key);
    static ValueType value_type_from_name(const std::string& name);
    static std::string value_type_name(ValueType type);

    bool register_function(const std::string& name, func_type func);

    template <typename R, typename... Args>
    bool register_function(const std::string& name, R (*func)(Args...))
    {
        if (!validate_registration_name(name))
        {
            return false;
        }
        functions[name] = [this, name, func](ObjectVector& args) -> Object
        {
            constexpr size_t argc = sizeof...(Args);
            if (args.size() != argc)
            {
                set_runtime_error("function '" + name + "' expects " + std::to_string(argc) + " arguments, got " + std::to_string(args.size()));
                return Object();
            }
            return call_registered_function(func, args, std::index_sequence_for<Args...>{});
        };
        return true;
    }

    bool register_user_data(const std::string& name, void* p);
    bool register_parameter(const std::string& name, Object o);

    template <typename T>
    bool register_parameter(const std::string& name, std::map<std::string, T> m)
    {
        if (!validate_registration_name(name))
        {
            return false;
        }
        ObjectMap omap;
        for (auto& [k, v] : m)
        {
            omap[k] = Object(v);
        }
        global_variables[name] = Object(std::move(omap));
        return true;
    }

    template <typename T>
    bool register_vector(const std::string& name, const std::vector<T>& v)
    {
        if (!validate_registration_name(name))
        {
            return false;
        }
        std::vector<Object> arr;
        arr.reserve(v.size());
        for (auto& o : v)
        {
            arr.emplace_back(Object(o));
        }
        global_variables[name] = Object(std::move(arr));
        return true;
    }

    void* get_user_data(const std::string& name);

    void set_include_dirs(const std::vector<std::string>& dirs);    //设置#include搜索目录

    Object run_script(std::string script);    //运行脚本，使用实例全局变量表；按当前目录和include搜索目录处理#include

    Object run_file(const std::string& filename);    //从文件运行脚本，支持#include指令，并将文件所在目录作为搜索路径

    Ast compile_script(std::string script);    //解析并返回独立 AST，不执行
    Ast compile_file(const std::string& filename);    //从文件解析并返回独立 AST，不执行
    Object run(Ast& program, const std::string& entry_label = "");    //执行传入 AST；空标签从第一个顶层节点开始

    bool has_error() const;

    std::string get_errors_str() const;

    //建议优先使用 get_errors_str() 或 print_errors()，直接获取带行列信息的格式化字符串
    std::vector<ErrorMessage> get_errors() const;

    void print_errors() const;

    void set_output_error(bool oe) { output_error = oe; }

    void request_exit();
    bool is_exit_requested() const;

    std::string get_runtime_error() const;
    bool has_runtime_error() const;

    //用户可扩展的运算符函数列表
    std::vector<std::function<Object(const Object&, const Object&)>> user_add, user_sub, user_mul, user_div, user_mod,
        user_less, user_more, user_less_equal, user_more_equal,
        user_equal, user_not_equal, user_bit_and, user_bit_or, user_bit_xor, user_logic_and, user_logic_or,
        user_shift_left, user_shift_right;

private:
    static const std::unordered_set<std::string>& keyword_tokens();
    static const std::unordered_set<std::string>& operator_tokens();
    static const std::vector<std::unordered_set<std::string>>& operator_precedence_token_groups();

    Object eval_scoped(CalUnit& c, ScopeStack& scopes);
    bool eval_condition(CalUnit& c, ScopeStack& scopes);
    Object run_function(const CalUnit& call_site, std::vector<CalUnit>& vc, ScopeStack& scopes);
    void run_compilation(const std::function<void(Ast&)>& action);
    Object eval_builtin_method(const CalUnit& method, Object& obj, std::vector<CalUnit>& args, ScopeStack& scopes);
    bool apply_declared_type(Object& object, const std::string& type_name, const CalUnit* location, bool infer_auto);
    Object convert_object_type(const Object& source, const std::string& type_name, const CalUnit* location);
    Object& assign_object_value(Object& target, Object value, const CalUnit& lhs, const CalUnit* location);
    void set_array_element_type(Object& array, const std::string& type_name);
    Object make_declared_default(const std::string& type_name) const;
    ErrorSet& active_errors();
    const ErrorSet& active_errors() const;
    const std::vector<SourceLineInfo>& active_source_line_infos() const;
    void record_error(ErrorMessage error);
    FunctionOverloads* find_script_function(const std::string& name);
    const std::vector<StructField>* find_struct_definition(const std::string& name) const;

    void expand_comma(CalUnit& c1, std::vector<CalUnit>& v);
    CalUnitType guess_char(char c);
    std::list<CalUnit> split(std::string& str);
    CalUnit combine_all_cal(std::list<CalUnit>& ppp, bool curly = true, bool square = true, bool round = true,
        bool allow_labels = true, bool global_scope = true);
    void combine_curly_bracket(std::list<CalUnit>& ppp);
    void combine_square_bracket(std::list<CalUnit>& ppp);
    void combine_round_bracket(std::list<CalUnit>& ppp);
    void combine_ops(std::list<CalUnit>& ppp);
    void combine_semi(std::list<CalUnit>& ppp);
    void deal_special_keys(std::list<CalUnit>& ppp);
    void combine_keys(std::list<CalUnit>& ppp);
    void combine_functions2(std::list<CalUnit>& ppp, bool global_scope);
    void combine_structs(std::list<CalUnit>& ppp, bool global_scope);
    void check_goto_targets(CalUnit& root);

    Object& get_parameter(CalUnit& c, ScopeStack& scopes, bool only_check = false);
    Object& get_or_create_parameter(const std::string& name, ScopeStack& scopes, bool current_scope_only = false);
    Object& get_parameter_for_assign(CalUnit& c, ScopeStack& scopes, bool declare_current = false);
    Object& resolve_indexed_parameter(CalUnit& c, ScopeStack& scopes, bool only_check, bool declare_current, bool declaration_as_array);
    bool try_eval_array_literal(CalUnit& c, ScopeStack& scopes, Object& out);
    bool is_array_literal_candidate(CalUnit& c) const;
    bool validate_registration_name(const std::string& name);
    Object* find_object_from_inner(ScopeStack& scopes, const std::string& name);
    bool has_return_value() const;
    Object& return_value();
    std::string format_runtime_frame(const CalUnit& c) const;
    void set_runtime_error(const std::string& message, const Object* source = nullptr, const CalUnit* location = nullptr);
    void clear_runtime_error();
    bool should_stop_execution() const { return has_runtime_error() || is_exit_requested(); }
    bool is_control_signal(const Object& value, const std::string& signal) const;
    std::string format_runtime_error() const;
    void print_runtime_error() const;
    Object make_error_result() const;
    void compile_pipeline(std::string str, Ast& program);

    void check_cal_unit(CalUnit& c, CalUnit* father, std::unordered_map<std::string, Object>& p);
    void check_non_block_body(CalUnit& c, const std::unordered_map<std::string, Object>& p);

    static std::string get_directory(const std::string& filepath);
    static bool is_absolute_path(const std::string& filepath);
    static bool read_text_file(const std::string& filename, std::string& content);
    std::string preprocess_includes(const std::string& source, const std::string& current_file, const std::string& current_dir, const std::vector<std::string>& extra_include_dirs, std::set<std::string>& visited);
    template <typename... Args>
    void add_error(size_t line, size_t col, std::format_string<Args...> format, Args&&... args)
    {
        ErrorMessage e;
        e.expanded_line = line;
        e.line = line;
        e.col = col;
        const auto& source_line_infos = active_source_line_infos();
        if (line > 0 && line <= source_line_infos.size())
        {
            const auto& source_line = source_line_infos[line - 1];
            e.filename = source_line.filename;
            e.line = source_line.line;
            e.source_text = source_line.text;
            e.has_source_text = true;
        }
        e.message = std::format(format, std::forward<Args>(args)...);
        record_error(std::move(e));
    }

    template <typename... Args>
    void add_error(const std::string& filename, size_t line, size_t col, std::format_string<Args...> format, Args&&... args)
    {
        ErrorMessage e;
        e.filename = filename;
        e.line = line;
        e.col = col;
        e.message = std::format(format, std::forward<Args>(args)...);
        record_error(std::move(e));
    }
    template <typename... Args>
    void add_error(CalUnit& c, std::format_string<Args...> format, Args&&... args)
    {
        add_error(c.line, c.col, format, std::forward<Args>(args)...);
    }

    //四则运算准许用户增加自定义功能。数值运算优先使用内建类型规则。
    Object add(const Object& o1, const Object& o2);
    Object sub(const Object& o1, const Object& o2);
    Object mul(const Object& o1, const Object& o2);
    Object div(const Object& o1, const Object& o2);
    Object mod(const Object& o1, const Object& o2);
    Object less(const Object& o1, const Object& o2);
    Object more(const Object& o1, const Object& o2);
    Object less_equal(const Object& o1, const Object& o2);
    Object more_equal(const Object& o1, const Object& o2);
    Object equal(const Object& o1, const Object& o2);
    Object not_equal(const Object& o1, const Object& o2);
    Object bit_and(const Object& o1, const Object& o2);
    Object bit_or(const Object& o1, const Object& o2);
    Object bit_xor(const Object& o1, const Object& o2);
    Object logic_and(const Object& o1, const Object& o2);
    Object logic_or(const Object& o1, const Object& o2);
    Object shift_left(const Object& o1, const Object& o2);
    Object shift_right(const Object& o1, const Object& o2);
};

}    // namespace cifa
