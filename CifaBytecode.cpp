#include "CifaBytecode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace cifa {

enum Op : unsigned {
    MOVE, LOADI, LOADF, LOADK, LOADKX, LOADFALSE, LFALSESKIP, LOADTRUE, LOADNIL,
    GETUPVAL, SETUPVAL, GETTABUP, GETTABLE, GETI, GETFIELD,
    SETTABUP, SETTABLE, SETI, SETFIELD, NEWTABLE, SELF,
    ADDI, ADDK, SUBK, MULK, MODK, POWK, DIVK, IDIVK,
    BANDK, BORK, BXORK, SHRI, SHLI,
    ADD, SUB, MUL, MOD, POW, DIV, IDIV, BAND, BOR, BXOR, SHL, SHR,
    MMBIN, MMBINI, MMBINK, UNM, BNOT, NOT, LEN, CONCAT, CLOSE, TBC, JMP,
    EQ, LT, LE, EQK, EQI, LTI, LEI, GTI, GEI, TEST, TESTSET,
    CALL, TAILCALL, RETURN, RETURN0, RETURN1, FORLOOP, FORPREP,
    TFORPREP, TFORCALL, TFORLOOP, SETLIST, CLOSURE, VARARG, VARARGPREP, EXTRAARG
};

constexpr unsigned POS_A = 7;
constexpr unsigned POS_k = 15;
constexpr unsigned POS_B = 16;
constexpr unsigned POS_C = 24;
constexpr unsigned POS_Bx = POS_k;
constexpr unsigned POS_Ax = POS_A;
constexpr int OFFSET_sJ = (1 << 24) - 1;
constexpr unsigned LUA_VNUMFLT = 3u | (1u << 4);
constexpr unsigned LUA_VNUMINT = 3u;
constexpr unsigned LUA_VSHRSTR = 4u;
constexpr unsigned LUA_VTRUE = 1u | (1u << 4);
constexpr unsigned LUA_VFALSE = 1u;

static std::uint32_t abc(unsigned op, unsigned a, unsigned b, unsigned c, unsigned k = 0)
{
    return op | (a << POS_A) | (k << POS_k) | (b << POS_B) | (c << POS_C);
}
static std::uint32_t abx(unsigned op, unsigned a, unsigned bx)
{
    return op | (a << POS_A) | (bx << POS_Bx);
}
static std::uint32_t asj(unsigned op, int jump)
{
    return op | (static_cast<unsigned>(jump + OFFSET_sJ) << POS_Ax);
}
static std::uint32_t ax(unsigned op, unsigned value) { return op | (value << POS_Ax); }

/* Lua TString stores its bytes directly after the object header. */
struct CifaLuaString {
    unsigned char marked = 0;
    unsigned char tt = LUA_VSHRSTR;
    unsigned char extra = 0;
    unsigned char shrlen = 0;
    std::uint32_t hash = 0;
    std::size_t length = 0;
    CifaLuaString* hnext = nullptr;
    char contents[1];
};

static const char* string_data(const CifaLuaString* value) { return value->contents; }
static std::size_t string_size(const CifaLuaString* value) { return value->length; }
static std::string string_copy(const CifaLuaString* value) { return {string_data(value), string_size(value)}; }
static CifaLuaString* new_lua_string(const std::string& text)
{
    const std::size_t bytes = offsetof(CifaLuaString, contents) + text.size() + 1;
    auto* value = static_cast<CifaLuaString*>(::operator new(bytes));
    value->marked = 0;
    value->tt = LUA_VSHRSTR;
    value->extra = 0;
    value->shrlen = static_cast<unsigned char>(std::min<std::size_t>(text.size(), 255));
    value->length = text.size();
    std::memcpy(value->contents, text.data(), text.size());
    value->contents[text.size()] = '\0';
    return value;
}
static void free_lua_string(CifaLuaString* value) { ::operator delete(value); }

struct Constant {
    enum Kind { Integer, Number, String, Boolean } kind;
    std::int64_t integer = 0;
    double number = 0;
    bool boolean = false;
    std::string string;
    mutable const CifaLuaString* runtime_string = nullptr;
};

struct UpvalueDesc {
    bool instack = false;
    unsigned index = 0;
};

static std::string script_function_key(const std::string& name, std::size_t arity)
{
    return name + '\x1f' + std::to_string(arity);
}

struct Proto {
    std::string source;
    std::string debug_name;
    unsigned linedefined = 0;
    unsigned lastline = 0;
    unsigned params = 0;
    unsigned vararg = 0;
    unsigned maxstack = 2;
    std::vector<std::string> parameter_types;
    std::string return_type;
    std::vector<std::uint32_t> code;
    std::vector<std::pair<std::size_t, std::string>> call_frames;
    std::vector<std::pair<std::size_t, std::string>> call_errors;
    std::vector<std::pair<std::size_t, std::string>> value_errors;
    std::vector<std::size_t> create_table_reads;
    std::vector<Constant> constants;
    std::vector<Proto> children;
    std::vector<UpvalueDesc> upvalues;
};

class CifaBytecode::FunctionCompiler {
public:
    FunctionCompiler(const CifaBytecode& owner, Proto& proto, const std::unordered_map<std::string, FunctionOverloads>* functions,
                bool root = false, unsigned first_register = 0,
                const std::unordered_map<std::string, unsigned>* function_upvalues = nullptr)
                : owner_(owner), proto_(proto), functions_(functions), root_(root),
                    function_upvalues_(function_upvalues), next_register_(first_register) {}

    bool compile_body(const CalUnit& body, const std::vector<Function2::Argument>& args, std::string& error)
    {
        for (const auto& arg : args) {
            local(arg.name);
            numeric_kinds_.back()[arg.name] = type_kind(arg.type_name);
            constexpr std::string_view vector_prefix = "vector<";
            if (arg.type_name.starts_with(vector_prefix) && arg.type_name.ends_with('>'))
                array_element_types_.back()[arg.name] = arg.type_name.substr(vector_prefix.size(), arg.type_name.size() - vector_prefix.size() - 1);
        }
        if (!emit_block(body, error)) return false;
        for (const auto& [name, jump] : goto_jumps_) {
            const auto label = labels_.find(name);
            if (label == labels_.end()) {
                error = "undefined goto label: " + name;
                return false;
            }
            patch_jump(jump, label->second);
        }
        proto_.code.push_back(abc(RETURN0, 0, 0, 0));
        proto_.maxstack = std::max<unsigned>(2, next_register_ + 1);
        if (proto_.maxstack > 255)
        {
            error = "Lua register limit exceeded: " + std::to_string(proto_.maxstack);
            return false;
        }
        return true;
    }

private:
    const CifaBytecode& owner_;
    Proto& proto_;
    const std::unordered_map<std::string, FunctionOverloads>* functions_;
    bool root_ = false;
    const std::unordered_map<std::string, unsigned>* function_upvalues_ = nullptr;
    std::vector<std::unordered_map<std::string, unsigned>> locals_{1};
    enum class NumericKind { Unknown, Integer, Float };
    std::vector<std::unordered_map<std::string, NumericKind>> numeric_kinds_{1};
    std::vector<std::unordered_set<std::string>> string_locals_{1};
    std::vector<std::unordered_map<std::string, std::string>> declared_types_{1};
    std::vector<std::unordered_map<std::string, std::string>> array_element_types_{1};
    std::vector<std::unordered_map<std::string, std::unordered_map<std::string, std::string>>> struct_field_types_{1};
    std::vector<std::unordered_set<std::string>> uninitialized_locals_{1};
    std::vector<std::vector<unsigned>> loop_breaks_;
    std::vector<std::vector<unsigned>> loop_continues_;
    std::unordered_map<std::string, unsigned> labels_;
    std::vector<std::pair<std::string, unsigned>> goto_jumps_;
    std::vector<std::pair<std::string, unsigned>> one_based_indices_;
    std::vector<std::pair<std::string, unsigned>> append_indices_;
    std::unordered_set<std::string> root_globals_;
    unsigned next_register_ = 0;
    unsigned block_depth_ = 0;

    std::optional<unsigned> one_based_index(const CalUnit& node) const
    {
        if (node.type != CalUnitType::Parameter || !node.v.empty()) return std::nullopt;
        for (auto index = one_based_indices_.rbegin(); index != one_based_indices_.rend(); ++index)
            if (index->first == node.str) return index->second;
        return std::nullopt;
    }

    std::optional<unsigned> append_index(const CalUnit& node) const
    {
        if (node.type != CalUnitType::Parameter || !node.v.empty()) return std::nullopt;
        for (auto index = append_indices_.rbegin(); index != append_indices_.rend(); ++index)
            if (index->first == node.str) return index->second;
        return std::nullopt;
    }

    unsigned local(const std::string& name)
    {
        if (const auto existing = find_local(name)) return *existing;
        return declare_local(name);
    }

    unsigned declare_local(const std::string& name)
    {
        if (const auto it = locals_.back().find(name); it != locals_.back().end()) return it->second;
        const unsigned result = next_register_++;
        locals_.back().emplace(name, result);
        return result;
    }

    std::optional<unsigned> find_local(const std::string& name) const
    {
        for (auto scope = locals_.rbegin(); scope != locals_.rend(); ++scope) {
            if (const auto it = scope->find(name); it != scope->end()) return it->second;
        }
        return std::nullopt;
    }

    bool has_current_local(const std::string& name) const
    {
        return locals_.back().contains(name);
    }

    bool is_string_local(const std::string& name) const
    {
        for (auto scope = string_locals_.rbegin(); scope != string_locals_.rend(); ++scope)
            if (scope->contains(name)) return true;
        return false;
    }

    std::string declared_type(const std::string& name) const
    {
        for (auto scope = declared_types_.rbegin(); scope != declared_types_.rend(); ++scope)
            if (const auto found = scope->find(name); found != scope->end()) return found->second;
        return {};
    }

    std::string array_element_type(const std::string& name) const
    {
        for (auto scope = array_element_types_.rbegin(); scope != array_element_types_.rend(); ++scope)
            if (const auto found = scope->find(name); found != scope->end()) return found->second;
        return {};
    }

    std::string struct_field_type(const std::string& object, const std::string& field) const
    {
        for (auto scope = struct_field_types_.rbegin(); scope != struct_field_types_.rend(); ++scope) {
            if (const auto object_types = scope->find(object); object_types != scope->end()) {
                if (const auto type = object_types->second.find(field); type != object_types->second.end()) return type->second;
            }
        }
        return {};
    }

    bool is_uninitialized_local(const std::string& name) const
    {
        for (auto scope = uninitialized_locals_.rbegin(); scope != uninitialized_locals_.rend(); ++scope)
            if (scope->contains(name)) return true;
        return false;
    }

    const std::vector<StructField>* struct_definition(const std::string& name) const
    {
        const auto found = owner_.compilation_struct_defs.find(name);
        return found == owner_.compilation_struct_defs.end() ? nullptr : &found->second;
    }

    void emit_uninitialized_check(const CalUnit& node, unsigned value)
    {
        if (!is_uninitialized_local(node.str)) return;
        const unsigned checked = next_register_++;
        proto_.code.push_back(abc(NOT, checked, value, 0));
        proto_.value_errors.emplace_back(proto_.code.size() - 1,
            "variable '" + node.str + "' has not been initialized\n" + owner_.compiled_source_frame(node));
    }

    static NumericKind type_kind(const std::string& type_name)
    {
        if (type_name == "int") return NumericKind::Integer;
        if (type_name == "double" || type_name == "float") return NumericKind::Float;
        return NumericKind::Unknown;
    }

    NumericKind expression_kind(const CalUnit& node) const
    {
        if (node.type == CalUnitType::Constant)
            return node.str.find_first_of(".eE") == std::string::npos ? NumericKind::Integer : NumericKind::Float;
        if (node.type == CalUnitType::Parameter) {
            std::optional<NumericKind> kind;
            for (auto scope = numeric_kinds_.rbegin(); scope != numeric_kinds_.rend(); ++scope) {
                if (const auto it = scope->find(node.str); it != scope->end()) { kind = it->second; break; }
            }
            if (node.v.empty()) return kind.value_or(NumericKind::Unknown);
            if (node.v.size() == 1 && node.v[0].str == "[]") return type_kind(array_element_type(node.str));
            return NumericKind::Unknown;
        }
        if (node.type == CalUnitType::Cast && node.v.size() == 1)
            return type_kind(node.type_name);
        if (node.type == CalUnitType::Operator && node.str == "()" && node.v.size() == 1)
            return expression_kind(node.v[0]);
        if (node.type == CalUnitType::Operator && node.v.size() == 1) {
            if (node.str == "~") return NumericKind::Integer;
            if (node.str == "+" || node.str == "-") return expression_kind(node.v[0]);
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2) {
            const NumericKind left = expression_kind(node.v[0]);
            const NumericKind right = expression_kind(node.v[1]);
            if (left == NumericKind::Float || right == NumericKind::Float) return NumericKind::Float;
            if (left == NumericKind::Integer && right == NumericKind::Integer
                && (node.str == "+" || node.str == "-" || node.str == "*" || node.str == "%" || node.str == "/" || node.str == "//"
                    || node.str == "&" || node.str == "|" || node.str == "^" || node.str == "<<" || node.str == ">>"))
                return NumericKind::Integer;
        }
        if (node.type == CalUnitType::Function && node.str == "size") return NumericKind::Integer;
        return NumericKind::Unknown;
    }

    unsigned constant(const Constant& value)
    {
        for (unsigned i = 0; i < proto_.constants.size(); ++i) {
            const auto& old = proto_.constants[i];
            if (old.kind != value.kind) continue;
            if (value.kind == Constant::Integer && old.integer == value.integer) return i;
            if (value.kind == Constant::Number && old.number == value.number) return i;
            if (value.kind == Constant::String && old.string == value.string) return i;
            if (value.kind == Constant::Boolean && old.boolean == value.boolean) return i;
        }
        proto_.constants.push_back(value);
        return static_cast<unsigned>(proto_.constants.size() - 1);
    }

    bool number(const std::string& text, Constant& result) const
    {
        try {
            if (text.find_first_of(".eE") == std::string::npos) {
                result.kind = Constant::Integer;
                if (text.size() > 2 && text[0] == '0' && (text[1] == 'b' || text[1] == 'B')) {
                    result.integer = 0;
                    for (std::size_t index = 2; index < text.size(); ++index) {
                        if (text[index] != '0' && text[index] != '1') return false;
                        result.integer = (result.integer << 1) | (text[index] - '0');
                    }
                } else result.integer = std::stoll(text, nullptr, 0);
            } else {
                result.kind = Constant::Number;
                result.number = std::stod(text);
            }
            return true;
        } catch (...) { return false; }
    }

    unsigned emit_constant(const Constant& value)
    {
        const auto index = constant(value);
        if (value.kind == Constant::Integer && value.integer >= -(1 << 16) && value.integer <= (1 << 16)) {
            const unsigned reg = next_register_++;
            proto_.code.push_back(abx(LOADI, reg, static_cast<unsigned>(value.integer + ((1 << 17) - 1) / 2)));
            return reg;
        }
        const unsigned reg = next_register_++;
        proto_.code.push_back(abx(LOADK, reg, index));
        return reg;
    }

    unsigned emit_global_get(const std::string& name, std::string& error)
    {
        (void)error;
        const unsigned result = next_register_++;
        const unsigned key = constant(Constant{Constant::String, 0, 0, false, name});
        proto_.code.push_back(abc(GETTABUP, result, 0, key, 1));
        return result;
    }

    void emit_global_set(const std::string& name, unsigned value)
    {
        const unsigned key = constant(Constant{Constant::String, 0, 0, false, name});
        proto_.code.push_back(abc(SETTABUP, 0, key, value, 0));
    }

    unsigned emit_name(const std::string& name, std::string& error)
    {
        if (const auto slot = find_local(name)) return *slot;
        return emit_global_get(name, error);
    }

    unsigned emit_index(const CalUnit& node, std::string& error)
    {
        if (node.v.empty() || node.v[0].str != "[]") {
            error = "unsupported index shape";
            return 0;
        }
        auto emit_step = [&](unsigned table, const CalUnit& subscript) -> unsigned {
            if (subscript.type == CalUnitType::Constant) {
                Constant constant_index{};
                if (number(subscript.str, constant_index) && constant_index.kind == Constant::Integer
                    && constant_index.integer >= 0 && constant_index.integer < 255) {
                    const unsigned result = next_register_++;
                    proto_.code.push_back(abc(GETI, result, table, static_cast<unsigned>(constant_index.integer + 1)));
                    return result;
                }
            }
            if (const auto index = one_based_index(subscript)) {
                const unsigned result = next_register_++;
                proto_.code.push_back(abc(GETTABLE, result, table, *index));
                return result;
            }
            const unsigned source_index = emit_expression(subscript, error);
            if (subscript.type == CalUnitType::String
                || subscript.type == CalUnitType::Parameter && is_string_local(subscript.str)) {
                const unsigned result = next_register_++;
                proto_.code.push_back(abc(GETTABLE, result, table, source_index));
                return result;
            }
            const unsigned index = next_register_++;
            proto_.code.push_back(abc(ADDI, index, source_index, 128));
            const unsigned result = next_register_++;
            proto_.code.push_back(abc(GETTABLE, result, table, index));
            return result;
        };
        unsigned result = emit_name(node.str, error);
        for (const CalUnit& accessor : node.v) {
            if (accessor.str != "[]" || accessor.v.size() != 1) {
                error = "unsupported index shape";
                return 0;
            }
            result = emit_step(result, accessor.v[0]);
        }
        return result;
    }

