#pragma once
#include "Cifa.h"
#include <optional>
#include "CifaMemory.h"

namespace cifa
{
class CifaBytecode : public Cifa
{
    memory::Resource allocation_resource;
    friend class Cifa;
    // 最终执行流的操作码；构建期标记会在 compact() 中移出执行流。
    enum class Opcode { Constant, ConstantLocal, Load, LoadLocal, DeclareLocal, StoreLocal, IncrementLocal, Enter, Leave, Add, Subtract, Multiply, Divide, Modulo, Less, Greater,
        LessEqual, GreaterEqual, Equal, NotEqual, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight,
        Positive, Negative, LogicalNot, BitNot, Cast, Size, MathUnary, MathBinary, Empty, Jump, Branch,
        AndBranch, OrBranch, LogicalAnd, LogicalOr, Return, ScopeEnter, ScopeLeave,
        PrepareStore, Store, Increment, Unwind, LoopMark, SwitchMark, SwitchCase, SwitchDefault, SwitchEnd,
        CallBegin, Call, CallEnd, Peek, Array, Index, IndexLocal, RangeBegin, RangeNext, RangeEnd, MethodNoArgs, BindArgument,
        MethodBegin, MethodValue, MethodPush, ArrayPushGlobal, ArrayPushGlobalLocal, Member, NumericBinary, NumericBinaryLocal,
        NumericCompareBranch, NumericForNext, RegisterBinary, RegisterSnapshot, Exit, Removed };
    // 源码位置在冷表中的稳定编号，零表示没有对应源码位置。
    struct SourceRef
    {
        size_t id = 0;
        SourceRef() = default;
        explicit SourceRef(size_t value) : id(value) {}
    };
    // 诊断所需的源码片段；不放入热 Instruction，避免每次取指搬运字符串。
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
    // 一个词法作用域的名称绑定；动态创建的变量单独使用动态槽窗口。
    struct Scope
    {
        const memory::Resource* value_resource;
        struct Binding
        {
            std::pmr::string name;
            RegisterSlots* file = nullptr;
            size_t slot = 0;
        };
        std::pmr::vector<Binding> bindings;
        std::unique_ptr<RegisterSlots> dynamic_registers;
        explicit Scope(const memory::Resource& resource = memory::default_resource())
            : value_resource(&resource), bindings(resource.get()) {}
        Binding* find(std::string_view name);
        Binding& create(const std::string& name);
        void bind(const std::string& name, RegisterSlots* file, size_t slot);
    };
    using ScopeStack = std::pmr::vector<Scope>;
    struct Machine;
    enum class WriteOperation { Assign, Add, Subtract, Multiply, Divide, Modulo, BitAnd, BitOr, BitXor, ShiftLeft, ShiftRight, PostAdd, PostSubtract, Invalid };
    static WriteOperation write_operation(const std::string& symbol);
    static std::optional<Opcode> write_opcode(WriteOperation operation);
    // 密封后的热执行指令，只保存运行时必需的操作数和控制信息。
    struct Instruction
    {
        Opcode opcode;
        size_t operand = 0;
        size_t auxiliary = 0;
        size_t member_site = 0;
        WriteOperation write = WriteOperation::Assign;
        size_t variable_site = 0;
        size_t destination = 0;
        size_t input_offset = 0;
        size_t input_count = 0;
        bool discard_result = false;
        bool plain_increment = false; // Compiled syntax fact; no declaration/type binding.
    };
    // 编译期指令，额外携带 SourceRef，seal() 后投影为热 Instruction。
    struct BuildInstruction
    {
        Opcode opcode;
        SourceRef source;
        size_t operand = 0;
        size_t auxiliary = 0;
        size_t member_site = 0;
        WriteOperation write = WriteOperation::Assign;
        size_t variable_site = 0;
        size_t destination = 0;
        size_t input_offset = 0;
        size_t input_count = 0;
        bool discard_result = false;
    };
    static_assert(sizeof(Instruction) == 80);
    static_assert(sizeof(BuildInstruction) == 88);
    // 与最终 PC 一一对应的冷诊断表，按需取得条件和赋值目标位置。
    struct InstructionDiagnostic
    {
        SourceRef source;
        SourceRef condition_source;
        SourceRef target_source;
    };
    static_assert(sizeof(InstructionDiagnostic) == sizeof(size_t) * 3);
    // 固定格式的数值热操作，供 NumericBinary 等专用路径使用。
    struct RegisterOperation
    {
        std::uint16_t opcode;
        std::uint16_t flags;
        std::uint32_t destination;
        std::uint32_t left;
        std::uint32_t right;
    };
    static_assert(sizeof(RegisterOperation) == 16);
    // 一段根代码或函数代码的构建期、执行期和验证元数据集合。
    struct Instructions
    {
        struct IntegerLoop
        {
            std::int64_t limit;
            size_t body;
            size_t exit;
        };
        std::pmr::memory_resource* resource;
        explicit Instructions(std::pmr::memory_resource* value = std::pmr::get_default_resource()) : resource(value) {}
        std::pmr::vector<BuildInstruction> build_code{resource};
        std::pmr::vector<Instruction> code{resource};
        std::pmr::vector<InstructionDiagnostic> diagnostics{resource};
        std::pmr::vector<RegisterOperation> numeric_operations{resource};
        std::pmr::vector<IntegerLoop> integer_loops{resource};
        std::pmr::vector<size_t> numeric_local_sites{resource};
        std::pmr::vector<size_t> register_inputs{resource};
        std::pmr::vector<std::pmr::vector<std::pair<size_t, bool>>> diagnostic_frames{resource};
        size_t register_capacity = 0;
        size_t temporary_count = 0;
        size_t loop_state_count = 0;
        size_t switch_count = 0;
        size_t range_count = 0;
        size_t method_argument_count = 0;
        size_t scope_capacity = 0;
    };
    struct CompactValue;
    // VM 内部数组；共享底层容器并在写入时复制，维持语言的按值隔离语义。
    struct VmArray
    {
        struct Values
        {
            using Container = std::pmr::vector<CompactValue>;
            using const_iterator = Container::const_iterator;
            struct Storage {
                memory::Resource resource;
                Container elements;
                explicit Storage(memory::Resource owner) : resource(std::move(owner)), elements(resource.get()) {}
                Storage(memory::Resource owner, const Container& source)
                    : resource(std::move(owner)), elements(source, resource.get()) {}
                Storage(memory::Resource owner, Container&& source)
                    : resource(std::move(owner)), elements(std::move(source), resource.get()) {}
            };
            std::shared_ptr<Storage> storage;

