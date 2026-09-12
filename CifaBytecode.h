#pragma once
#include "Cifa.h"
#include <optional>

namespace cifa
{
class CifaBytecode : public Cifa
{
    friend class Cifa;
    enum class Opcode { Constant, Load, LoadLocal, DeclareLocal, StoreLocal, IncrementLocal, Enter, Leave, Add, Subtract, Multiply, Divide, Modulo, Less, Greater,
        LessEqual, GreaterEqual, Equal, NotEqual, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight,
        Positive, Negative, LogicalNot, BitNot, Cast, Drop, Empty, Jump, Branch,
        AndBranch, OrBranch, LogicalAnd, LogicalOr, Return, ScopeEnter, ScopeLeave,
        PrepareStore, Store, Increment, Unwind, LoopMark, SwitchMark, SwitchCase, SwitchDefault, SwitchEnd,
        CallBegin, Call, CallEnd, Peek, Array, Index, RangeBegin, RangeNext, RangeEnd, MethodNoArgs, BindArgument,
        MethodBegin, MethodValue, MethodPush, Member, Exit };
    struct SourceRef
    {
        size_t id = 0;
        SourceRef() = default;
        explicit SourceRef(size_t value) : id(value) {}
    };
    struct SourceLocation
    {
        std::string str;
        size_t line = 0;
        size_t col = 0;
        std::string filename;
        std::string text;

        SourceLocation() = default;
        explicit SourceLocation(const CalUnit& node);
    };
    struct Scope : std::unordered_map<std::string, Object>
    {
        std::unordered_map<std::string, Object*> slots;
    };
    using ScopeStack = std::vector<Scope>;
    struct Machine;
    enum class WriteOperation { Assign, Add, Subtract, Multiply, Divide, Modulo, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight, PostAdd, PostSubtract, Invalid };
    static WriteOperation write_operation(const std::string& symbol);
    static Object write_value(Machine& machine, WriteOperation operation, const Object& target, Object right);
    struct Instruction
    {
        Opcode opcode;
        SourceRef source;
        size_t operand = 0;
        size_t auxiliary = 0;
        size_t member_site = 0;
        WriteOperation write = WriteOperation::Assign;
        size_t variable_site = 0;
        SourceRef condition_source;
        SourceRef target_source;
    };
    struct Instructions
    {
        std::vector<Instruction> code;
        std::vector<std::vector<std::pair<size_t, bool>>> diagnostic_frames;
        size_t stack_capacity = 0;
    };
    std::unordered_map<const CalUnit*, size_t> source_ids;
    std::unordered_map<size_t, const CalUnit*> compile_sources;
    std::string translation_error;
    std::string runtime_error;
    std::unordered_map<std::string, size_t> name_ids;
    struct IndexSite
    {
        size_t name_id;
        size_t type_id;
        size_t dimensions;
        bool declaration;
        bool string_index;
        bool with_type;
    };
    struct VariableSite
    {
        size_t name_id;
        size_t type_id;
        bool with_type;
    };
    struct CallSite
    {
        SourceRef source;
        std::vector<SourceLocation> arguments;
        size_t local_slot = 0;
        size_t name_id = 0;
        size_t base_name_id = 0;
        SourceRef method_source;
    };
    struct Module;
    struct FunctionCode
    {
        struct Parameter
        {
            std::string name;
            std::string type_name;
        };
        std::vector<Parameter> parameters;
        std::string name;
        std::string return_type;
        SourceRef body_source;
        Instructions instructions;
        size_t local_slot_count = 0;
    };
    struct Module
    {
        struct SourceLine
        {
            std::string filename;
            size_t line = 0;
            std::string text;
        };
        bool compiled_valid = false;
        std::vector<SourceLine> source_lines;
        std::unordered_map<std::string, size_t> entry_labels;
        std::unordered_map<std::string, std::vector<StructField>> structures;
        std::deque<SourceLocation> sources;
        std::vector<Object> constants;
        std::deque<std::string> names;
        std::vector<IndexSite> index_sites;
        std::vector<VariableSite> variable_sites;
        std::vector<std::pair<size_t, size_t>> member_sites;
        Instructions root_instructions;
        SourceRef root_source;
        std::vector<size_t> root_entries;
        std::deque<CallSite> calls;
        std::unordered_map<std::string, std::unordered_map<size_t, std::shared_ptr<FunctionCode>>> function_code;
        const SourceLocation& source(const SourceRef& reference) const { return sources.at(reference.id - 1); }
    };
    struct Machine
    {
        struct ReturnState
        {
            bool has_value = false;
            Object value;
            std::string return_type;
        };
        CifaBytecode& host;
        ScopeStack scopes;
        std::unordered_map<std::string, std::unordered_map<size_t, std::shared_ptr<const Module>>> functions;
        std::unordered_map<std::string, std::vector<StructField>> structures;
        std::vector<ReturnState> returns;
        std::vector<std::pair<const SourceLocation*, bool>> call_stack;
        std::function<void(std::vector<std::pair<const SourceLocation*, bool>>&)> append_diagnostic_frames;
        std::string error;
        bool exit_requested = false;

