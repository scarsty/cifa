#pragma once
#include <any>
#include <array>
#include <concepts>
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
#include <type_traits>
#include <typeindex>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <variant>
#include <vector>

namespace cifa
{
struct CalUnit;
class Cifa;
class CifaBytecode;

struct Object
{
    friend CalUnit;
    friend Cifa;
    friend class CifaBytecode;

    using Storage = std::variant<std::monostate, std::int64_t, double, bool, std::any>;

    Object() {}

    Object(double v)
    {
        value = v;
    }

    Object(double v, const std::string& t)
    {
        value = v;
        type1 = t;
    }

    Object(float v)
    {
        value = static_cast<double>(v);
    }

    Object(float v, const std::string& t)
    {
        value = static_cast<double>(v);
        type1 = t;
    }

    Object(const std::string& str)
    {
        value = std::any(str);
    }

    Object(const std::string& str, const std::string& t)
    {
        value = std::any(str);
        type1 = t;
    }

    template <typename T>
        requires ((std::integral<std::decay_t<T>> || std::floating_point<std::decay_t<T>>)
            && !std::same_as<std::decay_t<T>, double>
            && !std::same_as<std::decay_t<T>, float>
            && !std::same_as<std::decay_t<T>, int>
            && !std::same_as<std::decay_t<T>, bool>
            && !std::same_as<std::decay_t<T>, char>)
    Object(T v)
    {
        if constexpr (std::is_integral_v<std::decay_t<T>>)
        {
            if constexpr (std::is_unsigned_v<std::decay_t<T>>)
            {
                if (v > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max()))
                {
                    report_runtime_error("integer value out of range", this);
                    return;
                }
            }
            value = static_cast<std::int64_t>(v);
        }
        else
        {
            value = static_cast<double>(v);
        }
    }

    template <typename T>
        requires (!std::same_as<std::decay_t<T>, Object>
            && !std::integral<std::decay_t<T>> && !std::floating_point<std::decay_t<T>>)
    Object(const T& v)
    {
        value = std::any(v);
    }

    Object(int v)
    {
        value = static_cast<std::int64_t>(v);
    }

    Object(bool v)
    {
        value = v;
    }

    Object(char v)
    {
        value = static_cast<std::int64_t>(v);
    }

    operator bool() const { return toDouble() != 0; }

    operator int() const { return toInt(); }

    operator double() const { return toDouble(); }

    operator std::string() const { return toString(); }

    bool toBool() const
    {
        if (const auto* boolean = std::get_if<bool>(&value))
        {
            return *boolean;
        }
        if (const auto* integer = std::get_if<std::int64_t>(&value))
        {
            return *integer != 0;
        }
        if (const auto* floating = std::get_if<double>(&value))
        {
            return *floating != 0.0;
        }
        report_conversion_error("bool");
        return false;
    }

    int toInt() const
    {
        const auto number = toInt64();
        return static_cast<int>(number);
    }

    std::int64_t toInt64() const
    {
        if (const auto* integer = std::get_if<std::int64_t>(&value)) { return *integer; }
        if (const auto* boolean = std::get_if<bool>(&value)) { return *boolean ? 1 : 0; }
        if (const auto* floating = std::get_if<double>(&value)) { return static_cast<std::int64_t>(*floating); }
        report_conversion_error("int");
        return 0;
    }

    float toFloat() const
    {
        return static_cast<float>(toDouble());
    }

    double toDouble() const
    {
        if (const auto* floating = std::get_if<double>(&value))
        {
            return *floating;
        }
        if (const auto* integer = std::get_if<std::int64_t>(&value))
        {
            return static_cast<double>(*integer);
        }
        if (const auto* boolean = std::get_if<bool>(&value))
        {
            return *boolean ? 1.0 : 0.0;
        }
        report_conversion_error("double");
        return NAN;
    }

    std::string toString() const
    {
        if (const auto* object = std::get_if<std::any>(&value); object != nullptr && object->type() == typeid(std::string))
        {
            return std::any_cast<std::string>(*object);
        }
        report_conversion_error("string");
        return "";
    }