    unsigned emit_condition(const CalUnit& node, std::string& error)
    {
        if (node.type == CalUnitType::Operator && node.str == "&&" && node.v.size() == 2) {
            const auto first = emit_condition(node.v[0], error);
            const auto second = emit_condition(node.v[1], error);
            const auto success_exit = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            const auto false_exit = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            patch_jump(first, false_exit);
            patch_jump(second, false_exit);
            patch_jump(success_exit, static_cast<unsigned>(proto_.code.size()));
            return false_exit;
        }
        if (node.type == CalUnitType::Operator && node.str == "||" && node.v.size() == 2) {
            const auto first = emit_condition(node.v[0], error);
            const auto success_exit = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            const auto second_start = static_cast<unsigned>(proto_.code.size());
            patch_jump(first, second_start);
            const auto second = emit_condition(node.v[1], error);
            patch_jump(success_exit, static_cast<unsigned>(proto_.code.size()));
            return second;
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2 && node.str != "." && node.str != "::") {
            auto left = emit_expression(node.v[0], error);
            auto right = emit_expression(node.v[1], error);
            unsigned op = 0;
            bool invert = false;
            if (node.str == "==") op = EQ;
            else if (node.str == "!=") { op = EQ; invert = true; }
            else if (node.str == "<") op = LT;
            else if (node.str == "<=") op = LE;
            else if (node.str == ">" || node.str == ">=") {
                op = node.str == ">" ? LT : LE;
            }
            else { error = "unsupported condition: " + node.str; return 0; }
            Constant right_constant{};
            const bool immediate = (node.str == "==" || node.str == "!=" || node.str == "<" || node.str == "<=")
                && node.v[1].type == CalUnitType::Constant && number(node.v[1].str, right_constant)
                && right_constant.kind == Constant::Integer && right_constant.integer >= -127 && right_constant.integer <= 128;
            if (node.str == ">" || node.str == ">=") std::swap(left, right);
            const auto stable_left = next_register_++;
            const auto stable_right = next_register_++;
            proto_.code.push_back(abc(MOVE, stable_left, left, 0));
            proto_.code.push_back(abc(MOVE, stable_right, right, 0));
            if (immediate && (node.str == "==" || node.str == "!=" || node.str == "<" || node.str == "<=")) {
                const unsigned immediate_op = op == EQ ? EQI : op == LT ? LTI : LEI;
                proto_.code.push_back(abc(immediate_op, stable_left, static_cast<unsigned>(right_constant.integer + 127), 0, invert ? 1u : 0u));
            }
            else
                proto_.code.push_back(abc(op, stable_left, stable_right, 0, invert ? 1u : 0u));
            const auto jump = proto_.code.size();
            proto_.code.push_back(asj(JMP, 0));
            return static_cast<unsigned>(jump);
        }
        const auto value = emit_expression(node, error);
        proto_.code.push_back(abc(TEST, value, 0, 0, 1));
        const auto jump = proto_.code.size();
        proto_.code.push_back(asj(JMP, 0));
        return static_cast<unsigned>(jump);
    }

    void patch_jump(unsigned jump, unsigned target)
    {
        proto_.code[jump] = asj(JMP, static_cast<int>(target) - static_cast<int>(jump) - 1);
    }

    unsigned emit_expression(const CalUnit& node, std::string& error, bool prefer_general_arithmetic = false,
        bool allow_uninitialized = false)
    {
        if (node.type == CalUnitType::Union && node.str == "{}") {
            std::vector<const CalUnit*> elements;
            const auto collect = [&](const CalUnit& item, const auto& self) -> void {
                if (item.type == CalUnitType::Operator && item.str == "," && item.v.size() == 2) {
                    self(item.v[0], self);
                    self(item.v[1], self);
                } else if (item.type != CalUnitType::None && item.type != CalUnitType::Split && item.str != ";") {
                    elements.push_back(&item);
                }
            };
            for (const CalUnit& item : node.v) collect(item, collect);
            const unsigned table = next_register_++;
            proto_.code.push_back(abc(NEWTABLE, table, 0, static_cast<unsigned>(elements.size())));
            proto_.code.push_back(ax(EXTRAARG, 0));
            for (std::size_t index = 0; index < elements.size(); ++index) {
                const unsigned value = emit_expression(*elements[index], error);
                proto_.code.push_back(abc(SETI, table, static_cast<unsigned>(index + 1), value));
            }
            return table;
        }
        if (node.type == CalUnitType::Constant) {
            Constant value{};
            if (!number(node.str, value)) { error = "unsupported numeric literal: " + node.str; return 0; }
            return emit_constant(value);
        }
        if (node.type == CalUnitType::String) {
            Constant value{}; value.kind = Constant::String; value.string = node.str;
            return emit_constant(value);
        }
        if (node.type == CalUnitType::Key && (node.str == "true" || node.str == "false")) {
            Constant value{}; value.kind = Constant::Boolean; value.boolean = node.str == "true";
            return emit_constant(value);
        }
        if (node.type == CalUnitType::Cast && node.v.size() == 1) {
            unsigned source = emit_expression(node.v[0], error, true);
            const NumericKind source_kind = expression_kind(node.v[0]);
            if (node.type_name == "int" || node.type_name == "double" || node.type_name == "float") {
                const unsigned checked = next_register_++;
                proto_.code.push_back(abc(NOT, checked, source, 0));
                proto_.value_errors.emplace_back(proto_.code.size() - 1,
                    "cannot convert value to '" + (node.type_name == "float" ? std::string("double") : node.type_name) + "'");
            }
            if (node.type_name == "int" && source_kind == NumericKind::Float) {
                const unsigned result = next_register_++;
                const unsigned one = constant(Constant{Constant::Integer, 1, 0, false, {}});
                proto_.code.push_back(abc(IDIVK, result, source, one));
                return result;
            }
            if ((node.type_name == "double" || node.type_name == "float") && source_kind == NumericKind::Integer) {
                const unsigned result = next_register_++;
                const unsigned one = constant(Constant{Constant::Number, 0, 1.0, false, {}});
                proto_.code.push_back(abc(DIVK, result, source, one));
                return result;
            }
            if (node.type_name == "bool") {
                const unsigned negated = next_register_++;
                const unsigned result = next_register_++;
                proto_.code.push_back(abc(NOT, negated, source, 0));
                proto_.code.push_back(abc(NOT, result, negated, 0));
                return result;
            }
            return source;
        }
        if (node.type == CalUnitType::Parameter) {
            if (!node.v.empty() && node.v[0].str == "[]") {
                return emit_index(node, error);
            }
            const unsigned value = emit_name(node.str, error);
            if (!allow_uninitialized) emit_uninitialized_check(node, value);
            return value;
        }
        if (node.type == CalUnitType::Operator && node.str == "()" && node.v.size() == 1)
            return emit_expression(node.v[0], error);
        if (node.type == CalUnitType::Operator && node.str == "::" && node.v.size() == 2
            && node.v[1].type == CalUnitType::Parameter) {
            const unsigned table = emit_expression(node.v[0], error);
            const unsigned result = next_register_++;
            const unsigned key = constant(Constant{Constant::String, 0, 0, false, node.v[1].str});
            proto_.code.push_back(abc(GETFIELD, result, table, key));
            return result;
        }
        if (node.type == CalUnitType::Operator && node.str == "." && node.v.size() == 2
            && node.v[1].type == CalUnitType::Parameter) {
            const unsigned table = emit_expression(node.v[0], error);
            const unsigned result = next_register_++;
            const unsigned key = constant(Constant{Constant::String, 0, 0, false, node.v[1].str});
            proto_.code.push_back(abc(GETFIELD, result, table, key));
            return result;
        }
        if (node.type == CalUnitType::Operator && node.str == "," && node.v.size() == 2) {
            emit_expression(node.v[0], error);
            return emit_expression(node.v[1], error);
        }
        if (node.type == CalUnitType::Operator && node.str == "?" && node.v.size() == 2
            && node.v[1].v.size() == 2) {
            const unsigned result = next_register_++;
            const unsigned false_jump = emit_condition(node.v[0], error);
            const unsigned true_value = emit_expression(node.v[1].v[0], error);
            if (true_value != result) proto_.code.push_back(abc(MOVE, result, true_value, 0));
            const unsigned end_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
            const unsigned false_value = emit_expression(node.v[1].v[1], error);
            if (false_value != result) proto_.code.push_back(abc(MOVE, result, false_value, 0));
            patch_jump(end_jump, static_cast<unsigned>(proto_.code.size()));
            return result;
        }
        if (node.type == CalUnitType::Operator && node.str == "&&" && node.v.size() == 2) {
            const unsigned result = next_register_++;
            const unsigned false_jump = emit_condition(node, error);
            proto_.code.push_back(abx(LOADI, result, (1 << 16)));
            const unsigned end_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
            proto_.code.push_back(abx(LOADI, result, (1 << 16) - 1));
            patch_jump(end_jump, static_cast<unsigned>(proto_.code.size()));
            return result;
        }
        if (node.type == CalUnitType::Operator && node.str == "||" && node.v.size() == 2) {
            const unsigned result = next_register_++;
            const unsigned left = emit_expression(node.v[0], error);
            proto_.code.push_back(abc(TEST, left, 0, 0, 1));
            const unsigned rhs_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            proto_.code.push_back(abx(LOADI, result, (1 << 16)));
            const unsigned left_end_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            patch_jump(rhs_jump, static_cast<unsigned>(proto_.code.size()));
            const unsigned right = emit_expression(node.v[1], error);
            proto_.code.push_back(abc(TEST, right, 0, 0, 1));
            const unsigned false_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            proto_.code.push_back(abx(LOADI, result, (1 << 16)));
            const unsigned right_end_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
            proto_.code.push_back(abx(LOADI, result, (1 << 16) - 1));
            const unsigned end = static_cast<unsigned>(proto_.code.size());
            patch_jump(left_end_jump, end);
            patch_jump(right_end_jump, end);
            return result;
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 1
            && (node.str == "+" || node.str == "-" || node.str == "~" || node.str == "!")) {
            const unsigned value = emit_expression(node.v[0], error);
            if (node.str == "+") return value;
            const unsigned result = next_register_++;
            const unsigned operation = node.str == "-" ? UNM : node.str == "~" ? BNOT : NOT;
            proto_.code.push_back(abc(operation, result, value, 0));
            return result;
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2
            && (node.str == "==" || node.str == "!=" || node.str == "<" || node.str == "<="
                || node.str == ">" || node.str == ">=")) {
            const unsigned result = next_register_++;
            const unsigned false_jump = emit_condition(node, error);
            proto_.code.push_back(abx(LOADI, result, 1u << 16));
            const unsigned end_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
            proto_.code.push_back(abx(LOADI, result, (1u << 16) - 1));
            patch_jump(end_jump, static_cast<unsigned>(proto_.code.size()));
            return result;
        }
        if (node.type == CalUnitType::Operator && (node.str == "++" || node.str == "--" || node.str == "()++" || node.str == "()--") && node.v.size() == 1
            && node.v[0].type == CalUnitType::Parameter) {
            const auto target = emit_name(node.v[0].str, error);
            const bool postfix = node.str == "()++" || node.str == "()--";
            const unsigned result = postfix ? next_register_++ : target;
            if (postfix) proto_.code.push_back(abc(MOVE, result, target, 0));
            const auto encoded_delta = (node.str == "++" || node.str == "()++") ? 128u : 126u;
            proto_.code.push_back(abc(ADDI, target, target, encoded_delta));
            if (root_ && !find_local(node.v[0].str)) emit_global_set(node.v[0].str, target);
            return result;
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2 && node.str != "." && node.str != "::") {
            static const std::unordered_map<std::string, unsigned> operations = {
                {"+", ADD}, {"-", SUB}, {"*", MUL}, {"%", MOD}, {"//", IDIV},
                {"&", BAND}, {"|", BOR}, {"^", BXOR}, {"<<", SHL}, {">>", SHR}
            };
            unsigned operation = 0;
            if (node.str == "/") {
                operation = expression_kind(node.v[0]) == NumericKind::Integer
                    && expression_kind(node.v[1]) == NumericKind::Integer ? IDIV : DIV;
            } else if (const auto it = operations.find(node.str); it != operations.end()) {
                operation = it->second;
            } else {
                error = "unsupported operator: " + node.str;
                if (node.str == "." && node.v.size() == 2) {
                    error += " left=" + std::to_string(static_cast<int>(node.v[0].type))
                        + ":" + node.v[0].str + " right=" + std::to_string(static_cast<int>(node.v[1].type))
                        + ":" + node.v[1].str;
                }
                return 0;
            }
            const auto left_value = emit_expression(node.v[0], error);
            const auto left = next_register_++;
            proto_.code.push_back(abc(MOVE, left, left_value, 0));
            const auto contains_string = [&](const CalUnit& item, const auto& self) -> bool {
                if (item.type == CalUnitType::String || item.type == CalUnitType::Parameter && is_string_local(item.str)) return true;
                if (item.type == CalUnitType::Function) return item.str == "to_string";
                if (item.type == CalUnitType::Operator && item.str == "." && item.v.size() == 2
                    && item.v[1].type == CalUnitType::Function) return self(item.v[0], self);
                for (const auto& child : item.v) if (self(child, self)) return true;
                return false;
            };
            if (node.str == "+" && (contains_string(node.v[0], contains_string) || contains_string(node.v[1], contains_string))) {
                const auto right = emit_expression(node.v[1], error);
                const auto first = next_register_++;
                const auto second = next_register_++;
                proto_.code.push_back(abc(MOVE, first, left, 0));
                proto_.code.push_back(abc(MOVE, second, right, 0));
                proto_.code.push_back(abc(CONCAT, first, 2, 0));
                return first;
            }
            Constant right_constant{};
            const bool right_is_constant = node.v[1].type == CalUnitType::Constant
                && number(node.v[1].str, right_constant);
            const NumericKind left_kind = expression_kind(node.v[0]);
            const bool has_right_constant = !prefer_general_arithmetic && right_is_constant
                && (left_kind == NumericKind::Integer || operation == DIV)
                && (operation == ADD || operation == SUB || operation == MUL || operation == MOD
                    || operation == DIV || operation == IDIV);
            const unsigned right = has_right_constant ? 0 : emit_expression(node.v[1], error);
            const auto result = next_register_++;
            if (has_right_constant && operation == ADD && right_constant.kind == Constant::Integer
                && right_constant.integer >= -127 && right_constant.integer <= 127) {
                proto_.code.push_back(abc(ADDI, result, left, static_cast<unsigned>(right_constant.integer + 127)));
            } else if (has_right_constant) {
                const unsigned constant_index = constant(right_constant);
                const unsigned constant_operation = operation == ADD ? ADDK
                    : operation == SUB ? SUBK : operation == MUL ? MULK : operation == MOD ? MODK
                    : operation == DIV ? DIVK : operation == IDIV ? IDIVK : operation;
                proto_.code.push_back(abc(constant_operation, result, left, constant_index));
            } else {
                proto_.code.push_back(abc(operation, result, left, right));
            }
            return result;
        }
        if (node.type == CalUnitType::Union && node.str == "{}") {
            const unsigned table = next_register_++;
            proto_.code.push_back(abc(NEWTABLE, table, 0, static_cast<unsigned>(node.v.size())));
            proto_.code.push_back(ax(EXTRAARG, 0));
            unsigned index = 1;
            for (const auto& child : node.v) {
                if (child.type == CalUnitType::None) continue;
                const auto value = emit_expression(child, error);
                proto_.code.push_back(abc(SETI, table, index++, value));
            }
            return table;
        }
        if (node.type == CalUnitType::Operator && node.str == "." && node.v.size() == 2
            && node.v[1].type == CalUnitType::Function) {
            const auto receiver = emit_expression(node.v[0], error);
            const auto method = node.v[1].str;
            std::vector<const CalUnit*> arguments;
            std::function<void(const CalUnit&)> flatten = [&](const CalUnit& item) {
                if (item.type == CalUnitType::Operator && item.str == ",") for (const auto& child : item.v) flatten(child);
                else if (item.type != CalUnitType::None) arguments.push_back(&item);
            };
            for (const auto& child : node.v[1].v) flatten(child);
            if (method == "push_back") {
                if (arguments.size() != 1) { error = "push_back expects one argument"; return 0; }
                const auto value = emit_expression(*arguments[0], error);
                if (const auto index = append_index(node.v[0])) {
                    proto_.code.push_back(abc(SETTABLE, receiver, *index, value));
                    proto_.code.push_back(abc(ADDI, *index, *index, 128));
                    return *index;
                }
                const auto index = next_register_++;
                proto_.code.push_back(abc(LEN, index, receiver, 0));
                proto_.code.push_back(abc(ADDI, index, index, 128));
                proto_.code.push_back(abc(SETTABLE, receiver, index, value));
                return index;
            }
            if (method == "pop_back") {
                const unsigned function = next_register_;
                next_register_ += 3;
                const unsigned table_key = constant(Constant{Constant::String, 0, 0, false, "table"});
                proto_.code.push_back(abc(GETTABUP, function, 0, table_key, 1));
                const unsigned method_key = constant(Constant{Constant::String, 0, 0, false, "remove"});
                proto_.code.push_back(abc(GETFIELD, function, function, method_key));
                proto_.code.push_back(abc(MOVE, function + 1, receiver, 0));
                const auto size = next_register_++;
                proto_.code.push_back(abc(LEN, size, receiver, 0));
                proto_.code.push_back(abc(MOVE, function + 2, size, 0));
                proto_.code.push_back(abc(CALL, function, 3, 2));
                return function;
            }
            const std::string function_name = method == "push_back" ? "insert" : method == "pop_back" ? "remove" : method;
            std::vector<unsigned> argument_registers;
            argument_registers.reserve(arguments.size());
            for (const CalUnit* argument : arguments)
                argument_registers.push_back(emit_expression(*argument, error));
            const unsigned function = next_register_;
            next_register_ += static_cast<unsigned>(argument_registers.size() + 2);
            const unsigned table_key = constant(Constant{Constant::String, 0, 0, false, "table"});
            proto_.code.push_back(abc(GETTABUP, function, 0, table_key, 1));
            const unsigned method_key = constant(Constant{Constant::String, 0, 0, false, function_name});
            proto_.code.push_back(abc(GETFIELD, function, function, method_key));
            proto_.code.push_back(abc(MOVE, function + 1, receiver, 0));
            for (std::size_t index = 0; index < argument_registers.size(); ++index)
                proto_.code.push_back(abc(MOVE, function + 2 + static_cast<unsigned>(index), argument_registers[index], 0));
            proto_.code.push_back(abc(CALL, function, static_cast<unsigned>(argument_registers.size() + 2), 2));
            const unsigned result = next_register_++;
            proto_.code.push_back(abc(MOVE, result, function, 0));
            return result;
        }
        if (node.type == CalUnitType::Function) {
            std::vector<const CalUnit*> arguments;
            std::function<void(const CalUnit&)> flatten = [&](const CalUnit& item) {
                if (item.type == CalUnitType::Operator && item.str == ",") for (const auto& child : item.v) flatten(child);
                else if (item.type != CalUnitType::None) arguments.push_back(&item);
            };
            for (const auto& child : node.v) flatten(child);
            if (node.str == "size") {
                if (arguments.size() != 1) { error = "size expects one argument"; return 0; }
                const auto value = emit_expression(*arguments[0], error);
                const auto result = next_register_++;
                proto_.code.push_back(abc(LEN, result, value, 0));
                return result;
            }
            const unsigned function_reg = next_register_;
            const std::string function_name = node.str == "to_string" ? "tostring"
                : node.str == "println" ? "print" : node.str;
            const bool is_script_function = functions_ != nullptr && functions_->contains(function_name);
            const std::string closure_name = is_script_function
                ? script_function_key(function_name, arguments.size()) : function_name;
            next_register_ += static_cast<unsigned>(arguments.size() + 1);
            if (function_upvalues_) {
                const auto function = function_upvalues_->find(closure_name);
                if (function != function_upvalues_->end()) {
                    proto_.code.push_back(abc(GETUPVAL, function_reg, function->second, 0));
                } else {
                    const unsigned name = constant(Constant{Constant::String, 0, 0, false, closure_name});
                    proto_.code.push_back(abc(GETTABUP, function_reg, 0, name, 1));
                }
            } else {
                const unsigned name = constant(Constant{Constant::String, 0, 0, false, closure_name});
                proto_.code.push_back(abc(GETTABUP, function_reg, 0, name, 1));
            }
            std::vector<unsigned> argument_registers;
            argument_registers.reserve(arguments.size());
            for (const auto* argument : arguments)
                argument_registers.push_back(emit_expression(*argument, error, false, function_name == "type"));
            for (std::size_t argument_index = 0; argument_index < arguments.size(); ++argument_index) {
                const auto reg = argument_registers[argument_index];
                const auto target = function_reg + 1 + static_cast<unsigned>(argument_index);
                if (reg != target) proto_.code.push_back(abc(MOVE, target, reg, 0));
            }
            proto_.code.push_back(abc(CALL, function_reg, static_cast<unsigned>(arguments.size() + 1), 2));
            proto_.call_frames.emplace_back(proto_.code.size() - 1,
                owner_.compiled_source_frame(is_script_function || arguments.empty() ? node : *arguments.front()));
            if (is_script_function) {
                if (!functions_->at(function_name).contains(arguments.size())) {
                    std::vector<std::size_t> arities;
                    arities.reserve(functions_->at(function_name).size());
                    for (const auto& [arity, function] : functions_->at(function_name)) arities.push_back(arity);
                    std::sort(arities.begin(), arities.end());
                    std::string available;
                    for (std::size_t index = 0; index < arities.size(); ++index) {
                        if (index != 0) available += ", ";
                        available += std::to_string(arities[index]);
                    }
                    proto_.call_errors.emplace_back(proto_.code.size() - 1,
                        "function '" + function_name + "' has no overload for " + std::to_string(arguments.size())
                        + " arguments; available: " + available);
                }
            }
            if (function_name == "tostring") {
                const auto result = next_register_++;
                proto_.code.push_back(abc(MOVE, result, function_reg, 0));
                return result;
            }
            return function_reg;
        }
        error = "unsupported expression node: " + node.str;
        return 0;
    }

