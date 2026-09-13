#include "CifaBytecode.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>

namespace cifa
{
#ifdef CIFA_VALUE_PROFILE
struct ValueProfile
{
    std::uint64_t object_imports = 0, object_exports = 0, metadata_allocations = 0;
    std::uint64_t copies = 0, object_copies = 0, clears = 0, object_clears = 0;
    std::uint64_t resource_allocations = 0, resource_copies = 0;
    std::uint64_t array_imports = 0, array_import_elements = 0;
    std::uint64_t array_exports = 0, array_export_elements = 0;
    std::uint64_t array_copies = 0, array_copy_elements = 0;
    std::uint64_t array_import_const = 0, array_import_move = 0;
    std::uint64_t array_export_copy = 0, array_export_take = 0;
    ~ValueProfile()
    {
        std::cout << std::dec << "ValueProfile object_imports=" << object_imports << " object_exports=" << object_exports
            << " metadata_allocations=" << metadata_allocations << " copies=" << copies
            << " object_copies=" << object_copies << " clears=" << clears << " object_clears=" << object_clears
            << " resource_allocations=" << resource_allocations << " resource_copies=" << resource_copies
            << " array_imports=" << array_imports << " array_import_elements=" << array_import_elements
            << " array_exports=" << array_exports << " array_export_elements=" << array_export_elements
            << " array_copies=" << array_copies << " array_copy_elements=" << array_copy_elements
            << " array_import_const=" << array_import_const << " array_import_move=" << array_import_move
            << " array_export_copy=" << array_export_copy << " array_export_take=" << array_export_take
            << " sizeof_Object=" << sizeof(Object) << '\n';
    }
};
static thread_local ValueProfile value_profile;
#endif
CifaBytecode::CompactValue::CompactValue(std::int64_t value)
{
    payload.integer = value;
    tag = Tag::Integer;
}

CifaBytecode::CompactValue::CompactValue(double value)
{
    payload.floating = value;
    tag = Tag::Floating;
}

CifaBytecode::CompactValue::CompactValue(bool value)
{
    payload.boolean = value;
    tag = Tag::Boolean;
}

CifaBytecode::CompactValue::CompactValue(std::any value)
{
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.resource_allocations;
#endif
    payload.resource = new Resource(import_resource(std::move(value)));
    tag = Tag::Resource;
}

CifaBytecode::CompactValue::CompactValue(const Object::Storage& value)
{
    if (const auto* integer = value_get_if<std::int64_t>(&value)) { payload.integer = *integer; tag = Tag::Integer; }
    else if (const auto* floating = value_get_if<double>(&value)) { payload.floating = *floating; tag = Tag::Floating; }
    else if (const auto* boolean = value_get_if<bool>(&value)) { payload.boolean = *boolean; tag = Tag::Boolean; }
    else if (const auto* resource = value_get_if<std::any>(&value))
    {
#ifdef CIFA_VALUE_PROFILE
        ++value_profile.resource_allocations;
        if (std::any_cast<ObjectVector>(resource)) ++value_profile.array_import_const;
#endif
        payload.resource = new Resource(import_resource(*resource));
        tag = Tag::Resource;
    }
}

CifaBytecode::CompactValue::CompactValue(Object::Storage&& value)
{
    if (auto* integer = value_get_if<std::int64_t>(&value)) { payload.integer = *integer; tag = Tag::Integer; }
    else if (auto* floating = value_get_if<double>(&value)) { payload.floating = *floating; tag = Tag::Floating; }
    else if (auto* boolean = value_get_if<bool>(&value)) { payload.boolean = *boolean; tag = Tag::Boolean; }
    else if (auto* resource = value_get_if<std::any>(&value))
    {
#ifdef CIFA_VALUE_PROFILE
        ++value_profile.resource_allocations;
        if (std::any_cast<ObjectVector>(resource)) ++value_profile.array_import_move;
#endif
        payload.resource = new Resource(import_resource(std::move(*resource)));
        tag = Tag::Resource;
    }
}

CifaBytecode::CompactValue::CompactValue(const CompactValue& other) : tag(other.tag)
{
    if (tag == Tag::Resource)
    {
#ifdef CIFA_VALUE_PROFILE
        ++value_profile.resource_allocations;
        ++value_profile.resource_copies;
        if (const auto* array = std::any_cast<VmArray>(&other.payload.resource->value))
        {
            ++value_profile.array_copies;
            value_profile.array_copy_elements += array->values.size();
        }
#endif
        payload.resource = new Resource(other.payload.resource->value);
    }
    else payload.bits = other.payload.bits;
}

CifaBytecode::CompactValue::CompactValue(CompactValue&& other) noexcept : payload(other.payload), tag(other.tag)
{
    other.payload.bits = 0;
    other.tag = Tag::Empty;
}

CifaBytecode::CompactValue& CifaBytecode::CompactValue::operator=(const CompactValue& other)
{
    if (this == &other) return *this;
    CompactValue copy(other);
    *this = std::move(copy);
    return *this;
}

CifaBytecode::CompactValue& CifaBytecode::CompactValue::operator=(CompactValue&& other) noexcept
{
    if (this == &other) return *this;
    clear();
    payload = other.payload;
    tag = other.tag;
    other.payload.bits = 0;
    other.tag = Tag::Empty;
    return *this;
}

CifaBytecode::CompactValue::~CompactValue()
{
    clear();
}

void CifaBytecode::CompactValue::clear()
{
    if (tag == Tag::Resource) delete payload.resource;
    payload.bits = 0;
    tag = Tag::Empty;
}

std::any CifaBytecode::CompactValue::import_resource(const std::any& value)
{
    const auto* objects = std::any_cast<ObjectVector>(&value);
    if (!objects) return value;
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.array_imports;
    value_profile.array_import_elements += objects->size();
#endif
    std::vector<CompactValue> elements;
    elements.reserve(objects->size());
    for (const auto& object : *objects) elements.emplace_back(object.value);
    return VmArray(std::move(elements));
}

std::any CifaBytecode::CompactValue::import_resource(std::any&& value)
{
    auto* objects = std::any_cast<ObjectVector>(&value);
    if (!objects) return std::move(value);
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.array_imports;
    value_profile.array_import_elements += objects->size();
#endif
    std::vector<CompactValue> elements;
    elements.reserve(objects->size());
    for (auto& object : *objects) elements.emplace_back(std::move(object.value));
    return VmArray(std::move(elements));
}

std::any CifaBytecode::CompactValue::export_resource(const std::any& value)
{
    const auto* array = std::any_cast<VmArray>(&value);
    if (!array) return value;
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.array_exports;
    value_profile.array_export_elements += array->values.size();
#endif
    ObjectVector objects(array->values.size());
    for (size_t index = 0; index < array->values.size(); ++index)
        objects[index].value = array->values[index].export_storage();
    return objects;
}

Object::Storage CifaBytecode::CompactValue::export_storage() const
{
    switch (tag)
    {
    case Tag::Integer: return payload.integer;
    case Tag::Floating: return payload.floating;
    case Tag::Boolean: return payload.boolean;
    case Tag::Resource:
#ifdef CIFA_VALUE_PROFILE
        if (std::any_cast<VmArray>(&payload.resource->value)) ++value_profile.array_export_copy;
#endif
        return export_resource(payload.resource->value);
    default: return std::monostate{};
    }
}

Object::Storage CifaBytecode::CompactValue::take_storage()
{
    Object::Storage result;
    switch (tag)
    {
    case Tag::Integer: result = payload.integer; break;
    case Tag::Floating: result = payload.floating; break;
    case Tag::Boolean: result = payload.boolean; break;
    case Tag::Resource:
        if (std::any_cast<VmArray>(&payload.resource->value))
        {
#ifdef CIFA_VALUE_PROFILE
            ++value_profile.array_export_take;
#endif
            result = export_resource(payload.resource->value);
        }
        else result = std::move(payload.resource->value);
        break;
    default: break;
    }
    clear();
    return result;
}

CifaBytecode::Scope::Binding* CifaBytecode::Scope::find(const std::string& name)
{
    for (auto& binding : bindings)
        if (binding.name == name) return &binding;
    return nullptr;
}

CifaBytecode::Scope::Binding& CifaBytecode::Scope::create(const std::string& name)
{
    if (auto* existing = find(name)) return *existing;
    if (!dynamic_registers) dynamic_registers = std::make_unique<RegisterSlots>();
    const size_t slot = dynamic_registers->append();
    bindings.push_back({name, dynamic_registers.get(), slot});
    return bindings.back();
}

void CifaBytecode::Scope::bind(const std::string& name, RegisterSlots* file, size_t slot)
{
    if (auto* existing = find(name))
    {
        existing->file = file;
        existing->slot = slot;
    }
    else bindings.push_back({name, file, slot});
}

size_t CifaBytecode::RegisterSlots::append()
{
    const size_t slot = window_size;
    const size_t required = window_base + slot + 1;
    if (values.size() < required)
    {
        values.resize(required);
        bindings.resize(required);
        origins.resize(required);
        slot_names.resize(required);
        slot_types.resize(required);
    }
    ++window_size;
    window_top = required;
    clear(slot);
    return slot;
}

void CifaBytecode::RegisterSlots::enter(size_t count)
{
    window_base = window_top;
    window_size = count;
    window_top += count;
    if (values.size() < window_top)
    {
        values.resize(window_top);
        bindings.resize(window_top);
        origins.resize(window_top);
        slot_names.resize(window_top);
        slot_types.resize(window_top);
    }
}

void CifaBytecode::RegisterSlots::restore(size_t base, size_t size, size_t top)
{
    window_base = base;
    for (size_t index = top; index < window_top; ++index)
        clear(index - window_base);
    window_size = size;
    window_top = top;
}

void CifaBytecode::RegisterSlots::export_argument(size_t slot, Object& destination)
{
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.object_exports;
#endif
    const size_t index = window_base + slot;
    destination.bound_type = typeid(void);
    destination.declared_type_name.clear();
    destination.element_type_name.clear();
    destination.type1.clear();
    destination.name.clear();
    if (slot_names[index] != 0) destination.name = name_pool[slot_names[index]];
    destination.argument_origin = origins[index];
    const auto& type = type_pool[slot_types[index]];
    destination.bound_type = type.bound;
    destination.declared_type_name = type.declared;
    destination.element_type_name = type.element;
    destination.type1 = type.special;
    if (bindings[index] != NumericBinding::None)
    {
        const bool integer = bindings[index] == NumericBinding::Int;
        destination.bound_type = integer ? typeid(std::int64_t) : typeid(double);
        destination.declared_type_name = integer ? "int" : "double";
    }
    destination.value = values[index].value.take_storage();
    if (const auto* resource = value_get_if<std::any>(&destination.value);
        resource && std::any_cast<Object::NoValue>(resource)) destination.type1 = "NoValue";
    clear(slot);
}

void CifaBytecode::RegisterSlots::clear(size_t slot)
{
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.clears;
#endif
    const size_t index = window_base + slot;
    release_payload(slot);
    slot_names[index] = 0;
    slot_types[index] = 0;
    bindings[index] = NumericBinding::None;
    origins[index] = nullptr;
}

void CifaBytecode::RegisterSlots::import_object(size_t slot, const Object& value)
{
    import_value(slot, value);
}

void CifaBytecode::RegisterSlots::set_name(size_t slot, const std::string& name)
{
    const size_t index = window_base + slot;
    if (name.empty()) slot_names[index] = 0;
    else
    {
        const auto [entry, inserted] = name_ids.try_emplace(name, name_pool.size());
        if (inserted) name_pool.push_back(name);
        slot_names[index] = entry->second;
    }
}

void CifaBytecode::RegisterSlots::set_type(size_t slot, const TypeDescriptor& type)
{
    const size_t index = window_base + slot;
    bindings[index] = NumericBinding::None;
    if (type.element.empty() && type.special.empty()
        && ((type.declared == "int" && type.bound == typeid(std::int64_t))
            || (type.declared == "double" && type.bound == typeid(double))))
    {
        bindings[index] = type.declared == "int" ? NumericBinding::Int : NumericBinding::Double;
        slot_types[index] = 0;
        return;
    }
    for (size_t entry = 0; entry < type_pool.size(); ++entry)
        if (type_pool[entry] == type) { slot_types[index] = entry; return; }
    slot_types[index] = type_pool.size();
    type_pool.push_back(type);
}

void CifaBytecode::RegisterSlots::import_object(size_t slot, Object&& value)
{
    import_value(slot, std::move(value));
}

template<class Value>
void CifaBytecode::RegisterSlots::import_value(size_t slot, Value&& value)
{
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.object_imports;
#endif
    const size_t index = window_base + slot;
    origins[index] = value.argument_origin;
    slot_names[index] = 0;
    slot_types[index] = 0;
    const auto numeric_binding = value.declared_type_name == "int" && value.bound_type == typeid(std::int64_t)
        ? NumericBinding::Int : value.declared_type_name == "double" && value.bound_type == typeid(double)
        ? NumericBinding::Double : NumericBinding::None;
    if (value.type1 == "NoValue" && value.bound_type == typeid(void)
        && value.declared_type_name.empty() && value.element_type_name.empty())
    {
        set_name(slot, value.name);
        bindings[index] = NumericBinding::None;
        store_payload(slot, std::forward<Value>(value).value);
        return;
    }
    if (numeric_binding != NumericBinding::None && value.element_type_name.empty() && value.type1.empty())
    {
        set_name(slot, value.name);
        bindings[index] = numeric_binding;
        store_payload(slot, std::forward<Value>(value).value);
        return;
    }
    bindings[index] = NumericBinding::None;
    set_name(slot, value.name);
    set_type(slot, {value.bound_type, value.declared_type_name, value.element_type_name, value.type1});
    store_payload(slot, std::forward<Value>(value).value);
}

CifaBytecode::BytecodeValue::Storage CifaBytecode::RegisterSlots::payload(size_t slot) const
{
    return values[window_base + slot].value;
}

CifaBytecode::BytecodeValue::Storage& CifaBytecode::RegisterSlots::resource_payload(size_t slot)
{
    return values[window_base + slot].value;
}

const CifaBytecode::BytecodeValue::Storage& CifaBytecode::RegisterSlots::payload(size_t slot, BytecodeValue::Storage& numeric) const
{
    (void)numeric;
    return values[window_base + slot].value;
}

void CifaBytecode::RegisterSlots::release_payload(size_t slot)
{
    values[window_base + slot].value.clear();
}

template<class Number>
void CifaBytecode::RegisterSlots::write_number(size_t slot, Number number, bool preserve_binding)
{
    const size_t index = window_base + slot;
    if (!preserve_binding)
    {
        slot_names[index] = 0;
        bindings[index] = NumericBinding::None;
        slot_types[index] = 0;
    }
    values[index].value = number;
}

void CifaBytecode::RegisterSlots::store_payload(size_t slot, BytecodeValue::Storage payload)
{
    values[window_base + slot].value = std::move(payload);
}

void CifaBytecode::RegisterSlots::store_payload(size_t slot, const Object::Storage& value)
{
    values[window_base + slot].value = CompactValue(value);
}

void CifaBytecode::RegisterSlots::store_payload(size_t slot, Object::Storage&& value)
{
    values[window_base + slot].value = CompactValue(std::move(value));
}

void CifaBytecode::RegisterSlots::write_payload(size_t slot, BytecodeValue::Storage value)
{
    if (const auto* integer = value.get_if<std::int64_t>()) { write_number(slot, *integer); return; }
    if (const auto* floating = value.get_if<double>()) { write_number(slot, *floating); return; }
    if (const auto* boolean = value.get_if<bool>()) { write_number(slot, *boolean); return; }
    const size_t index = window_base + slot;
    slot_names[index] = 0;
    bindings[index] = NumericBinding::None;
    slot_types[index] = 0;
    store_payload(slot, std::move(value));
}

void CifaBytecode::RegisterSlots::write_payload(size_t slot, std::any value)
{
    write_payload(slot, CompactValue(std::move(value)));
}

void CifaBytecode::RegisterSlots::write_payload(size_t slot, std::int64_t value)
{
    write_number(slot, value);
}

void CifaBytecode::RegisterSlots::write_payload(size_t slot, double value)
{
    write_number(slot, value);
}

void CifaBytecode::RegisterSlots::write_payload(size_t slot, bool value)
{
    write_number(slot, value);
}

void CifaBytecode::RegisterSlots::copy(size_t destination, const RegisterSlots& source, size_t slot)
{
    if (&values == &source.values && window_base + destination == source.window_base + slot) return;
#ifdef CIFA_VALUE_PROFILE
    ++value_profile.copies;
#endif
    const auto binding = source.bindings[source.window_base + slot];
    const auto origin = source.origins[source.window_base + slot];
    const size_t name = source.slot_names[source.window_base + slot];
    const size_t type = source.slot_types[source.window_base + slot];
    write_payload(destination, source.values[source.window_base + slot].value);
    const size_t index = window_base + destination;
    if (&name_pool == &source.name_pool) slot_names[index] = name;
    else if (name != 0) set_name(destination, source.name_pool[name]);
    if (&type_pool == &source.type_pool) slot_types[index] = type;
    else if (type != 0) set_type(destination, source.type_pool[type]);
    bindings[index] = binding;
    origins[index] = origin;
}

void CifaBytecode::RegisterSlots::move(size_t destination, RegisterSlots& source, size_t slot)
{
    if (&values == &source.values && window_base + destination == source.window_base + slot) return;
    const size_t source_index = source.window_base + slot;
    const size_t destination_index = window_base + destination;
    if (&values == &source.values)
    {
        clear(destination);
        values[destination_index].value = std::move(values[source_index].value);
        bindings[destination_index] = bindings[source_index];
        origins[destination_index] = source.origins[source_index];
        slot_names[destination_index] = slot_names[source_index];
        slot_types[destination_index] = slot_types[source_index];
        values[source_index].value.clear();
        bindings[source_index] = NumericBinding::None;
        source.origins[source_index] = nullptr;
        slot_names[source_index] = 0;
        slot_types[source_index] = 0;
        return;
    }
    const auto binding = source.bindings[source_index];
    const auto origin = source.origins[source_index];
    const size_t name = source.slot_names[source_index];
    const size_t type = source.slot_types[source_index];
    auto value = std::move(source.values[source_index].value);
    write_payload(destination, std::move(value));
    if (&name_pool == &source.name_pool) slot_names[destination_index] = name;
    else if (name != 0) set_name(destination, source.name_pool[name]);
    if (&type_pool == &source.type_pool) slot_types[destination_index] = type;
    else if (type != 0) set_type(destination, source.type_pool[type]);
    bindings[destination_index] = binding;
    origins[destination_index] = origin;
    source.clear(slot);
}

bool CifaBytecode::RegisterSlots::number(size_t slot, std::int64_t& integer, double& floating, bool& is_double) const
{
    const size_t index = window_base + slot;
    is_double = false;
    const auto& value = values[index].value;
    if (const auto* stored_integer = value.get_if<std::int64_t>()) integer = *stored_integer;
    else if (const auto* stored_double = value.get_if<double>()) { floating = *stored_double; is_double = true; }
    else if (const auto* stored_boolean = value.get_if<bool>()) integer = *stored_boolean;
    else return false;
    return true;
}

bool CifaBytecode::RegisterSlots::integer(size_t slot, std::int64_t& result) const
{
    double floating = 0;
    bool is_double = false;
    if (!number(slot, result, floating, is_double)) return false;
    if (is_double)
    {
        if (!std::isfinite(floating) || floating >= 9223372036854775808.0
            || floating < -9223372036854775808.0) return false;
        result = static_cast<std::int64_t>(floating);
    }
    return true;
}

bool CifaBytecode::RegisterSlots::cast_numeric(size_t destination, const RegisterSlots& source, size_t slot, const std::string& type)
{
    std::int64_t integer = 0;
    double floating = 0;
    bool is_double = false;
    if (!source.number(slot, integer, floating, is_double)) return false;
    if (type == "bool")
    {
        write_number(destination, is_double ? floating != 0 : integer != 0);
    }
    else if (type == "double" || type == "float")
    {
        write_number(destination, is_double ? floating : static_cast<double>(integer));
    }
    else if (type == "int" || type == "char")
    {
        if (is_double)
        {
            if (!std::isfinite(floating) || floating >= 9223372036854775808.0
                || floating < -9223372036854775808.0) return false;
            integer = static_cast<std::int64_t>(floating);
        }
        write_number(destination, integer);
    }
    else return false;
    return true;
}

bool CifaBytecode::RegisterSlots::cast_numeric(size_t destination, const RegisterSlots& source, size_t slot, NumericBinding binding)
{
    std::int64_t integer = 0;
    double floating = 0;
    bool is_double = false;
    if (!source.number(slot, integer, floating, is_double)) return false;
    if (binding == NumericBinding::Double)
        write_number(destination, is_double ? floating : static_cast<double>(integer));
    else if (binding == NumericBinding::Int)
    {
        if (is_double)
        {
            if (!std::isfinite(floating) || floating >= 9223372036854775808.0
                || floating < -9223372036854775808.0) return false;
            integer = static_cast<std::int64_t>(floating);
        }
        write_number(destination, integer);
    }
    else return false;
    return true;
}

bool CifaBytecode::RegisterSlots::assign_numeric(size_t destination, const RegisterSlots& source, size_t slot)
{
    const size_t index = window_base + destination;
    std::int64_t integer = 0;
    double floating = 0;
    bool is_double = false;
    if (!source.number(slot, integer, floating, is_double)) return false;
    if (bindings[index] == NumericBinding::Int)
    {
        if (is_double)
        {
            if (!std::isfinite(floating) || floating >= 9223372036854775808.0
                || floating < -9223372036854775808.0) return false;
            integer = static_cast<std::int64_t>(floating);
        }
        write_number(destination, integer, true);
    }
    else if (bindings[index] == NumericBinding::Double)
    {
        write_number(destination, is_double ? floating : static_cast<double>(integer), true);
    }
    else return false;
    return true;
}

void CifaBytecode::RegisterSlots::bind_numeric(size_t slot, NumericBinding binding)
{
    bindings[window_base + slot] = binding;
}

bool CifaBytecode::RegisterSlots::binary(Opcode opcode, size_t destination, size_t left, size_t right,
    Machine& machine, const SourceLocation& location)
{
    std::int64_t left_integer = 0, right_integer = 0;
    double left_number = 0, right_number = 0;
    bool left_double = false, right_double = false;
    if (!number(left, left_integer, left_number, left_double)
        || !number(right, right_integer, right_number, right_double)) return false;
    return binary_numbers(opcode, destination, left_integer, left_number, left_double,
        right_integer, right_number, right_double, machine, location);
}

void CifaBytecode::RegisterSlots::binary_fallback(Opcode opcode, size_t destination, size_t left, size_t right,
    Machine& machine, const SourceLocation& location, bool optimized)
{
    BytecodeValue::Storage left_numeric, right_numeric;
    if (binary_payloads(opcode, destination, payload(left, left_numeric), payload(right, right_numeric), machine, location))
    {
        if (left != destination) clear(left);
        if (right != left && right != destination) clear(right);
        return;
    }
    Cifa::OperatorCallbacks* callbacks = nullptr;
    switch (opcode)
    {
    case Opcode::Add: callbacks = &machine.host.user_add; break;
    case Opcode::Subtract: callbacks = &machine.host.user_sub; break;
    case Opcode::Multiply: callbacks = &machine.host.user_mul; break;
    case Opcode::Divide: callbacks = &machine.host.user_div; break;
    case Opcode::Modulo: callbacks = &machine.host.user_mod; break;
    case Opcode::Less: callbacks = &machine.host.user_less; break;
    case Opcode::Greater: callbacks = &machine.host.user_more; break;
    case Opcode::LessEqual: callbacks = &machine.host.user_less_equal; break;
    case Opcode::GreaterEqual: callbacks = &machine.host.user_more_equal; break;
    case Opcode::Equal: callbacks = &machine.host.user_equal; break;
    case Opcode::NotEqual: callbacks = &machine.host.user_not_equal; break;
    case Opcode::BitAnd: callbacks = &machine.host.user_bit_and; break;
    case Opcode::BitOr: callbacks = &machine.host.user_bit_or; break;
    case Opcode::BitXor: callbacks = &machine.host.user_bit_xor; break;
    case Opcode::ShiftLeft: callbacks = &machine.host.user_shift_left; break;
    case Opcode::ShiftRight: callbacks = &machine.host.user_shift_right; break;
    case Opcode::LogicalAnd: callbacks = &machine.host.user_logic_and; break;
    case Opcode::LogicalOr: callbacks = &machine.host.user_logic_or; break;
    default: machine.set_error("invalid bytecode binary operation", &location); break;
    }
    if (!callbacks || callbacks->empty())
    {
        clear(left);
        if (right != left) clear(right);
        clear(destination);
        return;
    }
    Object left_argument, right_argument;
    export_argument(left, left_argument);
    if (right != left) export_argument(right, right_argument);
    for (auto& callback : *callbacks)
    {
        auto result = callback(left_argument, right == left ? left_argument : right_argument);
        if (machine.host.has_runtime_error() || machine.should_stop()) break;
        if (result.hasValue())
        {
            import_object(destination, std::move(result));
            return;
        }
    }
    clear(destination);
}

bool CifaBytecode::RegisterSlots::binary_payloads(Opcode opcode, size_t destination,
    const BytecodeValue::Storage& left_payload, const BytecodeValue::Storage& right_payload,
    Machine& machine, const SourceLocation& location)
{
    const auto numeric = [](const BytecodeValue::Storage& value) { return value.index() >= 1 && value.index() <= 3; };
    const auto* left_resource = value_get_if<std::any>(&left_payload);
    const auto* right_resource = value_get_if<std::any>(&right_payload);
    const auto* left_no_value = left_resource ? std::any_cast<Object::NoValue>(left_resource) : nullptr;
    const auto* right_no_value = right_resource ? std::any_cast<Object::NoValue>(right_resource) : nullptr;
    if (left_no_value || right_no_value)
    {
        machine.set_no_value_error(left_no_value ? left_no_value : right_no_value, &location);
        clear(destination);
        return true;
    }
    const auto* left_text = left_resource ? std::any_cast<std::string>(left_resource) : nullptr;
    const auto* right_text = right_resource ? std::any_cast<std::string>(right_resource) : nullptr;
    if (left_text && right_text)
    {
        switch (opcode)
        {
        case Opcode::Add: write_payload(destination, std::any(*left_text + *right_text)); return true;
        case Opcode::Less: write_number(destination, *left_text < *right_text); return true;
        case Opcode::Greater: write_number(destination, *left_text > *right_text); return true;
        case Opcode::LessEqual: write_number(destination, *left_text <= *right_text); return true;
        case Opcode::GreaterEqual: write_number(destination, *left_text >= *right_text); return true;
        case Opcode::Equal: write_number(destination, *left_text == *right_text); return true;
        case Opcode::NotEqual: write_number(destination, *left_text != *right_text); return true;
        default: return false;
        }
    }
    if (!numeric(left_payload) || !numeric(right_payload)) return false;
    const auto integer = [](const BytecodeValue::Storage& value) {
        return value_holds<bool>(value) ? std::int64_t(value_get<bool>(value)) : value_get<std::int64_t>(value);
    };
    const bool left_double = value_holds<double>(left_payload);
    const bool right_double = value_holds<double>(right_payload);
    return binary_numbers(opcode, destination, left_double ? 0 : integer(left_payload), left_double ? value_get<double>(left_payload) : 0, left_double,
        right_double ? 0 : integer(right_payload), right_double ? value_get<double>(right_payload) : 0, right_double, machine, location);
}

bool CifaBytecode::RegisterSlots::binary_numbers(Opcode opcode, size_t destination,
    std::int64_t left_integer, double left_number, bool left_double,
    std::int64_t right_integer, double right_number, bool right_double, Machine& machine, const SourceLocation& location)
{
    bool result_is_double = false;
    bool result_is_boolean = false;
    std::int64_t integer_result = 0;
    double double_result = 0;
    bool boolean_result = false;
    if (left_double || right_double)
    {
        const double left_value = left_double ? left_number : static_cast<double>(left_integer);
        const double right_value = right_double ? right_number : static_cast<double>(right_integer);
        result_is_double = true;
        switch (opcode)
        {
        case Opcode::Add: double_result = left_value + right_value; break;
        case Opcode::Subtract: double_result = left_value - right_value; break;
        case Opcode::Multiply: double_result = left_value * right_value; break;
        case Opcode::Divide: double_result = left_value / right_value; break;
        case Opcode::Less: boolean_result = left_value < right_value; result_is_boolean = true; break;
        case Opcode::Greater: boolean_result = left_value > right_value; result_is_boolean = true; break;
        case Opcode::LessEqual: boolean_result = left_value <= right_value; result_is_boolean = true; break;
        case Opcode::GreaterEqual: boolean_result = left_value >= right_value; result_is_boolean = true; break;
        case Opcode::Equal: boolean_result = left_value == right_value; result_is_boolean = true; break;
        case Opcode::NotEqual: boolean_result = left_value != right_value; result_is_boolean = true; break;
        case Opcode::LogicalAnd: boolean_result = left_value != 0 && right_value != 0; result_is_boolean = true; break;
        case Opcode::LogicalOr: boolean_result = left_value != 0 || right_value != 0; result_is_boolean = true; break;
        case Opcode::Modulo: case Opcode::BitAnd: case Opcode::BitOr: case Opcode::BitXor:
        case Opcode::ShiftLeft: case Opcode::ShiftRight:
        {
            const char* symbol = opcode == Opcode::Modulo ? "%" : opcode == Opcode::BitAnd ? "&"
                : opcode == Opcode::BitOr ? "|" : opcode == Opcode::BitXor ? "^"
                : opcode == Opcode::ShiftLeft ? "<<" : ">>";
            machine.set_error(std::string("operator ") + symbol + " requires integer operands", &location);
            return true;
        }
        default: return false;
        }
    }
    else
    {
        const auto left_value = left_integer;
        const auto right_value = right_integer;
        const auto unsigned_left = static_cast<std::uint64_t>(left_value);
        const auto unsigned_right = static_cast<std::uint64_t>(right_value);
        if ((opcode == Opcode::Divide || opcode == Opcode::Modulo) && right_value == 0)
        {
            machine.set_error(opcode == Opcode::Divide ? "integer division by zero" : "integer modulo by zero", &location);
            return true;
        }
        if ((opcode == Opcode::ShiftLeft || opcode == Opcode::ShiftRight) && (right_value < 0 || right_value >= 64))
        {
            machine.set_error(opcode == Opcode::ShiftLeft ? "left shift count is out of range" : "right shift count is out of range", &location);
            return true;
        }
        const bool overflow = left_value == std::numeric_limits<std::int64_t>::min() && right_value == -1;
        switch (opcode)
        {
        case Opcode::Add: integer_result = std::bit_cast<std::int64_t>(unsigned_left + unsigned_right); break;
        case Opcode::Subtract: integer_result = std::bit_cast<std::int64_t>(unsigned_left - unsigned_right); break;
        case Opcode::Multiply: integer_result = std::bit_cast<std::int64_t>(unsigned_left * unsigned_right); break;
        case Opcode::Divide:
            if (overflow) { machine.set_error("integer division overflow", &location); return true; }
            integer_result = left_value / right_value; break;
        case Opcode::Modulo: integer_result = overflow ? 0 : left_value % right_value; break;
        case Opcode::Less: boolean_result = left_value < right_value; result_is_boolean = true; break;
        case Opcode::Greater: boolean_result = left_value > right_value; result_is_boolean = true; break;
        case Opcode::LessEqual: boolean_result = left_value <= right_value; result_is_boolean = true; break;
        case Opcode::GreaterEqual: boolean_result = left_value >= right_value; result_is_boolean = true; break;
        case Opcode::Equal: boolean_result = left_value == right_value; result_is_boolean = true; break;
        case Opcode::NotEqual: boolean_result = left_value != right_value; result_is_boolean = true; break;
        case Opcode::BitAnd: integer_result = left_value & right_value; break;
        case Opcode::BitOr: integer_result = left_value | right_value; break;
        case Opcode::BitXor: integer_result = left_value ^ right_value; break;
        case Opcode::ShiftLeft: integer_result = std::bit_cast<std::int64_t>(unsigned_left << right_value); break;
        case Opcode::ShiftRight: integer_result = left_value >> right_value; break;
        case Opcode::LogicalAnd: boolean_result = left_value != 0 && right_value != 0; result_is_boolean = true; break;
        case Opcode::LogicalOr: boolean_result = left_value != 0 || right_value != 0; result_is_boolean = true; break;
        default: return false;
        }
    }
    if (result_is_boolean) write_number(destination, boolean_result);
    else if (result_is_double) write_number(destination, double_result);
    else write_number(destination, integer_result);
    return true;
}

std::string CifaBytecode::Machine::format_frame(const SourceLocation& location)
{
    const std::string filename = location.filename.empty() ? "<script>" : location.filename;
    const std::string text = location.text.empty() ? location.str : location.text;
    const std::string header = filename + ":" + std::to_string(location.line) + ", col " + std::to_string(location.col) + ": ";
    std::string caret(header.size(), ' ');
    const size_t column = location.col == 0 ? 0 : location.col - 1;
    const size_t prefix = (std::min)(column, text.size());
    for (size_t index = 0; index < prefix; ++index) caret += text[index] == '\t' ? '\t' : ' ';
    if (column > prefix) caret.append(column - prefix, ' ');
    return header + text + "\n" + caret + "^";
}

CifaBytecode::SourceLocation::SourceLocation(const CalUnit& node)
    : str(node.str), line(node.line), col(node.col)
{
}

CifaBytecode::WriteOperation CifaBytecode::write_operation(const std::string& symbol)
{
    if (symbol == "=") return WriteOperation::Assign;
    if (symbol == "+=" || symbol == "++") return WriteOperation::Add;
    if (symbol == "-=" || symbol == "--") return WriteOperation::Subtract;
    if (symbol == "*=") return WriteOperation::Multiply;
    if (symbol == "/=") return WriteOperation::Divide;
    if (symbol == "%=") return WriteOperation::Modulo;
    if (symbol == "&=") return WriteOperation::BitAnd;
    if (symbol == "|=") return WriteOperation::BitOr;
    if (symbol == "^=") return WriteOperation::BitXor;
    if (symbol == "<<=") return WriteOperation::ShiftLeft;
    if (symbol == ">>=") return WriteOperation::ShiftRight;
    if (symbol == "()++") return WriteOperation::PostAdd;
    if (symbol == "()--") return WriteOperation::PostSubtract;
    return WriteOperation::Invalid;
}

std::optional<CifaBytecode::Opcode> CifaBytecode::write_opcode(WriteOperation operation)
{
    switch (operation)
    {
    case WriteOperation::Add: case WriteOperation::PostAdd: return Opcode::Add;
    case WriteOperation::Subtract: case WriteOperation::PostSubtract: return Opcode::Subtract;
    case WriteOperation::Multiply: return Opcode::Multiply;
    case WriteOperation::Divide: return Opcode::Divide;
    case WriteOperation::Modulo: return Opcode::Modulo;
    case WriteOperation::BitAnd: return Opcode::BitAnd;
    case WriteOperation::BitOr: return Opcode::BitOr;
    case WriteOperation::BitXor: return Opcode::BitXor;
    case WriteOperation::ShiftLeft: return Opcode::ShiftLeft;
    case WriteOperation::ShiftRight: return Opcode::ShiftRight;
    default: return std::nullopt;
    }
}

size_t CifaBytecode::intern_name(const std::string& name)
{
    const auto [entry, inserted] = name_ids.emplace(name, names.size());
    if (inserted) names.push_back(name);
    return entry->second;
}

size_t CifaBytecode::source_id(const CalUnit& source)
{
    if (const auto found = source_ids.find(&source); found != source_ids.end()) return found->second;
    const size_t id = sources.size() + 1;
    sources.emplace_back(source);
    auto& location = sources.back();
    for (const auto& line : source_lines)
        if (line.line == source.line)
        {
            location.filename = line.filename;
            location.text = line.text;
            break;
        }
    source_ids.emplace(&source, id);
    compile_sources.emplace(id, &source);
    return id;
}

CifaBytecode::SourceRef CifaBytecode::source_ref(const CalUnit* node)
{
    return node == nullptr ? SourceRef{} : SourceRef(source_id(*node));
}

size_t CifaBytecode::index_site(const CalUnit& node)
{
    const size_t site = index_sites.size();
    const auto local = local_slot(node, false);
    index_sites.push_back({intern_name(node.str), intern_name(node.type_name), node.v.size(), local ? *local + 1 : 0,
        node.with_type && node.suffix, !node.v[0].v.empty() && node.v[0].v[0].type == CalUnitType::String,
        node.with_type});
    return site;
}

void CifaBytecode::seal(Instructions& instructions)
{
    size_t scope_depth = 0;
    for (auto& instruction : instructions.code)
    {
        if (instruction.opcode == Opcode::ScopeEnter)
            instructions.scope_capacity = (std::max)(instructions.scope_capacity, ++scope_depth);
        else if (instruction.opcode == Opcode::ScopeLeave && scope_depth != 0) --scope_depth;
        const auto found = compile_sources.find(instruction.source.id);
        const auto* pending_source = found == compile_sources.end() ? nullptr : found->second;
        if (pending_source != nullptr && instruction.opcode == Opcode::Branch && instruction.condition_source.id == 0)
        {
            const auto& node = *pending_source;
            if (node.type == CalUnitType::Key)
            {
                if ((node.str == "if" || node.str == "while") && !node.v.empty()) instruction.condition_source = source_ref(&node.v[0]);
                else if (node.str == "for" && !node.v.empty() && node.v[0].v.size() > 1) instruction.condition_source = source_ref(&node.v[0].v[1]);
                else if (node.str == "do" && node.v.size() > 1 && !node.v[1].v.empty()) instruction.condition_source = source_ref(&node.v[1].v[0]);
            }
        }
        if (instruction.target_source.id == 0 && pending_source != nullptr)
        {
            const auto& node = *pending_source;
            if (instruction.opcode == Opcode::RangeBegin && node.v.size() > 1)
                instruction.target_source = source_ref(&node.v[1]);
            else if (!node.v.empty() && (instruction.opcode == Opcode::RangeNext || instruction.opcode == Opcode::PrepareStore
                || instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment))
                instruction.target_source = source_ref(&node.v[0]);
        }
    }
}

void CifaBytecode::seal_calls()
{
    for (auto& call : calls)
    {
        const auto found = compile_sources.find(call.source.id);
        if (found == compile_sources.end()) continue;
        const auto& node = *found->second;
        const auto& name = node.type == CalUnitType::Function ? node.str : node.v.empty() ? node.str : node.v.back().str;
        call.name_id = intern_name(name);
        if (node.type == CalUnitType::Operator && node.str == "." && node.v.size() == 2)
        {
            call.base_name_id = intern_name(node.v[0].str);
            if (call.method_source.id == 0)
            {
                call.method_source = source_ref(&node.v[1]);
            }
        }
    }
}

std::vector<size_t> CifaBytecode::compact(Instructions& instructions)
{
    const auto compact_only = [](Opcode opcode)
    {
        return opcode == Opcode::Enter || opcode == Opcode::Leave;
    };
    const size_t original_size = instructions.code.size();
    std::vector<size_t> remap(original_size + 1);
    size_t compact_size = 0;
    for (size_t index = 0; index < original_size; ++index)
        if (!compact_only(instructions.code[index].opcode))
            ++compact_size;
    remap[original_size] = compact_size;
    for (size_t index = original_size; index > 0; --index)
    {
        const size_t current = index - 1;
        const auto opcode = instructions.code[current].opcode;
        remap[current] = compact_only(opcode) ? remap[current + 1] : --compact_size;
    }
    std::vector<Instruction> code;
    std::vector<std::vector<std::pair<size_t, bool>>> diagnostic_frames;
    code.reserve(remap.back());
    diagnostic_frames.reserve(remap.back());
    for (size_t index = 0; index < original_size; ++index)
    {
        auto instruction = instructions.code[index];
        if (compact_only(instruction.opcode)) continue;
        if (instruction.opcode == Opcode::Jump || instruction.opcode == Opcode::Branch
            || instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch)
        {
            instruction.operand = remap[instruction.operand];
        }
        else if (instruction.opcode == Opcode::Unwind || instruction.opcode == Opcode::RangeNext
            || instruction.opcode == Opcode::RangeEnd || instruction.opcode == Opcode::SwitchMark
            || instruction.opcode == Opcode::SwitchCase || instruction.opcode == Opcode::SwitchDefault
            || instruction.opcode == Opcode::SwitchEnd)
        {
            instruction.operand = remap[instruction.operand];
        }
        else if (instruction.opcode == Opcode::RangeBegin)
        {
            instruction.operand = remap[index];
        }
        code.push_back(std::move(instruction));
        diagnostic_frames.push_back(std::move(instructions.diagnostic_frames[index]));
    }
    instructions.code = std::move(code);
    instructions.diagnostic_frames = std::move(diagnostic_frames);
    std::vector<size_t> loop_ids(instructions.code.size(), std::numeric_limits<size_t>::max());
    for (size_t pc = 0; pc < instructions.code.size(); ++pc)
        if (instructions.code[pc].opcode == Opcode::LoopMark)
        {
            loop_ids[pc] = instructions.loop_state_count++;
            instructions.code[pc].auxiliary = loop_ids[pc];
        }
    for (auto& instruction : instructions.code)
        if (instruction.opcode == Opcode::Unwind)
            instruction.operand = loop_ids[instruction.operand];
    return remap;
}

CifaBytecode::SourceLocation& CifaBytecode::source(const SourceRef& reference) const
{
    return const_cast<SourceLocation&>(sources.at(reference.id - 1));
}

std::optional<size_t> CifaBytecode::local_slot(const CalUnit& node, bool declare, bool allow_untyped_declaration)
{
    if (compiling_function == nullptr || node.type != CalUnitType::Parameter || (declare && !node.v.empty())) return {};
    if (declare)
    {
        if (!node.with_type && !allow_untyped_declaration) return local_slot(node, false);
        if (const auto found = compile_local_scopes.back().find(node.str); found != compile_local_scopes.back().end())
            return found->second;
        if ((!node.with_type || node.type_name.empty()) && !allow_untyped_declaration) return {};
        const size_t slot = next_local_slot();
        if (slot >= compiling_function->local_slot_count) compiling_function->local_slot_count = slot + 1;
        compile_local_scopes.back().emplace(node.str, slot);
        return slot;
    }
    for (auto scope = compile_local_scopes.rbegin(); scope != compile_local_scopes.rend(); ++scope)
        if (const auto found = scope->find(node.str); found != scope->end()) return found->second;
    return {};
}

bool CifaBytecode::operation(const CalUnit& node, Opcode& opcode)
{
    if (node.type == CalUnitType::Cast && node.v.size() == 1)
    {
        opcode = Opcode::Cast;
        return true;
    }
    if (node.type == CalUnitType::Operator && node.v.size() == 1)
    {
        if (node.str == "+") opcode = Opcode::Positive;
        else if (node.str == "-") opcode = Opcode::Negative;
        else if (node.str == "!") opcode = Opcode::LogicalNot;
        else if (node.str == "~") opcode = Opcode::BitNot;
        else return false;
        return true;
    }
    if (node.type != CalUnitType::Operator || node.v.size() != 2) return false;
    if (node.str == "?") { opcode = Opcode::Branch; return true; }
    if (node.str == ",") return false;
    if (node.str == "&&") { opcode = Opcode::LogicalAnd; return true; }
    if (node.str == "||") { opcode = Opcode::LogicalOr; return true; }
    if (node.str == "+") opcode = Opcode::Add;
    else if (node.str == "-") opcode = Opcode::Subtract;
    else if (node.str == "*") opcode = Opcode::Multiply;
    else if (node.str == "/") opcode = Opcode::Divide;
    else if (node.str == "%") opcode = Opcode::Modulo;
    else if (node.str == "<") opcode = Opcode::Less;
    else if (node.str == ">") opcode = Opcode::Greater;
    else if (node.str == "<=") opcode = Opcode::LessEqual;
    else if (node.str == ">=") opcode = Opcode::GreaterEqual;
    else if (node.str == "==") opcode = Opcode::Equal;
    else if (node.str == "!=") opcode = Opcode::NotEqual;
    else if (node.str == "&") opcode = Opcode::BitAnd;
    else if (node.str == "|") opcode = Opcode::BitOr;
    else if (node.str == "^") opcode = Opcode::BitXor;
    else if (node.str == "<<") opcode = Opcode::ShiftLeft;
    else if (node.str == ">>") opcode = Opcode::ShiftRight;
    else return false;
    return true;
}

bool CifaBytecode::try_fold_constant(const CalUnit& node, Object& value,
    const std::unordered_map<std::string, Object>* parameters, std::unordered_set<std::string>* active_functions) const
{
    if (node.type == CalUnitType::Constant) return Cifa::parse_number_literal(node.str, value);
    if (node.type == CalUnitType::Parameter && node.v.empty() && parameters != nullptr)
    {
        const auto parameter = parameters->find(node.str);
        if (parameter != parameters->end())
        {
            value = parameter->second;
            return true;
        }
    }
    if (node.type == CalUnitType::Key && (node.str == "true" || node.str == "false"))
    {
        value = Object(node.str == "true");
        return true;
    }
    if (node.type == CalUnitType::Union && node.str == "()" && node.v.size() == 1)
        return try_fold_constant(node.v[0], value, parameters, active_functions);
    if (node.type == CalUnitType::Cast && node.v.size() == 1)
    {
        Object source;
        if (!try_fold_constant(node.v[0], source, parameters, active_functions)) return false;
        if (node.type_name == "double" || node.type_name == "float")
        {
            if (!source.isNumber()) return false;
            value = Object(source.toDouble());
            return true;
        }
        if (node.type_name == "int" || node.type_name == "char")
        {
            if (!source.isNumber()) return false;
            const double number = source.toDouble();
            if (!std::isfinite(number) || number >= 9223372036854775808.0 || number < -9223372036854775808.0)
                return false;
            value = Object(source.toInt64());
            return true;
        }
        if (node.type_name == "bool")
        {
            if (!source.isNumber()) return false;
            value = Object(source.toBool());
            return true;
        }
        return false;
    }
    if (node.type == CalUnitType::Operator && node.v.size() == 1)
    {
        Object operand;
        if (!try_fold_constant(node.v[0], operand, parameters, active_functions) || !operand.isNumber()) return false;
        if (node.str == "+")
        {
            value = operand.isType<bool>() ? Object(operand.toInt64()) : operand;
            return true;
        }
        if (node.str == "-")
        {
            if (operand.isInteger())
            {
                value = Object(std::bit_cast<std::int64_t>(0ULL - static_cast<std::uint64_t>(operand.toInt64())));
            }
            else value = Object(-operand.toDouble());
            return true;
        }
        if (node.str == "!")
        {
            value = Object(!operand.toBool());
            return true;
        }
        if (node.str == "~" && operand.isInteger())
        {
            value = Object(~operand.toInt64());
            return true;
        }
        return false;
    }
    if (node.type == CalUnitType::Function)
    {
        ObjectVector arguments;
        std::function<bool(const CalUnit&)> append_arguments = [&](const CalUnit& argument)
        {
            if (argument.type == CalUnitType::Union && argument.str == "()" && argument.v.size() > 1)
            {
                for (const auto& child : argument.v)
                    if (!append_arguments(child)) return false;
                return true;
            }
            if (argument.str == ",")
            {
                for (const auto& child : argument.v)
                    if (!append_arguments(child)) return false;
                return true;
            }
            if (argument.type == CalUnitType::None) return true;
            Object folded;
            if (!try_fold_constant(argument, folded, parameters, active_functions)) return false;
            arguments.push_back(std::move(folded));
            return true;
        };
        for (const auto& argument : node.v)
            if (!append_arguments(argument)) return false;

        if (compile_allows_script_constant_folding && compile_script_functions != nullptr)
        {
            const auto overloads = compile_script_functions->find(node.str);
            if (overloads != compile_script_functions->end())
            {
                const auto definition = overloads->second.find(arguments.size());
                if (definition != overloads->second.end()
                    && (definition->second.return_type == "int" || definition->second.return_type == "double" || definition->second.return_type == "bool")
                    && definition->second.body.type == CalUnitType::Union && definition->second.body.str == "{}"
                    && definition->second.body.v.size() == 1 && definition->second.body.v[0].type == CalUnitType::Key
                    && definition->second.body.v[0].str == "return" && definition->second.body.v[0].v.size() == 1)
                {
                    std::unordered_map<std::string, Object> function_parameters;
                    for (size_t index = 0; index < arguments.size(); ++index)
                    {
                        if (!definition->second.arguments[index].type_name.empty()) return false;
                        function_parameters.emplace(definition->second.arguments[index].name, arguments[index]);
                    }
                    std::unordered_set<std::string> local_active;
                    auto& active = active_functions == nullptr ? local_active : *active_functions;
                    const std::string key = node.str + "/" + std::to_string(arguments.size());
                    if (!active.insert(key).second) return false;
                    const bool folded = try_fold_constant(definition->second.body.v[0].v[0], value, &function_parameters, &active);
                    active.erase(key);
                    if (folded && value.isNumber())
                    {
                        if (definition->second.return_type == "int") value = Object(value.toInt64());
                        else if (definition->second.return_type == "double") value = Object(value.toDouble());
                        else value = Object(value.toBool());
                        return true;
                    }
                }
            }
        }

        const auto builtin = builtin_function_generations.find(node.str);
        const auto generation = function_generations.find(node.str);
        if (builtin == builtin_function_generations.end() || generation == function_generations.end()
            || generation->second != builtin->second) return false;

        const auto all_numeric = [&]()
        {
            return std::all_of(arguments.begin(), arguments.end(), [](const Object& argument) { return argument.isNumber(); });
        };
        if (node.str == "abs" && arguments.size() == 1 && arguments[0].isNumber())
        {
            if (arguments[0].isInteger())
            {
                const auto integer = arguments[0].toInt64();
                if (integer == std::numeric_limits<std::int64_t>::min()) return false;
                value = Object(integer < 0 ? -integer : integer);
            }
            else value = Object(std::fabs(arguments[0].toDouble()));
            return true;
        }
        if ((node.str == "max" || node.str == "min") && !arguments.empty() && all_numeric())
        {
            size_t best = 0;
            for (size_t index = 1; index < arguments.size(); ++index)
            {
                const bool less = arguments[best].isInteger() && arguments[index].isInteger()
                    ? arguments[best].toInt64() < arguments[index].toInt64()
                    : arguments[best].toDouble() < arguments[index].toDouble();
                if ((node.str == "max" && less) || (node.str == "min" && !less && arguments[best].toDouble() != arguments[index].toDouble()))
                    best = index;
            }
            const bool floating = std::any_of(arguments.begin(), arguments.end(), [](const Object& argument) { return argument.isType<double>(); });
            value = floating ? Object(arguments[best].toDouble()) : Object(arguments[best].toInt64());
            return true;
        }
        if ((node.str == "ifv" || node.str == "ifvalue") && arguments.size() == 3 && arguments[0].isNumber())
        {
            value = std::move(arguments[arguments[0].toBool() ? 1 : 2]);
            return true;
        }
        if (arguments.size() == 1 && arguments[0].isNumber())
        {
            const double input = arguments[0].toDouble();
            static const std::unordered_map<std::string, double (*)(double)> math1 = {
                { "sqrt", std::sqrt }, { "cbrt", std::cbrt }, { "round", std::round }, { "trunc", std::trunc },
                { "nearbyint", std::nearbyint }, { "rint", std::rint }, { "ceil", std::ceil }, { "floor", std::floor },
                { "sin", std::sin }, { "cos", std::cos }, { "tan", std::tan }, { "asin", std::asin }, { "acos", std::acos },
                { "atan", std::atan }, { "sinh", std::sinh }, { "cosh", std::cosh }, { "tanh", std::tanh }, { "exp", std::exp },
                { "log", std::log }, { "log2", std::log2 }, { "log10", std::log10 }, { "erf", std::erf }, { "erfc", std::erfc },
                { "tgamma", std::tgamma }, { "lgamma", std::lgamma }
            };
            if (const auto found = math1.find(node.str); found != math1.end())
            {
                value = Object(found->second(input));
                return true;
            }
        }
        if (arguments.size() == 2 && all_numeric())
        {
            const double left = arguments[0].toDouble();
            const double right = arguments[1].toDouble();
            static const std::unordered_map<std::string, double (*)(double, double)> math2 = {
                { "atan2", std::atan2 }, { "pow", std::pow }, { "hypot", std::hypot }, { "fmod", std::fmod },
                { "remainder", std::remainder }, { "copysign", std::copysign }, { "fdim", std::fdim }, { "fmax", std::fmax }, { "fmin", std::fmin }
            };
            if (const auto found = math2.find(node.str); found != math2.end())
            {
                value = Object(found->second(left, right));
                return true;
            }
        }
        return false;
    }
    if (node.type != CalUnitType::Operator || node.v.size() != 2) return false;

    Object left;
    Object right;
    if (!try_fold_constant(node.v[0], left, parameters, active_functions) || !try_fold_constant(node.v[1], right, parameters, active_functions)
        || !left.isNumber() || !right.isNumber()) return false;

    const bool integers = left.isInteger() && right.isInteger();
    const auto left_integer = left.toInt64();
    const auto right_integer = right.toInt64();
    const auto wrap = [](std::uint64_t number) { return Object(std::bit_cast<std::int64_t>(number)); };
    if (node.str == "==") { value = Object(integers ? left_integer == right_integer : left.toDouble() == right.toDouble()); return true; }
    if (node.str == "!=") { value = Object(integers ? left_integer != right_integer : left.toDouble() != right.toDouble()); return true; }
    if (node.str == "<") { value = Object(integers ? left_integer < right_integer : left.toDouble() < right.toDouble()); return true; }
    if (node.str == ">") { value = Object(integers ? left_integer > right_integer : left.toDouble() > right.toDouble()); return true; }
    if (node.str == "<=") { value = Object(integers ? left_integer <= right_integer : left.toDouble() <= right.toDouble()); return true; }
    if (node.str == ">=") { value = Object(integers ? left_integer >= right_integer : left.toDouble() >= right.toDouble()); return true; }
    if (node.str == "&&") { value = Object(left.toBool() && right.toBool()); return true; }
    if (node.str == "||") { value = Object(left.toBool() || right.toBool()); return true; }
    if (!integers)
    {
        if (node.str == "+") { value = Object(left.toDouble() + right.toDouble()); return true; }
        if (node.str == "-") { value = Object(left.toDouble() - right.toDouble()); return true; }
        if (node.str == "*") { value = Object(left.toDouble() * right.toDouble()); return true; }
        if (node.str == "/") { value = Object(left.toDouble() / right.toDouble()); return true; }
        return false;
    }
    if (node.str == "+") { value = wrap(static_cast<std::uint64_t>(left_integer) + static_cast<std::uint64_t>(right_integer)); return true; }
    if (node.str == "-") { value = wrap(static_cast<std::uint64_t>(left_integer) - static_cast<std::uint64_t>(right_integer)); return true; }
    if (node.str == "*") { value = wrap(static_cast<std::uint64_t>(left_integer) * static_cast<std::uint64_t>(right_integer)); return true; }
    if (node.str == "/")
    {
        if (right_integer == 0 || (left_integer == std::numeric_limits<std::int64_t>::min() && right_integer == -1)) return false;
        value = Object(left_integer / right_integer);
        return true;
    }
    if (node.str == "%")
    {
        if (right_integer == 0) return false;
        value = Object(left_integer == std::numeric_limits<std::int64_t>::min() && right_integer == -1 ? 0 : left_integer % right_integer);
        return true;
    }
    if (node.str == "&") { value = Object(left_integer & right_integer); return true; }
    if (node.str == "|") { value = Object(left_integer | right_integer); return true; }
    if (node.str == "^") { value = Object(left_integer ^ right_integer); return true; }
    if (node.str == "<<")
    {
        if (right_integer < 0 || right_integer >= 64) return false;
        value = wrap(static_cast<std::uint64_t>(left_integer) << right_integer);
        return true;
    }
    if (node.str == ">>")
    {
        if (right_integer < 0 || right_integer >= 64) return false;
        value = Object(left_integer >> right_integer);
        return true;
    }
    return false;
}

size_t CifaBytecode::next_local_slot() const
{
    size_t next = 0;
    for (const auto& scope : compile_local_scopes)
        for (const auto& entry : scope)
            if (entry.second >= next) next = entry.second + 1;
    return next;
}

bool CifaBytecode::emit_register_expression(CalUnit& node, std::vector<Instruction>& instructions)
{
    const auto eligible = [&](const auto& self, const CalUnit& value) -> bool
    {
        if (value.type == CalUnitType::Constant) return true;
        if (value.type == CalUnitType::Parameter && value.v.empty() && !value.with_type)
            return local_slot(value, false).has_value();
        Opcode opcode;
        return value.v.size() == 2 && operation(value, opcode) && opcode >= Opcode::Add && opcode <= Opcode::ShiftRight
            && self(self, value.v[0]) && self(self, value.v[1]);
    };
    if (node.v.size() != 2 || !eligible(eligible, node)
        || (node.v[0].v.empty() && node.v[1].v.empty())) return false;
    size_t temporary = 0;
    const auto generate = [&](const auto& self, CalUnit& value, bool root) -> size_t
    {
        if (value.v.empty())
        {
            const size_t slot = temporary++;
            if (value.type == CalUnitType::Constant)
            {
                Object number;
                if (!Cifa::parse_number_literal(value.str, number)) translation_error = "unsupported bytecode numeric literal";
                constants.push_back(std::move(number));
                instructions.push_back({Opcode::RegisterSnapshot, source_ref(&value), constants.size() - 1, slot});
                instructions.back().write = WriteOperation::Assign;
            }
            else
            {
                instructions.push_back({Opcode::RegisterSnapshot, source_ref(&value), *local_slot(value, false), slot});
                instructions.back().write = WriteOperation::Add;
                instructions.back().variable_site = intern_name(value.str);
            }
            return slot;
        }
        instructions.push_back({Opcode::Enter, source_ref(&value)});
        const size_t left = self(self, value.v[0], false);
        const size_t right = self(self, value.v[1], false);
        Opcode opcode;
        operation(value, opcode);
        const size_t destination = temporary++;
        RegisterBinarySite site{};
        site.code = {static_cast<std::uint16_t>(opcode), static_cast<std::uint16_t>(root ? 0 : 1), 0,
            static_cast<std::uint32_t>(left), static_cast<std::uint32_t>(right)};
        site.left_name = intern_name(value.v[0].str);
        site.right_name = intern_name(value.v[1].str);
        site.left_source = source_ref(&value.v[0]);
        site.right_source = source_ref(&value.v[1]);
        site.left_temporary = site.right_temporary = true;
        site.temporary_destination = root ? 0 : destination + 1;
        module_data->register_binary_sites.push_back(site);
        instructions.push_back({Opcode::RegisterBinary, source_ref(&value), module_data->register_binary_sites.size() - 1});
        instructions.push_back({Opcode::Leave, source_ref(&value)});
        return destination;
    };
    generate(generate, node, true);
    return true;
}

size_t CifaBytecode::emit_statement(CalUnit& node, std::vector<Instruction>& instructions)
{
    const size_t begin = instructions.size();
    auto* saved_statement_node = active_statement_node;
    active_statement_node = &node;
    emit(node, instructions);
    active_statement_node = saved_statement_node;
    if ((node.type == CalUnitType::Key && node.str == "if")
        || (node.type == CalUnitType::Operator && node.str == "?"))
    {
        const auto branch = std::find_if(instructions.begin() + begin, instructions.end(), [](const Instruction& instruction)
            { return instruction.opcode == Opcode::Branch; });
        if (branch != instructions.end() && branch->operand != 0 && branch->operand < instructions.size())
        {
            const size_t branch_index = static_cast<size_t>(branch - instructions.begin());
            const size_t else_begin = branch->operand;
            const auto jump = std::find_if(instructions.begin() + branch_index + 1, instructions.begin() + else_begin,
                [](const Instruction& instruction) { return instruction.opcode == Opcode::Jump; });
            const auto mark_result = [&](size_t first, size_t last)
            {
                for (size_t index = last; index > first; --index)
                {
                    auto& instruction = instructions[index - 1];
                    if (instruction.opcode == Opcode::Enter || instruction.opcode == Opcode::Leave
                        || instruction.opcode == Opcode::ScopeEnter || instruction.opcode == Opcode::ScopeLeave
                        || instruction.opcode == Opcode::LoopMark || instruction.opcode == Opcode::SwitchEnd) continue;
                    if (instruction.opcode != Opcode::Jump && instruction.opcode != Opcode::Unwind
                        && instruction.opcode != Opcode::Return && instruction.opcode != Opcode::Exit)
                        instruction.discard_result = true;
                    return;
                }
            };
            if (jump != instructions.begin() + else_begin)
                mark_result(branch_index + 1, static_cast<size_t>(jump - instructions.begin()));
            mark_result(else_begin, instructions.size());
        }
    }
    if (node.type == CalUnitType::Operator && (node.str == "&&" || node.str == "||"))
    {
        const auto branch = std::find_if(instructions.begin() + begin, instructions.end(), [](const Instruction& instruction)
            { return instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch; });
        const auto logical = std::find_if(instructions.rbegin(), instructions.rend(), [](const Instruction& instruction)
            { return instruction.opcode == Opcode::LogicalAnd || instruction.opcode == Opcode::LogicalOr; });
        if (branch != instructions.end() && logical != instructions.rend())
        {
            branch->discard_result = true;
            logical->discard_result = true;
        }
    }
    return begin;
}

void CifaBytecode::discard_statement_result(std::vector<Instruction>& instructions, size_t begin)
{
    for (size_t index = instructions.size(); index > begin; --index)
    {
        auto& instruction = instructions[index - 1];
        if (instruction.opcode == Opcode::Enter || instruction.opcode == Opcode::Leave
            || instruction.opcode == Opcode::ScopeEnter || instruction.opcode == Opcode::ScopeLeave
            || instruction.opcode == Opcode::LoopMark || instruction.opcode == Opcode::SwitchEnd) continue;
        if (instruction.opcode != Opcode::Jump && instruction.opcode != Opcode::Unwind
            && instruction.opcode != Opcode::Return && instruction.opcode != Opcode::Exit)
            instruction.discard_result = true;
        break;
    }
}

void CifaBytecode::emit(CalUnit& node, std::vector<Instruction>& instructions)
{
    if (optimization_enabled)
    {
        Object value;
        if (try_fold_constant(node, value))
        {
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.push_back(std::move(value));
            return;
        }
    }
    if (node.type == CalUnitType::None || node.type == CalUnitType::Split || node.str == ";")
    {
        instructions.push_back({Opcode::Empty, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Operator && node.str == "," && node.v.size() == 2)
    {
        discard_statement_result(instructions, emit_statement(node.v[0], instructions));
        if (active_statement_node == &node) emit_statement(node.v[1], instructions);
        else emit(node.v[1], instructions);
        return;
    }
    if (node.type == CalUnitType::Key && (node.str == "true" || node.str == "false"))
    {
        instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
        constants.emplace_back(node.str == "true");
        return;
    }
    if (node.type == CalUnitType::Key && node.str == "exit")
    {
        instructions.push_back({Opcode::Exit, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Operator && (node.str == "." || node.str == "::") && node.v.size() == 2
        && node.v[0].type == CalUnitType::Parameter && node.v[0].v.empty()
        && node.v[1].type == CalUnitType::Parameter)
    {
        instructions.push_back({Opcode::Member, source_ref(&node), intern_name(node.v[0].str), intern_name(node.v[1].str)});
        return;
    }
    if (node.type == CalUnitType::Operator && node.str == "." && node.v.size() == 2
        && node.v[0].type == CalUnitType::Parameter && node.v[0].v.empty()
        && node.v[1].type == CalUnitType::Function
        && (node.v[1].str == "push_back" || node.v[1].str == "resize" || node.v[1].str == "insert"
            || node.v[1].str == "erase" || node.v[1].str == "contains"))
    {
        const size_t site = calls.size();
        calls.push_back({source_ref(&node), {}});
        auto& call = calls.back();
        if (const auto slot = local_slot(node.v[0], false)) call.local_slot = *slot + 1;
        std::vector<CalUnit*> argument_nodes;
        std::function<void(CalUnit&)> flatten = [&](CalUnit& child)
        {
            if (child.str == ",") for (auto& item : child.v) flatten(item);
            else if (child.type != CalUnitType::None)
            {
                call.arguments.push_back(source(source_ref(&child)));
                argument_nodes.push_back(&child);
            }
        };
        for (auto& child : node.v[1].v) flatten(child);
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        if (node.v[1].str == "push_back" && argument_nodes.size() == 1)
        {
            emit(*argument_nodes.front(), instructions);
            instructions.push_back({Opcode::MethodPush, source_ref(&node), site});
                if (const auto slot = local_slot(node.v[0], false)) calls.back().local_slot = *slot + 1;
            instructions.push_back({Opcode::Leave, source_ref(&node)});
            return;
        }
        instructions.push_back({Opcode::MethodBegin, source_ref(&node), site});
        const size_t count = node.v[1].str == "push_back" ? call.arguments.size()
            : node.v[1].str == "insert" ? std::min<size_t>(2, call.arguments.size())
            : (call.arguments.empty() ? 0 : 1);
        for (size_t index = 0; index < count; ++index)
        {
            emit(*argument_nodes[index], instructions);
            instructions.push_back({Opcode::MethodValue, source_ref(&node), site, index});
        }
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Goto
        || (node.type == CalUnitType::Key && node.str == "goto" && node.v.size() == 1))
    {
        const auto& target = node.type == CalUnitType::Goto ? node.str : node.v[0].str;
        for (size_t index = compile_blocks.size(); index > 0; --index)
        {
            auto& block = compile_blocks[index - 1];
            if (!block.targets.contains(target)) continue;
            instructions.push_back({Opcode::Unwind, source_ref(&node), block.mark});
            block.jumps.emplace_back(instructions.size(), target);
            instructions.push_back({Opcode::Jump, source_ref(&node)});
            return;
        }
    }
    if (node.type == CalUnitType::Operator && node.str == "." && node.v.size() == 2
        && node.v[0].type == CalUnitType::Parameter && node.v[0].v.empty()
        && node.v[1].type == CalUnitType::Function
        && (node.v[1].str == "pop_back" || node.v[1].str == "clear" || node.v[1].str == "keys"))
    {
        const size_t site = calls.size();
        calls.push_back({source_ref(&node), {}});
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        instructions.push_back({Opcode::MethodNoArgs, source_ref(&node), site});
        if (const auto slot = local_slot(node.v[0], false)) instructions.back().auxiliary = *slot + 1;
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Parameter && !node.v.empty() && node.v[0].str == "[]")
    {
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        for (auto& dimension : node.v)
        {
            if (dimension.v.empty()) instructions.push_back({Opcode::Empty, source_ref(&node)});
            else emit(dimension.v[0], instructions);
        }
        const size_t site = index_site(node);
        instructions.push_back({Opcode::Index, source_ref(&node), node.v.size(), site});
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Union && node.str == "{}" && !node.v.empty()
        && std::none_of(node.v.begin(), node.v.end(), [](CalUnit& child) { return child.is_statement(); }))
    {
        size_t count = 0;
        std::function<void(CalUnit&)> element = [&](CalUnit& child)
        {
            if (child.str == ",") for (auto& item : child.v) element(item);
            else if (child.type != CalUnitType::None) { emit(child, instructions); ++count; }
        };
        for (auto& child : node.v) element(child);
        instructions.push_back({Opcode::Array, source_ref(&node), count});
        return;
    }
    if (node.type == CalUnitType::Union && node.str == "{}" && node.v.empty())
    {
        instructions.push_back({Opcode::Empty, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Union && node.str == "()")
    {
        if (node.v.empty()) instructions.push_back({Opcode::Empty, source_ref(&node)});
        else emit(node.v[0], instructions);
        return;
    }
    if (node.type == CalUnitType::Function)
    {
        const auto is_inline_argument = [](const auto& self, const CalUnit& value) -> bool
        {
            if (value.type == CalUnitType::Constant || value.type == CalUnitType::String
                || (value.type == CalUnitType::Key && (value.str == "true" || value.str == "false"))) return true;
            if (value.type == CalUnitType::Parameter && value.v.empty()) return true;
            return value.type == CalUnitType::Union && value.str == "()" && value.v.size() == 1 && self(self, value.v[0]);
        };
        const auto is_inline_expression = [](const auto& self, const CalUnit& value,
            const std::unordered_set<std::string>& parameters) -> bool
        {
            if (value.type == CalUnitType::Constant || value.type == CalUnitType::String
                || (value.type == CalUnitType::Key && (value.str == "true" || value.str == "false"))) return true;
            if (value.type == CalUnitType::Parameter) return value.v.empty() && parameters.contains(value.str);
            if ((value.type == CalUnitType::Operator || value.type == CalUnitType::Cast
                    || (value.type == CalUnitType::Union && value.str == "()")) && !value.v.empty())
            {
                return std::all_of(value.v.begin(), value.v.end(), [&](const CalUnit& child)
                    { return self(self, child, parameters); });
            }
            return false;
        };
        if (optimization_enabled && module_data->freeze_script_functions && compile_script_functions != nullptr)
        {
            std::vector<CalUnit*> arguments;
            std::function<bool(CalUnit&)> flatten = [&](CalUnit& item)
            {
                if (item.str == ",")
                {
                    for (auto& child : item.v)
                        if (!flatten(child)) return false;
                    return true;
                }
                if (item.type != CalUnitType::None) arguments.push_back(&item);
                return true;
            };
            for (auto& child : node.v)
                if (!flatten(child)) break;
            const auto overloads = compile_script_functions->find(node.str);
            if (overloads != compile_script_functions->end())
            {
                const auto definition = overloads->second.find(arguments.size());
                if (definition != overloads->second.end()
                    && (definition->second.return_type == "int" || definition->second.return_type == "double" || definition->second.return_type == "bool")
                    && definition->second.body.type == CalUnitType::Union && definition->second.body.str == "{}"
                    && definition->second.body.v.size() == 1 && definition->second.body.v[0].type == CalUnitType::Key
                    && definition->second.body.v[0].str == "return" && definition->second.body.v[0].v.size() == 1)
                {
                    std::unordered_set<std::string> parameters;
                    bool eligible = true;
                    for (size_t index = 0; index < arguments.size(); ++index)
                    {
                        eligible = eligible && definition->second.arguments[index].type_name.empty()
                            && is_inline_argument(is_inline_argument, *arguments[index]);
                        parameters.insert(definition->second.arguments[index].name);
                    }
                    const auto count_parameter_uses = [&](const auto& self, const CalUnit& value,
                        const std::string& parameter) -> size_t
                    {
                        size_t count = value.type == CalUnitType::Parameter && value.v.empty() && value.str == parameter ? 1 : 0;
                        for (const auto& child : value.v) count += self(self, child, parameter);
                        return count;
                    };
                    for (const auto& parameter : definition->second.arguments)
                        eligible = eligible && count_parameter_uses(count_parameter_uses, definition->second.body.v[0].v[0], parameter.name) == 1;
                    const std::string key = node.str + "/" + std::to_string(arguments.size());
                    eligible = eligible && !compile_inline_functions.contains(key)
                        && is_inline_expression(is_inline_expression, definition->second.body.v[0].v[0], parameters);
                    if (eligible)
                    {
                        std::unordered_map<std::string, const CalUnit*> replacements;
                        for (size_t index = 0; index < arguments.size(); ++index)
                            replacements.emplace(definition->second.arguments[index].name, arguments[index]);
                        const auto substitute = [&](const auto& self, const CalUnit& source) -> CalUnit
                        {
                            if (source.type == CalUnitType::Parameter && source.v.empty())
                            {
                                if (const auto found = replacements.find(source.str); found != replacements.end()) return *found->second;
                            }
                            CalUnit result = source;
                            result.v.clear();
                            result.v.reserve(source.v.size());
                            for (const auto& child : source.v) result.v.push_back(self(self, child));
                            return result;
                        };
                        compile_inline_functions.insert(key);
                        auto expression = substitute(substitute, definition->second.body.v[0].v[0]);
                        CalUnit converted;
                        converted.type = CalUnitType::Cast;
                        converted.type_name = definition->second.return_type;
                        converted.v.push_back(std::move(expression));
                        emit(converted, instructions);
                        compile_inline_functions.erase(key);
                        return;
                    }
                }
            }
        }
        const bool original_size = optimization_enabled && node.str == "size"
            && builtin_function_generations.contains("size") && function_generations.contains("size")
            && builtin_function_generations.at("size") == function_generations.at("size");
        if (original_size && node.v.size() == 1)
        {
            CalUnit* argument = &node.v[0];
            if (argument->type == CalUnitType::Union && argument->str == "()" && argument->v.size() == 1)
                argument = &argument->v[0];
            if (argument->str != "," && argument->type != CalUnitType::None)
            {
                instructions.push_back({Opcode::Enter, source_ref(&node)});
                if (argument->type == CalUnitType::Parameter && argument->v.empty() && !argument->with_type)
                    instructions.push_back({Opcode::Size, source_ref(&node), intern_name(argument->str), 1});
                else
                {
                    emit(*argument, instructions);
                    instructions.push_back({Opcode::Size, source_ref(&node)});
                }
                instructions.back().target_source = source_ref(argument);
                instructions.push_back({Opcode::Leave, source_ref(&node)});
                return;
            }
        }
        const auto math_kind = [](const std::string& name) -> MathKind
        {
            static const std::unordered_map<std::string, MathKind> kinds = {
                {"abs", MathKind::Abs}, {"sqrt", MathKind::Sqrt}, {"cbrt", MathKind::Cbrt}, {"round", MathKind::Round},
                {"trunc", MathKind::Trunc}, {"nearbyint", MathKind::NearbyInt}, {"rint", MathKind::Rint}, {"ceil", MathKind::Ceil},
                {"floor", MathKind::Floor}, {"sin", MathKind::Sin}, {"cos", MathKind::Cos}, {"tan", MathKind::Tan},
                {"asin", MathKind::Asin}, {"acos", MathKind::Acos}, {"atan", MathKind::Atan}, {"sinh", MathKind::Sinh},
                {"cosh", MathKind::Cosh}, {"tanh", MathKind::Tanh}, {"exp", MathKind::Exp}, {"log", MathKind::Log},
                {"log2", MathKind::Log2}, {"log10", MathKind::Log10}, {"erf", MathKind::Erf}, {"erfc", MathKind::Erfc},
                {"tgamma", MathKind::TGamma}, {"lgamma", MathKind::LGamma}, {"atan2", MathKind::Atan2}, {"pow", MathKind::Pow},
                {"hypot", MathKind::Hypot}, {"fmod", MathKind::Fmod}, {"remainder", MathKind::Remainder}, {"copysign", MathKind::CopySign},
                {"fdim", MathKind::FDim}, {"fmax", MathKind::FMax}, {"fmin", MathKind::FMin}
            };
            const auto found = kinds.find(name);
            return found == kinds.end() ? MathKind::None : found->second;
        };
        const MathKind direct_math_kind = math_kind(node.str);
        const size_t direct_math_arity = direct_math_kind <= MathKind::LGamma ? 1
            : direct_math_kind != MathKind::None ? 2 : 0;
        const bool original_math = optimization_enabled && direct_math_kind != MathKind::None
            && builtin_function_generations.contains(node.str) && function_generations.contains(node.str)
            && builtin_function_generations.at(node.str) == function_generations.at(node.str);
        if (original_math)
        {
            std::vector<CalUnit*> arguments;
            std::function<void(CalUnit&)> flatten = [&](CalUnit& item)
            {
                if (item.str == ",") for (auto& child : item.v) flatten(child);
                else if (item.type != CalUnitType::None) arguments.push_back(&item);
            };
            for (auto& child : node.v) flatten(child);
            if (arguments.size() == direct_math_arity)
            {
                const size_t site = calls.size();
                calls.push_back({source_ref(&node), {}});
                calls.back().name_id = intern_name(node.str);
                calls.back().math_kind = direct_math_kind;
                for (auto* argument : arguments)
                {
                    calls.back().arguments.push_back(source(source_ref(argument)));
                    emit(*argument, instructions);
                }
                instructions.push_back({direct_math_arity == 1 ? Opcode::MathUnary : Opcode::MathBinary,
                    source_ref(&node), site});
                return;
            }
        }
        const size_t index = calls.size();
        calls.push_back({source_ref(&node), {}});
        auto& call = calls.back();
        std::vector<CalUnit*> argument_nodes;
        std::function<void(CalUnit&)> flatten = [&](CalUnit& item)
        {
            if (item.str == ",") for (auto& child : item.v) flatten(child);
            else if (item.type != CalUnitType::None)
            {
                call.arguments.push_back(source(source_ref(&item)));
                argument_nodes.push_back(&item);
            }
        };
        for (auto& child : node.v) flatten(child);
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        instructions.push_back({Opcode::CallBegin, source_ref(&node), index});
        for (size_t argument_index = 0; argument_index < call.arguments.size(); ++argument_index)
        {
            auto& argument = *argument_nodes[argument_index];
            if (node.str == "type" && argument.type == CalUnitType::Parameter && argument.v.empty())
            {
                instructions.push_back({Opcode::Peek, source_ref(&argument), intern_name(argument.str), argument.with_type ? 0u : 1u});
                if (argument.with_type)
                {
                    variable_sites.push_back({intern_name(argument.str), intern_name(argument.type_name), true});
                    instructions.back().variable_site = variable_sites.size();
                }
            }
            else emit(argument, instructions);
            instructions.push_back({Opcode::BindArgument, source_ref(&node), argument_index, index});
        }
        instructions.push_back({Opcode::Call, source_ref(&node), index});
        instructions.push_back({Opcode::CallEnd, source_ref(&node), index});
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    const size_t trace_before = compile_traces;
    const bool traced = node.type != CalUnitType::Union;
    if (traced) ++compile_traces;
    struct RestoreDepth
    {
        size_t& register_top;
        size_t previous;
        ~RestoreDepth() { register_top = previous; }
    } restore_depth{compile_traces, trace_before};
    if (node.type == CalUnitType::Key && (node.str == "break" || node.str == "continue"))
    {
        Loop* loop = compile_loops.empty() ? nullptr : &compile_loops.back();
        if (node.str == "continue")
        {
            loop = nullptr;
            for (size_t index = compile_loops.size(); index > 0; --index)
                if (!compile_loops[index - 1].is_switch)
                {
                    loop = &compile_loops[index - 1];
                    break;
                }
        }
        if (loop == nullptr)
        {
            translation_error = node.str + " statement has no valid target";
            return;
        }
        instructions.push_back({Opcode::Unwind, source_ref(&node), loop->traces});
        instructions.push_back({Opcode::Jump, source_ref(&node)});
        (node.str == "break" ? loop->breaks : loop->continues).push_back(instructions.size() - 1);
        return;
    }
    if (node.type == CalUnitType::Key && node.str == "switch")
    {
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        const size_t mark = instructions.size();
        instructions.push_back({Opcode::LoopMark, source_ref(&node)});
        compile_loops.push_back({compile_scopes, mark, {}, {}, true});
        emit(node.v[0], instructions);
        instructions.push_back({Opcode::SwitchMark, source_ref(&node), mark});
        const size_t scope_enter = instructions.size();
        instructions.push_back({Opcode::ScopeEnter, source_ref(&node)});
        ++compile_scopes;
        if (compiling_function != nullptr)
        {
            compile_local_scope_bases.push_back(next_local_slot());
            instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
            compile_local_scopes.emplace_back();
        }
        std::vector<size_t> pending_cases;
        for (auto& child : node.v[1].v)
        {
            if (child.str == "case" || child.str == "default")
            {
                for (auto jump : pending_cases) instructions[jump].operand = instructions.size();
                pending_cases.clear();
                if (child.str == "case")
                {
                    const size_t active = instructions.size();
                    instructions.push_back({Opcode::SwitchCase, source_ref(&child), mark});
                    instructions.push_back({Opcode::Branch, source_ref(&child)});
                    const size_t skip_test = instructions.size();
                    instructions.push_back({Opcode::Jump, source_ref(&child)});
                    instructions[active + 1].operand = instructions.size();
                    emit(child.v[0], instructions);
                    instructions.push_back({Opcode::SwitchDefault, source_ref(&child), mark, 1});
                    instructions.push_back({Opcode::Branch, source_ref(&child)});
                    pending_cases.push_back(instructions.size() - 1);
                    instructions[skip_test].operand = instructions.size();
                }
                else
                {
                    instructions.push_back({Opcode::SwitchDefault, source_ref(&child), mark});
                }
            }
            else
            {
                const size_t active = instructions.size();
                instructions.push_back({Opcode::SwitchCase, source_ref(&child), mark});
                instructions.push_back({Opcode::Branch, source_ref(&child)});
                discard_statement_result(instructions, emit_statement(child, instructions));
                instructions[active + 1].operand = instructions.size();
            }
        }
        for (auto jump : pending_cases) instructions[jump].operand = instructions.size();
        instructions.push_back({Opcode::ScopeLeave, source_ref(&node)});
        --compile_scopes;
        if (compiling_function != nullptr)
        {
            instructions[scope_enter].operand = next_local_slot() - compile_local_scope_bases.back();
            instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
            compile_local_scope_bases.pop_back();
            compile_local_scopes.pop_back();
        }
        const size_t finish = instructions.size();
        const auto loop = std::move(compile_loops.back());
        compile_loops.pop_back();
        for (auto jump : loop.breaks) instructions[jump].operand = finish + 1;
        instructions.push_back({Opcode::SwitchEnd, source_ref(&node), mark});
        instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
        constants.emplace_back(0);
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    const bool ordinary_for = node.type == CalUnitType::Key && node.str == "for" && node.v.size() == 2
        && node.v[0].v.size() == 3;
    if (node.type == CalUnitType::Key && node.str == "for" && !ordinary_for)
    {
        CalUnit* clause = &node.v[0];
        if (clause->str == "()" && clause->v.size() == 1) clause = &clause->v[0];
        if (clause->str == ":" && clause->v.size() == 2)
        {
            instructions.push_back({Opcode::Enter, source_ref(&node)});
            emit(clause->v[1], instructions);
            const size_t mark = instructions.size();
            instructions.push_back({Opcode::RangeBegin, source_ref(clause), mark});
            const size_t loop_mark = instructions.size();
            instructions.push_back({Opcode::LoopMark, source_ref(&node)});
            compile_loops.push_back({compile_scopes, loop_mark, {}, {}});
            const size_t next = instructions.size();
            instructions.push_back({Opcode::RangeNext, source_ref(clause), mark});
            variable_sites.push_back({intern_name(clause->v[0].str), intern_name(clause->v[0].type_name), clause->v[0].with_type});
            instructions.back().variable_site = variable_sites.size();
            const size_t branch = instructions.size();
            instructions.push_back({Opcode::Branch, source_ref(clause)});
            const size_t scope_enter = instructions.size();
            instructions.push_back({Opcode::ScopeEnter, source_ref(&node)});
            ++compile_scopes;
            if (compiling_function != nullptr)
            {
                compile_local_scope_bases.push_back(next_local_slot());
                compile_local_scopes.emplace_back();
                if (const auto slot = local_slot(clause->v[0], true, true))
                    instructions[next].auxiliary = *slot + 1;
                instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
            }
            discard_statement_result(instructions, emit_statement(node.v[1], instructions));
            instructions.push_back({Opcode::ScopeLeave, source_ref(&node)});
            --compile_scopes;
            if (compiling_function != nullptr)
            {
                instructions[scope_enter].operand = next_local_slot() - compile_local_scope_bases.back();
                instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
                compile_local_scope_bases.pop_back();
                compile_local_scopes.pop_back();
            }
            instructions.push_back({Opcode::Jump, source_ref(&node), next});
            const size_t finish = instructions.size();
            instructions[branch].operand = finish;
            const auto loop = std::move(compile_loops.back());
            compile_loops.pop_back();
            for (auto jump : loop.breaks) instructions[jump].operand = finish;
            for (auto jump : loop.continues) instructions[jump].operand = next;
            instructions.push_back({Opcode::RangeEnd, source_ref(clause), mark});
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.emplace_back(0);
            instructions.push_back({Opcode::Leave, source_ref(&node)});
            return;
        }
    }
    if (node.type == CalUnitType::Key && (node.str == "while" || node.str == "do" || ordinary_for))
    {
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        const size_t mark = instructions.size();
        instructions.push_back({Opcode::LoopMark, source_ref(&node)});
        compile_loops.push_back({compile_scopes, mark, {}, {}});
        if (ordinary_for)
        {
            discard_statement_result(instructions, emit_statement(node.v[0].v[0], instructions));
        }
        const size_t start = instructions.size();
        size_t branch = 0;
        if (node.str != "do")
        {
            emit(ordinary_for ? node.v[0].v[1] : node.v[0], instructions);
            branch = instructions.size();
            instructions.push_back({Opcode::Branch, source_ref(&node)});
            auto* condition = ordinary_for ? &node.v[0].v[1] : &node.v[0];
            if (condition->str == "()" && condition->v.size() == 1) condition = &condition->v[0];
            instructions.back().condition_source = source_ref(condition);
        }
        discard_statement_result(instructions, emit_statement(node.v[node.str == "do" ? 0 : 1], instructions));
        const size_t next = instructions.size();
        if (ordinary_for)
        {
            discard_statement_result(instructions, emit_statement(node.v[0].v[2], instructions));
        }
        if (node.str == "do")
        {
            emit(node.v[1].v[0], instructions);
            branch = instructions.size();
            instructions.push_back({Opcode::Branch, source_ref(&node)});
            auto* condition = &node.v[1].v[0];
            if (condition->str == "()" && condition->v.size() == 1) condition = &condition->v[0];
            instructions.back().condition_source = source_ref(condition);
        }
        instructions.push_back({Opcode::Jump, source_ref(&node), start});
        const size_t finish = instructions.size();
        instructions[branch].operand = finish;
        const auto loop = std::move(compile_loops.back());
        compile_loops.pop_back();
        for (auto jump : loop.breaks) instructions[jump].operand = finish;
        for (auto jump : loop.continues) instructions[jump].operand = next;
        instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
        constants.emplace_back(0);
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Operator && !node.v.empty()
        && ((node.v[0].type == CalUnitType::Parameter
                && (node.v[0].v.empty() || (!node.v[0].v.empty() && node.v[0].v[0].str == "[]")))
            || (node.v[0].type == CalUnitType::Operator && node.v[0].str == "." && node.v[0].v.size() == 2
                && node.v[0].v[0].type == CalUnitType::Parameter && node.v[0].v[1].type == CalUnitType::Parameter)))
    {
        if (node.v.size() == 2 && (node.str == "=" || node.str == "+=" || node.str == "-="
            || node.str == "*=" || node.str == "/=" || node.str == "%=" || node.str == "&="
            || node.str == "|=" || node.str == "^=" || node.str == "<<=" || node.str == ">>="))
        {
            if (node.v[0].type == CalUnitType::Parameter && node.v[0].v.empty())
            {
                if (const auto slot = local_slot(node.v[0], true))
                {
                    instructions.push_back({Opcode::Enter, source_ref(&node)});
                    emit(node.v[1], instructions);
                    if (node.str == "=" && instructions.size() >= 2
                        && instructions.back().opcode == Opcode::Leave
                        && instructions[instructions.size() - 2].opcode == Opcode::RegisterBinary)
                    {
                        auto& site = module_data->register_binary_sites[instructions[instructions.size() - 2].operand];
                        if (*slot >= std::numeric_limits<std::uint32_t>::max())
                        {
                            translation_error = "bytecode register destination exceeds encoding range";
                            return;
                        }
                        site.code.destination = static_cast<std::uint32_t>(*slot + 1);
                        variable_sites.push_back({intern_name(node.v[0].str), intern_name(node.v[0].type_name), node.v[0].with_type});
                        site.variable_site = variable_sites.size();
                        site.assignment_source = source_ref(&node);
                        instructions.push_back({Opcode::Leave, source_ref(&node)});
                        return;
                    }
                    instructions.push_back({Opcode::StoreLocal, source_ref(&node), *slot});
                    instructions.back().write = write_operation(node.str);
                    variable_sites.push_back({intern_name(node.v[0].str), intern_name(node.v[0].type_name), node.v[0].with_type});
                    instructions.back().variable_site = variable_sites.size();
                    instructions.push_back({Opcode::Leave, source_ref(&node)});
                    return;
                }
            }
            const bool empty_array = node.v[1].type == CalUnitType::Union && node.v[1].str == "{}" && node.v[1].v.empty();
            const size_t indexed_site = node.v[0].type == CalUnitType::Parameter && !node.v[0].v.empty()
                ? index_site(node.v[0]) + 1 : 0;
            size_t member_site = 0;
            if (node.v[0].type == CalUnitType::Operator)
            {
                member_sites.emplace_back(intern_name(node.v[0].v[0].str), intern_name(node.v[0].v[1].str));
                member_site = member_sites.size();
            }
            instructions.push_back({Opcode::Enter, source_ref(&node)});
            if (indexed_site != 0)
                for (auto& dimension : node.v[0].v)
                    if (dimension.v.empty()) instructions.push_back({Opcode::Empty, source_ref(&node)});
                    else emit(dimension.v[0], instructions);
            instructions.push_back({Opcode::PrepareStore, source_ref(&node), indexed_site});
            instructions.back().member_site = member_site;
            size_t variable_site = 0;
            if (indexed_site == 0 && member_site == 0)
            {
                variable_sites.push_back({intern_name(node.v[0].str), intern_name(node.v[0].type_name), node.v[0].with_type});
                variable_site = variable_sites.size();
                instructions.back().variable_site = variable_site;
            }
            if (empty_array)
            {
                instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
                constants.emplace_back(std::vector<Object>{});
            }
            else emit(node.v[1], instructions);
            instructions.push_back({Opcode::Store, source_ref(&node), indexed_site});
            instructions.back().write = write_operation(node.str);
            instructions.back().variable_site = variable_site;
            instructions.back().member_site = member_site;
            CalUnit* base = node.v[0].type == CalUnitType::Parameter ? &node.v[0]
                : node.v[0].type == CalUnitType::Operator && node.v[0].str == "." ? &node.v[0].v[0] : nullptr;
            if (base != nullptr)
                if (const auto slot = local_slot(*base, false)) instructions.back().auxiliary = *slot + 1;
            instructions.push_back({Opcode::Leave, source_ref(&node)});
            return;
        }
        if (node.v.size() == 1 && (node.str == "++" || node.str == "--" || node.str == "()++" || node.str == "()--"))
        {
            if (node.v[0].type == CalUnitType::Parameter && node.v[0].v.empty())
            {
                if (const auto slot = local_slot(node.v[0], false))
                {
                    instructions.push_back({Opcode::Enter, source_ref(&node)});
                    instructions.push_back({Opcode::IncrementLocal, source_ref(&node), *slot});
                    instructions.back().write = write_operation(node.str);
                    variable_sites.push_back({intern_name(node.v[0].str), intern_name(node.v[0].type_name), node.v[0].with_type});
                    instructions.back().variable_site = variable_sites.size();
                    instructions.push_back({Opcode::Leave, source_ref(&node)});
                    return;
                }
            }
            instructions.push_back({Opcode::Enter, source_ref(&node)});
            const size_t indexed_site = node.v[0].type == CalUnitType::Parameter && !node.v[0].v.empty()
                ? index_site(node.v[0]) + 1 : 0;
            if (indexed_site != 0)
                for (auto& dimension : node.v[0].v)
                    if (dimension.v.empty()) instructions.push_back({Opcode::Empty, source_ref(&node)});
                    else emit(dimension.v[0], instructions);
            instructions.push_back({Opcode::Increment, source_ref(&node), indexed_site});
            instructions.back().write = write_operation(node.str);
            const CalUnit* base = &node.v[0];
            if (indexed_site == 0 && base->type == CalUnitType::Parameter)
            {
                variable_sites.push_back({intern_name(base->str), intern_name(base->type_name), base->with_type});
                instructions.back().variable_site = variable_sites.size();
            }
            if (base->type == CalUnitType::Operator)
            {
                member_sites.emplace_back(intern_name(base->v[0].str), intern_name(base->v[1].str));
                instructions.back().member_site = member_sites.size();
                base = &base->v[0];
            }
            if (const auto slot = local_slot(*base, false)) instructions.back().auxiliary = *slot + 1;
            instructions.push_back({Opcode::Leave, source_ref(&node)});
            return;
        }
    }
    if (node.type == CalUnitType::Union && !node.v.empty()
        && std::any_of(node.v.begin(), node.v.end(), [](CalUnit& child) { return child.is_statement() || child.type == CalUnitType::Label; }))
    {
        const bool scope = node.str == "{}";
        size_t scope_enter = 0;
        if (scope)
        {
            scope_enter = instructions.size();
            instructions.push_back({Opcode::ScopeEnter, source_ref(&node)});
            ++compile_scopes;
            if (compiling_function != nullptr)
            {
                compile_local_scope_bases.push_back(next_local_slot());
                instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
                compile_local_scopes.emplace_back();
            }
        }
        const bool has_child = std::any_of(node.v.begin(), node.v.end(), [](const CalUnit& child)
            { return child.type != CalUnitType::Label; });
        if (!has_child) instructions.push_back({Opcode::Empty, source_ref(&node)});
        const size_t block_mark = instructions.size();
        instructions.push_back({Opcode::LoopMark, source_ref(&node)});
        compile_blocks.push_back({block_mark, {}, {}});
        for (const auto& child : node.v)
            if (child.type == CalUnitType::Label) compile_blocks.back().targets[child.str] = 0;
        for (auto& child : node.v)
        {
            if (child.type == CalUnitType::Label)
            {
                compile_blocks.back().targets[child.str] = instructions.size();
                continue;
            }
            if (has_child && instructions.size() > block_mark + 1)
                discard_statement_result(instructions, block_mark + 1);
            emit_statement(child, instructions);
        }
        for (const auto& jump : compile_blocks.back().jumps)
            instructions[jump.first].operand = compile_blocks.back().targets.at(jump.second);
        compile_blocks.pop_back();
        if (scope)
        {
            instructions.push_back({Opcode::ScopeLeave, source_ref(&node)});
            --compile_scopes;
            if (compiling_function != nullptr)
            {
                instructions[scope_enter].operand = next_local_slot() - compile_local_scope_bases.back();
                instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
                compile_local_scope_bases.pop_back();
                compile_local_scopes.pop_back();
            }
        }
        return;
    }
    if (node.type == CalUnitType::Key && node.str == "if")
    {
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        emit(node.v[0], instructions);
        const size_t branch = instructions.size();
        instructions.push_back({Opcode::Branch, source_ref(&node)});
        auto* condition = &node.v[0];
        if (condition->str == "()" && condition->v.size() == 1) condition = &condition->v[0];
        instructions.back().condition_source = source_ref(condition);
        emit(node.v[1], instructions);
        const size_t jump = instructions.size();
        instructions.push_back({Opcode::Jump, source_ref(&node)});
        instructions[branch].operand = instructions.size();
        if (node.v.size() > 2)
        {
            if (active_statement_node == &node) emit_statement(node.v[2], instructions);
            else emit(node.v[2], instructions);
        }
        else
        {
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.emplace_back(0);
        }
        instructions[jump].operand = instructions.size();
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    if (node.type == CalUnitType::Key && node.str == "return")
    {
        instructions.push_back({Opcode::Enter, source_ref(&node)});
        if (node.v.empty()) instructions.push_back({Opcode::Empty, source_ref(&node)});
        else emit(node.v[0], instructions);
        instructions.push_back({Opcode::Return, source_ref(&node)});
        instructions.push_back({Opcode::Leave, source_ref(&node)});
        return;
    }
    Opcode opcode;
    if (!operation(node, opcode))
    {
        if (node.type == CalUnitType::Constant)
        {
            Object value;
            if (Cifa::parse_number_literal(node.str, value))
            {
                instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
                constants.push_back(std::move(value));
            }
            else translation_error = "unsupported bytecode numeric literal";
        }
        else if (node.type == CalUnitType::Parameter && node.v.empty())
        {
            if (node.with_type)
            {
                auto slot = local_slot(node, false);
                if (!slot) slot = local_slot(node, true);
                if (slot) instructions.push_back({Opcode::DeclareLocal, source_ref(&node), *slot});
                else instructions.push_back({Opcode::Load, source_ref(&node)});
                variable_sites.push_back({intern_name(node.str), intern_name(node.type_name), true});
                instructions.back().variable_site = variable_sites.size();
            }
            else if (const auto slot = local_slot(node, false)) instructions.push_back({Opcode::LoadLocal, source_ref(&node), *slot, intern_name(node.str)});
            else instructions.push_back({Opcode::Load, source_ref(&node), intern_name(node.str), 1});
        }
        else if (node.type == CalUnitType::String)
        {
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.emplace_back(node.str);
        }
        else translation_error = "unsupported bytecode node: " + node.str;
        return;
    }
    if (emit_register_expression(node, instructions)) return;
    instructions.push_back({Opcode::Enter, source_ref(&node)});
    if (node.v.size() == 2
        && opcode >= Opcode::Add && opcode <= Opcode::ShiftRight
        && (node.v[0].type == CalUnitType::Parameter || node.v[0].type == CalUnitType::Constant)
        && node.v[0].v.empty() && !node.v[0].with_type
        && (node.v[1].type == CalUnitType::Parameter || node.v[1].type == CalUnitType::Constant)
        && node.v[1].v.empty() && !node.v[1].with_type)
    {
        const auto operand = [&](CalUnit& value) -> std::optional<size_t>
        {
            if (value.type != CalUnitType::Constant) return local_slot(value, false);
            Object number;
            if (!Cifa::parse_number_literal(value.str, number)) return {};
            constants.push_back(std::move(number));
            return constants.size() - 1;
        };
        const auto left = operand(node.v[0]);
        const auto right = operand(node.v[1]);
        if (left && right)
        {
            if (*left > std::numeric_limits<std::uint32_t>::max() || *right > std::numeric_limits<std::uint32_t>::max())
            {
                translation_error = "bytecode register operand exceeds encoding range";
                return;
            }
            const size_t site = module_data->register_binary_sites.size();
            module_data->register_binary_sites.push_back({{static_cast<std::uint16_t>(opcode), 0, 0,
                static_cast<std::uint32_t>(*left), static_cast<std::uint32_t>(*right)},
                intern_name(node.v[0].str), intern_name(node.v[1].str),
                source_ref(&node.v[0]), source_ref(&node.v[1])});
            module_data->register_binary_sites.back().left_constant = node.v[0].type == CalUnitType::Constant;
            module_data->register_binary_sites.back().right_constant = node.v[1].type == CalUnitType::Constant;
            instructions.push_back({Opcode::RegisterBinary, source_ref(&node), site});
            instructions.push_back({Opcode::Leave, source_ref(&node)});
            return;
        }
    }
    emit(node.v[0], instructions);
    if (opcode == Opcode::Branch)
    {
        const size_t branch = instructions.size();
        instructions.push_back({Opcode::Branch, source_ref(&node)});
        emit(node.v[1].v[0], instructions);
        const size_t jump = instructions.size();
        instructions.push_back({Opcode::Jump, source_ref(&node)});
        instructions[branch].operand = instructions.size();
        emit(node.v[1].v[1], instructions);
        instructions[jump].operand = instructions.size();
    }
    else if (opcode == Opcode::LogicalAnd || opcode == Opcode::LogicalOr)
    {
        const size_t branch = instructions.size();
        instructions.push_back({opcode == Opcode::LogicalAnd ? Opcode::AndBranch : Opcode::OrBranch, source_ref(&node)});
        emit(node.v[1], instructions);
        instructions.push_back({opcode, source_ref(&node)});
        instructions[branch].operand = instructions.size();
    }
    else
    {
        if (node.v.size() == 2) emit(node.v[1], instructions);
        instructions.push_back({opcode, source_ref(&node)});
        if (opcode == Opcode::Cast) instructions.back().operand = intern_name(node.type_name);
    }
    instructions.push_back({Opcode::Leave, source_ref(&node)});
}

bool CifaBytecode::verify(Instructions& instructions, size_t local_slot_count)
{
    for (const auto& instruction : instructions.code)
    {
        if (instruction.opcode == Opcode::RegisterSnapshot)
        {
            if (instruction.auxiliary >= std::numeric_limits<std::uint32_t>::max())
            { translation_error = "bytecode temporary register exceeds encoding range"; return false; }
            instructions.temporary_count = (std::max)(instructions.temporary_count, instruction.auxiliary + 1);
        }
        if (instruction.opcode == Opcode::RegisterBinary && instruction.operand < module_data->register_binary_sites.size())
            instructions.temporary_count = (std::max)(instructions.temporary_count,
                module_data->register_binary_sites[instruction.operand].temporary_destination);
    }
    struct State
    {
        size_t register_top = 0;
        std::vector<size_t> frames;
        std::vector<std::pair<size_t, bool>> diagnostics;
        size_t scopes = 0;
        std::vector<size_t> local_scope_bases;
        std::vector<std::pair<size_t, bool>> calls;
        std::vector<std::pair<size_t, size_t>> methods;
        std::vector<size_t> ranges;
        std::vector<size_t> switches;
        bool visited = false;
    };
    std::vector<State> states(instructions.code.size() + 1);
    instructions.diagnostic_frames.assign(instructions.code.size(), {});
    instructions.register_inputs.clear();
    instructions.register_capacity = 0;
    std::vector<size_t> pending{0};
    states[0].visited = true;
    if (&instructions == &root_instructions)
    {
        for (const auto entry : root_entries)
        {
            if (entry > instructions.code.size())
            { translation_error = "bytecode entry out of range"; return false; }
            states[entry].register_top = 0;
            if (!states[entry].visited)
            {
                states[entry].visited = true;
                pending.push_back(entry);
            }
        }
        if (instructions.code.size() > 1)
        {
            states[0].register_top = 0;
            states[0].visited = true;
            pending.push_back(0);
        }
    }
    auto merge = [&](size_t target, const State& state)
    {
        if (target > instructions.code.size())
        {
            translation_error = "bytecode jump out of range";
            return false;
        }
        auto& previous = states[target];
        if (previous.visited)
        {
            if (previous.register_top != state.register_top || previous.frames != state.frames || previous.diagnostics != state.diagnostics || previous.scopes != state.scopes
                || previous.local_scope_bases != state.local_scope_bases || previous.calls != state.calls || previous.methods != state.methods
                || previous.ranges != state.ranges || previous.switches != state.switches)
            {
                translation_error = "bytecode control flow register mismatch";
                return false;
            }
        }
        else
        {
            previous = state;
            previous.visited = true;
            pending.push_back(target);
        }
        return true;
    };
    for (const auto& instruction : instructions.code)
    {
        const size_t pc = static_cast<size_t>(&instruction - instructions.code.data());
        if (instruction.opcode == Opcode::RangeBegin && instruction.operand != pc)
        { translation_error = "invalid bytecode range identifier"; return false; }
        if ((instruction.opcode == Opcode::RangeNext || instruction.opcode == Opcode::RangeEnd)
            && (instruction.operand >= pc || instructions.code[instruction.operand].opcode != Opcode::RangeBegin))
        { translation_error = "invalid bytecode range identifier"; return false; }
        if (instruction.opcode == Opcode::Unwind && (instruction.operand >= instructions.code.size()
            || instructions.code[instruction.operand].opcode != Opcode::LoopMark))
        { translation_error = "invalid bytecode loop unwind target"; return false; }
        if ((instruction.opcode == Opcode::SwitchMark || instruction.opcode == Opcode::SwitchCase
            || instruction.opcode == Opcode::SwitchDefault || instruction.opcode == Opcode::SwitchEnd)
            && (instruction.operand >= pc || instructions.code[instruction.operand].opcode != Opcode::LoopMark
                || ((instruction.opcode == Opcode::SwitchCase || instruction.opcode == Opcode::SwitchDefault)
                    && instruction.auxiliary > 1)))
        { translation_error = "bytecode switch frame mismatch"; return false; }
        if (static_cast<unsigned>(instruction.opcode) > static_cast<unsigned>(Opcode::Exit))
        { translation_error = "invalid bytecode opcode"; return false; }
        if (instruction.opcode == Opcode::Constant && instruction.operand >= constants.size())
        { translation_error = "bytecode constant index out of range"; return false; }
        if (instruction.opcode == Opcode::RegisterBinary)
        {
            if (instruction.operand >= module_data->register_binary_sites.size())
            { translation_error = "invalid bytecode register operation"; return false; }
            const auto& site = module_data->register_binary_sites[instruction.operand];
            if (site.code.opcode < static_cast<unsigned>(Opcode::Add) || site.code.opcode > static_cast<unsigned>(Opcode::ShiftRight)
                || site.code.flags > 1 || (site.code.flags != 0 && site.code.destination == 0 && site.temporary_destination == 0)
                || site.code.left >= (site.left_temporary ? instructions.temporary_count : site.left_constant ? constants.size() : local_slot_count)
                || site.code.right >= (site.right_temporary ? instructions.temporary_count : site.right_constant ? constants.size() : local_slot_count)
                || site.left_name >= names.size() || site.right_name >= names.size()
                || site.left_source.id == 0 || site.left_source.id > sources.size()
                || site.right_source.id == 0 || site.right_source.id > sources.size())
            { translation_error = "invalid bytecode register operand"; return false; }
            if (site.code.destination != 0)
            {
                if (site.code.destination - 1 >= local_slot_count || site.variable_site == 0
                    || site.variable_site > variable_sites.size() || site.assignment_source.id == 0
                    || site.assignment_source.id > sources.size())
                { translation_error = "invalid bytecode register destination"; return false; }
                const auto& binding = variable_sites[site.variable_site - 1];
                if (binding.name_id >= names.size() || binding.type_id >= names.size())
                { translation_error = "invalid bytecode register binding"; return false; }
            }
        }
        if ((instruction.opcode == Opcode::Jump || instruction.opcode == Opcode::Branch
            || instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch)
            && instruction.operand > instructions.code.size())
        { translation_error = "bytecode jump out of range"; return false; }
        if ((instruction.opcode == Opcode::ScopeEnter || instruction.opcode == Opcode::ScopeLeave)
            && instruction.auxiliary != 0 && instruction.auxiliary - 1 > local_slot_count)
        { translation_error = "bytecode scope slot base out of range"; return false; }
        if (instruction.opcode == Opcode::CallBegin || instruction.opcode == Opcode::Call || instruction.opcode == Opcode::CallEnd)
        {
            if (instruction.operand >= calls.size())
            { translation_error = "invalid bytecode call index"; return false; }
            if (calls[instruction.operand].name_id >= names.size())
            { translation_error = "invalid bytecode name index"; return false; }
        }
        if (instruction.opcode == Opcode::BindArgument && (instruction.auxiliary >= calls.size()
            || instruction.operand >= calls[instruction.auxiliary].arguments.size()))
        { translation_error = "invalid bytecode argument binding"; return false; }
        if (instruction.opcode == Opcode::Index && (instruction.operand == 0
            || instruction.auxiliary >= index_sites.size()
            || index_sites[instruction.auxiliary].dimensions != instruction.operand
            || index_sites[instruction.auxiliary].name_id >= names.size()
            || index_sites[instruction.auxiliary].type_id >= names.size()))
        { translation_error = "invalid bytecode index descriptor"; return false; }
        if (instruction.source.id == 0 || instruction.source.id > sources.size())
        {
            translation_error = "bytecode instruction has no source";
            return false;
        }
        if (instruction.opcode == Opcode::Member && (instruction.operand >= names.size() || instruction.auxiliary >= names.size()))
        { translation_error = "invalid bytecode member name"; return false; }
        if (instruction.condition_source.id > sources.size())
        { translation_error = "invalid bytecode condition source"; return false; }
        if (instruction.target_source.id > sources.size())
        { translation_error = "invalid bytecode target source"; return false; }
        if ((instruction.opcode == Opcode::RangeBegin || instruction.opcode == Opcode::RangeNext
            || instruction.opcode == Opcode::PrepareStore || instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment)
            && instruction.target_source.id == 0)
        { translation_error = "invalid bytecode target source"; return false; }
        if (instruction.opcode == Opcode::LoadLocal && instruction.auxiliary >= names.size())
        { translation_error = "invalid bytecode variable name"; return false; }
        if (instruction.opcode == Opcode::Cast && instruction.operand >= names.size())
        { translation_error = "invalid bytecode type name"; return false; }
        if ((instruction.opcode == Opcode::Peek || instruction.opcode == Opcode::Load) && instruction.auxiliary == 1
            && instruction.operand >= names.size())
        { translation_error = "invalid bytecode variable name"; return false; }
        if (instruction.opcode == Opcode::Store || instruction.opcode == Opcode::StoreLocal
            || instruction.opcode == Opcode::Increment || instruction.opcode == Opcode::IncrementLocal)
        {
            const bool increment = instruction.opcode == Opcode::Increment || instruction.opcode == Opcode::IncrementLocal;
            const bool valid_increment = instruction.write == WriteOperation::Add || instruction.write == WriteOperation::Subtract
                || instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract;
            if ((increment && !valid_increment) || (!increment && instruction.write > WriteOperation::ShiftRight))
            { translation_error = "invalid bytecode write operation"; return false; }
        }
        if (instruction.opcode == Opcode::MethodBegin || instruction.opcode == Opcode::MethodValue
            || instruction.opcode == Opcode::MethodNoArgs || instruction.opcode == Opcode::MethodPush)
        {
            if (instruction.operand >= calls.size())
            { translation_error = "invalid method site"; return false; }
            const auto& site = calls[instruction.operand];
            if (site.local_slot != 0 && site.local_slot - 1 >= local_slot_count)
            { translation_error = "bytecode local slot out of range"; return false; }
            if (site.name_id >= names.size() || site.base_name_id >= names.size()
                || site.method_source.id == 0 || site.method_source.id > sources.size())
            { translation_error = "invalid bytecode method descriptor"; return false; }
            if (instruction.opcode == Opcode::MethodValue && instruction.auxiliary >= site.arguments.size())
            { translation_error = "invalid bytecode method argument"; return false; }
        }
        if (instruction.opcode == Opcode::PrepareStore || instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment)
        {
            const bool plain = instruction.variable_site != 0;
            const bool indexed = instruction.operand != 0;
            const bool member_target = instruction.member_site != 0;
            if (static_cast<unsigned>(plain) + static_cast<unsigned>(indexed) + static_cast<unsigned>(member_target) != 1)
            { translation_error = "invalid bytecode variable descriptor"; return false; }
            if (plain)
            {
                if (instruction.variable_site - 1 >= variable_sites.size())
                { translation_error = "invalid bytecode variable descriptor"; return false; }
                const auto& site = variable_sites[instruction.variable_site - 1];
                if (site.name_id >= names.size() || site.type_id >= names.size())
                { translation_error = "invalid bytecode variable descriptor"; return false; }
            }
            if (instruction.member_site != 0)
            {
                if (instruction.member_site - 1 >= member_sites.size())
                { translation_error = "invalid bytecode member descriptor"; return false; }
                const auto& member = member_sites[instruction.member_site - 1];
                if (member.first >= names.size() || member.second >= names.size())
                { translation_error = "invalid bytecode member descriptor"; return false; }
            }
            if (indexed)
            {
                if (instruction.operand - 1 >= index_sites.size())
                { translation_error = "invalid bytecode index descriptor"; return false; }
                const auto& site = index_sites[instruction.operand - 1];
                if (site.dimensions == 0 || site.name_id >= names.size() || site.type_id >= names.size())
                { translation_error = "invalid bytecode index descriptor"; return false; }
                if (site.local_slot != 0 && site.local_slot - 1 >= local_slot_count)
                { translation_error = "bytecode local slot out of range"; return false; }
            }
        }
        if ((instruction.opcode == Opcode::LoadLocal || instruction.opcode == Opcode::DeclareLocal
                || instruction.opcode == Opcode::StoreLocal || instruction.opcode == Opcode::IncrementLocal)
            && instruction.operand >= local_slot_count)
        {
            translation_error = "bytecode local slot out of range";
            return false;
        }
        if (instruction.opcode == Opcode::StoreLocal || instruction.opcode == Opcode::IncrementLocal
            || instruction.opcode == Opcode::DeclareLocal || instruction.opcode == Opcode::RangeNext
            || ((instruction.opcode == Opcode::Load || instruction.opcode == Opcode::Peek) && instruction.auxiliary != 1))
        {
            if (instruction.variable_site == 0 || instruction.variable_site - 1 >= variable_sites.size())
            { translation_error = "invalid bytecode variable descriptor"; return false; }
            const auto& site = variable_sites[instruction.variable_site - 1];
            if (site.name_id >= names.size() || site.type_id >= names.size())
            { translation_error = "invalid bytecode variable descriptor"; return false; }
        }
        if ((instruction.opcode == Opcode::RangeNext || instruction.opcode == Opcode::MethodNoArgs
            || instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment)
            && instruction.auxiliary != 0 && instruction.auxiliary - 1 >= local_slot_count)
        {
            translation_error = "bytecode local slot out of range";
            return false;
        }
    }
    while (!pending.empty())
    {
        const size_t pc = pending.back();
        pending.pop_back();
        if (pc == instructions.code.size()) continue;
        State state = states[pc];
        auto& register_top = state.register_top;
        auto& frames = state.frames;
        auto& instruction = instructions.code[pc];
        const size_t input_top = register_top;
        instructions.diagnostic_frames[pc] = state.diagnostics;
        switch (instruction.opcode)
        {
        case Opcode::RegisterSnapshot:
            if ((instruction.write == WriteOperation::Assign && instruction.operand >= constants.size())
                || (instruction.write == WriteOperation::Add && (instruction.operand >= local_slot_count || instruction.variable_site >= names.size()))
                || (instruction.write != WriteOperation::Assign && instruction.write != WriteOperation::Add))
            { translation_error = "invalid bytecode register snapshot"; return false; }
            break;
        case Opcode::RegisterBinary:
            if ((module_data->register_binary_sites[instruction.operand].code.flags & 1) == 0) ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::Exit:
            continue;
        case Opcode::MethodBegin:
            if (instruction.operand >= calls.size()) { translation_error = "invalid method site"; return false; }
            if (calls[instruction.operand].local_slot != 0
                && calls[instruction.operand].local_slot - 1 >= local_slot_count)
            { translation_error = "bytecode local slot out of range"; return false; }
            if (!calls[instruction.operand].arguments.empty()) state.methods.emplace_back(instruction.operand, 0);
            {
                size_t argument_count = 0;
                for (const auto& [site_index, received] : state.methods)
                {
                    const auto& site = calls[site_index];
                    const auto& name = names[site.name_id];
                    argument_count += name == "push_back" ? site.arguments.size()
                        : name == "insert" ? std::min<size_t>(2, site.arguments.size()) : 1;
                }
                instructions.method_argument_count = (std::max)(instructions.method_argument_count, argument_count);
            }
            ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::MethodValue:
            if (register_top < 2 || instruction.operand >= calls.size()
                || instruction.auxiliary >= calls[instruction.operand].arguments.size())
            { translation_error = "invalid method operand"; return false; }
            if (state.methods.empty() || state.methods.back() != std::pair<size_t, size_t>{instruction.operand, instruction.auxiliary})
            { translation_error = "bytecode method frame mismatch"; return false; }
            {
                const auto& site = calls[instruction.operand];
                const auto& name = names[site.name_id];
                const size_t count = name == "push_back" ? site.arguments.size()
                    : name == "insert" ? std::min<size_t>(2, site.arguments.size()) : 1;
                if (++state.methods.back().second == count) state.methods.pop_back();
            }
            --register_top;
            break;
        case Opcode::MethodPush:
            if (register_top == 0 || instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 1)
            { translation_error = "invalid bytecode method operand"; return false; }
            break;
        case Opcode::BindArgument:
            if (register_top == 0 || instruction.auxiliary >= calls.size()
                || instruction.operand >= calls[instruction.auxiliary].arguments.size())
            { translation_error = "invalid bytecode argument binding"; return false; }
            break;
        case Opcode::RangeBegin:
            if (instruction.operand != pc)
            { translation_error = "invalid bytecode range identifier"; return false; }
            if (register_top == 0) { translation_error = "bytecode range register underflow"; return false; }
            if (std::find(state.ranges.begin(), state.ranges.end(), pc) != state.ranges.end())
            { translation_error = "bytecode range frame mismatch"; return false; }
            state.ranges.push_back(pc);
            instructions.range_count = (std::max)(instructions.range_count, state.ranges.size());
            --register_top;
            break;
        case Opcode::RangeEnd:
        case Opcode::RangeNext:
            if (instruction.operand >= pc || instructions.code[instruction.operand].opcode != Opcode::RangeBegin)
            { translation_error = "invalid bytecode range identifier"; return false; }
            if (state.ranges.empty() || state.ranges.back() != instruction.operand)
            { translation_error = "bytecode range frame mismatch"; return false; }
            if (instruction.opcode == Opcode::RangeEnd) state.ranges.pop_back();
            if (instruction.opcode == Opcode::RangeNext) ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::Index:
            if (instruction.operand == 0 || register_top < instruction.operand)
            { translation_error = "invalid bytecode index stack"; return false; }
            if (instruction.auxiliary >= index_sites.size()
                || index_sites[instruction.auxiliary].dimensions != instruction.operand
                || index_sites[instruction.auxiliary].name_id >= names.size()
                || index_sites[instruction.auxiliary].type_id >= names.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            register_top = register_top - instruction.operand + 1;
            break;
        case Opcode::Array:
            if (register_top < instruction.operand) { translation_error = "invalid bytecode array stack"; return false; }
            register_top = register_top - instruction.operand + 1;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::CallBegin:
            if (instruction.operand >= calls.size()) { translation_error = "invalid bytecode call index"; return false; }
            if (calls[instruction.operand].name_id >= names.size())
            { translation_error = "invalid bytecode name index"; return false; }
            state.calls.emplace_back(instruction.operand, false);
            state.diagnostics.emplace_back(instruction.source.id, true);
            break;
        case Opcode::CallEnd:
            if (state.calls.empty() || state.calls.back() != std::pair<size_t, bool>{instruction.operand, true})
            { translation_error = "bytecode call frame mismatch"; return false; }
            state.calls.pop_back();
            if (state.diagnostics.empty() || state.diagnostics.back() != std::pair<size_t, bool>{instruction.source.id, true})
            { translation_error = "bytecode diagnostic frame mismatch"; return false; }
            state.diagnostics.pop_back();
            break;
        case Opcode::Call:
            if (instruction.operand >= calls.size() || register_top < calls[instruction.operand].arguments.size())
            { translation_error = "invalid bytecode call stack"; return false; }
            if (state.calls.empty() || state.calls.back() != std::pair<size_t, bool>{instruction.operand, false})
            { translation_error = "bytecode call frame mismatch"; return false; }
            state.calls.back().second = true;
            register_top = register_top - calls[instruction.operand].arguments.size() + 1;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::SwitchMark:
            if (register_top == 0) { translation_error = "bytecode switch register underflow"; return false; }
            if (instruction.operand >= pc || instructions.code[instruction.operand].opcode != Opcode::LoopMark
                || std::find(state.switches.begin(), state.switches.end(), instruction.operand) != state.switches.end())
            { translation_error = "bytecode switch frame mismatch"; return false; }
            state.switches.push_back(instruction.operand);
            instructions.switch_count = (std::max)(instructions.switch_count, state.switches.size());
            --register_top;
            break;
        case Opcode::SwitchCase:
        case Opcode::SwitchDefault:
            if (state.switches.empty() || state.switches.back() != instruction.operand || instruction.auxiliary > 1)
            { translation_error = "bytecode switch frame mismatch"; return false; }
            if (instruction.opcode == Opcode::SwitchCase)
            {
                ++register_top;
                if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            }
            else if (instruction.auxiliary != 0)
            {
                if (register_top == 0) { translation_error = "bytecode case register underflow"; return false; }
            }
            break;
        case Opcode::SwitchEnd:
            if (state.switches.empty() || state.switches.back() != instruction.operand)
            { translation_error = "bytecode switch frame mismatch"; return false; }
            state.switches.pop_back();
            break;
        case Opcode::LoopMark: break;
        case Opcode::Unwind:
            if (instruction.operand >= instructions.code.size() || !states[instruction.operand].visited
                || instructions.code[instruction.operand].opcode != Opcode::LoopMark)
            {
                translation_error = "invalid bytecode loop unwind target";
                return false;
            }
            state = states[instruction.operand];
            break;
        case Opcode::PrepareStore:
            if (instruction.operand != 0 && instruction.operand - 1 >= index_sites.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            if (instruction.operand != 0 && register_top < index_sites[instruction.operand - 1].dimensions)
            { translation_error = "bytecode store index underflow"; return false; }
            break;
        case Opcode::Store: case Opcode::StoreLocal:
            if (instruction.opcode == Opcode::Store && instruction.operand != 0 && instruction.operand - 1 >= index_sites.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            if (register_top < (instruction.opcode == Opcode::Store && instruction.operand != 0
                ? index_sites[instruction.operand - 1].dimensions : 0) + 1)
            { translation_error = "bytecode store operand underflow"; return false; }
            if (instruction.opcode == Opcode::Store && instruction.operand != 0)
                register_top -= index_sites[instruction.operand - 1].dimensions;
            break;
        case Opcode::Increment: case Opcode::IncrementLocal:
            if (instruction.opcode == Opcode::Increment && instruction.operand != 0 && instruction.operand - 1 >= index_sites.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            if (instruction.opcode == Opcode::IncrementLocal)
            {
                ++register_top;
                if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
                break;
            }
            if (instruction.operand != 0)
            {
                if (register_top < index_sites[instruction.operand - 1].dimensions)
                { translation_error = "bytecode increment index underflow"; return false; }
                register_top -= index_sites[instruction.operand - 1].dimensions - 1;
            }
            else ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::ScopeEnter:
            if (instruction.auxiliary != 0 && instruction.auxiliary - 1 > local_slot_count)
            { translation_error = "bytecode scope slot base out of range"; return false; }
            if (instruction.operand > local_slot_count)
            { translation_error = "bytecode scope binding count out of range"; return false; }
            state.local_scope_bases.push_back(instruction.auxiliary);
            ++state.scopes;
            break;
        case Opcode::ScopeLeave:
            if (state.scopes == 0)
            {
                translation_error = "bytecode scope underflow";
                return false;
            }
            --state.scopes;
            if (state.local_scope_bases.back() != instruction.auxiliary)
            { translation_error = "bytecode scope slot base mismatch"; return false; }
            state.local_scope_bases.pop_back();
            break;
        case Opcode::Jump:
            if (!merge(instruction.operand, state)) return false;
            continue;
        case Opcode::Branch: case Opcode::AndBranch: case Opcode::OrBranch:
            if (register_top == 0)
            {
                translation_error = "bytecode branch register underflow";
                return false;
            }
            if (instruction.opcode == Opcode::Branch) --register_top;
            if (instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch)
            {
                auto target_state = state;
                if (instruction.discard_result && target_state.register_top != 0) --target_state.register_top;
                if (!merge(instruction.operand, target_state)) return false;
            }
            else if (!merge(instruction.operand, state)) return false;
            break;
        case Opcode::Enter:
            frames.push_back(instruction.source.id);
            state.diagnostics.emplace_back(instruction.source.id, false);
            break;
        case Opcode::Leave:
            if (frames.empty() || frames.back() != instruction.source.id)
            {
                translation_error = "bytecode diagnostic frame mismatch";
                return false;
            }
            frames.pop_back();
            if (state.diagnostics.empty() || state.diagnostics.back() != std::pair<size_t, bool>{instruction.source.id, false})
            { translation_error = "bytecode diagnostic frame mismatch"; return false; }
            state.diagnostics.pop_back();
            break;
        case Opcode::Constant:
            if (instruction.operand >= constants.size())
            {
                translation_error = "bytecode constant index out of range";
                return false;
            }
            [[fallthrough]];
        case Opcode::Load: case Opcode::LoadLocal: case Opcode::DeclareLocal: case Opcode::Empty: case Opcode::Peek: case Opcode::MethodNoArgs: case Opcode::Member:
            ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::Return:
            if (register_top == 0 || !state.calls.empty() || !state.methods.empty())
            { translation_error = "invalid bytecode return state"; return false; }
            instruction.input_offset = instructions.register_inputs.size();
            instruction.input_count = 1;
            instructions.register_inputs.push_back(register_top - 1);
            continue;
        case Opcode::Positive: case Opcode::Negative: case Opcode::LogicalNot: case Opcode::BitNot: case Opcode::Cast: case Opcode::Size:
            if (instruction.opcode == Opcode::Size && instruction.auxiliary == 1)
            {
                if (instruction.operand >= names.size())
                { translation_error = "invalid bytecode size name"; return false; }
                ++register_top;
                if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
                break;
            }
            if (register_top == 0)
            {
                translation_error = "bytecode unary operand register underflow";
                return false;
            }
            break;
        case Opcode::MathUnary:
            if (register_top == 0 || instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 1)
            { translation_error = "invalid bytecode unary math operand"; return false; }
            break;
        case Opcode::MathBinary:
            if (register_top < 2 || instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 2)
            { translation_error = "invalid bytecode binary math operand"; return false; }
            --register_top;
            break;
        case Opcode::Add: case Opcode::Subtract: case Opcode::Multiply:
        case Opcode::Divide: case Opcode::Modulo: case Opcode::Less: case Opcode::Greater:
        case Opcode::LessEqual: case Opcode::GreaterEqual: case Opcode::Equal: case Opcode::NotEqual:
        case Opcode::BitAnd: case Opcode::BitOr: case Opcode::BitXor: case Opcode::ShiftLeft: case Opcode::ShiftRight:
        case Opcode::LogicalAnd: case Opcode::LogicalOr:
            if ((instruction.opcode == Opcode::LogicalAnd || instruction.opcode == Opcode::LogicalOr)
                && instruction.discard_result && register_top == 1)
                break;
            if (register_top < 2)
            {
                translation_error = "bytecode operand register underflow";
                return false;
            }
            --register_top;
            break;
        default:
            translation_error = "invalid bytecode opcode";
            return false;
        }
        const auto input_count = [&]() -> size_t
        {
            switch (instruction.opcode)
            {
            case Opcode::MethodPush: case Opcode::MethodValue: case Opcode::BindArgument:
            case Opcode::RangeBegin: case Opcode::SwitchMark: case Opcode::Return:
            case Opcode::Branch: case Opcode::AndBranch: case Opcode::OrBranch:
            case Opcode::StoreLocal: case Opcode::MathUnary:
            case Opcode::Positive: case Opcode::Negative: case Opcode::LogicalNot:
            case Opcode::BitNot: case Opcode::Cast:
                return 1;
            case Opcode::Index:
                return instruction.operand;
            case Opcode::PrepareStore:
                return instruction.operand == 0 ? 0 : index_sites[instruction.operand - 1].dimensions;
            case Opcode::Array:
                return instruction.operand;
            case Opcode::Call:
                return calls[instruction.operand].arguments.size();
            case Opcode::Store:
                return instruction.operand == 0 ? 1 : index_sites[instruction.operand - 1].dimensions + 1;
            case Opcode::Increment:
                return instruction.operand == 0 ? 0 : index_sites[instruction.operand - 1].dimensions;
            case Opcode::SwitchDefault:
                return instruction.auxiliary == 0 ? 0 : 1;
            case Opcode::MathBinary:
            case Opcode::Add: case Opcode::Subtract: case Opcode::Multiply:
            case Opcode::Divide: case Opcode::Modulo: case Opcode::Less: case Opcode::Greater:
            case Opcode::LessEqual: case Opcode::GreaterEqual: case Opcode::Equal: case Opcode::NotEqual:
            case Opcode::BitAnd: case Opcode::BitOr: case Opcode::BitXor: case Opcode::ShiftLeft: case Opcode::ShiftRight:
                return 2;
            case Opcode::LogicalAnd: case Opcode::LogicalOr:
                return instruction.discard_result && input_top == 1 ? 1 : 2;
            case Opcode::Size:
                return instruction.auxiliary == 1 ? 0 : 1;
            default:
                return 0;
            }
        }();
        if (input_count > input_top)
        { translation_error = "bytecode input register underflow"; return false; }
        instruction.input_offset = instructions.register_inputs.size();
        instruction.input_count = input_count;
        for (size_t slot = input_top - input_count; slot < input_top; ++slot)
            instructions.register_inputs.push_back(slot);
        instruction.destination = register_top == 0 ? 0 : register_top - 1;
        if (instruction.discard_result && register_top != 0) --register_top;
        if (register_top > std::numeric_limits<std::uint32_t>::max())
        { translation_error = "bytecode register count exceeds encoding range"; return false; }
        if (!merge(pc + 1, state)) return false;
    }
    if (states.back().visited && (states.back().register_top > 1 || !states.back().frames.empty() || states.back().scopes != 0
        || !states.back().calls.empty() || !states.back().methods.empty() || !states.back().ranges.empty() || !states.back().switches.empty()))
    {
        translation_error = "bytecode expression has an invalid final register state";
        return false;
    }
    return true;
}

CifaBytecode::CifaBytecode() : session(std::make_unique<Session>(*this))
{
    register_function("run_string", [this](ObjectVector& arguments) -> Object
        {
            return arguments.empty() ? Object() : run_script(arguments[0].toString());
        });
    register_function("run_file", [this](ObjectVector& arguments) -> Object
        {
            return arguments.empty() ? Object() : run_file(arguments[0].toString());
        });
}

CifaBytecode::~CifaBytecode() = default;

void CifaBytecode::translate(Cifa& compiler, size_t script_function_version)
{
    module_data->int_type_id = intern_name("int");
    module_data->double_type_id = intern_name("double");
    source_lines.reserve(compiler.compilation_source_line_infos.size());
    for (auto& line : compiler.compilation_source_line_infos)
        source_lines.push_back({std::move(line.filename), line.line, std::move(line.text)});
    module_data->structures = compiler.compilation_struct_defs;
    compiled_valid = compiler.compiled && !compiler.compile_failed;
    module_data->host_function_version = optimization_enabled ? compiler.function_version : 0;
    module_data->script_function_version = optimization_enabled ? script_function_version : 0;
    module_data->freeze_script_functions = optimization_enabled;
    if (compiled_valid)
    {
        compile_script_functions = &compiler.compilation_functions;
        compile_allows_script_constant_folding = optimization_enabled;
        const size_t mark = root_instructions.code.size();
        root_instructions.code.push_back({Opcode::LoopMark, source_ref(&compiler.compilation_root)});
        compile_blocks.push_back({mark, {}, {}});
        for (const auto& child : compiler.compilation_root.v)
            if (child.type == CalUnitType::Label) compile_blocks.back().targets[child.str] = 0;
        bool has_root_statement = false;
        for (auto& child : compiler.compilation_root.v)
        {
            root_entries.push_back(root_instructions.code.size());
            if (child.type == CalUnitType::Label)
            {
                entry_labels[child.str] = root_entries.size() - 1;
                compile_blocks.back().targets[child.str] = root_instructions.code.size();
                continue;
            }
            if (has_root_statement) discard_statement_result(root_instructions.code, 0);
            emit_statement(child, root_instructions.code);
            has_root_statement = true;
        }
        for (const auto& jump : compile_blocks.back().jumps)
            root_instructions.code[jump.first].operand = compile_blocks.back().targets.at(jump.second);
        compile_blocks.pop_back();
        seal(root_instructions);
        root_source = source_ref(&compiler.compilation_root);
        seal_calls();
        if (translation_error.empty() && verify(root_instructions))
        {
            const auto remap = compact(root_instructions);
            for (auto& entry : root_entries) entry = remap[entry];
        }
        for (const auto& [name, overloads] : compiler.compilation_functions)
        {
            for (const auto& [arity, definition] : overloads)
            {
                auto compiled = std::make_shared<FunctionCode>();
                compiled->name = name;
                for (const auto& parameter : definition.arguments)
                    compiled->parameters.push_back({parameter.name, parameter.type_name});
                compiled->return_type = definition.return_type;
                compiled->body_source = source_ref(&definition.body);
                auto* saved_function = compiling_function;
                auto saved_local_scopes = std::move(compile_local_scopes);
                auto saved_local_scope_bases = std::move(compile_local_scope_bases);
                compiling_function = compiled.get();
                compile_local_scopes.emplace_back();
                for (const auto& parameter : compiled->parameters)
                {
                    const size_t slot = compiled->local_slot_count++;
                    compile_local_scopes.back().emplace(parameter.name, slot);
                }
                emit(const_cast<CalUnit&>(definition.body), compiled->instructions.code);
                seal(compiled->instructions);
                seal_calls();
                if (translation_error.empty() && verify(compiled->instructions, compiled->local_slot_count))
                    compact(compiled->instructions);
                compiling_function = saved_function;
                compile_local_scopes = std::move(saved_local_scopes);
                compile_local_scope_bases = std::move(saved_local_scope_bases);
                function_code[name][arity] = std::move(compiled);
            }
        }
        compile_script_functions = nullptr;
        compile_allows_script_constant_folding = false;
    }
    source_ids.clear();
    compile_sources.clear();
}

bool CifaBytecode::execute_instructions(Machine& machine, const Module& module, const Instructions& instructions,
    Object& result, size_t start)
{
#ifdef CIFA_VM_PROFILE
    struct VmProfile
    {
        struct Timer
        {
            std::uint64_t& elapsed_ns;
            std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
            explicit Timer(std::uint64_t& value) : elapsed_ns(value) {}
            ~Timer()
            {
                elapsed_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count());
            }
        };
        struct PathTimer
        {
            std::uint64_t& total_ns;
            std::uint64_t* path_ns;
            std::uint64_t* path_counts;
            size_t path = 0;
            std::chrono::steady_clock::time_point started = std::chrono::steady_clock::now();
            PathTimer(std::uint64_t& total, std::uint64_t* elapsed, std::uint64_t* counts)
                : total_ns(total), path_ns(elapsed), path_counts(counts) {}
            void select(size_t value) { path = value; }
            ~PathTimer()
            {
                const auto elapsed = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - started).count());
                total_ns += elapsed;
                path_ns[path] += elapsed;
                ++path_counts[path];
            }
        };
        std::array<std::uint64_t, 80> opcodes{};
        std::uint64_t register_binary_fast = 0;
        std::uint64_t register_binary_fallback = 0;
        std::uint64_t script_calls = 0;
        std::uint64_t host_calls = 0;
        std::uint64_t array_get_fast = 0;
        std::uint64_t array_set_fast = 0;
        std::uint64_t method_push = 0;
        std::uint64_t host_prepare_ns = 0;
        std::uint64_t host_invoke_ns = 0;
        std::uint64_t script_setup_ns = 0;
        std::uint64_t register_binary_ns = 0;
        std::array<std::uint64_t, 7> register_binary_path_ns{};
        std::array<std::uint64_t, 7> register_binary_path_counts{};
        std::uint64_t register_binary_numeric_discard = 0;
        std::uint64_t register_binary_numeric_result = 0;
        std::uint64_t register_binary_numeric_initialize = 0;
        std::uint64_t register_binary_numeric_assign = 0;
        std::uint64_t register_binary_numeric_int = 0;
        std::uint64_t register_binary_numeric_double = 0;
        std::uint64_t local_load_ns = 0;
        std::uint64_t local_store_ns = 0;
        std::uint64_t local_store_fast_ns = 0;
        std::uint64_t local_store_value_ns = 0;
        std::uint64_t local_store_assign_ns = 0;
        std::uint64_t local_store_bind_ns = 0;
        std::uint64_t local_store_result_ns = 0;
        std::array<std::uint64_t, 8> local_store_path_ns{};
        std::array<std::uint64_t, 8> local_store_path_counts{};
        std::uint64_t increment_local_ns = 0;
        std::array<std::uint64_t, 3> increment_local_path_ns{};
        std::array<std::uint64_t, 3> increment_local_path_counts{};
        std::uint64_t register_snapshot_ns = 0;
        std::array<std::uint64_t, 3> register_snapshot_path_ns{};
        std::array<std::uint64_t, 3> register_snapshot_path_counts{};
        std::uint64_t method_push_lookup_ns = 0;
        std::uint64_t method_push_prepare_ns = 0;
        std::uint64_t method_push_convert_ns = 0;
        std::uint64_t method_push_append_ns = 0;
        std::uint64_t method_push_result_ns = 0;
        std::uint64_t method_push_grow_ns = 0;
        std::uint64_t method_push_stable_ns = 0;
        std::uint64_t method_push_grow_count = 0;
        std::uint64_t method_push_stable_count = 0;
        std::uint64_t constant_ns = 0;
        std::uint64_t scope_enter_ns = 0;
        std::uint64_t scope_release_ns = 0;
        std::uint64_t local_store_fast = 0;
        std::uint64_t local_store_fallback = 0;
        std::array<std::uint64_t, 7> local_store_reasons{};
        std::array<std::uint64_t, 5> local_store_rhs{};
        std::array<std::uint64_t, 3> local_store_declarations{};
        std::uint64_t local_store_rebind = 0;
        std::uint64_t local_store_direct_bind = 0;
        std::uint64_t local_store_skip_bind = 0;
        std::uint64_t branch_ns = 0;
        std::uint64_t array_get_fast_ns = 0;
        std::uint64_t method_push_ns = 0;
        std::uint64_t execution_ns = 0;
        std::uint64_t scope_enter_with_slots = 0;
        std::uint64_t scope_enter_without_slots = 0;
        std::uint64_t scope_releases = 0;
        std::uint64_t global_link_hits = 0;
        std::uint64_t global_link_lookups = 0;
        std::uint64_t global_link_creates = 0;
        std::array<std::uint64_t, 3> alias_read_paths{};
        std::array<std::uint64_t, 11> instruction_fields{};
        std::unordered_map<std::string, std::uint64_t> host_call_names;
        std::unordered_map<std::string, std::uint64_t> script_call_names;
        ~VmProfile()
        {
            constexpr const char* reasons[] = { "compound", "declaration", "alias", "target_special", "target_binding", "rhs_double_to_int", "rhs_other" };
            constexpr const char* rhs_types[] = { "int", "double", "bool", "array", "other" };
            constexpr const char* declaration_types[] = { "int", "double", "other" };
            constexpr const char* store_paths[] = { "other", "scoped_alias", "typed_numeric", "compound_local",
                "numeric_assign", "typed_assign", "dynamic_assign", "global_alias" };
            constexpr const char* increment_paths[] = { "integer_fast", "slot", "object" };
            constexpr const char* snapshot_paths[] = { "constant", "local", "alias" };
            constexpr const char* binary_paths[] = { "temporary_result", "temporary_numeric_assign", "temporary_generic_assign",
                "local_result", "local_numeric_assign", "local_generic_assign", "fallback" };
            std::uint64_t classified = 0;
            for (size_t reason = 0; reason < local_store_reasons.size(); ++reason)
            {
                classified += local_store_reasons[reason];
                std::cerr << "VM_PROFILE store_reason=" << reasons[reason] << " count=" << local_store_reasons[reason] << '\n';
            }
            for (size_t kind = 0; kind < local_store_rhs.size(); ++kind)
                std::cerr << "VM_PROFILE store_fallback_rhs=" << rhs_types[kind] << " count=" << local_store_rhs[kind] << '\n';
            for (size_t kind = 0; kind < local_store_declarations.size(); ++kind)
                std::cerr << "VM_PROFILE store_declaration=" << declaration_types[kind] << " count=" << local_store_declarations[kind] << '\n';
            for (size_t path = 0; path < local_store_path_ns.size(); ++path)
                std::cerr << "VM_PROFILE store_path=" << store_paths[path] << " count=" << local_store_path_counts[path]
                    << " ms=" << local_store_path_ns[path] / 1'000'000.0 << '\n';
            for (size_t path = 0; path < increment_local_path_ns.size(); ++path)
                std::cerr << "VM_PROFILE increment_path=" << increment_paths[path] << " count=" << increment_local_path_counts[path]
                    << " ms=" << increment_local_path_ns[path] / 1'000'000.0 << '\n';
            for (size_t path = 0; path < register_snapshot_path_ns.size(); ++path)
                std::cerr << "VM_PROFILE snapshot_path=" << snapshot_paths[path] << " count=" << register_snapshot_path_counts[path]
                    << " ms=" << register_snapshot_path_ns[path] / 1'000'000.0 << '\n';
            for (size_t path = 0; path < register_binary_path_ns.size(); ++path)
                std::cerr << "VM_PROFILE binary_path=" << binary_paths[path] << " count=" << register_binary_path_counts[path]
                    << " ms=" << register_binary_path_ns[path] / 1'000'000.0 << '\n';
            std::cerr << "VM_PROFILE binary_numeric_discard=" << register_binary_numeric_discard
                << " binary_numeric_result=" << register_binary_numeric_result
                << " binary_numeric_initialize=" << register_binary_numeric_initialize
                << " binary_numeric_assign=" << register_binary_numeric_assign
                << " binary_numeric_int=" << register_binary_numeric_int
                << " binary_numeric_double=" << register_binary_numeric_double << '\n';
            std::cerr << "VM_PROFILE store_classified=" << classified << " store_rebind=" << local_store_rebind
                << " store_direct_bind=" << local_store_direct_bind
                << " store_skip_bind=" << local_store_skip_bind << '\n';
            std::cerr << "VM_PROFILE register_binary_fast=" << register_binary_fast
                << " register_binary_fallback=" << register_binary_fallback
                << " script_calls=" << script_calls << " host_calls=" << host_calls
                << " array_get_fast=" << array_get_fast << " array_set_fast=" << array_set_fast
                << " method_push=" << method_push << " host_prepare_ms=" << host_prepare_ns / 1'000'000.0
                << " host_invoke_ms=" << host_invoke_ns / 1'000'000.0
                << " script_setup_ms=" << script_setup_ns / 1'000'000.0
                << " register_binary_ms=" << register_binary_ns / 1'000'000.0
                << " local_load_ms=" << local_load_ns / 1'000'000.0
                << " local_store_ms=" << local_store_ns / 1'000'000.0
                << " local_store_fast_ms=" << local_store_fast_ns / 1'000'000.0
                << " local_store_value_ms=" << local_store_value_ns / 1'000'000.0
                << " local_store_assign_ms=" << local_store_assign_ns / 1'000'000.0
                << " local_store_bind_ms=" << local_store_bind_ns / 1'000'000.0
                << " local_store_result_ms=" << local_store_result_ns / 1'000'000.0
                << " increment_local_ms=" << increment_local_ns / 1'000'000.0
                << " register_snapshot_ms=" << register_snapshot_ns / 1'000'000.0
                << " constant_ms=" << constant_ns / 1'000'000.0
                << " scope_enter_ms=" << scope_enter_ns / 1'000'000.0
                << " scope_release_ms=" << scope_release_ns / 1'000'000.0
                << " local_store_fast=" << local_store_fast
                << " local_store_fallback=" << local_store_fallback
                << " branch_ms=" << branch_ns / 1'000'000.0
                << " array_get_fast_ms=" << array_get_fast_ns / 1'000'000.0
                << " method_push_ms=" << method_push_ns / 1'000'000.0
                << " method_push_lookup_ms=" << method_push_lookup_ns / 1'000'000.0
                << " method_push_prepare_ms=" << method_push_prepare_ns / 1'000'000.0
                << " method_push_convert_ms=" << method_push_convert_ns / 1'000'000.0
                << " method_push_append_ms=" << method_push_append_ns / 1'000'000.0
                << " method_push_result_ms=" << method_push_result_ns / 1'000'000.0
                << " method_push_grow_ms=" << method_push_grow_ns / 1'000'000.0
                << " method_push_stable_ms=" << method_push_stable_ns / 1'000'000.0
                << " method_push_grow_count=" << method_push_grow_count
                << " method_push_stable_count=" << method_push_stable_count
                << " execution_ms=" << execution_ns / 1'000'000.0
                << " scope_enter_with_slots=" << scope_enter_with_slots
                << " scope_enter_without_slots=" << scope_enter_without_slots
                << " scope_releases=" << scope_releases
                << " global_link_hits=" << global_link_hits
                << " global_link_lookups=" << global_link_lookups
                << " global_link_creates=" << global_link_creates
                << " alias_read_local=" << alias_read_paths[0]
                << " alias_read_global=" << alias_read_paths[1]
                << " alias_read_dynamic=" << alias_read_paths[2] << '\n';
            std::cerr << "VM_PROFILE instruction_size=" << sizeof(Instruction)
                << " source=" << instruction_fields[0]
                << " operand=" << instruction_fields[1]
                << " auxiliary=" << instruction_fields[2]
                << " member_site=" << instruction_fields[3]
                << " write=" << instruction_fields[4]
                << " variable_site=" << instruction_fields[5]
                << " condition_source=" << instruction_fields[6]
                << " target_source=" << instruction_fields[7]
                << " destination=" << instruction_fields[8]
                << " inputs=" << instruction_fields[9]
                << " discard=" << instruction_fields[10] << '\n';
            for (size_t opcode = 0; opcode < opcodes.size(); ++opcode)
                if (opcodes[opcode] != 0) std::cerr << "VM_PROFILE opcode=" << opcode << " count=" << opcodes[opcode] << '\n';
            for (const auto& [name, count] : host_call_names)
                std::cerr << "VM_PROFILE host=" << name << " count=" << count << '\n';
            for (const auto& [name, count] : script_call_names)
                std::cerr << "VM_PROFILE script=" << name << " count=" << count << '\n';
        }
    } profile;
    VmProfile::Timer execution_timer(profile.execution_ns);
#endif
    auto& interpreter = machine.host;
    auto& scopes = machine.scopes;
    scopes.reserve(scopes.size() + instructions.scope_capacity);
    auto& registers = machine.registers;
    struct RestoreRegisters
    {
        RegisterSlots& registers;
        size_t base;
        size_t size;
        size_t top;
        ~RestoreRegisters() { registers.restore(base, size, top); }
    } restore_registers{registers, registers.base(), registers.size(), registers.top()};
    registers.enter(instructions.register_capacity + instructions.temporary_count + instructions.switch_count
        + instructions.range_count + instructions.method_argument_count + 5);
    const size_t result_base = registers.base();
    ++registers.window_base;
    --registers.window_size;
    const size_t scope_base = scopes.size();
    struct RestoreScopes
    {
        ScopeStack& scopes;
        size_t size;
        ~RestoreScopes() { scopes.resize(size); }
    } restore_scopes{scopes, scope_base};
    std::vector<Scope> reusable_scopes;
    auto release_scope = [&]()
    {
#ifdef CIFA_VM_PROFILE
        VmProfile::Timer scope_release_timer(profile.scope_release_ns);
        ++profile.scope_releases;
#endif
        if (!scopes.back().dynamic_registers)
        {
            scopes.back().bindings.clear();
            reusable_scopes.push_back(std::move(scopes.back()));
        }
        scopes.pop_back();
    };
    auto release_scopes_to = [&](size_t size)
    {
        while (scopes.size() > size) release_scope();
    };
    struct LoopState { size_t scopes; size_t local_scope_bases; std::vector<size_t> ranges; std::vector<size_t> switches; };
    std::vector<std::optional<LoopState>> loop_states(instructions.loop_state_count);
    std::vector<size_t> local_scope_bases;
    if (start != 0 && !loop_states.empty()) loop_states[0] = LoopState{scopes.size(), local_scope_bases.size()};
    struct SwitchState { size_t condition_slot = 0; bool active = false; };
    std::unordered_map<size_t, SwitchState> switches;
    struct RangeState { size_t snapshot_slot = 0; size_t index = 0; };
    std::unordered_map<size_t, RangeState> ranges;
    struct MethodArguments { size_t base = 0; size_t count = 0; size_t received = 0; };
    std::unordered_map<size_t, MethodArguments> method_arguments;
    size_t method_argument_top = 0;
    struct RangeBinding { std::string name; size_t value_slot = 0; size_t slot = 0; };
    std::optional<RangeBinding> range_binding;
    std::deque<RegisterSlots> local_windows;
    local_windows.emplace_back(registers, 0, 0);
    struct PendingCall
    {
        RegisterSlots* parameters = nullptr;
        size_t caller_top = 0;
        size_t call_site = 0;
    };
    std::vector<PendingCall> pending_calls;
    struct Frame
    {
        const Instructions* instructions;
        const SourceLocation* node;
        size_t pc;
        size_t call_pc;
        size_t return_register;
        RegisterSlots* locals;
        ScopeStack scopes;
        std::vector<size_t> local_scope_bases;
        std::vector<std::optional<LoopState>> loops;
        std::unordered_map<size_t, SwitchState> switches;
        const FunctionCode* function;
        const SourceLocation* call;
        std::unordered_map<size_t, RangeState> ranges;
        std::unordered_map<size_t, MethodArguments> method_arguments;
        size_t method_argument_top;
        const Module* owner;
        std::shared_ptr<const Module> module;
        std::vector<bool> aliases;
        size_t register_base;
        size_t register_size;
        size_t register_top;
        std::vector<PendingCall> pending_calls;
    };
    std::deque<Frame> frames;
    struct RestoreCaller
    {
        std::deque<Frame>& frames;
        ScopeStack& scopes;
        ~RestoreCaller()
        {
            if (!frames.empty()) scopes = std::move(frames.front().scopes);
        }
    } restore_caller{frames, scopes};
    const Module* active_owner = &module;
    std::unordered_map<const Module*, std::vector<Object*>> global_links;
    auto link_globals = [&](const Module* owner) -> std::vector<Object*>&
    {
        auto [entry, inserted] = global_links.try_emplace(owner);
        if (inserted) entry->second.resize(owner->names.size());
        return entry->second;
    };
    auto* active_globals = &link_globals(active_owner);
    auto linked_global = [&](size_t name_id) -> Object*
    {
        auto*& linked = (*active_globals)[name_id];
#ifdef CIFA_VM_PROFILE
        if (linked) ++profile.global_link_hits;
        else ++profile.global_link_lookups;
#endif
        if (!linked)
        {
            const auto global = interpreter.global_variables.find(active_owner->names[name_id]);
            if (global != interpreter.global_variables.end()) linked = &global->second;
        }
        return linked;
    };
    auto alias_target = [&](size_t name_id) -> Object&
    {
        const auto& name = active_owner->names[name_id];
        if (auto* linked = linked_global(name_id)) return *linked;
    #ifdef CIFA_VM_PROFILE
        ++profile.global_link_creates;
    #endif
        auto& value = interpreter.global_variables[name];
        (*active_globals)[name_id] = &value;
        return value;
    };
    auto read_alias = [&](size_t destination, size_t name_id, const SourceLocation& location)
    {
        const auto& name = active_owner->names[name_id];
        if (auto* binding = machine.find_slot(name))
        {
#ifdef CIFA_VM_PROFILE
            ++profile.alias_read_paths[0];
#endif
            registers.copy(destination, *binding->file, binding->slot);
        }
        else
        {
            auto* linked = linked_global(name_id);
            if (linked)
            {
#ifdef CIFA_VM_PROFILE
                ++profile.alias_read_paths[1];
#endif
                registers.import_object(destination, *linked);
            }
            else
            {
#ifdef CIFA_VM_PROFILE
                ++profile.alias_read_paths[2];
#endif
                machine.read_named(registers, destination, name, "", false, false, false, location);
            }
        }
    };
    std::shared_ptr<const Module> active_module;
    const Instructions* active_instructions = &instructions;
    const SourceLocation* active_node = &module.source(module.root_source);
    const FunctionCode* active_function = nullptr;
    const SourceLocation* active_call = nullptr;
    const SourceLocation* current_source = active_node;
    size_t pc = start;
    SourceRef register_assignment_source;
    auto append_diagnostic_frames = [&](std::vector<std::pair<const SourceLocation*, bool>>& destination)
    {
        auto append = [&](const Module* owner, const Instructions* code, size_t instruction_pc, const SourceLocation* node)
        {
            if (owner == nullptr || code == nullptr || instruction_pc >= code->diagnostic_frames.size()) return;
            for (const auto& [source_id, function] : code->diagnostic_frames[instruction_pc])
            {
                const auto* source = &owner->source(SourceRef(source_id));
                if (!function && source == node) continue;
                destination.emplace_back(source, function);
                if (owner == active_owner && code == active_instructions && instruction_pc == pc - 1
                    && register_assignment_source.id == source_id) break;
            }
            if (instruction_pc < code->code.size() && code->code[instruction_pc].opcode == Opcode::CallBegin)
                destination.emplace_back(&owner->source(code->code[instruction_pc].source), true);
            if (instruction_pc < code->code.size() && code->code[instruction_pc].opcode == Opcode::Size)
                destination.emplace_back(&owner->source(code->code[instruction_pc].source), true);
        };
        for (const auto& frame : frames) append(frame.owner, frame.instructions, frame.call_pc, frame.node);
        append(active_owner, active_instructions, pc == 0 ? 0 : pc - 1, active_node);
    };
    struct RestoreDiagnosticFrames
    {
        Machine& machine;
        decltype(Machine::append_diagnostic_frames) previous;
        ~RestoreDiagnosticFrames() { machine.append_diagnostic_frames = std::move(previous); }
    } restore_diagnostic_frames{machine, std::move(machine.append_diagnostic_frames)};
    machine.append_diagnostic_frames = append_diagnostic_frames;
    Object::set_runtime_error_reporter([&machine, &current_source](const std::string& message, const Object* value)
        {
            if (value != nullptr && value->getSpecialType() == "NoValue") machine.set_no_value_error(*value, current_source);
            else machine.set_error(message, current_source);
        });
    struct RestoreObjectReporter
    {
        ~RestoreObjectReporter() { Object::clear_runtime_error_reporter(); }
    } restore_object_reporter;
    auto* active_locals = &local_windows.front();
    std::vector<bool> active_aliases;
    auto bind_local_storage = [&](const std::string& name, size_t slot, bool current_only)
    {
        active_aliases[slot] = true;
        for (size_t scope = scopes.size(); scope > 0; --scope)
        {
            auto& current = scopes[scope - 1];
            if (auto* found = current.find(name))
            {
                if (found->file != current.dynamic_registers.get())
                    active_aliases[slot] = found->file != active_locals || found->slot != slot;
                else
                {
                    active_locals->move(slot, *current.dynamic_registers, found->slot);
                    current.bind(name, active_locals, slot);
                    active_aliases[slot] = false;
                }
                return;
            }
            if (current_only) break;
        }
    };
    auto& return_states = machine.returns;
    const size_t return_base = return_states.size();
    struct RestoreReturns
    {
        decltype(return_states)& states;
        size_t size;
        ~RestoreReturns() { states.resize(size); }
    } restore_returns{return_states, return_base};
    RegisterSlots call_result(registers, result_base, 1);
    auto initialize_numeric = [&](size_t slot, RegisterSlots& source, size_t argument, const VariableSite& binding)
    {
        const auto numeric_binding = binding.type_id == active_owner->int_type_id ? RegisterSlots::NumericBinding::Int
            : binding.type_id == active_owner->double_type_id ? RegisterSlots::NumericBinding::Double
            : RegisterSlots::NumericBinding::None;
        if (!binding.with_type || scopes.empty() || numeric_binding == RegisterSlots::NumericBinding::None) return false;
        const auto& name = active_owner->names[binding.name_id];
        const auto* existing = scopes.back().find(name);
        if (existing && (existing->file != active_locals || existing->slot != slot)) return false;
        if (!active_locals->cast_numeric(slot, source, argument, numeric_binding)) return false;
        active_locals->bind_numeric(slot, numeric_binding);
        if (!existing) scopes.back().bind(name, active_locals, slot);
        active_aliases[slot] = false;
        return true;
    };
    auto finish_call = [&]()
    {
        if (active_function && call_result.empty(0))
        {
            call_result.write_payload(0, std::any(Object::NoValue{active_function->name, Machine::format_frame(*active_call)}));
            call_result.set_type(0, {typeid(void), "", "", "NoValue"});
        }
        auto saved = std::move(frames.back());
        frames.pop_back();
        return_states.pop_back();
        local_windows.pop_back();
        active_locals = saved.locals;
        active_aliases = std::move(saved.aliases);
        scopes = std::move(saved.scopes);
        local_scope_bases = std::move(saved.local_scope_bases);
        registers.restore(saved.register_base, saved.register_size, saved.register_top);
        registers.move(saved.return_register, call_result, 0);
        loop_states = std::move(saved.loops);
        switches = std::move(saved.switches);
        ranges = std::move(saved.ranges);
        method_arguments = std::move(saved.method_arguments);
        method_argument_top = saved.method_argument_top;
        pending_calls = std::move(saved.pending_calls);
        active_owner = saved.owner;
        active_globals = &link_globals(active_owner);
        active_module = std::move(saved.module);
        active_instructions = saved.instructions;
        active_node = saved.node;
        active_function = std::move(saved.function);
        active_call = saved.call;
        pc = saved.pc;
    };
    while (true)
    {
        if (pc >= active_instructions->code.size())
        {
            if (frames.empty()) break;
            call_result.clear(0);
            finish_call();
            continue;
        }
        const auto& instruction = active_instructions->code[pc++];
    #ifdef CIFA_VM_PROFILE
        ++profile.opcodes[static_cast<size_t>(instruction.opcode)];
        profile.instruction_fields[0] += instruction.source.id != 0;
        profile.instruction_fields[1] += instruction.operand != 0;
        profile.instruction_fields[2] += instruction.auxiliary != 0;
        profile.instruction_fields[3] += instruction.member_site != 0;
        profile.instruction_fields[4] += instruction.write != WriteOperation::Assign;
        profile.instruction_fields[5] += instruction.variable_site != 0;
        profile.instruction_fields[6] += instruction.condition_source.id != 0;
        profile.instruction_fields[7] += instruction.target_source.id != 0;
        profile.instruction_fields[8] += instruction.destination != 0;
        profile.instruction_fields[9] += instruction.input_count != 0;
        profile.instruction_fields[10] += instruction.discard_result;
    #endif
        const auto input_slot = [&](size_t index)
        {
            if (index >= instruction.input_count
                || instruction.input_offset + index >= active_instructions->register_inputs.size())
                return std::numeric_limits<size_t>::max();
            return active_instructions->register_inputs[instruction.input_offset + index];
        };
        const size_t output = instruction.destination;
        register_assignment_source = {};
        current_source = instruction.source.id != 0 ? &active_owner->source(instruction.source) : active_node;
        if (instruction.opcode == Opcode::Exit)
        {
            machine.exit_requested = true;
            result = Object();
            return true;
        }
        if (instruction.opcode == Opcode::Member)
        {
            registers.import_object(output, machine.resolve_member(active_owner->names[instruction.operand],
                active_owner->names[instruction.auxiliary]));
            if (machine.should_stop()) { result = Object(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::MethodPush)
        {
    #ifdef CIFA_VM_PROFILE
            VmProfile::Timer method_push_timer(profile.method_push_ns);
    #endif
    #ifdef CIFA_VM_PROFILE
            ++profile.method_push;
    #endif
            const auto& site = active_owner->calls[instruction.operand];
            auto receiver = machine.named_value(active_owner->names[site.base_name_id]);
            auto* container = receiver.resource();
            auto* array = receiver.file && container ? std::any_cast<VmArray>(container) : nullptr;
            auto* global_array = receiver.global && container ? std::any_cast<ObjectVector>(container) : nullptr;
            const auto& location = active_owner->source(site.method_source);
            const size_t argument = input_slot(instruction.input_count - 1);
#ifdef CIFA_VM_PROFILE
            auto method_push_stage = std::chrono::steady_clock::now();
            profile.method_push_lookup_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                method_push_stage - method_push_timer.started).count());
#endif
            if (!array && !global_array)
            {
                registers.clear(argument);
                machine.set_error("push_back() requires an array or map", &location);
                registers.clear(output);
            }
            else
            {
                if (global_array)
                {
                    Object value;
                    registers.export_argument(argument, value);
                    if (!receiver.element_type.empty()) value = machine.convert_type(value, receiver.element_type, location);
                    if (!machine.should_stop()) global_array->push_back(std::move(value));
                    registers.write_payload(output, double(global_array->size()));
                    continue;
                }
                auto& values = array->values;
                if (receiver.element_type.empty())
                {
                    const size_t argument_index = registers.base() + argument;
                    BytecodeValue::Storage numeric;
                    const auto& payload = registers.payload(argument, numeric);
                    if (registers.bindings[argument_index] == RegisterSlots::NumericBinding::None
                        && registers.slot_types[argument_index] == 0 && registers.slot_names[argument_index] == 0
                        && registers.origins[argument_index] == nullptr
                        && (value_holds<std::int64_t>(payload)
                            || value_holds<double>(payload) || value_holds<bool>(payload)))
                    {
#ifdef CIFA_VM_PROFILE
                        const auto append_start = std::chrono::steady_clock::now();
                        const bool grows = values.size() == values.capacity();
                        profile.method_push_prepare_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            append_start - method_push_stage).count());
#endif
                        values.push_back(std::move(registers.resource_payload(argument)));
#ifdef CIFA_VM_PROFILE
                        method_push_stage = std::chrono::steady_clock::now();
                        const auto append_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            method_push_stage - append_start).count());
                        profile.method_push_append_ns += append_ns;
                        if (grows) { profile.method_push_grow_ns += append_ns; ++profile.method_push_grow_count; }
                        else { profile.method_push_stable_ns += append_ns; ++profile.method_push_stable_count; }
#endif
                        registers.clear(argument);
                    }
                    else
                    {
#ifdef CIFA_VM_PROFILE
                        const auto append_start = std::chrono::steady_clock::now();
                        const bool grows = values.size() == values.capacity();
#endif
                        values.push_back(std::move(registers.resource_payload(argument)));
#ifdef CIFA_VM_PROFILE
                        method_push_stage = std::chrono::steady_clock::now();
                        const auto append_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            method_push_stage - append_start).count());
                        profile.method_push_append_ns += append_ns;
                        if (grows) { profile.method_push_grow_ns += append_ns; ++profile.method_push_grow_count; }
                        else { profile.method_push_stable_ns += append_ns; ++profile.method_push_stable_count; }
#endif
                        registers.clear(argument);
                    }
#ifdef CIFA_VM_PROFILE
                    const auto result_start = std::chrono::steady_clock::now();
                    profile.method_push_prepare_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        result_start - method_push_stage).count());
#endif
                    registers.write_payload(output, double(values.size()));
#ifdef CIFA_VM_PROFILE
                    profile.method_push_result_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now() - result_start).count());
#endif
                }
                else
                {
                    Object value;
                    registers.export_argument(argument, value);
#ifdef CIFA_VM_PROFILE
                    const auto convert_start = std::chrono::steady_clock::now();
                    profile.method_push_prepare_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        convert_start - method_push_stage).count());
#endif
                    value = machine.convert_type(value, receiver.element_type, location);
#ifdef CIFA_VM_PROFILE
                    const auto append_start = std::chrono::steady_clock::now();
                    const bool grows = values.size() == values.capacity();
                    profile.method_push_convert_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        append_start - convert_start).count());
#endif
                    if (!machine.should_stop())
                    {
                        values.emplace_back(std::move(value.value));
#ifdef CIFA_VM_PROFILE
                        const auto result_start = std::chrono::steady_clock::now();
                        const auto append_ns = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            result_start - append_start).count());
                        profile.method_push_append_ns += append_ns;
                        if (grows) { profile.method_push_grow_ns += append_ns; ++profile.method_push_grow_count; }
                        else { profile.method_push_stable_ns += append_ns; ++profile.method_push_stable_count; }
#endif
                        registers.write_payload(output, double(values.size()));
#ifdef CIFA_VM_PROFILE
                        profile.method_push_result_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                            std::chrono::steady_clock::now() - result_start).count());