            explicit Values(const memory::Resource& resource = memory::default_resource());
            Values(size_t size, const memory::Resource& resource);
            Values(Container elements, const memory::Resource& resource);
            size_t size() const { return storage->elements.size(); }
            size_t capacity() const { return storage->elements.capacity(); }
            bool empty() const { return storage->elements.empty(); }
            operator const Container&() const { return storage->elements; }
            const CompactValue& operator[](size_t index) const { return (storage->elements)[index]; }
            CompactValue& operator[](size_t index) { return writable()[index]; }
            const CompactValue& front() const { return storage->elements.front(); }
            CompactValue& front() { return writable().front(); }
            const_iterator begin() const { return storage->elements.begin(); }
            const_iterator end() const { return storage->elements.end(); }
            const_iterator begin() { return storage->elements.begin(); }
            const_iterator end() { return storage->elements.end(); }
            void resize(size_t size) { writable().resize(size); }
            void reserve(size_t size) { writable().reserve(size); }
            void clear() { writable().clear(); }
            void push_back(CompactValue value);
            template<class... Arguments> void emplace_back(Arguments&&... arguments)
            {
                writable().emplace_back(std::forward<Arguments>(arguments)...);
            }
            void pop_back() { writable().pop_back(); }
            Container::iterator insert(const_iterator position, CompactValue value);
            Container::iterator erase(const_iterator position);

        private:
            Container& writable();
        };
        Values values;
        VmArray() = default;
        VmArray(size_t size, const memory::Resource& resource) : values(size, resource) {}
        VmArray(std::pmr::vector<CompactValue> elements, const memory::Resource& resource) : values(std::move(elements), resource) {}
    };
    // VM 内部 map；与 VmArray 相同，使用写时复制保持值隔离。
    struct VmMap
    {
        struct Values
        {
            using Container = std::pmr::map<std::pmr::string, CompactValue, std::less<>>;
            using const_iterator = Container::const_iterator;
            struct Storage {
                memory::Resource resource;
                Container elements;
                explicit Storage(memory::Resource owner) : resource(std::move(owner)), elements(resource.get()) {}
                Storage(memory::Resource owner, const Container& source)
                    : resource(std::move(owner)), elements(source, resource.get()) {}
                Storage(memory::Resource owner, Container&& source)
                    : resource(std::move(owner)), elements(std::move(source), resource.get()) {}
            };
            std::shared_ptr<Storage> storage;

            explicit Values(const memory::Resource& resource = memory::default_resource());
            Values(Container elements, const memory::Resource& resource);
            size_t size() const { return storage->elements.size(); }
            bool contains(std::string_view key) const { return storage->elements.find(key) != storage->elements.end(); }
            const_iterator begin() const { return storage->elements.begin(); }
            const_iterator end() const { return storage->elements.end(); }
            CompactValue& operator[](std::string_view key) {
                auto& container = writable();
                if (auto found = container.find(key); found != container.end()) return found->second;
                return container.try_emplace(std::pmr::string(key, container.get_allocator().resource())).first->second;
            }
            size_t erase(std::string_view key) { auto& container = writable(); auto found = container.find(key); if (found == container.end()) return 0; container.erase(found); return 1; }
            void clear() { writable().clear(); }
            const Container& readable() const { return storage->elements; }

