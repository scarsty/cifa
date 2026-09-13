#pragma once
#include "Cifa.h"
#include <optional>
#include <memory_resource>

namespace cifa
{
class CifaBytecode : public Cifa
{
    friend class Cifa;
    friend struct RegisterBackendTest;
    enum class Opcode { Constant, Load, LoadLocal, DeclareLocal, StoreLocal, IncrementLocal, Enter, Leave, Add, Subtract, Multiply, Divide, Modulo, Less, Greater,
        LessEqual, GreaterEqual, Equal, NotEqual, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight,
        Positive, Negative, LogicalNot, BitNot, Cast, Size, MathUnary, MathBinary, Empty, Jump, Branch,
        AndBranch, OrBranch, LogicalAnd, LogicalOr, Return, ScopeEnter, ScopeLeave,
        PrepareStore, Store, Increment, Unwind, LoopMark, SwitchMark, SwitchCase, SwitchDefault, SwitchEnd,
        CallBegin, Call, CallEnd, Peek, Array, Index, RangeBegin, RangeNext, RangeEnd, MethodNoArgs, BindArgument,
        MethodBegin, MethodValue, MethodPush, Member, RegisterBinary, RegisterSnapshot, Exit };
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
    struct RegisterSlots;
    struct Scope
    {
        struct Binding
        {
            std::string name;
            RegisterSlots* file = nullptr;
            size_t slot = 0;
        };
        std::vector<Binding> bindings;
        std::unique_ptr<RegisterSlots> dynamic_registers;
        Binding* find(const std::string& name);
        Binding& create(const std::string& name);
        void bind(const std::string& name, RegisterSlots* file, size_t slot);
    };
    using ScopeStack = std::vector<Scope>;
    struct Machine;
    enum class WriteOperation { Assign, Add, Subtract, Multiply, Divide, Modulo, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight, PostAdd, PostSubtract, Invalid };
    static WriteOperation write_operation(const std::string& symbol);
    static std::optional<Opcode> write_opcode(WriteOperation operation);
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
        size_t destination = 0;
        size_t input_offset = 0;
        size_t input_count = 0;
        bool discard_result = false;
    };
    struct RegisterOperation
    {
        std::uint16_t opcode;
        std::uint16_t flags;
        std::uint32_t destination;
        std::uint32_t left;
        std::uint32_t right;
    };
    static_assert(sizeof(RegisterOperation) == 16);
    struct Instructions
    {
        std::vector<Instruction> code;
        std::vector<size_t> register_inputs;
        std::vector<std::vector<std::pair<size_t, bool>>> diagnostic_frames;
        size_t register_capacity = 0;
        size_t temporary_count = 0;
        size_t loop_state_count = 0;
        size_t switch_count = 0;
        size_t range_count = 0;
        size_t method_argument_count = 0;
        size_t scope_capacity = 0;
    };
    struct BytecodeValue
    {
        using Storage = std::variant<std::monostate, std::int64_t, double, bool, std::any>;
        enum class Region : std::uint8_t { Empty, Int, Double, Bool, Any };
        size_t offset = 0;
        Region region = Region::Empty;
    };
    static_assert(sizeof(BytecodeValue) < sizeof(Object));
    struct ConstantValue
    {
        BytecodeValue::Storage value;
        bool continue_marker = false;
        ConstantValue(Object object) : value(std::move(object.value)) {}
        explicit ConstantValue(bool boolean) : value(boolean) {}
        explicit ConstantValue(int integer) : value(std::int64_t(integer)) {}
        explicit ConstantValue(const std::string& text) : value(std::any(text)) {}
        explicit ConstantValue(ObjectVector elements) : value(std::any(std::move(elements))) {}
        ConstantValue(const char* text, bool marker) : value(std::any(std::string(text))), continue_marker(marker) {}
    };
    struct RegisterSlots
    {
        enum class NumericBinding : std::uint8_t { None, Int, Double };
        struct TypeDescriptor
        {
            std::type_index bound = typeid(void);
            std::string declared;
            std::string element;
            std::string special;
            bool operator==(const TypeDescriptor&) const = default;
        };
        struct Storage
        {
            std::vector<BytecodeValue> values;
            std::vector<std::int64_t> integers;
            std::vector<double> doubles;
            std::vector<std::uint8_t> booleans;
            std::vector<std::unique_ptr<BytecodeValue::Storage>> resources;
            std::array<std::vector<size_t>, 4> free_offsets;
            std::vector<NumericBinding> bindings;
            std::vector<const Object*> origins;
            std::vector<size_t> names;
            std::deque<std::string> name_pool{std::string{}};
            std::unordered_map<std::string, size_t> name_ids;
            std::vector<size_t> types;
            std::deque<TypeDescriptor> type_pool{TypeDescriptor{}};
            explicit Storage(size_t count) : values(count), bindings(count), origins(count), names(count), types(count) {}
        };
        std::unique_ptr<Storage> storage;
        std::vector<BytecodeValue>& values;
        Storage& owner;
        std::vector<NumericBinding>& bindings;
        std::vector<const Object*>& origins;
        std::vector<size_t>& slot_names;
        std::deque<std::string>& name_pool;
        std::unordered_map<std::string, size_t>& name_ids;
        std::vector<size_t>& slot_types;
        std::deque<TypeDescriptor>& type_pool;
        size_t window_base = 0;
        size_t window_top = 0;
        size_t window_size = 0;