#endif
                    }
                }
            }
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::MethodBegin || instruction.opcode == Opcode::MethodValue)
        {
            const auto& site = active_owner->calls[instruction.operand];
            const auto& method_name = active_owner->names[site.name_id];
            auto receiver = machine.named_value(active_owner->names[site.base_name_id]);
            std::vector<SourceLocation> arguments;
            RegisterSlots values(registers, registers.base() + active_instructions->register_capacity
                + active_instructions->temporary_count + active_instructions->switch_count
                + active_instructions->range_count + 1, 0);
            if (instruction.opcode == Opcode::MethodBegin && !site.arguments.empty())
            {
                const size_t count = method_name == "push_back" ? site.arguments.size()
                    : method_name == "insert" ? std::min<size_t>(2, site.arguments.size()) : 1;
                method_arguments[instruction.operand] = MethodArguments{method_argument_top, count, 0};
                method_argument_top += count;
                registers.clear(output);
                continue;
            }
            if (instruction.opcode == Opcode::MethodValue)
            {
                auto& pending = method_arguments[instruction.operand];
                const size_t argument = active_instructions->register_capacity + active_instructions->temporary_count
                    + active_instructions->switch_count + active_instructions->range_count
                    + 1 + pending.base + pending.received++;
                registers.move(argument, registers, input_slot(instruction.input_count - 1));
                if (method_name == "insert" && pending.received < 2)
                {
                    const auto size = receiver.size();
                    if (!size)
                    {
                        machine.set_error(method_name + "() requires an array or map",
                            &active_owner->source(site.method_source));
                        result = machine.error_result();
                        return true;
                    }
                    registers.write_payload(output, double(*size));
                    continue;
                }
                values = RegisterSlots(registers, registers.base() + active_instructions->register_capacity
                    + active_instructions->temporary_count + active_instructions->switch_count
                    + active_instructions->range_count + 1 + pending.base, pending.count);
                method_argument_top = pending.base;
                method_arguments.erase(instruction.operand);
                for (size_t index = 0; index < values.size(); ++index)
                    arguments.push_back(site.arguments[method_name == "insert" ? index : instruction.auxiliary]);
            }
            machine.call_method(registers, output, method_name, active_owner->source(site.method_source), receiver, arguments, values);
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            const size_t local_slot = active_owner->calls[instruction.operand].local_slot;
            if (local_slot != 0)
            {
                if (local_slot - 1 >= active_locals->size())
                {
                    machine.set_error("bytecode local slot out of range");
                    result = machine.error_result();
                    return true;
                }
            }
            continue;
        }
        if (instruction.opcode == Opcode::BindArgument)
        {
            const auto& call = active_owner->calls[instruction.auxiliary];
            const auto& call_name = active_owner->names[call.name_id];
            if (interpreter.functions.contains(call_name) && !pending_calls.empty()
                && pending_calls.back().call_site == instruction.auxiliary)
            {
                pending_calls.back().parameters->move(instruction.operand, registers, input_slot(instruction.input_count - 1));
                continue;
            }
            if (!interpreter.functions.contains(call_name))
            {
                std::shared_ptr<const Module> function_module;
                const auto* function = machine.find_function(call_name, call.arguments.size(), function_module);
                if (!function || instruction.operand >= function->parameters.size())
                {
                    machine.set_error("function changed while evaluating arguments");
                    result = machine.error_result(); return true;
                }
                const auto& parameter = function->parameters[instruction.operand];
                const size_t argument = input_slot(instruction.input_count - 1);
                if (pending_calls.empty() || pending_calls.back().call_site != instruction.auxiliary)
                {
                    machine.set_error("bytecode argument window was not prepared");
                    result = machine.error_result(); return true;
                }
                auto& parameters = *pending_calls.back().parameters;
                if (parameter.type_name.empty() || parameter.type_name == "auto")
                    parameters.move(instruction.operand, registers, argument);
                else
                {
                    machine.convert_type(parameters, instruction.operand, registers, argument, parameter.type_name, call.arguments[instruction.operand]);
                    registers.clear(argument);
                }
                if (machine.should_stop()) { result = machine.error_result(); return true; }
            }
            continue;
        }
        if (instruction.opcode == Opcode::MethodNoArgs)
        {
            const auto& site = active_owner->calls[instruction.operand];
            auto receiver = machine.named_value(active_owner->names[site.base_name_id]);
            std::vector<SourceLocation> arguments;
            RegisterSlots values(registers, registers.base() + active_instructions->register_capacity
                + active_instructions->temporary_count + active_instructions->switch_count
                + active_instructions->range_count + 1, 0);
            machine.call_method(registers, output, active_owner->names[site.name_id],
                active_owner->source(site.method_source), receiver, arguments, values);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (instruction.auxiliary != 0)
            {
                if (instruction.auxiliary - 1 >= active_locals->size())
                {
                    machine.set_error("bytecode local slot out of range");
                    result = machine.error_result();
                    return true;
                }
            }
            continue;
        }
        if (instruction.opcode == Opcode::RangeBegin)
        {
            RangeState state;
            const size_t argument = input_slot(instruction.input_count - 1);
            state.snapshot_slot = active_instructions->register_capacity + active_instructions->temporary_count
                + active_instructions->switch_count + ranges.size();
            if (!machine.range(registers, argument, active_owner->source(instruction.target_source),
                registers, state.snapshot_slot))
            { result = machine.error_result(); return true; }
            registers.clear(input_slot(instruction.input_count - 1));
            ranges[instruction.operand] = std::move(state);
            continue;
        }
        if (instruction.opcode == Opcode::RangeNext)
        {
            auto& state = ranges.at(instruction.operand);
            auto* payload = value_get_if<std::any>(&registers.resource_payload(state.snapshot_slot));
            auto* snapshot = payload ? std::any_cast<VmArray>(payload) : nullptr;
            const bool available = snapshot && state.index < snapshot->values.size();
            if (available)
            {
                const auto& parameter = active_owner->source(instruction.target_source);
                const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
                const auto& name = active_owner->names[site.name_id];
                range_binding.emplace();
                range_binding->name = name;
                range_binding->slot = instruction.auxiliary;
                range_binding->value_slot = active_instructions->register_capacity + active_instructions->temporary_count
                    + active_instructions->switch_count + active_instructions->range_count;
                registers.store_payload(range_binding->value_slot, snapshot->values[state.index++]);
                if (!machine.bind_range(registers, range_binding->value_slot, name, active_owner->names[site.type_id], parameter))
                { result = machine.error_result(); return true; }
                if (instruction.auxiliary != 0)
                {
                    const size_t slot = instruction.auxiliary - 1;
                    if (slot >= active_locals->size())
                    {
                        machine.set_error("bytecode local slot out of range");
                        result = machine.error_result();
                        return true;
                    }
                    active_locals->move(slot, registers, range_binding->value_slot);
                    active_aliases[slot] = false;
                }
            }
            registers.write_payload(output, available);
            continue;
        }
        if (instruction.opcode == Opcode::RangeEnd)
        {
            registers.clear(ranges.at(instruction.operand).snapshot_slot);
            ranges.erase(instruction.operand);
            if (range_binding) registers.clear(range_binding->value_slot);
            range_binding.reset();
            continue;
        }
        if (instruction.opcode == Opcode::Index)
        {
            const auto& site = active_owner->index_sites[instruction.auxiliary];
            const auto& name = active_owner->names[site.name_id];
            auto receiver = machine.named_value(name);
            auto* container = receiver.resource();
            auto* array = container ? std::any_cast<VmArray>(container) : nullptr;
            const bool map_access = (container && std::any_cast<ObjectMap>(container)) || site.string_index;
            if (site.declaration)
            {
                if (auto* binding = machine.find_slot(name))
                {
                    auto& file = *binding->file;
                    const size_t slot = binding->slot;
                    std::int64_t requested = 0;
                    if (site.dimensions != 0)
                    {
                        const size_t argument = input_slot(instruction.input_count - instruction.operand);
                        if (!registers.empty(argument) && !registers.integer(argument, requested))
                        {
                            machine.conversion_error(registers, argument, "int", nullptr);
                            requested = 0;
                        }
                    }
                    const size_t count = requested < 0 ? 0 : static_cast<size_t>(requested);
                    if (!array)
                    {
                        file.write_payload(slot, std::any(VmArray(count)));
                        array = std::any_cast<VmArray>(&value_get<std::any>(file.resource_payload(slot)));
                    }
                    else array->values.resize(count);
                    const auto& type = active_owner->names[site.type_id];
                    const size_t absolute = file.base() + slot;
                    auto descriptor = file.type_pool[file.slot_types[absolute]];
                    descriptor.element = type;
                    file.set_type(slot, descriptor);
                    file.set_name(slot, name);
                    registers.copy(output, file, slot);
                    for (size_t index = 0; index < instruction.operand; ++index)
                    {
                        const size_t argument = input_slot(instruction.input_count - instruction.operand + index);
                        if (argument != output) registers.clear(argument);
                    }
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    continue;
                }
            }
            if (active_owner->host_function_version != 0 && !site.declaration && site.dimensions == 1
                && array != nullptr)
            {
#ifdef CIFA_VM_PROFILE
                VmProfile::Timer array_get_timer(profile.array_get_fast_ns);
                ++profile.array_get_fast;
#endif
                const size_t index_slot = input_slot(instruction.input_count - 1);
                std::int64_t offset = 0;
                if (!registers.integer(index_slot, offset))
                {
                    machine.conversion_error(registers, index_slot, "int", nullptr);
                    offset = 0;
                }
                if (offset < 0)
                {
                    machine.set_error("array index is out of range");
                    result = machine.error_result();
                    return true;
                }
                const auto index = static_cast<size_t>(offset);
                auto& values = array->values;
                if (index >= values.size())
                {
                    const auto old_size = values.size();
                    values.resize(index + 1);
                }
                machine.read_indexed(registers, output,
                    {&values[index], nullptr, name, receiver.element_type}, false);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                continue;
            }
            auto element = machine.indexed(name, active_owner->names[site.type_id],
                site.dimensions, site.declaration, false, false, registers,
                active_instructions->register_inputs.data() + instruction.input_offset + instruction.input_count - instruction.operand);
            machine.read_indexed(registers, output, element, map_access);
            for (size_t index = 0; index < instruction.operand; ++index)
            {
                const size_t argument = input_slot(instruction.input_count - instruction.operand + index);
                if (argument != output) registers.clear(argument);
            }
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Array)
        {
            VmArray elements(instruction.operand);
            for (size_t index = elements.values.size(); index > 0; --index)
            {
                const size_t argument = input_slot(instruction.input_count - elements.values.size() + index - 1);
                elements.values[index - 1] = std::move(registers.resource_payload(argument));
                registers.clear(argument);
            }
            registers.write_payload(output, std::any(std::move(elements)));
            continue;
        }
        if (instruction.opcode == Opcode::CallBegin)
        {
            const auto& call = active_owner->calls[instruction.operand];
            const auto& call_name = active_owner->names[call.name_id];
            if (!interpreter.functions.contains(call_name))
            {
#ifdef CIFA_VM_PROFILE
                ++profile.script_calls;
                ++profile.script_call_names[call_name];
#endif
                std::shared_ptr<const Module> function_module;
                const auto* prepared_function = machine.find_cached_function(call, call_name, call.arguments.size(), function_module);
                if (prepared_function == nullptr)
                {
                    const auto known = machine.functions.find(call_name);
                    if (known == machine.functions.end()) machine.set_error("function '" + call_name + "' is not defined");
                    else
                    {
                        std::vector<size_t> arities;
                        for (const auto& entry : known->second) arities.push_back(entry.first);
                        std::sort(arities.begin(), arities.end());
                        std::string available;
                        for (size_t index = 0; index < arities.size(); ++index)
                        {
                            if (index != 0) available += ", ";
                            available += std::to_string(arities[index]);
                        }
                        machine.set_error("function '" + call_name + "' has no overload for "
                            + std::to_string(call.arguments.size()) + " arguments; available: " + available);
                    }
                }
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                const size_t caller_base = registers.base();
                const size_t caller_size = registers.size();
                const size_t caller_top = registers.top();
                registers.enter((std::max)(prepared_function->local_slot_count, call.arguments.size()));
                local_windows.emplace_back(registers, registers.base(), registers.size());
                pending_calls.push_back({&local_windows.back(), caller_top, instruction.operand});
                registers.window_base = caller_base;
                registers.window_size = caller_size;
            }
            continue;
        }
        if (instruction.opcode == Opcode::CallEnd) continue;
        if (instruction.opcode == Opcode::Call)
        {
            auto& call = active_owner->calls[instruction.operand];
            auto& call_source = active_owner->source(call.source);
            const auto& call_name = active_owner->names[call.name_id];
            if (interpreter.functions.contains(call_name))
            {
#ifdef CIFA_VM_PROFILE
                ++profile.host_calls;
                ++profile.host_call_names[call_name];
                const auto host_prepare_start = std::chrono::steady_clock::now();
#endif
                ObjectVector arguments(call.arguments.size());
                RegisterSlots* prepared_arguments = !pending_calls.empty() && pending_calls.back().call_site == instruction.operand
                    ? pending_calls.back().parameters : nullptr;
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    if (prepared_arguments) prepared_arguments->export_argument(index, arguments[index]);
                    else registers.export_argument(input_slot(instruction.input_count - arguments.size() + index), arguments[index]);
                    if (arguments[index].name.empty()) arguments[index].name = call.arguments[index].str;
                }
                if (prepared_arguments)
                {
                    registers.restore(registers.base(), registers.size(), pending_calls.back().caller_top);
                    pending_calls.pop_back();
                    local_windows.pop_back();
                }
                Object value;
#ifdef CIFA_VM_PROFILE
                const auto host_invoke_start = std::chrono::steady_clock::now();
                profile.host_prepare_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    host_invoke_start - host_prepare_start).count());