    //复制，不会改变原来的值
    template <typename T>
    T to() const
    {
        if constexpr (std::same_as<T, std::int64_t>)
        {
            if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
        }
        else if constexpr (std::same_as<T, double>)
        {
            if (const auto* floating = std::get_if<double>(&value)) return *floating;
        }
        else if constexpr (std::same_as<T, bool>)
        {
            if (const auto* boolean = std::get_if<bool>(&value)) return *boolean;
        }
        if (const auto* object = std::get_if<std::any>(&value); object != nullptr && object->type() == typeid(T))
        {
            return std::any_cast<T>(*object);
        }
        report_conversion_error(typeid(T).name());
        return T();
    }

    //const与非const版本，按需使用
    //如果转换失败，后续使用时也不会正常，因此应谨慎使用，或者在isType()判断后使用
    template <typename T>
    const T& ref() const
    {
        if constexpr (std::same_as<T, std::int64_t>)
        {
            if (const auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
        }
        else if constexpr (std::same_as<T, double>)
        {
            if (const auto* floating = std::get_if<double>(&value)) return *floating;
        }
        else if constexpr (std::same_as<T, bool>)
        {
            if (const auto* boolean = std::get_if<bool>(&value)) return *boolean;
        }
        if (const auto* object = std::get_if<std::any>(&value); object != nullptr && object->type() == typeid(T))
        {
            return std::any_cast<const T&>(*object);
        }
        report_conversion_error(typeid(T).name());
        return empty_reference<T>();
    }

    template <typename T>
    T& ref()
    {
        if constexpr (std::same_as<T, std::int64_t>)
        {
            if (auto* integer = std::get_if<std::int64_t>(&value)) return *integer;
        }
        else if constexpr (std::same_as<T, double>)
        {
            if (auto* floating = std::get_if<double>(&value)) return *floating;
        }
        else if constexpr (std::same_as<T, bool>)
        {
            if (auto* boolean = std::get_if<bool>(&value)) return *boolean;
        }
        if (auto* object = std::get_if<std::any>(&value); object != nullptr && object->type() == typeid(T))
        {
            return std::any_cast<T&>(*object);
        }
        report_conversion_error(typeid(T).name());
        return empty_reference<T>();
    }

    template <typename T>
    bool isType() const
    {
        if constexpr (std::same_as<T, std::int64_t> || std::same_as<T, double> || std::same_as<T, bool>)
            return std::holds_alternative<T>(value);
        else if (const auto* object = std::get_if<std::any>(&value)) return object->type() == typeid(T);
        else return false;
    }

    bool isNumber() const
    {
        return isInteger() || isType<double>();
    }

    bool isInteger() const
    {
        return isType<std::int64_t>() || isType<bool>();
    }

    bool isEffectNumber() const { return isNumber() && !std::isnan(toDouble()) && !std::isinf(toDouble()); }

    bool hasValue() const { return !std::holds_alternative<std::monostate>(value); }

    const std::string& getSpecialType() const { return type1; }

    const std::string& getDeclaredTypeName() const { return declared_type_name; }

    bool isTyped() const { return !declared_type_name.empty(); }

    std::type_info const& getType() const
    {
        if (std::holds_alternative<std::int64_t>(value)) return typeid(std::int64_t);
        if (std::holds_alternative<double>(value)) return typeid(double);
        if (std::holds_alternative<bool>(value)) return typeid(bool);
        if (const auto* object = std::get_if<std::any>(&value)) return object->type();
        return typeid(void);
    }

private:
    struct NoValue
    {
        std::string function_name;
        std::string call_frame;
    };

    static Object make_no_value(const std::string& function_name, std::string call_frame)
    {
        Object result;
        result.value = std::any(NoValue{ function_name, std::move(call_frame) });
        result.type1 = "NoValue";
        return result;
    }

    bool report_no_value() const
    {
        if (type1 != "NoValue")
        {
            return false;
        }
        const auto* object = std::get_if<std::any>(&value);
        const auto* no_value = object == nullptr ? nullptr : std::any_cast<NoValue>(object);
        const std::string function_name = no_value == nullptr ? "<unknown>" : no_value->function_name;
        report_runtime_error("function '" + function_name + "' has no return value", this);
        return true;
    }

    void report_conversion_error(const std::string& target_type) const
    {
        if (report_no_value()) { return; }
        const std::string object_name = name.empty() ? "<temporary>" : name;
        const std::string source_type = hasValue() ? getType().name() : "<empty>";
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

    Storage value;
    std::type_index bound_type = typeid(void);
    std::string declared_type_name;
    std::string element_type_name;
    std::string type1;        //特别的值类型，例如 Error、NoValue
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

    bool can_cal() const
    {
        return type == CalUnitType::Constant || type == CalUnitType::String || type == CalUnitType::Parameter
            || type == CalUnitType::Function || type == CalUnitType::Cast
            || type == CalUnitType::Operator && v.size() > 0;
    }

    bool is_statement() const
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

class Cifa
{
    friend class CifaBytecode;
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
            const auto number = o.toInt64();
            if (!std::in_range<T>(number))
            {
                o.report_conversion_error(typeid(T).name());
                return T{};
            }
            return static_cast<T>(number);
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
    struct RegisteredType
    {
        std::type_index identity;
        std::function<Object(const Object&)> convert;
    };
    std::unordered_map<std::string, RegisteredType> registered_types;
    std::unordered_map<std::type_index, std::string> type_names;
    //内置的运算符表示列表，用户可扩展运算符时会用到，注意这些运算符在语法分析阶段会被转换为对应的符号（如and转换为&&），因此用户扩展时也应使用符号形式的运算符
    inline static const std::map<std::string, std::string> op_representations = { { "and", "&&" }, { "and_eq", "&=" }, { "bitand", "&" }, { "bitor", "|" }, { "compl", "~" }, { "not", "!" }, { "not_eq", "!=" }, { "or", "||" }, { "or_eq", "|=" }, { "xor", "^" }, { "xor_eq", "^=" }, { "<%", "{" }, { "%>", "}" }, { "<:", "[" }, { ":>", "]" }, { "%:", "#" }, { "%:%:", "##" } };
    //内置的数组/map方法列表
    inline static const std::set<std::string> builtin_methods = { "push_back", "pop_back", "resize", "reserve", "insert", "erase", "clear", "contains", "keys" };

    std::unordered_map<std::string, func_type> functions;     //在宿主程序中注册的函数
    size_t function_version = 0;
    std::unordered_map<std::string, size_t> function_generations;
    std::unordered_map<std::string, size_t> builtin_function_generations;
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

    enum class ControlFlow
    {
        None,
        Break,
        Continue,
        Goto
    };

    struct RuntimeFrame
    {
        const CalUnit* node = nullptr;
        const std::vector<SourceLineInfo>* source_lines = nullptr;
        std::string function_name;
    };

    struct ExecutionContext
    {
        CalUnit root;
        std::unordered_map<std::string, FunctionOverloads> functions;
        std::unordered_map<std::string, std::vector<StructField>> struct_defs;
        std::vector<SourceLineInfo> source_line_infos;
        std::vector<RuntimeFrame> runtime_call_stack;
        std::vector<std::string> runtime_error_call_stack;
        const std::vector<CalUnit>* active_function_arguments = nullptr;
        const ObjectVector* active_function_values = nullptr;
        std::string runtime_error_message;
        std::vector<ReturnState> return_states;
        ErrorSet errors;
        ControlFlow control_flow = ControlFlow::None;
        std::string goto_label;
        bool exit_requested = false;
    };

    std::deque<ExecutionContext> execution_contexts;
    CalUnit compilation_root;
    std::unordered_map<std::string, FunctionOverloads> compilation_functions;
    std::unordered_map<std::string, std::vector<StructField>> compilation_struct_defs;
    std::vector<SourceLineInfo> compilation_source_line_infos;
    const std::unordered_map<std::string, FunctionOverloads>* compile_visible_functions = nullptr;
    const std::unordered_map<std::string, std::vector<StructField>>* compile_visible_struct_defs = nullptr;
    const std::unordered_set<std::string>* compile_visible_host_functions = nullptr;
    bool compiling = false;
    bool compiled = false;
    bool compile_failed = false;
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
    std::string registered_type_name(const Object& object) const;

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
        ++function_version;
        ++function_generations[name];
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

    Object run_script(std::string script);
    Object run_file(const std::string& filename);

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
    template <typename T>
    bool register_type(const std::string& name)
    {
        if (!is_valid_key(name) || registered_types.contains(name) || functions.contains(name)
            || functions2.contains(name) || global_variables.contains(name) || struct_defs.contains(name))
        {
            set_runtime_error("invalid or duplicate type name '" + name + "'");
            return false;
        }
        using Stored = std::conditional_t<std::is_same_v<T, bool>, bool,
            std::conditional_t<std::is_integral_v<T>, std::int64_t,
            std::conditional_t<std::is_floating_point_v<T>, double, T>>>;
        type_names.try_emplace(typeid(Stored), name);
        registered_types.emplace(name, RegisteredType{typeid(Stored), [](const Object& source) -> Object
            {
                if (source.isType<Stored>()) { return source; }
                if constexpr (std::is_same_v<T, bool>) { return Object(source.toBool()); }
                else if constexpr (std::is_integral_v<T>) { return Object(source.toInt64()); }
                else if constexpr (std::is_floating_point_v<T>) { return Object(static_cast<T>(source.toDouble())); }
                else { return Object(source.to<T>()); }
            }});
        return true;
    }

    using OperatorCallbacks = std::vector<std::function<Object(const Object&, const Object&)>>;
    OperatorCallbacks user_add, user_sub, user_mul, user_div, user_mod,
        user_less, user_more, user_less_equal, user_more_equal, user_equal, user_not_equal,
        user_bit_and, user_bit_or, user_bit_xor, user_logic_and, user_logic_or,
        user_shift_left, user_shift_right;

private:
    static const std::unordered_set<std::string>& keyword_tokens();
    static const std::unordered_set<std::string>& operator_tokens();
    static const std::vector<std::unordered_set<std::string>>& operator_precedence_token_groups();

    Object eval_scoped(CalUnit& c, ScopeStack& scopes);
    bool eval_condition(CalUnit& c, ScopeStack& scopes);
    Object run_function(const CalUnit& call_site, std::vector<CalUnit>& vc, ScopeStack& scopes);
    void run_compilation(const std::function<void()>& action);
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

private:
    bool compile_script_internal(std::string script);
    bool compile_file_internal(const std::string& filename);
    static bool parse_number_literal(const std::string& text, Object& value);
    Object run_compilation_result();
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
    void set_control_flow(ControlFlow flow, std::string label = {});
    bool consume_control_flow(ControlFlow flow);
    std::string format_runtime_frame(const CalUnit& c) const;
    static std::string format_runtime_frame(const CalUnit& c, const std::vector<SourceLineInfo>& source_line_infos);
    static std::string format_runtime_frame(const RuntimeFrame& frame);
    void set_runtime_error(const std::string& message, const Object* source = nullptr, const CalUnit* location = nullptr);
    void clear_runtime_error();
    bool should_stop_execution() const { return has_runtime_error() || is_exit_requested(); }
    std::string format_runtime_error() const;
    void print_runtime_error() const;
    Object make_error_result() const;
    void compile_pipeline(std::string str);

    void check_cal_unit(CalUnit& c, CalUnit* father, std::unordered_map<std::string, Object>& p,
        size_t loop_depth = 0, size_t switch_depth = 0);
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
    template <char Symbol, bool Extended = false>
    Object evaluate_binary(const Object& o1, const Object& o2, OperatorCallbacks& callbacks);

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