    bool emit_statement(const CalUnit& node, std::string& error)
    {
        if (node.type == CalUnitType::None || node.type == CalUnitType::Split || node.str == ";") return true;
        if (node.type == CalUnitType::Label || (node.type == CalUnitType::Key && node.v.empty())) {
            labels_[node.str] = static_cast<unsigned>(proto_.code.size());
            return true;
        }
        if (node.type == CalUnitType::Union && (node.str.empty() || node.str == "{}")) return emit_block(node, error);
        if (node.type == CalUnitType::Operator && node.str == "," && node.v.size() == 2)
            return emit_statement(node.v[0], error) && emit_statement(node.v[1], error);
        if (node.type == CalUnitType::Parameter && node.v.empty()) {
            const NumericKind kind = node.with_type ? type_kind(node.type_name) : NumericKind::Unknown;
            if (node.with_type) numeric_kinds_.back()[node.str] = kind;
            if (node.with_type && node.type_name == "string") string_locals_.back().insert(node.str);
            if (node.with_type && kind != NumericKind::Unknown) uninitialized_locals_.back().insert(node.str);
            const auto target = node.with_type ? declare_local(node.str) : local(node.str);
            if (kind == NumericKind::Integer) proto_.code.push_back(abx(LOADI, target, (1u << 16) - 1));
            else if (kind == NumericKind::Float) {
                const unsigned zero = constant(Constant{Constant::Number, 0, 0.0, false, {}});
                proto_.code.push_back(abx(LOADK, target, zero));
            } else {
                proto_.code.push_back(abc(NEWTABLE, target, 0, 0));
                proto_.code.push_back(ax(EXTRAARG, 0));
            }
            if (node.with_type && struct_definition(node.type_name)) {
                const unsigned key = constant(Constant{Constant::String, 0, 0, false, "__cifa_struct_type"});
                const unsigned type = emit_constant(Constant{Constant::String, 0, 0, false, node.type_name});
                proto_.code.push_back(abc(SETFIELD, target, key, type));
                auto& fields = struct_field_types_.back()[node.str];
                for (const auto& field : *struct_definition(node.type_name))
                    fields[field.name] = field.type_name;
            }
            return true;
        }
        if (node.type == CalUnitType::Parameter && node.with_type && !node.v.empty() && node.v[0].str == "[]") {
            array_element_types_.back()[node.str] = node.type_name;
            const unsigned target = declare_local(node.str);
            proto_.code.push_back(abc(NEWTABLE, target, 0, 0));
            proto_.code.push_back(ax(EXTRAARG, 0));
            unsigned table = target;
            for (std::size_t depth = 1; depth < node.v.size(); ++depth) {
                const unsigned child = next_register_++;
                proto_.code.push_back(abc(NEWTABLE, child, 0, 0));
                proto_.code.push_back(ax(EXTRAARG, 0));
                proto_.code.push_back(abc(SETI, table, 1, child));
                table = child;
            }
            return true;
        }
        if (node.type == CalUnitType::Operator && node.str == "=" && node.v.size() == 2) {
            const bool field_assignment = node.v[0].type == CalUnitType::Operator && node.v[0].str == "."
                && node.v[0].v.size() == 2 && node.v[0].v[0].type == CalUnitType::Parameter
                && node.v[0].v[1].type == CalUnitType::Parameter;
            if (node.v[0].type != CalUnitType::Parameter && !field_assignment) { error = "unsupported assignment target"; return false; }
            if (field_assignment) {
                unsigned source = emit_expression(node.v[1], error);
                const std::string field_type = struct_field_type(node.v[0].v[0].str, node.v[0].v[1].str);
                if (field_type == "int" && expression_kind(node.v[1]) == NumericKind::Float) {
                    const unsigned converted = next_register_++;
                    const unsigned one = constant(Constant{Constant::Integer, 1, 0, false, {}});
                    proto_.code.push_back(abc(IDIVK, converted, source, one));
                    source = converted;
                } else if ((field_type == "double" || field_type == "float") && expression_kind(node.v[1]) == NumericKind::Integer) {
                    const unsigned converted = next_register_++;
                    const unsigned one = constant(Constant{Constant::Number, 0, 1.0, false, {}});
                    proto_.code.push_back(abc(DIVK, converted, source, one));
                    source = converted;
                } else if (field_type == "bool") {
                    const unsigned negated = next_register_++;
                    const unsigned converted = next_register_++;
                    proto_.code.push_back(abc(NOT, negated, source, 0));
                    proto_.code.push_back(abc(NOT, converted, negated, 0));
                    source = converted;
                }
                const unsigned table = emit_name(node.v[0].v[0].str, error);
                const unsigned key = constant(Constant{Constant::String, 0, 0, false, node.v[0].v[1].str});
                proto_.code.push_back(abc(SETFIELD, table, key, source));
                return error.empty();
            }
            if (node.v[0].with_type && !node.v[0].v.empty() && node.v[0].v[0].str == "[]")
                array_element_types_.back()[node.v[0].str] = node.v[0].type_name;
            if (node.v[0].with_type) numeric_kinds_.back()[node.v[0].str] = type_kind(node.v[0].type_name);
            if (node.v[0].with_type && node.v[0].type_name == "string") string_locals_.back().insert(node.v[0].str);
            std::string target_type = node.v[0].with_type ? node.v[0].type_name : declared_type(node.v[0].str);
            if (target_type == "auto") {
                target_type = expression_kind(node.v[1]) == NumericKind::Float ? "double"
                    : expression_kind(node.v[1]) == NumericKind::Integer ? "int"
                    : node.v[1].type == CalUnitType::String ? "string"
                    : node.v[1].str == "true" || node.v[1].str == "false" ? "bool" : "";
            }
            if (node.v[0].with_type && target_type != "") {
                declared_types_.back()[node.v[0].str] = target_type;
                numeric_kinds_.back()[node.v[0].str] = type_kind(target_type);
                if (target_type == "string") string_locals_.back().insert(node.v[0].str);
                if (const auto* definition = struct_definition(target_type)) {
                    auto& fields = struct_field_types_.back()[node.v[0].str];
                    for (const auto& field : *definition) fields[field.name] = field.type_name;
                }
            }
            if (!node.v[0].v.empty() && node.v[0].v[0].str == "[]" && root_
                && !find_local(node.v[0].str) && !root_globals_.contains(node.v[0].str)
                && !owner_.registered_globals().contains(node.v[0].str)) {
                const unsigned table = next_register_++;
                proto_.code.push_back(abc(NEWTABLE, table, 0, 0));
                proto_.code.push_back(ax(EXTRAARG, 0));
                emit_global_set(node.v[0].str, table);
                root_globals_.insert(node.v[0].str);
            }
            unsigned source = emit_expression(node.v[1], error);
            uninitialized_locals_.back().erase(node.v[0].str);
            if (node.v[0].with_type && node.v[0].type_name == "auto") {
                const unsigned checked = next_register_++;
                proto_.code.push_back(abc(NOT, checked, source, 0));
                proto_.value_errors.emplace_back(proto_.code.size() - 1,
                    "cannot infer type for auto variable from NoValue");
            }
            if (!target_type.empty()) {
                if ((target_type == "int" || target_type == "double" || target_type == "float")
                    && expression_kind(node.v[1]) == NumericKind::Unknown) {
                    const unsigned checked = next_register_++;
                    proto_.code.push_back(abc(NOT, checked, source, 0));
                    proto_.value_errors.emplace_back(proto_.code.size() - 1,
                        "cannot convert value to '" + (target_type == "float" ? std::string("double") : target_type) + "'");
                }
                if (type_kind(target_type) == NumericKind::Float
                    && expression_kind(node.v[1]) == NumericKind::Integer) {
                    const unsigned converted = next_register_++;
                    const unsigned one = constant(Constant{Constant::Number, 0, 1.0, false, {}});
                    proto_.code.push_back(abc(DIVK, converted, source, one));
                    source = converted;
                } else if (target_type == "int" && expression_kind(node.v[1]) == NumericKind::Unknown) {
                    const unsigned converted = next_register_++;
                    proto_.code.push_back(abc(TEST, source, 0, 0, 1));
                    const unsigned false_jump = static_cast<unsigned>(proto_.code.size());
                    proto_.code.push_back(asj(JMP, 0));
                    proto_.code.push_back(abx(LOADI, converted, 1u << 16));
                    const unsigned end_jump = static_cast<unsigned>(proto_.code.size());
                    proto_.code.push_back(asj(JMP, 0));
                    patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
                    proto_.code.push_back(abx(LOADI, converted, (1u << 16) - 1));
                    patch_jump(end_jump, static_cast<unsigned>(proto_.code.size()));
                    source = converted;
                } else if (target_type == "int" && expression_kind(node.v[1]) == NumericKind::Float) {
                    const unsigned converted = next_register_++;
                    const unsigned one = constant(Constant{Constant::Integer, 1, 0, false, {}});
                    proto_.code.push_back(abc(IDIVK, converted, source, one));
                    source = converted;
                } else if (target_type == "bool") {
                    const unsigned negated = next_register_++;
                    const unsigned converted = next_register_++;
                    proto_.code.push_back(abc(NOT, negated, source, 0));
                    proto_.code.push_back(abc(NOT, converted, negated, 0));
                    source = converted;
                }
            }
            if (!node.v[0].v.empty() && node.v[0].v[0].str == "[]") {
                const std::string element_type = array_element_type(node.v[0].str);
                if (element_type == "int" && expression_kind(node.v[1]) == NumericKind::Float) {
                    const unsigned converted = next_register_++;
                    const unsigned one = constant(Constant{Constant::Integer, 1, 0, false, {}});
                    proto_.code.push_back(abc(IDIVK, converted, source, one));
                    source = converted;
                } else if ((element_type == "double" || element_type == "float")
                    && expression_kind(node.v[1]) == NumericKind::Integer) {
                    const unsigned converted = next_register_++;
                    const unsigned one = constant(Constant{Constant::Number, 0, 1.0, false, {}});
                    proto_.code.push_back(abc(DIVK, converted, source, one));
                    source = converted;
                } else if (element_type == "bool") {
                    const unsigned negated = next_register_++;
                    const unsigned converted = next_register_++;
                    proto_.code.push_back(abc(NOT, negated, source, 0));
                    proto_.code.push_back(abc(NOT, converted, negated, 0));
                    source = converted;
                }
            }
            if (!node.v[0].v.empty() && node.v[0].v[0].str == "[]") {
                const auto table = emit_name(node.v[0].str, error);
                unsigned target = table;
                for (std::size_t accessor_index = 0; accessor_index + 1 < node.v[0].v.size(); ++accessor_index) {
                    const CalUnit& accessor = node.v[0].v[accessor_index];
                    if (accessor.str != "[]" || accessor.v.size() != 1) { error = "unsupported index shape"; return false; }
                    const unsigned subscript = emit_expression(accessor.v[0], error);
                    const unsigned lua_index = next_register_++;
                    proto_.code.push_back(abc(ADDI, lua_index, subscript, 128));
                    const unsigned next_table = next_register_++;
                    proto_.code.push_back(abc(GETTABLE, next_table, target, lua_index));
                    proto_.create_table_reads.push_back(proto_.code.size() - 1);
                    target = next_table;
                }
                const CalUnit& last_accessor = node.v[0].v.back();
                if (last_accessor.str != "[]" || last_accessor.v.size() != 1) { error = "unsupported index shape"; return false; }
                Constant constant_index{};
                if (last_accessor.v[0].type == CalUnitType::Constant && number(last_accessor.v[0].str, constant_index)
                    && constant_index.kind == Constant::Integer && constant_index.integer >= 0 && constant_index.integer < 255) {
                    proto_.code.push_back(abc(SETI, target, static_cast<unsigned>(constant_index.integer + 1), source));
                } else {
                    const unsigned source_index = emit_expression(last_accessor.v[0], error);
                    if (last_accessor.v[0].type == CalUnitType::String
                        || last_accessor.v[0].type == CalUnitType::Parameter && is_string_local(last_accessor.v[0].str))
                        proto_.code.push_back(abc(SETTABLE, target, source_index, source));
                    else {
                        const unsigned lua_index = next_register_++;
                        proto_.code.push_back(abc(ADDI, lua_index, source_index, 128));
                        proto_.code.push_back(abc(SETTABLE, target, lua_index, source));
                    }
                }
            } else if (root_ && !node.v[0].with_type && !find_local(node.v[0].str)) {
                emit_global_set(node.v[0].str, source);
                root_globals_.insert(node.v[0].str);
            }
            else {
                const bool indexed_parameter = node.v[1].type == CalUnitType::Parameter
                    && !node.v[1].v.empty() && node.v[1].v[0].str == "[]";
                if (node.v[0].with_type && !has_current_local(node.v[0].str)
                    && (node.v[1].type != CalUnitType::Parameter || indexed_parameter)) {
                    locals_.back().emplace(node.v[0].str, source);
                    return error.empty();
                }
                const auto target = local(node.v[0].str);
                if (target != source) proto_.code.push_back(abc(MOVE, target, source, 0));
            }
            return error.empty();
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2
            && (node.str == "+=" || node.str == "-=" || node.str == "*=" || node.str == "/=" || node.str == "%="
                || node.str == "&=" || node.str == "|=" || node.str == "^=" || node.str == "<<=" || node.str == ">>=")) {
            const bool field_assignment = node.v[0].type == CalUnitType::Operator && node.v[0].str == "."
                && node.v[0].v.size() == 2 && node.v[0].v[0].type == CalUnitType::Parameter
                && node.v[0].v[1].type == CalUnitType::Parameter;
            if ((node.v[0].type != CalUnitType::Parameter || !node.v[0].v.empty()) && !field_assignment) {
                error = "unsupported compound assignment";
                return false;
            }
            if (field_assignment) {
                const unsigned table = emit_name(node.v[0].v[0].str, error);
                const unsigned key = constant(Constant{Constant::String, 0, 0, false, node.v[0].v[1].str});
                const unsigned left = next_register_++;
                proto_.code.push_back(abc(GETFIELD, left, table, key));
                const unsigned right = emit_expression(node.v[1], error);
                const unsigned op = node.str == "+=" ? ADD : node.str == "-=" ? SUB
                    : node.str == "*=" ? MUL : node.str == "/=" ? IDIV : node.str == "%=" ? MOD
                    : node.str == "&=" ? BAND : node.str == "|=" ? BOR : node.str == "^=" ? BXOR
                    : node.str == "<<=" ? SHL : SHR;
                proto_.code.push_back(abc(op, left, left, right));
                proto_.code.push_back(abc(SETFIELD, table, key, left));
                return error.empty();
            }
            const bool is_global = !find_local(node.v[0].str).has_value();
            const auto left = emit_name(node.v[0].str, error);
            const auto contains_string = [&](const CalUnit& item, const auto& self) -> bool {
                if (item.type == CalUnitType::String || item.type == CalUnitType::Parameter && is_string_local(item.str)) return true;
                if (item.type == CalUnitType::Function && item.str == "to_string") return true;
                for (const auto& child : item.v) if (self(child, self)) return true;
                return false;
            };
            if (node.str == "+=" && contains_string(node.v[1], contains_string)) {
                std::vector<const CalUnit*> pieces;
                const auto flatten = [&](const CalUnit& item, const auto& self) -> void {
                    if (item.type == CalUnitType::Operator && item.str == "+" && item.v.size() == 2) {
                        self(item.v[0], self);
                        self(item.v[1], self);
                    } else pieces.push_back(&item);
                };
                flatten(node.v[1], flatten);
                const auto first = next_register_;
                next_register_ += static_cast<unsigned>(pieces.size() + 1);
                proto_.code.push_back(abc(MOVE, first, left, 0));
                for (std::size_t index = 0; index < pieces.size(); ++index) {
                    const unsigned value = emit_expression(*pieces[index], error);
                    const unsigned target = first + 1 + static_cast<unsigned>(index);
                    if (value != target) proto_.code.push_back(abc(MOVE, target, value, 0));
                }
                proto_.code.push_back(abc(CONCAT, first, static_cast<unsigned>(pieces.size() + 1), 0));
                if (left != first) proto_.code.push_back(abc(MOVE, left, first, 0));
                if (is_global) emit_global_set(node.v[0].str, left);
                return error.empty();
            }
            const auto right = emit_expression(node.v[1], error);
            const unsigned op = node.str == "+=" ? ADD : node.str == "-=" ? SUB
                : node.str == "*=" ? MUL : node.str == "/=" ? IDIV : node.str == "%=" ? MOD
                : node.str == "&=" ? BAND : node.str == "|=" ? BOR : node.str == "^=" ? BXOR
                : node.str == "<<=" ? SHL : SHR;
            proto_.code.push_back(abc(op, left, left, right));
            if (is_global) emit_global_set(node.v[0].str, left);
            return error.empty();
        }
        if (node.type == CalUnitType::Key && node.str == "return") {
            if (node.v.empty() || node.v[0].type == CalUnitType::None || node.v[0].str == ";") { proto_.code.push_back(abc(RETURN0, 0, 0, 0)); return true; }
            const auto value = emit_expression(node.v[0], error);
            proto_.code.push_back(abc(RETURN1, value, 0, 0));
            return error.empty();
        }
        if (node.type == CalUnitType::Key && node.str == "break") {
            if (loop_breaks_.empty()) { error = "break outside loop"; return false; }
            loop_breaks_.back().push_back(static_cast<unsigned>(proto_.code.size()));
            proto_.code.push_back(asj(JMP, 0));
            return true;
        }
        if (node.type == CalUnitType::Key && node.str == "continue") {
            if (loop_continues_.empty()) { error = "continue outside loop"; return false; }
            loop_continues_.back().push_back(static_cast<unsigned>(proto_.code.size()));
            proto_.code.push_back(asj(JMP, 0));
            return true;
        }
        if ((node.type == CalUnitType::Goto)
            || (node.type == CalUnitType::Key && node.str == "goto" && node.v.size() == 1
                && node.v[0].type == CalUnitType::Parameter)) {
            const unsigned jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));
            goto_jumps_.emplace_back(node.type == CalUnitType::Goto ? node.str : node.v[0].str, jump);
            return true;
        }
        if (node.type == CalUnitType::Key && node.str == "if" && node.v.size() >= 2) {
            const auto false_jump = emit_condition(node.v[0], error);
            if (!emit_block(node.v[1], error)) return false;
            if (node.v.size() >= 3) {
                const auto end_jump = proto_.code.size(); proto_.code.push_back(asj(JMP, 0));
                patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
                if (!emit_block(node.v[2], error)) return false;
                patch_jump(static_cast<unsigned>(end_jump), static_cast<unsigned>(proto_.code.size()));
            } else patch_jump(false_jump, static_cast<unsigned>(proto_.code.size()));
            return error.empty();
        }
        if (node.type == CalUnitType::Key && node.str == "switch" && node.v.size() == 2
            && node.v[1].type == CalUnitType::Union) {
            const unsigned condition = emit_expression(node.v[0], error);
            std::vector<std::pair<const CalUnit*, unsigned>> match_jumps;
            unsigned previous_false_jump = std::numeric_limits<unsigned>::max();
            for (const CalUnit& item : node.v[1].v) {
                if (item.type == CalUnitType::Key && item.str == "case" && !item.v.empty()) {
                    if (previous_false_jump != std::numeric_limits<unsigned>::max())
                        patch_jump(previous_false_jump, static_cast<unsigned>(proto_.code.size()));
                    const unsigned case_value = emit_expression(item.v[0], error);
                    proto_.code.push_back(abc(EQ, condition, case_value, 0));
                    previous_false_jump = static_cast<unsigned>(proto_.code.size());
                    proto_.code.push_back(asj(JMP, 0));
                    const unsigned match_jump = static_cast<unsigned>(proto_.code.size());
                    proto_.code.push_back(asj(JMP, 0));
                    match_jumps.emplace_back(&item, match_jump);
                }
            }
            const unsigned default_jump = static_cast<unsigned>(proto_.code.size());
            proto_.code.push_back(asj(JMP, 0));

            loop_breaks_.emplace_back();
            std::unordered_map<const CalUnit*, unsigned> body_labels;
            const CalUnit* default_case = nullptr;
            for (const CalUnit& item : node.v[1].v) {
                if (item.type == CalUnitType::Key && (item.str == "case" || item.str == "default")) {
                    body_labels.emplace(&item, static_cast<unsigned>(proto_.code.size()));
                    if (item.str == "default") default_case = &item;
                    continue;
                }
                if (!emit_statement(item, error)) return false;
            }
            const unsigned switch_end = static_cast<unsigned>(proto_.code.size());
            for (const auto& [case_node, jump] : match_jumps)
                patch_jump(jump, body_labels.at(case_node));
            patch_jump(default_jump, default_case ? body_labels.at(default_case) : switch_end);
            if (previous_false_jump != std::numeric_limits<unsigned>::max())
                patch_jump(previous_false_jump, default_case ? body_labels.at(default_case) : switch_end);
            for (const unsigned jump : loop_breaks_.back()) patch_jump(jump, switch_end);
            loop_breaks_.pop_back();
            return error.empty();
        }
        if (node.type == CalUnitType::Key && node.str == "do" && node.v.size() == 2
            && !node.v[1].v.empty()) {
            const unsigned body_start = static_cast<unsigned>(proto_.code.size());
            loop_breaks_.emplace_back();
            loop_continues_.emplace_back();
            if (!emit_block(node.v[0], error)) return false;
            const unsigned condition_start = static_cast<unsigned>(proto_.code.size());
            for (const unsigned jump : loop_continues_.back()) patch_jump(jump, condition_start);
            loop_continues_.pop_back();
            const unsigned false_exit = emit_condition(node.v[1].v[0], error);
            proto_.code.push_back(asj(JMP, static_cast<int>(body_start) - static_cast<int>(proto_.code.size()) - 1));
            const unsigned loop_end = static_cast<unsigned>(proto_.code.size());
            patch_jump(false_exit, loop_end);
            for (const unsigned jump : loop_breaks_.back()) patch_jump(jump, loop_end);
            loop_breaks_.pop_back();
            return error.empty();
        }
        if (node.type == CalUnitType::Key && (node.str == "while" || node.str == "for")) {
            const CalUnit* clauses = nullptr; const CalUnit* body = nullptr;
            if (node.str == "while") { clauses = &node.v[0]; body = &node.v[1]; }
            else {
                clauses = &node.v[0];
                body = &node.v[1];
                const CalUnit* range_clause = clauses->type == CalUnitType::Operator && clauses->str == ":" ? clauses : nullptr;
                if (range_clause && range_clause->v.size() == 2
                    && range_clause->v[0].type == CalUnitType::Parameter) {
                    const CalUnit& variable = range_clause->v[0];
                    const unsigned table = emit_expression(range_clause->v[1], error);
                    const unsigned index = next_register_++;
                    const unsigned length = next_register_++;
                    proto_.code.push_back(abx(LOADI, index, (1u << 16) - 1));
                    proto_.code.push_back(abc(LEN, length, table, 0));
                    const unsigned loop_start = static_cast<unsigned>(proto_.code.size());
                    proto_.code.push_back(abc(LT, index, length, 0));
                    const unsigned exit_jump = static_cast<unsigned>(proto_.code.size());
                    proto_.code.push_back(asj(JMP, 0));
                    const unsigned lua_index = next_register_++;
                    proto_.code.push_back(abc(ADDI, lua_index, index, 128));
                    const unsigned value = next_register_++;
                    proto_.code.push_back(abc(GETTABLE, value, table, lua_index));
                    locals_.emplace_back();
                    numeric_kinds_.emplace_back();
                    locals_.back()[variable.str] = value;
                    numeric_kinds_.back()[variable.str] = type_kind(variable.type_name);
                    loop_breaks_.emplace_back();
                    loop_continues_.emplace_back();
                    if (!emit_block(*body, error)) return false;
                    const unsigned continue_target = static_cast<unsigned>(proto_.code.size());
                    for (const unsigned jump : loop_continues_.back()) patch_jump(jump, continue_target);
                    loop_continues_.pop_back();
                    proto_.code.push_back(abc(ADDI, index, index, 128));
                    proto_.code.push_back(asj(JMP, static_cast<int>(loop_start) - static_cast<int>(proto_.code.size()) - 1));
                    const unsigned loop_end = static_cast<unsigned>(proto_.code.size());
                    patch_jump(exit_jump, loop_end);
                    for (const unsigned jump : loop_breaks_.back()) patch_jump(jump, loop_end);
                    loop_breaks_.pop_back();
                    numeric_kinds_.pop_back();
                    locals_.pop_back();
                    return error.empty();
                }
                const auto writes_name = [&](const CalUnit& item, const std::string& name, const auto& self) -> bool {
                    if (item.type == CalUnitType::Operator
                        && (item.str == "=" || item.str == "+=" || item.str == "-=" || item.str == "++" || item.str == "()++")
                        && !item.v.empty() && item.v[0].type == CalUnitType::Parameter && item.v[0].str == name)
                        return true;
                    for (const auto& child : item.v) if (self(child, name, self)) return true;
                    return false;
                };
                const auto loop_step = [&](const CalUnit& item, const std::string& name) -> int {
                    if ((item.str == "++" || item.str == "()++") && item.v.size() == 1
                        && item.v[0].type == CalUnitType::Parameter && item.v[0].str == name)
                        return 1;
                    if ((item.str == "--" || item.str == "()--") && item.v.size() == 1
                        && item.v[0].type == CalUnitType::Parameter && item.v[0].str == name)
                        return -1;
                    if ((item.str != "+=" && item.str != "-=") || item.v.size() != 2 || item.v[0].type != CalUnitType::Parameter
                        || item.v[0].str != name || item.v[1].type != CalUnitType::Constant)
                        return 0;
                    Constant amount{};
                    if (!number(item.v[1].str, amount) || amount.kind != Constant::Integer || amount.integer != 1) return 0;
                    return item.str == "+=" ? 1 : -1;
                };
                const auto unique_append_receiver = [&](const CalUnit& item, std::string& receiver, unsigned& count, const auto& self) -> void {
                    if (item.type == CalUnitType::Operator && item.str == "." && item.v.size() == 2
                        && item.v[0].type == CalUnitType::Parameter && item.v[0].v.empty()
                        && item.v[1].type == CalUnitType::Function && item.v[1].str == "push_back") {
                        receiver = item.v[0].str;
                        ++count;
                    }
                    for (const auto& child : item.v) self(child, receiver, count, self);
                };
                const CalUnit* numeric_condition = &clauses->v[1];
                const CalUnit* extra_condition = nullptr;
                if (numeric_condition->type == CalUnitType::Operator && numeric_condition->str == "&&" && numeric_condition->v.size() == 2) {
                    numeric_condition = &numeric_condition->v[0];
                    extra_condition = &clauses->v[1].v[1];
                }
                if (clauses->v.size() == 3 && clauses->v[0].type == CalUnitType::Operator
                    && clauses->v[0].str == "=" && clauses->v[0].v.size() == 2
                    && clauses->v[0].v[0].type == CalUnitType::Parameter && clauses->v[0].v[0].with_type
                    && type_kind(clauses->v[0].v[0].type_name) == NumericKind::Integer
                    && numeric_condition->type == CalUnitType::Operator
                    && (numeric_condition->str == "<" || numeric_condition->str == ">=") && numeric_condition->v.size() == 2
                    && numeric_condition->v[0].type == CalUnitType::Parameter
                    && numeric_condition->v[0].str == clauses->v[0].v[0].str
                    && loop_step(clauses->v[2], clauses->v[0].v[0].str) != 0) {
                    const std::string& index_name = clauses->v[0].v[0].str;
                    const CalUnit& limit = numeric_condition->v[1];
                    const int step = loop_step(clauses->v[2], index_name);
                    const bool ascending = numeric_condition->str == "<" && step == 1;
                    const bool descending = numeric_condition->str == ">=" && step == -1;
                    const bool stable_limit = limit.type == CalUnitType::Constant
                        || (limit.type == CalUnitType::Parameter && limit.v.empty() && find_local(limit.str)
                            && !writes_name(*body, limit.str, writes_name));
                    if (stable_limit && (ascending || descending)) {
                        const unsigned initial = emit_expression(clauses->v[0].v[1], error);
                        const unsigned loop_base = next_register_;
                        next_register_ += 4;
                        locals_.back()[index_name] = loop_base + 3;
                        numeric_kinds_.back()[index_name] = NumericKind::Integer;
                        proto_.code.push_back(abc(MOVE, loop_base, initial, 0));
                        const bool stable_index = !writes_name(*body, index_name, writes_name);
                        const unsigned one_based = next_register_++;
                        proto_.code.push_back(abc(ADDI, one_based, loop_base, 128));
                        std::string append_receiver;
                        unsigned append_count = 0;
                        unique_append_receiver(*body, append_receiver, append_count, unique_append_receiver);
                        const bool cached_append = append_count == 1 && !writes_name(*body, append_receiver, writes_name);
                        unsigned append_index_register = 0;
                        if (cached_append) {
                            const unsigned receiver = emit_name(append_receiver, error);
                            append_index_register = next_register_++;
                            proto_.code.push_back(abc(LEN, append_index_register, receiver, 0));
                            proto_.code.push_back(abc(ADDI, append_index_register, append_index_register, 128));
                        }
                        const unsigned limit_register = emit_expression(limit, error);
                        proto_.code.push_back(abc(MOVE, loop_base + 1, limit_register, 0));
                        if (ascending) {
                            proto_.code.push_back(abc(ADDI, loop_base + 1, loop_base + 1, 126));
                        }
                        proto_.code.push_back(abx(LOADI, loop_base + 2, step + 65535));
                        const unsigned prep = static_cast<unsigned>(proto_.code.size());
                        proto_.code.push_back(abx(FORPREP, loop_base, 0));
                        const unsigned body_start = static_cast<unsigned>(proto_.code.size());
                        const unsigned extra_exit = extra_condition ? emit_condition(*extra_condition, error) : 0;
                        loop_breaks_.emplace_back();
                        loop_continues_.emplace_back();
                        if (stable_index) one_based_indices_.emplace_back(index_name, one_based);
                        if (cached_append) append_indices_.emplace_back(append_receiver, append_index_register);
                        if (!emit_block(*body, error)) return false;
                        if (cached_append) append_indices_.pop_back();
                        if (stable_index) one_based_indices_.pop_back();
                        const unsigned loop = static_cast<unsigned>(proto_.code.size());
                        for (const unsigned jump : loop_continues_.back()) patch_jump(jump, loop);
                        loop_continues_.pop_back();
                        const unsigned encoded_step = static_cast<unsigned>(step + 127);
                        proto_.code.push_back(abc(ADDI, one_based, one_based, encoded_step));
                        const unsigned for_loop = static_cast<unsigned>(proto_.code.size());
                        proto_.code.push_back(abx(FORLOOP, loop_base, for_loop - body_start + 1));
                        const unsigned loop_end = static_cast<unsigned>(proto_.code.size());
                        proto_.code[prep] = abx(FORPREP, loop_base, loop - prep - 1);
                        if (extra_condition) patch_jump(extra_exit, loop_end);
                        for (const unsigned jump : loop_breaks_.back()) patch_jump(jump, loop_end);
                        loop_breaks_.pop_back();
                        return error.empty();
                    }
                }
                if (clauses->v.size() == 3 && clauses->v[0].type == CalUnitType::Operator
                    && clauses->v[0].str == "=" && clauses->v[0].v.size() == 2
                    && clauses->v[0].v[0].type == CalUnitType::Parameter) {
                    local(clauses->v[0].v[0].str);
                }
                if (clauses->v.size() == 3 && !emit_statement(clauses->v[0], error)) return false;
            }
            const auto loop_start = static_cast<unsigned>(proto_.code.size());
            const auto false_jump = emit_condition(node.str == "while" ? *clauses : clauses->v[1], error);
            loop_breaks_.emplace_back();
            loop_continues_.emplace_back();
            if (!emit_block(*body, error)) return false;
            const unsigned continue_target = static_cast<unsigned>(proto_.code.size());
            if (node.str == "for" && clauses->v.size() == 3 && !emit_statement(clauses->v[2], error)) return false;
            for (const unsigned jump : loop_continues_.back()) patch_jump(jump, continue_target);
            loop_continues_.pop_back();
            proto_.code.push_back(asj(JMP, static_cast<int>(loop_start) - static_cast<int>(proto_.code.size()) - 1));
            const auto loop_end = static_cast<unsigned>(proto_.code.size());
            patch_jump(false_jump, loop_end);
            for (const auto jump : loop_breaks_.back()) patch_jump(jump, loop_end);
            loop_breaks_.pop_back();
            return error.empty();
        }
        if (node.can_cal()) {
            emit_expression(node, error);
            return error.empty();
        }
        error = "unsupported statement type=" + std::to_string(static_cast<int>(node.type)) + " str=" + node.str;
        return false;
    }

    bool emit_block(const CalUnit& node, std::string& error)
    {
        if (node.type == CalUnitType::Union && (node.str.empty() || node.str == "{}")) {
            const bool nested_block = block_depth_++ != 0;
            if (nested_block) {
                locals_.emplace_back();
                numeric_kinds_.emplace_back();
                string_locals_.emplace_back();
                declared_types_.emplace_back();
            }
            for (const auto& child : node.v) {
                if (child.type == CalUnitType::Split || child.str == ";") continue;
                if (!emit_statement(child, error)) {
                    if (nested_block) {
                        locals_.pop_back();
                        numeric_kinds_.pop_back();
                        string_locals_.pop_back();
                        declared_types_.pop_back();
                    }
                    --block_depth_;
                    return false;
                }
            }
            if (nested_block) {
                locals_.pop_back();
                numeric_kinds_.pop_back();
                string_locals_.pop_back();
                declared_types_.pop_back();
            }
            --block_depth_;
            return true;
        }
        return emit_statement(node, error);
    }
};