#endif
                if (active_owner->host_function_version != 0 && machine.call_builtin_math(call_name, arguments, value))
                    registers.import_object(output, std::move(value));
                else registers.import_object(output, machine.call_host(call_name, arguments, call.arguments));
#ifdef CIFA_VM_PROFILE
                profile.host_invoke_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - host_invoke_start).count());
#endif
                if (active_owner->host_function_version != 0
                    && active_owner->host_function_version != machine.host.function_version)
                {
                    machine.set_error("host functions changed during optimized bytecode execution", &call_source);
                }
                if (active_owner->freeze_script_functions
                    && active_owner->script_function_version != machine.function_version)
                {
                    machine.set_error("script functions changed during optimized bytecode execution", &call_source);
                }
            }
            else
            {
#ifdef CIFA_VM_PROFILE
                const auto script_setup_start = std::chrono::steady_clock::now();
#endif
                std::shared_ptr<const Module> function_module;
                const auto* cached = machine.find_cached_function(call, call_name, call.arguments.size(), function_module);
                if (!cached)
                {
                    machine.set_error("bytecode function was not prepared: " + call_name);
                    result = machine.error_result();
                    return true;
                }
                ScopeStack locals(1);
                const size_t caller_base = registers.base();
                const size_t caller_size = registers.size();
                if (pending_calls.empty() || pending_calls.back().call_site != instruction.operand)
                {
                    machine.set_error("bytecode call window was not prepared");
                    result = machine.error_result(); return true;
                }
                const auto pending = pending_calls.back();
                pending_calls.pop_back();
                const size_t caller_top = pending.caller_top;
                const size_t local_count = (std::max)(cached->local_slot_count, call.arguments.size());
                auto* local_values = pending.parameters;
                if (local_count > local_values->size())
                {
                    registers.enter(local_count - local_values->size());
                    local_values->window_size = local_count;
                    local_values->window_top = local_values->base() + local_count;
                    registers.window_base = caller_base;
                    registers.window_size = caller_size;
                }
                for (size_t index = 0; index < call.arguments.size(); ++index)
                {
                    const auto& parameter = cached->parameters[index];
                    if ((parameter.type_name == "int" || parameter.type_name == "double")
                        && local_values->payload(index).index() >= 1 && local_values->payload(index).index() <= 3)
                    {
                        local_values->bind_numeric(index, parameter.type_name == "int"
                            ? RegisterSlots::NumericBinding::Int : RegisterSlots::NumericBinding::Double);
                        if (!local_values->has_name(index)) local_values->set_name(index, call.arguments[index].str);
                        locals.back().bind(parameter.name, local_values, index);
                        continue;
                    }
                    machine.bind_type(*local_values, index, parameter.type_name, call.arguments[index]);
                    if (!local_values->has_name(index)) local_values->set_name(index, call.arguments[index].str);
                    locals.back().bind(parameter.name, local_values, index);
                }
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                frames.push_back({active_instructions, active_node, pc, pc - 1, output,
                    active_locals, std::move(scopes), std::move(local_scope_bases), std::move(loop_states), std::move(switches), active_function, active_call,
                    std::move(ranges), std::move(method_arguments), method_argument_top, active_owner, std::move(active_module), std::move(active_aliases),
                    caller_base, caller_size, caller_top, std::move(pending_calls)});
                active_function = cached;
                active_call = &call_source;
                active_node = &function_module->source(cached->body_source);
                active_owner = function_module.get();
                active_globals = &link_globals(active_owner);
                active_module = std::move(function_module);
                active_instructions = &cached->instructions;
                registers.enter(active_instructions->register_capacity + active_instructions->temporary_count
                    + active_instructions->switch_count + active_instructions->range_count
                    + active_instructions->method_argument_count + 4);
                active_locals = local_values;
                active_aliases.assign(active_locals->size(), false);
                scopes = std::move(locals);
                scopes.reserve(scopes.size() + active_instructions->scope_capacity);
                local_scope_bases.clear();
                loop_states.assign(active_instructions->loop_state_count, std::nullopt);
                switches.clear();
                ranges.clear();
                method_arguments.clear();
                method_argument_top = 0;
                pending_calls.clear();
                return_states.emplace_back();
                return_states.back().return_type = cached->return_type;
                pc = 0;
#ifdef CIFA_VM_PROFILE
                profile.script_setup_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - script_setup_start).count());