        private:
            Container& writable();
        };
        Values values;
        VmMap() = default;
        explicit VmMap(const memory::Resource& resource) : values(resource) {}
        VmMap(const ObjectMap& elements, const memory::Resource& resource);
        VmMap(ObjectMap&& elements, const memory::Resource& resource);
        VmMap(Values::Container elements, const memory::Resource& resource) : values(std::move(elements), resource) {}
    };
    // 字符串保留其资源所有权，跨模块复制后不依赖原模块的存活期。
    struct VmString {
        memory::Resource allocation;
        std::pmr::string text;
        VmString(std::string_view value, const memory::Resource& resource)
            : allocation(resource), text(value, resource.get()) {}
        VmString(std::pmr::string&& value, const memory::Resource& resource)
            : allocation(resource), text(std::move(value), resource.get()) {}
        VmString(const VmString& other) : VmString(std::string_view(other.text), other.allocation) {}
        VmString(VmString&&) noexcept = default;
        VmString& operator=(const VmString& other) {
            if (this != &other) { VmString copy(other); *this = std::move(copy); }
            return *this;
        }
        VmString& operator=(VmString&& other) noexcept {
            if (this != &other) { std::destroy_at(this); std::construct_at(this, std::move(other)); }
            return *this;
        }
    };
    // VM 槽中的唯一值表示：基础类型内联，字符串保留 PMR 资源，数组/map 使用 COW，其余资源保留 any。
    struct CompactValue : std::variant<std::monostate, std::int64_t, double, bool, VmArray, VmMap, VmString, std::any>
    {
        // Copies share existing COW ownership; default construction is empty.
        using Storage = std::variant<std::monostate, std::int64_t, double, bool, VmArray, VmMap, VmString, std::any>;
        CompactValue() = default;
        using Storage::Storage;
        using Storage::operator=;
        explicit CompactValue(std::any value, const memory::Resource& allocation = memory::default_resource());
        CompactValue(const Object::Storage& value, const memory::Resource& allocation = memory::default_resource());
        CompactValue(Object::Storage&& value, const memory::Resource& allocation = memory::default_resource());
        CompactValue(const CompactValue& other);
        CompactValue(CompactValue&& other) noexcept;
        CompactValue& operator=(const CompactValue& other);
        CompactValue& operator=(CompactValue&& other) noexcept;

        void clear() { emplace<std::monostate>(); }
        bool empty() const { return std::holds_alternative<std::monostate>(*this); }
        static Storage import_resource(const std::any& value, const memory::Resource& allocation);
        static Storage import_resource(std::any&& value, const memory::Resource& allocation);
        Object::Storage export_storage() const;
        Object::Storage take_storage();
        template<class T> T* resource()
        {
            if constexpr (std::same_as<T, VmArray>)
            {
                return std::get_if<VmArray>(this);
            }
            else if constexpr (std::same_as<T, VmMap>)
            {
                return std::get_if<VmMap>(this);
            }
            if constexpr (std::same_as<T, std::pmr::string>) {
                auto* string = std::get_if<VmString>(this);
                return string ? &string->text : nullptr;
            }
            auto* value = std::get_if<std::any>(this);
            return value ? std::any_cast<T>(value) : nullptr;
        }
        template<class T> const T* resource() const
        {
            return const_cast<CompactValue*>(this)->resource<T>();
        }
        template<class T> bool holds() const
        {
            return std::holds_alternative<T>(*this);
        }
        template<class T> T* get_if()
        {
            return std::get_if<T>(this);
        }
        template<class T> const T* get_if() const
        {
            return const_cast<CompactValue*>(this)->get_if<T>();
        }
        template<class T> T& get() { return *get_if<T>(); }
        template<class T> const T& get() const { return *get_if<T>(); }
        using Storage::emplace;
    };
    template<class T> static bool value_holds(const CompactValue& value) { return value.holds<T>(); }
    template<class T> static T* value_get_if(CompactValue* value) { return value ? value->get_if<T>() : nullptr; }
    template<class T> static const T* value_get_if(const CompactValue* value) { return value ? value->get_if<T>() : nullptr; }
    template<class T> static T& value_get(CompactValue& value) { return value.get<T>(); }
    template<class T> static const T& value_get(const CompactValue& value) { return value.get<T>(); }
    template<class T, class... Types> static bool value_holds(const std::variant<Types...>& value)
    {
        return std::holds_alternative<T>(value);
    }
    template<class T, class... Types> static T* value_get_if(std::variant<Types...>* value)
    {
        return std::get_if<T>(value);
    }
    template<class T, class... Types> static const T* value_get_if(const std::variant<Types...>* value)
    {
        return std::get_if<T>(value);
    }
    template<class T, class... Types> static T& value_get(std::variant<Types...>& value)
    {
        return std::get<T>(value);
    }
    template<class T, class... Types> static const T& value_get(const std::variant<Types...>& value)
    {
        return std::get<T>(value);
    }
    struct BytecodeValue
    {
        using Storage = CompactValue;
        Storage value;
    };
    static_assert(sizeof(BytecodeValue) == sizeof(BytecodeValue::Storage));
    // 常量池条目；continue_marker 仅用于编译期控制流归约。
    struct ConstantValue
    {
        BytecodeValue::Storage value;
        bool continue_marker = false;
        ConstantValue(Object object, const memory::Resource& resource) : value(std::move(object.value), resource) {}
        explicit ConstantValue(bool boolean) : value(boolean) {}
        explicit ConstantValue(int integer) : value(std::int64_t(integer)) {}
        ConstantValue(const std::string& text, const memory::Resource& resource) : value(VmString(text, resource)) {}
        ConstantValue(ObjectVector elements, const memory::Resource& resource) : value(std::any(std::move(elements)), resource) {}
        ConstantValue(const char* text, bool marker) : value(std::any(std::string(text))), continue_marker(marker) {}
    };
    // 连续寄存器文件的一个窗口。函数参数、局部和临时值共享同一存储，通过 base/size 划分生命周期。
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
        // 与槽下标并行的负载、数值绑定、诊断来源和类型描述；扩容必须同步进行。
        struct Storage
        {
            memory::Resource resource;
            std::pmr::vector<BytecodeValue> values;
            std::pmr::vector<NumericBinding> bindings;
            std::pmr::vector<const Object*> origins;
            std::pmr::vector<size_t> names;
            std::pmr::deque<std::pmr::string> name_pool;
            std::pmr::unordered_map<std::pmr::string, size_t, memory::StringHash, memory::StringEqual> name_ids;
            std::pmr::vector<size_t> types;
            std::pmr::deque<TypeDescriptor> type_pool;
            Storage(size_t count, memory::Resource value_resource)
                : resource(std::move(value_resource)), values(count, resource.get()), bindings(count, resource.get()),
                  origins(count, resource.get()), names(count, resource.get()), name_pool(resource.get()),
                  name_ids(resource.get()), types(count, resource.get()), type_pool(resource.get()) {
                name_pool.emplace_back(); type_pool.emplace_back();
            }
        };
        std::unique_ptr<Storage> storage;
        std::pmr::vector<BytecodeValue>& values;
        Storage& owner;
        std::pmr::vector<NumericBinding>& bindings;
        std::pmr::vector<const Object*>& origins;
        std::pmr::vector<size_t>& slot_names;
        std::pmr::deque<std::pmr::string>& name_pool;
        std::pmr::unordered_map<std::pmr::string, size_t, memory::StringHash, memory::StringEqual>& name_ids;
        std::pmr::vector<size_t>& slot_types;
        std::pmr::deque<TypeDescriptor>& type_pool;
        size_t window_base = 0;
        size_t window_top = 0;
        size_t window_size = 0;