class ChunkWriter {
    std::vector<std::uint8_t> data_;
    template<class T> void raw(const T& value) { const auto* p = reinterpret_cast<const std::uint8_t*>(&value); data_.insert(data_.end(), p, p + sizeof(T)); }
    void byte(std::uint8_t value) { data_.push_back(value); }
    void size(std::size_t value) { std::uint8_t buffer[16]{}; int n = 0; do { buffer[15 - n++] = static_cast<std::uint8_t>(value & 0x7f); value >>= 7; } while (value); buffer[15] |= 0x80; data_.insert(data_.end(), buffer + 16 - n, buffer + 16); }
    void string(const std::string* value) { if (!value) { size(0); return; } size(value->size() + 1); data_.insert(data_.end(), value->begin(), value->end()); }
    void proto(const Proto& p, const std::string* parent_source, bool root)
    {
        if (p.source.empty() || p.source == (parent_source ? *parent_source : std::string{})) string(nullptr); else string(&p.source);
        size(p.linedefined); size(p.lastline); byte(static_cast<std::uint8_t>(p.params)); byte(static_cast<std::uint8_t>(p.vararg)); byte(static_cast<std::uint8_t>(p.maxstack));
        size(p.code.size()); for (auto instruction : p.code) raw(instruction);
        size(p.constants.size());
        for (const auto& c : p.constants) {
            switch (c.kind) {
            case Constant::Integer: byte(LUA_VNUMINT); raw(c.integer); break;
            case Constant::Number: byte(LUA_VNUMFLT); raw(c.number); break;
            case Constant::String: byte(LUA_VSHRSTR); string(&c.string); break;
            case Constant::Boolean: byte(c.boolean ? LUA_VTRUE : LUA_VFALSE); break;
            }
        }
        size(p.upvalues.size()); for (const auto& upvalue : p.upvalues) { byte(upvalue.instack ? 1 : 0); byte(static_cast<std::uint8_t>(upvalue.index)); byte(0); }
        size(p.children.size()); for (const auto& child : p.children) proto(child, p.source.empty() ? parent_source : &p.source, false);
        size(0); size(0); size(0); size(p.upvalues.size()); for (const auto& upvalue : p.upvalues) string(nullptr);
    }
public:
    std::vector<std::uint8_t> write(const Proto& root)
    {
        data_.insert(data_.end(), {0x1b, 'L', 'u', 'a', 0x54, 0, 0x19, 0x93, 0x0d, 0x0a, 0x1a, 0x0a});
        byte(sizeof(std::uint32_t)); byte(sizeof(std::int64_t)); byte(sizeof(double)); const std::int64_t magic = 0x5678; const double number = 370.5; raw(magic); raw(number); byte(1); proto(root, nullptr, true); return data_;
    }
};