#endif
            }
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::SwitchMark)
        {
            auto& state = switches[instruction.operand];
            state.condition_slot = active_instructions->register_capacity + active_instructions->temporary_count + switches.size() - 1;
            registers.move(state.condition_slot, registers, input_slot(instruction.input_count - 1));
            state.active = false;
            continue;
        }
        if (instruction.opcode == Opcode::SwitchCase)
        {
            registers.write_payload(output, switches.at(instruction.operand).active);
            continue;
        }
        if (instruction.opcode == Opcode::SwitchDefault)
        {
            auto& state = switches.at(instruction.operand);
            if (instruction.auxiliary != 0)
            {
                const size_t argument = input_slot(instruction.input_count - 1);
                BytecodeValue::Storage condition_numeric, argument_numeric;
                if (!registers.binary_payloads(Opcode::Equal, output, registers.payload(state.condition_slot, condition_numeric),
                    registers.payload(argument, argument_numeric), machine, *current_source))
                {
                    const size_t scratch = registers.size() - 1;
                    registers.copy(scratch, registers, state.condition_slot);
                    registers.binary_fallback(Opcode::Equal, output, scratch, argument, machine, *current_source, false);
                }
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                state.active = machine.condition(registers, output, nullptr);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                registers.write_payload(output, state.active);
            }
            else state.active = true;
            if (interpreter.should_stop_execution()) { result = Object(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::SwitchEnd)
        {
            registers.clear(switches.at(instruction.operand).condition_slot);
            switches.erase(instruction.operand);
            continue;
        }
        if (instruction.opcode == Opcode::LoopMark)
        {
            auto& state = loop_states[instruction.auxiliary];
            state = LoopState{scopes.size(), local_scope_bases.size()};
            for (const auto& entry : ranges) state->ranges.push_back(entry.first);
            for (const auto& entry : switches) state->switches.push_back(entry.first);
            continue;
        }
        if (instruction.opcode == Opcode::Unwind)
        {
            const auto& state = loop_states.at(instruction.operand).value();
            for (auto range = ranges.begin(); range != ranges.end();)
                if (std::find(state.ranges.begin(), state.ranges.end(), range->first) == state.ranges.end())
                {
                    registers.clear(range->second.snapshot_slot);
                    range = ranges.erase(range);
                }
                else ++range;
            range_binding.reset();
            for (auto current = switches.begin(); current != switches.end();)
                if (std::find(state.switches.begin(), state.switches.end(), current->first) == state.switches.end())
                {
                    registers.clear(current->second.condition_slot);
                    current = switches.erase(current);
                }
                else ++current;
            release_scopes_to(state.scopes);
            while (local_scope_bases.size() > state.local_scope_bases)
            {
                const size_t base = local_scope_bases.back();
                local_scope_bases.pop_back();
                if (base != 0)
                    for (size_t slot = base - 1; slot < active_locals->size(); ++slot)
                    {
                        active_locals->clear(slot);
                        active_aliases[slot] = false;
                    }
            }
            continue;
        }
        if (instruction.opcode == Opcode::PrepareStore)
        {
            auto& target = active_owner->source(instruction.target_source);
            if (instruction.member_site != 0)
            {
                const auto& member = active_owner->member_sites[instruction.member_site - 1];
                machine.resolve_member(active_owner->names[member.first], active_owner->names[member.second]);
            }
            else if (instruction.variable_site != 0)
            {
                const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
                const auto& name = active_owner->names[site.name_id];
                Scope::Binding* binding = nullptr;
                if (site.with_type && !scopes.empty())
                    binding = &scopes.back().create(name);
                else binding = machine.find_slot(name);
                if (binding)
                {
                    if (site.with_type) machine.bind_type(*binding->file, binding->slot,
                        active_owner->names[site.type_id], target);
                }
                else machine.assign_named(name, active_owner->names[site.type_id],
                    site.with_type, site.with_type, target);
            }
            else
            {
                const auto& site = active_owner->index_sites[instruction.operand - 1];
                const auto& name = active_owner->names[site.name_id];
                if (site.declaration && !scopes.empty())
                {
                    auto& binding = scopes.back().create(name);
                    if (!value_holds<std::any>(binding.file->resource_payload(binding.slot))
                        || !std::any_cast<VmArray>(&value_get<std::any>(binding.file->resource_payload(binding.slot))))
                        binding.file->write_payload(binding.slot, std::any(VmArray{}));
                    const size_t absolute = binding.file->base() + binding.slot;
                    auto descriptor = binding.file->type_pool[binding.file->slot_types[absolute]];
                    descriptor.element = active_owner->names[site.type_id];
                    binding.file->set_type(binding.slot, descriptor);
                    binding.file->set_name(binding.slot, name);
                }
                else machine.indexed(name, active_owner->names[site.type_id],
                    site.dimensions, site.declaration, false, site.with_type, registers,
                    active_instructions->register_inputs.data() + instruction.input_offset + instruction.input_count - site.dimensions);
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::DeclareLocal)
        {
            auto& source = active_owner->source(instruction.source);
            if (instruction.operand >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            machine.read_named(registers, output, active_owner->names[site.name_id], active_owner->names[site.type_id],
                site.with_type, false, true, source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            active_locals->copy(instruction.operand, registers, output);
            bind_local_storage(active_owner->names[site.name_id], instruction.operand, false);
            if (active_aliases[instruction.operand]) active_locals->clear(instruction.operand);
            continue;
        }
        if (instruction.opcode == Opcode::IncrementLocal)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::PathTimer increment_local_timer(profile.increment_local_ns,
                profile.increment_local_path_ns.data(), profile.increment_local_path_counts.data());
#endif
            auto& source = active_owner->source(instruction.source);
            if (instruction.operand >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& binding = active_owner->variable_sites[instruction.variable_site - 1];
            std::int64_t integer = 0;
            double floating = 0;
            bool is_double = false;
            const bool integer_value = active_locals->number(instruction.operand, integer, floating, is_double)
                && !is_double && value_holds<std::int64_t>(active_locals->payload(instruction.operand));
            if (!active_aliases[instruction.operand] && !binding.with_type
                && integer_value)
            {
                const bool post = instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract;
                if (post && !instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                const auto before = static_cast<std::uint64_t>(integer);
                const bool add = instruction.write == WriteOperation::Add || instruction.write == WriteOperation::PostAdd;
                const auto after = std::bit_cast<std::int64_t>(add ? before + 1 : before - 1);
                active_locals->write_number(instruction.operand, after, true);
                if (!post && !instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                continue;
            }
            RegisterSlots* increment_file = active_aliases[instruction.operand] ? nullptr : active_locals;
            size_t increment_slot = instruction.operand;
            if (binding.with_type && !scopes.empty())
            {
                const auto& name = active_owner->names[binding.name_id];
                const auto* existing = scopes.back().find(name);
                if (!existing || (existing->file == active_locals && existing->slot == instruction.operand))
                {
                    machine.bind_type(*active_locals, instruction.operand, active_owner->names[binding.type_id], source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    if (!existing) scopes.back().bind(name, active_locals, instruction.operand);
                    active_aliases[instruction.operand] = false;
                    increment_file = active_locals;
                }
                else
                {
                    machine.bind_type(*existing->file, existing->slot, active_owner->names[binding.type_id], source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    active_aliases[instruction.operand] = true;
                    increment_file = existing->file;
                    increment_slot = existing->slot;
                }
            }
            if (!increment_file && !binding.with_type)
                for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
                    if (auto* found = scope->find(active_owner->names[binding.name_id]))
                    {
                        increment_file = found->file;
                        increment_slot = found->slot;
                        break;
                    }
            if (increment_file)
            {
#ifdef CIFA_VM_PROFILE
                increment_local_timer.select(1);
#endif
                const bool post = instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract;
                if (post && !instruction.discard_result) registers.copy(output, *increment_file, increment_slot);
                const size_t computed = registers.size() - 2;
                const size_t unit = registers.size() - 1;
                registers.copy(computed, *increment_file, increment_slot);
                registers.write_payload(unit, std::int64_t(1));
                const auto operation = instruction.write == WriteOperation::Add || instruction.write == WriteOperation::PostAdd
                    ? Opcode::Add : Opcode::Subtract;
                registers.binary_fallback(operation, computed, computed, unit, machine, source, false);
                if (!machine.should_stop()) machine.assign(*increment_file, increment_slot, registers, computed, unit, source);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                registers.clear(computed);
                registers.clear(unit);
                if (!post && !instruction.discard_result) registers.copy(output, *increment_file, increment_slot);
                continue;
            }
#ifdef CIFA_VM_PROFILE
            increment_local_timer.select(2);
#endif
            auto& target = alias_target(binding.name_id);
            const bool preserve_old = !instruction.discard_result
                && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract);
            if (preserve_old) registers.import_object(output, target);
            const size_t computed = registers.size() - 2;
            const size_t unit = registers.size() - 1;
            registers.import_object(computed, target);
            registers.write_payload(unit, std::int64_t(1));
            const auto operation = write_opcode(instruction.write);
            if (!operation) machine.set_error("invalid bytecode write operation");
            else registers.binary_fallback(*operation, computed, computed, unit, machine, source, false);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            Object value;
            registers.export_argument(computed, value);
            registers.clear(unit);
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            machine.assign(target, std::move(value), site.with_type, active_owner->names[site.type_id], source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (!instruction.discard_result && !preserve_old)
                registers.import_object(output, target);
            continue;
        }
        if (instruction.opcode == Opcode::StoreLocal)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::PathTimer local_store_timer(profile.local_store_ns,
            profile.local_store_path_ns.data(), profile.local_store_path_counts.data());
#endif
            auto& source = active_owner->source(instruction.source);
            if (instruction.operand >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& binding = active_owner->variable_sites[instruction.variable_site - 1];
            const auto& declared_type = active_owner->names[binding.type_id];
            if (!binding.with_type && active_aliases[instruction.operand])
            {
                RegisterSlots* target_file = nullptr;
                size_t target_slot = 0;
                const auto& name = active_owner->names[binding.name_id];
                for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
                    if (auto* found = scope->find(name))
                    {
                        target_file = found->file;
                        target_slot = found->slot;
                        break;
                    }
                if (target_file)
                {
#ifdef CIFA_VM_PROFILE
                    local_store_timer.select(1);
#endif
                    const size_t argument = input_slot(instruction.input_count - 1);
                    const size_t computed = registers.size() - 2;
                    const size_t conversion = registers.size() - 1;
                    size_t value_slot = argument;
                    if (instruction.write != WriteOperation::Assign)
                    {
                        const auto operation = write_opcode(instruction.write);
                        if (!operation) { machine.set_error("invalid bytecode write operation", &source); result = machine.error_result(); return true; }
                        registers.copy(computed, *target_file, target_slot);
                        registers.binary_fallback(*operation, computed, computed, argument, machine, source, false);
                        value_slot = computed;
                    }
                    if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, value_slot, conversion, source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(argument);
                    registers.clear(computed);
                    registers.clear(conversion);
                    if (!instruction.discard_result) registers.copy(output, *target_file, target_slot);
                    continue;
                }
            }
            if (instruction.write == WriteOperation::Assign && binding.with_type && !scopes.empty()
                && (binding.type_id == active_owner->int_type_id || binding.type_id == active_owner->double_type_id))
            {
                const auto& name = active_owner->names[binding.name_id];
                const auto* existing = scopes.back().find(name);
                if (existing == nullptr || (existing->file == active_locals && existing->slot == instruction.operand))
                {
#ifdef CIFA_VM_PROFILE
                    local_store_timer.select(2);
#endif
                    const size_t argument = input_slot(instruction.input_count - 1);
                    if (initialize_numeric(instruction.operand, registers, argument, binding))
                    {
                        registers.clear(argument);
                        if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                        continue;
                    }
                    machine.bind_type(*active_locals, instruction.operand, declared_type, source);
                    if (!machine.should_stop()) machine.assign(*active_locals, instruction.operand, registers,
                        argument, registers.size() - 2, source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(argument);
                    if (existing == nullptr) scopes.back().bind(name, active_locals, instruction.operand);
                    active_aliases[instruction.operand] = false;
                    if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                    continue;
                }
            }
#ifdef CIFA_VM_PROFILE
            const auto fast_start = std::chrono::steady_clock::now();
#endif
            if (!binding.with_type && !active_aliases[instruction.operand]
                && instruction.write != WriteOperation::Assign)
            {
#ifdef CIFA_VM_PROFILE
                local_store_timer.select(3);
#endif
                const auto operation = write_opcode(instruction.write);
                const size_t argument = input_slot(instruction.input_count - 1);
                const size_t scratch = registers.size() - 2;
                if (operation)
                {
                    BytecodeValue::Storage local_numeric, argument_numeric;
                    if (!registers.binary_payloads(*operation, scratch,
                        active_locals->payload(instruction.operand, local_numeric), registers.payload(argument, argument_numeric), machine, source))
                    {
                        registers.copy(scratch, *active_locals, instruction.operand);
                        registers.binary_fallback(*operation, scratch, scratch, argument, machine, source, false);
                    }
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    if (!active_locals->assign_numeric(instruction.operand, registers, scratch))
                        machine.assign(*active_locals, instruction.operand, registers, scratch, registers.size() - 1, source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(argument);
                    registers.clear(scratch);
                    if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                    continue;
                }
            }
            if (instruction.write == WriteOperation::Assign && !binding.with_type && !active_aliases[instruction.operand]
                && active_locals->assign_numeric(instruction.operand, registers, input_slot(instruction.input_count - 1)))
            {
#ifdef CIFA_VM_PROFILE
                local_store_timer.select(4);
#endif
#ifdef CIFA_VM_PROFILE
                ++profile.local_store_fast;
                const auto result_start = std::chrono::steady_clock::now();
                profile.local_store_fast_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    result_start - fast_start).count());
#endif
                if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
#ifdef CIFA_VM_PROFILE
                profile.local_store_result_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                    std::chrono::steady_clock::now() - result_start).count());
#endif
                continue;
            }
#ifdef CIFA_VM_PROFILE
            ++profile.local_store_fallback;
#endif
            if (instruction.write == WriteOperation::Assign && binding.with_type && !scopes.empty())
            {
#ifdef CIFA_VM_PROFILE
                local_store_timer.select(5);
#endif
                const auto& name = active_owner->names[binding.name_id];
                const auto* position = scopes.back().find(name);
                if (!position || (position->file == active_locals && position->slot == instruction.operand))
                {
                    const size_t argument = input_slot(instruction.input_count - 1);
                    machine.bind_type(*active_locals, instruction.operand, declared_type, source);
                    if (!machine.should_stop()) machine.assign(*active_locals, instruction.operand, registers,
                        argument, registers.size() - 2, source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(argument);
                    if (!position) scopes.back().bind(name, active_locals, instruction.operand);
                    active_aliases[instruction.operand] = false;
                    if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                    continue;
                }
                const size_t argument = input_slot(instruction.input_count - 1);
                machine.bind_type(*position->file, position->slot, declared_type, source);
                if (!machine.should_stop()) machine.assign(*position->file, position->slot, registers,
                    argument, registers.size() - 2, source);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                registers.clear(argument);
                active_aliases[instruction.operand] = true;
                if (!instruction.discard_result) registers.copy(output, *position->file, position->slot);
                continue;
            }
            if (instruction.write == WriteOperation::Assign && !binding.with_type && !active_aliases[instruction.operand])
            {
#ifdef CIFA_VM_PROFILE
                local_store_timer.select(6);
#endif
                const size_t argument = input_slot(instruction.input_count - 1);
                machine.assign(*active_locals, instruction.operand, registers, argument, registers.size() - 2, source);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                registers.clear(argument);
                if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                continue;
            }
            if (!binding.with_type && active_aliases[instruction.operand])
            {
                const auto& name = active_owner->names[binding.name_id];
                if (auto* alias = machine.find_slot(name))
                {
#ifdef CIFA_VM_PROFILE
                    local_store_timer.select(7);
#endif
                    const size_t argument = input_slot(instruction.input_count - 1);
                    const size_t computed = registers.size() - 2;
                    size_t value_slot = argument;
                    if (instruction.write != WriteOperation::Assign)
                    {
                        registers.copy(computed, *alias->file, alias->slot);
                        const auto operation = write_opcode(instruction.write);
                        if (!operation) machine.set_error("invalid bytecode write operation");
                        else registers.binary_fallback(*operation, computed, computed, argument, machine, source, false);
                        value_slot = computed;
                    }
                    if (!machine.should_stop()) machine.assign(*alias->file, alias->slot, registers,
                        value_slot, registers.size() - 1, source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(argument);
                    registers.clear(computed);
                    if (!instruction.discard_result) registers.copy(output, *alias->file, alias->slot);
                    continue;
                }
            }
#ifdef CIFA_VM_PROFILE
            const size_t profile_target_index = active_locals->base() + instruction.operand;
            auto profile_target_type = active_locals->type_pool[active_locals->slot_types[profile_target_index]];
            BytecodeValue::Storage profile_rhs_numeric;
            const auto& profile_rhs = registers.payload(input_slot(instruction.input_count - 1), profile_rhs_numeric);
            const size_t reason = instruction.write != WriteOperation::Assign ? 0
                : binding.with_type ? 1
                : active_aliases[instruction.operand] ? 2
                : !profile_target_type.special.empty() ? 3
                : !((profile_target_type.declared == "int" && profile_target_type.bound == typeid(std::int64_t))
                    || (profile_target_type.declared == "double" && profile_target_type.bound == typeid(double))) ? 4
                : profile_target_type.declared == "int" && value_holds<double>(profile_rhs) ? 5 : 6;
            ++profile.local_store_reasons[reason];
            const auto* profile_rhs_resource = value_get_if<std::any>(&profile_rhs);
            ++profile.local_store_rhs[value_holds<std::int64_t>(profile_rhs) ? 0
                : value_holds<double>(profile_rhs) ? 1 : value_holds<bool>(profile_rhs) ? 2
                : profile_rhs_resource && std::any_cast<ObjectVector>(profile_rhs_resource) ? 3 : 4];
            if (reason == 1)
            {
                const auto& type_name = active_owner->names[binding.type_id];
                ++profile.local_store_declarations[type_name == "int" ? 0 : type_name == "double" ? 1 : 2];
            }
            const auto value_start = std::chrono::steady_clock::now();
#endif
            auto& target = alias_target(binding.name_id);
            const size_t argument = input_slot(instruction.input_count - 1);
            Object value;
            if (instruction.write == WriteOperation::Assign) registers.export_argument(argument, value);
            else
            {
                const size_t computed = registers.size() - 2;
                registers.import_object(computed, target);
                const auto operation = write_opcode(instruction.write);
                if (!operation) machine.set_error("invalid bytecode write operation");
                else registers.binary_fallback(*operation, computed, computed, argument, machine, source, false);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                registers.export_argument(computed, value);
                registers.clear(argument);
            }
#ifdef CIFA_VM_PROFILE
            const auto assign_start = std::chrono::steady_clock::now();
            profile.local_store_value_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                assign_start - value_start).count());
#endif
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            machine.assign(target, std::move(value), site.with_type, active_owner->names[site.type_id], source);
#ifdef CIFA_VM_PROFILE
            const auto bind_start = std::chrono::steady_clock::now();
            profile.local_store_assign_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                bind_start - assign_start).count());
#endif
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (binding.with_type || active_aliases[instruction.operand])
            {
                bool directly_bound = false;
                const auto& name = active_owner->names[site.name_id];
                if (binding.with_type && !scopes.empty())
                {
                    const auto* position = scopes.back().find(name);
                    directly_bound = position == nullptr || (position->file == active_locals
                        && position->slot == instruction.operand);
                    if (directly_bound)
                    {
                        if (position == nullptr) scopes.back().bind(name, active_locals, instruction.operand);
                        active_aliases[instruction.operand] = false;
                    }
                }
                if (!directly_bound)
                {
#ifdef CIFA_VM_PROFILE
                    ++profile.local_store_rebind;
#endif
                    bind_local_storage(name, instruction.operand, site.with_type);
                }
#ifdef CIFA_VM_PROFILE
                else ++profile.local_store_direct_bind;
#endif
            }
#ifdef CIFA_VM_PROFILE
            else ++profile.local_store_skip_bind;
            const auto result_start = std::chrono::steady_clock::now();
            profile.local_store_bind_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                result_start - bind_start).count());
#endif
            if (!instruction.discard_result) registers.import_object(output, target);
#ifdef CIFA_VM_PROFILE
            profile.local_store_result_ns += static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - result_start).count());
#endif
            continue;
        }
        if (instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment)
        {
            auto& source = active_owner->source(instruction.source);
            const bool increment = instruction.opcode == Opcode::Increment;
            const bool member = instruction.member_site != 0;
            const bool indexed = instruction.operand != 0;
            const IndexSite* site = indexed ? &active_owner->index_sites[instruction.operand - 1] : nullptr;
            const VariableSite* variable = instruction.variable_site != 0
                ? &active_owner->variable_sites[instruction.variable_site - 1] : nullptr;
            if (!member && !indexed && variable)
            {
                const auto& name = active_owner->names[variable->name_id];
                RegisterSlots* target_file = nullptr;
                size_t target_slot = 0;
                for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
                {
                    if (auto* found = scope->find(name))
                    {
                        target_file = found->file;
                        target_slot = found->slot;
                        break;
                    }
                    if (increment && variable->with_type) break;
                }
                if (target_file)
                {
                    const auto& type = active_owner->names[variable->type_id];
                    if (variable->with_type) machine.bind_type(*target_file, target_slot, type, active_owner->source(instruction.target_source));
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    const bool post = increment && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract);
                    if (post) registers.copy(output, *target_file, target_slot);
                    const size_t computed = registers.size() - 2;
                    const size_t conversion = registers.size() - 1;
                    const size_t argument = increment ? conversion : input_slot(instruction.input_count - 1);
                    if (increment) registers.write_payload(argument, std::int64_t(1));
                    size_t value_slot = argument;
                    if (instruction.write != WriteOperation::Assign)
                    {
                        const auto operation = write_opcode(instruction.write);
                        if (!operation) { machine.set_error("invalid bytecode write operation", &source); result = machine.error_result(); return true; }
                        registers.copy(computed, *target_file, target_slot);
                        registers.binary_fallback(*operation, computed, computed, argument, machine, source, false);
                        value_slot = computed;
                    }
                    if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, value_slot, conversion, source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(argument);
                    registers.clear(computed);
                    registers.clear(conversion);
                    if (!post) registers.copy(output, *target_file, target_slot);
                    continue;
                }
            }
            Machine::IndexedValueRef target;
            if (member)
                target.object = &machine.resolve_member(
                    active_owner->names[active_owner->member_sites[instruction.member_site - 1].first],
                    active_owner->names[active_owner->member_sites[instruction.member_site - 1].second]);
            else if (indexed)
                target = machine.indexed(
                    active_owner->names[site->name_id], active_owner->names[site->type_id],
                    site->dimensions, false, false, increment && site->with_type, registers,
                    active_instructions->register_inputs.data() + instruction.input_offset + instruction.input_count
                        - (increment ? 0 : 1) - site->dimensions);
            else
                target.object = &machine.assign_named(active_owner->names[variable->name_id], active_owner->names[variable->type_id],
                    variable->with_type, increment && variable->with_type, active_owner->source(instruction.target_source));
            const bool post = increment && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract);
            if (post) machine.read_indexed(registers, output, target, false);
            const size_t computed = registers.size() - 2;
            const size_t argument = increment ? registers.size() - 1 : input_slot(instruction.input_count - 1);
            if (increment) registers.write_payload(argument, std::int64_t(1));
            size_t value_slot = argument;
            if (instruction.write != WriteOperation::Assign)
            {
                machine.read_indexed(registers, computed, target, false);
                const auto operation = write_opcode(instruction.write);
                if (!operation) machine.set_error("invalid bytecode write operation");
                else registers.binary_fallback(*operation, computed, computed, argument, machine, source, false);
                value_slot = computed;
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            Object value;
            registers.export_argument(value_slot, value);
            if (argument != value_slot) registers.clear(argument);
            const bool with_type = variable != nullptr ? variable->with_type : site != nullptr && site->with_type;
            const std::string& type_name = variable != nullptr ? active_owner->names[variable->type_id]
                : site != nullptr ? active_owner->names[site->type_id] : std::string();
            machine.assign_indexed(target, std::move(value), with_type, type_name, source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (instruction.auxiliary != 0)
            {
                const size_t slot = instruction.auxiliary - 1;
                if (slot >= active_locals->size())
                {
                    machine.set_error("bytecode local slot out of range");
                    result = machine.error_result();
                    return true;
                }
            }
            if (!post) machine.read_indexed(registers, output, target, false);
            continue;
        }
        if (instruction.opcode == Opcode::ScopeEnter)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::Timer scope_enter_timer(profile.scope_enter_ns);
            if (instruction.auxiliary != 0) ++profile.scope_enter_with_slots;
            else ++profile.scope_enter_without_slots;
#endif
            if (reusable_scopes.empty()) scopes.emplace_back();
            else
            {
                scopes.push_back(std::move(reusable_scopes.back()));
                reusable_scopes.pop_back();
            }
            scopes.back().bindings.reserve(instruction.operand);
            local_scope_bases.push_back(instruction.auxiliary);
            if (range_binding)
            {
                if (range_binding->slot != 0)
                    scopes.back().bind(range_binding->name, active_locals, range_binding->slot - 1);
                else
                {
                    auto& binding = scopes.back().create(range_binding->name);
                    binding.file->move(binding.slot, registers, range_binding->value_slot);
                }
                range_binding.reset();
            }
            continue;
        }
        if (instruction.opcode == Opcode::ScopeLeave)
        {
            if (instruction.auxiliary != 0)
                for (size_t slot = instruction.auxiliary - 1; slot < active_locals->size(); ++slot)
                {
                    active_locals->clear(slot);
                    active_aliases[slot] = false;
                }
            release_scope();
            local_scope_bases.pop_back();
            continue;
        }
        if (instruction.opcode == Opcode::Return)
        {
            call_result.move(0, registers, input_slot(instruction.input_count - 1));
            const auto& states = return_states;
            if (!states.empty() && !states.back().return_type.empty() && states.back().return_type != "void")
            {
                machine.convert_type(call_result, 0, call_result, 0, states.back().return_type, active_owner->source(instruction.source));
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (return_states.empty()) return_states.emplace_back();
            if (!frames.empty())
            {
                finish_call();
                continue;
            }
            call_result.export_argument(0, result);
            return true;
        }
        if (instruction.opcode == Opcode::Jump) { pc = instruction.operand; continue; }
        if (instruction.opcode == Opcode::Empty) { registers.clear(output); continue; }
        if (instruction.opcode == Opcode::Branch || instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::Timer branch_timer(profile.branch_ns);
#endif
            const SourceLocation* condition_source = instruction.condition_source.id != 0
                ? &active_owner->source(instruction.condition_source) : nullptr;
            const SourceLocation* call_source = &active_owner->source(instruction.source);
            if (condition_source != nullptr) current_source = condition_source;
            const bool condition = machine.condition(registers, input_slot(instruction.input_count - 1), condition_source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (instruction.opcode == Opcode::Branch)
            {
                registers.clear(input_slot(instruction.input_count - 1));
                if (!condition) pc = instruction.operand;
            }
            else if ((instruction.opcode == Opcode::AndBranch && !condition)
                || (instruction.opcode == Opcode::OrBranch && condition))
            {
                registers.write_payload(output, std::int64_t(condition ? 1 : 0));
                pc = instruction.operand;
            }
            continue;
        }
        if (instruction.opcode == Opcode::Constant)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::Timer constant_timer(profile.constant_ns);
#endif
            const auto& constant = active_owner->constants[instruction.operand];
            registers.write_payload(output, constant.value);
            if (constant.continue_marker) registers.set_type(output, {typeid(void), "", "", "__"});
            continue;
        }
        if (instruction.opcode == Opcode::Enter)
        {
            continue;
        }
        if (instruction.opcode == Opcode::Leave)
        {
            continue;
        }
        if (instruction.opcode == Opcode::RegisterSnapshot)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::PathTimer register_snapshot_timer(profile.register_snapshot_ns,
                profile.register_snapshot_path_ns.data(), profile.register_snapshot_path_counts.data());
#endif
            const size_t destination = active_instructions->register_capacity + instruction.auxiliary;
            if (instruction.write == WriteOperation::Assign)
            {
                const auto& constant = active_owner->constants[instruction.operand];
                registers.write_payload(destination, constant.value);
                if (constant.continue_marker) registers.set_type(destination, {typeid(void), "", "", "__"});
            }
            else
            {
                const auto& name = active_owner->names[instruction.variable_site];
                if (active_aliases[instruction.operand])
                {
#ifdef CIFA_VM_PROFILE
                    register_snapshot_timer.select(2);
#endif
                    read_alias(destination, instruction.variable_site, *current_source);
                    if (registers.empty(destination)) machine.set_error("variable '" + name + "' has not been initialized", current_source);
                }
                else
                {
#ifdef CIFA_VM_PROFILE
                    register_snapshot_timer.select(1);
#endif
                    if (active_locals->empty(instruction.operand))
                        machine.set_error("variable '" + name + "' has not been initialized", current_source);
                    registers.copy(destination, *active_locals, instruction.operand);
                }
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::RegisterBinary)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::PathTimer register_binary_timer(profile.register_binary_ns,
            profile.register_binary_path_ns.data(), profile.register_binary_path_counts.data());
#endif
            const auto& site = active_owner->register_binary_sites[instruction.operand];
            if (site.left_temporary && site.right_temporary)
            {
                const size_t left = active_instructions->register_capacity + site.code.left;
                const size_t right = active_instructions->register_capacity + site.code.right;
                const size_t destination = site.temporary_destination != 0
                    ? active_instructions->register_capacity + site.temporary_destination - 1
                    : (site.code.flags & 1) != 0 ? registers.size() - 1 : output;
                if (!registers.binary(static_cast<Opcode>(site.code.opcode), destination, left, right, machine, *current_source))
                {
#ifdef CIFA_VM_PROFILE
                    ++profile.register_binary_fallback;
#endif
                    registers.binary_fallback(static_cast<Opcode>(site.code.opcode), destination, left, right, machine, *current_source, true);
                }
#ifdef CIFA_VM_PROFILE
                else ++profile.register_binary_fast;
#endif
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                if (site.code.destination != 0)
                {
                    const size_t slot = site.code.destination - 1;
                    const auto& binding = active_owner->variable_sites[site.variable_site - 1];
                    const auto& name = active_owner->names[binding.name_id];
                    const bool initialized = initialize_numeric(slot, registers, destination, binding);
                    const bool assigned = !initialized && !binding.with_type && !active_aliases[slot]
                        && active_locals->assign_numeric(slot, registers, destination);
                    if (initialized || assigned)
                    {
#ifdef CIFA_VM_PROFILE
                        register_binary_timer.select(1);
                        if (initialized) ++profile.register_binary_numeric_initialize;
                        else ++profile.register_binary_numeric_assign;
                        if (binding.type_id == active_owner->int_type_id) ++profile.register_binary_numeric_int;
                        else if (binding.type_id == active_owner->double_type_id) ++profile.register_binary_numeric_double;
                        if ((site.code.flags & 1) != 0 || instruction.discard_result) ++profile.register_binary_numeric_discard;
                        else ++profile.register_binary_numeric_result;
#endif
                        registers.clear(destination);
                        if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *active_locals, slot);
                        continue;
                    }
#ifdef CIFA_VM_PROFILE
                    register_binary_timer.select(2);
#endif
                    register_assignment_source = site.assignment_source;
                    current_source = &active_owner->source(site.assignment_source);
                    const auto* existing = binding.with_type && !scopes.empty() ? scopes.back().find(name) : nullptr;
                    const auto* alias = !binding.with_type && active_aliases[slot] ? machine.find_slot(name) : nullptr;
                    auto* target_file = binding.with_type && existing ? existing->file : alias ? alias->file : active_locals;
                    const size_t target_slot = binding.with_type && existing ? existing->slot : alias ? alias->slot : slot;
                    const bool direct = binding.with_type ? !scopes.empty() : !active_aliases[slot] || alias;
                    if (direct)
                    {
                        if (binding.with_type) machine.bind_type(*target_file, target_slot,
                            active_owner->names[binding.type_id], *current_source);
                        const size_t conversion = destination == registers.size() - 1 ? registers.size() - 2 : registers.size() - 1;
                        if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, destination, conversion, *current_source);
                        if (machine.should_stop()) { result = machine.error_result(); return true; }
                        registers.clear(destination);
                        if (binding.with_type)
                        {
                            if (!existing) scopes.back().bind(name, active_locals, slot);
                            active_aliases[slot] = existing && (existing->file != active_locals || existing->slot != slot);
                        }
                        if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target_file, target_slot);
                        continue;
                    }
                    auto& target = alias_target(binding.name_id);
                    Object value;
                    registers.export_argument(destination, value);
                    machine.assign(target, std::move(value), binding.with_type, active_owner->names[binding.type_id], *current_source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    bind_local_storage(name, slot, false);
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.import_object(output, target);
                }