        explicit Machine(CifaBytecode& value_host) : host(value_host) { }
        void publish(const std::shared_ptr<const Module>& module);
        const FunctionCode* find_function(const std::string& name, size_t arity, std::shared_ptr<const Module>& owner) const;
        void set_error(std::string message, const SourceLocation* location = nullptr);
        static std::string format_frame(const SourceLocation& location);
        void set_no_value_error(const Object& value, const SourceLocation* location = nullptr);
        Object error_result() const { return Object("RuntimeError", "Error"); }
        Object& get_or_create(const std::string& name, bool current_scope_only = false);
        Object* find_object(const std::string& name);
        Object& resolve_member(const std::string& base_name, const std::string& field_name);
        Object read_named(const std::string& name, const std::string& type_name, bool with_type, bool only_check,
            bool initialize_struct, const SourceLocation& location);
        Object& assign_named(const std::string& name, const std::string& type_name, bool with_type, bool declare_current,
            const SourceLocation& location);
        bool assign(Object& target, Object value, bool with_type, const std::string& type_name, const SourceLocation& location);
        bool bind_type(Object& value, const std::string& type_name, const SourceLocation& location);
        Object convert_type(const Object& value, const std::string& type_name, const SourceLocation& location);
        bool condition(Object& value, const SourceLocation& location);
        bool range(Object& value, const SourceLocation& location, ObjectVector& values);
        bool bind_range(Object& value, const std::string& name, const std::string& type_name, const SourceLocation& location);
        Object call_host(const std::string& name, ObjectVector& arguments, const std::vector<SourceLocation>& locations);
        Object call_method(const std::string& name, const SourceLocation& location, Object& object,
            const std::vector<SourceLocation>& locations, const std::function<Object(size_t)>& argument_value);
        Object push_back(const SourceLocation& location, Object& object, Object value);
        Object& indexed(const std::string& name, const std::string& type_name, size_t dimensions, bool is_decl_array,
            bool only_check, bool declare_current, const std::function<Object(size_t)>& index_value);
        Object checked_index(const Object& element, bool map_access);
        Object binary(Opcode opcode, const Object& left, const Object& right, const SourceLocation& location);
        Object make_no_value(const std::string& function_name, const SourceLocation& call_site) const;
        bool should_stop() const { return exit_requested || !error.empty(); }
    };
    std::shared_ptr<Module> module_data = std::make_shared<Module>();
    bool& compiled_valid = module_data->compiled_valid;
    std::vector<Module::SourceLine>& source_lines = module_data->source_lines;
    std::unordered_map<std::string, size_t>& entry_labels = module_data->entry_labels;
    std::deque<SourceLocation>& sources = module_data->sources;
    std::vector<Object>& constants = module_data->constants;
    std::deque<std::string>& names = module_data->names;
    std::vector<IndexSite>& index_sites = module_data->index_sites;
    std::vector<VariableSite>& variable_sites = module_data->variable_sites;
    std::vector<std::pair<size_t, size_t>>& member_sites = module_data->member_sites;
    Instructions& root_instructions = module_data->root_instructions;
    SourceRef& root_source = module_data->root_source;
    std::vector<size_t>& root_entries = module_data->root_entries;
    std::deque<CallSite>& calls = module_data->calls;
    decltype(Module::function_code)& function_code = module_data->function_code;
    struct Loop
    {
        size_t scopes;
        size_t traces;
        std::vector<size_t> breaks;
        std::vector<size_t> continues;
        bool is_switch = false;
    };
    std::vector<Loop> compile_loops;
    struct LabelBlock
    {
        size_t mark;
        std::unordered_map<std::string, size_t> targets;
        std::vector<std::pair<size_t, std::string>> jumps;
    };
    std::vector<LabelBlock> compile_blocks;
    size_t compile_scopes = 0;
    size_t compile_traces = 0;
    FunctionCode* compiling_function = nullptr;
    std::vector<std::unordered_map<std::string, size_t>> compile_local_scopes;
    std::vector<size_t> compile_local_scope_bases;