std::shared_ptr<Proto> CifaBytecode::compile_lua_program(std::vector<std::uint8_t>& chunk, std::string& error) const
{
    const auto* root = compiled ? &compilation_root : nullptr;
    const auto* current_functions = compiled ? &compilation_functions : nullptr;
    if (!root || !current_functions) { error = "Cifa AST is not compiled"; return {}; }
    std::unordered_map<std::string, FunctionOverloads> functions = functions2;
    for (const auto& [name, overloads] : *current_functions)
        for (const auto& [arity, function] : overloads) functions[name][arity] = function;
    auto proto = std::make_shared<Proto>();
    proto->upvalues.push_back({true, 0});
    std::vector<std::pair<std::string, std::size_t>> function_names;
    for (const auto& [name, overloads] : functions) {
        for (const auto& [arity, function] : overloads) function_names.emplace_back(name, arity);
    }
    std::unordered_map<std::string, unsigned> root_function_registers;
    root_function_registers.emplace("tostring", 1);
    const Constant tostring_key{Constant::String, 0, 0, false, "tostring"};
    proto->constants.push_back(tostring_key);
    proto->code.push_back(abc(GETTABUP, 1, 0, 0, 1));
    for (const auto& [name, arity] : function_names)
        root_function_registers.emplace(script_function_key(name, arity), static_cast<unsigned>(root_function_registers.size() + 1));
    for (const auto& [name, arity] : function_names) {
        const auto& function = functions.at(name).at(arity);
        Proto child;
        child.debug_name = name;
        child.params = static_cast<unsigned>(function.arguments.size());
        child.return_type = function.return_type;
        for (const auto& argument : function.arguments) child.parameter_types.push_back(argument.type_name);
        child.upvalues.push_back({false, 0});
        std::unordered_map<std::string, unsigned> child_function_upvalues;
        for (const auto& [function_name, register_index] : root_function_registers) {
            child_function_upvalues.emplace(function_name, static_cast<unsigned>(child.upvalues.size()));
            child.upvalues.push_back({true, register_index});
        }
        FunctionCompiler child_compiler(*this, child, &functions, false, 0, &child_function_upvalues);
        if (!child_compiler.compile_body(function.body, function.arguments, error)) return {};
        proto->children.push_back(std::move(child));
        const std::string closure_name = script_function_key(name, arity);
        const unsigned reg = root_function_registers.at(closure_name);
        proto->code.push_back(abx(CLOSURE, reg, static_cast<unsigned>(proto->children.size() - 1)));
        const Constant key{Constant::String, 0, 0, false, closure_name};
        const unsigned key_index = [&]() { for (unsigned i = 0; i < proto->constants.size(); ++i) if (proto->constants[i].kind == Constant::String && proto->constants[i].string == closure_name) return i; proto->constants.push_back(key); return static_cast<unsigned>(proto->constants.size() - 1); }();
        proto->code.push_back(abc(SETTABUP, 0, key_index, reg, 0));
    }
    FunctionCompiler root_compiler(*this, *proto, &functions, true, static_cast<unsigned>(root_function_registers.size() + 1));
    if (!root_compiler.compile_body(*root, {}, error)) return {};
    chunk = ChunkWriter().write(*proto);
    return proto;
}

struct Table;
union Closure;

/* Lua 5.4 lobject.h: TValue is a tagged value; StackValue keeps the value
    representation separate from stack bookkeeping, as it does in Lua. */
enum LuaType : unsigned char { LUA_VNIL, LUA_VNOVALUE = 0x7f, LUA_VTABLE = 5, LUA_VLCL = 22, LUA_VCCL = 38 };
union Value { std::int64_t i; double n; bool b; const CifaLuaString* str; Table* table; Closure* closure; };
struct TValue { Value value_; unsigned char tt_; };
struct StackValue { TValue val; };
using StkId = StackValue*;
union StkIdRel { StkId p; std::ptrdiff_t offset; };

/* Runtime Proto follows Lua's lobject.h ownership shape. The existing Proto
   remains compiler-only; before execution it is frozen into these contiguous
   code/constant/child arrays and is never read by the interpreter. */
struct RuntimeProto {
    std::string debug_name;
    std::vector<std::string> parameter_types;
    std::string return_type;
    std::vector<std::string> call_frames;
    std::vector<std::string> call_errors;
    std::vector<std::string> value_errors;
    std::vector<bool> create_table_reads;
    unsigned char numparams = 0;
    unsigned char is_vararg = 0;
    unsigned char maxstacksize = 2;
    int sizeupvalues = 0;
    int sizek = 0;
    int sizecode = 0;
    int sizep = 0;
    TValue* k = nullptr;
    std::uint32_t* code = nullptr;
    RuntimeProto** p = nullptr;
    UpvalueDesc* upvalues = nullptr;
};

static TValue make_nil() { return {}; }
static TValue make_novalue(RuntimeProto* source = nullptr) { TValue result; result.value_.closure = reinterpret_cast<Closure*>(source); result.tt_ = LUA_VNOVALUE; return result; }
static TValue make_integer(std::int64_t value) { TValue result; result.value_.i = value; result.tt_ = LUA_VNUMINT; return result; }
static TValue make_number(double value) { TValue result; result.value_.n = value; result.tt_ = LUA_VNUMFLT; return result; }
static TValue make_boolean(bool value) { TValue result; result.value_.b = value; result.tt_ = value ? LUA_VTRUE : LUA_VFALSE; return result; }
static TValue make_string_value(const CifaLuaString* value) { TValue result; result.value_.str = value; result.tt_ = LUA_VSHRSTR; return result; }
static TValue make_table_value(Table* value) { TValue result; result.value_.table = value; result.tt_ = LUA_VTABLE; return result; }
static TValue make_closure_value(Closure* value, bool cclosure) { TValue result; result.value_.closure = value; result.tt_ = cclosure ? LUA_VCCL : LUA_VLCL; return result; }
static bool ttisnil(const TValue& value) { return value.tt_ == LUA_VNIL; }
static bool ttisnovalue(const TValue& value) { return value.tt_ == LUA_VNOVALUE; }
static bool ttisinteger(const TValue& value) { return value.tt_ == LUA_VNUMINT; }
static bool ttisfloat(const TValue& value) { return value.tt_ == LUA_VNUMFLT; }
static bool ttisnumber(const TValue& value) { return ttisinteger(value) || ttisfloat(value); }
static bool ttisstring(const TValue& value) { return value.tt_ == LUA_VSHRSTR; }
static bool ttistable(const TValue& value) { return value.tt_ == LUA_VTABLE; }
static bool ttislclosure(const TValue& value) { return value.tt_ == LUA_VLCL; }
static bool ttiscclosure(const TValue& value) { return value.tt_ == LUA_VCCL; }
static void setobj(TValue& destination, const TValue& source) { destination.value_ = source.value_; destination.tt_ = source.tt_; }

/* Lua 5.4.9 lobject.h/ltable.c table representation.  The table itself
   owns no STL containers: its array and hash parts are raw contiguous
   allocations, and lastfree walks the hash allocation backwards. */
struct Node {
    TValue i_val;
    unsigned char key_tt = LUA_VNIL;
    int next = 0;
    Value key_val{};
};
static_assert(sizeof(TValue) == 16, "Lua TValue layout must remain 16 bytes");
struct Table {
    Table* allnext = nullptr;
    unsigned char flags = 0;
    unsigned char lsizenode = 0;
    bool is_map = false;
    unsigned int alimit = 0;
    std::size_t array_length = 0;
    TValue* array = nullptr;
    Node* node = nullptr;
    Node* lastfree = nullptr;
    Table* metatable = nullptr;
    void* gclist = nullptr;
};
using CFunction = int (*)(class LuaVm&, StkId);
/* Direct Lua 5.4 closure shape: an LClosure points to UpVal objects and a
   CClosure stores its C entry point in the same union-backed allocation. */
struct CifaLuaGCObject { CifaLuaGCObject* next = nullptr; unsigned char tt = 0; unsigned char marked = 0; };
struct UpVal {
    CifaLuaGCObject header;
    union { TValue* p; std::ptrdiff_t offset; } v{};
    union { struct { UpVal* next; UpVal** previous; } open; TValue value; } u{};
    bool to_be_closed = false;
};
struct CClosure {
    CifaLuaGCObject header;
    unsigned char nupvalues = 0;
    CifaLuaGCObject* gclist = nullptr;
    CFunction f = nullptr;
    TValue upvalue[1]{};
};
struct LClosure {
    CifaLuaGCObject header;
    unsigned char nupvalues = 0;
    CifaLuaGCObject* gclist = nullptr;
    RuntimeProto* p = nullptr;
    UpVal* upvals[1]{};
};
union Closure { CClosure c; LClosure l; };
struct NoValueInfo { RuntimeProto* source = nullptr; std::string call_frame; };

/* Lua 5.4 lstate.h fields needed by ldo.c/lvm.c's register interpreter. */
struct CallInfo {
    StkIdRel func{};
    StkIdRel top{};
    CallInfo* previous = nullptr;
    CallInfo* next = nullptr;
    RuntimeProto* p = nullptr;
    std::size_t savedpc = 0;
    int nresults = 0;
    unsigned callstatus = 0;
};
struct CifaLuaState { StkIdRel stack{}; StkIdRel top{}; StkIdRel stack_last{}; CallInfo* ci = nullptr; };

class LuaVm {
public:
    ~LuaVm()
    {
#if defined(CIFA_LUA_OPCODE_PROFILE)
        for (unsigned op = 0; op <= EXTRAARG; ++op) {
            if (opcode_counts_[op] != 0)
                std::fprintf(stderr, "lua_opcode_profile op=%u count=%llu\n", op,
                    static_cast<unsigned long long>(opcode_counts_[op]));
        }
#endif
        free_tables();
        free_table(global_table_);
        free_closures();
        free_upvalues();
        clear_callinfo();
        delete[] stack_;
        for (CifaLuaString* string : strings_) free_lua_string(string);
        clear_string_table();
    }
    TValue run(RuntimeProto& proto, Cifa& owner)
    {
        error_.clear();
        exit_requested_ = false;
        free_tables();
        free_closures();
        free_upvalues();
        state_.ci = nullptr;
        if (!global_table_) {
            global_env_ = new_table();
            global_table_ = global_env_.value_.table;
            tables_ = global_table_->allnext;
            global_table_->allnext = nullptr;
        }
        TValue root = make_lclosure(&proto);
        LClosure* root_closure = &root.value_.closure->l;
        root_closure->upvals[0] = new_upvalue(&global_env_);
        for (unsigned index = 1; index < root_closure->nupvalues; ++index)
            root_closure->upvals[index] = new_closed_upvalue();
        TValue tostring = make_cclosure(&LuaVm::luaB_tostring);
        set_table(global_env_, string_value("tostring"), tostring);
        set_table(global_env_, string_value("type"), make_cclosure(&LuaVm::luaB_type));
        TValue table_library = new_table();
        set_table(table_library, string_value("contains"), make_cclosure(&LuaVm::luaB_table_contains));
        set_table(table_library, string_value("clear"), make_cclosure(&LuaVm::luaB_table_clear));
        set_table(table_library, string_value("erase"), make_cclosure(&LuaVm::luaB_table_erase));
        set_table(table_library, string_value("insert"), make_cclosure(&LuaVm::luaB_table_insert));
        set_table(table_library, string_value("remove"), make_cclosure(&LuaVm::luaB_table_remove));
        set_table(table_library, string_value("resize"), make_cclosure(&LuaVm::luaB_table_resize));
        set_table(table_library, string_value("reserve"), make_cclosure(&LuaVm::luaB_table_reserve));
        set_table(table_library, string_value("keys"), make_cclosure(&LuaVm::luaB_table_keys));
        set_table(global_env_, string_value("table"), table_library);
        for (const auto& [name, value] : owner.registered_globals())
            set_table(global_env_, string_value(name), value_from_object(value));
        host_functions_.clear();
        host_functions_.reserve(owner.registered_functions().size());
        for (const auto& [name, function] : owner.registered_functions()) {
            host_functions_.push_back({&owner, &function, name});
            set_table(global_env_, string_value(name), make_host_closure(&host_functions_.back()));
        }
        set_table(global_env_, string_value("type"), make_cclosure(&LuaVm::luaB_type));
        const std::size_t initial_stack = std::max<unsigned>(proto.maxstacksize + 1, 2);
        if (!stack_) {
            stack_size_ = initial_stack;
            stack_ = new StackValue[stack_size_]{};
        }
        else if (stack_size_ < initial_stack)
            grow_stack(initial_stack);
        refresh_stack();
        setobj(stack_[0].val, root);
        state_.top.p = state_.stack.p + 1;
        precall(state_.stack.p, 1);
        if (!error_.empty()) return make_nil();
        const TValue result = execute();
        sync_globals(owner);
        return result;
    }
    const std::string& error() const { return error_; }
private:
    friend class CifaBytecode;
    friend struct Program;
    CifaLuaState state_;
    std::string error_;
    bool exit_requested_ = false;
    TValue global_env_;
    StackValue* stack_ = nullptr;
    std::size_t stack_size_ = 0;
    std::vector<CifaLuaString*> strings_;
    CifaLuaString** string_hash_ = nullptr;
    std::size_t string_hash_size_ = 0;
    std::size_t string_count_ = 0;
    Table* tables_ = nullptr;
    Table* global_table_ = nullptr;
    Closure* closures_ = nullptr;
    std::vector<UpVal*> upvalues_;
    std::vector<std::unique_ptr<NoValueInfo>> no_values_;
    CallInfo* callinfo_root_ = nullptr;
    struct HostFunction { Cifa* owner; const Cifa::func_type* function; std::string name; };
    std::vector<HostFunction> host_functions_;
#if defined(CIFA_LUA_OPCODE_PROFILE)
    std::array<std::uint64_t, EXTRAARG + 1> opcode_counts_{};
#endif

    static unsigned opcode(std::uint32_t i) { return i & 0x7f; }
    static unsigned arg_a(std::uint32_t i) { return (i >> POS_A) & 0xff; }
    static unsigned arg_b(std::uint32_t i) { return (i >> POS_B) & 0xff; }
    static unsigned arg_c(std::uint32_t i) { return (i >> POS_C) & 0xff; }
    static int arg_sb(std::uint32_t i) { return static_cast<int>(arg_b(i)) - 127; }
    static unsigned arg_bx(std::uint32_t i) { return (i >> POS_Bx) & 0x1ffff; }
    static unsigned arg_ax(std::uint32_t i) { return i >> POS_Ax; }
    static bool arg_k(std::uint32_t i) { return ((i >> POS_k) & 1) != 0; }
    static int arg_sj(std::uint32_t i) { return static_cast<int>(arg_ax(i)) - OFFSET_sJ; }
    static int arg_sc(std::uint32_t i) { return static_cast<int>(arg_c(i)) - 127; }
    static std::size_t hashmod(std::size_t hash, std::size_t size) { return hash & (size - 1); }
    static std::size_t sizenode(const Table& table) { return table.node ? (std::size_t{1} << table.lsizenode) : 0; }
    static constexpr unsigned char BITRAS = 1u << 7;
    static bool isrealasize(const Table& table) { return (table.flags & BITRAS) == 0; }
    static void setrealasize(Table& table) { table.flags &= static_cast<unsigned char>(~BITRAS); }
    static void setnorealasize(Table& table) { table.flags |= BITRAS; }
    static bool ispow2(std::size_t value) { return value != 0 && (value & (value - 1)) == 0; }
    static std::size_t realasize(const Table& table)
    {
        if (isrealasize(table) || ispow2(table.alimit)) return table.alimit;
        std::size_t size = table.alimit;
        size |= size >> 1;
        size |= size >> 2;
        size |= size >> 4;
        size |= size >> 8;
        size |= size >> 16;
        if constexpr (sizeof(std::size_t) > 4) size |= size >> 32;
        return size + 1;
    }
    static std::size_t setlimittosize(Table& table) { table.alimit = static_cast<unsigned int>(realasize(table)); setrealasize(table); return table.alimit; }
    static unsigned char nodekeytag(const Node& node) { return node.key_tt; }
    static void setnodekeytag(Node& node, unsigned char tag) { node.key_tt = tag; }
    static int nodenext(const Node& node) { return node.next; }
    static void setnodenext(Node& node, int next) { node.next = next; }
    static TValue nodekey(const Node& node) { TValue key; key.value_ = node.key_val; key.tt_ = node.key_tt; return key; }
    static void setnodekey(Node& node, const TValue& key) { node.key_val = key.value_; node.key_tt = key.tt_; }
    static bool keyisnil(const Node& node) { return nodekeytag(node) == LUA_VNIL; }
    static unsigned ceillog2(std::size_t value)
    {
        unsigned result = 0;
        --value;
        while (value != 0) { ++result; value >>= 1; }
        return result;
    }
    static bool countint(std::int64_t key, std::array<unsigned, 33>& nums)
    {
        if (key <= 0 || static_cast<std::uint64_t>(key) > UINT_MAX) return false;
        ++nums[ceillog2(static_cast<std::size_t>(key))];
        return true;
    }
    static std::size_t computesizes(const std::array<unsigned, 33>& nums, std::size_t& integer_keys)
    {
        std::size_t candidate = 1;
        std::size_t accumulated = 0;
        std::size_t array_keys = 0;
        std::size_t optimal = 0;
        for (unsigned index = 0; index < nums.size() && integer_keys > candidate / 2; ++index, candidate <<= 1) {
            accumulated += nums[index];
            if (accumulated > candidate / 2) {
                optimal = candidate;
                array_keys = accumulated;
            }
        }
        integer_keys = array_keys;
        return optimal;
    }