        explicit RegisterSlots(size_t count = 0, const memory::Resource& resource = memory::default_resource())
            : storage(std::make_unique<Storage>(count, resource)), values(storage->values), owner(*storage), bindings(storage->bindings),
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
        void enter(size_t count); // 在当前顶端创建子窗口。
        size_t append(); // 向当前窗口追加一个已清空的槽。
        void grow(size_t required); // 同步扩容所有并行槽元数据。
        void restore(size_t base, size_t size, size_t top); // 释放退出窗口的值并恢复外层游标。
        void export_object(size_t slot, Object& destination) const; // 复制导出到公开 Object 边界。
        void export_argument(size_t slot, Object& destination); // 消费槽值并导出，用于返回值或实参。
        void export_metadata(size_t slot, Object& destination) const; // 填写类型、名称和来源，不处理负载。
        void import_object(size_t slot, const Object& value);
        void import_object(size_t slot, Object&& value);
        template<class Value> void import_value(size_t slot, Value&& value);
        void clear(size_t slot);
        void set_name(size_t slot, std::string_view name);
        void set_type(size_t slot, const TypeDescriptor& type);
        const BytecodeValue::Storage& payload(size_t slot) const;
        BytecodeValue::Storage& resource_payload(size_t slot);
        void release_payload(size_t slot);
        void store_payload(size_t slot, BytecodeValue::Storage value);
        void store_payload(size_t slot, const Object::Storage& value);
        void store_payload(size_t slot, Object::Storage&& value);
        template<class Number> void write_number(size_t slot, Number number, bool preserve_binding = false);
        void write_payload(size_t slot, BytecodeValue::Storage value);
        void write_payload(size_t slot, std::any value);
        void write_text(size_t slot, std::string_view value) { write_payload(slot, CompactValue(VmString(value, owner.resource))); }
        void write_text(size_t slot, std::pmr::string&& value) { write_payload(slot, CompactValue(VmString(std::move(value), owner.resource))); }
        void write_payload(size_t slot, std::int64_t value);
        void write_payload(size_t slot, double value);
        void write_payload(size_t slot, bool value);
        void copy(size_t destination, const RegisterSlots& source, size_t slot);
        void move(size_t destination, RegisterSlots& source, size_t slot);
        bool assign_numeric(size_t destination, const RegisterSlots& source, size_t slot);
        bool cast_numeric(size_t destination, const RegisterSlots& source, size_t slot, const std::string& type);
        bool cast_numeric(size_t destination, const RegisterSlots& source, size_t slot, NumericBinding binding);
        void bind_numeric(size_t slot, NumericBinding binding);
        bool has_name(size_t slot) const { return slot_names[window_base + slot] != 0; }
        bool integer(size_t slot, std::int64_t& result) const;
        bool number(size_t slot, std::int64_t& integer, double& floating, bool& is_double) const;
        bool empty(size_t slot) const
        {
            const size_t index = window_base + slot;
            return values[index].value.empty();
        }
        bool binary(Opcode opcode, size_t destination, size_t left, size_t right,
            Machine& machine, const SourceLocation& location);
        void binary_fallback(Opcode opcode, size_t destination, size_t left, size_t right,
            Machine& machine, const SourceLocation& location, bool optimized);
        bool binary_payloads(Opcode opcode, size_t destination, const BytecodeValue::Storage& left_payload,
            const BytecodeValue::Storage& right_payload, Machine& machine, const SourceLocation& location);
        bool binary_numbers(Opcode opcode, size_t destination, std::int64_t left_integer, double left_number, bool left_double,
            std::int64_t right_integer, double right_number, bool right_double, Machine& machine, const SourceLocation& location,
            bool preserve_binding = false);
    };
    // RegisterBinary 的冷站点信息，保存名称和精确诊断来源。
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
    std::pmr::unordered_map<const CalUnit*, size_t> source_ids{allocation_resource.get()};
    std::pmr::unordered_map<size_t, const CalUnit*> compile_sources{allocation_resource.get()};
    CalUnit* active_statement_node = nullptr;
    std::string translation_error;
    std::string runtime_error;
    std::pmr::unordered_map<std::string, size_t> name_ids{allocation_resource.get()};
    // 已解析的索引表达式站点，避免执行期重复解析名称和声明形态。
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
    // 已解析的变量声明或写入站点。
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
    // 调用站点的参数诊断、接收者和可选数值快路径描述。
    struct CallSite
    {
        explicit CallSite(std::pmr::memory_resource* resource = std::pmr::get_default_resource()) : arguments(resource) {}
        CallSite(SourceRef value, std::pmr::memory_resource* resource) : source(value), arguments(resource) {}
        SourceRef source;
        std::pmr::vector<SourceLocation> arguments;
        size_t local_slot = 0;
        bool global_receiver = false;
        size_t name_id = 0;
        size_t base_name_id = 0;
        SourceRef method_source;
        MathKind math_kind = MathKind::None;
        std::array<size_t, 2> math_local_slots{};
        std::array<size_t, 2> math_local_names{};
        bool math_local_operands = false;
    };
    struct Module;
    // 已编译脚本函数及其局部槽需求。
    struct FunctionCode
    {
        explicit FunctionCode(std::pmr::memory_resource* resource) : parameters(resource), instructions(resource) {}
        struct Parameter
        {
            std::string name;
            std::string type_name;
        };
        std::pmr::vector<Parameter> parameters;
        std::string name;
        std::string return_type;
        SourceRef body_source;
        Instructions instructions;
        size_t local_slot_count = 0;
    };
    // 一次编译产生的不可变代码、常量、名称和源码诊断目录。
    struct Module
    {
        memory::Resource resource;
        explicit Module(memory::Resource value) : resource(std::move(value)), root_instructions(resource.get()) {}
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
        std::pmr::vector<SourceLine> source_lines{resource.get()};
        std::pmr::unordered_map<std::string, size_t> entry_labels{resource.get()};
        std::unordered_map<std::string, std::vector<StructField>> structures;
        std::pmr::deque<SourceLocation> sources{resource.get()};
        std::pmr::vector<ConstantValue> constants{resource.get()};
        std::pmr::deque<std::string> names{resource.get()};
        size_t int_type_id = 0;
        size_t double_type_id = 0;
        std::pmr::vector<IndexSite> index_sites{resource.get()};
        std::pmr::vector<VariableSite> variable_sites{resource.get()};
        std::pmr::vector<RegisterBinarySite> register_binary_sites{resource.get()};
        std::pmr::vector<std::pair<size_t, size_t>> member_sites{resource.get()};
        Instructions root_instructions;
        SourceRef root_source;
        std::pmr::vector<size_t> root_entries{resource.get()};
        std::pmr::deque<CallSite> calls{resource.get()};
        std::pmr::unordered_map<std::string, std::pmr::unordered_map<size_t, std::shared_ptr<FunctionCode>>> function_code{resource.get()};
        const SourceLocation& source(const SourceRef& reference) const { return sources.at(reference.id - 1); }
    };
    // 单次或嵌套执行的 VM 状态；全局槽、作用域、调用缓存和错误状态均归属此对象。
    struct Machine
    {
        struct ReturnState
        {
            std::string return_type;
        };
        CifaBytecode& host;
        RegisterSlots registers;
        RegisterSlots global_values;
        std::pmr::unordered_map<std::string, size_t> global_slots;
        std::pmr::vector<bool> global_exists;
        ScopeStack scopes;
        std::pmr::unordered_map<std::string, std::pmr::unordered_map<size_t, std::shared_ptr<const Module>>> functions;
        struct CachedFunction
        {
            std::shared_ptr<const Module> owner;
            const FunctionCode* code = nullptr;
        };
        std::pmr::unordered_map<const CallSite*, CachedFunction> function_cache;
        std::unordered_map<std::string, std::vector<StructField>> structures;
        size_t function_version = 0;
        const Module* frozen_function_module = nullptr;
        std::pmr::vector<ReturnState> returns;
        std::pmr::vector<std::pair<const SourceLocation*, bool>> call_stack;
        std::function<void(std::pmr::vector<std::pair<const SourceLocation*, bool>>&)> append_diagnostic_frames;
        std::string error;
        Object error_placeholder;
        bool exit_requested = false;

