#include "CifaBytecode.h"
#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <iostream>
#include <limits>
#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#endif

#if defined(_MSC_VER) && !defined(__clang__)
#define CIFA_NOINLINE __declspec(noinline)
#elif defined(__clang__) || defined(__GNUC__)
#define CIFA_NOINLINE __attribute__((noinline))
#else
#define CIFA_NOINLINE
#endif

namespace cifa
{
namespace
{
std::uint64_t profile_now_ns()
{
#ifdef __EMSCRIPTEN__
    return static_cast<std::uint64_t>(emscripten_get_now() * 1000000.0);
#else
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
#endif
}
}

CifaBytecode::VmArray::Values::Values(const memory::Resource& resource) : storage(std::make_shared<Storage>(resource)) {}

CifaBytecode::VmArray::Values::Values(size_t size, const memory::Resource& resource) : Values(resource) { storage->elements.resize(size); }

CifaBytecode::VmArray::Values::Values(Container elements, const memory::Resource& resource)
    : storage(std::make_shared<Storage>(resource, std::move(elements))) {}

CifaBytecode::VmArray::Values::Container& CifaBytecode::VmArray::Values::writable()
{
    // 仅写入时分离，读取必须继续共享按值语义的底层存储。
    if (storage.use_count() != 1)
    {
        storage = std::make_shared<Storage>(storage->resource, storage->elements);
    }
    return storage->elements;
}

CifaBytecode::VmMap::Values::Values(const memory::Resource& resource) : storage(std::make_shared<Storage>(resource)) {}

CifaBytecode::VmMap::Values::Values(Container elements, const memory::Resource& resource)
    : storage(std::make_shared<Storage>(resource, std::move(elements))) {}

CifaBytecode::VmMap::Values::Container& CifaBytecode::VmMap::Values::writable()
{
    if (storage.use_count() != 1)
    {
        storage = std::make_shared<Storage>(storage->resource, storage->elements);
    }
    return storage->elements;
}

CifaBytecode::VmArray::Values::Container::iterator CifaBytecode::VmArray::Values::insert(
    const_iterator position, CompactValue value)
{
    // 写时分离可能使迭代器失效，先保存相对偏移。
    const auto offset = position - storage->elements.begin();
    auto& values = writable();
    return values.insert(values.begin() + offset, std::move(value));
}

CifaBytecode::VmArray::Values::Container::iterator CifaBytecode::VmArray::Values::erase(const_iterator position)
{
    const auto offset = position - storage->elements.begin();
    auto& values = writable();
    return values.erase(values.begin() + offset);
}

void CifaBytecode::VmArray::Values::push_back(CompactValue value)
{
    writable().push_back(std::move(value));
}

CifaBytecode::VmMap::VmMap(const ObjectMap& elements, const memory::Resource& allocation) : values(allocation)
{
    for (const auto& [key, object] : elements) values[key] = CompactValue(object.value, allocation);
}

CifaBytecode::VmMap::VmMap(ObjectMap&& elements, const memory::Resource& allocation) : values(allocation)
{
    for (auto& [key, object] : elements) values[key] = CompactValue(std::move(object.value), allocation);
}

CifaBytecode::CompactValue::CompactValue(std::any value, const memory::Resource& allocation)
    : Storage(import_resource(std::move(value), allocation))
{
}

CifaBytecode::CompactValue::CompactValue(const Object::Storage& value, const memory::Resource& allocation)
{
    if (const auto* integer = value_get_if<std::int64_t>(&value)) emplace<std::int64_t>(*integer);
    else if (const auto* floating = value_get_if<double>(&value)) emplace<double>(*floating);
    else if (const auto* boolean = value_get_if<bool>(&value)) emplace<bool>(*boolean);
    else if (const auto* resource = value_get_if<std::any>(&value))
    {
        Storage::operator=(import_resource(*resource, allocation));
    }
}

CifaBytecode::CompactValue::CompactValue(Object::Storage&& value, const memory::Resource& allocation)
{
    if (auto* integer = value_get_if<std::int64_t>(&value)) emplace<std::int64_t>(*integer);
    else if (auto* floating = value_get_if<double>(&value)) emplace<double>(*floating);
    else if (auto* boolean = value_get_if<bool>(&value)) emplace<bool>(*boolean);
    else if (auto* resource = value_get_if<std::any>(&value))
    {
        Storage::operator=(import_resource(std::move(*resource), allocation));
    }
}

CifaBytecode::CompactValue::CompactValue(const CompactValue& other)
    : Storage(static_cast<const Storage&>(other))
{
}

CifaBytecode::CompactValue::CompactValue(CompactValue&& other) noexcept
    : Storage(std::move(static_cast<Storage&>(other))) { other.clear(); }

CifaBytecode::CompactValue& CifaBytecode::CompactValue::operator=(const CompactValue& other)
{
    if (this == &other) return *this;
    Storage::operator=(static_cast<const Storage&>(other));
    return *this;
}

CifaBytecode::CompactValue& CifaBytecode::CompactValue::operator=(CompactValue&& other) noexcept
{
    if (this == &other) return *this;
    Storage::operator=(std::move(static_cast<Storage&>(other)));
    other.clear();
    return *this;
}

CifaBytecode::CompactValue::Storage CifaBytecode::CompactValue::import_resource(const std::any& value, const memory::Resource& allocation)
{
    if (const auto* text = std::any_cast<std::string>(&value)) return VmString(*text, allocation);
    const auto* objects = std::any_cast<ObjectVector>(&value);
    if (!objects)
    {
        if (const auto* map = std::any_cast<ObjectMap>(&value)) return VmMap(*map, allocation);
        return value;
    }
    std::pmr::vector<CompactValue> elements(allocation.get());
    elements.reserve(objects->size());
    for (const auto& object : *objects) elements.emplace_back(object.value, allocation);
    return VmArray(std::move(elements), allocation);
}

CifaBytecode::CompactValue::Storage CifaBytecode::CompactValue::import_resource(std::any&& value, const memory::Resource& allocation)
{
    if (auto* text = std::any_cast<std::string>(&value)) return VmString(*text, allocation);
    auto* objects = std::any_cast<ObjectVector>(&value);
    if (!objects)
    {
        if (auto* map = std::any_cast<ObjectMap>(&value)) return VmMap(std::move(*map), allocation);
        return std::move(value);
    }
    std::pmr::vector<CompactValue> elements(allocation.get());
    elements.reserve(objects->size());
    for (auto& object : *objects) elements.emplace_back(std::move(object.value), allocation);
    return VmArray(std::move(elements), allocation);
}

Object::Storage CifaBytecode::CompactValue::export_storage() const
{
    if (const auto* array = resource<VmArray>())
    {
        ObjectVector objects(array->values.size());
        for (size_t index = 0; index < array->values.size(); ++index)
            objects[index].value = array->values[index].export_storage();
        return objects;
    }
    if (const auto* map = resource<VmMap>())
    {
        ObjectMap fields;
        for (const auto& [key, value] : map->values.readable()) fields[std::string(key)].value = value.export_storage();
        return fields;
    }
    if (const auto* text = resource<std::pmr::string>()) return std::any(std::string(*text));
    if (const auto* resource = std::get_if<std::any>(this)) return *resource;
    if (const auto* integer = std::get_if<std::int64_t>(this)) return *integer;
    if (const auto* floating = std::get_if<double>(this)) return *floating;
    if (const auto* boolean = std::get_if<bool>(this)) return *boolean;
    return {};
}

Object::Storage CifaBytecode::CompactValue::take_storage()
{
    if (resource<VmArray>() || resource<VmMap>() || resource<std::pmr::string>())
    {
        // 消费当前句柄不能连带消费另一个 VM 值仍共享的元素。
        auto result = export_storage();
        clear();
        return result;
    }
    Object::Storage result;
    if (auto* resource = std::get_if<std::any>(this)) result = std::move(*resource);
    else if (auto* integer = std::get_if<std::int64_t>(this)) result = *integer;
    else if (auto* floating = std::get_if<double>(this)) result = *floating;
    else if (auto* boolean = std::get_if<bool>(this)) result = *boolean;
    clear();
    return result;
}

CifaBytecode::Scope::Binding* CifaBytecode::Scope::find(std::string_view name)
{
    for (auto& binding : bindings)
        if (std::string_view(binding.name) == name) return &binding;
    return nullptr;
}

CifaBytecode::Scope::Binding& CifaBytecode::Scope::create(const std::string& name)
{
    if (auto* existing = find(name)) return *existing;
    if (!dynamic_registers) dynamic_registers = std::make_unique<RegisterSlots>(0, *value_resource);
    const size_t slot = dynamic_registers->append();
    bindings.push_back({std::pmr::string(name, bindings.get_allocator().resource()), dynamic_registers.get(), slot});
    return bindings.back();
}

void CifaBytecode::Scope::bind(const std::string& name, RegisterSlots* file, size_t slot)
{
    if (auto* existing = find(name))
    {
        existing->file = file;
        existing->slot = slot;
    }
    else bindings.push_back({std::pmr::string(name, bindings.get_allocator().resource()), file, slot});
}

void CifaBytecode::RegisterSlots::grow(size_t required)
{
    // 负载和元数据在所有窗口中均使用同一个绝对槽下标。
    if (values.size() < required)
    {
        values.resize(required);
        bindings.resize(required);
        origins.resize(required);
        slot_names.resize(required);
        slot_types.resize(required);
    }
}

size_t CifaBytecode::RegisterSlots::append()
{
    const size_t slot = window_size;
    const size_t required = window_base + slot + 1;
    grow(required);
    ++window_size;
    window_top = required;
    clear(slot);
    return slot;
}

void CifaBytecode::RegisterSlots::enter(size_t count)
{
    // 调用窗口共享同一底层存储，扩容会使元素引用失效。
    window_base = window_top;
    window_size = count;
    window_top += count;
    grow(window_top);
}

void CifaBytecode::RegisterSlots::restore(size_t base, size_t size, size_t top)
{
    // 立即释放离开窗口的值以保持 RAII 语义，同时保留槽容量。
    window_base = base;
    for (size_t index = top; index < window_top; ++index)
        clear(index - window_base);
    window_size = size;
    window_top = top;
}

void CifaBytecode::RegisterSlots::export_metadata(size_t slot, Object& destination) const
{
    const size_t index = window_base + slot;
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
}

void CifaBytecode::RegisterSlots::export_argument(size_t slot, Object& destination)
{
    const size_t index = window_base + slot;
    // 这里不能调用 export_object，否则会在消费负载前额外复制一次。
    export_metadata(slot, destination);
    destination.value = values[index].value.take_storage();
    if (const auto* resource = value_get_if<std::any>(&destination.value);
        resource && std::any_cast<Object::NoValue>(resource)) destination.type1 = "NoValue";
    clear(slot);
}

void CifaBytecode::RegisterSlots::export_object(size_t slot, Object& destination) const
{
    const size_t index = window_base + slot;
    export_metadata(slot, destination);
    destination.value = values[index].value.export_storage();
    if (const auto* resource = value_get_if<std::any>(&destination.value);
        resource && std::any_cast<Object::NoValue>(resource)) destination.type1 = "NoValue";
}

void CifaBytecode::RegisterSlots::clear(size_t slot)
{
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

void CifaBytecode::RegisterSlots::set_name(size_t slot, std::string_view name)
{
    const size_t index = window_base + slot;
    if (name.empty()) slot_names[index] = 0;
    else
    {
        const auto found = name_ids.find(name);
        if(found != name_ids.end()) { slot_names[index] = found->second; return; }
        const size_t id = name_pool.size();
        name_pool.emplace_back(name);
        name_ids.emplace(name_pool.back(), id);
        slot_names[index] = id;
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

CifaBytecode::BytecodeValue::Storage const& CifaBytecode::RegisterSlots::payload(size_t slot) const
{
    return values[window_base + slot].value;
}

CifaBytecode::BytecodeValue::Storage& CifaBytecode::RegisterSlots::resource_payload(size_t slot)
{
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
    values[window_base + slot].value = CompactValue(value, owner.resource);
}

void CifaBytecode::RegisterSlots::store_payload(size_t slot, Object::Storage&& value)
{
    values[window_base + slot].value = CompactValue(std::move(value), owner.resource);
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
    write_payload(slot, CompactValue(std::move(value), owner.resource));
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
    if (binary_payloads(opcode, destination, payload(left), payload(right), machine, location))
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
    machine.export_host_globals();
    for (auto& callback : *callbacks)
    {
        auto result = callback(left_argument, right == left ? left_argument : right_argument);
        if (machine.host.has_runtime_error() || machine.should_stop())
        {
            machine.import_host_globals();
            break;
        }
        if (result.hasValue())
        {
            machine.import_host_globals();
            import_object(destination, std::move(result));
            return;
        }
    }
    machine.import_host_globals();
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
    const auto* left_text = left_payload.resource<std::pmr::string>();
    const auto* right_text = right_payload.resource<std::pmr::string>();
    if (left_text && right_text)
    {
        switch (opcode)
        {
        case Opcode::Add: {
            VmString result(*left_text, owner.resource);
            result.text += *right_text;
            write_payload(destination, CompactValue(std::move(result)));
            return true;
        }
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
    std::int64_t right_integer, double right_number, bool right_double, Machine& machine, const SourceLocation& location,
    bool preserve_binding)
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
    if (result_is_boolean) write_number(destination, boolean_result, preserve_binding);
    else if (result_is_double) write_number(destination, double_result, preserve_binding);
    else write_number(destination, integer_result, preserve_binding);
    return true;
}

std::string CifaBytecode::Machine::format_frame(const SourceLocation& location)
{
    const std::string_view filename = location.filename.empty() ? std::string_view("<script>") : std::string_view(location.filename);
    const std::string_view text = location.text.empty() ? std::string_view(location.str) : std::string_view(location.text);
    const std::string header = std::string(filename) + ":" + std::to_string(location.line) + ", col " + std::to_string(location.col) + ": ";
    std::string caret(header.size(), ' ');
    const size_t column = location.col == 0 ? 0 : location.col - 1;
    const size_t prefix = (std::min)(column, text.size());
    for (size_t index = 0; index < prefix; ++index) caret += text[index] == '\t' ? '\t' : ' ';
    if (column > prefix) caret.append(column - prefix, ' ');
    std::string result = header;
    result += text;
    return result + "\n" + caret + "^";
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

CifaBytecode::InstructionDiagnostic& CifaBytecode::pending_diagnostic(std::pmr::vector<BuildInstruction>& instructions)
{
    auto& diagnostics = pending_diagnostics[&instructions];
    diagnostics.resize(instructions.size());
    return diagnostics.back();
}

size_t CifaBytecode::index_site(const CalUnit& node)
{
    const size_t site = index_sites.size();
    const auto local = local_slot(node, false);
    if (compiling_function != nullptr && node.with_type && node.suffix)
        compile_array_locals.insert(node.str);
    index_sites.push_back({intern_name(node.str), intern_name(node.type_name), node.v.size(), local ? *local + 1 : 0,
        node.with_type && node.suffix, !node.v[0].v.empty() && node.v[0].v[0].type == CalUnitType::String,
        node.with_type});
    return site;
}

void CifaBytecode::seal(Instructions& instructions)
{
    if (const auto found = pending_diagnostics.find(&instructions.build_code); found != pending_diagnostics.end())
    {
        instructions.diagnostics = std::move(found->second);
        pending_diagnostics.erase(found);
    }
    instructions.diagnostics.resize(instructions.build_code.size());
    size_t scope_depth = 0;
    for (size_t pc = 0; pc < instructions.build_code.size(); ++pc)
    {
        auto& instruction = instructions.build_code[pc];
        auto& diagnostic = instructions.diagnostics[pc];
        diagnostic.source = instruction.source;
        if (instruction.opcode == Opcode::ScopeEnter)
            instructions.scope_capacity = (std::max)(instructions.scope_capacity, ++scope_depth);
        else if (instruction.opcode == Opcode::ScopeLeave && scope_depth != 0) --scope_depth;
        const auto found = compile_sources.find(instruction.source.id);
        const auto* pending_source = found == compile_sources.end() ? nullptr : found->second;
        if (pending_source != nullptr && instruction.opcode == Opcode::Branch && diagnostic.condition_source.id == 0)
        {
            const auto& node = *pending_source;
            if (node.type == CalUnitType::Key)
            {
                if ((node.str == "if" || node.str == "while") && !node.v.empty()) diagnostic.condition_source = source_ref(&node.v[0]);
                else if (node.str == "for" && !node.v.empty() && node.v[0].v.size() > 1) diagnostic.condition_source = source_ref(&node.v[0].v[1]);
                else if (node.str == "do" && node.v.size() > 1 && !node.v[1].v.empty()) diagnostic.condition_source = source_ref(&node.v[1].v[0]);
            }
        }
        if (diagnostic.target_source.id == 0 && pending_source != nullptr)
        {
            const auto& node = *pending_source;
            if (instruction.opcode == Opcode::RangeBegin && node.v.size() > 1)
                diagnostic.target_source = source_ref(&node.v[1]);
            else if (!node.v.empty() && (instruction.opcode == Opcode::RangeNext || instruction.opcode == Opcode::PrepareStore
                || instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment))
                diagnostic.target_source = source_ref(&node.v[0]);
        }
    }
    instructions.code.clear();
    instructions.code.reserve(instructions.build_code.size());
    for (const auto& instruction : instructions.build_code)
        instructions.code.push_back({instruction.opcode, instruction.operand, instruction.auxiliary, instruction.member_site,
            instruction.write, instruction.variable_site, instruction.destination, instruction.input_offset,
            instruction.input_count, instruction.discard_result});
    instructions.build_code.clear();
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

std::pmr::vector<size_t> CifaBytecode::compact(Instructions& instructions)
{
    const auto compact_only = [](Opcode opcode)
    {
        return opcode == Opcode::Enter || opcode == Opcode::Leave || opcode == Opcode::CallEnd
            || opcode == Opcode::Removed;
    };
    const size_t original_size = instructions.code.size();
    std::pmr::vector<size_t> remap(original_size + 1, instructions.resource);
    size_t compact_size = 0;
    for (size_t index = 0; index < original_size; ++index)
        if (!compact_only(instructions.code[index].opcode))
            ++compact_size;
    remap[original_size] = compact_size;
    for (size_t index = original_size; index > 0; --index)
    {
        const size_t current = index - 1;
        remap[current] = compact_only(instructions.code[current].opcode) ? remap[current + 1] : --compact_size;
    }
    std::pmr::vector<Instruction> code(instructions.resource);
    std::pmr::vector<InstructionDiagnostic> diagnostics(instructions.resource);
    std::pmr::vector<std::pmr::vector<std::pair<size_t, bool>>> diagnostic_frames(instructions.resource);
    code.reserve(remap.back());
    diagnostics.reserve(remap.back());
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
        else if (instruction.opcode == Opcode::NumericForNext)
        {
            instruction.auxiliary = remap[instruction.auxiliary];
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
        diagnostics.push_back(std::move(instructions.diagnostics[index]));
        diagnostic_frames.push_back(std::move(instructions.diagnostic_frames[index]));
    }
    instructions.code = std::move(code);
    instructions.diagnostics = std::move(diagnostics);
    instructions.diagnostic_frames = std::move(diagnostic_frames);
    std::pmr::vector<size_t> loop_ids(instructions.code.size(), std::numeric_limits<size_t>::max(), instructions.resource);
    for (size_t pc = 0; pc < instructions.code.size(); ++pc)
        if (instructions.code[pc].opcode == Opcode::LoopMark)
        {
            loop_ids[pc] = instructions.loop_state_count++;
            instructions.code[pc].auxiliary = loop_ids[pc];
        }
    for (auto& instruction : instructions.code)
        if (instruction.opcode == Opcode::Unwind)
            instruction.operand = loop_ids[instruction.operand];
    for (size_t pc = 0; pc + 1 < instructions.code.size(); ++pc)
    {
        auto& instruction = instructions.code[pc];
        const auto& next = instructions.code[pc + 1];
        if (instruction.opcode != Opcode::NumericBinary || next.opcode != Opcode::Branch
            || instruction.auxiliary >= instructions.numeric_operations.size()) continue;
        const auto& operation = instructions.numeric_operations[instruction.auxiliary];
        const auto opcode = static_cast<Opcode>(operation.opcode);
        if (operation.destination != 0 || opcode < Opcode::Less || opcode > Opcode::NotEqual) continue;
        instruction.opcode = Opcode::NumericCompareBranch;
        instruction.member_site = next.operand;
    }
    // Resolve the loop pattern once, after jump remapping and compare fusion.
    // Other conditions (including side effects and mutable bounds) retain the
    // ordinary update followed by full condition evaluation.
    instructions.integer_loops.clear();
    for (auto& instruction : instructions.code)
    {
        if (instruction.opcode == Opcode::IncrementLocal || instruction.opcode == Opcode::NumericForNext)
            instruction.plain_increment = !variable_sites[instruction.variable_site - 1].with_type;
        if (instruction.opcode != Opcode::NumericForNext) continue;
        instruction.member_site = 0;
        const size_t condition = instruction.auxiliary;
        if (condition + 1 >= instructions.code.size()) continue;
        const auto& compare = instructions.code[condition];
        if (compare.opcode != Opcode::NumericCompareBranch
            || instructions.code[condition + 1].opcode != Opcode::Branch) continue;
        const auto& operation = instructions.numeric_operations[compare.auxiliary];
        // A local integer compared with an integer literal using '<'. The local
        // may be changed by the body; its current value/type is checked at runtime.
        if (operation.flags != 4 || operation.left != instruction.operand
            || static_cast<Opcode>(operation.opcode) != Opcode::Less) continue;
        const auto* limit = constants[operation.right].value.get_if<std::int64_t>();
        if (!limit) continue;
        instructions.integer_loops.push_back({*limit, condition + 2, compare.member_site});
        instruction.member_site = instructions.integer_loops.size();
    }
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

bool CifaBytecode::block_needs_runtime_scope(const CalUnit& node)
{
    const auto found = compile_scope_effects.find(&node);
    return found == compile_scope_effects.end() || found->second;
}

void CifaBytecode::analyze_binding_scopes(const CalUnit& root, bool custom_conversions)
{
    // Lexical binding facts are independent of runtime scope and local-slot
    // allocation. The bool additionally proves a built-in numeric binding.
    using Frame = std::pmr::unordered_map<std::string, bool>;
    using State = std::pmr::vector<Frame>;
    auto* resource = allocation_resource.get();
    State scopes(resource);
    scopes.emplace_back();
    const auto numeric_type = [](const std::string& type)
        { return type == "int" || type == "double" || type == "float"; };
    if (compiling_function)
        for (const auto& parameter : compiling_function->parameters)
            scopes.back()[parameter.name] = numeric_type(parameter.type_name);
    const auto copy = [&]() { return State(scopes, resource); };
    const auto invalidate = [&]() { for (auto& frame : scopes) frame.clear(); };
    const auto lookup = [&](const std::string& name) -> bool*
    {
        for (auto frame = scopes.rbegin(); frame != scopes.rend(); ++frame)
            if (auto found = frame->find(name); found != frame->end()) return &found->second;
        return nullptr;
    };
    const auto intersect = [](State& left, const State& right)
    {
        for (size_t i = 0; i < left.size(); ++i)
        {
            std::erase_if(left[i], [&](const auto& entry) { return !right[i].contains(entry.first); });
            for (auto& [name, numeric] : left[i]) numeric = numeric && right[i].at(name);
        }
    };
    struct Effect { bool scope = false; bool numeric = false; };
    const auto analyze = [&](auto&& self, const CalUnit& node) -> Effect
    {
        if (node.type == CalUnitType::Constant) return {false, true};
        if (node.type == CalUnitType::String || node.type == CalUnitType::Split) return {};
        if (node.type == CalUnitType::Label || node.type == CalUnitType::Goto)
        {
            invalidate();
            return {true, false};
        }
        if (node.type == CalUnitType::Parameter && node.v.empty())
        {
            if (node.with_type)
            {
                const bool numeric = numeric_type(node.type_name);
                scopes.back()[node.str] = numeric;
                return {true, numeric};
            }
            const auto* binding = lookup(node.str);
            return {!binding, binding && *binding};
        }
        const bool statement_block = node.type == CalUnitType::Union && node.str == "{}"
            && std::any_of(node.v.begin(), node.v.end(), [](const CalUnit& child)
                { return child.is_statement() || child.type == CalUnitType::Label; });
        if (node.type == CalUnitType::Union && (statement_block || node.str == "()"))
        {
            const bool block = statement_block;
            if (block) scopes.emplace_back();
            Effect result;
            for (const auto& child : node.v)
            {
                const auto effect = self(self, child);
                result.scope |= effect.scope;
                result.numeric = effect.numeric;
            }
            if (block)
            {
                scopes.pop_back();
                // A later loop iteration may require a more conservative decision.
                compile_scope_effects[&node] |= result.scope;
                // Effects owned by a nested block are contained by that block's
                // own scope; they do not force its parent to allocate another.
                result.scope = false;
                result.numeric = false;
            }
            return result;
        }
        if ((node.type == CalUnitType::Key && node.str == "if")
            || (node.type == CalUnitType::Operator && node.str == "?"))
        {
            auto condition = self(self, node.v[0]);
            if (!condition.numeric) { invalidate(); condition.scope = true; }
            auto incoming = copy();
            const bool ternary = node.type == CalUnitType::Operator;
            const auto yes = self(self, ternary ? node.v[1].v[0] : node.v[1]);
            auto yes_state = copy();
            scopes = std::move(incoming);
            const auto no = ternary ? self(self, node.v[1].v[1])
                : node.v.size() > 2 ? self(self, node.v[2]) : Effect{};
            intersect(scopes, yes_state);
            return {condition.scope || yes.scope || no.scope, yes.numeric && no.numeric};
        }
        const bool ordinary_for = node.type == CalUnitType::Key && node.str == "for"
            && node.v.size() == 2 && node.v[0].v.size() == 3;
        if (node.type == CalUnitType::Key && (ordinary_for || node.str == "while" || node.str == "do"))
        {
            Effect result;
            if (ordinary_for) result = self(self, node.v[0].v[0]);
            // Include entry for zero-trip loops, then intersect backedges until
            // no definite binding/type fact changes. Do-loops use the same safe
            // lower bound; they do not export first-iteration-only bindings.
            auto entry = copy();
            for (;;)
            {
                auto previous = copy();
                if (node.str != "do")
                {
                    const auto condition = self(self, ordinary_for ? node.v[0].v[1] : node.v[0]);
                    result.scope |= condition.scope || !condition.numeric;
                    if (!condition.numeric) invalidate();
                }
                result.scope |= self(self, node.v[node.str == "do" ? 0 : 1]).scope;
                if (ordinary_for) result.scope |= self(self, node.v[0].v[2]).scope;
                if (node.str == "do")
                {
                    const auto condition = self(self, node.v[1].v[0]);
                    result.scope |= condition.scope || !condition.numeric;
                    if (!condition.numeric) invalidate();
                }
                intersect(scopes, entry);
                intersect(scopes, previous);
                if (scopes == previous) break;
            }
            result.numeric = false;
            return result;
        }
        if (node.type == CalUnitType::Key && (node.str == "true" || node.str == "false"))
            return {false, true};
        if (node.type == CalUnitType::Key && (node.str == "return" || node.str == "break" || node.str == "continue"))
        {
            Effect result;
            for (const auto& child : node.v) result.scope |= self(self, child).scope;
            invalidate(); // Exited paths contribute no fallthrough facts.
            return result;
        }
        if (node.type == CalUnitType::Operator && node.str == "=" && node.v.size() == 2
            && node.v[0].type == CalUnitType::Parameter && node.v[0].v.empty())
        {
            const auto& target = node.v[0];
            const bool existing = lookup(target.str) != nullptr;
            const bool known_numeric = existing && *lookup(target.str);
            const auto value = self(self, node.v[1]);
            // A store through an unresolved/static host type can run a registered
            // conversion. It must not preserve facts about other bindings.
            const bool conversion = custom_conversions
                && !(target.with_type ? numeric_type(target.type_name) : known_numeric);
            if (conversion) invalidate();
            bool* binding = target.with_type ? nullptr : lookup(target.str);
            // Untyped names may resolve to host bindings, so literal assignment
            // establishes existence without assuming their declared type.
            const bool numeric = target.with_type ? numeric_type(target.type_name)
                || (target.type_name == "auto" && value.numeric) : binding && *binding;
            if (binding) *binding = numeric;
            else scopes.back()[target.str] = numeric;
            return {target.with_type || !existing || value.scope || conversion, value.numeric && numeric};
        }
        if (node.type == CalUnitType::Operator)
        {
            Opcode opcode;
            const bool increment = node.str == "++" || node.str == "--" || node.str == "()++" || node.str == "()--";
            if (operation(node, opcode) || increment || node.str == ",")
            {
                Effect result{false, true};
                const bool conditional = node.str == "&&" || node.str == "||";
                State skipped(resource);
                for (size_t i = 0; i < node.v.size(); ++i)
                {
                    const auto child = self(self, node.v[i]);
                    result.scope |= child.scope;
                    result.numeric &= child.numeric;
                    if (!child.numeric && node.str != ",") { invalidate(); result.scope = true; }
                    if (conditional && i == 0) skipped = copy();
                }
                if (conditional) intersect(scopes, skipped);
                return result;
            }
        }
        // Calls, casts, containers, switch and range constructs can invoke host
        // code or establish special bindings. Do not let child states leak into
        // sibling alternatives or subsequent statements.
        invalidate();
        for (const auto& child : node.v) { self(self, child); invalidate(); }
        return {true, false};
    };
    if (compiling_function) analyze(analyze, root);
    else for (const auto& child : root.v) analyze(analyze, child);
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
    const std::pmr::unordered_map<std::string, Object>* parameters, std::pmr::unordered_set<std::string>* active_functions) const
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
                    std::pmr::unordered_map<std::string, Object> function_parameters(allocation_resource.get());
                    for (size_t index = 0; index < arguments.size(); ++index)
                    {
                        if (!definition->second.arguments[index].type_name.empty()) return false;
                        function_parameters.emplace(definition->second.arguments[index].name, arguments[index]);
                    }
                    std::pmr::unordered_set<std::string> local_active(allocation_resource.get());
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

        const auto native = native_functions.find(node.str);
        if (native == native_functions.end() || !native->second.builtin) return false;

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
                const bool integers = arguments[best].isInteger() && arguments[index].isInteger();
                const bool less = integers ? arguments[best].toInt64() < arguments[index].toInt64()
                    : arguments[best].toDouble() < arguments[index].toDouble();
                const bool different = integers ? arguments[best].toInt64() != arguments[index].toInt64()
                    : arguments[best].toDouble() != arguments[index].toDouble();
                if ((node.str == "max" && less) || (node.str == "min" && !less && different))
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

bool CifaBytecode::emit_register_expression(CalUnit& node, std::pmr::vector<BuildInstruction>& instructions)
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
    struct Operand
    {
        size_t slot;
        bool constant;
        bool temporary;
        size_t temporary_count;
    };
    const auto generate = [&](const auto& self, CalUnit& value, bool root, size_t temporary_base) -> Operand
    {
        if (value.v.empty())
        {
            if (value.type == CalUnitType::Constant)
            {
                Object number;
                if (!Cifa::parse_number_literal(value.str, number)) translation_error = "unsupported bytecode numeric literal";
                constants.emplace_back(std::move(number), allocation_resource);
                return {constants.size() - 1, true, false, 0};
            }
            return {*local_slot(value, false), false, false, 0};
        }
        instructions.push_back({Opcode::Enter, source_ref(&value)});
        const auto left = self(self, value.v[0], false, temporary_base);
        const auto right = self(self, value.v[1], false, temporary_base + (left.temporary ? 1 : 0));
        Opcode opcode;
        operation(value, opcode);
        const size_t destination = left.temporary ? left.slot : right.temporary ? right.slot : temporary_base;
        const size_t temporary_count = (std::max)({left.temporary_count, right.temporary_count,
            root ? size_t(0) : destination + 1});
        RegisterBinarySite site{};
        site.code = {static_cast<std::uint16_t>(opcode), static_cast<std::uint16_t>(root ? 0 : 1), 0,
            static_cast<std::uint32_t>(left.slot), static_cast<std::uint32_t>(right.slot)};
        site.left_name = intern_name(value.v[0].str);
        site.right_name = intern_name(value.v[1].str);
        site.left_source = source_ref(&value.v[0]);
        site.right_source = source_ref(&value.v[1]);
        site.left_constant = left.constant;
        site.right_constant = right.constant;
        site.left_temporary = left.temporary;
        site.right_temporary = right.temporary;
        site.temporary_destination = root ? 0 : destination + 1;
        module_data->register_binary_sites.push_back(site);
        instructions.push_back({Opcode::NumericBinary, source_ref(&value),
            module_data->register_binary_sites.size() - 1});
        instructions.push_back({Opcode::Leave, source_ref(&value)});
        return {destination, false, true, temporary_count};
    };
    generate(generate, node, true, 0);
    return true;
}

size_t CifaBytecode::emit_statement(CalUnit& node, std::pmr::vector<BuildInstruction>& instructions)
{
    const size_t begin = instructions.size();
    auto* saved_statement_node = active_statement_node;
    active_statement_node = &node;
    emit(node, instructions);
    active_statement_node = saved_statement_node;
    if ((node.type == CalUnitType::Key && node.str == "if")
        || (node.type == CalUnitType::Operator && node.str == "?"))
    {
        const auto branch = std::find_if(instructions.begin() + begin, instructions.end(), [](const BuildInstruction& instruction)
            { return instruction.opcode == Opcode::Branch; });
        if (branch != instructions.end() && branch->operand != 0 && branch->operand < instructions.size())
        {
            const size_t branch_index = static_cast<size_t>(branch - instructions.begin());
            const size_t else_begin = branch->operand;
            const auto jump = std::find_if(instructions.begin() + branch_index + 1, instructions.begin() + else_begin,
                [](const BuildInstruction& instruction) { return instruction.opcode == Opcode::Jump; });
            if (jump != instructions.begin() + else_begin)
                discard_statement_result(instructions, branch_index + 1, static_cast<size_t>(jump - instructions.begin()));
            discard_statement_result(instructions, else_begin);
        }
    }
    if (node.type == CalUnitType::Operator && (node.str == "&&" || node.str == "||"))
    {
        const auto branch = std::find_if(instructions.begin() + begin, instructions.end(), [](const BuildInstruction& instruction)
            { return instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch; });
        const auto logical = std::find_if(instructions.rbegin(), instructions.rend(), [](const BuildInstruction& instruction)
            { return instruction.opcode == Opcode::LogicalAnd || instruction.opcode == Opcode::LogicalOr; });
        if (branch != instructions.end() && logical != instructions.rend())
        {
            branch->discard_result = true;
            logical->discard_result = true;
        }
    }
    return begin;
}

void CifaBytecode::discard_statement_result(std::pmr::vector<BuildInstruction>& instructions, size_t begin,
    std::optional<size_t> end)
{
    for (size_t index = end.value_or(instructions.size()); index > begin; --index)
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

void CifaBytecode::emit(CalUnit& node, std::pmr::vector<BuildInstruction>& instructions)
{
    if (optimization_enabled)
    {
        Object value;
        if (try_fold_constant(node, value))
        {
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.emplace_back(std::move(value), allocation_resource);
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
        && (node.v[1].str == "push_back" || node.v[1].str == "resize" || node.v[1].str == "reserve" || node.v[1].str == "insert"
            || node.v[1].str == "erase" || node.v[1].str == "contains"))
    {
        const size_t site = calls.size();
        calls.emplace_back(source_ref(&node), allocation_resource.get());
        auto& call = calls.back();
        if (const auto slot = local_slot(node.v[0], false)) call.local_slot = *slot + 1;
        call.global_receiver = compiling_function == nullptr
            || (call.local_slot == 0 && !compile_array_locals.contains(node.v[0].str));
        std::pmr::vector<CalUnit*> argument_nodes(allocation_resource.get());
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
        calls.emplace_back(source_ref(&node), allocation_resource.get());
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
            const std::pmr::unordered_set<std::string>& parameters) -> bool
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
            std::pmr::vector<CalUnit*> arguments(allocation_resource.get());
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
                    std::pmr::unordered_set<std::string> parameters(allocation_resource.get());
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
                        std::pmr::unordered_map<std::string, const CalUnit*> replacements(allocation_resource.get());
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
        const auto native_size = native_functions.find("size");
        const bool original_size = optimization_enabled && node.str == "size"
            && native_size != native_functions.end() && native_size->second.builtin;
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
                pending_diagnostic(instructions).target_source = source_ref(argument);
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
        const auto native_math = native_functions.find(node.str);
        const bool original_math = optimization_enabled && direct_math_kind != MathKind::None
            && native_math != native_functions.end() && native_math->second.builtin;
        if (original_math)
        {
            std::pmr::vector<CalUnit*> arguments(allocation_resource.get());
            std::function<void(CalUnit&)> flatten = [&](CalUnit& item)
            {
                if (item.str == ",") for (auto& child : item.v) flatten(child);
                else if (item.type != CalUnitType::None) arguments.push_back(&item);
            };
            for (auto& child : node.v) flatten(child);
            if (arguments.size() == direct_math_arity)
            {
                const size_t site = calls.size();
                calls.emplace_back(source_ref(&node), allocation_resource.get());
                calls.back().name_id = intern_name(node.str);
                calls.back().math_kind = direct_math_kind;
                if (direct_math_arity == 2)
                {
                    const auto left = local_slot(*arguments[0], false);
                    const auto right = local_slot(*arguments[1], false);
                    if (left && right)
                    {
                        calls.back().math_local_operands = true;
                        calls.back().math_local_slots = {*left, *right};
                        calls.back().math_local_names = {intern_name(arguments[0]->str), intern_name(arguments[1]->str)};
                    }
                }
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    auto* argument = arguments[index];
                    calls.back().arguments.push_back(source(source_ref(argument)));
                    if (!calls.back().math_local_operands) emit(*argument, instructions);
                }
                instructions.push_back({direct_math_arity == 1 ? Opcode::MathUnary : Opcode::MathBinary,
                    source_ref(&node), site});
                return;
            }
        }
        const size_t index = calls.size();
        calls.emplace_back(source_ref(&node), allocation_resource.get());
        auto& call = calls.back();
        std::pmr::vector<CalUnit*> argument_nodes(allocation_resource.get());
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
        compile_loops.emplace_back(compile_scopes, mark, allocation_resource.get(), true);
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
        std::pmr::vector<size_t> pending_cases(allocation_resource.get());
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
            compile_loops.emplace_back(compile_scopes, loop_mark, allocation_resource.get());
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
        compile_loops.emplace_back(compile_scopes, mark, allocation_resource.get());
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
            pending_diagnostic(instructions).condition_source = source_ref(condition);
        }
        discard_statement_result(instructions, emit_statement(node.v[node.str == "do" ? 0 : 1], instructions));
        const size_t next = instructions.size();
        if (ordinary_for)
        {
            const size_t update = emit_statement(node.v[0].v[2], instructions);
            discard_statement_result(instructions, update);
            for (size_t index = instructions.size(); index > update; --index)
            {
                auto& candidate = instructions[index - 1];
                if (candidate.opcode == Opcode::IncrementLocal)
                {
                    candidate.opcode = Opcode::NumericForNext;
                    candidate.auxiliary = start;
                    break;
                }
                if (candidate.opcode != Opcode::Enter && candidate.opcode != Opcode::Leave) break;
            }
        }
        if (node.str == "do")
        {
            emit(node.v[1].v[0], instructions);
            branch = instructions.size();
            instructions.push_back({Opcode::Branch, source_ref(&node)});
            auto* condition = &node.v[1].v[0];
            if (condition->str == "()" && condition->v.size() == 1) condition = &condition->v[0];
            pending_diagnostic(instructions).condition_source = source_ref(condition);
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
                        && (instructions[instructions.size() - 2].opcode == Opcode::NumericBinary
                            || instructions[instructions.size() - 2].opcode == Opcode::RegisterBinary))
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
                constants.emplace_back(ObjectVector{}, allocation_resource);
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
        // Keep the function/root binding frame. For nested blocks, elide only
        // scopes proven to have no binding effects. Block/label bookkeeping
        // remains below, so goto visibility and unwind targets are unchanged.
        const bool scope = node.str == "{}" && (!optimization_enabled
            || (compiling_function != nullptr && compile_local_scopes.size() <= 1)
            || block_needs_runtime_scope(node));
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
        compile_blocks.emplace_back(block_mark, allocation_resource.get());
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
        // With no runtime scope and no labels, this block has no unwind target
        // to snapshot. Retain the compile-time block boundary for goto checks.
        if (!scope && node.str == "{}" && compile_blocks.back().targets.empty())
            instructions[block_mark].opcode = Opcode::Removed;
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
        pending_diagnostic(instructions).condition_source = source_ref(condition);
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
                constants.emplace_back(std::move(value), allocation_resource);
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
            constants.emplace_back(node.str, allocation_resource);
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
            constants.emplace_back(std::move(number), allocation_resource);
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
            instructions.push_back({Opcode::NumericBinary, source_ref(&node), site});
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
    instructions.numeric_operations.clear();
    instructions.numeric_local_sites.clear();
    for (auto& instruction : instructions.code)
    {
        if (instruction.opcode == Opcode::RegisterSnapshot)
        {
            if (instruction.auxiliary >= std::numeric_limits<std::uint32_t>::max())
            { translation_error = "bytecode temporary register exceeds encoding range"; return false; }
            instructions.temporary_count = (std::max)(instructions.temporary_count, instruction.auxiliary + 1);
        }
        if ((instruction.opcode == Opcode::NumericBinary || instruction.opcode == Opcode::RegisterBinary)
            && instruction.operand < module_data->register_binary_sites.size())
        {
            const auto& site = module_data->register_binary_sites[instruction.operand];
            instructions.temporary_count = (std::max)(instructions.temporary_count, site.temporary_destination);
            if (instruction.opcode == Opcode::NumericBinary && site.code.destination != 0)
            {
                const auto& binding = variable_sites[site.variable_site - 1];
                const auto operation_opcode = static_cast<Opcode>(site.code.opcode);
                const bool integer_result = operation_opcode == Opcode::Add || operation_opcode == Opcode::Subtract
                    || operation_opcode == Opcode::Multiply || operation_opcode == Opcode::Divide
                    || operation_opcode == Opcode::Modulo || operation_opcode == Opcode::BitAnd
                    || operation_opcode == Opcode::BitOr || operation_opcode == Opcode::BitXor
                    || operation_opcode == Opcode::ShiftLeft || operation_opcode == Opcode::ShiftRight;
                const bool direct_integer = (!binding.with_type || binding.type_id == module_data->int_type_id) && integer_result;
                const bool direct_double = binding.with_type && binding.type_id == module_data->double_type_id
                    && (operation_opcode == Opcode::Add || operation_opcode == Opcode::Subtract
                        || operation_opcode == Opcode::Multiply || operation_opcode == Opcode::Divide);
                if (direct_integer || direct_double)
                {
                    instruction.opcode = Opcode::NumericBinaryLocal;
                    instruction.auxiliary = instructions.numeric_operations.size();
                    auto operation = site.code;
                    operation.flags = static_cast<std::uint16_t>((site.left_constant ? 1 : site.left_temporary ? 2 : 0)
                        | (site.right_constant ? 4 : site.right_temporary ? 8 : 0));
                    instructions.numeric_operations.push_back(operation);
                    instructions.numeric_local_sites.push_back(instruction.operand);
                }
                else instruction.opcode = Opcode::RegisterBinary;
            }
            else if (instruction.opcode == Opcode::NumericBinary)
            {
                instruction.auxiliary = instructions.numeric_operations.size();
                auto operation = site.code;
                operation.flags = static_cast<std::uint16_t>((site.left_constant ? 1 : site.left_temporary ? 2 : 0)
                    | (site.right_constant ? 4 : site.right_temporary ? 8 : 0));
                operation.destination = static_cast<std::uint32_t>(site.temporary_destination);
                instructions.numeric_operations.push_back(operation);
                instructions.numeric_local_sites.push_back(std::numeric_limits<size_t>::max());
            }
        }
    }
    for (auto& instruction : instructions.code)
        if (instruction.opcode == Opcode::MethodPush && calls[instruction.operand].global_receiver)
            instruction.opcode = Opcode::ArrayPushGlobal;
    for (size_t pc = 0; pc + 1 < instructions.code.size(); ++pc)
    {
        auto& constant = instructions.code[pc];
        auto& store = instructions.code[pc + 1];
        if (constant.opcode != Opcode::Constant || store.opcode != Opcode::StoreLocal
            || store.write != WriteOperation::Assign || store.variable_site == 0
            || store.variable_site > variable_sites.size()) continue;
        const auto& binding = variable_sites[store.variable_site - 1];
        if (!binding.with_type || binding.type_id != module_data->int_type_id
            || constant.operand >= constants.size()
            || !value_holds<std::int64_t>(constants[constant.operand].value)) continue;
        constant.opcode = Opcode::ConstantLocal;
        constant.auxiliary = store.operand + 1;
        constant.variable_site = store.variable_site;
        instructions.diagnostics[pc].target_source = instructions.diagnostics[pc + 1].source;
        constant.discard_result = store.discard_result;
        store.opcode = Opcode::Removed;
    }
    for (size_t pc = 0; pc + 1 < instructions.code.size(); ++pc)
    {
        auto& load = instructions.code[pc];
        auto& push = instructions.code[pc + 1];
        if (load.opcode != Opcode::LoadLocal || push.opcode != Opcode::ArrayPushGlobal) continue;
        push.opcode = Opcode::ArrayPushGlobalLocal;
        push.auxiliary = load.operand + 1;
        push.member_site = load.auxiliary;
        instructions.diagnostics[pc + 1].condition_source = instructions.diagnostics[pc].source;
        load.opcode = Opcode::Removed;
    }
    if (module_data->host_function_version != 0)
        for (size_t pc = 0; pc + 1 < instructions.code.size(); ++pc)
        {
            auto& load = instructions.code[pc];
            auto& index = instructions.code[pc + 1];
            if (load.opcode != Opcode::LoadLocal || index.opcode != Opcode::Index
                || index.operand != 1 || index.auxiliary >= index_sites.size()) continue;
            const auto& site = index_sites[index.auxiliary];
            if (site.declaration || site.string_index || site.dimensions != 1) continue;
            index.opcode = Opcode::IndexLocal;
            index.member_site = load.operand + 1;
            index.variable_site = load.auxiliary;
            instructions.diagnostics[pc + 1].condition_source = instructions.diagnostics[pc].source;
            load.opcode = Opcode::Removed;
        }
    alignas(std::max_align_t) std::byte verify_buffer[8192];
    std::pmr::monotonic_buffer_resource verify_scratch(verify_buffer, sizeof(verify_buffer), instructions.resource);
    struct State
    {
        explicit State(std::pmr::memory_resource* resource)
            : frames(resource), diagnostics(resource), local_scope_bases(resource), calls(resource),
              methods(resource), ranges(resource), switches(resource) {}
        State(const State& other) : State(other.frames.get_allocator().resource()) { *this=other; }
        State& operator=(const State&) = default;
        size_t register_top = 0;
        std::pmr::vector<size_t> frames;
        std::pmr::vector<std::pair<size_t, bool>> diagnostics;
        size_t scopes = 0;
        std::pmr::vector<size_t> local_scope_bases;
        std::pmr::vector<std::pair<size_t, bool>> calls;
        std::pmr::vector<std::pair<size_t, size_t>> methods;
        std::pmr::vector<size_t> ranges;
        std::pmr::vector<size_t> switches;
        bool visited = false;
    };
    std::pmr::vector<State> states(instructions.code.size() + 1, State(&verify_scratch), &verify_scratch);
    instructions.diagnostic_frames.clear();
    instructions.diagnostic_frames.resize(instructions.code.size());
    if (instructions.diagnostics.size() != instructions.code.size())
    { translation_error = "bytecode diagnostic table size mismatch"; return false; }
    instructions.register_inputs.clear();
    instructions.register_capacity = 0;
    std::pmr::vector<size_t> pending(&verify_scratch);
    pending.push_back(0);
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
        if (static_cast<unsigned>(instruction.opcode) > static_cast<unsigned>(Opcode::Removed))
        { translation_error = "invalid bytecode opcode"; return false; }
        if (instruction.opcode == Opcode::Constant && instruction.operand >= constants.size())
        { translation_error = "bytecode constant index out of range"; return false; }
        if (instruction.opcode == Opcode::NumericBinary || instruction.opcode == Opcode::NumericBinaryLocal
            || instruction.opcode == Opcode::RegisterBinary)
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
        if (instruction.opcode == Opcode::NumericForNext && instruction.auxiliary > instructions.code.size())
        { translation_error = "bytecode for jump out of range"; return false; }
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
        const auto& diagnostic = instructions.diagnostics[pc];
        if (diagnostic.source.id == 0 || diagnostic.source.id > sources.size())
        {
            translation_error = "bytecode instruction has no source";
            return false;
        }
        if (instruction.opcode == Opcode::Member && (instruction.operand >= names.size() || instruction.auxiliary >= names.size()))
        { translation_error = "invalid bytecode member name"; return false; }
        if (diagnostic.condition_source.id > sources.size())
        { translation_error = "invalid bytecode condition source"; return false; }
        if (diagnostic.target_source.id > sources.size())
        { translation_error = "invalid bytecode target source"; return false; }
        if ((instruction.opcode == Opcode::RangeBegin || instruction.opcode == Opcode::RangeNext
            || instruction.opcode == Opcode::PrepareStore || instruction.opcode == Opcode::Store || instruction.opcode == Opcode::Increment)
            && diagnostic.target_source.id == 0)
        { translation_error = "invalid bytecode target source"; return false; }
        if (instruction.opcode == Opcode::LoadLocal && instruction.auxiliary >= names.size())
        { translation_error = "invalid bytecode variable name"; return false; }
        if (instruction.opcode == Opcode::Cast && instruction.operand >= names.size())
        { translation_error = "invalid bytecode type name"; return false; }
        if ((instruction.opcode == Opcode::Peek || instruction.opcode == Opcode::Load) && instruction.auxiliary == 1
            && instruction.operand >= names.size())
        { translation_error = "invalid bytecode variable name"; return false; }
        if (instruction.opcode == Opcode::Store || instruction.opcode == Opcode::StoreLocal
            || instruction.opcode == Opcode::Increment || instruction.opcode == Opcode::IncrementLocal
            || instruction.opcode == Opcode::NumericForNext)
        {
            const bool increment = instruction.opcode == Opcode::Increment || instruction.opcode == Opcode::IncrementLocal
                || instruction.opcode == Opcode::NumericForNext;
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
            || instruction.opcode == Opcode::StoreLocal || instruction.opcode == Opcode::IncrementLocal
            || instruction.opcode == Opcode::NumericForNext)
            && instruction.operand >= local_slot_count)
        {
            translation_error = "bytecode local slot out of range";
            return false;
        }
        if (instruction.opcode == Opcode::StoreLocal || instruction.opcode == Opcode::IncrementLocal
            || instruction.opcode == Opcode::NumericForNext
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
        case Opcode::NumericCompareBranch:
        case Opcode::NumericBinary:
        case Opcode::NumericBinaryLocal:
        case Opcode::RegisterBinary:
            if ((module_data->register_binary_sites[instruction.operand].code.flags & 1) == 0) ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            break;
        case Opcode::Exit:
            continue;
        case Opcode::Removed:
            break;
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
        case Opcode::ArrayPushGlobal:
            if (register_top == 0 || instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 1)
            { translation_error = "invalid bytecode method operand"; return false; }
            break;
        case Opcode::ArrayPushGlobalLocal:
            if (instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 1
                || instruction.auxiliary == 0 || instruction.auxiliary - 1 >= local_slot_count
                || instruction.member_site >= names.size() || instructions.diagnostics[pc].condition_source.id == 0)
            { translation_error = "invalid bytecode local array push"; return false; }
            ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
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
        case Opcode::IndexLocal:
            if (instruction.operand != 1 || instruction.auxiliary >= index_sites.size()
                || instruction.member_site == 0 || instruction.member_site - 1 >= local_slot_count
                || instruction.variable_site >= names.size() || instructions.diagnostics[pc].condition_source.id == 0)
            { translation_error = "invalid bytecode local index"; return false; }
            ++register_top;
            if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
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
            state.diagnostics.emplace_back(instructions.diagnostics[pc].source.id, true);
            break;
        case Opcode::CallEnd:
            if (state.calls.empty() || state.calls.back() != std::pair<size_t, bool>{instruction.operand, true})
            { translation_error = "bytecode call frame mismatch"; return false; }
            state.calls.pop_back();
            if (state.diagnostics.empty() || state.diagnostics.back() != std::pair<size_t, bool>{instructions.diagnostics[pc].source.id, true})
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
        case Opcode::Increment: case Opcode::IncrementLocal: case Opcode::NumericForNext:
            if (instruction.opcode == Opcode::Increment && instruction.operand != 0 && instruction.operand - 1 >= index_sites.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            if (instruction.opcode == Opcode::IncrementLocal || instruction.opcode == Opcode::NumericForNext)
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
            frames.push_back(instructions.diagnostics[pc].source.id);
            state.diagnostics.emplace_back(instructions.diagnostics[pc].source.id, false);
            break;
        case Opcode::Leave:
            if (frames.empty() || frames.back() != instructions.diagnostics[pc].source.id)
            {
                translation_error = "bytecode diagnostic frame mismatch";
                return false;
            }
            frames.pop_back();
            if (state.diagnostics.empty() || state.diagnostics.back() != std::pair<size_t, bool>{instructions.diagnostics[pc].source.id, false})
            { translation_error = "bytecode diagnostic frame mismatch"; return false; }
            state.diagnostics.pop_back();
            break;
        case Opcode::Constant:
        case Opcode::ConstantLocal:
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
        {
            if (instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 2)
            { translation_error = "invalid bytecode binary math operand"; return false; }
            if (calls[instruction.operand].math_local_operands)
            {
                for (size_t index = 0; index < 2; ++index)
                    if (calls[instruction.operand].math_local_slots[index] >= local_slot_count
                        || calls[instruction.operand].math_local_names[index] >= names.size())
                    { translation_error = "invalid bytecode local math operand"; return false; }
                ++register_top;
                if (register_top > instructions.register_capacity) instructions.register_capacity = register_top;
            }
            else
            {
                if (register_top < 2) { translation_error = "bytecode binary math operand underflow"; return false; }
                --register_top;
            }
            break;
        }
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
            case Opcode::MethodPush: case Opcode::ArrayPushGlobal: case Opcode::MethodValue: case Opcode::BindArgument:
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
                return calls[instruction.operand].math_local_operands ? 0 : 2;
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
    for (auto& instruction : instructions.code)
    {
        if (instruction.opcode != Opcode::CallBegin && instruction.opcode != Opcode::BindArgument) continue;
        const size_t call_site = instruction.opcode == Opcode::CallBegin ? instruction.operand : instruction.auxiliary;
        if (call_site < calls.size() && native_functions.contains(names[calls[call_site].name_id]))
            instruction.opcode = Opcode::Removed;
    }
    return true;
}

CifaBytecode::CifaBytecode()
    : CifaBytecode(memory::default_resource()) {}

CifaBytecode::CifaBytecode(memory::Resource resource)
    : allocation_resource(resource ? std::move(resource) : memory::default_resource()), session(std::make_unique<Session>(*this))
{
    for (const auto* name : {"print", "println", "to_string", "to_number", "type", "size", "sprintf", "format", "run_string", "run_file",
        "exit", "max", "min", "ifv", "ifvalue",
        "abs", "sqrt", "cbrt", "round", "trunc", "nearbyint", "rint", "ceil", "floor",
        "sin", "cos", "tan", "asin", "acos", "atan", "sinh", "cosh", "tanh", "exp",
        "log", "log2", "log10", "erf", "erfc", "tgamma", "lgamma", "atan2", "pow",
        "hypot", "fmod", "remainder", "copysign", "fdim", "fmax", "fmin"})
        register_builtin(name);
}

CifaBytecode::~CifaBytecode() = default;

CifaBytecode::ProfileInstructionGuard::ProfileInstructionGuard(CifaBytecode& owner, std::string function_id,
    std::vector<std::string> stack_snapshot, size_t pc, size_t previous_pc)
    : owner(owner), function_id(std::move(function_id)), stack_snapshot(std::move(stack_snapshot)),
      pc(pc), previous_pc(previous_pc), start_ns(profile_now_ns())
{
}

CifaBytecode::ProfileInstructionGuard::~ProfileInstructionGuard()
{
    const std::uint64_t now_ns = profile_now_ns();
    owner.record_profile_instruction(function_id, pc, previous_pc, stack_snapshot,
        now_ns >= start_ns ? now_ns - start_ns : 0);
}

void CifaBytecode::set_profiling_enabled(bool enabled)
{
    profile_state = {};
    profile_state.enabled = enabled;
}

bool CifaBytecode::is_profiling_enabled() const
{
    return profile_state.enabled;
}

void CifaBytecode::reset_profile()
{
    const bool enabled = profile_state.enabled;
    profile_state = {};
    profile_state.enabled = enabled;
}

void CifaBytecode::profile_enter_function(const std::string& id)
{
    if (!profile_state.enabled || id.empty()) return;
    ++profile_state.functions[id].calls;
    profile_state.stack.push_back(id);
    profile_state.last_pc.push_back(std::numeric_limits<size_t>::max());
}

void CifaBytecode::profile_leave_function()
{
    if (!profile_state.enabled || profile_state.stack.size() <= 1) return;
    profile_state.stack.pop_back();
    profile_state.last_pc.pop_back();
}

void CifaBytecode::record_profile_instruction(const std::string& function_id, size_t pc,
    size_t previous_pc, const std::vector<std::string>& stack, std::uint64_t duration_ns)
{
    if (!profile_state.enabled || function_id.empty()) return;

    constexpr char separator = '\x1f';
    const std::string instruction_key = function_id + "#" + std::to_string(pc);
    auto& instruction = profile_state.instructions[instruction_key];
    ++instruction.count;
    instruction.time_ns += duration_ns;
    profile_state.total_ns += duration_ns;

    if (previous_pc != std::numeric_limits<size_t>::max())
    {
        const std::string edge_key = function_id + "#" + std::to_string(previous_pc)
            + ">" + std::to_string(pc);
        auto& edge = profile_state.edges[edge_key];
        ++edge.count;
        edge.time_ns += duration_ns;
    }

    for (size_t index = 0; index < stack.size(); ++index)
    {
        auto& function = profile_state.functions[stack[index]];
        ++function.instructions;
        function.total_ns += duration_ns;

        std::string path;
        path.reserve(stack[index].size() * (index + 1) + index);
        for (size_t path_index = 0; path_index <= index; ++path_index)
        {
            if (path_index != 0) path.push_back(separator);
            path += stack[path_index];
        }
        auto& flame = profile_state.flames[path];
        flame.total_ns += duration_ns;
        if (index + 1 == stack.size()) flame.self_ns += duration_ns;
    }

    if (!stack.empty()) profile_state.functions[stack.back()].self_ns += duration_ns;

    ++profile_state.instruction_count;
    if (profile_state.instruction_count >= profile_state.instruction_limit)
    {
        profile_state.truncated = true;
        profile_state.enabled = false;
    }

    if (!profile_state.stack.empty() && profile_state.stack.size() == stack.size()
        && profile_state.stack.back() == function_id)
        profile_state.last_pc.back() = pc;
}

CifaBytecode::NativeCallContext::NativeCallContext(Machine& value_machine, RegisterSlots& value_destination,
    size_t value_result_slot, RegisterSlots& value_arguments, const size_t* value_argument_slots,
    size_t value_argument_count, const std::pmr::vector<SourceLocation>& value_locations)
    : machine(value_machine), destination(value_destination), arguments(value_arguments),
      argument_slots(value_argument_slots), locations(value_locations), result_slot(value_result_slot),
      argument_count_value(value_argument_count) {}

bool CifaBytecode::NativeCallContext::is_empty(size_t index) const
{
    return index >= argument_count_value || arguments.empty(argument_slots[index]);
}

bool CifaBytecode::NativeCallContext::is_integer(size_t index) const
{
    if (index >= argument_count_value) return false;
    return value_holds<std::int64_t>(arguments.payload(argument_slots[index]));
}

bool CifaBytecode::NativeCallContext::is_number(size_t index) const
{
    if (index >= argument_count_value) return false;
    std::int64_t integer = 0;
    double floating = 0;
    bool is_double = false;
    return arguments.number(argument_slots[index], integer, floating, is_double);
}

bool CifaBytecode::NativeCallContext::is_boolean(size_t index) const
{
    if (index >= argument_count_value) return false;
    return value_holds<bool>(arguments.payload(argument_slots[index]));
}

bool CifaBytecode::NativeCallContext::is_string(size_t index) const
{
    if (index >= argument_count_value) return false;
    const auto& value = arguments.payload(argument_slots[index]);
    const auto* resource = value_get_if<std::any>(&value);
    return value.resource<std::pmr::string>() != nullptr;
}

std::int64_t CifaBytecode::NativeCallContext::to_integer(size_t index) const
{
    std::int64_t value = 0;
    if (index >= argument_count_value) const_cast<NativeCallContext*>(this)->report_error("native argument is missing");
    else if (!arguments.integer(argument_slots[index], value))
        machine.conversion_error(arguments, argument_slots[index], "int", locations.empty() ? nullptr : &locations[index]);
    return value;
}

double CifaBytecode::NativeCallContext::to_number(size_t index) const
{
    std::int64_t integer = 0;
    double floating = 0;
    bool is_double = false;
    if (index >= argument_count_value)
    {
        const_cast<NativeCallContext*>(this)->report_error("native argument is missing");
        return 0;
    }
    if (!arguments.number(argument_slots[index], integer, floating, is_double))
    {
        machine.conversion_error(arguments, argument_slots[index], "double", locations.empty() ? nullptr : &locations[index]);
        return 0;
    }
    return is_double ? floating : static_cast<double>(integer);
}

bool CifaBytecode::NativeCallContext::to_boolean(size_t index) const
{
    if (index >= argument_count_value) { const_cast<NativeCallContext*>(this)->report_error("native argument requires a boolean"); return false; }
    return machine.condition(arguments, argument_slots[index], locations.empty() ? nullptr : &locations[index]);
}

std::string CifaBytecode::NativeCallContext::to_string(size_t index) const
{
    if (index >= argument_count_value) { const_cast<NativeCallContext*>(this)->report_error("native argument is missing"); return {}; }
    const auto& value = arguments.payload(argument_slots[index]);
    const auto* resource = value_get_if<std::any>(&value);
    if (const auto* text = value.resource<std::pmr::string>()) return std::string(*text);
    machine.conversion_error(arguments, argument_slots[index], "string", locations.empty() ? nullptr : &locations[index]);
    return {};
}

void CifaBytecode::NativeCallContext::set_result(std::int64_t value) { destination.write_payload(result_slot, value); result_written = true; }
void CifaBytecode::NativeCallContext::set_result(double value) { destination.write_payload(result_slot, value); result_written = true; }
void CifaBytecode::NativeCallContext::set_result(bool value) { destination.write_payload(result_slot, value); result_written = true; }
void CifaBytecode::NativeCallContext::set_result(std::string value) { destination.write_text(result_slot, std::string_view(value)); result_written = true; }
void CifaBytecode::NativeCallContext::set_empty_result() { destination.clear(result_slot); result_written = true; }
void CifaBytecode::NativeCallContext::report_error(const std::string& message)
{
    machine.set_error(message, locations.empty() ? nullptr : &locations.front());
}

bool CifaBytecode::register_builtin(const std::string& name)
{
    native_functions[name] = NativeFunction{{}, true};
    native_function_names.insert(name);
    ++native_function_version;
    return true;
}

bool CifaBytecode::register_native_function(const std::string& name, native_func_type function)
{
    if (!function || !validate_registration_name(name)) return false;
    native_functions[name] = NativeFunction{std::move(function), false};
    native_function_names.insert(name);
    ++native_function_version;
    return true;
}

void CifaBytecode::translate(Cifa& compiler, size_t script_function_version, size_t host_native_function_version)
{
    compile_scope_effects.clear();
    module_data->int_type_id = intern_name("int");
    module_data->double_type_id = intern_name("double");
    source_lines.reserve(compiler.compilation_source_line_infos.size());
    for (auto& line : compiler.compilation_source_line_infos)
        source_lines.push_back({std::move(line.filename), line.line, std::move(line.text)});
    module_data->structures = compiler.compilation_struct_defs;
    compiled_valid = compiler.compiled && !compiler.compile_failed;
    module_data->host_function_version = optimization_enabled ? host_native_function_version : 0;
    module_data->script_function_version = optimization_enabled ? script_function_version : 0;
    module_data->freeze_script_functions = optimization_enabled;
    if (compiled_valid)
    {
        const bool custom_conversions = std::any_of(compiler.registered_types.begin(), compiler.registered_types.end(),
            [](const auto& entry)
            {
                const auto& name = entry.first;
                return name != "int" && name != "double" && name != "bool"
                    && name != "string" && name != "float" && name != "char";
            });
        compile_script_functions = &compiler.compilation_functions;
        compile_allows_script_constant_folding = optimization_enabled;
        analyze_binding_scopes(compiler.compilation_root, custom_conversions);
        const size_t mark = root_instructions.code.size();
        root_instructions.build_code.push_back({Opcode::LoopMark, source_ref(&compiler.compilation_root)});
        compile_blocks.emplace_back(mark, allocation_resource.get());
        for (const auto& child : compiler.compilation_root.v)
            if (child.type == CalUnitType::Label) compile_blocks.back().targets[child.str] = 0;
        bool has_root_statement = false;
        for (auto& child : compiler.compilation_root.v)
        {
            root_entries.push_back(root_instructions.build_code.size());
            if (child.type == CalUnitType::Label)
            {
                entry_labels[child.str] = root_entries.size() - 1;
                compile_blocks.back().targets[child.str] = root_instructions.build_code.size();
                continue;
            }
            if (has_root_statement) discard_statement_result(root_instructions.build_code, 0);
            emit_statement(child, root_instructions.build_code);
            has_root_statement = true;
        }
        for (const auto& jump : compile_blocks.back().jumps)
            root_instructions.build_code[jump.first].operand = compile_blocks.back().targets.at(jump.second);
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
                auto compiled = std::make_shared<FunctionCode>(allocation_resource.get());
                compiled->name = name;
                for (const auto& parameter : definition.arguments)
                    compiled->parameters.push_back({parameter.name, parameter.type_name});
                compiled->return_type = definition.return_type;
                compiled->body_source = source_ref(&definition.body);
                auto* saved_function = compiling_function;
                auto saved_local_scopes = std::move(compile_local_scopes);
                auto saved_local_scope_bases = std::move(compile_local_scope_bases);
                auto saved_array_locals = std::move(compile_array_locals);
                compiling_function = compiled.get();
                compile_local_scopes.emplace_back();
                for (const auto& parameter : compiled->parameters)
                {
                    const size_t slot = compiled->local_slot_count++;
                    compile_local_scopes.back().emplace(parameter.name, slot);
                }
                analyze_binding_scopes(definition.body, custom_conversions);
                emit(const_cast<CalUnit&>(definition.body), compiled->instructions.build_code);
                seal(compiled->instructions);
                seal_calls();
                if (translation_error.empty() && verify(compiled->instructions, compiled->local_slot_count))
                    compact(compiled->instructions);
                compiling_function = saved_function;
                compile_local_scopes = std::move(saved_local_scopes);
                compile_local_scope_bases = std::move(saved_local_scope_bases);
                compile_array_locals = std::move(saved_array_locals);
                function_code[name][arity] = std::move(compiled);
            }
        }
        compile_script_functions = nullptr;
        compile_scope_effects.clear();
        compile_allows_script_constant_folding = false;
    }
    source_ids.clear();
    compile_sources.clear();
}

    // 执行器上下文：持有 execute_instructions 循环全部可变状态的引用，
    // 使大型 opcode handler 可以拆分为独立 noinline 函数而保持语义与命名不变。
    struct CifaBytecode::InterpState
    {
        struct LoopState
        {
            size_t scopes; size_t local_scope_bases;
            std::pmr::vector<size_t> ranges; std::pmr::vector<size_t> switches;
            LoopState(size_t s, size_t l, std::pmr::memory_resource* r) : scopes(s), local_scope_bases(l), ranges(r), switches(r) {}
            LoopState(const LoopState& other) : LoopState(other.scopes,other.local_scope_bases,other.ranges.get_allocator().resource()) {
                ranges=other.ranges; switches=other.switches;
            }
            LoopState(LoopState&&) = default;
            LoopState& operator=(LoopState&&) = default;
            LoopState& operator=(const LoopState&) = default;
        };
        struct SwitchState { size_t condition_slot = 0; bool active = false; };
        struct RangeState { size_t snapshot_slot = 0; size_t index = 0; };
        struct MethodArguments { size_t base = 0; size_t count = 0; size_t received = 0; };
        struct RangeBinding { std::string name; size_t value_slot = 0; size_t slot = 0; };
        struct Frame
        {
            const Instructions* instructions;
            const SourceLocation* node;
            size_t pc;
            size_t call_pc;
            size_t return_register;
            RegisterSlots* locals;
            ScopeStack scopes;
            std::pmr::vector<size_t> local_scope_bases;
            std::pmr::vector<std::optional<LoopState>> loops;
            std::pmr::unordered_map<size_t, SwitchState> switches;
            const FunctionCode* function;
            const SourceLocation* call;
            std::pmr::unordered_map<size_t, RangeState> ranges;
            std::pmr::unordered_map<size_t, MethodArguments> method_arguments;
            size_t method_argument_top;
            const Module* owner;
            std::shared_ptr<const Module> module;
            std::pmr::vector<unsigned char> aliases;
            size_t register_base;
            size_t register_size;
            size_t register_top;
        };

        Machine& machine;
        CifaBytecode& interpreter;
        RegisterSlots& registers;
        ScopeStack& scopes;
        std::pmr::vector<Scope>& reusable_scopes;
        Object& result;
        std::pmr::deque<Frame>& frames;
        std::pmr::deque<RegisterSlots>& local_windows;
        RegisterSlots*& active_locals;
        std::pmr::vector<unsigned char>& active_aliases;
        std::pmr::vector<std::optional<LoopState>>& loop_states;
        std::pmr::vector<size_t>& local_scope_bases;
        std::pmr::unordered_map<size_t, SwitchState>& switches;
        std::pmr::unordered_map<size_t, RangeState>& ranges;
        std::pmr::unordered_map<size_t, MethodArguments>& method_arguments;
        size_t& method_argument_top;
        std::optional<RangeBinding>& range_binding;
        std::pmr::unordered_map<const Module*, std::pmr::vector<size_t>>& global_links;
        std::pmr::vector<size_t>*& active_globals;
        const Module*& active_owner;
        std::shared_ptr<const Module>& active_module;
        const Instructions*& active_instructions;
        const SourceLocation*& active_node;
        const FunctionCode*& active_function;
        const SourceLocation*& active_call;
        const SourceLocation*& current_source;
        std::pmr::vector<Machine::ReturnState>& return_states;
        SourceRef& register_assignment_source;
        size_t& error_pc;
        size_t& pc;
        const size_t missing_global;
        std::pmr::memory_resource* persistent;
        std::pmr::monotonic_buffer_resource& execution_scratch;

        size_t input_slot(const Instruction& instruction, size_t index)
        {
            if (index >= instruction.input_count
                || instruction.input_offset + index >= active_instructions->register_inputs.size())
                return std::numeric_limits<size_t>::max();
            return active_instructions->register_inputs[instruction.input_offset + index];
        }
        const InstructionDiagnostic& current_diagnostic()
        {
            return active_instructions->diagnostics[error_pc];
        }
        const SourceLocation& current_location()
        {
            if (current_source == nullptr)
            {
                const auto source = current_diagnostic().source;
                current_source = source.id != 0 ? &active_owner->source(source) : active_node;
            }
            return *current_source;
        }
        void release_scope()
        {
            if (!scopes.back().dynamic_registers)
            {
                scopes.back().bindings.clear();
                reusable_scopes.push_back(std::move(scopes.back()));
            }
            scopes.pop_back();
        }
        void release_scopes_to(size_t size)
        {
            while (scopes.size() > size) release_scope();
        }
        std::pmr::vector<size_t>& link_globals(const Module* owner)
        {
            auto [entry, inserted] = global_links.try_emplace(owner);
            if (inserted) entry->second.resize(owner->names.size(), missing_global);
            return entry->second;
        }
        size_t linked_global(size_t name_id)
        {
            auto& linked = (*active_globals)[name_id];
            if (linked == missing_global) linked = machine.find_global_slot(active_owner->names[name_id]);
            return linked;
        }
        Machine::NamedValueRef alias_target(size_t name_id)
        {
            const size_t linked = linked_global(name_id);
            if (linked != missing_global && machine.global_exists_at(linked))
            {
                const size_t index = machine.global_values.base() + linked;
                return {&machine.global_values, linked,
                    machine.global_values.type_pool[machine.global_values.slot_types[index]].element, true};
            }
            auto value = machine.assign_named(active_owner->names[name_id], "", false, false, {});
            (*active_globals)[name_id] = value.slot;
            return value;
        }
        void read_alias(size_t destination, size_t name_id, const SourceLocation& location)
        {
            const auto& name = active_owner->names[name_id];
            if (auto* binding = machine.find_slot(name))
            {
                registers.copy(destination, *binding->file, binding->slot);
            }
            else
            {
                const size_t linked = linked_global(name_id);
                if (linked != missing_global && machine.global_exists_at(linked))
                {
                    registers.copy(destination, machine.global_values, linked);
                }
                else
                {
                    machine.read_named(registers, destination, name, "", false, false, false, location);
                }
            }
        }
        void bind_local_storage(const std::string& name, size_t slot, bool current_only)
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
        }
        bool initialize_numeric(size_t slot, RegisterSlots& source, size_t argument, const VariableSite& binding)
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
        }
        void finish_call()
        {
            auto& caller = frames.back();
            RegisterSlots return_value(registers, caller.register_base + caller.return_register, 1);
            if (active_function && return_value.empty(0))
            {
                return_value.write_payload(0, std::any(Object::NoValue{active_function->name, Machine::format_frame(*active_call)}));
                return_value.set_type(0, {typeid(void), "", "", "NoValue"});
            }
            auto saved = std::move(frames.back());
            frames.pop_back();
            machine.host.profile_leave_function();
            return_states.pop_back();
            local_windows.pop_back();
            active_locals = saved.locals;
            active_aliases = std::move(saved.aliases);
            scopes = std::move(saved.scopes);
            local_scope_bases = std::move(saved.local_scope_bases);
            registers.restore(saved.register_base, saved.register_size, saved.register_top);
            loop_states = std::move(saved.loops);
            switches = std::move(saved.switches);
            ranges = std::move(saved.ranges);
            method_arguments = std::move(saved.method_arguments);
            method_argument_top = saved.method_argument_top;
            active_owner = saved.owner;
            active_globals = &link_globals(active_owner);
            active_module = std::move(saved.module);
            active_instructions = saved.instructions;
            active_node = saved.node;
            active_function = std::move(saved.function);
            active_call = saved.call;
            pc = saved.pc;
        }

        bool op_index(const Instruction& instruction);
        bool op_index_local(const Instruction& instruction);
        bool op_call(const Instruction& instruction);
        bool op_store_increment(const Instruction& instruction);
        bool op_register_binary(const Instruction& instruction);
        bool op_method_push(const Instruction& instruction);
        bool op_array_push_global_local(const Instruction& instruction);
        bool op_method_begin(const Instruction& instruction);
        bool op_method_value(const Instruction& instruction);
        bool op_method_no_args(const Instruction& instruction);
        bool op_range_next(const Instruction& instruction);
        bool op_array(const Instruction& instruction);
        bool op_call_begin(const Instruction& instruction);
        bool op_unwind(const Instruction& instruction);
        bool op_prepare_store(const Instruction& instruction);
        bool op_return(const Instruction& instruction);
        bool op_register_snapshot(const Instruction& instruction);
        bool op_numeric_binary_local(const Instruction& instruction);
        bool op_size(const Instruction& instruction);
        bool op_math_unary(const Instruction& instruction);
        bool op_math_binary(const Instruction& instruction);
    };
CIFA_NOINLINE bool CifaBytecode::InterpState::op_index(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->index_sites[instruction.auxiliary];
        const auto& name = active_owner->names[site.name_id];
        Machine::NamedValueRef receiver;
        if (!site.declaration && site.local_slot != 0 && site.local_slot - 1 < active_locals->size()
            && !active_aliases[site.local_slot - 1])
        {
            const size_t slot = site.local_slot - 1;
            const size_t absolute = active_locals->base() + slot;
            receiver = {active_locals, slot,
                active_locals->type_pool[active_locals->slot_types[absolute]].element, true};
        }
        else receiver = machine.named_value(name);
        auto* container = receiver.resource();
        auto* array = container ? container->resource<VmArray>() : nullptr;
        const bool map_access = (container && container->resource<VmMap>()) || site.string_index;
        if (site.declaration)
        {
            if (auto* binding = machine.find_slot(name))
            {
                auto& file = *binding->file;
                const size_t slot = binding->slot;
                std::int64_t requested = 0;
                if (site.dimensions != 0)
                {
                    const size_t argument = input_slot(instruction, instruction.input_count - instruction.operand);
                    if (!registers.empty(argument) && !registers.integer(argument, requested))
                    {
                        machine.conversion_error(registers, argument, "int", nullptr);
                        requested = 0;
                    }
                }
                const size_t count = requested < 0 ? 0 : static_cast<size_t>(requested);
                if (!array)
                {
                    file.write_payload(slot, BytecodeValue::Storage(VmArray(count, machine.host.allocation_resource)));
                    array = file.resource_payload(slot).resource<VmArray>();
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
                    const size_t argument = input_slot(instruction, instruction.input_count - instruction.operand + index);
                    if (argument != output) registers.clear(argument);
                }
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                return true;
            }
        }
        if (active_owner->host_function_version != 0 && !site.declaration && site.dimensions == 1
            && array != nullptr)
        {
            const size_t index_slot = input_slot(instruction, instruction.input_count - 1);
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
                return false;
            }
            const auto index = static_cast<size_t>(offset);
            auto& values = array->values;
            if (index >= values.size())
            {
                values.resize(index + 1);
                machine.read_indexed(registers, output,
                    {&values[index], nullptr, name, receiver.element_type}, false);
            }
            else
            {
                const auto& readable = std::as_const(values);
                const auto& element = readable[index];
                if (element.empty()) machine.set_error("array element '" + name + "' has not been initialized");
                registers.store_payload(output, element);
                registers.set_name(output, name);
            }
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            return true;
        }
        auto element = machine.indexed(name, active_owner->names[site.type_id],
            site.dimensions, site.declaration, false, false, registers,
            active_instructions->register_inputs.data() + instruction.input_offset + instruction.input_count - instruction.operand);
        machine.read_indexed(registers, output, element, map_access);
        for (size_t index = 0; index < instruction.operand; ++index)
        {
            const size_t argument = input_slot(instruction, instruction.input_count - instruction.operand + index);
            if (argument != output) registers.clear(argument);
        }
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_index_local(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->index_sites[instruction.auxiliary];
        const auto& name = active_owner->names[site.name_id];
        const size_t local_slot = instruction.member_site - 1;
        const auto& local_name = active_owner->names[instruction.variable_site];
        RegisterSlots* index_file = active_locals;
        size_t source_slot = local_slot;
        if (active_aliases[local_slot])
            for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
                if (auto* found = scope->find(local_name))
                {
                    index_file = found->file;
                    source_slot = found->slot;
                    break;
                }
        if (index_file->empty(source_slot))
        {
            machine.set_error("variable '" + local_name + "' has not been initialized",
                &active_owner->source(current_diagnostic().condition_source));
            result = machine.error_result();
            return false;
        }
        Machine::NamedValueRef receiver;
        if (site.local_slot != 0 && site.local_slot - 1 < active_locals->size()
            && !active_aliases[site.local_slot - 1])
        {
            const size_t slot = site.local_slot - 1;
            const size_t absolute = active_locals->base() + slot;
            receiver = {active_locals, slot,
                active_locals->type_pool[active_locals->slot_types[absolute]].element, true};
        }
        else receiver = machine.named_value(name);
        auto* container = receiver.resource();
        auto* array = container ? container->resource<VmArray>() : nullptr;
        if (array != nullptr)
        {
            std::int64_t offset = 0;
            if (!index_file->integer(source_slot, offset))
                machine.conversion_error(*index_file, source_slot, "int", nullptr);
            if (offset < 0) machine.set_error("array index is out of range");
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            const size_t index = static_cast<size_t>(offset);
            auto& values = array->values;
            if (index >= values.size())
            {
                values.resize(index + 1);
                machine.read_indexed(registers, output,
                    {&values[index], nullptr, name, receiver.element_type}, false);
            }
            else
            {
                const auto& element = std::as_const(values)[index];
                if (element.empty()) machine.set_error("array element '" + name + "' has not been initialized");
                registers.store_payload(output, element);
                registers.set_name(output, name);
            }
        }
        else
        {
            registers.copy(output, *index_file, source_slot);
            const size_t index_slot = output;
            auto element = machine.indexed(name, active_owner->names[site.type_id], 1, false,
                false, false, registers, &index_slot);
            machine.read_indexed(registers, output, element,
                (container && container->resource<VmMap>()) || site.string_index);
        }
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_call(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        auto& call = active_owner->calls[instruction.operand];
        auto& call_source = active_owner->source(call.source);
        const auto& call_name = active_owner->names[call.name_id];
        if (interpreter.native_functions.contains(call_name))
        {
            const size_t* argument_slots = active_instructions->register_inputs.data()
                + instruction.input_offset + instruction.input_count - call.arguments.size();
            if (machine.call_native_registers(call_name, registers, output,
                registers, argument_slots, call.arguments.size(), call.arguments))
            {
                for (size_t index = 0; index < call.arguments.size(); ++index)
                    if (argument_slots[index] != output) registers.clear(argument_slots[index]);
                if (active_owner->host_function_version != 0
                    && active_owner->host_function_version != machine.host.native_function_version)
                    machine.set_error("native functions changed during optimized bytecode execution", &call_source);
                if (active_owner->freeze_script_functions
                    && active_owner->script_function_version != machine.function_version)
                    machine.set_error("script functions changed during optimized bytecode execution", &call_source);
                if (machine.exit_requested) { result = Object(); return false; }
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                return true;
            }
            if (active_owner->host_function_version != 0
                && active_owner->host_function_version != machine.host.native_function_version)
            {
                machine.set_error("native functions changed during optimized bytecode execution", &call_source);
            }
            if (active_owner->freeze_script_functions
                && active_owner->script_function_version != machine.function_version)
            {
                machine.set_error("script functions changed during optimized bytecode execution", &call_source);
            }
        }
        else
        {
            std::shared_ptr<const Module> function_module;
            const auto* cached = machine.find_cached_function(call, call_name, call.arguments.size(), function_module);
            if (!cached)
            {
                machine.set_error("bytecode function was not prepared: " + call_name);
                result = machine.error_result();
                return false;
            }
            ScopeStack locals(scopes.get_allocator());
            locals.emplace_back(machine.host.allocation_resource);
            const size_t caller_base = registers.base();
            const size_t caller_size = registers.size();
            const size_t local_count = (std::max)(cached->local_slot_count, call.arguments.size());
            const size_t caller_top = registers.top();
            RegisterSlots caller_values(registers, caller_base, caller_size);
            registers.enter(local_count);
            local_windows.emplace_back(registers, registers.base(), registers.size());
            auto* local_values = &local_windows.back();
            for (size_t index = 0; index < call.arguments.size(); ++index)
            {
                const auto& parameter = cached->parameters[index];
                const size_t argument = input_slot(instruction, instruction.input_count - call.arguments.size() + index);
                if (parameter.type_name.empty() || parameter.type_name == "auto")
                    local_values->move(index, caller_values, argument);
                else
                {
                    machine.convert_type(*local_values, index, caller_values, argument, parameter.type_name, call.arguments[index]);
                    caller_values.clear(argument);
                }
                if ((parameter.type_name == "int" || parameter.type_name == "double")
                    && local_values->payload(index).index() >= 1 && local_values->payload(index).index() <= 3)
                {
                    local_values->bind_numeric(index, parameter.type_name == "int"
                        ? RegisterSlots::NumericBinding::Int : RegisterSlots::NumericBinding::Double);
                }
                else machine.bind_type(*local_values, index, parameter.type_name, call.arguments[index]);
                if (!local_values->has_name(index)) local_values->set_name(index, call.arguments[index].str);
                locals.back().bind(parameter.name, local_values, index);
            }
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            frames.push_back({active_instructions, active_node, pc, pc - 1, output,
                active_locals, std::move(scopes), std::move(local_scope_bases), std::move(loop_states), std::move(switches), active_function, active_call,
                std::move(ranges), std::move(method_arguments), method_argument_top, active_owner, std::move(active_module), std::move(active_aliases),
                caller_base, caller_size, caller_top});
            active_function = cached;
            if (machine.host.profile_state.enabled)
                machine.host.profile_enter_function("fn:" + cached->name + "@" + std::to_string(cached->parameters.size()));
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
            return_states.emplace_back();
            return_states.back().return_type = cached->return_type;
            pc = 0;
        }
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_store_increment(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& source = current_location();
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
                if (variable->with_type) machine.bind_type(*target_file, target_slot, type, active_owner->source(current_diagnostic().target_source));
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                const bool post = increment && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract);
                if (post) registers.copy(output, *target_file, target_slot);
                const size_t computed = registers.size() - 2;
                const size_t conversion = registers.size() - 1;
                const size_t argument = increment ? conversion : input_slot(instruction, instruction.input_count - 1);
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
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                registers.clear(argument);
                registers.clear(computed);
                registers.clear(conversion);
                if (!post) registers.copy(output, *target_file, target_slot);
                return true;
            }
        }
        Machine::IndexedValueRef target;
        if (indexed && !increment && instruction.write == WriteOperation::Assign && site->dimensions == 1)
        {
            const auto& name = active_owner->names[site->name_id];
            if (auto* binding = machine.find_slot(name))
            {
                auto* array = binding->file->resource_payload(binding->slot).resource<VmArray>();
                if (array != nullptr)
                {
                    const size_t index_slot = input_slot(instruction, instruction.input_count - 2);
                    std::int64_t offset = 0;
                    if (!registers.integer(index_slot, offset))
                        machine.conversion_error(registers, index_slot, "int", nullptr);
                    if (offset < 0)
                    {
                        machine.set_error("array index is out of range");
                        result = machine.error_result();
                        return false;
                    }
                    const size_t index = static_cast<size_t>(offset);
                    auto& values = array->values;
                    if (index >= values.size()) values.resize(index + 1);
                    const size_t argument = input_slot(instruction, instruction.input_count - 1);
                    const size_t absolute = binding->file->base() + binding->slot;
                    const auto& element_type = binding->file->type_pool[binding->file->slot_types[absolute]].element;
                    if (element_type.empty())
                    {
                        values[index] = std::move(registers.resource_payload(argument));
                        registers.clear(argument);
                    }
                    else
                    {
                        const size_t converted = registers.size() - 1;
                        if (!machine.convert_type(registers, converted, registers, argument, element_type, source))
                        {
                            result = machine.error_result();
                            return false;
                        }
                        registers.clear(argument);
                        values[index] = std::move(registers.resource_payload(converted));
                        registers.clear(converted);
                    }
                    registers.store_payload(output, values[index]);
                    registers.set_name(output, name);
                    return true;
                }
            }
        }
        if (member)
            target = machine.resolve_member(
                active_owner->names[active_owner->member_sites[instruction.member_site - 1].first],
                active_owner->names[active_owner->member_sites[instruction.member_site - 1].second]);
        else if (indexed)
            target = machine.indexed(
                active_owner->names[site->name_id], active_owner->names[site->type_id],
                site->dimensions, false, false, increment && site->with_type, registers,
                active_instructions->register_inputs.data() + instruction.input_offset + instruction.input_count
                    - (increment ? 0 : 1) - site->dimensions);
        else
        {
            const auto named = machine.assign_named(active_owner->names[variable->name_id], active_owner->names[variable->type_id],
                variable->with_type, increment && variable->with_type, active_owner->source(current_diagnostic().target_source));
            target = {nullptr, nullptr, active_owner->names[variable->name_id], named.element_type, named.file, named.slot};
        }
        const bool post = increment && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract);
        if (post) machine.read_indexed(registers, output, target, false);
        const size_t computed = registers.size() - 2;
        const size_t argument = increment ? registers.size() - 1 : input_slot(instruction, instruction.input_count - 1);
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
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        if (target.file && !target.compact && !target.object)
        {
            const bool with_type = variable != nullptr ? variable->with_type : site != nullptr && site->with_type;
            const std::string& type_name = variable != nullptr ? active_owner->names[variable->type_id]
                : site != nullptr ? active_owner->names[site->type_id] : std::string();
            if (with_type) machine.bind_type(*target.file, target.slot, type_name, source);
            if (!machine.should_stop()) machine.assign(*target.file, target.slot, registers,
                value_slot, registers.size() - 1, source);
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            if (argument != value_slot) registers.clear(argument);
            if (!post) registers.copy(output, *target.file, target.slot);
            return true;
        }
        Object value;
        registers.export_argument(value_slot, value);
        if (argument != value_slot) registers.clear(argument);
        const bool with_type = variable != nullptr ? variable->with_type : site != nullptr && site->with_type;
        const std::string& type_name = variable != nullptr ? active_owner->names[variable->type_id]
            : site != nullptr ? active_owner->names[site->type_id] : std::string();
        machine.assign_indexed(target, std::move(value), with_type, type_name, source);
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        if (instruction.auxiliary != 0)
        {
            const size_t slot = instruction.auxiliary - 1;
            if (slot >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return false;
            }
        }
        if (!post) machine.read_indexed(registers, output, target, false);
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_register_binary(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->register_binary_sites[instruction.operand];
        const size_t binary_destination = site.temporary_destination != 0
            ? active_instructions->register_capacity + site.temporary_destination - 1
            : (site.code.flags & 1) != 0 ? registers.size() - 1 : output;
        if (site.left_temporary && site.right_temporary)
        {
            const size_t left = active_instructions->register_capacity + site.code.left;
            const size_t right = active_instructions->register_capacity + site.code.right;
            if (!registers.binary(static_cast<Opcode>(site.code.opcode), binary_destination, left, right, machine, current_location()))
            {
                registers.binary_fallback(static_cast<Opcode>(site.code.opcode), binary_destination, left, right, machine, current_location(), true);
            }
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            if (site.code.destination != 0)
            {
                const size_t slot = site.code.destination - 1;
                const auto& binding = active_owner->variable_sites[site.variable_site - 1];
                const auto& name = active_owner->names[binding.name_id];
                const bool initialized = initialize_numeric(slot, registers, binary_destination, binding);
                const bool assigned = !initialized && !binding.with_type && !active_aliases[slot]
                    && active_locals->assign_numeric(slot, registers, binary_destination);
                if (initialized || assigned)
                {
                    registers.clear(binary_destination);
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *active_locals, slot);
                    return true;
                }
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
                        active_owner->names[binding.type_id], current_location());
                    const size_t conversion = binary_destination == registers.size() - 1 ? registers.size() - 2 : registers.size() - 1;
                    if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, binary_destination, conversion, current_location());
                    if (machine.should_stop()) { result = machine.error_result(); return false; }
                    registers.clear(binary_destination);
                    if (binding.with_type)
                    {
                        if (!existing) scopes.back().bind(name, active_locals, slot);
                        active_aliases[slot] = existing && (existing->file != active_locals || existing->slot != slot);
                    }
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target_file, target_slot);
                    register_assignment_source = {};
                    return true;
                }
                const auto target = alias_target(binding.name_id);
                if (binding.with_type) machine.bind_type(*target.file, target.slot,
                    active_owner->names[binding.type_id], current_location());
                const size_t conversion = binary_destination == registers.size() - 1 ? registers.size() - 2 : registers.size() - 1;
                if (!machine.should_stop()) machine.assign(*target.file, target.slot, registers,
                    binary_destination, conversion, current_location());
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                bind_local_storage(name, slot, false);
                if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target.file, target.slot);
                register_assignment_source = {};
            }
            return true;
        }
        if ((site.left_constant || site.left_temporary || !active_aliases[site.code.left])
            && (site.right_constant || site.right_temporary || !active_aliases[site.code.right]))
        {
            const size_t scratch_left = registers.size() - 2;
            const auto read_number = [&](bool constant, bool temporary_value, size_t slot,
                std::int64_t& integer, double& floating, bool& is_double)
            {
                if (temporary_value)
                    return registers.number(active_instructions->register_capacity + slot, integer, floating, is_double);
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
            if (read_number(site.left_constant, site.left_temporary, site.code.left, left_integer, left_number, left_double)
                && read_number(site.right_constant, site.right_temporary, site.code.right, right_integer, right_number, right_double)
                && registers.binary_numbers(static_cast<Opcode>(site.code.opcode), scratch_left,
                    left_integer, left_number, left_double, right_integer, right_number, right_double, machine, current_location()))
            {
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                if (site.code.destination == 0)
                {
                    registers.move(binary_destination, registers, scratch_left);
                    return true;
                }
                const size_t slot = site.code.destination - 1;
                const auto& binding = active_owner->variable_sites[site.variable_site - 1];
                const bool initialized = initialize_numeric(slot, registers, scratch_left, binding);
                const bool assigned = !initialized && !binding.with_type && !active_aliases[slot]
                    && active_locals->assign_numeric(slot, registers, scratch_left);
                if (initialized || assigned)
                {
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *active_locals, slot);
                    registers.clear(scratch_left);
                    return true;
                }
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
                        active_owner->names[binding.type_id], current_location());
                    if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, scratch_left, registers.size() - 1, current_location());
                    if (machine.should_stop()) { result = machine.error_result(); return false; }
                    registers.clear(scratch_left);
                    if (binding.with_type)
                    {
                        if (!existing) scopes.back().bind(name, active_locals, slot);
                        active_aliases[slot] = existing && (existing->file != active_locals || existing->slot != slot);
                    }
                    if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target_file, target_slot);
                    register_assignment_source = {};
                    return true;
                }
                const auto target = alias_target(binding.name_id);
                if (binding.with_type) machine.bind_type(*target.file, target.slot,
                    active_owner->names[binding.type_id], current_location());
                if (!machine.should_stop()) machine.assign(*target.file, target.slot, registers,
                    scratch_left, registers.size() - 1, current_location());
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                bind_local_storage(name, slot, false);
                if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target.file, target.slot);
                register_assignment_source = {};
                return true;
            }
            else
            {
            }
            registers.clear(scratch_left);
        }
        const size_t left_slot = registers.size() - 2;
        const size_t right_slot = registers.size() - 1;
        const auto read_register = [&](size_t destination, bool constant, bool temporary_value,
            size_t slot, size_t name_id, SourceRef reference)
        {
            if (constant) { registers.write_payload(destination, active_owner->constants[slot].value); return; }
            if (temporary_value)
            {
                registers.copy(destination, registers, active_instructions->register_capacity + slot);
                return;
            }
            const auto& name = active_owner->names[name_id];
            if (active_aliases[slot])
                read_alias(destination, name_id, active_owner->source(reference));
            else registers.copy(destination, *active_locals, slot);
            if (registers.empty(destination))
                machine.set_error("variable '" + name + "' has not been initialized", &active_owner->source(reference));
        };
        read_register(left_slot, site.left_constant, site.left_temporary,
            site.code.left, site.left_name, site.left_source);
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        read_register(right_slot, site.right_constant, site.right_temporary,
            site.code.right, site.right_name, site.right_source);
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        registers.binary_fallback(static_cast<Opcode>(site.code.opcode), left_slot, left_slot, right_slot, machine, current_location(), true);
        if (machine.should_stop()) { result = machine.error_result(); return false; }
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
                    active_owner->names[binding.type_id], current_location());
                if (!machine.should_stop()) machine.assign(*target_file, target_slot, registers, left_slot, right_slot, current_location());
                if (machine.should_stop()) { result = machine.error_result(); return false; }
                registers.clear(left_slot);
                if (binding.with_type)
                {
                    if (!existing) scopes.back().bind(name, active_locals, slot);
                    active_aliases[slot] = existing && (existing->file != active_locals || existing->slot != slot);
                }
                if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target_file, target_slot);
                register_assignment_source = {};
                return true;
            }
            const auto target = alias_target(binding.name_id);
            if (binding.with_type) machine.bind_type(*target.file, target.slot,
                active_owner->names[binding.type_id], active_owner->source(site.assignment_source));
            if (!machine.should_stop()) machine.assign(*target.file, target.slot, registers,
                left_slot, right_slot, active_owner->source(site.assignment_source));
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            bind_local_storage(name, slot, false);
            if ((site.code.flags & 1) == 0 && !instruction.discard_result) registers.copy(output, *target.file, target.slot);
        }
        else registers.move(binary_destination, registers, left_slot);
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_method_push(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->calls[instruction.operand];
        const size_t argument = input_slot(instruction, instruction.input_count - 1);
        Machine::NamedValueRef receiver;
        if (site.local_slot != 0 && site.local_slot - 1 < active_locals->size()
            && !active_aliases[site.local_slot - 1])
        {
            const size_t slot = site.local_slot - 1;
            const size_t absolute = active_locals->base() + slot;
            receiver = {active_locals, slot,
                active_locals->type_pool[active_locals->slot_types[absolute]].element, true};
        }
        else receiver = machine.named_value(active_owner->names[site.base_name_id]);
        auto* container = receiver.resource();
        auto* array = receiver.file && container ? container->resource<VmArray>() : nullptr;
        const auto& location = active_owner->source(site.method_source);
        if (!array)
        {
            registers.clear(argument);
            machine.set_error("push_back() requires an array or map", &location);
            registers.clear(output);
        }
        else
        {
            auto& values = array->values;
            if (receiver.element_type.empty())
            {
                values.push_back(std::move(registers.resource_payload(argument)));
                registers.clear(argument);
                if (!instruction.discard_result) registers.write_payload(output, double(values.size()));
            }
            else
            {
                Object value;
                registers.export_argument(argument, value);
                value = machine.convert_type(value, receiver.element_type, location);
                if (!machine.should_stop())
                {
                    values.emplace_back(std::move(value.value));
                    if (!instruction.discard_result) registers.write_payload(output, double(values.size()));
                }
            }
        }
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_array_push_global_local(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->calls[instruction.operand];
        const size_t local_slot = instruction.auxiliary - 1;
        const size_t global_slot = linked_global(site.base_name_id);
        auto* container = global_slot != missing_global && machine.global_exists_at(global_slot)
            ? &machine.global_values.resource_payload(global_slot) : nullptr;
        auto* array = container ? container->resource<VmArray>() : nullptr;
        if (local_slot >= active_locals->size())
            machine.set_error("bytecode local slot out of range");
        else if (!array)
            machine.set_error("push_back() requires an array or map", &active_owner->source(site.method_source));
        else
        {
            const auto& load_source = active_owner->source(current_diagnostic().condition_source);
            const auto& local_name = active_owner->names[instruction.member_site];
            RegisterSlots* source_file = active_locals;
            size_t source_slot = local_slot;
            if (active_aliases[local_slot])
                for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
                    if (auto* found = scope->find(local_name))
                    {
                        source_file = found->file;
                        source_slot = found->slot;
                        break;
                    }
            if (source_file->empty(source_slot))
                machine.set_error("variable '" + local_name + "' has not been initialized", &load_source);
            if (!machine.should_stop())
            {
                const size_t absolute = machine.global_values.base() + global_slot;
                const auto& element_type = machine.global_values.type_pool[
                    machine.global_values.slot_types[absolute]].element;
                if (element_type.empty()) array->values.emplace_back(source_file->payload(source_slot));
                else
                {
                    Object value;
                    source_file->export_object(source_slot, value);
                    value = machine.convert_type(value, element_type, active_owner->source(site.method_source));
                    if (!machine.should_stop()) array->values.emplace_back(std::move(value.value));
                }
                if (!machine.should_stop()) registers.write_payload(output, double(array->values.size()));
            }
        }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_method_begin(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->calls[instruction.operand];
        const auto& method_name = active_owner->names[site.name_id];
        auto receiver = machine.named_value(active_owner->names[site.base_name_id]);
        alignas(std::max_align_t) std::byte argument_buffer[2048];
        std::pmr::monotonic_buffer_resource argument_scratch(argument_buffer,sizeof(argument_buffer),persistent);
        std::pmr::vector<SourceLocation> arguments(&argument_scratch);
        RegisterSlots values(registers, registers.base() + active_instructions->register_capacity
            + active_instructions->temporary_count + active_instructions->switch_count
            + active_instructions->range_count + 1, 0);
        if (!site.arguments.empty())
        {
            if (!receiver.size())
            {
                machine.set_error(method_name + "() requires an array or map",
                    &active_owner->source(site.method_source));
                result = machine.error_result();
                return false;
            }
            const size_t count = method_name == "push_back" ? site.arguments.size()
                : method_name == "insert" ? std::min<size_t>(2, site.arguments.size()) : 1;
            method_arguments[instruction.operand] = MethodArguments{method_argument_top, count, 0};
            method_argument_top += count;
            registers.clear(output);
            return true;
        }
        machine.call_method(registers, output, method_name, active_owner->source(site.method_source), receiver, arguments, values);
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        const size_t local_slot = active_owner->calls[instruction.operand].local_slot;
        if (local_slot != 0)
        {
            if (local_slot - 1 >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return false;
            }
        }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_method_value(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->calls[instruction.operand];
        const auto& method_name = active_owner->names[site.name_id];
        auto receiver = machine.named_value(active_owner->names[site.base_name_id]);
        alignas(std::max_align_t) std::byte argument_buffer[2048];
        std::pmr::monotonic_buffer_resource argument_scratch(argument_buffer,sizeof(argument_buffer),persistent);
        std::pmr::vector<SourceLocation> arguments(&argument_scratch);
        RegisterSlots values(registers, registers.base() + active_instructions->register_capacity
            + active_instructions->temporary_count + active_instructions->switch_count
            + active_instructions->range_count + 1, 0);
        auto& pending = method_arguments[instruction.operand];
        const size_t argument = active_instructions->register_capacity + active_instructions->temporary_count
            + active_instructions->switch_count + active_instructions->range_count
            + 1 + pending.base + pending.received++;
        registers.move(argument, registers, input_slot(instruction, instruction.input_count - 1));
        if (method_name == "insert" && pending.received < 2)
        {
            const auto size = receiver.size();
            if (!size)
            {
                machine.set_error(method_name + "() requires an array or map",
                    &active_owner->source(site.method_source));
                result = machine.error_result();
                return false;
            }
            registers.write_payload(output, double(*size));
            return true;
        }
        values = RegisterSlots(registers, registers.base() + active_instructions->register_capacity
            + active_instructions->temporary_count + active_instructions->switch_count
            + active_instructions->range_count + 1 + pending.base, pending.count);
        method_argument_top = pending.base;
        method_arguments.erase(instruction.operand);
        for (size_t index = 0; index < values.size(); ++index)
            arguments.push_back(site.arguments[method_name == "insert" ? index : instruction.auxiliary]);
        machine.call_method(registers, output, method_name, active_owner->source(site.method_source), receiver, arguments, values);
        if (machine.exit_requested) { result = Object(); return false; }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        const size_t local_slot = active_owner->calls[instruction.operand].local_slot;
        if (local_slot != 0)
        {
            if (local_slot - 1 >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return false;
            }
        }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_method_no_args(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& site = active_owner->calls[instruction.operand];
        auto receiver = machine.named_value(active_owner->names[site.base_name_id]);
        alignas(std::max_align_t) std::byte argument_buffer[2048];
        std::pmr::monotonic_buffer_resource argument_scratch(argument_buffer,sizeof(argument_buffer),persistent);
        std::pmr::vector<SourceLocation> arguments(&argument_scratch);
        RegisterSlots values(registers, registers.base() + active_instructions->register_capacity
            + active_instructions->temporary_count + active_instructions->switch_count
            + active_instructions->range_count + 1, 0);
        machine.call_method(registers, output, active_owner->names[site.name_id],
            active_owner->source(site.method_source), receiver, arguments, values);
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        if (instruction.auxiliary != 0)
        {
            if (instruction.auxiliary - 1 >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return false;
            }
        }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_range_next(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        auto& state = ranges.at(instruction.operand);
        auto* snapshot = registers.resource_payload(state.snapshot_slot).resource<VmArray>();
        const bool available = snapshot && state.index < snapshot->values.size();
        if (available)
        {
            const auto& parameter = active_owner->source(current_diagnostic().target_source);
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            const auto& name = active_owner->names[site.name_id];
            range_binding.emplace();
            range_binding->name = name;
            range_binding->slot = instruction.auxiliary;
            range_binding->value_slot = active_instructions->register_capacity + active_instructions->temporary_count
                + active_instructions->switch_count + active_instructions->range_count;
            registers.store_payload(range_binding->value_slot, snapshot->values[state.index++]);
            if (!machine.bind_range(registers, range_binding->value_slot, name, active_owner->names[site.type_id], parameter))
            { result = machine.error_result(); return false; }
            if (instruction.auxiliary != 0)
            {
                const size_t slot = instruction.auxiliary - 1;
                if (slot >= active_locals->size())
                {
                    machine.set_error("bytecode local slot out of range");
                    result = machine.error_result();
                    return false;
                }
                active_locals->move(slot, registers, range_binding->value_slot);
                active_aliases[slot] = false;
            }
        }
        registers.write_payload(output, available);
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_array(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        VmArray elements(instruction.operand, machine.host.allocation_resource);
        for (size_t index = elements.values.size(); index > 0; --index)
        {
            const size_t argument = input_slot(instruction, instruction.input_count - elements.values.size() + index - 1);
            elements.values[index - 1] = std::move(registers.resource_payload(argument));
            registers.clear(argument);
        }
        registers.write_payload(output, BytecodeValue::Storage(std::move(elements)));
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_call_begin(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& call = active_owner->calls[instruction.operand];
        const auto& call_name = active_owner->names[call.name_id];
        if (!interpreter.native_functions.contains(call_name))
        {
            std::shared_ptr<const Module> function_module;
            const auto* prepared_function = machine.find_cached_function(call, call_name, call.arguments.size(), function_module);
            if (prepared_function == nullptr)
            {
                const auto known = machine.functions.find(call_name);
                if (known == machine.functions.end()) machine.set_error("function '" + call_name + "' is not defined");
                else
                {
                    std::pmr::vector<size_t> arities(&execution_scratch);
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
            if (machine.should_stop()) { result = machine.error_result(); return false; }
        }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_unwind(const Instruction& instruction)
{
    const size_t output = instruction.destination;
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
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_prepare_store(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        auto& target = active_owner->source(current_diagnostic().target_source);
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
                    binding.file->write_payload(binding.slot, std::any(VmArray(0, machine.host.allocation_resource)));
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
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_return(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const size_t value_slot = input_slot(instruction, instruction.input_count - 1);
        if (!frames.empty())
        {
            auto& caller = frames.back();
            RegisterSlots return_value(registers, caller.register_base + caller.return_register, 1);
            return_value.move(0, registers, value_slot);
            const auto& states = return_states;
            if (!states.empty() && !states.back().return_type.empty() && states.back().return_type != "void")
                machine.convert_type(return_value, 0, return_value, 0, states.back().return_type, current_location());
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            if (return_states.empty()) return_states.emplace_back();
            finish_call();
            return true;
        }
        if (return_states.empty()) return_states.emplace_back();
        registers.export_argument(value_slot, result);
        return false;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_register_snapshot(const Instruction& instruction)
{
    const size_t output = instruction.destination;
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
                read_alias(destination, instruction.variable_site, current_location());
                if (registers.empty(destination)) machine.set_error("variable '" + name + "' has not been initialized", &current_location());
            }
            else
            {
                if (active_locals->empty(instruction.operand))
                    machine.set_error("variable '" + name + "' has not been initialized", &current_location());
                registers.copy(destination, *active_locals, instruction.operand);
            }
        }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_numeric_binary_local(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& operation = active_instructions->numeric_operations[instruction.auxiliary];
        const auto& site = active_owner->register_binary_sites[
            active_instructions->numeric_local_sites[instruction.auxiliary]];
        const size_t slot = site.code.destination - 1;
        const auto& binding = active_owner->variable_sites[site.variable_site - 1];
        const auto* existing = scopes.empty() ? nullptr : scopes.back().find(active_owner->names[binding.name_id]);
        const bool double_target = binding.with_type && binding.type_id == active_owner->double_type_id;
        const auto read_number = [&](std::uint32_t operand, std::uint16_t constant_flag,
            std::uint16_t temporary_flag, std::int64_t& integer, double& floating, bool& is_double)
        {
            bool valid = false;
            if ((operation.flags & constant_flag) != 0)
            {
                const auto& value = active_owner->constants[operand].value;
                if (const auto* number = value_get_if<std::int64_t>(&value)) { integer = *number; valid = true; }
                else if (const auto* number = value_get_if<bool>(&value)) { integer = *number; valid = true; }
                else if (const auto* number = value_get_if<double>(&value)) { floating = *number; is_double = true; valid = true; }
            }
            else if ((operation.flags & temporary_flag) != 0)
                valid = registers.number(active_instructions->register_capacity + operand, integer, floating, is_double);
            else if (!active_aliases[operand]) valid = active_locals->number(operand, integer, floating, is_double);
            return valid;
        };
        std::int64_t left = 0, right = 0;
        double left_number = 0, right_number = 0;
        bool left_double = false, right_double = false;
        if (slot < active_locals->size() && !active_aliases[slot]
            && (!existing || (existing->file == active_locals && existing->slot == slot))
            && read_number(operation.left, 1, 2, left, left_number, left_double)
            && read_number(operation.right, 4, 8, right, right_number, right_double)
            && active_locals->binary_numbers(static_cast<Opcode>(operation.opcode), slot,
                left, left_number, left_double, right, right_number, right_double, machine, current_location(), !binding.with_type))
        {
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            if (binding.with_type)
            {
                if (double_target && !active_locals->cast_numeric(slot, *active_locals, slot,
                    RegisterSlots::NumericBinding::Double)) return op_register_binary(instruction);
                active_locals->bind_numeric(slot, double_target ? RegisterSlots::NumericBinding::Double : RegisterSlots::NumericBinding::Int);
                if (!existing) scopes.back().bind(active_owner->names[binding.name_id], active_locals, slot);
            }
            active_aliases[slot] = false;
            if ((site.code.flags & 1) == 0 && !instruction.discard_result)
                registers.copy(output, *active_locals, slot);
            return true;
        }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return op_register_binary(instruction);
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_size(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        if (instruction.auxiliary == 1)
        {
            const auto& name = active_owner->names[instruction.operand];
            const auto value = machine.named_value(name);
            const auto& location = active_owner->source(current_diagnostic().target_source);
            if (value.existed && value.empty())
                machine.set_error("variable '" + name + "' has not been initialized", &location);
            if (const auto size = value.size()) registers.write_payload(output, double(*size));
            else machine.set_error("function 'size' requires a string, array, or map", &location);
            if (machine.should_stop()) { result = machine.error_result(); return false; }
            return true;
        }
        const size_t argument = input_slot(instruction, instruction.input_count - 1);
        const auto& input = registers.payload(argument);
        std::optional<size_t> length;
        if (const auto* text = input.resource<std::pmr::string>()) length = text->size();
        else if (const auto* array = input.resource<VmArray>()) length = array->values.size();
        else if (const auto* map = input.resource<VmMap>()) length = map->values.size();
        registers.clear(argument);
        if (length) registers.write_payload(output, double(*length));
        else
        {
            const auto target_source = current_diagnostic().target_source;
            const auto& location = target_source.id == 0
                ? current_location() : active_owner->source(target_source);
            machine.set_error("function 'size' requires a string, array, or map", &location);
            result = machine.error_result();
            return false;
        }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_math_unary(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& call = active_owner->calls[instruction.operand];
        const auto& name = active_owner->names[call.name_id];
        const size_t argument = input_slot(instruction, instruction.input_count - 1);
        const auto& input = registers.payload(argument);
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
                default: return true;
                }
                registers.write_payload(output, computed);
            }
        }
        else
        {
            machine.conversion_error(registers, argument, "number", &call.arguments.front());
        }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

CIFA_NOINLINE bool CifaBytecode::InterpState::op_math_binary(const Instruction& instruction)
{
    const size_t output = instruction.destination;
        const auto& call = active_owner->calls[instruction.operand];
        const auto& name = active_owner->names[call.name_id];
        RegisterSlots* left_file = &registers;
        RegisterSlots* right_file = &registers;
        size_t left_slot = 0;
        size_t right_slot = 0;
        bool scratch_left = false;
        bool scratch_right = false;
        if (call.math_local_operands)
        {
            left_file = active_locals;
            right_file = active_locals;
            left_slot = call.math_local_slots[0];
            right_slot = call.math_local_slots[1];
            if (active_aliases[left_slot])
            {
                left_slot = registers.size() - 2;
                read_alias(left_slot, call.math_local_names[0], call.arguments[0]);
                left_file = &registers;
                scratch_left = true;
            }
            if (active_aliases[right_slot])
            {
                right_slot = registers.size() - 1;
                read_alias(right_slot, call.math_local_names[1], call.arguments[1]);
                right_file = &registers;
                scratch_right = true;
            }
        }
        else
        {
            right_slot = input_slot(instruction, instruction.input_count - 1);
            left_slot = input_slot(instruction, instruction.input_count - 2);
        }
        if (call.math_local_operands && left_file->empty(left_slot))
        {
            machine.set_error("variable '" + active_owner->names[call.math_local_names[0]] + "' has not been initialized",
                &call.arguments[0]);
        }
        else if (call.math_local_operands && right_file->empty(right_slot))
            machine.set_error("variable '" + active_owner->names[call.math_local_names[1]] + "' has not been initialized",
                &call.arguments[1]);
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        const auto& right = right_file->payload(right_slot);
        const auto& left = left_file->payload(left_slot);
        if (left.index() >= 1 && left.index() <= 3 && right.index() >= 1 && right.index() <= 3)
        {
            const auto number = [](const BytecodeValue::Storage& value) {
                if (const auto* integer = value_get_if<std::int64_t>(&value)) return static_cast<double>(*integer);
                if (const auto* floating = value_get_if<double>(&value)) return *floating;
                return double(value_get<bool>(value));
            };
            const double first = number(left);
            const double second = number(right);
            if (!call.math_local_operands || scratch_left) registers.clear(left_slot);
            if (!call.math_local_operands || scratch_right) registers.clear(right_slot);
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
            default: return true;
            }
            registers.write_payload(output, computed);
        }
        else
        {
            const size_t invalid = left.index() >= 1 && left.index() <= 3 ? right_slot : left_slot;
            const size_t location = invalid == left_slot ? 0 : 1;
            auto& invalid_file = location == 0 ? *left_file : *right_file;
            machine.conversion_error(invalid_file, invalid, "number", &call.arguments[location]);
        }
        if (machine.should_stop()) { result = machine.error_result(); return false; }
        return true;
}

bool CifaBytecode::execute_instructions(Machine& machine, const Module& module, const Instructions& instructions,
    Object& result, size_t start)
{
    auto* persistent = machine.host.allocation_resource.get();
    alignas(std::max_align_t) std::byte execution_buffer[8192];
    std::pmr::monotonic_buffer_resource execution_scratch(execution_buffer, sizeof(execution_buffer), persistent);
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
    std::pmr::vector<Scope> reusable_scopes(&execution_scratch);
    using LoopState = InterpState::LoopState;
    using SwitchState = InterpState::SwitchState;
    using RangeState = InterpState::RangeState;
    using MethodArguments = InterpState::MethodArguments;
    using RangeBinding = InterpState::RangeBinding;
    using Frame = InterpState::Frame;
    std::pmr::vector<std::optional<LoopState>> loop_states(instructions.loop_state_count, persistent);
    std::pmr::vector<size_t> local_scope_bases(persistent);
    if (start != 0 && !loop_states.empty()) loop_states[0] = LoopState{scopes.size(), local_scope_bases.size(), persistent};
    std::pmr::unordered_map<size_t, SwitchState> switches(persistent);
    std::pmr::unordered_map<size_t, RangeState> ranges(persistent);
    std::pmr::unordered_map<size_t, MethodArguments> method_arguments(persistent);
    size_t method_argument_top = 0;
    std::optional<RangeBinding> range_binding;
    std::pmr::deque<RegisterSlots> local_windows(persistent);
    local_windows.emplace_back(registers, 0, 0);
    std::pmr::deque<Frame> frames(persistent);
    struct RestoreCaller
    {
        std::pmr::deque<Frame>& frames;
        ScopeStack& scopes;
        ~RestoreCaller()
        {
            if (!frames.empty()) scopes = std::move(frames.front().scopes);
        }
    } restore_caller{frames, scopes};
    const Module* active_owner = &module;
    const size_t missing_global = std::numeric_limits<size_t>::max();
    std::pmr::unordered_map<const Module*, std::pmr::vector<size_t>> global_links(&execution_scratch);
    auto global_entry = global_links.try_emplace(active_owner);
    if (global_entry.second) global_entry.first->second.resize(active_owner->names.size(), missing_global);
    auto* active_globals = &global_entry.first->second;
    std::shared_ptr<const Module> active_module;
    const Instructions* active_instructions = &instructions;
    const SourceLocation* active_node = &module.source(module.root_source);
    const FunctionCode* active_function = nullptr;
    const SourceLocation* active_call = nullptr;
    const SourceLocation* current_source = nullptr;
    size_t error_pc = start;
    size_t pc = start;
    SourceRef register_assignment_source;
    auto append_diagnostic_frames = [&](std::pmr::vector<std::pair<const SourceLocation*, bool>>& destination)
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
                destination.emplace_back(&owner->source(code->diagnostics[instruction_pc].source), true);
            if (instruction_pc < code->code.size() && code->code[instruction_pc].opcode == Opcode::Size)
                destination.emplace_back(&owner->source(code->diagnostics[instruction_pc].source), true);
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
    Object::set_runtime_error_reporter([&machine, &active_owner, &active_instructions, &active_node, &current_source, &error_pc](const std::string& message, const Object* value)
        {
            const auto* location = current_source;
            if (location == nullptr && active_instructions != nullptr && error_pc < active_instructions->diagnostics.size())
            {
                const auto source = active_instructions->diagnostics[error_pc].source;
                location = source.id != 0 ? &active_owner->source(source) : active_node;
            }
            if (value != nullptr && value->getSpecialType() == "NoValue") machine.set_no_value_error(*value, location);
            else machine.set_error(message, location);
        });
    struct RestoreObjectReporter
    {
        ~RestoreObjectReporter() { Object::clear_runtime_error_reporter(); }
    } restore_object_reporter;
    auto* active_locals = &local_windows.front();
    std::pmr::vector<unsigned char> active_aliases(persistent);
    auto& return_states = machine.returns;
    const size_t return_base = return_states.size();
    struct RestoreReturns
    {
        decltype(return_states)& states;
        size_t size;
        ~RestoreReturns() { states.resize(size); }
    } restore_returns{return_states, return_base};
    InterpState interp{machine, interpreter, registers, scopes, reusable_scopes, result, frames, local_windows,
        active_locals, active_aliases, loop_states, local_scope_bases, switches, ranges, method_arguments,
        method_argument_top, range_binding, global_links, active_globals, active_owner, active_module,
        active_instructions, active_node, active_function, active_call, current_source, return_states,
        register_assignment_source, error_pc, pc, missing_global, persistent, execution_scratch};
    while (true)
    {
        if (pc >= active_instructions->code.size())
        {
            if (frames.empty()) break;
            auto& caller = frames.back();
            RegisterSlots return_value(registers, caller.register_base + caller.return_register, 1);
            return_value.clear(0);
            interp.finish_call();
            continue;
        }
        const auto& instruction = active_instructions->code[pc++];
        const size_t profile_pc = pc - 1;
        std::optional<ProfileInstructionGuard> profile_guard;
        if (machine.host.profile_state.enabled && !machine.host.profile_state.stack.empty())
        {
            const size_t previous_pc = machine.host.profile_state.last_pc.empty()
                ? std::numeric_limits<size_t>::max() : machine.host.profile_state.last_pc.back();
            profile_guard.emplace(machine.host, machine.host.profile_state.stack.back(),
                machine.host.profile_state.stack, profile_pc, previous_pc);
        }
        error_pc = pc - 1;
        const size_t output = instruction.destination;
        current_source = nullptr;
        switch (instruction.opcode)
        {
        case Opcode::Exit:
            machine.exit_requested = true;
            result = Object();
            return true;
        case Opcode::Jump:
            pc = instruction.operand;
            continue;
        case Opcode::Empty:
            registers.clear(output);
            continue;
        case Opcode::Enter:
        case Opcode::Leave:
        case Opcode::Removed:
            continue;
        case Opcode::Constant:
        {
            const auto& constant = active_owner->constants[instruction.operand];
            registers.write_payload(output, constant.value);
            if (constant.continue_marker) registers.set_type(output, {typeid(void), "", "", "__"});
            continue;
        }
        case Opcode::ConstantLocal:
        {
            const size_t slot = instruction.auxiliary - 1;
            const auto& binding = active_owner->variable_sites[instruction.variable_site - 1];
            const auto* existing = scopes.empty() ? nullptr : scopes.back().find(active_owner->names[binding.name_id]);
            if (slot >= active_locals->size() || !scopes.empty()
                && existing != nullptr && (existing->file != active_locals || existing->slot != slot))
            {
                machine.set_error("bytecode local constant target is invalid");
                result = machine.error_result();
                return true;
            }
            active_locals->write_payload(slot, value_get<std::int64_t>(active_owner->constants[instruction.operand].value));
            active_locals->bind_numeric(slot, RegisterSlots::NumericBinding::Int);
            if (!existing) scopes.back().bind(active_owner->names[binding.name_id], active_locals, slot);
            active_aliases[slot] = false;
            if (!instruction.discard_result) registers.copy(output, *active_locals, slot);
            continue;
        }
        case Opcode::Branch:
        {
            const auto condition_ref = interp.current_diagnostic().condition_source;
            const SourceLocation* condition_source = condition_ref.id != 0 ? &active_owner->source(condition_ref) : nullptr;
            if (condition_source != nullptr) current_source = condition_source;
            const bool condition = machine.condition(registers, interp.input_slot(instruction, instruction.input_count - 1), condition_source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            registers.clear(interp.input_slot(instruction, instruction.input_count - 1));
            if (!condition) pc = instruction.operand;
            continue;
        }
        case Opcode::AndBranch:
        {
            const auto condition_ref = interp.current_diagnostic().condition_source;
            const SourceLocation* condition_source = condition_ref.id != 0 ? &active_owner->source(condition_ref) : nullptr;
            if (condition_source != nullptr) current_source = condition_source;
            const bool condition = machine.condition(registers, interp.input_slot(instruction, instruction.input_count - 1), condition_source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (!condition)
            {
                registers.write_payload(output, std::int64_t(0));
                pc = instruction.operand;
            }
            continue;
        }
        case Opcode::OrBranch:
        {
            const auto condition_ref = interp.current_diagnostic().condition_source;
            const SourceLocation* condition_source = condition_ref.id != 0 ? &active_owner->source(condition_ref) : nullptr;
            if (condition_source != nullptr) current_source = condition_source;
            const bool condition = machine.condition(registers, interp.input_slot(instruction, instruction.input_count - 1), condition_source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (condition)
            {
                registers.write_payload(output, std::int64_t(1));
                pc = instruction.operand;
            }
            continue;
        }
        case Opcode::ScopeEnter:
        {
            if (reusable_scopes.empty()) scopes.emplace_back(machine.host.allocation_resource);
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
        case Opcode::ScopeLeave:
            if (instruction.auxiliary != 0)
            {
                for (size_t slot = instruction.auxiliary - 1; slot < active_locals->size(); ++slot)
                {
                    active_locals->clear(slot);
                    active_aliases[slot] = false;
                }
            }
            interp.release_scope();
            local_scope_bases.pop_back();
            continue;
        case Opcode::LoopMark:
        {
            auto& state = loop_states[instruction.auxiliary];
            state = LoopState{scopes.size(), local_scope_bases.size(), persistent};
            for (const auto& entry : ranges) state->ranges.push_back(entry.first);
            for (const auto& entry : switches) state->switches.push_back(entry.first);
            continue;
        }
        case Opcode::Member:
        {
            const auto member = machine.resolve_member(active_owner->names[instruction.operand],
                active_owner->names[instruction.auxiliary]);
            machine.read_indexed(registers, output, member, true);
            if (machine.should_stop()) { result = Object(); return true; }
            continue;
        }
        case Opcode::MethodPush:
            if (!interp.op_method_push(instruction)) return true;
            continue;
        case Opcode::ArrayPushGlobal:
        {
            const auto& site = active_owner->calls[instruction.operand];
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            const size_t global_slot = interp.linked_global(site.base_name_id);
            auto* container = global_slot != missing_global && machine.global_exists_at(global_slot)
                ? &machine.global_values.resource_payload(global_slot) : nullptr;
            auto* array = container ? container->resource<VmArray>() : nullptr;
            if (!array)
            {
                registers.clear(argument);
                machine.set_error("push_back() requires an array or map", &active_owner->source(site.method_source));
                registers.clear(output);
            }
            else
            {
                const size_t absolute = machine.global_values.base() + global_slot;
                const auto& element_type = machine.global_values.type_pool[
                    machine.global_values.slot_types[absolute]].element;
                if (element_type.empty())
                {
                    array->values.push_back(std::move(registers.resource_payload(argument)));
                    registers.clear(argument);
                }
                else
                {
                    Object value;
                    registers.export_argument(argument, value);
                    value = machine.convert_type(value, element_type, active_owner->source(site.method_source));
                    if (!machine.should_stop()) array->values.emplace_back(std::move(value.value));
                }
                if (!machine.should_stop()) registers.write_payload(output, double(array->values.size()));
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        case Opcode::ArrayPushGlobalLocal:
            if (!interp.op_array_push_global_local(instruction)) return true;
            continue;
        case Opcode::MethodBegin:
            if (!interp.op_method_begin(instruction)) return true;
            continue;
        case Opcode::MethodValue:
            if (!interp.op_method_value(instruction)) return true;
            continue;
        case Opcode::BindArgument:
            continue;
        case Opcode::MethodNoArgs:
            if (!interp.op_method_no_args(instruction)) return true;
            continue;
        case Opcode::RangeBegin:
        {
            RangeState state;
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            state.snapshot_slot = active_instructions->register_capacity + active_instructions->temporary_count
                + active_instructions->switch_count + ranges.size();
            if (!machine.range(registers, argument, active_owner->source(interp.current_diagnostic().target_source),
                registers, state.snapshot_slot))
            { result = machine.error_result(); return true; }
            registers.clear(interp.input_slot(instruction, instruction.input_count - 1));
            ranges[instruction.operand] = std::move(state);
            continue;
        }
        case Opcode::RangeNext:
            if (!interp.op_range_next(instruction)) return true;
            continue;
        case Opcode::RangeEnd:
        {
            registers.clear(ranges.at(instruction.operand).snapshot_slot);
            ranges.erase(instruction.operand);
            if (range_binding) registers.clear(range_binding->value_slot);
            range_binding.reset();
            continue;
        }
        case Opcode::Index:
            if (!interp.op_index(instruction)) return true;
            continue;
        case Opcode::IndexLocal:
            if (!interp.op_index_local(instruction)) return true;
            continue;
        case Opcode::Array:
            if (!interp.op_array(instruction)) return true;
            continue;
        case Opcode::CallBegin:
            if (!interp.op_call_begin(instruction)) return true;
            continue;
        case Opcode::Call:
            if (!interp.op_call(instruction)) return true;
            continue;
        case Opcode::SwitchMark:
        {
            auto& state = switches[instruction.operand];
            state.condition_slot = active_instructions->register_capacity + active_instructions->temporary_count + switches.size() - 1;
            registers.move(state.condition_slot, registers, interp.input_slot(instruction, instruction.input_count - 1));
            state.active = false;
            continue;
        }
        case Opcode::SwitchCase:
        {
            registers.write_payload(output, switches.at(instruction.operand).active);
            continue;
        }
        case Opcode::SwitchDefault:
        {
            auto& state = switches.at(instruction.operand);
            if (instruction.auxiliary != 0)
            {
                const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
                if (!registers.binary_payloads(Opcode::Equal, output, registers.payload(state.condition_slot),
                    registers.payload(argument), machine, interp.current_location()))
                {
                    const size_t scratch = registers.size() - 1;
                    registers.copy(scratch, registers, state.condition_slot);
                    registers.binary_fallback(Opcode::Equal, output, scratch, argument, machine, interp.current_location(), false);
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
        case Opcode::SwitchEnd:
        {
            registers.clear(switches.at(instruction.operand).condition_slot);
            switches.erase(instruction.operand);
            continue;
        }
        case Opcode::Unwind:
            if (!interp.op_unwind(instruction)) return true;
            continue;
        case Opcode::PrepareStore:
            if (!interp.op_prepare_store(instruction)) return true;
            continue;
        case Opcode::DeclareLocal:
        {
            const auto& source = interp.current_location();
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
            interp.bind_local_storage(active_owner->names[site.name_id], instruction.operand, false);
            if (active_aliases[instruction.operand]) active_locals->clear(instruction.operand);
            continue;
        }
        case Opcode::NumericForNext:
        {
            // Keep the integer payload in place: no numeric conversion, result
            // copy, or diagnostic lookup is needed on this path.
            const auto slot = instruction.operand;
            if (instruction.plain_increment && slot < active_locals->size() && !active_aliases[slot])
                if (auto* integer = active_locals->resource_payload(slot).get_if<std::int64_t>()) {
                    const auto before = static_cast<std::uint64_t>(*integer);
                    const bool add = instruction.write == WriteOperation::Add || instruction.write == WriteOperation::PostAdd;
                    *integer = std::bit_cast<std::int64_t>(add ? before + 1 : before - 1);
                    if (instruction.member_site != 0) {
                        const auto& loop = active_instructions->integer_loops[instruction.member_site - 1];
                        pc = *integer < loop.limit ? loop.body : loop.exit;
                    }
                    else pc = instruction.auxiliary;
                    continue;
                }
            [[fallthrough]];
        }
        case Opcode::IncrementLocal:
        {
            const auto slot = instruction.operand;
            if (instruction.plain_increment && instruction.discard_result
                && slot < active_locals->size() && !active_aliases[slot])
                if (auto* integer = active_locals->resource_payload(slot).get_if<std::int64_t>()) {
                    const auto before = static_cast<std::uint64_t>(*integer);
                    const bool add = instruction.write == WriteOperation::Add || instruction.write == WriteOperation::PostAdd;
                    *integer = std::bit_cast<std::int64_t>(add ? before + 1 : before - 1);
                    continue;
                }
            const auto& source = interp.current_location();
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
            const auto target = interp.alias_target(binding.name_id);
            const bool preserve_old = !instruction.discard_result
                && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract);
            if (preserve_old) registers.copy(output, *target.file, target.slot);
            const size_t computed = registers.size() - 2;
            const size_t unit = registers.size() - 1;
            registers.copy(computed, *target.file, target.slot);
            registers.write_payload(unit, std::int64_t(1));
            const auto operation = write_opcode(instruction.write);
            if (!operation) machine.set_error("invalid bytecode write operation");
            else registers.binary_fallback(*operation, computed, computed, unit, machine, source, false);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            registers.clear(unit);
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            if (site.with_type) machine.bind_type(*target.file, target.slot, active_owner->names[site.type_id], source);
            if (!machine.should_stop()) machine.assign(*target.file, target.slot, registers, computed, unit, source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (!instruction.discard_result && !preserve_old)
                registers.copy(output, *target.file, target.slot);
            continue;
        }
        case Opcode::StoreLocal:
        {
            const auto& source = interp.current_location();
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
                    const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
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
                    const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
                    if (interp.initialize_numeric(instruction.operand, registers, argument, binding))
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
            if (!binding.with_type && !active_aliases[instruction.operand]
                && instruction.write != WriteOperation::Assign)
            {
                const auto operation = write_opcode(instruction.write);
                const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
                const size_t scratch = registers.size() - 2;
                if (operation)
                {
                    if (!registers.binary_payloads(*operation, scratch,
                        active_locals->payload(instruction.operand), registers.payload(argument), machine, source))
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
                && active_locals->assign_numeric(instruction.operand, registers, interp.input_slot(instruction, instruction.input_count - 1)))
            {
                if (!instruction.discard_result) registers.copy(output, *active_locals, instruction.operand);
                continue;
            }
            if (instruction.write == WriteOperation::Assign && binding.with_type && !scopes.empty())
            {
                const auto& name = active_owner->names[binding.name_id];
                const auto* position = scopes.back().find(name);
                if (!position || (position->file == active_locals && position->slot == instruction.operand))
                {
                    const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
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
                const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
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
                const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
                machine.assign(*active_locals, instruction.operand, registers, argument, registers.size() - 2, source,
                    instruction.discard_result);
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
                    const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
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
            const auto target = interp.alias_target(binding.name_id);
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            size_t value_slot = argument;
            const size_t computed = registers.size() - 2;
            if (instruction.write != WriteOperation::Assign)
            {
                registers.copy(computed, *target.file, target.slot);
                const auto operation = write_opcode(instruction.write);
                if (!operation) machine.set_error("invalid bytecode write operation");
                else registers.binary_fallback(*operation, computed, computed, argument, machine, source, false);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                value_slot = computed;
                registers.clear(argument);
            }
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            if (site.with_type) machine.bind_type(*target.file, target.slot, active_owner->names[site.type_id], source);
            if (!machine.should_stop()) machine.assign(*target.file, target.slot, registers,
                value_slot, registers.size() - 1, source);
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
                    interp.bind_local_storage(name, instruction.operand, site.with_type);
                }
            }
            if (!instruction.discard_result) registers.copy(output, *target.file, target.slot);
            continue;
        }
        case Opcode::Store:
        case Opcode::Increment:
            if (!interp.op_store_increment(instruction)) return true;
            continue;
        case Opcode::Return:
            if (!interp.op_return(instruction)) return true;
            continue;
        case Opcode::RegisterSnapshot:
            if (!interp.op_register_snapshot(instruction)) return true;
            continue;
        case Opcode::NumericCompareBranch:
        {
            const auto& operation = active_instructions->numeric_operations[instruction.auxiliary];
            const auto read_number = [&](std::uint32_t operand, std::uint16_t flags,
                std::uint16_t constant_flag, std::uint16_t temporary_flag,
                std::int64_t& integer, double& floating, bool& is_double)
            {
                if ((flags & constant_flag) != 0)
                {
                    const auto& value = active_owner->constants[operand].value;
                    if (const auto* number = value_get_if<std::int64_t>(&value)) integer = *number;
                    else if (const auto* number = value_get_if<bool>(&value)) integer = *number;
                    else if (const auto* number = value_get_if<double>(&value)) { floating = *number; is_double = true; }
                    else return false;
                    return true;
                }
                if ((flags & temporary_flag) != 0)
                    return registers.number(active_instructions->register_capacity + operand, integer, floating, is_double);
                return !active_aliases[operand] && active_locals->number(operand, integer, floating, is_double);
            };
            std::int64_t left_integer = 0, right_integer = 0;
            double left_number = 0, right_number = 0;
            bool left_double = false, right_double = false;
            if (read_number(operation.left, operation.flags, 1, 2, left_integer, left_number, left_double)
                && read_number(operation.right, operation.flags, 4, 8, right_integer, right_number, right_double))
            {
                const double left = left_double ? left_number : static_cast<double>(left_integer);
                const double right = right_double ? right_number : static_cast<double>(right_integer);
                bool condition = false;
                switch (static_cast<Opcode>(operation.opcode))
                {
                case Opcode::Less: condition = left_double || right_double ? left < right : left_integer < right_integer; break;
                case Opcode::Greater: condition = left_double || right_double ? left > right : left_integer > right_integer; break;
                case Opcode::LessEqual: condition = left_double || right_double ? left <= right : left_integer <= right_integer; break;
                case Opcode::GreaterEqual: condition = left_double || right_double ? left >= right : left_integer >= right_integer; break;
                case Opcode::Equal: condition = left_double || right_double ? left == right : left_integer == right_integer; break;
                case Opcode::NotEqual: condition = left_double || right_double ? left != right : left_integer != right_integer; break;
                default: break;
                }
                if (condition) ++pc;
                else pc = instruction.member_site;
                continue;
            }
            [[fallthrough]];
        }
        case Opcode::NumericBinary:
        {
            const auto& operation = active_instructions->numeric_operations[instruction.auxiliary];
            const auto read_number = [&](std::uint32_t operand, std::uint16_t flags,
                std::uint16_t constant_flag, std::uint16_t temporary_flag,
                std::int64_t& integer, double& floating, bool& is_double)
            {
                if ((flags & constant_flag) != 0)
                {
                    const auto& value = active_owner->constants[operand].value;
                    if (const auto* number = value_get_if<std::int64_t>(&value)) integer = *number;
                    else if (const auto* number = value_get_if<bool>(&value)) integer = *number;
                    else if (const auto* number = value_get_if<double>(&value)) { floating = *number; is_double = true; }
                    else return false;
                    return true;
                }
                if ((flags & temporary_flag) != 0)
                    return registers.number(active_instructions->register_capacity + operand, integer, floating, is_double);
                return !active_aliases[operand] && active_locals->number(operand, integer, floating, is_double);
            };
            std::int64_t left_integer = 0, right_integer = 0;
            double left_number = 0, right_number = 0;
            bool left_double = false, right_double = false;
            if (read_number(operation.left, operation.flags, 1, 2, left_integer, left_number, left_double)
                && read_number(operation.right, operation.flags, 4, 8, right_integer, right_number, right_double))
            {
                const size_t binary_destination = operation.destination != 0
                    ? active_instructions->register_capacity + operation.destination - 1 : output;
                if (registers.binary_numbers(static_cast<Opcode>(operation.opcode), binary_destination,
                    left_integer, left_number, left_double, right_integer, right_number, right_double,
                    machine, interp.current_location()))
                {
                    if (machine.should_stop()) { result = machine.error_result(); return true; }
                    continue;
                }
            }
            if (!interp.op_register_binary(instruction)) return true; continue;
        }
        case Opcode::NumericBinaryLocal:
            if (!interp.op_numeric_binary_local(instruction)) return true;
            continue;
        case Opcode::RegisterBinary:
            if (!interp.op_register_binary(instruction)) return true;
            continue;
        case Opcode::LoadLocal:
        {
            if (instruction.operand >= active_locals->size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& location = interp.current_location();
            const auto& name = active_owner->names[instruction.auxiliary];
            if (active_aliases[instruction.operand])
            {
                interp.read_alias(output, instruction.auxiliary, location);
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
        case Opcode::Load:
        case Opcode::Peek:
        {
            const auto& source = interp.current_location();
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
                size_t linked = interp.linked_global(instruction.operand);
                const bool existed = linked != missing_global && machine.global_exists_at(linked);
                if (!existed)
                {
                    const auto value = machine.assign_named(name, "", false, false, source);
                    linked = value.slot;
                    (*active_globals)[instruction.operand] = linked;
                }
                if (instruction.opcode != Opcode::Peek && existed && machine.global_values.empty(linked))
                    machine.set_error("variable '" + name + "' has not been initialized", &source);
                registers.copy(output, machine.global_values, linked);
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
        case Opcode::Size:
            if (!interp.op_size(instruction)) return true;
            continue;
        case Opcode::MathUnary:
            if (!interp.op_math_unary(instruction)) return true;
            continue;
        case Opcode::MathBinary:
            if (!interp.op_math_binary(instruction)) return true;
            continue;
        case Opcode::Positive:
        {
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            const auto& input = registers.payload(argument);
            if (input.index() >= 1 && input.index() <= 3)
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
            registers.move(output, registers, argument);
            continue;
        }
        case Opcode::Negative:
        {
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            const auto& input = registers.payload(argument);
            if (input.index() >= 1 && input.index() <= 3)
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
            const size_t zero = registers.size() - 1;
            registers.write_payload(zero, std::int64_t(0));
            registers.binary_fallback(Opcode::Subtract, output, zero, argument, machine, interp.current_location(), false);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        case Opcode::LogicalNot:
        {
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            const auto& input = registers.payload(argument);
            if (input.index() >= 1 && input.index() <= 3)
            {
                const bool zero = value_holds<double>(input) ? value_get<double>(input) == 0
                    : value_holds<std::int64_t>(input) ? value_get<std::int64_t>(input) == 0
                    : !value_get<bool>(input);
                registers.clear(argument);
                registers.write_payload(output, zero);
                continue;
            }
            const bool inverted = !machine.condition(registers, argument, nullptr);
            registers.clear(argument);
            if (!machine.should_stop()) registers.write_payload(output, inverted);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        case Opcode::BitNot:
        {
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            const auto& input = registers.payload(argument);
            if (value_holds<std::int64_t>(input) || value_holds<bool>(input))
            {
                const auto number = value_holds<std::int64_t>(input)
                    ? value_get<std::int64_t>(input) : std::int64_t(value_get<bool>(input));
                registers.clear(argument);
                registers.write_payload(output, ~number);
                continue;
            }
            std::int64_t integer = 0;
            if (!registers.integer(argument, integer)) machine.conversion_error(registers, argument, "int", nullptr);
            registers.clear(argument);
            if (!machine.should_stop()) registers.write_payload(output, ~integer);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        case Opcode::Cast:
        {
            const size_t argument = interp.input_slot(instruction, instruction.input_count - 1);
            machine.convert_type(registers, output, registers, argument, active_owner->names[instruction.operand],
                interp.current_location());
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (output != argument) registers.clear(argument);
            continue;
        }
        default:
        {
            if (registers.binary(instruction.opcode, output, interp.input_slot(instruction, instruction.input_count - 2), interp.input_slot(instruction, instruction.input_count - 1),
                machine, interp.current_location()))
            {
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                continue;
            }
            registers.binary_fallback(instruction.opcode, output, interp.input_slot(instruction, instruction.input_count - 2),
                interp.input_slot(instruction, instruction.input_count - 1), machine, interp.current_location(), active_owner->host_function_version != 0);
            break;
        }
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

size_t CifaBytecode::Machine::ensure_global_slot(const std::string& name)
{
    if (const auto found = global_slots.find(name); found != global_slots.end()) return found->second;
    const size_t slot = global_values.append();
    global_slots.emplace(name, slot);
    global_exists.push_back(false);
    global_values.set_name(slot, name);
    return slot;
}

size_t CifaBytecode::Machine::find_global_slot(const std::string& name) const
{
    const auto found = global_slots.find(name);
    return found == global_slots.end() ? std::numeric_limits<size_t>::max() : found->second;
}

bool CifaBytecode::Machine::global_exists_at(size_t slot) const
{
    return slot < global_exists.size() && global_exists[slot];
}

void CifaBytecode::Machine::import_host_globals()
{
    std::fill(global_exists.begin(), global_exists.end(), false);
    for (const auto& [name, value] : host.global_variables)
    {
        const size_t slot = ensure_global_slot(name);
        global_values.import_object(slot, value);
        global_exists[slot] = true;
    }
    for (size_t slot = 0; slot < global_exists.size(); ++slot)
        if (!global_exists[slot]) global_values.clear(slot);
}

void CifaBytecode::Machine::export_host_globals()
{
    for (const auto& [name, slot] : global_slots)
    {
        if (!global_exists_at(slot))
        {
            host.global_variables.erase(name);
            continue;
        }
        global_values.export_object(slot, host.global_variables[name]);
    }
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
    std::pmr::vector<const SourceLocation*> frames(host.allocation_resource.get());
    std::pmr::vector<bool> function_frames(host.allocation_resource.get());
    decltype(call_stack) diagnostic_frames(call_stack, host.allocation_resource.get());
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

CifaBytecode::Machine::IndexedValueRef CifaBytecode::Machine::resolve_member(
    const std::string& base_name, const std::string& field_name)
{
    auto base = named_value(base_name);
    if (base.file)
    {
        auto& payload = base.file->resource_payload(base.slot);
        if (auto* fields = payload.resource<VmMap>())
        {
            auto& element = fields->values[field_name];
            const size_t index = base.file->base() + base.slot;
            const auto& structure_name = base.file->type_pool[base.file->slot_types[index]].declared;
            std::string element_type;
            if (const auto found = structures.find(structure_name); found != structures.end())
                for (const auto& field : found->second)
                    if (field.name == field_name) { element_type = field.type_name; break; }
            return {&element, nullptr, base_name + "." + field_name, element_type};
        }
    }
    const auto fallback = assign_named(base_name + "::" + field_name, "", false, false, {});
    return {nullptr, nullptr, base_name + "::" + field_name, fallback.element_type, fallback.file, fallback.slot};
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
    const auto& payload = source.payload(slot);
    const auto* resource = value_get_if<std::any>(&payload);
    const auto identity = payload.resource<std::pmr::string>() ? std::type_index(typeid(std::string)) : payload.resource<VmMap>() ? std::type_index(typeid(ObjectMap)) : resource ? std::type_index(resource->type()) : value_holds<double>(payload)
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
        const auto& payload = values.payload(slot);
        const auto* resource = value_get_if<std::any>(&payload);
        descriptor.bound = payload.resource<std::pmr::string>() ? std::type_index(typeid(std::string)) : payload.resource<VmMap>() ? std::type_index(typeid(ObjectMap)) : resource ? std::type_index(resource->type()) : value_holds<double>(payload)
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

CifaBytecode::Machine::NamedValueRef CifaBytecode::Machine::assign_named(
    const std::string& name, const std::string& type_name, bool with_type,
    bool declare_current, const SourceLocation& location)
{
    const size_t slot = ensure_global_slot(name);
    const bool existed = global_exists_at(slot);
    global_exists[slot] = true;
    global_values.set_name(slot, name);
    if (with_type) bind_type(global_values, slot, type_name, location);
    const size_t index = global_values.base() + slot;
    return {&global_values, slot, global_values.type_pool[global_values.slot_types[index]].element, existed};
}

bool CifaBytecode::Machine::assign(RegisterSlots& destination, size_t target, RegisterSlots& source, size_t slot,
    size_t scratch, const SourceLocation& location, bool take_value)
{
    const size_t index = destination.base() + target;
    auto type = destination.type_pool[destination.slot_types[index]];
    if (destination.bindings[index] != RegisterSlots::NumericBinding::None)
    {
        const bool integer = destination.bindings[index] == RegisterSlots::NumericBinding::Int;
        type.bound = integer ? typeid(std::int64_t) : typeid(double);
        type.declared = integer ? "int" : "double";
    }
    // Name IDs belong to the destination storage and survive payload replacement.
    // Keep the ID instead of copying and re-interning the same diagnostic name.
    const size_t name_id = destination.slot_names[index];
    const auto& payload = source.payload(slot);
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
    const auto& converted = source.payload(value_slot);
    const auto* converted_resource = value_get_if<std::any>(&converted);
    const auto identity = converted.resource<std::pmr::string>() ? std::type_index(typeid(std::string)) : converted.resource<VmMap>() ? std::type_index(typeid(ObjectMap)) : converted_resource ? std::type_index(converted_resource->type()) : value_holds<double>(converted)
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
    if (take_value) destination.move(target, source, value_slot);
    else destination.copy(target, source, value_slot);
    const size_t destination_index = destination.base() + target;
    auto assigned = destination.type_pool[destination.slot_types[destination_index]];
    assigned.bound = type.bound;
    assigned.declared = type.declared;
    destination.set_type(target, assigned);
    destination.slot_names[destination_index] = name_id;
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
        const size_t global_slot = find_global_slot(name);
        bool existing_map = false;
        if (existing_slot)
        {
            existing_map = existing_slot->file->resource_payload(existing_slot->slot).resource<VmMap>() != nullptr;
        }
        else if (global_slot != std::numeric_limits<size_t>::max() && global_exists_at(global_slot))
        {
            existing_map = global_values.resource_payload(global_slot).resource<VmMap>() != nullptr;
        }
        if (!existing_map)
        {
            VmMap::Values::Container fields(host.allocation_resource.get());
            for (const auto& field : structures.at(type_name))
            {
                fields.emplace(std::pmr::string(field.name, host.allocation_resource.get()), CompactValue{});
            }
            if (!scopes.empty())
            {
                auto& binding = scopes.back().create(name);
                binding.file->write_payload(binding.slot, BytecodeValue::Storage(VmMap(std::move(fields), host.allocation_resource)));
                binding.file->set_type(binding.slot, {typeid(ObjectMap), type_name, "", ""});
                binding.file->set_name(binding.slot, name);
                destination.copy(slot, *binding.file, binding.slot);
                return;
            }
            const auto value = assign_named(name, type_name, true, true, location);
            value.file->write_payload(value.slot, BytecodeValue::Storage(VmMap(std::move(fields), host.allocation_resource)));
            value.file->set_type(value.slot, {typeid(ObjectMap), type_name, "", ""});
            value.file->set_name(value.slot, name);
            destination.copy(slot, *value.file, value.slot);
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
    const size_t global_slot = find_global_slot(name);
    const bool global_exists = global_slot != std::numeric_limits<size_t>::max() && global_exists_at(global_slot);
    if (!global_exists && !scopes.empty())
    {
        auto& binding = scopes.back().create(name);
        if (with_type && !bind_type(*binding.file, binding.slot, type_name, location)) return;
        destination.copy(slot, *binding.file, binding.slot);
        return;
    }
    const auto value = assign_named(name, type_name, with_type, false, location);
    if (!only_check && value.existed && !with_type && value.file->empty(value.slot))
        set_error("variable '" + name + "' has not been initialized", &location);
    destination.copy(slot, *value.file, value.slot);
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
    std::pmr::vector<std::pair<const SourceLocation*, bool>> frames(call_stack, host.allocation_resource.get());
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
    const std::string name(source.name_pool[source.slot_names[index]]);
    const auto& value = source.payload(slot);
    const auto* resource = value_get_if<std::any>(&value);
    if (const auto* no_value = resource ? std::any_cast<Object::NoValue>(resource) : nullptr)
    {
        set_no_value_error(no_value, location);
        return;
    }
    const std::string type = source.empty(slot) ? "<empty>" : value.resource<std::pmr::string>() ? typeid(std::string).name() : resource ? resource->type().name()
        : value_holds<double>(value) ? typeid(double).name()
        : value_holds<bool>(value) ? typeid(bool).name() : typeid(std::int64_t).name();
    set_error("type conversion failed: variable '" + (name.empty() ? std::string("<temporary>") : name)
        + "' from " + type + " to " + target, location);
}

std::string_view CifaBytecode::Machine::string_value(RegisterSlots& source, size_t slot)
{
    const auto& value = source.payload(slot);
    const auto* resource = value_get_if<std::any>(&value);
    if (const auto* text = value.resource<std::pmr::string>()) return *text;
    conversion_error(source, slot, "string", nullptr);
    return {};
}

bool CifaBytecode::Machine::range(RegisterSlots& source, size_t slot, const SourceLocation& location,
    RegisterSlots& destination, size_t target)
{
    const auto& payload = source.resource_payload(slot);
    if (const auto* no_value = payload.resource<Object::NoValue>())
    {
        set_no_value_error(no_value, &location);
        return false;
    }
    const auto* elements = payload.resource<VmArray>();
    if (!elements) { set_error("range for requires an array", &location); return false; }
    destination.write_payload(target, BytecodeValue::Storage(*elements));
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
    if (element.file)
    {
        if (element.file->empty(element.slot) && !map_access)
            set_error("array element '" + element.name + "' has not been initialized");
        destination.copy(slot, *element.file, element.slot);
        return;
    }
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
    if (target.file)
    {
        RegisterSlots boundary(2, host.allocation_resource);
        boundary.import_object(0, std::move(value));
        if (with_type && !bind_type(*target.file, target.slot, type_name, location)) return false;
        return assign(*target.file, target.slot, boundary, 0, 1, location);
    }
    if (target.object) return assign(*target.object, std::move(value), with_type, type_name, location);
    if (!target.compact) return false;
    const auto& target_type = !target.element_type.empty() ? target.element_type : type_name;
    if (!target_type.empty())
    {
        Object converted;
        if (!assign(converted, std::move(value), true, target_type, location)) return false;
        *target.compact = CompactValue(std::move(converted.value), host.allocation_resource);
    }
    else *target.compact = CompactValue(std::move(value.value), host.allocation_resource);
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
        return indices.resource_payload(index_slots[dimension]).resource<std::pmr::string>() != nullptr;
    };
    CompactValue* compact = nullptr;
    std::string compact_element_type;
    size_t first_dimension = 0;
    auto* binding = !is_decl_array && dimensions != 0 ? find_slot(name) : nullptr;
    if (binding && (!declare_current || (!scopes.empty() && scopes.back().find(name) == binding)))
    {
        auto& payload = binding->file->resource_payload(binding->slot);
        if (auto* fields = payload.resource<VmMap>())
            compact = &fields->values[string_value(indices, index_slots[0])];
        else if (auto* elements = payload.resource<VmArray>())
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
        if (compact) first_dimension = 1;
    }
    if (!compact)
    {
    if (binding && dimensions != 0)
    {
        auto& file = *binding->file;
        const size_t slot = binding->slot;
        if (string_key(0))
        {
            file.write_payload(slot, BytecodeValue::Storage(VmMap(host.allocation_resource)));
            compact = &file.resource_payload(slot).resource<VmMap>()->values[string_value(indices, index_slots[0])];
        }
        else
        {
            const auto offset = integer_key(0);
            if (offset < 0) { set_error("array index is out of range"); return {}; }
            file.write_payload(slot, BytecodeValue::Storage(VmArray(static_cast<size_t>(offset) + 1, host.allocation_resource)));
            auto& values = file.resource_payload(slot).resource<VmArray>()->values;
            const size_t absolute = file.base() + slot;
            compact_element_type = file.type_pool[file.slot_types[absolute]].element;
            compact = &values[static_cast<size_t>(offset)];
        }
        first_dimension = 1;
    }
    if (!compact)
    {
    const auto base = assign_named(name, "", false, false, {});
    auto& resource = base.file->resource_payload(base.slot);
    if (is_decl_array)
    {
        const auto requested = dimensions == 0
            || value_holds<std::monostate>(indices.payload(index_slots[0]))
            ? 0 : integer_key(0);
        const size_t size = requested < 0 ? 0 : static_cast<size_t>(requested);
        auto* elements = resource.resource<VmArray>();
        if (!elements)
        {
            base.file->write_payload(base.slot, BytecodeValue::Storage(VmArray(size, host.allocation_resource)));
            elements = base.file->resource_payload(base.slot).resource<VmArray>();
        }
        else if (!only_check) elements->values.resize(size);
        const size_t absolute = base.file->base() + base.slot;
        auto descriptor = base.file->type_pool[base.file->slot_types[absolute]];
        descriptor.element = type_name;
        base.file->set_type(base.slot, descriptor);
        base.file->set_name(base.slot, name);
        return {nullptr, nullptr, name, type_name, base.file, base.slot};
    }
    if (!resource.resource<VmArray>() && !resource.resource<VmMap>())
    {
        base.file->write_payload(base.slot, string_key(0)
            ? BytecodeValue::Storage(VmMap(host.allocation_resource))
            : BytecodeValue::Storage(VmArray(0, host.allocation_resource)));
    }
    auto& updated = base.file->resource_payload(base.slot);
    if (auto* fields = updated.resource<VmMap>())
        compact = &fields->values[string_value(indices, index_slots[0])];
    else if (auto* elements = updated.resource<VmArray>())
    {
        const auto offset = integer_key(0);
        if (offset < 0) { set_error("array index is out of range"); return {}; }
        const size_t index = static_cast<size_t>(offset);
        if (index >= elements->values.size()) elements->values.resize(index + 1);
        const size_t absolute = base.file->base() + base.slot;
        compact_element_type = base.file->type_pool[base.file->slot_types[absolute]].element;
        compact = &elements->values[index];
    }
    first_dimension = 1;
    }
    }
    for (size_t index = first_dimension; compact && index < dimensions; ++index)
    {
        auto* values = compact->resource<VmArray>();
        if (!values)
        {
            *compact = CompactValue(std::any(ObjectVector{}), host.allocation_resource);
            values = compact->resource<VmArray>();
        }
        const auto offset = integer_key(index);
        if (offset < 0) { set_error("array index is out of range"); return {compact, nullptr, name, compact_element_type}; }
        if (static_cast<size_t>(offset) >= values->values.size()) values->values.resize(static_cast<size_t>(offset) + 1);
        compact = &values->values[static_cast<size_t>(offset)];
    }
    return {compact, nullptr, name, compact_element_type.empty() ? type_name : compact_element_type};
}

Object CifaBytecode::Machine::make_no_value(const std::string& function_name, const SourceLocation& call_site) const
{
    return Object::make_no_value(function_name, format_frame(call_site));
}

Object CifaBytecode::Machine::call_host(const std::string& name, ObjectVector& arguments,
    const std::pmr::vector<SourceLocation>& locations)
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
    export_host_globals();
    Object result = function->second(arguments);
    import_host_globals();
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

bool CifaBytecode::Machine::call_native_registers(const std::string& name, RegisterSlots& destination, size_t result,
    RegisterSlots& values, const size_t* arguments, size_t count, const std::pmr::vector<SourceLocation>& locations)
{
    const auto found = host.native_functions.find(name);
    if (found == host.native_functions.end()) return false;
    if (!found->second.builtin)
    {
        NativeCallContext context(*this, destination, result, values, arguments, count, locations);
        found->second.function(context);
        if (!context.result_written && !should_stop()) context.set_empty_result();
        return true;
    }
    if (name == "run_string" || name == "run_file")
    {
        if (count != 1)
        {
            set_error("function '" + name + "' expects 1 argument, got " + std::to_string(count));
            return true;
        }
        const std::string source(string_value(values, arguments[0])); // 嵌套执行可能替换寄存器，必须持有副本。
        if (should_stop()) return true;
        auto nested_result = name == "run_string" ? host.run_script(source) : host.run_file(source);
        if (host.has_runtime_error())
        {
            error = host.get_runtime_error();
            return true;
        }
        destination.import_object(result, std::move(nested_result));
        return true;
    }
    if (call_builtin_registers(name, destination, result, values, arguments, count)) return true;
    if (count != 0) conversion_error(values, arguments[0], "valid argument for function '" + name + "'", locations.empty() ? nullptr : &locations.front());
    else set_error("invalid arguments for function '" + name + "'", locations.empty() ? nullptr : &locations.front());
    return true;
}

bool CifaBytecode::Machine::call_builtin_registers(const std::string& name, RegisterSlots& destination, size_t result,
    RegisterSlots& values, const size_t* arguments, size_t count)
{
    const auto text_argument = [&](size_t index, std::string_view& text) -> bool
    {
        if (index >= count) return false;
        const auto& value = values.payload(arguments[index]);
        const auto* resource = value_get_if<std::any>(&value);
        const auto* string = value.resource<std::pmr::string>();
        if (!string) return false;
        text = *string;
        return true;
    };
    const auto format_argument = [&](size_t index, std::string_view specification, std::pmr::string& text) -> bool
    {
        if (index >= count) return false;
        const auto& value = values.payload(arguments[index]);
        try
        {
            const auto append_formatted = [&](const auto& argument) {
                text.clear();
                if (specification.empty()) std::format_to(std::back_inserter(text), "{}", argument);
                else {
                    std::pmr::string pattern("{:", host.allocation_resource.get());
                    pattern += specification;
                    pattern += '}';
                    std::vformat_to(std::back_inserter(text), pattern, std::make_format_args(argument));
                }
            };
            if (const auto* integer = value_get_if<std::int64_t>(&value))
            {
                append_formatted(*integer);
                return true;
            }
            if (const auto* floating = value_get_if<double>(&value))
            {
                append_formatted(*floating);
                return true;
            }
            if (const auto* boolean = value_get_if<bool>(&value))
            {
                append_formatted(*boolean);
                return true;
            }
            if (const auto* string = value.resource<std::pmr::string>()) {
                if (specification.empty()) text = *string;
                else append_formatted(*string);
                return true;
            }
        }
        catch (const std::format_error&)
        {
            return false;
        }
        return false;
    };
    if (name == "exit")
    {
        exit_requested = true;
        destination.clear(result);
        return true;
    }
    if (name == "print" || name == "println")
    {
        for (size_t index = 0; index < count; ++index)
        {
            const auto& value = values.payload(arguments[index]);
            if (const auto* integer = value_get_if<std::int64_t>(&value)) std::cout << *integer;
            else if (const auto* floating = value_get_if<double>(&value)) std::cout << *floating;
            else if (const auto* boolean = value_get_if<bool>(&value)) std::cout << (*boolean ? 1 : 0);
            else if (const auto* text = value.resource<std::pmr::string>())
            {
                if (!text) { conversion_error(values, arguments[index], "string", nullptr); return true; }
                std::cout << *text;
            }
            else if (value_holds<std::any>(value)) { conversion_error(values, arguments[index], "string", nullptr); return true; }
            else { set_error("output requires a value"); return true; }
        }
        if (name == "println") std::cout << '\n';
        destination.write_payload(result, static_cast<double>(count));
        return true;
    }
    if (name == "to_string")
    {
        std::pmr::string text(host.allocation_resource.get());
        if (count != 0)
        {
            const auto& argument = values.payload(arguments[0]);
            if (const auto* integer = value_get_if<std::int64_t>(&argument)) std::format_to(std::back_inserter(text), "{}", *integer);
            else if (const auto* floating = value_get_if<double>(&argument)) std::format_to(std::back_inserter(text), "{}", *floating);
            else if (const auto* string = argument.resource<std::pmr::string>())
            {
                if (!string) return false;
                text = *string;
            }
            else return false;
        }
        destination.write_text(result, std::move(text));
        return true;
    }
    if (name == "to_number")
    {
        if (count == 0) { destination.clear(result); return true; }
        const auto& value = values.payload(arguments[0]);
        if (const auto* integer = value_get_if<std::int64_t>(&value)) destination.write_payload(result, *integer);
        else if (const auto* floating = value_get_if<double>(&value)) destination.write_payload(result, *floating);
        else if (const auto* boolean = value_get_if<bool>(&value)) destination.write_payload(result, std::int64_t(*boolean));
        else if (const auto* text = value.resource<std::pmr::string>())
        {
            if (!text) { conversion_error(values, arguments[0], "number", nullptr); return true; }
            destination.write_payload(result, std::atof(text->c_str()));
        }
        else if (value_holds<std::any>(value)) conversion_error(values, arguments[0], "number", nullptr);
        else destination.clear(result);
        return true;
    }
    if (name == "type")
    {
        std::string type = "empty";
        if (count != 0)
        {
            const size_t slot = values.base() + arguments[0];
            const auto& descriptor = values.type_pool[values.slot_types[slot]];
            const auto& value = values.payload(arguments[0]);
            const auto* resource = value_get_if<std::any>(&value);
            if (descriptor.special == "NoValue" || value.resource<Object::NoValue>()) type = "NoValue";
            else if (descriptor.bound == typeid(std::int64_t)) type = "int";
            else if (descriptor.bound == typeid(double)) type = "double";
            else if (descriptor.bound == typeid(bool)) type = "bool";
            else if (!descriptor.declared.empty() && descriptor.declared != "auto")
            {
                type = descriptor.declared == "float" ? "double"
                    : descriptor.declared == "char" ? "int" : descriptor.declared;
            }
            else if (values.bindings[slot] == RegisterSlots::NumericBinding::Int) type = "int";
            else if (values.bindings[slot] == RegisterSlots::NumericBinding::Double) type = "double";
            else if (!values.empty(arguments[0]))
            {
                if (value_holds<std::int64_t>(value)) type = "int";
                else if (value_holds<double>(value)) type = "double";
                else if (value_holds<bool>(value)) type = "bool";
                else if (value.resource<VmArray>()) type = "array";
                else if (value.resource<VmMap>()) type = "map";
                else if (value.resource<std::pmr::string>()) type = "string";
                else if (const auto* resource = value_get_if<std::any>(&value)) type = resource->type().name();
            }
        }
        destination.write_text(result, std::string_view(type));
        return true;
    }
    if (name == "size")
    {
        if (count == 0) { destination.write_payload(result, std::int64_t{0}); return true; }
        if (count != 1) { set_error("function 'size' expects 0 or 1 arguments, got " + std::to_string(count)); return true; }
        const auto& value = values.payload(arguments[0]);
        if (const auto* text = value.resource<std::pmr::string>())
            destination.write_payload(result, static_cast<std::int64_t>(text->size()));
        else if (const auto* array = value.resource<VmArray>())
            destination.write_payload(result, static_cast<std::int64_t>(array->values.size()));
        else if (const auto* map = value.resource<VmMap>())
            destination.write_payload(result, static_cast<std::int64_t>(map->values.size()));
        else conversion_error(values, arguments[0], "string, array, or map", nullptr);
        return true;
    }
    if (name == "sprintf")
    {
        std::string_view format; // 解析结束前不修改参数寄存器，也不调用宿主。
        if (count == 0) { destination.write_text(result, format); return true; }
        if (!text_argument(0, format)) { conversion_error(values, arguments[0], "string", nullptr); return true; }
        std::pmr::string output(host.allocation_resource.get());
        size_t argument_index = 1;
        for (size_t index = 0; index < format.size();)
        {
            if (format[index] != '%') { output += format[index++]; continue; }
            if (index + 1 < format.size() && format[index + 1] == '%') { output += '%'; index += 2; continue; }
            const size_t specification_begin = index++;
            while (index < format.size() && std::string_view("-+ #0").find(format[index]) != std::string_view::npos) ++index;
            while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
            if (index < format.size() && format[index] == '.')
            {
                ++index;
                while (index < format.size() && std::isdigit(static_cast<unsigned char>(format[index]))) ++index;
            }
            while (index < format.size() && std::string_view("hlLzjtq").find(format[index]) != std::string_view::npos) ++index;
            if (index >= format.size()) break;
            const char conversion = format[index++];
            if (argument_index >= count) continue;
            std::string base = "%";
            for (size_t position = specification_begin + 1; position + 1 < index; ++position)
                if (std::string_view("hlLzjtq").find(format[position]) == std::string_view::npos) base += format[position];
            char buffer[512] = {};
            const auto& value = values.payload(arguments[argument_index++]);
            if (conversion == 's')
            {
                const auto* resource = value_get_if<std::any>(&value);
                const auto* string = value.resource<std::pmr::string>();
                if (!string) { conversion_error(values, arguments[argument_index - 1], "string", nullptr); return true; }
                std::snprintf(buffer, sizeof(buffer), (base + 's').c_str(), string->c_str());
            }
            else if (conversion == 'd' || conversion == 'i' || conversion == 'u' || conversion == 'o' || conversion == 'x' || conversion == 'X')
            {
                std::int64_t integer = 0;
                if (!values.integer(arguments[argument_index - 1], integer)) { conversion_error(values, arguments[argument_index - 1], "int", nullptr); return true; }
                if (conversion == 'd' || conversion == 'i')
                    std::snprintf(buffer, sizeof(buffer), (base + (conversion == 'd' ? "lld" : "lli")).c_str(), static_cast<long long>(integer));
                else std::snprintf(buffer, sizeof(buffer), (base + "ll" + conversion).c_str(), static_cast<unsigned long long>(integer));
            }
            else
            {
                std::int64_t integer = 0;
                double floating = 0;
                bool is_double = false;
                if (!values.number(arguments[argument_index - 1], integer, floating, is_double)) { conversion_error(values, arguments[argument_index - 1], "double", nullptr); return true; }
                std::snprintf(buffer, sizeof(buffer), (base + conversion).c_str(), is_double ? floating : static_cast<double>(integer));
            }
            output += buffer;
        }
        destination.write_text(result, std::move(output));
        return true;
    }
    if (name == "format")
    {
        std::string_view format; // 解析结束前不修改参数寄存器，也不调用宿主。
        if (count == 0) { destination.write_text(result, format); return true; }
        if (!text_argument(0, format)) { conversion_error(values, arguments[0], "string", nullptr); return true; }
        std::pmr::string output(host.allocation_resource.get());
        size_t automatic_index = 0;
        for (size_t index = 0; index < format.size();)
        {
            if (format[index] == '{' && index + 1 < format.size() && format[index + 1] == '{') { output += '{'; index += 2; continue; }
            if (format[index] == '}' && index + 1 < format.size() && format[index + 1] == '}') { output += '}'; index += 2; continue; }
            if (format[index] != '{') { output += format[index++]; continue; }
            const size_t end = format.find('}', index + 1);
            if (end == std::string::npos) { output += format[index++]; continue; }
            const std::string_view inner = format.substr(index + 1, end - index - 1);
            const size_t colon = inner.find(':');
            const std::string_view index_text = inner.substr(0, colon);
            const std::string_view specification = colon == std::string::npos ? std::string_view{} : inner.substr(colon + 1);
            size_t value_index = automatic_index++;
            if (!index_text.empty())
            {
                if (!std::all_of(index_text.begin(), index_text.end(), [](unsigned char character) { return std::isdigit(character) != 0; }))
                { output += format[index++]; continue; }
                value_index = static_cast<size_t>(std::stoull(std::string(index_text)));
            }
            std::pmr::string formatted(host.allocation_resource.get());
            if (value_index + 1 < count && format_argument(value_index + 1, specification, formatted)) output += formatted;
            index = end + 1;
        }
        destination.write_text(result, std::move(output));
        return true;
    }
    if (name == "ifv" || name == "ifvalue")
    {
        if (count != 3)
        {
            set_error("function '" + name + "' expects 3 arguments, got " + std::to_string(count));
            return true;
        }
        const bool selected = condition(values, arguments[0], nullptr);
        if (!should_stop()) destination.copy(result, values, arguments[selected ? 1 : 2]);
        return true;
    }
    if (name == "max" || name == "min")
    {
        if (count == 0) { destination.clear(result); return true; }
        std::int64_t best_integer = 0;
        double best_floating = 0;
        bool best_double = false;
        if (!values.number(arguments[0], best_integer, best_floating, best_double))
        { conversion_error(values, arguments[0], "number", nullptr); return true; }
        bool any_double = best_double;
        for (size_t index = 1; index < count; ++index)
        {
            std::int64_t integer = 0;
            double floating = 0;
            bool is_double = false;
            if (!values.number(arguments[index], integer, floating, is_double))
            { conversion_error(values, arguments[index], "number", nullptr); return true; }
            const bool greater = !is_double && !best_double ? integer > best_integer
                : (is_double ? floating : static_cast<double>(integer))
                    > (best_double ? best_floating : static_cast<double>(best_integer));
            const bool less = !is_double && !best_double ? integer < best_integer
                : (is_double ? floating : static_cast<double>(integer))
                    < (best_double ? best_floating : static_cast<double>(best_integer));
            if ((name == "max" && greater) || (name == "min" && less))
            {
                best_integer = integer;
                best_floating = floating;
                best_double = is_double;
            }
            any_double = any_double || is_double;
        }
        destination.write_payload(result, any_double
            ? BytecodeValue::Storage(best_double ? best_floating : static_cast<double>(best_integer))
            : BytecodeValue::Storage(best_integer));
        return true;
    }
    if (name == "abs" && count == 1)
    {
        std::int64_t integer = 0;
        double floating = 0;
        bool is_double = false;
        if (!values.number(arguments[0], integer, floating, is_double)) return false;
        if (!is_double)
        {
            if (integer == std::numeric_limits<std::int64_t>::min()) return false;
            destination.write_payload(result, integer < 0 ? -integer : integer);
        }
        else destination.write_payload(result, std::fabs(floating));
        return true;
    }
    if (count == 1)
    {
        std::int64_t integer = 0;
        double input = 0;
        bool is_double = false;
        if (!values.number(arguments[0], integer, input, is_double)) return false;
        if (!is_double) input = static_cast<double>(integer);
        double output = 0;
        if (name == "sqrt") output = std::sqrt(input);
        else if (name == "cbrt") output = std::cbrt(input);
        else if (name == "round") output = std::round(input);
        else if (name == "trunc") output = std::trunc(input);
        else if (name == "nearbyint") output = std::nearbyint(input);
        else if (name == "rint") output = std::rint(input);
        else if (name == "ceil") output = std::ceil(input);
        else if (name == "floor") output = std::floor(input);
        else if (name == "sin") output = std::sin(input);
        else if (name == "cos") output = std::cos(input);
        else if (name == "tan") output = std::tan(input);
        else if (name == "asin") output = std::asin(input);
        else if (name == "acos") output = std::acos(input);
        else if (name == "atan") output = std::atan(input);
        else if (name == "sinh") output = std::sinh(input);
        else if (name == "cosh") output = std::cosh(input);
        else if (name == "tanh") output = std::tanh(input);
        else if (name == "exp") output = std::exp(input);
        else if (name == "log") output = std::log(input);
        else if (name == "log2") output = std::log2(input);
        else if (name == "log10") output = std::log10(input);
        else if (name == "erf") output = std::erf(input);
        else if (name == "erfc") output = std::erfc(input);
        else if (name == "tgamma") output = std::tgamma(input);
        else if (name == "lgamma") output = std::lgamma(input);
        else return false;
        destination.write_payload(result, output);
        return true;
    }
    if (count == 2)
    {
        std::int64_t left_integer = 0, right_integer = 0;
        double left = 0, right = 0;
        bool left_double = false, right_double = false;
        if (!values.number(arguments[0], left_integer, left, left_double)
            || !values.number(arguments[1], right_integer, right, right_double)) return false;
        if (!left_double) left = static_cast<double>(left_integer);
        if (!right_double) right = static_cast<double>(right_integer);
        double output = 0;
        if (name == "atan2") output = std::atan2(left, right);
        else if (name == "pow") output = std::pow(left, right);
        else if (name == "hypot") output = std::hypot(left, right);
        else if (name == "fmod") output = std::fmod(left, right);
        else if (name == "remainder") output = std::remainder(left, right);
        else if (name == "copysign") output = std::copysign(left, right);
        else if (name == "fdim") output = std::fdim(left, right);
        else if (name == "fmax") output = std::fmax(left, right);
        else if (name == "fmin") output = std::fmin(left, right);
        else return false;
        destination.write_payload(result, output);
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
            return {&file, binding->slot,
                file.type_pool[file.slot_types[file.base() + binding->slot]].element, true};
        }
    }
    const size_t global_slot = find_global_slot(name);
    if (global_slot != std::numeric_limits<size_t>::max() && global_exists_at(global_slot))
    {
        const size_t index = global_values.base() + global_slot;
        return {&global_values, global_slot,
            global_values.type_pool[global_values.slot_types[index]].element, true};
    }
    if (!scopes.empty())
    {
        auto& binding = scopes.back().create(name);
        return {binding.file, binding.slot, {}, false};
    }
    return assign_named(name, "", false, false, {});
}

void CifaBytecode::Machine::call_method(RegisterSlots& destination, size_t slot, const std::string& name, const SourceLocation& location,
    NamedValueRef& receiver, const std::pmr::vector<SourceLocation>& locations, RegisterSlots& arguments)
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
    if (auto* array = container ? container->resource<VmArray>() : nullptr)
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
        if (name == "reserve")
        {
            if (!locations.empty()) values.reserve(static_cast<size_t>(integer_argument(0)));
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
                    if (destination.binary_payloads(Opcode::Equal, slot, value,
                        arguments.payload(0), *this, location))
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
    if (auto* map = container ? container->resource<VmMap>() : nullptr)
    {
        auto& values = map->values;
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
            for (const auto& [key, value] : values) keys.emplace_back(std::string(key));
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
    machine.import_host_globals();
    struct ExportGlobals
    {
        Machine& machine;
        ~ExportGlobals() { machine.export_host_globals(); }
    } export_globals{machine};
    machine.publish(std::shared_ptr<const Module>(&module, [](const Module*) { }));
    Object result;
    execute_instructions(machine, module, module.root_instructions, result);
    return !machine.error.empty() ? machine.error_result() : result;
}

CifaBytecode::Session::Session(CifaBytecode& interpreter)
{
    machine = std::make_unique<Machine>(interpreter);
}

CifaBytecode::Session::~Session() = default;

Object CifaBytecode::Session::run(CifaBytecode& code, const std::string& entry_label)
{
    code.runtime_error.clear();
    auto& vm = *machine;
    const size_t profile_base_size = code.profile_state.stack.size();
    if (code.profile_state.enabled)
    {
        const std::string profile_id = profile_base_size == 0 ? "root" : "nested";
        code.profile_enter_function(profile_id);
    }
    struct RestoreProfileState
    {
        CifaBytecode& code;
        size_t base_size = 0;
        ~RestoreProfileState()
        {
            code.profile_state.stack.resize(base_size);
            code.profile_state.last_pc.resize(base_size);
        }
    } restore_profile_state{code, profile_base_size};
    vm.import_host_globals();
    struct ExportGlobals
    {
        Machine& machine;
        ~ExportGlobals() { machine.export_host_globals(); }
    } export_globals{vm};
    const bool nested = active;
    auto saved_scopes = std::move(vm.scopes);
    auto saved_returns = std::move(vm.returns);
    auto saved_call_stack = std::move(vm.call_stack);
    auto saved_append_diagnostic_frames = std::move(vm.append_diagnostic_frames);
    auto saved_error = std::move(vm.error);
    RegisterSlots saved_registers(0, vm.host.allocation_resource);
    const bool saved_exit_requested = vm.exit_requested;
    if (nested)
    {
        saved_registers = std::move(vm.registers);
        vm.registers = RegisterSlots(0, vm.host.allocation_resource);
    }
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
        std::pmr::vector<Machine::ReturnState> returns;
        std::pmr::vector<std::pair<const SourceLocation*, bool>> call_stack;
        std::function<void(std::pmr::vector<std::pair<const SourceLocation*, bool>>&)> append_diagnostic_frames;
        std::string error;
        RegisterSlots registers;
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
            machine.registers = std::move(registers);
            machine.exit_requested = exit_requested;
        }
    } restore{*this, vm, nested, std::move(saved_scopes), std::move(saved_returns), std::move(saved_call_stack),
        std::move(saved_append_diagnostic_frames), std::move(saved_error), std::move(saved_registers), saved_exit_requested};
    if (!code.valid())
    {
        vm.set_error(code.translation_error.empty() ? "cannot run an invalid bytecode module" : code.translation_error);
        code.runtime_error = vm.error;
        return vm.error_result();
    }
    if (code.module_data->host_function_version != 0 && code.module_data->host_function_version != vm.host.native_function_version)
    {
        vm.set_error("native functions changed after optimized bytecode compilation");
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

void CifaBytecode::Session::sync_globals_to_host()
{
    if (active) machine->export_host_globals();
}

bool CifaBytecode::compile_script(std::string script)
{
    if (compiled_valid || !translation_error.empty()) return false;
    prepare_compile_visibility();
    const bool parsed = compile_script_internal(std::move(script));
    clear_compile_visibility();
    if (parsed) translate(*this, session->script_function_version(), native_function_version);
    return valid() && !has_error();
}

bool CifaBytecode::compile_file(const std::string& filename)
{
    if (compiled_valid || !translation_error.empty()) return false;
    prepare_compile_visibility();
    const bool parsed = compile_file_internal(filename);
    clear_compile_visibility();
    if (parsed) translate(*this, session->script_function_version(), native_function_version);
    return valid() && !has_error();
}

CifaBytecode::BytecodeStatistics CifaBytecode::bytecode_statistics() const
{
    BytecodeStatistics statistics;
    statistics.instruction_size = sizeof(Instruction);
    statistics.root_instruction_count = module_data->root_instructions.code.size();
    statistics.root_operand_count = module_data->root_instructions.register_inputs.size();
    statistics.root_register_capacity = module_data->root_instructions.register_capacity;
    statistics.constant_count = module_data->constants.size();
    statistics.call_site_count = module_data->calls.size();
    statistics.total_instruction_count = statistics.root_instruction_count;
    statistics.integer_loop_count = module_data->root_instructions.integer_loops.size();
    statistics.total_operand_count = statistics.root_operand_count;
    for (const auto& [name, overloads] : module_data->function_code)
    {
        for (const auto& [arity, function] : overloads)
        {
            statistics.functions.push_back({name, arity, function->instructions.code.size(),
                function->instructions.register_inputs.size(), function->instructions.register_capacity});
            statistics.total_instruction_count += function->instructions.code.size();
            statistics.integer_loop_count += function->instructions.integer_loops.size();
            statistics.total_operand_count += function->instructions.register_inputs.size();
        }
    }
    std::ranges::sort(statistics.functions, {}, [](const BytecodeStatistics::Function& function)
        { return std::pair(function.name, function.arity); });
    return statistics;
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
    auto nested = std::make_unique<CifaBytecode>(allocation_resource);
    nested->set_output_error(false);
    nested->set_optimization_enabled(optimization_enabled);
    session->sync_globals_to_host();
    const bool preserve_errors = session->is_active();
    ErrorSet earlier_errors;
    if (preserve_errors) earlier_errors = std::move(errors);
    prepare_compile_visibility();
    const bool parsed = compile_script_internal(std::move(script));
    clear_compile_visibility();
    const bool compile_error = !parsed || has_error();
    if (preserve_errors)
    {
        auto current_errors = std::move(errors);
        errors = std::move(earlier_errors);
        errors.insert(std::make_move_iterator(current_errors.begin()), std::make_move_iterator(current_errors.end()));
    }
    if (compile_error) return Object("", "Error");
    nested->translate(*this, session->script_function_version(), native_function_version);
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
    auto nested = std::make_unique<CifaBytecode>(allocation_resource);
    nested->set_output_error(false);
    nested->set_optimization_enabled(optimization_enabled);
    session->sync_globals_to_host();
    const bool preserve_errors = session->is_active();
    ErrorSet earlier_errors;
    if (preserve_errors) earlier_errors = std::move(errors);
    prepare_compile_visibility();
    const bool parsed = compile_file_internal(filename);
    clear_compile_visibility();
    const bool compile_error = !parsed || has_error();
    if (preserve_errors)
    {
        auto current_errors = std::move(errors);
        errors = std::move(earlier_errors);
        errors.insert(std::make_move_iterator(current_errors.begin()), std::make_move_iterator(current_errors.end()));
    }
    if (compile_error) return Object("", "Error");
    nested->translate(*this, session->script_function_version(), native_function_version);
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
    compile_visible_host_functions = &native_function_names;
}

void CifaBytecode::clear_compile_visibility()
{
    compile_visible_functions = nullptr;
    compile_visible_struct_defs = nullptr;
    compile_visible_host_functions = nullptr;
}
}