    void refresh_stack()
    {
        state_.stack.p = stack_;
        state_.top.p = state_.stack.p;
        state_.stack_last.p = state_.stack.p + stack_size_;
    }
    void clear_callinfo()
    {
        while (callinfo_root_) {
            CallInfo* next = callinfo_root_->next;
            delete callinfo_root_;
            callinfo_root_ = next;
        }
        state_.ci = nullptr;
    }
    CallInfo* next_ci()
    {
        if (!state_.ci && callinfo_root_) return callinfo_root_;
        if (state_.ci && state_.ci->next) return state_.ci->next;
        CallInfo* ci = new CallInfo();
        ci->previous = state_.ci;
        if (state_.ci) state_.ci->next = ci;
        else callinfo_root_ = ci;
        return ci;
    }
    CallInfo* prep_callinfo(StkId func, int nresults, StkId top)
    {
        CallInfo* ci = next_ci();
        ci->func.p = func;
        ci->top.p = top;
        ci->nresults = nresults;
        ci->callstatus = 0;
        ci->savedpc = 0;
        state_.ci = ci;
        return ci;
    }
    void free_closures()
    {
        while (closures_) {
            Closure* closure = closures_;
            closures_ = reinterpret_cast<Closure*>(closure->l.header.next);
            ::operator delete(closure);
        }
    }
    void free_upvalues()
    {
        for (UpVal* upvalue : upvalues_) delete upvalue;
        upvalues_.clear();
    }
    void close_upvalues(TValue* level)
    {
        const TValue* stack_begin = &stack_[0].val;
        const TValue* stack_end = reinterpret_cast<const TValue*>(stack_ + stack_size_);
        for (UpVal* upvalue : upvalues_) {
            TValue* value = upvalue->v.p;
            if (value < stack_begin || value >= stack_end || value < level) continue;
            setobj(upvalue->u.value, *value);
            upvalue->v.p = &upvalue->u.value;
            upvalue->to_be_closed = false;
        }
    }
    void grow_stack(std::size_t needed)
    {
        if (needed <= stack_size_) return;

        /* luaD_reallocstack: change every active stack pointer to an offset,
           move the raw stack allocation, and restore pointers afterwards. */
        const StkId old_stack = state_.stack.p;
        const std::uintptr_t old_begin = reinterpret_cast<std::uintptr_t>(old_stack);
        const std::uintptr_t old_end = old_begin + sizeof(StackValue) * stack_size_;
        state_.top.offset = state_.top.p - old_stack;
        state_.stack_last.offset = state_.stack_last.p - old_stack;
        for (CallInfo* frame = state_.ci; frame; frame = frame->previous) {
            frame->func.offset = frame->func.p - old_stack;
            frame->top.offset = frame->top.p - old_stack;
        }

        StackValue* grown = new StackValue[needed]{};
        std::copy_n(stack_, stack_size_, grown);
        for (UpVal* upvalue : upvalues_) {
            const std::uintptr_t address = reinterpret_cast<std::uintptr_t>(upvalue->v.p);
            if (address >= old_begin && address < old_end) {
                const std::size_t slot = (address - old_begin) / sizeof(StackValue);
                upvalue->v.p = &grown[slot].val;
            }
        }
        delete[] stack_;
        stack_ = grown;
        stack_size_ = needed;
        state_.stack.p = stack_;
        state_.top.p = state_.stack.p + state_.top.offset;
        state_.stack_last.p = state_.stack.p + stack_size_;
        for (CallInfo* frame = state_.ci; frame; frame = frame->previous) {
            frame->func.p = state_.stack.p + frame->func.offset;
            frame->top.p = state_.stack.p + frame->top.offset;
        }
    }
    static std::uint32_t string_hash(const std::string& value)
    {
        std::uint32_t hash = static_cast<std::uint32_t>(value.size());
        for (std::size_t index = value.size(); index > 0; --index)
            hash ^= (hash << 5) + (hash >> 2) + static_cast<unsigned char>(value[index - 1]);
        return hash;
    }
    void clear_string_table()
    {
        delete[] string_hash_;
        string_hash_ = nullptr;
        string_hash_size_ = 0;
        string_count_ = 0;
    }
    void resize_string_table(std::size_t size)
    {
        CifaLuaString** buckets = new CifaLuaString*[size]{};
        for (std::size_t index = 0; index < string_hash_size_; ++index) {
            CifaLuaString* value = string_hash_[index];
            while (value) {
                CifaLuaString* next = value->hnext;
                const std::size_t bucket = value->hash & (size - 1);
                value->hnext = buckets[bucket];
                buckets[bucket] = value;
                value = next;
            }
        }
        string_hash_ = buckets;
        string_hash_size_ = size;
    }
    void add_interned_string(CifaLuaString* value)
    {
        if (string_hash_size_ == 0) resize_string_table(32);
        if (string_count_ >= string_hash_size_) resize_string_table(string_hash_size_ * 2);
        const std::size_t bucket = value->hash & (string_hash_size_ - 1);
        for (CifaLuaString* current = string_hash_[bucket]; current; current = current->hnext)
            if (current == value) return;
        value->hnext = string_hash_[bucket];
        string_hash_[bucket] = value;
        ++string_count_;
    }
    const CifaLuaString* intern(std::string value)
    {
        if (string_hash_size_ == 0) resize_string_table(32);
        const std::uint32_t hash = string_hash(value);
        const std::size_t bucket = hash & (string_hash_size_ - 1);
        for (CifaLuaString* current = string_hash_[bucket]; current; current = current->hnext) {
            if (current->hash == hash && current->length == value.size()
                && std::memcmp(current->contents, value.data(), value.size()) == 0)
                return current;
        }
        CifaLuaString* string = new_lua_string(value);
        string->hash = hash;
        const CifaLuaString* result = string;
        add_interned_string(string);
        strings_.push_back(string);
        return result;
    }
    TValue string_value(std::string value) { return make_string_value(intern(std::move(value))); }
    TValue value_from_object(const Object& value)
    {
        if (value.isType<std::int64_t>()) return make_integer(value.to<std::int64_t>());
        if (value.isType<double>()) return make_number(value.to<double>());
        if (value.isType<bool>()) return make_boolean(value.to<bool>());
        if (value.isType<std::string>()) return string_value(value.to<std::string>());
        if (value.isType<ObjectVector>()) {
            const ObjectVector values = value.to<ObjectVector>();
            TValue table = new_table(values.size());
            for (std::size_t index = 0; index < values.size(); ++index)
                set_table(table, make_integer(static_cast<std::int64_t>(index + 1)), value_from_object(values[index]));
            return table;
        }
        if (value.isType<ObjectMap>()) {
            const ObjectMap values = value.to<ObjectMap>();
            TValue table = new_table(0, values.size());
            table.value_.table->is_map = true;
            for (const auto& [key, item] : values)
                set_table(table, string_value(key), value_from_object(item));
            return table;
        }
        return make_nil();
    }
    Object object_from_value(const TValue& value)
    {
        if (ttisinteger(value)) return Object(value.value_.i);
        if (ttisfloat(value)) return Object(value.value_.n);
        if (value.tt_ == LUA_VTRUE || value.tt_ == LUA_VFALSE) return Object(value.value_.b);
        if (ttisstring(value)) return Object(string_copy(value.value_.str));
        if (ttistable(value)) {
            const Table& table = *value.value_.table;
            if (table.is_map) {
                ObjectMap result;
                for (std::size_t index = 0; index < sizenode(table); ++index) {
                    const Node& node = table.node[index];
                    if (!ttisnil(node.i_val) && ttisstring(nodekey(node)))
                        result.emplace(string_copy(nodekey(node).value_.str), object_from_value(node.i_val));
                }
                return Object(std::move(result));
            }
            ObjectVector result;
            const std::size_t length = getn(*value.value_.table);
            result.reserve(length);
            for (std::size_t index = 1; index <= length; ++index)
                result.push_back(object_from_value(get_table(value, make_integer(static_cast<std::int64_t>(index)))));
            return Object(std::move(result));
        }
        return Object();
    }
    void sync_globals(Cifa& owner)
    {
        if (!global_table_) return;
        const std::size_t hash_size = sizenode(*global_table_);
        for (std::size_t index = 0; index < hash_size; ++index) {
            const Node& node = global_table_->node[index];
            if (!ttisstring(nodekey(node)) || ttisnil(node.i_val) || ttiscclosure(node.i_val) || ttislclosure(node.i_val)) continue;
            owner.update_registered_global(string_copy(nodekey(node).value_.str), object_from_value(node.i_val));
        }
    }
    void import_globals(const Cifa& owner)
    {
        for (const auto& [name, value] : owner.registered_globals())
            set_table(global_env_, string_value(name), value_from_object(value));
    }
    TValue new_table(std::size_t array_size = 0, std::size_t hash_size = 0)
    {
        Table* table = new Table();
        table->allnext = tables_;
        tables_ = table;
        if (array_size != 0 || hash_size != 0) resize(*table, array_size, hash_size);
        return make_table_value(table);
    }
    void free_tables()
    {
        while (tables_) {
            Table* table = tables_;
            tables_ = table->allnext;
            free_table(table);
        }
    }
    static void free_table(Table* table)
    {
        delete[] table->array;
        delete[] table->node;
        delete table;
    }
    UpVal* new_upvalue(TValue* value)
    {
        UpVal* upvalue = new UpVal{};
        upvalue->v.p = value;
        upvalues_.push_back(upvalue);
        return upvalue;
    }
    UpVal* new_closed_upvalue()
    {
        UpVal* upvalue = new UpVal{};
        upvalue->v.p = &upvalue->u.value;
        upvalue->u.value = make_nil();
        upvalues_.push_back(upvalue);
        return upvalue;
    }
    TValue make_lclosure(RuntimeProto* proto)
    {
        const unsigned count = static_cast<unsigned>(proto->sizeupvalues);
        const std::size_t bytes = offsetof(LClosure, upvals) + sizeof(UpVal*) * count;
        Closure* closure = static_cast<Closure*>(::operator new(bytes));
        std::memset(closure, 0, bytes);
        closure->l.header.tt = LUA_VLCL;
        closure->l.nupvalues = static_cast<unsigned char>(count);
        closure->l.p = proto;
        closure->l.header.next = reinterpret_cast<CifaLuaGCObject*>(closures_);
        closures_ = closure;
        return make_closure_value(closure, false);
    }
    TValue make_cclosure(CFunction function)
    {
        Closure* closure = new Closure{};
        closure->c.header.tt = LUA_VCCL;
        closure->c.f = function;
        closure->c.header.next = reinterpret_cast<CifaLuaGCObject*>(closures_);
        closures_ = closure;
        return make_closure_value(closure, true);
    }
    TValue make_host_closure(HostFunction* function)
    {
        TValue closure = make_cclosure(&LuaVm::lua_host_function);
        closure.value_.closure->c.upvalue[0].value_.str = reinterpret_cast<const CifaLuaString*>(function);
        return closure;
    }
    static std::size_t hashkey(const TValue& key)
    {
        if (ttisinteger(key)) return static_cast<std::size_t>(key.value_.i);
        if (ttisstring(key)) return key.value_.str->hash;
        if (ttistable(key)) return reinterpret_cast<std::size_t>(key.value_.table);
        return static_cast<std::size_t>(key.tt_);
    }
    static bool equalobj(const TValue& left, const TValue& right)
    {
        if (left.tt_ == right.tt_) {
            switch (left.tt_) {
            case LUA_VNIL: return true; case LUA_VFALSE: case LUA_VTRUE: return left.value_.b == right.value_.b;
            case LUA_VNUMINT: return left.value_.i == right.value_.i; case LUA_VNUMFLT: return left.value_.n == right.value_.n;
            case LUA_VSHRSTR: return left.value_.str == right.value_.str; case LUA_VTABLE: return left.value_.table == right.value_.table;
            case LUA_VLCL: case LUA_VCCL: return left.value_.closure == right.value_.closure; default: return false;
            }
        }
        return ttisnumber(left) && ttisnumber(right) && number(left) == number(right);
    }
    static double number(const TValue& value)
    {
        if (value.tt_ == LUA_VTRUE || value.tt_ == LUA_VFALSE) return value.tt_ == LUA_VTRUE ? 1.0 : 0.0;
        return ttisinteger(value) ? static_cast<double>(value.value_.i) : value.value_.n;
    }
    static std::int64_t integer(const TValue& value)
    {
        if (value.tt_ == LUA_VTRUE || value.tt_ == LUA_VFALSE) return value.tt_ == LUA_VTRUE ? 1 : 0;
        return ttisinteger(value) ? value.value_.i : static_cast<std::int64_t>(value.value_.n);
    }
    static bool l_isfalse(const TValue& value) { return ttisnil(value) || value.tt_ == LUA_VFALSE; }
    /* The Cifa DSL intentionally treats numeric zero as false. Native Lua truth
       testing is unchanged everywhere except this explicit TEST adaptation. */
    static bool cifa_test_false(const TValue& value) { return l_isfalse(value) || (ttisinteger(value) && value.value_.i == 0) || (ttisfloat(value) && value.value_.n == 0.0); }
    std::string tostring(const TValue& value)
    {
        if (ttisstring(value)) return string_copy(value.value_.str);
        if (ttisinteger(value)) return std::to_string(value.value_.i);
        if (ttisfloat(value)) { char buffer[64]; std::snprintf(buffer, sizeof(buffer), "%.14g", value.value_.n); return buffer; }
        if (value.tt_ == LUA_VTRUE) return "true";
        if (value.tt_ == LUA_VFALSE) return "false";
        return "nil";
    }
    void resize(Table& table, std::size_t nasize, std::size_t nhsize)
    {
        TValue* old_array = table.array;
        Node* old_node = table.node;
        const std::size_t old_asize = setlimittosize(table);
        const std::size_t old_hsize = sizenode(table);

        table.array = nasize == 0 ? nullptr : new TValue[nasize];
        table.alimit = static_cast<unsigned int>(nasize);
        setrealasize(table);
        for (std::size_t index = 0; index < nasize; ++index) setobj(table.array[index], make_nil());
        const std::size_t copied = std::min(old_asize, nasize);
        for (std::size_t index = 0; index < copied; ++index) setobj(table.array[index], old_array[index]);

        std::size_t nodes = 0;
        if (nhsize != 0) { nodes = 1; while (nodes < nhsize) nodes <<= 1; }
        table.node = nodes == 0 ? nullptr : new Node[nodes];
        table.lsizenode = 0;
        while ((std::size_t{1} << table.lsizenode) < nodes) ++table.lsizenode;
        table.lastfree = table.node ? table.node + nodes : nullptr;
        for (std::size_t index = 0; index < nodes; ++index) {
            setobj(table.node[index].i_val, make_nil());
            setnodekeytag(table.node[index], LUA_VNIL);
            setnodenext(table.node[index], 0);
        }

        for (std::size_t index = nasize; index < old_asize; ++index)
            if (!ttisnil(old_array[index])) setobj(*newkey(table, make_integer(static_cast<std::int64_t>(index + 1))), old_array[index]);
        for (std::size_t index = 0; index < old_hsize; ++index)
            if (!ttisnil(old_node[index].i_val)) setobj(*newkey(table, nodekey(old_node[index])), old_node[index].i_val);
        delete[] old_array;
        delete[] old_node;
    }
    void rehash(Table& table, const TValue& extra_key)
    {
        std::array<unsigned, 33> nums{};
        std::size_t integer_keys = 0;
        std::size_t total_keys = 0;
        const std::size_t array_size = setlimittosize(table);
        for (std::size_t index = 0; index < array_size; ++index) {
            if (!ttisnil(table.array[index])) {
                ++total_keys;
                if (countint(static_cast<std::int64_t>(index + 1), nums)) ++integer_keys;
            }
        }
        const std::size_t hash_size = sizenode(table);
        for (std::size_t index = 0; index < hash_size; ++index) {
            if (!ttisnil(table.node[index].i_val)) {
                ++total_keys;
                if (ttisinteger(nodekey(table.node[index])) && countint(nodekey(table.node[index]).value_.i, nums)) ++integer_keys;
            }
        }
        if (ttisinteger(extra_key) && countint(extra_key.value_.i, nums)) ++integer_keys;
        ++total_keys;
        const std::size_t new_array_size = computesizes(nums, integer_keys);
        resize(table, new_array_size, total_keys - integer_keys);
    }
    Node* mainposition(Table& table, const TValue& key) { return table.node + hashmod(hashkey(key), sizenode(table)); }
    Node* getfreepos(Table& table)
    {
        while (table.lastfree != table.node) { Node* node = --table.lastfree; if (keyisnil(*node)) return node; }
        return nullptr;
    }
    TValue* newkey(Table& table, const TValue& key)
    {
        if (!table.node) {
            rehash(table, key);
            if (TValue* slot = getslot(table, key)) return slot;
            return newkey(table, key);
        }
        Node* mp = mainposition(table, key);
        if (ttisnil(mp->i_val)) { setnodekey(*mp, key); return &mp->i_val; }
        Node* free = getfreepos(table);
        if (!free) {
            rehash(table, key);
            if (TValue* slot = getslot(table, key)) return slot;
            return newkey(table, key);
        }
        Node* other = mainposition(table, nodekey(*mp));
        if (other != mp) {
            while (other + nodenext(*other) != mp) other += nodenext(*other);
            setnodenext(*other, static_cast<int>(free - other));
            std::memcpy(free, mp, sizeof(Node));
            if (nodenext(*mp) != 0)
            {
                setnodenext(*free, nodenext(*free) + static_cast<int>(mp - free));
                setnodenext(*mp, 0);
            }
            setnodekey(*mp, key);
            return &mp->i_val;
        }
        if (nodenext(*mp) != 0)
            setnodenext(*free, static_cast<int>((mp + nodenext(*mp)) - free));
        else
            setnodenext(*free, 0);
        setnodenext(*mp, static_cast<int>(free - mp));
        setnodekey(*free, key);
        return &free->i_val;
    }
    TValue* getslot(Table& table, const TValue& key)
    {
        if (ttisinteger(key) && key.value_.i > 0) {
            const std::size_t index = static_cast<std::size_t>(key.value_.i);
            if (index <= table.alimit) return &table.array[index - 1];
            if (!isrealasize(table) && ((index - 1) & ~(static_cast<std::size_t>(table.alimit) - 1)) < table.alimit) {
                table.alimit = static_cast<unsigned int>(index);
                return &table.array[index - 1];
            }
        }
        if (!table.node) return nullptr;
        for (Node* node = mainposition(table, key); node; node = nodenext(*node) ? node + nodenext(*node) : nullptr) if (!ttisnil(node->i_val) && equalobj(nodekey(*node), key)) return &node->i_val;
        return nullptr;
    }
    TValue get_table(const TValue& table, const TValue& key)
    {
        if (!ttistable(table)) { set_error("attempt to index a non-table value (tag=" + std::to_string(table.tt_) + ")"); return make_nil(); }
        TValue* slot = getslot(*table.value_.table, key); return slot ? *slot : make_nil();
    }
    TValue* fastget(const TValue& table, const TValue& key)
    {
        if (!ttistable(table)) return nullptr;
        return getslot(*table.value_.table, key);
    }
    TValue* fastgeti(const TValue& table, std::int64_t key)
    {
        if (!ttistable(table)) return nullptr;
        Table& target = *table.value_.table;
        if (key > 0 && static_cast<std::size_t>(key) <= target.alimit)
            return &target.array[static_cast<std::size_t>(key - 1)];
        TValue index = make_integer(key);
        return getslot(target, index);
    }
    void set_table(const TValue& table, const TValue& key, const TValue& value)
    {
        if (!ttistable(table)) { set_error("attempt to index a non-table value"); return; }
        Table& target = *table.value_.table;
        if (ttisstring(key)) target.is_map = true;
        if (ttisinteger(key) && key.value_.i > 0 && !ttisnil(value))
            target.array_length = std::max(target.array_length, static_cast<std::size_t>(key.value_.i));
        TValue* slot = getslot(target, key);
        if (slot) { setobj(*slot, value); return; }
        if (!ttisnil(value)) setobj(*newkey(target, key), value);
    }
    std::size_t getn(Table& table) const
    {
        if (table.array_length != 0) return table.array_length;
        std::size_t limit = table.alimit;
        if (limit == 0) return 0;
        const auto boundary = [&](std::size_t lower, std::size_t upper) {
            while (upper - lower > 1) {
                const std::size_t middle = lower + (upper - lower) / 2;
                if (ttisnil(table.array[middle - 1])) upper = middle;
                else lower = middle;
            }
            return lower;
        };

        if (limit > 0 && ttisnil(table.array[limit - 1])) {
            if (limit >= 2 && !ttisnil(table.array[limit - 2])) {
                if ((isrealasize(table) || ispow2(table.alimit)) && !ispow2(limit - 1)) {
                    table.alimit = static_cast<unsigned int>(limit - 1);
                    setnorealasize(table);
                }
                return limit - 1;
            }
            const std::size_t result = boundary(0, limit);
            if ((isrealasize(table) || ispow2(table.alimit)) && result > realasize(table) / 2) {
                table.alimit = static_cast<unsigned int>(result);
                setnorealasize(table);
            }
            return result;
        }
        if (!(isrealasize(table) || ispow2(table.alimit))) {
            if (ttisnil(table.array[limit])) return limit;
            const std::size_t size = realasize(table);
            if (ttisnil(table.array[size - 1])) {
                const std::size_t result = boundary(limit, size);
                table.alimit = static_cast<unsigned int>(result);
                return result;
            }
            limit = size;
        }
        return limit;
    }
    void set_error(std::string message) { if (error_.empty()) error_ = std::move(message); }
    void append_script_call_stack()
    {
        if (error_.find("Call Stack (most recent call first):") == std::string::npos)
            error_ += "\nCall Stack (most recent call first):";
        for (CallInfo* frame = state_.ci; frame != nullptr; frame = frame->previous) {
            if (frame->p != nullptr && !frame->p->debug_name.empty())
                error_ += "\n  at func " + frame->p->debug_name + "()";
        }
    }
    bool require_value(const TValue& value)
    {
        if (ttisnil(value)) {
            set_error("value has not been initialized");
            return false;
        }
        if (!ttisnovalue(value)) return true;
        const auto* info = reinterpret_cast<const NoValueInfo*>(value.value_.closure);
        const auto* source = info == nullptr ? nullptr : info->source;
        const std::string& name = source == nullptr || source->debug_name.empty() ? std::string("<unknown>") : source->debug_name;
        set_error("function '" + name + "' has no return value");
        if (info != nullptr && !info->call_frame.empty()) error_ += "\nNo return value originated at:\n" + info->call_frame + "\n";
        return false;
    }
    bool require_value(const TValue& value, const RuntimeProto& proto, std::size_t pc)
    {
        if (pc < proto.value_errors.size() && !proto.value_errors[pc].empty()) {
            const std::string& message = proto.value_errors[pc];
            if (message.rfind("cannot convert value to '", 0) == 0
                && !ttisinteger(value) && !ttisfloat(value)
                && value.tt_ != LUA_VTRUE && value.tt_ != LUA_VFALSE) {
                set_error(message);
                return false;
            }
            if (message.rfind("variable '", 0) == 0) {
                set_error(message);
                return false;
            }
            if (ttisnovalue(value)) {
                set_error(message);
                return false;
            }
        }
        return require_value(value);
    }
    std::string current_call_frame() const
    {
        if (state_.ci == nullptr || state_.ci->p == nullptr || state_.ci->savedpc == 0
            || state_.ci->savedpc > static_cast<std::size_t>(state_.ci->p->sizecode)) return {};
        return state_.ci->p->call_frames[state_.ci->savedpc - 1];
    }
    static void convert_declared_value(TValue& value, const std::string& type_name)
    {
        if (ttisnovalue(value) || ttisnil(value)) return;
        if (type_name == "int" && (value.tt_ == LUA_VTRUE || value.tt_ == LUA_VFALSE)) value = make_integer(value.tt_ == LUA_VTRUE ? 1 : 0);
        else if (type_name == "int" && ttisfloat(value)) value = make_integer(static_cast<std::int64_t>(value.value_.n));
        else if ((type_name == "double" || type_name == "float") && ttisinteger(value)) value = make_number(static_cast<double>(value.value_.i));
        else if (type_name == "bool") value = make_boolean(!cifa_test_false(value));
    }
    bool precall(StkId func, int nresults)
    {
        TValue& callable = func->val;
        if (ttiscclosure(callable)) {
            const int results = callable.value_.closure->c.f(*this, func);
            if (results != 1) { set_error("Lua C closure returned an unsupported result count"); return false; }
            state_.top.p = func + 1;
            return true;
        }
        if (!ttislclosure(callable)) { set_error("attempt to call a non-function value"); return false; }
        LClosure* closure = &callable.value_.closure->l;
        const std::size_t function_slot = static_cast<std::size_t>(func - state_.stack.p);
        const std::size_t base = function_slot + 1;
        const int nargs = static_cast<int>(state_.top.p - func) - 1;
        const std::size_t needed = base + std::max<unsigned>(closure->p->maxstacksize, static_cast<unsigned>(nargs));
        grow_stack(needed);
        func = state_.stack.p + function_slot;
        for (int index = nargs; index < static_cast<int>(closure->p->numparams); ++index)
            setobj(state_.stack.p[base + index].val, make_nil());
        for (std::size_t index = 0; index < closure->p->parameter_types.size(); ++index)
            convert_declared_value(state_.stack.p[base + index].val, closure->p->parameter_types[index]);
        CallInfo* ci = prep_callinfo(func, nresults, state_.stack.p + base + closure->p->maxstacksize);
        ci->p = closure->p;
        return false;
    }
    bool poscall(TValue result)
    {
        CallInfo* finished = state_.ci;
        const std::size_t result_slot = static_cast<std::size_t>(finished->func.p - state_.stack.p);
        const int wanted = finished->nresults;
        convert_declared_value(result, finished->p->return_type);
        if (ttisnovalue(result)) {
            auto info = std::make_unique<NoValueInfo>();
            info->source = reinterpret_cast<RuntimeProto*>(result.value_.closure);
            if (finished->previous != nullptr && finished->previous->savedpc > 0
                && finished->previous->savedpc <= static_cast<std::size_t>(finished->previous->p->sizecode))
                info->call_frame = finished->previous->p->call_frames[finished->previous->savedpc - 1];
            result.value_.closure = reinterpret_cast<Closure*>(info.get());
            no_values_.push_back(std::move(info));
        }
        if (wanted > 0) setobj(stack_[result_slot].val, result);
        state_.top.p = state_.stack.p + result_slot + std::max(wanted, 0);
        state_.ci = finished->previous;
        if (!state_.ci) return true;
        return false;
    }
    static int luaB_tostring(LuaVm& vm, StkId func)
    {
        func->val = vm.string_value(vm.tostring((func + 1)->val));
        return 1;
    }
    static int luaB_type(LuaVm& vm, StkId func)
    {
        const TValue& value = (func + 1)->val;
        if (ttisinteger(value)) func->val = vm.string_value("int");
        else if (ttisfloat(value)) func->val = vm.string_value("double");
        else if (value.tt_ == LUA_VTRUE || value.tt_ == LUA_VFALSE) func->val = vm.string_value("bool");
        else if (ttisstring(value)) func->val = vm.string_value("string");
        else if (ttistable(value)) {
            const TValue* type = vm.getslot(*value.value_.table, vm.string_value("__cifa_struct_type"));
            func->val = type && ttisstring(*type) ? vm.string_value(string_copy(type->value_.str))
                : vm.string_value(value.value_.table->is_map ? "map" : "array");
        }
        else if (ttisnovalue(value)) func->val = vm.string_value("NoValue");
        else func->val = vm.string_value("null");
        return 1;
    }
    static std::size_t table_entry_count(const Table& table)
    {
        std::size_t count = 0;
        for (std::size_t index = 0; index < realasize(table); ++index)
            if (!ttisnil(table.array[index])) ++count;
        for (std::size_t index = 0; index < sizenode(table); ++index)
            if (!ttisnil(table.node[index].i_val)) ++count;
        return count;
    }
    static int luaB_table_contains(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        const TValue& needle = (func + 2)->val;
        if (!ttistable(receiver)) { vm.set_error("contains() requires an array or map"); return 0; }
        const Table& table = *receiver.value_.table;
        bool found = false;
        if (table.is_map) found = vm.getslot(*receiver.value_.table, needle) != nullptr;
        else {
            for (std::size_t index = 0; index < realasize(table) && !found; ++index)
                found = !ttisnil(table.array[index]) && equalobj(table.array[index], needle);
            for (std::size_t index = 0; index < sizenode(table) && !found; ++index)
                found = !ttisnil(table.node[index].i_val) && equalobj(table.node[index].i_val, needle);
        }
        func->val = make_boolean(found);
        return 1;
    }
    static int luaB_table_clear(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        if (!ttistable(receiver)) { vm.set_error("clear() requires an array or map"); return 0; }
        Table& table = *receiver.value_.table;
        for (std::size_t index = 0; index < realasize(table); ++index) setobj(table.array[index], make_nil());
        for (std::size_t index = 0; index < sizenode(table); ++index) setobj(table.node[index].i_val, make_nil());
        table.array_length = 0;
        func->val = make_integer(0);
        return 1;
    }
    static int luaB_table_erase(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        const TValue& key = (func + 2)->val;
        if (!ttistable(receiver)) { vm.set_error("erase() requires an array or map"); return 0; }
        Table& table = *receiver.value_.table;
        if (table.is_map) {
            if (TValue* slot = vm.getslot(table, key)) setobj(*slot, make_nil());
        } else if (ttisinteger(key) && key.value_.i >= 0) {
            const std::size_t index = static_cast<std::size_t>(key.value_.i);
            const std::size_t length = vm.getn(table);
            if (index < length) {
                for (std::size_t current = index; current + 1 < length; ++current)
                    setobj(table.array[current], table.array[current + 1]);
                setobj(table.array[length - 1], make_nil());
                table.array_length = length - 1;
            }
        }
        func->val = make_integer(static_cast<std::int64_t>(table_entry_count(table)));
        return 1;
    }
    static int luaB_table_insert(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        const TValue& position = (func + 2)->val;
        const TValue& value = (func + 3)->val;
        if (!ttistable(receiver) || receiver.value_.table->is_map) { vm.set_error("insert() requires an array"); return 0; }
        Table& table = *receiver.value_.table;
        const std::size_t length = vm.getn(table);
        const std::size_t index = ttisinteger(position) && position.value_.i > 0
            ? std::min<std::size_t>(static_cast<std::size_t>(position.value_.i), length) : 0;
        for (std::size_t current = length; current > index; --current)
            vm.set_table(receiver, make_integer(static_cast<std::int64_t>(current + 1)), vm.get_table(receiver, make_integer(static_cast<std::int64_t>(current))));
        vm.set_table(receiver, make_integer(static_cast<std::int64_t>(index + 1)), value);
        func->val = make_integer(static_cast<std::int64_t>(length + 1));
        return 1;
    }
    static int luaB_table_remove(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        const TValue& position = (func + 2)->val;
        if (!ttistable(receiver) || receiver.value_.table->is_map) { vm.set_error("remove() requires an array"); return 0; }
        Table& table = *receiver.value_.table;
        const std::size_t length = vm.getn(table);
        if (!ttisinteger(position) || position.value_.i <= 0 || static_cast<std::size_t>(position.value_.i) > length) {
            func->val = make_nil();
            return 1;
        }
        const std::size_t index = static_cast<std::size_t>(position.value_.i);
        const TValue removed = vm.get_table(receiver, position);
        for (std::size_t current = index; current < length; ++current)
            vm.set_table(receiver, make_integer(static_cast<std::int64_t>(current)),
                vm.get_table(receiver, make_integer(static_cast<std::int64_t>(current + 1))));
        vm.set_table(receiver, make_integer(static_cast<std::int64_t>(length)), make_nil());
        table.array_length = length - 1;
        func->val = removed;
        return 1;
    }
    static int luaB_table_resize(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        const TValue& requested_size = (func + 2)->val;
        if (!ttistable(receiver) || receiver.value_.table->is_map) { vm.set_error("resize() requires an array"); return 0; }
        Table& table = *receiver.value_.table;
        const std::size_t size = ttisinteger(requested_size) && requested_size.value_.i > 0
            ? static_cast<std::size_t>(requested_size.value_.i) : 0;
        if (size > realasize(table)) vm.resize(table, size, sizenode(table));
        if (size < table.array_length)
            for (std::size_t index = size; index < table.array_length; ++index) setobj(table.array[index], make_nil());
        table.array_length = size;
        func->val = make_integer(static_cast<std::int64_t>(size));
        return 1;
    }
    static int luaB_table_reserve(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        const TValue& requested_capacity = (func + 2)->val;
        if (!ttistable(receiver) || receiver.value_.table->is_map) { vm.set_error("reserve() requires an array"); return 0; }
        Table& table = *receiver.value_.table;
        const std::size_t capacity = ttisinteger(requested_capacity) && requested_capacity.value_.i > 0
            ? static_cast<std::size_t>(requested_capacity.value_.i) : 0;
        if (capacity > realasize(table)) vm.resize(table, capacity, sizenode(table));
        func->val = make_integer(static_cast<std::int64_t>(vm.getn(table)));
        return 1;
    }
    static int luaB_table_keys(LuaVm& vm, StkId func)
    {
        const TValue& receiver = (func + 1)->val;
        if (!ttistable(receiver) || !receiver.value_.table->is_map) { vm.set_error("keys() requires a map"); return 0; }
        const Table& table = *receiver.value_.table;
        TValue result = vm.new_table();
        std::size_t index = 1;
        for (std::size_t node_index = 0; node_index < sizenode(table); ++node_index) {
            const Node& node = table.node[node_index];
            if (!ttisnil(node.i_val)) vm.set_table(result, make_integer(static_cast<std::int64_t>(index++)), nodekey(node));
        }
        func->val = result;
        return 1;
    }
    static int lua_host_function(LuaVm& vm, StkId func)
    {
        const auto* handle = reinterpret_cast<const HostFunction*>(func->val.value_.closure->c.upvalue[0].value_.str);
        if (!handle || !handle->function) {
            vm.set_error("Lua VM host function handle is invalid");
            return 0;
        }
        ObjectVector arguments;
        const std::ptrdiff_t count = vm.state_.top.p - func - 1;
        arguments.reserve(static_cast<std::size_t>(std::max<std::ptrdiff_t>(count, 0)));
        for (std::ptrdiff_t index = 0; index < count; ++index) {
            const TValue& argument = (func + 1 + index)->val;
            if (!vm.require_value(argument)) {
                const std::string frame = vm.current_call_frame();
                if (!frame.empty() && vm.error_.find(frame) == std::string::npos)
                    vm.error_ += "\nCall Stack (most recent call first):\n  at " + frame;
                else vm.error_ += "\nCall Stack (most recent call first):\n";
                return 0;
            }
            arguments.push_back(vm.object_from_value(argument));
        }
        static const std::unordered_set<std::string> numeric_functions = {
            "abs", "sqrt", "cbrt", "exp", "exp2", "log", "log10", "log2", "sin", "cos", "tan",
            "asin", "acos", "atan", "sinh", "cosh", "tanh", "ceil", "floor", "round", "trunc",
            "pow", "hypot", "atan2", "fmod", "remainder", "copysign", "fdim", "fmax", "fmin"
        };
        if (numeric_functions.contains(handle->name)) {
            for (const Object& argument : arguments) {
                if (!argument.isNumber()) {
                    vm.set_error("type conversion failed");
                    vm.append_script_call_stack();
                    return 0;
                }
            }
        }
        vm.sync_globals(*handle->owner);
        Object result = (*handle->function)(arguments);
        if (handle->owner->has_runtime_error()) {
            vm.set_error(handle->owner->get_runtime_error());
            vm.append_script_call_stack();
            return 0;
        }
        vm.import_globals(*handle->owner);
        if (handle->owner->is_exit_requested()) vm.exit_requested_ = true;
        func->val = vm.value_from_object(result);
        return 1;
    }
    TValue execute()
    {
        CallInfo* ci;
        LClosure* cl;
        const Constant* k;
        StkId base;
        const std::uint32_t* pc;
    startfunc:
        if (!state_.ci) return stack_[0].val;
        {
            ci = state_.ci;
            cl = &ci->func.p->val.value_.closure->l;
            k = nullptr;
            pc = cl->p->code + ci->savedpc;
            base = ci->func.p + 1;
        }
        for (;;) {
            if (exit_requested_) return make_nil();
            if (pc >= cl->p->code + cl->p->sizecode) { set_error("Lua VM reached end of Proto"); return make_nil(); }
            const std::uint32_t instruction = *pc++;
            const unsigned op = opcode(instruction);
#if defined(CIFA_LUA_OPCODE_PROFILE)
            ++opcode_counts_[op];
#endif
            switch (op) {
            case MOVE: setobj(base[arg_a(instruction)].val, base[arg_b(instruction)].val); break;
            case LOADI: base[arg_a(instruction)].val = make_integer(static_cast<int>(arg_bx(instruction)) - 65535); break;
            case LOADK: setobj(base[arg_a(instruction)].val, cl->p->k[arg_bx(instruction)]); break;
            case GETUPVAL: setobj(base[arg_a(instruction)].val, *cl->upvals[arg_b(instruction)]->v.p); break;
            case SETUPVAL: setobj(*cl->upvals[arg_b(instruction)]->v.p, base[arg_a(instruction)].val); break;
            case TBC: {
                UpVal* upvalue = new_upvalue(&base[arg_a(instruction)].val);
                upvalue->to_be_closed = true;
                break;
            }
            case CLOSE: close_upvalues(&base[arg_a(instruction)].val); break;
            case LOADNIL: {
                const unsigned a = arg_a(instruction);
                for (unsigned index = 0; index <= arg_b(instruction); ++index) setobj(base[a + index].val, make_nil());
                break;
            }
            case GETTABUP: {
                TValue& ra = base[arg_a(instruction)].val;
                const TValue& upvalue = *cl->upvals[0]->v.p;
                const TValue& key = cl->p->k[arg_c(instruction)];
                if (TValue* slot = fastget(upvalue, key); slot && !ttisnil(*slot)) setobj(ra, *slot);
                else setobj(ra, get_table(upvalue, key));
                break;
            }
            case SETTABUP: {
                TValue& upvalue = *cl->upvals[0]->v.p;
                const TValue& key = cl->p->k[arg_b(instruction)];
                const TValue& rc = base[arg_c(instruction)].val;
                if (TValue* slot = fastget(upvalue, key); slot && !ttisnil(*slot)) setobj(*slot, rc);
                else set_table(upvalue, key, rc);
                break;
            }
            case GETTABLE: {
                TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                const TValue& rc = base[arg_c(instruction)].val;
                TValue* slot = ttisinteger(rc) ? fastgeti(rb, rc.value_.i) : fastget(rb, rc);
                if (slot && !ttisnil(*slot)) setobj(ra, *slot);
                else {
                    const std::size_t instruction_pc = static_cast<std::size_t>(pc - cl->p->code - 1);
                    if (instruction_pc < cl->p->create_table_reads.size() && cl->p->create_table_reads[instruction_pc]) {
                        TValue table = new_table();
                        set_table(rb, rc, table);
                        setobj(ra, table);
                    } else setobj(ra, get_table(rb, rc));
                }
                break;
            }
            case GETI: {
                TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                const TValue key = make_integer(arg_c(instruction));
                if (TValue* slot = fastgeti(rb, key.value_.i); slot && !ttisnil(*slot)) setobj(ra, *slot);
                else setobj(ra, get_table(rb, key));
                break;
            }
            case GETFIELD: {
                TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                const TValue& key = cl->p->k[arg_c(instruction)];
                if (TValue* slot = fastget(rb, key); slot && !ttisnil(*slot)) setobj(ra, *slot);
                else setobj(ra, get_table(rb, key));
                break;
            }
            case SETTABLE: {
                TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                const TValue& rc = base[arg_c(instruction)].val;
                TValue* slot = ttisinteger(rb) ? fastgeti(ra, rb.value_.i) : fastget(ra, rb);
                if (slot && !ttisnil(*slot)) setobj(*slot, rc);
                else set_table(ra, rb, rc);
                break;
            }
            case SETI: set_table(base[arg_a(instruction)].val, make_integer(arg_b(instruction)), base[arg_c(instruction)].val); break;
            case SETFIELD: set_table(base[arg_a(instruction)].val, cl->p->k[arg_b(instruction)], base[arg_c(instruction)].val); break;
            case NEWTABLE: {
                const unsigned b = arg_b(instruction);
                std::size_t hash_size = b == 0 ? 0 : (std::size_t{1} << (b - 1));
                std::size_t array_size = arg_c(instruction);
                if (arg_k(instruction)) array_size += static_cast<std::size_t>(arg_ax(*pc)) * 256;
                ++pc;  /* Lua OP_NEWTABLE always consumes EXTRAARG. */
                base[arg_a(instruction)].val = new_table(array_size, hash_size);
                break;
            }
            case LEN: {
                TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                if (!require_value(rb)) return make_nil();
                if (ttistable(rb)) {
                    Table& table = *rb.value_.table;
                    ra = make_integer(static_cast<std::int64_t>(table.is_map ? table_entry_count(table) : getn(table)));
                }
                else if (ttisstring(rb)) ra = make_integer(static_cast<std::int64_t>(rb.value_.str->length));
                else set_error("function 'size' requires a string, array, or map");
                break;
            }
            case CONCAT: {
                const unsigned a = arg_a(instruction);
                std::string value;
                for (unsigned index = 0; index < arg_b(instruction); ++index) value += tostring(base[a + index].val);
                base[a].val = string_value(std::move(value));
                break;
            }
            case UNM: {
                const TValue& value = base[arg_b(instruction)].val;
                base[arg_a(instruction)].val = ttisfloat(value) ? make_number(-value.value_.n) : make_integer(-integer(value));
                break;
            }
            case BNOT: base[arg_a(instruction)].val = make_integer(~integer(base[arg_b(instruction)].val)); break;
            case NOT:
                if (!require_value(base[arg_b(instruction)].val, *cl->p, static_cast<std::size_t>(pc - cl->p->code - 1))) return make_nil();
                base[arg_a(instruction)].val = make_boolean(cifa_test_false(base[arg_b(instruction)].val)); break;
            case ADDI:
                if (!require_value(base[arg_b(instruction)].val)) return make_nil();
                base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) + arg_sc(instruction)); break;
            case ADDK: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) + integer(cl->p->k[arg_c(instruction)])); break;
            case SUBK: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) - integer(cl->p->k[arg_c(instruction)])); break;
            case MULK: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) * integer(cl->p->k[arg_c(instruction)])); break;
            case MODK: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) % integer(cl->p->k[arg_c(instruction)])); break;
            case DIVK: base[arg_a(instruction)].val = make_number(number(base[arg_b(instruction)].val) / number(cl->p->k[arg_c(instruction)])); break;
            case IDIVK:
                if (number(cl->p->k[arg_c(instruction)]) == 0.0) { set_error("integer division by zero"); return make_nil(); }
                base[arg_a(instruction)].val = make_integer(static_cast<std::int64_t>(
                    number(base[arg_b(instruction)].val) / number(cl->p->k[arg_c(instruction)]))); break;
            case ADD: {
                const TValue& left = base[arg_b(instruction)].val; const TValue& right = base[arg_c(instruction)].val;
                if (!require_value(left) || !require_value(right)) return make_nil();
                base[arg_a(instruction)].val = ttisfloat(left) || ttisfloat(right)
                    ? make_number(number(left) + number(right)) : make_integer(integer(left) + integer(right)); break;
            }
            case SUB: {
                const TValue& left = base[arg_b(instruction)].val; const TValue& right = base[arg_c(instruction)].val;
                if (!require_value(left) || !require_value(right)) return make_nil();
                base[arg_a(instruction)].val = ttisfloat(left) || ttisfloat(right)
                    ? make_number(number(left) - number(right)) : make_integer(integer(left) - integer(right)); break;
            }
            case MUL: {
                const TValue& left = base[arg_b(instruction)].val; const TValue& right = base[arg_c(instruction)].val;
                if (!require_value(left) || !require_value(right)) return make_nil();
                base[arg_a(instruction)].val = ttisfloat(left) || ttisfloat(right)
                    ? make_number(number(left) * number(right)) : make_integer(integer(left) * integer(right)); break;
            }
            case MOD: {
                const TValue& left = base[arg_b(instruction)].val; const TValue& right = base[arg_c(instruction)].val;
                if (!require_value(left) || !require_value(right)) return make_nil();
                base[arg_a(instruction)].val = ttisfloat(left) || ttisfloat(right)
                    ? make_number(std::fmod(number(left), number(right))) : make_integer(integer(left) % integer(right)); break;
            }
            case DIV: {
                const TValue& left = base[arg_b(instruction)].val; const TValue& right = base[arg_c(instruction)].val;
                if (!require_value(left) || !require_value(right)) return make_nil();
                if (!ttisfloat(left) && !ttisfloat(right) && integer(right) == 0) { set_error("integer division by zero"); return make_nil(); }
                base[arg_a(instruction)].val = ttisfloat(left) || ttisfloat(right)
                    ? make_number(number(left) / number(right)) : make_integer(integer(left) / integer(right)); break;
            }
            case IDIV:
                if (integer(base[arg_c(instruction)].val) == 0) { set_error("integer division by zero"); return make_nil(); }
                base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) / integer(base[arg_c(instruction)].val)); break;
            case BAND: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) & integer(base[arg_c(instruction)].val)); break;
            case BOR: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) | integer(base[arg_c(instruction)].val)); break;
            case BXOR: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) ^ integer(base[arg_c(instruction)].val)); break;
            case SHL: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) << integer(base[arg_c(instruction)].val)); break;
            case SHR: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) >> integer(base[arg_c(instruction)].val)); break;
            case JMP: pc += arg_sj(instruction); break;
            case FORPREP: {
                const unsigned a = arg_a(instruction);
                const std::int64_t initial = integer(base[a].val);
                const std::int64_t limit = integer(base[a + 1].val);
                const std::int64_t step = integer(base[a + 2].val);
                base[a + 3].val = make_integer(initial);
                if ((step > 0 && initial > limit) || (step < 0 && initial < limit)) pc += arg_bx(instruction) + 1;
                else base[a + 1].val = make_integer(step > 0 ? limit - initial : initial - limit);
                break;
            }
            case FORLOOP: {
                const unsigned a = arg_a(instruction);
                const std::int64_t count = integer(base[a + 1].val);
                if (count > 0) {
                    base[a + 1].val = make_integer(count - 1);
                    const std::int64_t index = integer(base[a].val) + integer(base[a + 2].val);
                    base[a].val = make_integer(index);
                    base[a + 3].val = make_integer(index);
                    pc -= arg_bx(instruction);
                }
                break;
            }
            case EQ: if (equalobj(base[arg_a(instruction)].val, base[arg_b(instruction)].val) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case EQI: if ((integer(base[arg_a(instruction)].val) == arg_sb(instruction)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case LT: {
                const TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                if ((ttisstring(ra) && ttisstring(rb) ? string_copy(ra.value_.str) < string_copy(rb.value_.str) : number(ra) < number(rb)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1;
                break;
            }
            case LTI: if ((number(base[arg_a(instruction)].val) < arg_sb(instruction)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case LE: {
                const TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                if ((ttisstring(ra) && ttisstring(rb) ? string_copy(ra.value_.str) <= string_copy(rb.value_.str) : number(ra) <= number(rb)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1;
                break;
            }
            case LEI: if ((number(base[arg_a(instruction)].val) <= arg_sb(instruction)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case TEST:
                if (!require_value(base[arg_a(instruction)].val)) return make_nil();
                if ((!cifa_test_false(base[arg_a(instruction)].val)) != !arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case CLOSURE: {
                RuntimeProto* child = cl->p->p[arg_bx(instruction)];
                if (!child || !child->code) { set_error("Lua VM has an invalid child Proto"); return make_nil(); }
                TValue child_closure = make_lclosure(child);
                LClosure* captured = &child_closure.value_.closure->l;
                for (unsigned index = 0; index < captured->nupvalues; ++index) {
                    const UpvalueDesc& upvalue = child->upvalues[index];
                    if (upvalue.instack) captured->upvals[index] = new_upvalue(&base[upvalue.index].val);
                    else if (upvalue.index < cl->nupvalues) captured->upvals[index] = cl->upvals[upvalue.index];
                    else { set_error("Lua VM closure has an invalid upvalue descriptor"); return make_nil(); }
                }
                base[arg_a(instruction)].val = child_closure;
                break;
            }
            case CALL: {
                const unsigned a = arg_a(instruction);
                const unsigned b = arg_b(instruction);
                if (b != 0) state_.top.p = base + a + b;
                ci->savedpc = static_cast<std::size_t>(pc - cl->p->code);
                if (ci->savedpc > 0 && ci->savedpc <= cl->p->call_errors.size()
                    && !cl->p->call_errors[ci->savedpc - 1].empty()) {
                    set_error(cl->p->call_errors[ci->savedpc - 1]);
                    return make_nil();
                }
                if (ttiscclosure(base[a].val)) { if (!precall(base + a, 1)) return make_nil(); }
                else if (!precall(base + a, 1)) goto startfunc;
                break;
            }
            case RETURN0: close_upvalues(&base[0].val); if (poscall(make_novalue(cl->p))) return stack_[0].val; goto startfunc;
            case RETURN1: close_upvalues(&base[0].val); if (poscall(base[arg_a(instruction)].val)) return stack_[0].val; goto startfunc;
            case MMBIN: case MMBINI: case MMBINK: case EXTRAARG: break;
            default: set_error("Lua VM encountered an unsupported opcode"); return make_nil();
            }
            if (!error_.empty()) return make_nil();
        }
    }
};

struct Program {
    RuntimeProto root;
    std::vector<std::unique_ptr<RuntimeProto>> children;
    LuaVm vm;

    ~Program() { free_proto(root); }

    static void free_proto(RuntimeProto& proto)
    {
        delete[] proto.k;
        delete[] proto.code;
        delete[] proto.p;
        delete[] proto.upvalues;
        proto = {};
    }

    RuntimeProto* freeze(const Proto& source, RuntimeProto& target)
    {
        target.debug_name = source.debug_name;
        target.parameter_types = source.parameter_types;
        target.return_type = source.return_type;
        target.numparams = static_cast<unsigned char>(source.params);
        target.is_vararg = static_cast<unsigned char>(source.vararg);
        target.maxstacksize = static_cast<unsigned char>(source.maxstack);
        target.sizeupvalues = static_cast<int>(source.upvalues.size());
        target.sizecode = static_cast<int>(source.code.size());
        target.sizek = static_cast<int>(source.constants.size());
        target.sizep = static_cast<int>(source.children.size());
        target.call_frames.assign(target.sizecode, {});
        for (const auto& [pc, frame] : source.call_frames)
            if (pc < target.call_frames.size()) target.call_frames[pc] = frame;
        target.call_errors.assign(target.sizecode, {});
        for (const auto& [pc, message] : source.call_errors)
            if (pc < target.call_errors.size()) target.call_errors[pc] = message;
        target.value_errors.assign(target.sizecode, {});
        for (const auto& [pc, message] : source.value_errors)
            if (pc < target.value_errors.size()) target.value_errors[pc] = message;
        target.create_table_reads.assign(target.sizecode, false);
        for (const std::size_t pc : source.create_table_reads)
            if (pc < target.create_table_reads.size()) target.create_table_reads[pc] = true;
        target.code = target.sizecode == 0 ? nullptr : new std::uint32_t[target.sizecode];
        std::copy(source.code.begin(), source.code.end(), target.code);
        target.k = target.sizek == 0 ? nullptr : new TValue[target.sizek];
        for (int index = 0; index < target.sizek; ++index) {
            const Constant& constant = source.constants[static_cast<std::size_t>(index)];
            switch (constant.kind) {
            case Constant::Integer: target.k[index] = make_integer(constant.integer); break;
            case Constant::Number: target.k[index] = make_number(constant.number); break;
            case Constant::String: target.k[index] = make_string_value(vm.intern(constant.string)); break;
            case Constant::Boolean: target.k[index] = make_boolean(constant.boolean); break;
            }
        }
        target.p = target.sizep == 0 ? nullptr : new RuntimeProto*[target.sizep];
        target.upvalues = target.sizeupvalues == 0 ? nullptr : new UpvalueDesc[target.sizeupvalues];
        std::copy(source.upvalues.begin(), source.upvalues.end(), target.upvalues);
        for (int index = 0; index < target.sizep; ++index) {
            auto child = std::make_unique<RuntimeProto>();
            RuntimeProto* child_ptr = child.get();
            children.push_back(std::move(child));
            target.p[index] = freeze(source.children[static_cast<std::size_t>(index)], *child_ptr);
        }
        return &target;
    }

    void initialize(const Proto& source) { freeze(source, root); }
};

bool CifaBytecode::compile_script(std::string script)
{
    compiled_ = false;
    translation_error_.clear();
    runtime_error_.clear();
    chunk_.clear();
    return compile_script_internal(std::move(script)) && emit_chunk();
}

bool CifaBytecode::compile_file(const std::string& filename)
{
    compiled_ = false;
    translation_error_.clear();
    runtime_error_.clear();
    chunk_.clear();
    return compile_file_internal(filename) && emit_chunk();
}

bool CifaBytecode::emit_chunk()
{
    auto proto = compile_lua_program(chunk_, translation_error_);
    if (!proto) return false;
    auto program = std::make_shared<Program>();
    program->initialize(*proto);
    program_ = std::move(program);
    persist_compiled_script_functions();
    persist_compiled_struct_definitions();
    compiled_ = true;
    return true;
}

Object CifaBytecode::run(const std::string& entry_label)
{
    runtime_error_.clear();
    if (!entry_label.empty())
    {
        runtime_error_ = "named bytecode entry points are not supported by the Lua backend";
        return Object("", "Error");
    }
    if (!valid())
    {
        runtime_error_ = translation_error_.empty() ? "CifaBytecode has not been compiled" : translation_error_;
        return Object("", "Error");
    }
    return execute_chunk();
}

Object CifaBytecode::run_script(std::string script)
{
    return compile_script(std::move(script)) ? run() : Object("", "Error");
}

Object CifaBytecode::run_file(const std::string& filename)
{
    return compile_file(filename) ? run() : Object("", "Error");
}

Object CifaBytecode::execute_chunk()
{
    const auto program = std::static_pointer_cast<Program>(program_);
    if (!program || !program->root.code) return Object("", "Error");
    const TValue result = program->vm.run(program->root, *this);
    runtime_error_ = program->vm.error();
    if (runtime_error_.empty() && ttisnovalue(result)) {
        const auto* info = reinterpret_cast<const NoValueInfo*>(result.value_.closure);
        const std::string name = info == nullptr || info->source == nullptr || info->source->debug_name.empty()
            ? "<unknown>" : info->source->debug_name;
        return Object::make_no_value(name, info == nullptr ? std::string{} : info->call_frame);
    }
    return runtime_error_.empty() ? program->vm.object_from_value(result) : Object("", "Error");
}

} // namespace cifa