        // 指向局部、动态作用域或全局槽的可写命名值引用。
        struct NamedValueRef
        {
            RegisterSlots* file = nullptr;
            size_t slot = 0;
            std::string element_type;
            bool existed = false;

            CompactValue* resource()
            {
                return file ? &file->resource_payload(slot) : nullptr;
            }
            const CompactValue* resource() const
            {
                return const_cast<NamedValueRef*>(this)->resource();
            }
            bool empty() const
            {
                return !file || file->empty(slot);
            }
            std::optional<size_t> size() const
            {
                const auto* value = resource();
                if (!value) return std::nullopt;
                if (const auto* text = value->resource<std::pmr::string>()) return text->size();
                if (const auto* map = value->resource<VmMap>()) return map->values.size();
                if (const auto* array = value->resource<VmArray>()) return array->values.size();
                return std::nullopt;
            }
        };
        // 索引结果的统一引用，可指向 VM 内部值或宿主 Object 边界。
        struct IndexedValueRef
        {
            CompactValue* compact = nullptr;
            Object* object = nullptr;
            std::string name;
            std::string element_type;
            RegisterSlots* file = nullptr;
            size_t slot = 0;
        };

        explicit Machine(CifaBytecode& value_host)
            : host(value_host), registers(0,host.allocation_resource), global_values(0,host.allocation_resource),
              global_slots(host.allocation_resource.get()), global_exists(host.allocation_resource.get()),
              scopes(host.allocation_resource.get()), functions(host.allocation_resource.get()),
              function_cache(host.allocation_resource.get()), returns(host.allocation_resource.get()),
              call_stack(host.allocation_resource.get()) { }
    size_t ensure_global_slot(const std::string& name);
    size_t find_global_slot(const std::string& name) const;
    bool global_exists_at(size_t slot) const;
    void import_host_globals();
    void export_host_globals();
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
        NamedValueRef named_value(const std::string& name);
        IndexedValueRef resolve_member(const std::string& base_name, const std::string& field_name);
        void read_named(RegisterSlots& destination, size_t slot, const std::string& name, const std::string& type_name, bool with_type, bool only_check,
            bool initialize_struct, const SourceLocation& location);
        NamedValueRef assign_named(const std::string& name, const std::string& type_name, bool with_type, bool declare_current,
            const SourceLocation& location);
        bool assign(Object& target, Object value, bool with_type, const std::string& type_name, const SourceLocation& location);
        bool assign(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
            size_t scratch, const SourceLocation& location, bool take_value = false);
        bool bind_type(Object& value, const std::string& type_name, const SourceLocation& location);
        bool bind_type(RegisterSlots& values, size_t slot, const std::string& type_name, const SourceLocation& location);
        Object convert_type(const Object& value, const std::string& type_name, const SourceLocation& location);
        bool convert_type(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
            const std::string& type_name, const SourceLocation& location);
        bool condition(RegisterSlots& source, size_t slot, const SourceLocation* location);
        void conversion_error(RegisterSlots& source, size_t slot, const std::string& target, const SourceLocation* location);
        // 仅借用当前槽的字符串；槽被修改或发生嵌套执行前，调用者必须消费完或复制。
        std::string_view string_value(RegisterSlots& source, size_t slot);
        bool range(RegisterSlots& source, size_t slot, const SourceLocation& location,
            RegisterSlots& destination, size_t target);
        bool bind_range(RegisterSlots& values, size_t slot, const std::string& name, const std::string& type_name, const SourceLocation& location);
        Object call_host(const std::string& name, ObjectVector& arguments, const std::pmr::vector<SourceLocation>& locations);
        bool call_native_registers(const std::string& name, RegisterSlots& destination, size_t result,
            RegisterSlots& values, const size_t* arguments, size_t count,
            const std::pmr::vector<SourceLocation>& locations);
        bool call_builtin_registers(const std::string& name, RegisterSlots& destination, size_t result,
            RegisterSlots& values, const size_t* arguments, size_t count);
        void call_method(RegisterSlots& destination, size_t slot, const std::string& name, const SourceLocation& location,
            NamedValueRef& receiver, const std::pmr::vector<SourceLocation>& locations, RegisterSlots& arguments);
        IndexedValueRef indexed(const std::string& name, const std::string& type_name, size_t dimensions, bool is_decl_array,
            bool only_check, bool declare_current, RegisterSlots& indices, const size_t* index_slots);
        void read_indexed(RegisterSlots& destination, size_t slot, const IndexedValueRef& element, bool map_access);
        bool assign_indexed(const IndexedValueRef& target, Object value, bool with_type,
            const std::string& type_name, const SourceLocation& location);
        Object make_no_value(const std::string& function_name, const SourceLocation& call_site) const;
        bool should_stop() const { return exit_requested || !error.empty(); }
    };
    // 兼容 Playground 的旧诊断接口：热指令保持不变，只在采样开启时记录。
    struct ProfileInstructionGuard
    {
        CifaBytecode& owner;
        std::string function_id;
        std::vector<std::string> stack_snapshot;
        size_t pc = 0;
        size_t previous_pc = 0;
        std::uint64_t start_ns = 0;