    size_t source_id(const CalUnit& source);
    SourceRef source_ref(const CalUnit* node);
    size_t intern_name(const std::string& name);
    size_t index_site(const CalUnit& node);
    void seal(Instructions& instructions);
    void seal_calls();
    static std::vector<size_t> compact(Instructions& instructions);
    SourceLocation& source(const SourceRef& reference) const;
    std::optional<size_t> local_slot(const CalUnit& node, bool declare, bool allow_untyped_declaration = false);
    size_t next_local_slot() const;
    static bool operation(const CalUnit& node, Opcode& opcode);
    void emit(CalUnit& node, std::vector<Instruction>& instructions);
    bool verify(Instructions& instructions, size_t local_slot_count = 0);
    static bool execute_instructions(Machine& machine, const Module& module, const Instructions& instructions,
        Object& result, size_t start = 0);
    static Object run_module(Machine& machine, const Module& module);

public:
    class Session
    {
    public:
        explicit Session(CifaBytecode& interpreter);
        ~Session();
        Session(const Session&) = delete;
        Session& operator=(const Session&) = delete;
        Object run(CifaBytecode& code, const std::string& entry_label = {});
        std::vector<size_t> function_arities(const std::string& name) const;
        const std::vector<StructField>* find_structure(const std::string& name) const;
        void copy_catalog(std::unordered_map<std::string, FunctionOverloads>& functions,
            std::unordered_map<std::string, std::vector<StructField>>& structures) const;
        bool is_exit_requested() const;

    private:
        std::unique_ptr<Machine> machine;
        bool active = false;
    };

    CifaBytecode();
    ~CifaBytecode();
    CifaBytecode(const CifaBytecode&) = delete;
    CifaBytecode& operator=(const CifaBytecode&) = delete;
    CifaBytecode(CifaBytecode&&) = delete;
    CifaBytecode& operator=(CifaBytecode&&) = delete;
    bool valid() const { return compiled_valid && translation_error.empty(); }
    const std::string& get_translation_error() const { return translation_error; }
    std::string get_runtime_error() const { return runtime_error.empty() ? Cifa::get_runtime_error() : runtime_error; }
    bool has_runtime_error() const { return !runtime_error.empty() || Cifa::has_runtime_error(); }
    bool is_exit_requested() const;
    bool compile_script(std::string script);
    bool compile_file(const std::string& filename);
    Object run(const std::string& entry_label = {});
    Object run_script(std::string script);
    Object run_file(const std::string& filename);

private:
    void translate(Cifa& compiler);
    void prepare_compile_visibility();
    void clear_compile_visibility();
    std::unique_ptr<Session> session;
    std::vector<std::unique_ptr<CifaBytecode>> nested_modules;
    std::unordered_map<std::string, FunctionOverloads> persistent_functions;
    std::unordered_map<std::string, std::vector<StructField>> persistent_struct_defs;
};
}