        explicit RegisterSlots(size_t count = 0)
            : storage(std::make_unique<Storage>(count)), values(storage->values), owner(*storage), bindings(storage->bindings),
              origins(storage->origins), slot_names(storage->names), name_pool(storage->name_pool),
              name_ids(storage->name_ids), slot_types(storage->types), type_pool(storage->type_pool), window_top(count), window_size(count) {}
        RegisterSlots(RegisterSlots& owner, size_t base, size_t count)
            : values(owner.values), owner(owner.owner), bindings(owner.bindings), origins(owner.origins), slot_names(owner.slot_names),
                            name_pool(owner.name_pool), name_ids(owner.name_ids),
                              slot_types(owner.slot_types), type_pool(owner.type_pool),
              window_base(base), window_top(base + count), window_size(count) {}
        RegisterSlots(RegisterSlots&&) = default;
        RegisterSlots& operator=(RegisterSlots&& other)
        {
            if (this != &other)
            {
                this->~RegisterSlots();
                new (this) RegisterSlots(std::move(other));
            }
            return *this;
        }
        size_t size() const { return window_size; }
        size_t base() const { return window_base; }
        size_t top() const { return window_top; }
        void enter(size_t count);
        size_t append();
        void restore(size_t base, size_t size, size_t top);
        void export_argument(size_t slot, Object& destination);
        void import_object(size_t slot, const Object& value);
        void import_object(size_t slot, Object&& value);
        template<class Value> void import_value(size_t slot, Value&& value);
        void clear(size_t slot);
        void set_name(size_t slot, const std::string& name);
        void set_type(size_t slot, const TypeDescriptor& type);
        BytecodeValue::Storage payload(size_t slot) const;
        const BytecodeValue::Storage& payload(size_t slot, BytecodeValue::Storage& numeric) const;
        BytecodeValue::Storage& resource_payload(size_t slot);
        void release_payload(size_t slot);
        void store_payload(size_t slot, BytecodeValue::Storage value);
        template<class Number> void write_number(size_t slot, Number number, bool preserve_binding = false);
        void write_payload(size_t slot, BytecodeValue::Storage value);
        void write_payload(size_t slot, std::int64_t value);
        void write_payload(size_t slot, double value);
        void write_payload(size_t slot, bool value);
        void copy(size_t destination, const RegisterSlots& source, size_t slot);
        void move(size_t destination, RegisterSlots& source, size_t slot);
        bool assign_numeric(size_t destination, const RegisterSlots& source, size_t slot);
        bool cast_numeric(size_t destination, const RegisterSlots& source, size_t slot, const std::string& type);
        void bind_numeric(size_t slot, NumericBinding binding);
        bool has_name(size_t slot) const { return slot_names[window_base + slot] != 0; }
        bool integer(size_t slot, std::int64_t& result) const;
        bool number(size_t slot, std::int64_t& integer, double& floating, bool& is_double) const;
        bool empty(size_t slot) const
        {
            const size_t index = window_base + slot;
            return values[index].region == BytecodeValue::Region::Empty;
        }
        bool binary(Opcode opcode, size_t destination, size_t left, size_t right,
            Machine& machine, const SourceLocation& location);
        void binary_fallback(Opcode opcode, size_t destination, size_t left, size_t right,
            Machine& machine, const SourceLocation& location, bool optimized);
        bool binary_payloads(Opcode opcode, size_t destination, const BytecodeValue::Storage& left_payload,
            const BytecodeValue::Storage& right_payload, Machine& machine, const SourceLocation& location);
        bool binary_numbers(Opcode opcode, size_t destination, std::int64_t left_integer, double left_number, bool left_double,
            std::int64_t right_integer, double right_number, bool right_double, Machine& machine, const SourceLocation& location);
    };
    struct RegisterBinarySite
    {
        RegisterOperation code;
        size_t left_name;
        size_t right_name;
        SourceRef left_source;
        SourceRef right_source;
        size_t variable_site = 0;
        SourceRef assignment_source;
        bool left_constant = false;
        bool right_constant = false;
        bool left_temporary = false;
        bool right_temporary = false;
        size_t temporary_destination = 0;
    };
    std::unordered_map<const CalUnit*, size_t> source_ids;
    std::unordered_map<size_t, const CalUnit*> compile_sources;
    CalUnit* active_statement_node = nullptr;
    std::string translation_error;
    std::string runtime_error;
    std::unordered_map<std::string, size_t> name_ids;
    struct IndexSite
    {
        size_t name_id;
        size_t type_id;
        size_t dimensions;
        size_t local_slot = 0;
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
    enum class MathKind : std::uint8_t
    {
        None, Abs, Sqrt, Cbrt, Round, Trunc, NearbyInt, Rint, Ceil, Floor,
        Sin, Cos, Tan, Asin, Acos, Atan, Sinh, Cosh, Tanh, Exp, Log, Log2,
        Log10, Erf, Erfc, TGamma, LGamma, Atan2, Pow, Hypot, Fmod,
        Remainder, CopySign, FDim, FMax, FMin
    };
    struct CallSite
    {
        SourceRef source;
        std::vector<SourceLocation> arguments;
        size_t local_slot = 0;
        size_t name_id = 0;
        size_t base_name_id = 0;
        SourceRef method_source;
        MathKind math_kind = MathKind::None;
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
        size_t host_function_version = 0;
        size_t script_function_version = 0;
        bool freeze_script_functions = false;
        std::vector<SourceLine> source_lines;
        std::unordered_map<std::string, size_t> entry_labels;
        std::unordered_map<std::string, std::vector<StructField>> structures;
        std::deque<SourceLocation> sources;
        std::vector<ConstantValue> constants;
        std::deque<std::string> names;
        std::vector<IndexSite> index_sites;
        std::vector<VariableSite> variable_sites;
        std::vector<RegisterBinarySite> register_binary_sites;
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
            std::string return_type;
        };
        CifaBytecode& host;
        RegisterSlots registers;
        ScopeStack scopes;
        std::unordered_map<std::string, std::unordered_map<size_t, std::shared_ptr<const Module>>> functions;
        struct CachedFunction
        {
            std::shared_ptr<const Module> owner;
            const FunctionCode* code = nullptr;
        };
        std::unordered_map<const CallSite*, CachedFunction> function_cache;
        std::unordered_map<std::string, std::vector<StructField>> structures;
        size_t function_version = 0;
        size_t frozen_function_version = 0;
        const Module* frozen_function_module = nullptr;
        std::vector<ReturnState> returns;
        std::vector<std::pair<const SourceLocation*, bool>> call_stack;
        std::function<void(std::vector<std::pair<const SourceLocation*, bool>>&)> append_diagnostic_frames;
        std::string error;
        Object error_placeholder;
        bool exit_requested = false;