        ProfileInstructionGuard(CifaBytecode& owner, std::string function_id,
            std::vector<std::string> stack_snapshot, size_t pc, size_t previous_pc);
        ~ProfileInstructionGuard();
    };

    struct ProfileState
    {
        struct Metric
        {
            size_t count = 0;
            std::uint64_t time_ns = 0;
        };
        struct FunctionMetric
        {
            size_t calls = 0;
            size_t instructions = 0;
            std::uint64_t self_ns = 0;
            std::uint64_t total_ns = 0;
        };
        struct FlameMetric
        {
            std::uint64_t self_ns = 0;
            std::uint64_t total_ns = 0;
        };

        bool enabled = false;
        bool truncated = false;
        size_t instruction_limit = 2000000;
        size_t instruction_count = 0;
        std::uint64_t total_ns = 0;
        std::unordered_map<std::string, Metric> instructions;
        std::unordered_map<std::string, Metric> edges;
        std::unordered_map<std::string, FunctionMetric> functions;
        std::unordered_map<std::string, FlameMetric> flames;
        std::vector<std::string> stack;
        std::vector<size_t> last_pc;
    };

    ProfileState profile_state;

    std::shared_ptr<Module> module_data = std::make_shared<Module>(allocation_resource);
    bool& compiled_valid = module_data->compiled_valid;
    std::pmr::vector<Module::SourceLine>& source_lines = module_data->source_lines;
    std::pmr::unordered_map<std::string, size_t>& entry_labels = module_data->entry_labels;
    std::pmr::deque<SourceLocation>& sources = module_data->sources;
    std::pmr::vector<ConstantValue>& constants = module_data->constants;
    std::pmr::deque<std::string>& names = module_data->names;
    std::pmr::vector<IndexSite>& index_sites = module_data->index_sites;
    std::pmr::vector<VariableSite>& variable_sites = module_data->variable_sites;
    std::pmr::vector<std::pair<size_t, size_t>>& member_sites = module_data->member_sites;
    Instructions& root_instructions = module_data->root_instructions;
    SourceRef& root_source = module_data->root_source;
    std::pmr::vector<size_t>& root_entries = module_data->root_entries;
    std::pmr::deque<CallSite>& calls = module_data->calls;
    decltype(Module::function_code)& function_code = module_data->function_code;
    struct Loop
    {
        Loop(size_t scope_count, size_t trace_count, std::pmr::memory_resource* resource, bool switch_loop = false)
            : scopes(scope_count), traces(trace_count), breaks(resource), continues(resource), is_switch(switch_loop) {}
        size_t scopes;
        size_t traces;
        std::pmr::vector<size_t> breaks;
        std::pmr::vector<size_t> continues;
        bool is_switch = false;
    };
    std::pmr::vector<Loop> compile_loops{allocation_resource.get()};
    struct LabelBlock
    {
        LabelBlock(size_t value, std::pmr::memory_resource* resource) : mark(value), targets(resource), jumps(resource) {}
        LabelBlock(const LabelBlock&) = delete;
        LabelBlock(LabelBlock&&) = default;
        LabelBlock& operator=(LabelBlock&&) = default;
        size_t mark;
        std::pmr::unordered_map<std::string, size_t> targets;
        std::pmr::vector<std::pair<size_t, std::string>> jumps;
    };
    std::pmr::vector<LabelBlock> compile_blocks{allocation_resource.get()};
    size_t compile_scopes = 0;
    size_t compile_traces = 0;
    FunctionCode* compiling_function = nullptr;
    std::pmr::vector<std::pmr::unordered_map<std::string, size_t>> compile_local_scopes{allocation_resource.get()};
    std::pmr::unordered_map<const CalUnit*, bool> compile_scope_effects{allocation_resource.get()};
    std::pmr::vector<size_t> compile_local_scope_bases{allocation_resource.get()};
    std::pmr::unordered_set<std::string> compile_array_locals{allocation_resource.get()};
    const std::unordered_map<std::string, FunctionOverloads>* compile_script_functions = nullptr;
    bool compile_allows_script_constant_folding = false;
    std::pmr::unordered_set<std::string> compile_inline_functions{allocation_resource.get()};
    std::pmr::unordered_map<const std::pmr::vector<BuildInstruction>*, std::pmr::vector<InstructionDiagnostic>> pending_diagnostics{allocation_resource.get()};

