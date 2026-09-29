#include "CifaBytecode.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <memory>
#include <optional>
#include <unordered_map>

namespace cifa {
namespace {

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

struct Proto {
    std::string source;
    unsigned linedefined = 0;
    unsigned lastline = 0;
    unsigned params = 0;
    unsigned vararg = 0;
    unsigned maxstack = 2;
    std::vector<std::uint32_t> code;
    std::vector<Constant> constants;
    std::vector<Proto> children;
    std::vector<unsigned> upvalue_names;
};

class FunctionCompiler {
public:
    FunctionCompiler(const Cifa& owner, Proto& proto, const std::unordered_map<std::string, FunctionOverloads>* functions,
        bool root = false)
        : owner_(owner), proto_(proto), functions_(functions), root_(root) {}

    bool compile_body(const CalUnit& body, const std::vector<Function2::Argument>& args, std::string& error)
    {
        for (const auto& arg : args) local(arg.name);
        if (!emit_block(body, error)) return false;
        if (proto_.code.empty() || (proto_.code.back() & 0x7f) != RETURN0 && (proto_.code.back() & 0x7f) != RETURN1)
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
    const Cifa& owner_;
    Proto& proto_;
    const std::unordered_map<std::string, FunctionOverloads>* functions_;
    bool root_ = false;
    std::unordered_map<std::string, unsigned> locals_;
    std::vector<std::vector<unsigned>> loop_breaks_;
    unsigned next_register_ = root_ ? 1u : 0u;

    unsigned local(const std::string& name)
    {
        auto it = locals_.find(name);
        if (it != locals_.end()) return it->second;
        const unsigned result = next_register_++;
        locals_.emplace(name, result);
        return result;
    }

    std::optional<unsigned> find_local(const std::string& name) const
    {
        auto it = locals_.find(name);
        return it == locals_.end() ? std::nullopt : std::optional<unsigned>(it->second);
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
                result.integer = std::stoll(text, nullptr, 0);
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
        if (node.v.empty() || node.v[0].str != "[]" || node.v[0].v.size() != 1) {
            error = "unsupported index shape";
            return 0;
        }
        const auto base = emit_name(node.str, error);
        const auto source_index = emit_expression(node.v[0].v[0], error);
        const auto index = next_register_++;
        proto_.code.push_back(abc(MOVE, index, source_index, 0));
        const auto one = emit_constant(Constant{Constant::Integer, 1});
        proto_.code.push_back(abc(ADD, index, index, one));
        proto_.code.push_back(abc(MMBIN, index, one, 6));
        const auto result = next_register_++;
        proto_.code.push_back(abc(GETTABLE, result, base, index));
        return result;
    }

    unsigned emit_condition(const CalUnit& node, std::string& error)
    {
        if (node.type == CalUnitType::Operator && node.str == "&&" && node.v.size() == 2) {
            const auto first = emit_condition(node.v[0], error);
            const auto second = emit_condition(node.v[1], error);
            patch_jump(first, second);
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
            else if (node.str == ">") { op = LT; std::swap(left, right); }
            else if (node.str == ">=") { op = LE; std::swap(left, right); }
            else { error = "unsupported condition: " + node.str; return 0; }
            const auto stable_left = next_register_++;
            const auto stable_right = next_register_++;
            proto_.code.push_back(abc(MOVE, stable_left, left, 0));
            proto_.code.push_back(abc(MOVE, stable_right, right, 0));
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

    unsigned emit_expression(const CalUnit& node, std::string& error)
    {
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
        if (node.type == CalUnitType::Parameter) {
            if (!node.v.empty() && node.v[0].str == "[]") {
                return emit_index(node, error);
            }
            return emit_name(node.str, error);
        }
        if (node.type == CalUnitType::Operator && node.str == "()" && node.v.size() == 1)
            return emit_expression(node.v[0], error);
        if (node.type == CalUnitType::Operator && (node.str == "()++" || node.str == "()--") && node.v.size() == 1
            && node.v[0].type == CalUnitType::Parameter) {
            const auto target = emit_name(node.v[0].str, error);
            const auto encoded_delta = node.str == "()++" ? 128u : 126u;
            proto_.code.push_back(abc(ADDI, target, target, encoded_delta));
            proto_.code.push_back(abc(MMBINI, target, encoded_delta, 6));
            if (root_ && !find_local(node.v[0].str)) emit_global_set(node.v[0].str, target);
            return target;
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2 && node.str != "." && node.str != "::") {
            static const std::unordered_map<std::string, unsigned> operations = {
                {"+", ADD}, {"-", SUB}, {"*", MUL}, {"/", IDIV}, {"%", MOD}, {"//", IDIV}
            };
            const auto it = operations.find(node.str);
            if (it == operations.end()) {
                error = "unsupported operator: " + node.str;
                if (node.str == "." && node.v.size() == 2) {
                    error += " left=" + std::to_string(static_cast<int>(node.v[0].type))
                        + ":" + node.v[0].str + " right=" + std::to_string(static_cast<int>(node.v[1].type))
                        + ":" + node.v[1].str;
                }
                return 0;
            }
            const auto left = emit_expression(node.v[0], error);
            const auto right = emit_expression(node.v[1], error);
            const auto contains_string = [&](const CalUnit& item, const auto& self) -> bool {
                if (item.type == CalUnitType::String) return true;
                if (item.type == CalUnitType::Function && item.str == "to_string") return true;
                for (const auto& child : item.v) if (self(child, self)) return true;
                return false;
            };
            if (node.str == "+" && (contains_string(node.v[0], contains_string) || contains_string(node.v[1], contains_string))) {
                const auto first = next_register_++;
                const auto second = next_register_++;
                proto_.code.push_back(abc(MOVE, first, left, 0));
                proto_.code.push_back(abc(MOVE, second, right, 0));
                proto_.code.push_back(abc(CONCAT, first, 2, 0));
                return first;
            }
            const auto result = next_register_++;
            proto_.code.push_back(abc(it->second, result, left, right));
            const unsigned metamethod = node.str == "+" ? 6u
                : node.str == "-" ? 7u
                : node.str == "*" ? 8u
                : node.str == "%" ? 9u
                : node.str == "^" ? 10u
                : node.str == "/" ? 12u
                : node.str == "//" ? 12u : 0u;
            proto_.code.push_back(abc(MMBIN, result, right, metamethod));
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
            if (method == "reserve") return receiver;
            std::vector<const CalUnit*> arguments;
            std::function<void(const CalUnit&)> flatten = [&](const CalUnit& item) {
                if (item.type == CalUnitType::Operator && item.str == ",") for (const auto& child : item.v) flatten(child);
                else if (item.type != CalUnitType::None) arguments.push_back(&item);
            };
            for (const auto& child : node.v[1].v) flatten(child);
            if (method == "push_back") {
                if (arguments.size() != 1) { error = "push_back expects one argument"; return 0; }
                const auto value = emit_expression(*arguments[0], error);
                const auto index = next_register_++;
                proto_.code.push_back(abc(LEN, index, receiver, 0));
                const auto one = emit_constant(Constant{Constant::Integer, 1});
                proto_.code.push_back(abc(ADD, index, index, one));
                proto_.code.push_back(abc(MMBIN, index, one, 6));
                proto_.code.push_back(abc(SETTABLE, receiver, index, value));
                return receiver;
            }
            if (method == "pop_back") {
                next_register_ = std::max(next_register_, receiver + 1);
                const auto index = next_register_++;
                proto_.code.push_back(abc(LEN, index, receiver, 0));
                const auto value = next_register_++;
                proto_.code.push_back(abc(GETTABLE, value, receiver, index));
                const auto nil_value = next_register_++;
                proto_.code.push_back(abc(LOADNIL, nil_value, 0, 0));
                proto_.code.push_back(abc(SETTABLE, receiver, index, nil_value));
                return value;
            }
            const std::string function_name = method == "push_back" ? "insert" : method == "pop_back" ? "remove" : method;
            const unsigned function = next_register_;
            next_register_ += 4;
            const unsigned table_key = constant(Constant{Constant::String, 0, 0, false, "table"});
            proto_.code.push_back(abc(GETTABUP, function, 0, table_key, 1));
            const unsigned method_key = constant(Constant{Constant::String, 0, 0, false, function_name});
            proto_.code.push_back(abc(GETFIELD, function, function, method_key));
            proto_.code.push_back(abc(MOVE, function + 1, receiver, 0));
            unsigned count = 1;
            if (method == "push_back") {
                if (arguments.size() != 1) { error = "push_back expects one argument"; return 0; }
                const auto value = emit_expression(*arguments[0], error);
                proto_.code.push_back(abc(MOVE, function + 2, value, 0)); count = 2;
            } else if (method == "pop_back") {
                const auto size = next_register_++;
                proto_.code.push_back(abc(LEN, size, receiver, 0));
                proto_.code.push_back(abc(MOVE, function + 2, size, 0)); ++count;
            }
            proto_.code.push_back(abc(CALL, function, count + 1, 1));
            return receiver;
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
            next_register_ += static_cast<unsigned>(arguments.size() + 1);
            const std::string function_name = node.str == "to_string" ? "tostring"
                : node.str == "println" ? "print" : node.str;
            const unsigned name = constant(Constant{Constant::String, 0, 0, false, function_name});
            proto_.code.push_back(abc(GETTABUP, function_reg, 0, name, 1));
            std::vector<unsigned> argument_registers;
            argument_registers.reserve(arguments.size());
            for (const auto* argument : arguments) argument_registers.push_back(emit_expression(*argument, error));
            for (std::size_t argument_index = 0; argument_index < arguments.size(); ++argument_index) {
                const auto reg = argument_registers[argument_index];
                const auto target = function_reg + 1 + static_cast<unsigned>(argument_index);
                if (reg != target) proto_.code.push_back(abc(MOVE, target, reg, 0));
            }
            proto_.code.push_back(abc(CALL, function_reg, static_cast<unsigned>(arguments.size() + 1), 2));
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
        if (node.type == CalUnitType::Parameter && node.v.empty()) {
            const auto target = local(node.str);
            proto_.code.push_back(abc(NEWTABLE, target, 0, 0));
            proto_.code.push_back(ax(EXTRAARG, 0));
            return true;
        }
        if (node.type == CalUnitType::Operator && node.str == "=" && node.v.size() == 2) {
            if (node.v[0].type != CalUnitType::Parameter) { error = "unsupported assignment target"; return false; }
            const auto source = emit_expression(node.v[1], error);
            if (!node.v[0].v.empty() && node.v[0].v[0].str == "[]") {
                const auto table = emit_name(node.v[0].str, error);
                const auto source_index = emit_expression(node.v[0].v[0].v[0], error);
                const auto index = next_register_++;
                proto_.code.push_back(abc(MOVE, index, source_index, 0));
                const auto one = emit_constant(Constant{Constant::Integer, 1});
                proto_.code.push_back(abc(ADD, index, index, one));
                proto_.code.push_back(abc(MMBIN, index, one, 6));
                proto_.code.push_back(abc(SETTABLE, table, index, source));
            } else if (root_ && !find_local(node.v[0].str)) emit_global_set(node.v[0].str, source);
            else {
                const auto target = local(node.v[0].str);
                if (target != source) proto_.code.push_back(abc(MOVE, target, source, 0));
            }
            return error.empty();
        }
        if (node.type == CalUnitType::Operator && node.v.size() == 2
            && (node.str == "+=" || node.str == "-=")) {
            if (node.v[0].type != CalUnitType::Parameter || !node.v[0].v.empty()) { error = "unsupported compound assignment"; return false; }
            const auto left = emit_name(node.v[0].str, error);
            const auto right = emit_expression(node.v[1], error);
            const auto contains_string = [&](const CalUnit& item, const auto& self) -> bool {
                if (item.type == CalUnitType::String) return true;
                if (item.type == CalUnitType::Function && item.str == "to_string") return true;
                for (const auto& child : item.v) if (self(child, self)) return true;
                return false;
            };
            if (node.str == "+=" && contains_string(node.v[1], contains_string)) {
                const auto first = next_register_++;
                const auto second = next_register_++;
                proto_.code.push_back(abc(MOVE, first, left, 0));
                proto_.code.push_back(abc(MOVE, second, right, 0));
                proto_.code.push_back(abc(CONCAT, first, 2, 0));
                if (left != first) proto_.code.push_back(abc(MOVE, left, first, 0));
                if (root_ && !find_local(node.v[0].str)) emit_global_set(node.v[0].str, left);
                return error.empty();
            }
            const unsigned op = node.str == "+=" ? ADD : SUB;
            proto_.code.push_back(abc(op, left, left, right));
            proto_.code.push_back(abc(MMBIN, left, right, node.str == "+=" ? 6u : 7u));
            if (root_ && !find_local(node.v[0].str)) emit_global_set(node.v[0].str, left);
            return error.empty();
        }
        if (node.type == CalUnitType::Key && node.str == "return") {
            if (node.v.empty()) { proto_.code.push_back(abc(RETURN0, 0, 0, 0)); return true; }
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
        if (node.type == CalUnitType::Key && (node.str == "while" || node.str == "for")) {
            const CalUnit* clauses = nullptr; const CalUnit* body = nullptr;
            if (node.str == "while") { clauses = &node.v[0]; body = &node.v[1]; }
            else {
                clauses = &node.v[0];
                body = &node.v[1];
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
            if (!emit_block(*body, error)) return false;
            if (node.str == "for" && clauses->v.size() == 3 && !emit_statement(clauses->v[2], error)) return false;
            proto_.code.push_back(asj(JMP, static_cast<int>(loop_start) - static_cast<int>(proto_.code.size()) - 1));
            const auto loop_end = static_cast<unsigned>(proto_.code.size());
            patch_jump(false_jump, loop_end);
            for (const auto jump : loop_breaks_.back()) patch_jump(jump, loop_end);
            loop_breaks_.pop_back();
            return error.empty();
        }
        if (node.type == CalUnitType::Function || node.type == CalUnitType::Operator) {
            emit_expression(node, error);
            return error.empty();
        }
        error = "unsupported statement type=" + std::to_string(static_cast<int>(node.type)) + " str=" + node.str;
        return false;
    }

    bool emit_block(const CalUnit& node, std::string& error)
    {
        if (node.type == CalUnitType::Union && (node.str.empty() || node.str == "{}")) {
            for (const auto& child : node.v) {
                if (child.type == CalUnitType::Split || child.str == ";") continue;
                if (!emit_statement(child, error)) return false;
            }
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
        size(p.upvalue_names.size()); for (unsigned i = 0; i < p.upvalue_names.size(); ++i) { byte(root ? 1 : 0); byte(0); byte(0); }
        size(p.children.size()); for (const auto& child : p.children) proto(child, p.source.empty() ? parent_source : &p.source, false);
        size(0); size(0); size(0); size(p.upvalue_names.size()); for (const auto& name : p.upvalue_names) string(nullptr);
    }
public:
    std::vector<std::uint8_t> write(const Proto& root)
    {
        data_.insert(data_.end(), {0x1b, 'L', 'u', 'a', 0x54, 0, 0x19, 0x93, 0x0d, 0x0a, 0x1a, 0x0a});
        byte(sizeof(std::uint32_t)); byte(sizeof(std::int64_t)); byte(sizeof(double)); const std::int64_t magic = 0x5678; const double number = 370.5; raw(magic); raw(number); byte(1); proto(root, nullptr, true); return data_;
    }
};

} // namespace

namespace {

std::shared_ptr<Proto> compile_lua_program(const Cifa& compiler, std::vector<std::uint8_t>& chunk, std::string& error)
{
    const auto* root = compiler.compiled_ast();
    const auto* functions = compiler.compiled_functions();
    if (!root || !functions) { error = "Cifa AST is not compiled"; return {}; }
    auto proto = std::make_shared<Proto>();
    proto->upvalue_names.push_back(0);
    for (const auto& [name, overloads] : *functions) {
        if (overloads.size() != 1) { error = "overloads are not supported: " + name; return {}; }
        const auto& function = overloads.begin()->second;
        Proto child;
        child.params = static_cast<unsigned>(function.arguments.size());
        child.upvalue_names.push_back(0);
        FunctionCompiler child_compiler(compiler, child, functions);
        if (!child_compiler.compile_body(function.body, function.arguments, error)) return {};
        proto->children.push_back(std::move(child));
        const unsigned reg = 1;
        proto->code.push_back(abx(CLOSURE, reg, static_cast<unsigned>(proto->children.size() - 1)));
        const Constant key{Constant::String, 0, 0, false, name};
        const unsigned key_index = [&]() { for (unsigned i = 0; i < proto->constants.size(); ++i) if (proto->constants[i].kind == Constant::String && proto->constants[i].string == name) return i; proto->constants.push_back(key); return static_cast<unsigned>(proto->constants.size() - 1); }();
        proto->code.push_back(abc(SETTABUP, 0, key_index, reg, 0));
    }
    FunctionCompiler root_compiler(compiler, *proto, functions, true);
    if (!root_compiler.compile_body(*root, {}, error)) return {};
    chunk = ChunkWriter().write(*proto);
    return proto;
}

struct Table;
union Closure;

/* Lua 5.4 lobject.h: TValue is a tagged value; StackValue keeps the value
    representation separate from stack bookkeeping, as it does in Lua. */
enum LuaType : unsigned char { LUA_VNIL, LUA_VTABLE = 5, LUA_VLCL = 22, LUA_VCCL = 38 };
union Value { std::int64_t i; double n; bool b; const CifaLuaString* str; Table* table; Closure* closure; };
struct TValue { Value value_; unsigned char tt_; };
struct StackValue { TValue val; };
using StkId = StackValue*;
union StkIdRel { StkId p; std::ptrdiff_t offset; };

/* Runtime Proto follows Lua's lobject.h ownership shape. The existing Proto
   remains compiler-only; before execution it is frozen into these contiguous
   code/constant/child arrays and is never read by the interpreter. */
struct RuntimeProto {
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
};

static TValue make_nil() { return {}; }
static TValue make_integer(std::int64_t value) { TValue result; result.value_.i = value; result.tt_ = LUA_VNUMINT; return result; }
static TValue make_number(double value) { TValue result; result.value_.n = value; result.tt_ = LUA_VNUMFLT; return result; }
static TValue make_boolean(bool value) { TValue result; result.value_.b = value; result.tt_ = value ? LUA_VTRUE : LUA_VFALSE; return result; }
static TValue make_string_value(const CifaLuaString* value) { TValue result; result.value_.str = value; result.tt_ = LUA_VSHRSTR; return result; }
static TValue make_table_value(Table* value) { TValue result; result.value_.table = value; result.tt_ = LUA_VTABLE; return result; }
static TValue make_closure_value(Closure* value, bool cclosure) { TValue result; result.value_.closure = value; result.tt_ = cclosure ? LUA_VCCL : LUA_VLCL; return result; }
static bool ttisnil(const TValue& value) { return value.tt_ == LUA_VNIL; }
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
    unsigned int alimit = 0;
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
        clear_callinfo();
        delete[] stack_;
        for (CifaLuaString* string : strings_) free_lua_string(string);
        clear_string_table();
    }
    TValue run(RuntimeProto& proto)
    {
        error_.clear();
        free_tables();
        free_closures();
        state_.ci = nullptr;
        if (!global_table_) {
            global_env_ = new_table();
            global_table_ = global_env_.value_.table;
            tables_ = global_table_->allnext;
            global_table_->allnext = nullptr;
        }
        TValue root = make_lclosure(&proto, &global_env_);
        TValue tostring = make_cclosure(&LuaVm::luaB_tostring);
        set_table(global_env_, string_value("tostring"), tostring);
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
        return execute();
    }
    const std::string& error() const { return error_; }
private:
    friend struct Program;
    CifaLuaState state_;
    std::string error_;
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
    CallInfo* callinfo_root_ = nullptr;
#if defined(CIFA_LUA_OPCODE_PROFILE)
    std::array<std::uint64_t, EXTRAARG + 1> opcode_counts_{};
#endif

    static unsigned opcode(std::uint32_t i) { return i & 0x7f; }
    static unsigned arg_a(std::uint32_t i) { return (i >> POS_A) & 0xff; }
    static unsigned arg_b(std::uint32_t i) { return (i >> POS_B) & 0xff; }
    static unsigned arg_c(std::uint32_t i) { return (i >> POS_C) & 0xff; }
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
            if (closure->l.header.tt == LUA_VLCL) delete closure->l.upvals[0];
            delete closure;
        }
    }
    void grow_stack(std::size_t needed)
    {
        if (needed <= stack_size_) return;

        /* luaD_reallocstack: change every active stack pointer to an offset,
           move the raw stack allocation, and restore pointers afterwards. */
        const StkId old_stack = state_.stack.p;
        state_.top.offset = state_.top.p - old_stack;
        state_.stack_last.offset = state_.stack_last.p - old_stack;
        for (CallInfo* frame = state_.ci; frame; frame = frame->previous) {
            frame->func.offset = frame->func.p - old_stack;
            frame->top.offset = frame->top.p - old_stack;
        }

        StackValue* grown = new StackValue[needed]{};
        std::copy_n(stack_, stack_size_, grown);
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
        delete[] string_hash_;
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
    TValue make_lclosure(RuntimeProto* proto, TValue* upvalue)
    {
        Closure* closure = new Closure{};
        closure->l.header.tt = LUA_VLCL;
        closure->l.nupvalues = 1;
        closure->l.p = proto;
        closure->l.upvals[0] = new UpVal{};
        closure->l.upvals[0]->v.p = upvalue;
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
    static double number(const TValue& value) { return ttisinteger(value) ? static_cast<double>(value.value_.i) : value.value_.n; }
    static std::int64_t integer(const TValue& value) { return ttisinteger(value) ? value.value_.i : static_cast<std::int64_t>(value.value_.n); }
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
        if (!ttistable(table)) { set_error("attempt to index a non-table value"); return make_nil(); }
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
        TValue* slot = getslot(target, key);
        if (slot) { setobj(*slot, value); return; }
        if (!ttisnil(value)) setobj(*newkey(target, key), value);
    }
    std::size_t getn(Table& table) const
    {
        std::size_t limit = table.alimit;
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
        CallInfo* ci = prep_callinfo(func, nresults, state_.stack.p + base + closure->p->maxstacksize);
        ci->p = closure->p;
        return false;
    }
    bool poscall(TValue result)
    {
        CallInfo* finished = state_.ci;
        const std::size_t result_slot = static_cast<std::size_t>(finished->func.p - state_.stack.p);
        const int wanted = finished->nresults;
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
                else setobj(ra, get_table(rb, rc));
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
                if (!ttistable(rb)) set_error("attempt to get length of a non-table value"); else ra = make_integer(static_cast<std::int64_t>(getn(*rb.value_.table)));
                break;
            }
            case CONCAT: {
                const unsigned a = arg_a(instruction);
                std::string value;
                for (unsigned index = 0; index < arg_b(instruction); ++index) value += tostring(base[a + index].val);
                base[a].val = string_value(std::move(value));
                break;
            }
            case ADDI: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) + arg_sc(instruction)); ++pc; break;
            case ADD: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) + integer(base[arg_c(instruction)].val)); ++pc; break;
            case SUB: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) - integer(base[arg_c(instruction)].val)); ++pc; break;
            case MUL: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) * integer(base[arg_c(instruction)].val)); ++pc; break;
            case MOD: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) % integer(base[arg_c(instruction)].val)); ++pc; break;
            case IDIV: base[arg_a(instruction)].val = make_integer(integer(base[arg_b(instruction)].val) / integer(base[arg_c(instruction)].val)); ++pc; break;
            case JMP: pc += arg_sj(instruction); break;
            case EQ: if (equalobj(base[arg_a(instruction)].val, base[arg_b(instruction)].val) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case LT: {
                const TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                if ((ttisstring(ra) && ttisstring(rb) ? string_copy(ra.value_.str) < string_copy(rb.value_.str) : number(ra) < number(rb)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1;
                break;
            }
            case LE: {
                const TValue& ra = base[arg_a(instruction)].val;
                const TValue& rb = base[arg_b(instruction)].val;
                if ((ttisstring(ra) && ttisstring(rb) ? string_copy(ra.value_.str) <= string_copy(rb.value_.str) : number(ra) <= number(rb)) != arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1;
                break;
            }
            case TEST: if ((!cifa_test_false(base[arg_a(instruction)].val)) != !arg_k(instruction)) ++pc; else pc += arg_sj(*pc) + 1; break;
            case CLOSURE: {
                RuntimeProto* child = cl->p->p[arg_bx(instruction)];
                if (!child || !child->code) { set_error("Lua VM has an invalid child Proto"); return make_nil(); }
                base[arg_a(instruction)].val = make_lclosure(child, cl->upvals[0]->v.p);
                break;
            }
            case CALL: {
                const unsigned a = arg_a(instruction);
                const unsigned b = arg_b(instruction);
                if (b != 0) state_.top.p = base + a + b;
                ci->savedpc = static_cast<std::size_t>(pc - cl->p->code);
                if (ttiscclosure(base[a].val)) { if (!precall(base + a, 1)) return make_nil(); }
                else if (!precall(base + a, 1)) goto startfunc;
                break;
            }
            case RETURN0: if (poscall(make_nil())) return stack_[0].val; goto startfunc;
            case RETURN1: if (poscall(base[arg_a(instruction)].val)) return stack_[0].val; goto startfunc;
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
        proto = {};
    }

    RuntimeProto* freeze(const Proto& source, RuntimeProto& target)
    {
        target.numparams = static_cast<unsigned char>(source.params);
        target.is_vararg = static_cast<unsigned char>(source.vararg);
        target.maxstacksize = static_cast<unsigned char>(source.maxstack);
        target.sizeupvalues = static_cast<int>(source.upvalue_names.size());
        target.sizecode = static_cast<int>(source.code.size());
        target.sizek = static_cast<int>(source.constants.size());
        target.sizep = static_cast<int>(source.children.size());
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

Object object_from_value(const TValue& value)
{
    if (ttisinteger(value)) return Object(static_cast<std::int64_t>(value.value_.i));
    if (ttisfloat(value)) return Object(value.value_.n);
    if (value.tt_ == LUA_VTRUE || value.tt_ == LUA_VFALSE) return Object(static_cast<bool>(value.value_.b));
    if (ttisstring(value)) return Object(string_copy(value.value_.str));
    return {};
}

} // namespace

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
    auto proto = compile_lua_program(*this, chunk_, translation_error_);
    if (!proto) return false;
    auto program = std::make_shared<Program>();
    program->initialize(*proto);
    program_ = std::move(program);
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
    const TValue result = program->vm.run(program->root);
    runtime_error_ = program->vm.error();
    return runtime_error_.empty() ? object_from_value(result) : Object("", "Error");
}

} // namespace cifa