        explicit Machine(CifaBytecode& value_host) : host(value_host) { }
        void publish(const std::shared_ptr<const Module>& module);
        const FunctionCode* find_function(const std::string& name, size_t arity, std::shared_ptr<const Module>& owner) const;
        const FunctionCode* find_cached_function(const CallSite& call, const std::string& name, size_t arity,
            std::shared_ptr<const Module>& owner);
        void set_error(std::string message, const SourceLocation* location = nullptr);
        static std::string format_frame(const SourceLocation& location);
        void set_no_value_error(const Object& value, const SourceLocation* location = nullptr);
        void set_no_value_error(const Object::NoValue* value, const SourceLocation* location = nullptr);
        Object error_result() const { return Object("RuntimeError", "Error"); }
        Scope::Binding* find_slot(const std::string& name);
        BytecodeValue::Storage& named_payload(const std::string& name, std::string& element_type, bool* existed = nullptr);
        Object& resolve_member(const std::string& base_name, const std::string& field_name);
        void read_named(RegisterSlots& destination, size_t slot, const std::string& name, const std::string& type_name, bool with_type, bool only_check,
            bool initialize_struct, const SourceLocation& location);
        Object& assign_named(const std::string& name, const std::string& type_name, bool with_type, bool declare_current,
            const SourceLocation& location);
        bool assign(Object& target, Object value, bool with_type, const std::string& type_name, const SourceLocation& location);
        bool assign(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
            size_t scratch, const SourceLocation& location);
        bool bind_type(Object& value, const std::string& type_name, const SourceLocation& location);
        bool bind_type(RegisterSlots& values, size_t slot, const std::string& type_name, const SourceLocation& location);
        Object convert_type(const Object& value, const std::string& type_name, const SourceLocation& location);
        bool convert_type(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
            const std::string& type_name, const SourceLocation& location);
        bool condition(RegisterSlots& source, size_t slot, const SourceLocation* location);
        void conversion_error(RegisterSlots& source, size_t slot, const std::string& target, const SourceLocation* location);
        std::string string_value(RegisterSlots& source, size_t slot);
        bool range(RegisterSlots& source, size_t slot, const SourceLocation& location,
            RegisterSlots& destination, size_t target);
        bool bind_range(RegisterSlots& values, size_t slot, const std::string& name, const std::string& type_name, const SourceLocation& location);
        Object call_host(const std::string& name, ObjectVector& arguments, const std::vector<SourceLocation>& locations);
        bool call_builtin_math(const std::string& name, const ObjectVector& arguments, Object& result) const;
        void call_method(RegisterSlots& destination, size_t slot, const std::string& name, const SourceLocation& location,
            BytecodeValue::Storage& receiver, const std::string& element_type,
            const std::vector<SourceLocation>& locations, RegisterSlots& arguments);
        Object& indexed(const std::string& name, const std::string& type_name, size_t dimensions, bool is_decl_array,
            bool only_check, bool declare_current, RegisterSlots& indices, const size_t* index_slots);
        void checked_index(RegisterSlots& destination, size_t slot, const Object& element, bool map_access);
        Object make_no_value(const std::string& function_name, const SourceLocation& call_site) const;
        bool should_stop() const { return exit_requested || !error.empty(); }
    };
    std::shared_ptr<Module> module_data = std::make_shared<Module>();
    bool& compiled_valid = module_data->compiled_valid;
    std::vector<Module::SourceLine>& source_lines = module_data->source_lines;
    std::unordered_map<std::string, size_t>& entry_labels = module_data->entry_labels;
    std::deque<SourceLocation>& sources = module_data->sources;
    std::vector<ConstantValue>& constants = module_data->constants;
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
    const std::unordered_map<std::string, FunctionOverloads>* compile_script_functions = nullptr;
    bool compile_allows_script_constant_folding = false;
    std::unordered_set<std::string> compile_inline_functions;

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
    bool try_fold_constant(const CalUnit& node, Object& value,
        const std::unordered_map<std::string, Object>* parameters = nullptr,
        std::unordered_set<std::string>* active_functions = nullptr) const;
    void emit(CalUnit& node, std::vector<Instruction>& instructions);
    size_t emit_statement(CalUnit& node, std::vector<Instruction>& instructions);
    static void discard_statement_result(std::vector<Instruction>& instructions, size_t begin);
    bool emit_register_expression(CalUnit& node, std::vector<Instruction>& instructions);
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
        size_t script_function_version() const;

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
    void set_optimization_enabled(bool enabled) { optimization_enabled = enabled; }
    bool is_optimization_enabled() const { return optimization_enabled; }
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
    bool optimization_enabled = false;
};
}