    // 编译器辅助函数：建立冷源码表、密封指令流，并在 compact 时重映射控制流 PC。
    size_t source_id(const CalUnit& source);
    SourceRef source_ref(const CalUnit* node);
    InstructionDiagnostic& pending_diagnostic(std::pmr::vector<BuildInstruction>& instructions);
    size_t intern_name(const std::string& name);
    size_t index_site(const CalUnit& node);
    void seal(Instructions& instructions);
    void seal_calls();
    std::pmr::vector<size_t> compact(Instructions& instructions);
    SourceLocation& source(const SourceRef& reference) const;
    std::optional<size_t> local_slot(const CalUnit& node, bool declare, bool allow_untyped_declaration = false);
    size_t next_local_slot() const;
    bool block_needs_runtime_scope(const CalUnit& node);
    void analyze_binding_scopes(const CalUnit& node, bool custom_conversions);
    static bool operation(const CalUnit& node, Opcode& opcode);
    bool try_fold_constant(const CalUnit& node, Object& value,
        const std::pmr::unordered_map<std::string, Object>* parameters = nullptr,
        std::pmr::unordered_set<std::string>* active_functions = nullptr) const;
    void emit(CalUnit& node, std::pmr::vector<BuildInstruction>& instructions);
    size_t emit_statement(CalUnit& node, std::pmr::vector<BuildInstruction>& instructions);
    static void discard_statement_result(std::pmr::vector<BuildInstruction>& instructions, size_t begin,
        std::optional<size_t> end = std::nullopt);
    bool emit_register_expression(CalUnit& node, std::pmr::vector<BuildInstruction>& instructions);
    bool verify(Instructions& instructions, size_t local_slot_count = 0);
    struct InterpState;
    static bool execute_instructions(Machine& machine, const Module& module, const Instructions& instructions,
        Object& result, size_t start = 0);
    static Object run_module(Machine& machine, const Module& module);

public:
    // 高性能宿主函数的受控寄存器视图，不暴露 RegisterSlots 的窗口和扩容细节。
    class NativeCallContext
    {
        friend struct Machine;
        Machine& machine;
        RegisterSlots& destination;
        RegisterSlots& arguments;
        const size_t* argument_slots;
        const std::pmr::vector<SourceLocation>& locations;
        size_t result_slot;
        size_t argument_count_value;
        bool result_written = false;