#ifdef CIFA_VM_PROFILE
                register_binary_timer.select(0);
#endif
                continue;
            }
            if ((site.left_constant || !active_aliases[site.code.left]) && (site.right_constant || !active_aliases[site.code.right]))
            {
                const size_t scratch_left = registers.size() - 2;
                const auto read_number = [&](bool constant, size_t slot, std::int64_t& integer, double& floating, bool& is_double)
                {
                    if (!constant) return active_locals->number(slot, integer, floating, is_double);
                    const auto& value = active_owner->constants[slot].value;
                    if (const auto* number = value_get_if<std::int64_t>(&value)) integer = *number;
                    else if (const auto* number = value_get_if<bool>(&value)) integer = *number;
                    else if (const auto* number = value_get_if<double>(&value)) { floating = *number; is_double = true; }
                    else return false;
                    return true;
                };
                std::int64_t left_integer = 0, right_integer = 0;
                double left_number = 0, right_number = 0;
                bool left_double = false, right_double = false;
                if (read_number(site.left_constant, site.code.left, left_integer, left_number, left_double)
                    && read_number(site.right_constant, site.code.right, right_integer, right_number, right_double)
                    && registers.binary_numbers(static_cast<Opcode>(site.code.opcode), scratch_left,
                        left_integer, left_number, left_double, right_integer, right_number, right_double, machine, *current_source))
                {
#ifdef CIFA_VM_PROFILE
                    ++profile.register_binary_fast;
#endif
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    if (site.code.destination == 0)
                    {
#ifdef CIFA_VM_PROFILE
                        register_binary_timer.select(3);
#endif
                        registers.move(output, registers, scratch_left);
                        continue;
                    }
                    const size_t slot = site.code.destination - 1;
                    const auto& binding = active_owner->variable_sites[site.variable_site - 1];
                    const bool initialized = initialize_numeric(slot, registers, scratch_left, binding);
                    const bool assigned = !initialized && !binding.with_type && !active_aliases[slot]
                        && active_locals->assign_numeric(slot, registers, scratch_left);
                    if (initialized || assigned)
                    {
#ifdef CIFA_VM_PROFILE
                        register_binary_timer.select(4);
                        if (initialized) ++profile.register_binary_numeric_initialize;
                        else ++profile.register_binary_numeric_assign;
                        if (binding.type_id == active_owner->int_type_id) ++profile.register_binary_numeric_int;
                        else if (binding.type_id == active_owner->double_type_id) ++profile.register_binary_numeric_double;
                        if ((site.code.flags & 1) != 0 || instruction.discard_result) ++profile.register_binary_numeric_discard;
                        else ++profile.register_binary_numeric_result;
#endif
                        if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *active_locals, slot);
                        registers.clear(scratch_left);
                        continue;
                    }
#ifdef CIFA_VM_PROFILE
                    register_binary_timer.select(5);
#endif
                    const auto& name = active_owner->names[binding.name_id];
                    register_assignment_source = site.assignment_source;
                    current_source = &active_owner->source(site.assignment_source);
                    const auto* existing = binding.with_type && !scopes.empty() ? scopes.back().find(name) : nullptr;
                    const auto* alias = !binding.with_type && active_aliases[slot] ? machine.find_slot(name) : nullptr;
                    auto* target_file = binding.with_type && existing ? existing->file : alias ? alias->file : active_locals;
                    const size_t target_slot = binding.with_type && existing ? existing->slot : alias ? alias->slot : slot;
                    const bool direct = binding.with_type ? !scopes.empty() : !active_aliases[slot] || alias;
                    if (direct)
                    {
                        if (binding.with_type) machine.bind_type(*target_file, target_slot,
                            active_owner->names[binding.type_id], *current_source);
                        if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, scratch_left, registers.size() - 1, *current_source);
                        if (machine.should_stop()) { result = machine.error_result(); return true; }
                        registers.clear(scratch_left);
                        if (binding.with_type)
                        {
                            if (!existing) scopes.back().bind(name, active_locals, slot);
                            active_aliases[slot] = existing && (existing->file != active_locals || existing->slot != slot);
                        }
                        if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target_file, target_slot);
                        continue;
                    }
                    auto& target = alias_target(binding.name_id);
                    Object value;
                    registers.export_argument(scratch_left, value);
                    machine.assign(target, std::move(value), binding.with_type,
                        active_owner->names[binding.type_id], *current_source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    bind_local_storage(name, slot, false);
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.import_object(output, target);
                    continue;
                }
                else
                {
#ifdef CIFA_VM_PROFILE
                    ++profile.register_binary_fallback;
                    register_binary_timer.select(6);
#endif
                }
                registers.clear(scratch_left);
            }
            const size_t left_slot = registers.size() - 2;
            const size_t right_slot = registers.size() - 1;
            const auto read_register = [&](size_t destination, bool constant, size_t slot, size_t name_id, SourceRef reference)
            {
                if (constant) { registers.write_payload(destination, active_owner->constants[slot].value); return; }
                const auto& name = active_owner->names[name_id];
                if (active_aliases[slot])
                    read_alias(destination, name_id, active_owner->source(reference));
                else registers.copy(destination, *active_locals, slot);
                if (registers.empty(destination))
                    machine.set_error("variable '" + name + "' has not been initialized", &active_owner->source(reference));
            };
            read_register(left_slot, site.left_constant, site.code.left, site.left_name, site.left_source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            read_register(right_slot, site.right_constant, site.code.right, site.right_name, site.right_source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            registers.binary_fallback(static_cast<Opcode>(site.code.opcode), left_slot, left_slot, right_slot, machine, *current_source, true);
#ifdef CIFA_VM_PROFILE
            register_binary_timer.select(6);
#endif
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (site.code.destination != 0)
            {
                const size_t slot = site.code.destination - 1;
                const auto& binding = active_owner->variable_sites[site.variable_site - 1];
                const auto& name = active_owner->names[binding.name_id];
                register_assignment_source = site.assignment_source;
                current_source = &active_owner->source(site.assignment_source);
                const auto* existing = binding.with_type && !scopes.empty() ? scopes.back().find(name) : nullptr;
                const auto* alias = !binding.with_type && active_aliases[slot] ? machine.find_slot(name) : nullptr;
                auto* target_file = binding.with_type && existing ? existing->file : alias ? alias->file : active_locals;
                const size_t target_slot = binding.with_type && existing ? existing->slot : alias ? alias->slot : slot;
                const bool direct = binding.with_type ? !scopes.empty() : !active_aliases[slot] || alias;
                if (direct)
                {
                    if (binding.with_type) machine.bind_type(*target_file, target_slot,
                        active_owner->names[binding.type_id], *current_source);
                    if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, left_slot, right_slot, *current_source);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    registers.clear(left_slot);
                    if (binding.with_type)
                    {
                        if (!existing) scopes.back().bind(name, active_locals, slot);
                        active_aliases[slot] = existing && (existing->file != active_locals || existing->slot != slot);
                    }
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target_file, target_slot);
                    continue;
                }
                auto& target = alias_target(binding.name_id);
                Object value;
                registers.export_argument(left_slot, value);
                machine.assign(target, std::move(value), binding.with_type,
                    active_owner->names[binding.type_id], active_owner->source(site.assignment_source));
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                bind_local_storage(name, slot, false);
                if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.import_object(output, target);
            }
            else registers.move(output, registers, left_slot);
            continue;
        }
        if (instruction.opcode == Opcode::LoadLocal)
        {
#ifdef CIFA_VM_PROFILE
            VmProfile::Timer local_load_timer(profile.local_load_ns);
#endif
            if (instruction.operand >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            auto& location = active_owner->source(instruction.source);
            const auto& name = active_owner->names[instruction.auxiliary];
            if (active_aliases[instruction.operand])
            {
                read_alias(output, instruction.auxiliary, location);
                if (registers.empty(output)) machine.set_error("variable '" + name + "' has not been initialized", &location);
            }
            else
            {
                if (active_locals->empty(instruction.operand))
                    machine.set_error("variable '" + name + "' has not been initialized", &location);
                registers.copy(output, *active_locals, instruction.operand);
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Load || instruction.opcode == Opcode::Peek)
        {
            auto& source = active_owner->source(instruction.source);
            if (instruction.auxiliary == 1)
            {
                const auto& name = active_owner->names[instruction.operand];
                Scope::Binding* slot_binding = nullptr;
                for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
                {
                    if (auto* found = scope->find(name))
                    {
                        slot_binding = found;
                        break;
                    }
                }
                if (slot_binding != nullptr && slot_binding->file != nullptr)
                {
                    if (instruction.opcode != Opcode::Peek
                        && slot_binding->file->empty(slot_binding->slot))
                        machine.set_error("variable '" + name + "' has not been initialized", &source);
                    registers.copy(output, *slot_binding->file, slot_binding->slot);
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    continue;
                }
                auto* linked = linked_global(instruction.operand);
                auto* existing = linked;
                auto& value = linked ? *linked : interpreter.global_variables[name];
                if (!linked) (*active_globals)[instruction.operand] = &value;
                if (instruction.opcode != Opcode::Peek && existing != nullptr && !value.hasValue())
                    machine.set_error("variable '" + name + "' has not been initialized", &source);
                registers.import_object(output, value);
            }
            else
            {
                const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
                const bool peek = instruction.opcode == Opcode::Peek;
                machine.read_named(registers, output, active_owner->names[site.name_id], active_owner->names[site.type_id],
                    site.with_type, peek, !peek, source);
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Size && instruction.auxiliary == 1)
        {
            const auto& name = active_owner->names[instruction.operand];
            const auto value = machine.named_value(name);
            const auto& location = active_owner->source(instruction.target_source);
            if (value.existed && value.empty())
                machine.set_error("variable '" + name + "' has not been initialized", &location);
            if (const auto size = value.size()) registers.write_payload(output, double(*size));
            else machine.set_error("function 'size' requires a string, array, or map", &location);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::MathUnary || instruction.opcode == Opcode::MathBinary)
        {
            const auto& call = active_owner->calls[instruction.operand];
            const auto& name = active_owner->names[call.name_id];
            if (instruction.opcode == Opcode::MathUnary)
            {
                const size_t argument = input_slot(instruction.input_count - 1);
                BytecodeValue::Storage numeric;
                const auto& input = registers.payload(argument, numeric);
                if (input.index() >= 1 && input.index() <= 3)
                {
                    const auto* integer = value_get_if<std::int64_t>(&input);
                    const double value = integer ? static_cast<double>(*integer)
                        : value_holds<double>(input) ? value_get<double>(input) : double(value_get<bool>(input));
                    if (call.math_kind == MathKind::Abs && integer && *integer != std::numeric_limits<std::int64_t>::min())
                    {
                        const auto result_value = *integer < 0 ? -*integer : *integer;
                        registers.clear(argument);
                        registers.write_payload(output, result_value);
                    }
                    else
                    {
                        registers.clear(argument);
                        double computed = 0;
                        switch (call.math_kind)
                        {
                        case MathKind::Abs: computed = std::fabs(value); break;
                        case MathKind::Sqrt: computed = std::sqrt(value); break;
                        case MathKind::Cbrt: computed = std::cbrt(value); break;
                        case MathKind::Round: computed = std::round(value); break;
                        case MathKind::Trunc: computed = std::trunc(value); break;
                        case MathKind::NearbyInt: computed = std::nearbyint(value); break;
                        case MathKind::Rint: computed = std::rint(value); break;
                        case MathKind::Ceil: computed = std::ceil(value); break;
                        case MathKind::Floor: computed = std::floor(value); break;
                        case MathKind::Sin: computed = std::sin(value); break;
                        case MathKind::Cos: computed = std::cos(value); break;
                        case MathKind::Tan: computed = std::tan(value); break;
                        case MathKind::Asin: computed = std::asin(value); break;
                        case MathKind::Acos: computed = std::acos(value); break;
                        case MathKind::Atan: computed = std::atan(value); break;
                        case MathKind::Sinh: computed = std::sinh(value); break;
                        case MathKind::Cosh: computed = std::cosh(value); break;
                        case MathKind::Tanh: computed = std::tanh(value); break;
                        case MathKind::Exp: computed = std::exp(value); break;
                        case MathKind::Log: computed = std::log(value); break;
                        case MathKind::Log2: computed = std::log2(value); break;
                        case MathKind::Log10: computed = std::log10(value); break;
                        case MathKind::Erf: computed = std::erf(value); break;
                        case MathKind::Erfc: computed = std::erfc(value); break;
                        case MathKind::TGamma: computed = std::tgamma(value); break;
                        case MathKind::LGamma: computed = std::lgamma(value); break;
                        default: continue;
                        }
                        registers.write_payload(output, computed);
                    }
                }
                else
                {
                    ObjectVector arguments;
                    arguments.emplace_back();
                    registers.export_argument(argument, arguments.back());
                    registers.import_object(output, machine.call_host(name, arguments, call.arguments));
                }
            }
            else
            {
                const size_t right_slot = input_slot(instruction.input_count - 1);
                const size_t left_slot = input_slot(instruction.input_count - 2);
                BytecodeValue::Storage right_numeric, left_numeric;
                const auto& right = registers.payload(right_slot, right_numeric);
                const auto& left = registers.payload(left_slot, left_numeric);
                if (left.index() >= 1 && left.index() <= 3 && right.index() >= 1 && right.index() <= 3)
                {
                    const auto number = [](const BytecodeValue::Storage& value) {
                        if (const auto* integer = value_get_if<std::int64_t>(&value)) return static_cast<double>(*integer);
                        if (const auto* floating = value_get_if<double>(&value)) return *floating;
                        return double(value_get<bool>(value));
                    };
                    const double first = number(left);
                    const double second = number(right);
                    registers.clear(left_slot);
                    registers.clear(right_slot);
                    double computed = 0;
                    switch (call.math_kind)
                    {
                    case MathKind::Atan2: computed = std::atan2(first, second); break;
                    case MathKind::Pow: computed = std::pow(first, second); break;
                    case MathKind::Hypot: computed = std::hypot(first, second); break;
                    case MathKind::Fmod: computed = std::fmod(first, second); break;
                    case MathKind::Remainder: computed = std::remainder(first, second); break;
                    case MathKind::CopySign: computed = std::copysign(first, second); break;
                    case MathKind::FDim: computed = std::fdim(first, second); break;
                    case MathKind::FMax: computed = std::fmax(first, second); break;
                    case MathKind::FMin: computed = std::fmin(first, second); break;
                    default: continue;
                    }
                    registers.write_payload(output, computed);
                }
                else
                {
                    ObjectVector arguments;
                    arguments.resize(2);
                    registers.export_argument(left_slot, arguments[0]);
                    registers.export_argument(right_slot, arguments[1]);
                    registers.import_object(output, machine.call_host(name, arguments, call.arguments));
                }
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Positive || instruction.opcode == Opcode::Negative
            || instruction.opcode == Opcode::LogicalNot || instruction.opcode == Opcode::BitNot
            || instruction.opcode == Opcode::Cast || instruction.opcode == Opcode::Size)
        {
            const size_t argument = input_slot(instruction.input_count - 1);
            BytecodeValue::Storage numeric;
            const auto& input = registers.payload(argument, numeric);
            if (instruction.opcode == Opcode::Cast)
            {
                machine.convert_type(registers, output, registers, argument, active_owner->names[instruction.operand],
                    active_owner->source(instruction.source));
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                if (output != argument) registers.clear(argument);
                continue;
            }
            if (instruction.opcode == Opcode::Positive && (input.index() >= 1 && input.index() <= 3))
            {
                if (const auto* boolean = value_get_if<bool>(&input))
                {
                    const auto number = std::int64_t(*boolean);
                    registers.clear(argument);
                    registers.write_payload(output, number);
                }
                else registers.move(output, registers, argument);
                continue;
            }
            if (instruction.opcode == Opcode::Negative && (input.index() >= 1 && input.index() <= 3))
            {
                BytecodeValue::Storage negative;
                if (const auto* floating = value_get_if<double>(&input)) negative = 0.0 - *floating;
                else
                {
                    const auto number = value_holds<std::int64_t>(input)
                        ? value_get<std::int64_t>(input) : std::int64_t(value_get<bool>(input));
                    negative = std::bit_cast<std::int64_t>(std::uint64_t(0) - static_cast<std::uint64_t>(number));
                }
                registers.clear(argument);
                registers.write_payload(output, std::move(negative));
                continue;
            }
            if (instruction.opcode == Opcode::Size)
            {
                const auto* container = value_get_if<std::any>(&input);
                std::optional<size_t> length;
                if (container)
                {
                    if (const auto* text = std::any_cast<std::string>(container)) length = text->size();
                    else if (const auto* array = std::any_cast<VmArray>(container)) length = array->values.size();
                    else if (const auto* map = std::any_cast<ObjectMap>(container)) length = map->size();
                }
                registers.clear(argument);
                if (length) registers.write_payload(output, double(*length));
                else
                {
                    const auto& location = instruction.target_source.id == 0
                        ? active_owner->source(instruction.source) : active_owner->source(instruction.target_source);
                    machine.set_error("function 'size' requires a string, array, or map", &location);
                    result = machine.error_result();
                    return true;
                }
                continue;
            }
            if (instruction.opcode == Opcode::LogicalNot && (input.index() >= 1 && input.index() <= 3))
            {
                const bool zero = value_holds<double>(input) ? value_get<double>(input) == 0
                    : value_holds<std::int64_t>(input) ? value_get<std::int64_t>(input) == 0
                    : !value_get<bool>(input);
                registers.clear(argument);
                registers.write_payload(output, zero);
                continue;
            }
            if (instruction.opcode == Opcode::BitNot
                && (value_holds<std::int64_t>(input) || value_holds<bool>(input)))
            {
                const auto number = value_holds<std::int64_t>(input)
                    ? value_get<std::int64_t>(input) : std::int64_t(value_get<bool>(input));
                registers.clear(argument);
                registers.write_payload(output, ~number);
                continue;
            }
            if (instruction.opcode == Opcode::Positive)
            {
                registers.move(output, registers, argument);
                continue;
            }
            if (instruction.opcode == Opcode::Negative)
            {
                const size_t zero = registers.size() - 1;
                registers.write_payload(zero, std::int64_t(0));
                registers.binary_fallback(Opcode::Subtract, output, zero, argument, machine, *current_source, false);
            }
            else if (instruction.opcode == Opcode::LogicalNot)
            {
                const bool inverted = !machine.condition(registers, argument, nullptr);
                registers.clear(argument);
                if (!machine.should_stop()) registers.write_payload(output, inverted);
            }
            else if (instruction.opcode == Opcode::BitNot)
            {
                std::int64_t integer = 0;
                if (!registers.integer(argument, integer)) machine.conversion_error(registers, argument, "int", nullptr);
                registers.clear(argument);
                if (!machine.should_stop()) registers.write_payload(output, ~integer);
            }
        }
        else
        {
            if (registers.binary(instruction.opcode, output, input_slot(instruction.input_count - 2), input_slot(instruction.input_count - 1),
                machine, *current_source))
            {
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                continue;
            }
            registers.binary_fallback(instruction.opcode, output, input_slot(instruction.input_count - 2),
                input_slot(instruction.input_count - 1), machine, *current_source, active_owner->host_function_version != 0);
        }
        if (machine.should_stop())
        {
            result = machine.error_result();
            return true;
        }
    }
    registers.export_argument(0, result);
    return true;
}

void CifaBytecode::Machine::publish(const std::shared_ptr<const Module>& module)
{
    for (const auto& [name, fields] : module->structures) structures[name] = fields;
    if (!module->function_code.empty())
    {
        function_cache.clear();
        ++function_version;
    }
    for (const auto& [name, overloads] : module->function_code)
        for (const auto& [arity, function] : overloads) functions[name][arity] = module;
}

const CifaBytecode::FunctionCode* CifaBytecode::Machine::find_function(const std::string& name, size_t arity,
    std::shared_ptr<const Module>& owner) const
{
    const auto functions_by_name = functions.find(name);
    if (functions_by_name == functions.end()) return nullptr;
    const auto function = functions_by_name->second.find(arity);
    if (function == functions_by_name->second.end()) return nullptr;
    owner = function->second;
    const auto compiled = owner->function_code.find(name);
    if (compiled == owner->function_code.end()) return nullptr;
    const auto overload = compiled->second.find(arity);
    return overload == compiled->second.end() ? nullptr : overload->second.get();
}

const CifaBytecode::FunctionCode* CifaBytecode::Machine::find_cached_function(const CallSite& call, const std::string& name,
    size_t arity, std::shared_ptr<const Module>& owner)
{
    if (const auto found = function_cache.find(&call); found != function_cache.end())
    {
        owner = found->second.owner;
        return found->second.code;
    }
    const auto* code = find_function(name, arity, owner);
    if (code != nullptr) function_cache.emplace(&call, CachedFunction{owner, code});
    return code;
}

void CifaBytecode::Machine::set_error(std::string message, const SourceLocation* location)
{
    if (!error.empty()) return;
    error = "Runtime Error: " + std::move(message) + "\n";
    std::vector<const SourceLocation*> frames;
    std::vector<bool> function_frames;
    auto diagnostic_frames = call_stack;
    if (append_diagnostic_frames) append_diagnostic_frames(diagnostic_frames);
    for (const auto& frame : diagnostic_frames)
    {
        frames.push_back(frame.first);
        function_frames.push_back(frame.second);
    }
    if (location != nullptr && (frames.empty() || frames.back() != location))
    {
        frames.push_back(location);
        function_frames.push_back(false);
    }
    if (frames.empty()) return;
    error += "Call Stack (most recent call first):\n";
    const SourceLocation* previous = nullptr;
    bool previous_function = false;
    for (size_t index = frames.size(); index > 0; --index)
    {
        const auto* frame = frames[index - 1];
        const bool function = function_frames[index - 1];
        if (frame == nullptr || (frame == previous && function == previous_function)) continue;
        previous = frame;
        previous_function = function;
        if (function)
        {
            error += "  at func " + (frame->str.empty() ? "<unknown>" : frame->str) + "()\n";
            continue;
        }
        const std::string formatted = format_frame(*frame);
        const size_t newline = formatted.find('\n');
        error += "  at " + (newline == std::string::npos ? formatted : formatted.substr(0, newline)) + "\n";
        if (newline != std::string::npos) error += "     " + formatted.substr(newline + 1) + "\n";
    }
}

CifaBytecode::Scope::Binding* CifaBytecode::Machine::find_slot(const std::string& name)
{
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
        if (auto* binding = scope->find(name)) return binding;
    return nullptr;
}

Object& CifaBytecode::Machine::resolve_member(const std::string& base_name, const std::string& field_name)
{
    const auto* binding = find_slot(base_name);
    if (binding)
    {
        auto* resource = value_get_if<std::any>(&binding->file->resource_payload(binding->slot));
        if (auto* fields = resource ? std::any_cast<ObjectMap>(resource) : nullptr)
        {
            auto& element = (*fields)[field_name];
            element.name = base_name + "." + field_name;
            return element;
        }
    }
    if (!binding)
    {
        const auto global = host.global_variables.find(base_name);
        if (global != host.global_variables.end() && global->second.isType<ObjectMap>())
        {
            auto& element = global->second.ref<ObjectMap>()[field_name];
            element.name = base_name + "." + field_name;
            return element;
        }
    }
    return host.global_variables[base_name + "::" + field_name];
}

bool CifaBytecode::Machine::convert_type(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
    const std::string& type_name, const SourceLocation& location)
{
    if (type_name.empty() || type_name == "auto") { destination.copy(target, source, slot); return true; }
    if (source.empty(slot))
    {
        set_error("cannot convert an empty value to '" + type_name + "'", &location);
        return false;
    }
    if (type_name == "void") { destination.clear(target); return true; }
    if (destination.cast_numeric(target, source, slot, type_name)) return true;
    BytecodeValue::Storage numeric;
    const auto& payload = source.payload(slot, numeric);
    const auto* resource = value_get_if<std::any>(&payload);
    const auto identity = resource ? std::type_index(resource->type()) : value_holds<double>(payload)
        ? std::type_index(typeid(double)) : value_holds<bool>(payload)
        ? std::type_index(typeid(bool)) : std::type_index(typeid(std::int64_t));
    const auto registered = host.registered_types.find(type_name);
    if (registered == host.registered_types.end())
    {
        if (structures.contains(type_name) && identity == typeid(ObjectMap))
        {
            destination.copy(target, source, slot);
            return true;
        }
        set_error("unknown conversion type '" + type_name + "'", &location);
        return false;
    }
    const auto target_type = registered->second.identity;
    if (target_type != identity && !(payload.index() >= 1 && payload.index() <= 3
        && (target_type == typeid(std::int64_t) || target_type == typeid(double) || target_type == typeid(bool))))
    {
        set_error("cannot convert value to '" + type_name + "'", &location);
        return false;
    }
    Object boundary;
    const size_t index = source.base() + slot;
    boundary.value = payload.export_storage();
    boundary.name = source.name_pool[source.slot_names[index]];
    boundary.argument_origin = source.origins[index];
    const auto& type = source.type_pool[source.slot_types[index]];
    boundary.bound_type = type.bound;
    boundary.declared_type_name = type.declared;
    boundary.element_type_name = type.element;
    boundary.type1 = type.special;
    if (source.bindings[index] != RegisterSlots::NumericBinding::None)
    {
        const bool integer = source.bindings[index] == RegisterSlots::NumericBinding::Int;
        boundary.bound_type = integer ? typeid(std::int64_t) : typeid(double);
        boundary.declared_type_name = integer ? "int" : "double";
        if (resource && std::any_cast<Object::NoValue>(resource)) boundary.type1 = "NoValue";
    }
    auto converted = registered->second.convert(boundary);
    if (should_stop()) return false;
    destination.import_object(target, std::move(converted));
    return true;
}

Object CifaBytecode::Machine::convert_type(const Object& source, const std::string& type_name, const SourceLocation& location)
{
    if (type_name.empty() || type_name == "auto") return source;
    if (!source.hasValue())
    {
        set_error("cannot convert an empty value to '" + type_name + "'", &location);
        return {};
    }
    if (type_name == "void") return {};
    const auto registered = host.registered_types.find(type_name);
    if (registered != host.registered_types.end())
    {
        const auto identity = registered->second.identity;
        const bool compatible = identity == source.getType() || (source.isNumber()
            && (identity == typeid(std::int64_t) || identity == typeid(double) || identity == typeid(bool)));
        if (!compatible)
        {
            set_error("cannot convert value to '" + type_name + "'", &location);
            return {};
        }
        return registered->second.convert(source);
    }
    if (structures.contains(type_name) && source.isType<ObjectMap>()) return source;
    set_error("unknown conversion type '" + type_name + "'", &location);
    return {};
}

bool CifaBytecode::Machine::bind_type(RegisterSlots& values, size_t slot, const std::string& type_name, const SourceLocation& location)
{
    const size_t index = values.base() + slot;
    auto descriptor = values.type_pool[values.slot_types[index]];
    descriptor.bound = typeid(void);
    descriptor.declared.clear();
    values.set_type(slot, descriptor);
    if (type_name.empty()) return true;
    if (type_name == "void") { set_error("variable cannot have type void", &location); return false; }
    descriptor.declared = type_name;
    if (type_name == "auto" && !values.empty(slot))
    {
        BytecodeValue::Storage numeric;
        const auto& payload = values.payload(slot, numeric);
        const auto* resource = value_get_if<std::any>(&payload);
        descriptor.bound = resource ? std::type_index(resource->type()) : value_holds<double>(payload)
            ? std::type_index(typeid(double)) : value_holds<bool>(payload)
            ? std::type_index(typeid(bool)) : std::type_index(typeid(std::int64_t));
        const auto name = host.type_names.find(descriptor.bound);
        descriptor.declared = name == host.type_names.end() ? descriptor.bound.name() : name->second;
    }
    else if (type_name != "auto")
    {
        const auto registered = host.registered_types.find(type_name);
        if (registered != host.registered_types.end()) descriptor.bound = registered->second.identity;
        else if (structures.contains(type_name)) descriptor.bound = typeid(ObjectMap);
        else { set_error("unknown type '" + type_name + "'", &location); return false; }
    }
    values.set_type(slot, descriptor);
    return true;
}

bool CifaBytecode::Machine::bind_type(Object& value, const std::string& type_name, const SourceLocation& location)
{
    value.bound_type = typeid(void);
    value.declared_type_name.clear();
    if (type_name.empty()) return true;
    if (type_name == "void") { set_error("variable cannot have type void", &location); return false; }
    if (type_name == "auto")
    {
        value.declared_type_name = "auto";
        if (value.hasValue())
        {
            value.bound_type = value.getType();
            value.declared_type_name = host.registered_type_name(value);
        }
        return true;
    }
    const auto registered = host.registered_types.find(type_name);
    if (registered == host.registered_types.end() && !structures.contains(type_name))
    {
        set_error("unknown type '" + type_name + "'", &location);
        return false;
    }
    value.declared_type_name = type_name;
    value.bound_type = registered != host.registered_types.end() ? registered->second.identity : typeid(ObjectMap);
    return true;
}

Object& CifaBytecode::Machine::assign_named(const std::string& name, const std::string& type_name, bool with_type,
    bool declare_current, const SourceLocation& location)
{
    auto& value = host.global_variables[name];
    if (with_type) bind_type(value, type_name, location);
    return value;
}

bool CifaBytecode::Machine::assign(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
    size_t scratch, const SourceLocation& location)
{
    const size_t index = destination.base() + target;
    auto type = destination.type_pool[destination.slot_types[index]];
    if (destination.bindings[index] != RegisterSlots::NumericBinding::None)
    {
        const bool integer = destination.bindings[index] == RegisterSlots::NumericBinding::Int;
        type.bound = integer ? typeid(std::int64_t) : typeid(double);
        type.declared = integer ? "int" : "double";
    }
    const std::string name = destination.name_pool[destination.slot_names[index]];
    BytecodeValue::Storage numeric;
    const auto& payload = source.payload(slot, numeric);
    const auto* resource = value_get_if<std::any>(&payload);
    const bool no_value = resource && std::any_cast<Object::NoValue>(resource);
    const bool registered = host.registered_types.contains(type.declared);
    const bool structure = structures.contains(type.declared);
    const bool builtin = type.declared == "int" || type.declared == "double" || type.declared == "float"
        || type.declared == "bool" || type.declared == "char" || type.declared == "string";
    size_t value_slot = slot;
    if (!no_value && !type.declared.empty() && type.declared != "auto" && (registered || structure || builtin))
    {
        if (!convert_type(source, scratch, source, slot, type.declared, location)) return false;
        value_slot = scratch;
    }
    const auto& converted = source.payload(value_slot, numeric);
    const auto* converted_resource = value_get_if<std::any>(&converted);
    const auto identity = converted_resource ? std::type_index(converted_resource->type()) : value_holds<double>(converted)
        ? std::type_index(typeid(double)) : value_holds<bool>(converted)
        ? std::type_index(typeid(bool)) : std::type_index(typeid(std::int64_t));
    if (!no_value && type.declared == "auto" && !source.empty(value_slot))
    {
        type.bound = identity;
        const auto registered_name = host.type_names.find(identity);
        type.declared = registered_name == host.type_names.end() ? identity.name() : registered_name->second;
    }
    else if (!no_value && !type.declared.empty() && type.bound != typeid(void) && !registered && !structure
        && !source.empty(value_slot) && type.bound != identity)
    {
        if (value_slot != slot) source.clear(value_slot);
        set_error("cannot change the type of a static variable", &location);
        return false;
    }
    destination.copy(target, source, value_slot);
    const size_t destination_index = destination.base() + target;
    auto assigned = destination.type_pool[destination.slot_types[destination_index]];
    assigned.bound = type.bound;
    assigned.declared = type.declared;
    destination.set_type(target, assigned);
    destination.set_name(target, name);
    if (value_slot != slot) source.clear(value_slot);
    return true;
}

bool CifaBytecode::Machine::assign(Object& target, Object value, bool with_type, const std::string& type_name,
    const SourceLocation& location)
{
    if (with_type && !bind_type(target, type_name, location)) return false;
    if (value.getSpecialType() == "NoValue")
    {
        const auto name = target.name;
        const auto bound_type = target.bound_type;
        const auto declared_type = target.declared_type_name;
        target = std::move(value);
        target.name = name;
        target.bound_type = bound_type;
        target.declared_type_name = declared_type;
        return true;
    }
    const bool registered = host.registered_types.contains(target.declared_type_name);
    const bool structure = structures.contains(target.declared_type_name);
    const bool builtin = target.declared_type_name == "int" || target.declared_type_name == "double"
        || target.declared_type_name == "float" || target.declared_type_name == "bool"
        || target.declared_type_name == "char" || target.declared_type_name == "string";
    if (!target.declared_type_name.empty() && target.declared_type_name != "auto" && (registered || structure || builtin))
    {
        value = convert_type(value, target.declared_type_name, location);
        if (should_stop()) return false;
    }
    if (target.declared_type_name == "auto" && value.hasValue() && value.getSpecialType() != "NoValue")
    {
        target.bound_type = value.getType();
        target.declared_type_name = host.registered_type_name(value);
    }
    else if (!target.declared_type_name.empty() && target.bound_type != typeid(void) && !registered && !structure
        && value.hasValue() && target.bound_type != value.getType())
    {
        set_error("cannot change the type of a static variable", &location);
        return false;
    }
    const auto name = target.name;
    const auto bound_type = target.bound_type;
    const auto declared_type = target.declared_type_name;
    target = std::move(value);
    target.name = name;
    target.bound_type = bound_type;
    target.declared_type_name = declared_type;
    return true;
}

void CifaBytecode::Machine::read_named(RegisterSlots& destination, size_t slot, const std::string& name, const std::string& type_name, bool with_type, bool only_check,
    bool initialize_struct, const SourceLocation& location)
{
    if (initialize_struct && with_type && structures.contains(type_name))
    {
        auto* existing_slot = find_slot(name);
        const auto global = host.global_variables.find(name);
        bool existing_map = false;
        if (existing_slot)
        {
            auto* resource = value_get_if<std::any>(&existing_slot->file->resource_payload(existing_slot->slot));
            existing_map = resource && std::any_cast<ObjectMap>(resource);
        }
        else existing_map = global != host.global_variables.end() && global->second.isType<ObjectMap>();
        if (!existing_map)
        {
            ObjectMap fields;
            for (const auto& field : structures.at(type_name))
            {
                Object field_value;
                bind_type(field_value, field.type_name, location);
                field_value.name = name + "." + field.name;
                fields.emplace(field.name, std::move(field_value));
            }
            if (!scopes.empty())
            {
                auto& binding = scopes.back().create(name);
                binding.file->write_payload(binding.slot, std::any(std::move(fields)));
                binding.file->set_type(binding.slot, {typeid(ObjectMap), type_name, "", ""});
                binding.file->set_name(binding.slot, name);
                destination.copy(slot, *binding.file, binding.slot);
                return;
            }
            auto& value = assign_named(name, type_name, true, true, location);
            value = Object(std::move(fields));
            value.declared_type_name = type_name;
            value.bound_type = typeid(ObjectMap);
            value.name = name;
            destination.import_object(slot, value);
            return;
        }
    }
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
    {
        if (auto* binding = scope->find(name))
        {
            auto& values = *binding->file;
            if (with_type && !bind_type(values, binding->slot, type_name, location)) return;
            if (!only_check && !with_type && values.empty(binding->slot))
                set_error("variable '" + name + "' has not been initialized", &location);
            destination.copy(slot, values, binding->slot);
            return;
        }
    }
    if (!host.global_variables.contains(name) && !scopes.empty())
    {
        auto& binding = scopes.back().create(name);
        if (with_type && !bind_type(*binding.file, binding.slot, type_name, location)) return;
        destination.copy(slot, *binding.file, binding.slot);
        return;
    }
    const bool existed = host.global_variables.contains(name);
    auto& value = host.global_variables[name];
    if (with_type) bind_type(value, type_name, location);
    if (!only_check && existed && !with_type && !value.hasValue()) set_error("variable '" + name + "' has not been initialized", &location);
    destination.import_object(slot, value);
}

void CifaBytecode::Machine::set_no_value_error(const Object& value, const SourceLocation* location)
{
    const auto* object = value_get_if<std::any>(&value.value);
    const auto* no_value = object == nullptr ? nullptr : std::any_cast<Object::NoValue>(object);
    set_no_value_error(no_value, location);
}

void CifaBytecode::Machine::set_no_value_error(const Object::NoValue* no_value, const SourceLocation* location)
{
    if (!error.empty()) return;
    const std::string name = no_value == nullptr ? "<unknown>" : no_value->function_name;
    const std::string origin = no_value == nullptr ? "" : no_value->call_frame;
    error = "Runtime Error: function '" + name + "' has no return value\nNo return value originated at:\n" + origin;
    std::vector<std::pair<const SourceLocation*, bool>> frames = call_stack;
    if (append_diagnostic_frames) append_diagnostic_frames(frames);
    if (location != nullptr && format_frame(*location) != origin
        && (frames.empty() || frames.back().first != location)) frames.emplace_back(location, false);
    if (frames.empty()) return;
    error += "\nCall Stack (most recent call first):\n";
    const SourceLocation* previous = nullptr;
    bool previous_function = false;
    for (size_t index = frames.size(); index > 0; --index)
    {
        const auto* frame = frames[index - 1].first;
        const bool function = frames[index - 1].second;
        if (frame == nullptr || (frame == previous && function == previous_function)) continue;
        previous = frame;
        previous_function = function;
        if (function)
        {
            error += "  at func " + (frame->str.empty() ? "<unknown>" : frame->str) + "()\n";
            continue;
        }
        const std::string formatted = format_frame(*frame);
        const size_t newline = formatted.find('\n');
        error += "  at " + (newline == std::string::npos ? formatted : formatted.substr(0, newline)) + "\n";
        if (newline != std::string::npos) error += "     " + formatted.substr(newline + 1) + "\n";
    }
}

bool CifaBytecode::Machine::condition(RegisterSlots& source, size_t slot, const SourceLocation* location)
{
    std::int64_t integer = 0;
    double floating = 0;
    bool is_double = false;
    if (source.number(slot, integer, floating, is_double)) return is_double ? floating != 0 : integer != 0;
    if (location != nullptr && source.empty(slot))
    {
        set_error("condition requires a value", location);
        return false;
    }
    conversion_error(source, slot, "bool", location);
    return false;
}

void CifaBytecode::Machine::conversion_error(RegisterSlots& source, size_t slot, const std::string& target, const SourceLocation* location)
{
    const size_t index = source.base() + slot;
    const auto& name = source.name_pool[source.slot_names[index]];
    BytecodeValue::Storage numeric;
    const auto& value = source.payload(slot, numeric);
    const auto* resource = value_get_if<std::any>(&value);
    if (const auto* no_value = resource ? std::any_cast<Object::NoValue>(resource) : nullptr)
    {
        set_no_value_error(no_value, location);
        return;
    }
    const std::string type = source.empty(slot) ? "<empty>" : resource ? resource->type().name()
        : value_holds<double>(value) ? typeid(double).name()
        : value_holds<bool>(value) ? typeid(bool).name() : typeid(std::int64_t).name();
    set_error("type conversion failed: variable '" + (name.empty() ? std::string("<temporary>") : name)
        + "' from " + type + " to " + target, location);
}

std::string CifaBytecode::Machine::string_value(RegisterSlots& source, size_t slot)
{
    BytecodeValue::Storage numeric;
    const auto& value = source.payload(slot, numeric);
    const auto* resource = value_get_if<std::any>(&value);
    if (const auto* text = resource ? std::any_cast<std::string>(resource) : nullptr) return *text;
    conversion_error(source, slot, "string", nullptr);
    return {};
}

bool CifaBytecode::Machine::range(RegisterSlots& source, size_t slot, const SourceLocation& location,
    RegisterSlots& destination, size_t target)
{
    const auto* payload = value_get_if<std::any>(&source.resource_payload(slot));
    if (const auto* no_value = payload ? std::any_cast<Object::NoValue>(payload) : nullptr)
    {
        set_no_value_error(no_value, &location);
        return false;
    }
    const auto* elements = payload ? std::any_cast<VmArray>(payload) : nullptr;
    if (!elements) { set_error("range for requires an array", &location); return false; }
    destination.write_payload(target, std::any(*elements));
    return true;
}

bool CifaBytecode::Machine::bind_range(RegisterSlots& values, size_t slot, const std::string& name, const std::string& type_name, const SourceLocation& location)
{
    if (!bind_type(values, slot, type_name, location)) return false;
    values.set_name(slot, name);
    return true;
}

void CifaBytecode::Machine::read_indexed(RegisterSlots& destination, size_t slot,
    const IndexedValueRef& element, bool map_access)
{
    if (element.compact)
    {
        if (element.compact->empty() && !map_access)
            set_error("array element '" + element.name + "' has not been initialized");
        destination.store_payload(slot, *element.compact);
        destination.set_name(slot, element.name);
        return;
    }
    if (!element.object) { destination.clear(slot); return; }
    const auto& object = *element.object;
    if (!map_access && !object.hasValue()) set_error("array element '" + object.name + "' has not been initialized");
    if (object.bound_type == typeid(void) && object.declared_type_name.empty()
        && object.element_type_name.empty() && object.type1.empty()
        && (value_holds<std::int64_t>(object.value)
            || value_holds<double>(object.value) || value_holds<bool>(object.value)))
    {
        destination.store_payload(slot, object.value);
        destination.set_name(slot, object.name);
        destination.origins[destination.base() + slot] = object.argument_origin;
        return;
    }
    destination.import_object(slot, object);
}

bool CifaBytecode::Machine::assign_indexed(const IndexedValueRef& target, Object value, bool with_type,
    const std::string& type_name, const SourceLocation& location)
{
    if (target.object) return assign(*target.object, std::move(value), with_type, type_name, location);
    if (!target.compact) return false;
    const auto& target_type = !target.element_type.empty() ? target.element_type : type_name;
    if (!target_type.empty())
    {
        Object converted;
        if (!assign(converted, std::move(value), true, target_type, location)) return false;
        *target.compact = CompactValue(std::move(converted.value));
    }
    else *target.compact = CompactValue(std::move(value.value));
    return true;
}

CifaBytecode::Machine::IndexedValueRef CifaBytecode::Machine::indexed(const std::string& name, const std::string& type_name,
    size_t dimensions, bool is_decl_array,
    bool only_check, bool declare_current, RegisterSlots& indices, const size_t* index_slots)
{
    const auto integer_key = [&](size_t dimension)
    {
        std::int64_t value;
        if (indices.integer(index_slots[dimension], value)) return value;
        conversion_error(indices, index_slots[dimension], "int", nullptr);
        return std::int64_t(0);
    };
    const auto string_key = [&](size_t dimension)
    {
        if (dimension >= dimensions) return false;
        const auto* payload = value_get_if<std::any>(&indices.resource_payload(index_slots[dimension]));
        return payload != nullptr && std::any_cast<std::string>(payload) != nullptr;
    };
    Object* current = nullptr;
    CompactValue* compact = nullptr;
    std::string compact_element_type;
    size_t first_dimension = 0;
    auto* binding = !is_decl_array && dimensions != 0 ? find_slot(name) : nullptr;
    if (binding && (!declare_current || (!scopes.empty() && scopes.back().find(name) == binding)))
    {
        auto* resource = value_get_if<std::any>(&binding->file->resource_payload(binding->slot));
        if (auto* fields = resource ? std::any_cast<ObjectMap>(resource) : nullptr)
            current = &(*fields)[string_value(indices, index_slots[0])];
        else if (auto* elements = resource ? std::any_cast<VmArray>(resource) : nullptr)
        {
            const auto offset = integer_key(0);
            if (offset < 0) { set_error("array index is out of range"); return {}; }
            const size_t index = static_cast<size_t>(offset);
            if (index >= elements->values.size())
            {
                const size_t absolute = binding->file->base() + binding->slot;
                compact_element_type = binding->file->type_pool[binding->file->slot_types[absolute]].element;
                elements->values.resize(index + 1);
            }
            else
            {
                const size_t absolute = binding->file->base() + binding->slot;
                compact_element_type = binding->file->type_pool[binding->file->slot_types[absolute]].element;
            }
            compact = &elements->values[index];
        }
        if (current || compact) first_dimension = 1;
    }
    if (!current && !compact)
    {
    if (binding && dimensions != 0)
    {
        auto& file = *binding->file;
        const size_t slot = binding->slot;
        if (string_key(0))
        {
            file.write_payload(slot, std::any(ObjectMap{}));
            auto& values = *std::any_cast<ObjectMap>(&value_get<std::any>(file.resource_payload(slot)));
            current = &values[string_value(indices, index_slots[0])];
        }
        else
        {
            const auto offset = integer_key(0);
            if (offset < 0) { set_error("array index is out of range"); return {}; }
            file.write_payload(slot, std::any(VmArray(static_cast<size_t>(offset) + 1)));
            auto& values = std::any_cast<VmArray&>(value_get<std::any>(file.resource_payload(slot))).values;
            const size_t absolute = file.base() + slot;
            compact_element_type = file.type_pool[file.slot_types[absolute]].element;
            compact = &values[static_cast<size_t>(offset)];
        }
        first_dimension = 1;
    }
    if (!current && !compact)
    {
    auto& base = host.global_variables[name];
    if (is_decl_array)
    {
        const auto requested = dimensions == 0 || value_holds<std::monostate>(indices.payload(index_slots[0]))
            ? 0 : integer_key(0);
        const size_t size = requested < 0 ? 0 : static_cast<size_t>(requested);
        if (!base.isType<ObjectVector>()) base = Object(ObjectVector(size));
        else if (!only_check) base.ref<ObjectVector>().resize(size);
        base.name = name;
        base.element_type_name = type_name;
        for (auto& element : base.ref<ObjectVector>())
            if (!element.isTyped())
            {
                bind_type(element, type_name, SourceLocation{});
                element.name = name;
            }
        return {nullptr, &base, name, type_name};
    }
    if (!base.isType<ObjectVector>() && !base.isType<ObjectMap>())
    {
        base = string_key(0) ? Object(ObjectMap{}) : Object(ObjectVector{});
    }
    current = &base;
    }
    }
    for (size_t index = first_dimension; compact && index < dimensions; ++index)
    {
        auto* values = compact->resource<VmArray>();
        if (!values)
        {
            *compact = CompactValue(std::any(VmArray{}));
            values = compact->resource<VmArray>();
        }
        const auto offset = integer_key(index);
        if (offset < 0) { set_error("array index is out of range"); return {compact, nullptr, name, compact_element_type}; }
        if (static_cast<size_t>(offset) >= values->values.size()) values->values.resize(static_cast<size_t>(offset) + 1);
        compact = &values->values[static_cast<size_t>(offset)];
    }
    for (size_t index = first_dimension; !compact && index < dimensions; ++index)
    {
        if (!current->isType<ObjectVector>() && !current->isType<ObjectMap>())
        {
            const auto element_type = current->declared_type_name;
            *current = string_key(index) ? Object(ObjectMap{}) : Object(ObjectVector{});
            current->element_type_name = element_type;
        }
        if (current->isType<ObjectMap>())
        {
            const auto* payload = value_get_if<std::any>(&indices.resource_payload(index_slots[index]));
            const auto* key = payload ? std::any_cast<std::string>(payload) : nullptr;
            current = &current->ref<ObjectMap>()[key ? *key : string_value(indices, index_slots[index])];
        }
        else
        {
            const auto offset = integer_key(index);
            if (offset < 0) { set_error("array index is out of range"); return {nullptr, current, name, type_name}; }
            auto& values = current->ref<ObjectVector>();
            if (static_cast<size_t>(offset) >= values.size())
            {
                const auto old_size = values.size();
                values.resize(static_cast<size_t>(offset) + 1);
                for (size_t item = old_size; item < values.size(); ++item)
                    if (!current->element_type_name.empty()) bind_type(values[item], current->element_type_name, SourceLocation{});
            }
            current = &values[static_cast<size_t>(offset)];
        }
    }
    if (current) current->name = name;
    return {compact, current, name, compact_element_type.empty() ? type_name : compact_element_type};
}

Object CifaBytecode::Machine::make_no_value(const std::string& function_name, const SourceLocation& call_site) const
{
    return Object::make_no_value(function_name, format_frame(call_site));
}

Object CifaBytecode::Machine::call_host(const std::string& name, ObjectVector& arguments,
    const std::vector<SourceLocation>& locations)
{
    if (name == "exit")
    {
        exit_requested = true;
        return {};
    }
    if (name == "type" && !arguments.empty() && structures.contains(arguments.front().declared_type_name))
        return Object(arguments.front().declared_type_name);
    const auto function = host.functions.find(name);
    if (function == host.functions.end())
    {
        set_error("function '" + name + "' is not defined");
        return {};
    }
    auto earlier_errors = host.errors;
    Object::set_runtime_error_reporter([this, &locations](const std::string& message, const Object* source)
        {
            const auto* location = locations.empty() ? nullptr : &locations.front();
            if (source != nullptr && source->getSpecialType() == "NoValue") set_no_value_error(*source, location);
            else set_error(message, location);
            host.set_runtime_error(message, source);
        });
    Object result = function->second(arguments);
    Object::clear_runtime_error_reporter();
    earlier_errors.insert(host.errors.begin(), host.errors.end());
    host.errors = std::move(earlier_errors);
    const auto host_runtime_error = host.get_runtime_error();
    if (host.has_runtime_error() && error.empty())
    {
        const std::string prefix = "Runtime Error: ";
        const auto message = host_runtime_error.starts_with(prefix)
            ? host_runtime_error.substr(prefix.size(), host_runtime_error.find('\n') - prefix.size())
            : host_runtime_error;
        set_error(message, locations.empty() ? nullptr : &locations.front());
    }
    host.clear_runtime_error();
    host.last_exit_requested = false;
    if (result.getSpecialType() == "Error" && result.toString() == "RuntimeError")
    {
        set_error(result.toString());
        return {};
    }
    return result;
}

bool CifaBytecode::Machine::call_builtin_math(const std::string& name, const ObjectVector& arguments, Object& result) const
{
    const auto builtin = host.builtin_function_generations.find(name);
    const auto generation = host.function_generations.find(name);
    if (builtin == host.builtin_function_generations.end() || generation == host.function_generations.end()
        || builtin->second != generation->second) return false;
    if (name == "abs" && arguments.size() == 1 && arguments[0].isNumber())
    {
        if (arguments[0].isInteger())
        {
            const auto integer = arguments[0].toInt64();
            if (integer == std::numeric_limits<std::int64_t>::min()) return false;
            result = Object(integer < 0 ? -integer : integer);
        }
        else result = Object(std::fabs(arguments[0].toDouble()));
        return true;
    }
    if (arguments.size() == 1 && arguments[0].isNumber())
    {
        const double input = arguments[0].toDouble();
        if (name == "sqrt") result = Object(std::sqrt(input));
        else if (name == "cbrt") result = Object(std::cbrt(input));
        else if (name == "round") result = Object(std::round(input));
        else if (name == "trunc") result = Object(std::trunc(input));
        else if (name == "nearbyint") result = Object(std::nearbyint(input));
        else if (name == "rint") result = Object(std::rint(input));
        else if (name == "ceil") result = Object(std::ceil(input));
        else if (name == "floor") result = Object(std::floor(input));
        else if (name == "sin") result = Object(std::sin(input));
        else if (name == "cos") result = Object(std::cos(input));
        else if (name == "tan") result = Object(std::tan(input));
        else if (name == "asin") result = Object(std::asin(input));
        else if (name == "acos") result = Object(std::acos(input));
        else if (name == "atan") result = Object(std::atan(input));
        else if (name == "sinh") result = Object(std::sinh(input));
        else if (name == "cosh") result = Object(std::cosh(input));
        else if (name == "tanh") result = Object(std::tanh(input));
        else if (name == "exp") result = Object(std::exp(input));
        else if (name == "log") result = Object(std::log(input));
        else if (name == "log2") result = Object(std::log2(input));
        else if (name == "log10") result = Object(std::log10(input));
        else if (name == "erf") result = Object(std::erf(input));
        else if (name == "erfc") result = Object(std::erfc(input));
        else if (name == "tgamma") result = Object(std::tgamma(input));
        else if (name == "lgamma") result = Object(std::lgamma(input));
        else return false;
        return true;
    }
    if (arguments.size() == 2 && arguments[0].isNumber() && arguments[1].isNumber())
    {
        const double left = arguments[0].toDouble();
        const double right = arguments[1].toDouble();
        if (name == "atan2") result = Object(std::atan2(left, right));
        else if (name == "pow") result = Object(std::pow(left, right));
        else if (name == "hypot") result = Object(std::hypot(left, right));
        else if (name == "fmod") result = Object(std::fmod(left, right));
        else if (name == "remainder") result = Object(std::remainder(left, right));
        else if (name == "copysign") result = Object(std::copysign(left, right));
        else if (name == "fdim") result = Object(std::fdim(left, right));
        else if (name == "fmax") result = Object(std::fmax(left, right));
        else if (name == "fmin") result = Object(std::fmin(left, right));
        else return false;
        return true;
    }
    return false;
}

CifaBytecode::Machine::NamedValueRef CifaBytecode::Machine::named_value(const std::string& name)
{
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
    {
        if (auto* binding = scope->find(name))
        {
            auto& file = *binding->file;
            return {&file, binding->slot, nullptr,
                file.type_pool[file.slot_types[file.base() + binding->slot]].element, true};
        }
    }
    const auto global = host.global_variables.find(name);
    if (global != host.global_variables.end())
    {
        return {nullptr, 0, &global->second, global->second.element_type_name, true};
    }
    if (!scopes.empty())
    {
        auto& binding = scopes.back().create(name);
        return {binding.file, binding.slot, nullptr, {}, false};
    }
    return {nullptr, 0, &host.global_variables[name], {}, false};
}

void CifaBytecode::Machine::call_method(RegisterSlots& destination, size_t slot, const std::string& name, const SourceLocation& location,
    NamedValueRef& receiver, const std::vector<SourceLocation>& locations, RegisterSlots& arguments)
{
    destination.clear(slot);
    const auto integer_argument = [&](size_t index)
    {
        std::int64_t value;
        if (arguments.integer(index, value)) return value;
        conversion_error(arguments, index, "int", nullptr);
        return std::int64_t(0);
    };
    auto* container = receiver.resource();
    if (auto* array = receiver.global && container ? std::any_cast<ObjectVector>(container) : nullptr)
    {
        auto& values = *array;
        if (name == "push_back")
        {
            for (size_t index = 0; index < locations.size(); ++index)
            {
                Object value;
                arguments.export_argument(index, value);
                if (!receiver.element_type.empty()) value = convert_type(value, receiver.element_type, locations[index]);
                if (should_stop()) return;
                values.push_back(std::move(value));
            }
        }
        else if (name == "pop_back") { if (!values.empty()) values.pop_back(); }
        else if (name == "resize") { if (!locations.empty()) values.resize(static_cast<size_t>(integer_argument(0))); }
        else if (name == "insert" && locations.size() >= 2)
        {
            const auto requested = integer_argument(0);
            const auto position = static_cast<size_t>(std::clamp<int64_t>(requested, 0, static_cast<int64_t>(values.size())));
            Object value;
            arguments.export_argument(1, value);
            if (!receiver.element_type.empty()) value = convert_type(value, receiver.element_type, locations[1]);
            if (should_stop()) return;
            values.insert(values.begin() + position, std::move(value));
        }
        else if (name == "erase" && !locations.empty())
        {
            const auto index = integer_argument(0);
            if (index >= 0 && static_cast<size_t>(index) < values.size()) values.erase(values.begin() + index);
        }
        else if (name == "clear") values.clear();
        else if (name == "contains")
        {
            if (!locations.empty())
            {
                Object sought;
                arguments.export_argument(0, sought);
                for (const auto& value : values)
                    if (host.equal(value, sought).toBool()) { destination.write_payload(slot, 1.0); return; }
            }
            destination.write_payload(slot, 0.0);
            return;
        }
        else if (name == "keys") { set_error("keys() is not supported on arrays", &location); return; }
        else if (name != "push_back" && name != "pop_back" && name != "resize"
            && name != "insert" && name != "erase" && name != "clear")
        { set_error(name + "() is not supported on arrays", &location); return; }
        destination.write_payload(slot, double(values.size()));
        return;
    }
    if (auto* array = container ? std::any_cast<VmArray>(container) : nullptr)
    {
        auto& values = array->values;
        if (name == "push_back")
        {
            for (size_t index = 0; index < locations.size(); ++index)
            {
                if (receiver.element_type.empty())
                {
                    values.push_back(std::move(arguments.resource_payload(index)));
                    arguments.clear(index);
                }
                else
                {
                    convert_type(destination, slot, arguments, index, receiver.element_type, locations[index]);
                    if (should_stop()) return;
                    Object value;
                    destination.export_argument(slot, value);
                    values.emplace_back(std::move(value.value));
                }
            }
            destination.write_payload(slot, double(values.size()));
            return;
        }
        if (name == "pop_back")
        {
            if (!values.empty()) values.pop_back();
            destination.write_payload(slot, double(values.size()));
            return;
        }
        if (name == "resize")
        {
            if (!locations.empty()) values.resize(static_cast<size_t>(integer_argument(0)));
            destination.write_payload(slot, double(values.size()));
            return;
        }
        if (name == "insert")
        {
            if (locations.size() >= 2)
            {
                const auto requested = integer_argument(0);
                const auto position = static_cast<size_t>(std::clamp<int64_t>(requested, 0, static_cast<int64_t>(values.size())));
                if (!receiver.element_type.empty()) convert_type(arguments, 1, arguments, 1, receiver.element_type, locations[1]);
                if (should_stop()) return;
                values.insert(values.begin() + position, std::move(arguments.resource_payload(1)));
                arguments.clear(1);
            }
            destination.write_payload(slot, double(values.size()));
            return;
        }
        if (name == "erase")
        {
            if (!locations.empty())
            {
                const auto index = integer_argument(0);
                if (index >= 0 && static_cast<size_t>(index) < values.size()) values.erase(values.begin() + index);
            }
            destination.write_payload(slot, double(values.size()));
            return;
        }
        if (name == "clear") { values.clear(); destination.write_payload(slot, 0.0); return; }
        if (name == "contains")
        {
            if (!locations.empty())
            {
                for (const auto& value : values)
                {
                    BytecodeValue::Storage numeric;
                    if (destination.binary_payloads(Opcode::Equal, slot, value,
                        arguments.payload(0, numeric), *this, location))
                    {
                        if (should_stop()) return;
                        if (condition(destination, slot, nullptr)) { destination.write_payload(slot, 1.0); return; }
                    }
                    else
                    {
                        destination.copy(slot, arguments, 0);
                        Object sought;
                        destination.export_argument(slot, sought);
                        Object candidate;
                        candidate.value = value.export_storage();
                        destination.import_object(slot, host.equal(candidate, sought));
                        if (should_stop()) return;
                        if (condition(destination, slot, nullptr)) { destination.write_payload(slot, 1.0); return; }
                    }
                    if (should_stop()) return;
                }
            }
            destination.write_payload(slot, 0.0);
            return;
        }
        if (name == "keys") set_error("keys() is not supported on arrays", &location);
        else set_error(name + "() is not supported on arrays", &location);
        return;
    }
    if (auto* map = container ? std::any_cast<ObjectMap>(container) : nullptr)
    {
        auto& values = *map;
        if (name == "erase")
        {
            if (!locations.empty()) values.erase(string_value(arguments, 0));
            destination.write_payload(slot, double(values.size()));
            return;
        }
        if (name == "clear") { values.clear(); destination.write_payload(slot, 0.0); return; }
        if (name == "contains")
        {
            destination.write_payload(slot, !locations.empty() && values.contains(string_value(arguments, 0)));
            return;
        }
        if (name == "keys")
        {
            ObjectVector keys;
            for (const auto& [key, value] : values) keys.emplace_back(key);
            destination.write_payload(slot, std::any(std::move(keys)));
            return;
        }
        set_error(name + "() is not supported on maps", &location);
        return;
    }
    set_error(name + "() requires an array or map", &location);
}

Object CifaBytecode::run_module(Machine& machine, const Module& module)
{
    machine.publish(std::shared_ptr<const Module>(&module, [](const Module*) { }));
    Object result;
    execute_instructions(machine, module, module.root_instructions, result);
    return !machine.error.empty() ? machine.error_result() : result;
}

CifaBytecode::Session::Session(CifaBytecode& interpreter) : machine(std::make_unique<Machine>(interpreter))
{
}

CifaBytecode::Session::~Session() = default;

Object CifaBytecode::Session::run(CifaBytecode& code, const std::string& entry_label)
{
    code.runtime_error.clear();
    auto& vm = *machine;
    const bool nested = active;
    auto saved_scopes = std::move(vm.scopes);
    auto saved_returns = std::move(vm.returns);
    auto saved_call_stack = std::move(vm.call_stack);
    auto saved_append_diagnostic_frames = std::move(vm.append_diagnostic_frames);
    auto saved_error = std::move(vm.error);
    const bool saved_exit_requested = vm.exit_requested;
    vm.scopes.clear();
    vm.returns.clear();
    vm.call_stack.clear();
    vm.append_diagnostic_frames = {};
    vm.error.clear();
    vm.exit_requested = false;
    active = true;
    struct RestoreNestedState
    {
        Session& session;
        Machine& machine;
        bool nested;
        ScopeStack scopes;
        std::vector<Machine::ReturnState> returns;
        std::vector<std::pair<const SourceLocation*, bool>> call_stack;
        std::function<void(std::vector<std::pair<const SourceLocation*, bool>>&)> append_diagnostic_frames;
        std::string error;
        bool exit_requested;
        ~RestoreNestedState()
        {
            if (!nested)
            {
                session.active = false;
                return;
            }
            machine.scopes = std::move(scopes);
            machine.returns = std::move(returns);
            machine.call_stack = std::move(call_stack);
            machine.append_diagnostic_frames = std::move(append_diagnostic_frames);
            machine.error = std::move(error);
            machine.exit_requested = exit_requested;
        }
    } restore{*this, vm, nested, std::move(saved_scopes), std::move(saved_returns), std::move(saved_call_stack),
        std::move(saved_append_diagnostic_frames), std::move(saved_error), saved_exit_requested};
    if (!code.valid())
    {
        vm.set_error(code.translation_error.empty() ? "cannot run an invalid bytecode module" : code.translation_error);
        code.runtime_error = vm.error;
        return vm.error_result();
    }
    if (code.module_data->host_function_version != 0 && code.module_data->host_function_version != vm.host.function_version)
    {
        vm.set_error("host functions changed after optimized bytecode compilation");
        code.runtime_error = vm.error;
        return vm.error_result();
    }
    if (code.module_data->freeze_script_functions && vm.frozen_function_module != code.module_data.get()
        && code.module_data->script_function_version != vm.function_version)
    {
        vm.set_error("script functions changed after optimized bytecode compilation");
        code.runtime_error = vm.error;
        return vm.error_result();
    }
    if (nested && code.module_data->freeze_script_functions && !code.module_data->function_code.empty())
    {
        vm.set_error("script functions changed during optimized bytecode execution");
        code.runtime_error = vm.error;
        return vm.error_result();
    }
    size_t start = 0;
    if (!entry_label.empty())
    {
        const auto entry = code.entry_labels.find(entry_label);
        if (entry == code.entry_labels.end())
        {
            vm.set_error("bytecode entry label '" + entry_label + "' is not defined");
            code.runtime_error = vm.error;
            return vm.error_result();
        }
        start = entry->second == 0 ? 0 : code.root_entries.at(entry->second);
    }
    vm.publish(code.module_data);
    if (code.module_data->freeze_script_functions)
    {
        code.module_data->script_function_version = vm.function_version;
        vm.frozen_function_module = code.module_data.get();
    }
    Object result;
    execute_instructions(vm, *code.module_data, code.root_instructions, result, start);
    code.runtime_error = vm.error;
    return !vm.error.empty() ? vm.error_result() : result;
}

std::vector<size_t> CifaBytecode::Session::function_arities(const std::string& name) const
{
    std::vector<size_t> arities;
    const auto found = machine->functions.find(name);
    if (found == machine->functions.end()) return arities;
    arities.reserve(found->second.size());
    for (const auto& [arity, module] : found->second) arities.push_back(arity);
    return arities;
}

const std::vector<StructField>* CifaBytecode::Session::find_structure(const std::string& name) const
{
    const auto found = machine->structures.find(name);
    return found == machine->structures.end() ? nullptr : &found->second;
}

void CifaBytecode::Session::copy_catalog(std::unordered_map<std::string, FunctionOverloads>& functions,
    std::unordered_map<std::string, std::vector<StructField>>& structures) const
{
    functions.clear();
    for (const auto& [name, overloads] : machine->functions)
    {
        auto& signatures = functions[name];
        for (const auto& [arity, module] : overloads) signatures.emplace(arity, Function2{});
    }
    structures = machine->structures;
}

bool CifaBytecode::Session::is_exit_requested() const
{
    return machine->exit_requested;
}

size_t CifaBytecode::Session::script_function_version() const
{
    return machine->function_version;
}

bool CifaBytecode::compile_script(std::string script)
{
    if (compiled_valid || !translation_error.empty()) return false;
    prepare_compile_visibility();
    const bool parsed = compile_script_internal(std::move(script));
    clear_compile_visibility();
    if (parsed) translate(*this, session->script_function_version());
    return valid() && !has_error();
}

bool CifaBytecode::compile_file(const std::string& filename)
{
    if (compiled_valid || !translation_error.empty()) return false;
    prepare_compile_visibility();
    const bool parsed = compile_file_internal(filename);
    clear_compile_visibility();
    if (parsed) translate(*this, session->script_function_version());
    return valid() && !has_error();
}

Object CifaBytecode::run(const std::string& entry_label)
{
    return session->run(*this, entry_label);
}

bool CifaBytecode::is_exit_requested() const
{
    return session->is_exit_requested();
}

Object CifaBytecode::run_script(std::string script)
{
    runtime_error.clear();
    if (!compiled_valid && translation_error.empty())
    {
        if (!compile_script(std::move(script))) return Object("", "Error");
        return run();
    }
    auto nested = std::make_unique<CifaBytecode>();
    nested->set_output_error(false);
    nested->set_optimization_enabled(optimization_enabled);
    prepare_compile_visibility();
    const bool parsed = compile_script_internal(std::move(script));
    clear_compile_visibility();
    if (!parsed || has_error()) return Object("", "Error");
    nested->translate(*this, session->script_function_version());
    if (!nested->valid()) return Object("", "Error");
    const auto result = session->run(*nested);
    if (nested->has_runtime_error()) runtime_error = nested->get_runtime_error();
    nested_modules.push_back(std::move(nested));
    return result;
}

Object CifaBytecode::run_file(const std::string& filename)
{
    runtime_error.clear();
    if (!compiled_valid && translation_error.empty())
    {
        if (!compile_file(filename)) return Object("", "Error");
        return run();
    }
    auto nested = std::make_unique<CifaBytecode>();
    nested->set_output_error(false);
    nested->set_optimization_enabled(optimization_enabled);
    prepare_compile_visibility();
    const bool parsed = compile_file_internal(filename);
    clear_compile_visibility();
    if (!parsed || has_error()) return Object("", "Error");
    nested->translate(*this, session->script_function_version());
    if (!nested->valid()) return Object("", "Error");
    const auto result = session->run(*nested);
    if (nested->has_runtime_error()) runtime_error = nested->get_runtime_error();
    nested_modules.push_back(std::move(nested));
    return result;
}

void CifaBytecode::prepare_compile_visibility()
{
    session->copy_catalog(persistent_functions, persistent_struct_defs);
    compile_visible_functions = &persistent_functions;
    compile_visible_struct_defs = &persistent_struct_defs;
}

void CifaBytecode::clear_compile_visibility()
{
    compile_visible_functions = nullptr;
    compile_visible_struct_defs = nullptr;
}
}