        NativeCallContext(Machine& value_machine, RegisterSlots& value_destination, size_t value_result_slot,
            RegisterSlots& value_arguments, const size_t* value_argument_slots, size_t value_argument_count,
            const std::pmr::vector<SourceLocation>& value_locations);

    public:
        size_t argument_count() const { return argument_count_value; }
        bool is_empty(size_t index) const;
        bool is_integer(size_t index) const;
        bool is_number(size_t index) const;
        bool is_boolean(size_t index) const;
        bool is_string(size_t index) const;
        std::int64_t to_integer(size_t index) const;
        double to_number(size_t index) const;
        bool to_boolean(size_t index) const;
        std::string to_string(size_t index) const;
        template<class T> const T* resource(size_t index) const
        {
            if (index >= argument_count_value) return nullptr;
            const auto& value = arguments.payload(argument_slots[index]);
            const auto* payload = value_get_if<std::any>(&value);
            return payload ? std::any_cast<T>(payload) : nullptr;
        }
        void set_result(std::int64_t value);
        void set_result(double value);
        void set_result(bool value);
        void set_result(std::string value);
        template<class T> void set_resource(T value)
        {
            destination.write_payload(result_slot, std::any(std::move(value)));
            result_written = true;
        }
        void set_empty_result();
        void report_error(const std::string& message);
    };
    using native_func_type = std::function<void(NativeCallContext&)>;

    // 只读编译统计；用于外部观测，不公开私有指令布局。
    struct BytecodeStatistics
    {
        struct Function
        {
            std::string name;
            size_t arity = 0;
            size_t instruction_count = 0;
            size_t operand_count = 0;
            size_t register_capacity = 0;
        };
        size_t instruction_size = 0;
        size_t root_instruction_count = 0;
        size_t root_operand_count = 0;
        size_t root_register_capacity = 0;
        size_t constant_count = 0;
        size_t call_site_count = 0;
        size_t total_instruction_count = 0;
        size_t integer_loop_count = 0;
        size_t total_operand_count = 0;
        std::vector<Function> functions;
    };

    // 运行期 RAII 会话：持有 Machine，并在嵌套执行边界同步全局状态。
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
        bool is_active() const { return active; }
        size_t script_function_version() const;
        void sync_globals_to_host();

    private:
        std::unique_ptr<Machine> machine;
        bool active = false;
    };

    CifaBytecode();
    explicit CifaBytecode(memory::Resource resource);
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
    std::string get_cfg_json() const;
    void set_profiling_enabled(bool enabled);
    bool is_profiling_enabled() const;
    void reset_profile();
    std::string get_profile_json() const;
    Object run(const std::string& entry_label = {});
    Object run_script(std::string script);
    Object run_file(const std::string& filename);
    bool register_native_function(const std::string& name, native_func_type function);
    BytecodeStatistics bytecode_statistics() const;

private:
    struct NativeFunction
    {
        native_func_type function;
        bool builtin = false;
    };
    bool register_builtin(const std::string& name);
    void profile_enter_function(const std::string& id);
    void profile_leave_function();
    void record_profile_instruction(const std::string& function_id, size_t pc,
        size_t previous_pc, const std::vector<std::string>& stack, std::uint64_t duration_ns);
    void translate(Cifa& compiler, size_t script_function_version, size_t host_native_function_version);
    void prepare_compile_visibility();
    void clear_compile_visibility();
    std::unique_ptr<Session> session;
    std::pmr::vector<std::unique_ptr<CifaBytecode>> nested_modules{allocation_resource.get()};
    std::unordered_map<std::string, FunctionOverloads> persistent_functions;
    std::unordered_map<std::string, std::vector<StructField>> persistent_struct_defs;
    std::pmr::unordered_map<std::string, NativeFunction> native_functions{allocation_resource.get()};
    std::unordered_set<std::string> native_function_names;
    size_t native_function_version = 0;
    bool optimization_enabled = true;
};
}
