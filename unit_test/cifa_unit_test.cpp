#include "../Cifa.h"
#include <chrono>
#include <filesystem>
#include <cstdio>
#include <print>
#include <numeric>
#include <format>
#include "../CifaBytecode.h"

using namespace cifa;
using DirectCifa = Cifa;

namespace cifa
{
struct RegisterBackendTest
{
    static bool run()
    {
        using Slots = CifaBytecode::RegisterSlots;
        static_assert(sizeof(std::int64_t) == 8 && sizeof(double) == 8 && sizeof(std::uint8_t) == 1);
        static_assert(sizeof(CifaBytecode::CompactValue) == 16);
        CifaBytecode::CompactValue compact_integer(std::int64_t(-17));
        CifaBytecode::CompactValue compact_floating(2.75);
        CifaBytecode::CompactValue compact_boolean(true);
        if (compact_integer.tag != CifaBytecode::CompactValue::Tag::Integer || compact_integer.integer() != -17
            || compact_floating.tag != CifaBytecode::CompactValue::Tag::Floating || compact_floating.floating() != 2.75
            || compact_boolean.tag != CifaBytecode::CompactValue::Tag::Boolean || !compact_boolean.boolean()) return false;
        CifaBytecode::CompactValue compact_array(std::any(ObjectVector{Object(1), Object(2)}));
        CifaBytecode::CompactValue compact_array_copy = compact_array;
        compact_array_copy.resource<CifaBytecode::VmArray>()->values.front() = CifaBytecode::CompactValue(std::int64_t(9));
        if (CifaBytecode::value_get<std::int64_t>(compact_array.resource<CifaBytecode::VmArray>()->values.front()) != 1) return false;
        CifaBytecode::CompactValue compact_moved = std::move(compact_array_copy);
        if (!compact_array_copy.empty() || CifaBytecode::value_get<std::int64_t>(
            compact_moved.resource<CifaBytecode::VmArray>()->values.front()) != 9) return false;
        Slots unified(4);
        unified.write_payload(0, std::int64_t(8));
        unified.write_payload(1, 2.5);
        unified.write_payload(2, true);
        unified.write_payload(3, std::any(std::string("resource")));
        if (CifaBytecode::value_get<std::int64_t>(unified.values[0].value) != 8
            || CifaBytecode::value_get<double>(unified.values[1].value) != 2.5
            || !CifaBytecode::value_get<bool>(unified.values[2].value)
            || std::any_cast<std::string>(CifaBytecode::value_get<std::any>(unified.values[3].value)) != "resource") return false;
        unified.enter(64);
        for (size_t index = 0; index < 64; ++index)
            unified.write_payload(index, std::any(std::string("nested")));
        unified.restore(0, 4, 4);
        if (std::any_cast<std::string>(CifaBytecode::value_get<std::any>(unified.resource_payload(3))) != "resource") return false;
        for (size_t index = 0; index < 32; ++index)
        {
            unified.write_payload(0, 7.5);
            unified.write_payload(0, std::int64_t(9));
        }
        if (!CifaBytecode::value_holds<std::int64_t>(unified.values[0].value)
            || CifaBytecode::value_get<std::int64_t>(unified.payload(0)) != 9) return false;
        unified.move(0, unified, 3);
        if (std::any_cast<std::string>(CifaBytecode::value_get<std::any>(unified.resource_payload(0))) != "resource"
            || !CifaBytecode::value_holds<std::monostate>(unified.payload(3))) return false;
        CifaBytecode::Scope dynamic_scope;
        const auto initial_binding = dynamic_scope.create("first");
        initial_binding.file->write_payload(initial_binding.slot, std::int64_t(17));
        for (size_t index = 0; index < 64; ++index) dynamic_scope.create("slot_" + std::to_string(index));
        const auto* preserved = dynamic_scope.find("first");
        if (!preserved || preserved->file != initial_binding.file || preserved->slot != initial_binding.slot
            || CifaBytecode::value_get<std::int64_t>(preserved->file->payload(preserved->slot)) != 17) return false;
        const auto repeated = dynamic_scope.create("first");
        if (repeated.slot != initial_binding.slot || dynamic_scope.dynamic_registers->size() != 65) return false;
        Slots values(8);
        Slots shared_owner(8);
        Slots shared_window(shared_owner, 2, 3);
        if (shared_window.storage || &shared_window.values != &shared_owner.values
            || &shared_window.bindings != &shared_owner.bindings || &shared_window.origins != &shared_owner.origins) return false;
        shared_window.write_payload(0, std::int64_t(41));
        if (CifaBytecode::value_get<std::int64_t>(shared_owner.payload(2)) != 41) return false;
        shared_owner.enter(64);
        shared_window.write_payload(1, std::int64_t(42));
        shared_owner.restore(0, 8, 8);
        if (CifaBytecode::value_get<std::int64_t>(shared_owner.payload(3)) != 42) return false;
        shared_window.move(0, shared_owner, 2);
        shared_window.copy(1, shared_owner, 3);
        if (CifaBytecode::value_get<std::int64_t>(shared_owner.payload(2)) != 41
            || CifaBytecode::value_get<std::int64_t>(shared_owner.payload(3)) != 42) return false;
        values.import_object(0, Object(std::int64_t(7)));
        values.import_object(1, Object(2.5));
        values.import_object(2, Object(true));
        if (values.values.size() != 8 || CifaBytecode::value_get<std::int64_t>(values.payload(0)) != 7 || CifaBytecode::value_get<double>(values.payload(1)) != 2.5
            || !CifaBytecode::value_holds<bool>(values.payload(2))) return false;
        values.write_payload(0, std::int64_t(9));
        if (CifaBytecode::value_get<std::int64_t>(values.payload(0)) != 9) return false;
        values.enter(3);
        if (values.base() != 8 || values.top() != 11 || values.size() != 3) return false;
        values.restore(0, 8, 8);
        values.enter(2);
        values.import_object(0, Object(4));
        values.enter(2);
        values.import_object(0, Object(5));
        if (values.values.size() != 12 || CifaBytecode::value_get<std::int64_t>(values.payload(0)) != 5) return false;
        values.restore(8, 2, 10);
        if (CifaBytecode::value_get<std::int64_t>(values.payload(0)) != 4 || values.top() != 10) return false;
        values.restore(0, 8, 8);
        values.import_object(3, Object(ObjectVector{Object(1), Object(2)}));
        values.copy(4, values, 3);
        std::any_cast<CifaBytecode::VmArray&>(CifaBytecode::value_get<std::any>(values.resource_payload(4))).values[0]
            = CifaBytecode::CompactValue(std::int64_t(8));
        if (CifaBytecode::value_get<std::int64_t>(std::any_cast<CifaBytecode::VmArray&>(
            CifaBytecode::value_get<std::any>(values.resource_payload(3))).values[0]) != 1) return false;
        CifaBytecode numeric_host;
        CifaBytecode::Machine numeric_machine(numeric_host);
        Slots no_values(2);
        no_values.import_object(0, numeric_machine.make_no_value("missing_result", {}));
        if (!CifaBytecode::value_holds<std::any>(no_values.values[0].value)) return false;
        no_values.copy(1, no_values, 0);
        Object boundary_no_value;
        no_values.export_argument(1, boundary_no_value);
        if (boundary_no_value.getSpecialType() != "NoValue" || !no_values.empty(1)) return false;
        if (numeric_machine.condition(no_values, 0, nullptr)
            || numeric_machine.error.find("function 'missing_result' has no return value") == std::string::npos) return false;
        numeric_machine.error.clear();
        Slots conditions(4);
        CifaBytecode::SourceLocation binary_error_location;
        binary_error_location.filename = "slot_binary.c";
        binary_error_location.line = 2;
        binary_error_location.col = 25;
        binary_error_location.text = "return missing_result() + 1;";
        no_values.write_payload(1, std::int64_t(1));
        no_values.binary_fallback(CifaBytecode::Opcode::Add, 0, 0, 1, numeric_machine, binary_error_location, false);
        if (numeric_machine.error.find("function 'missing_result' has no return value") == std::string::npos
            || !no_values.empty(0) || !no_values.empty(1)) return false;
        const std::string binary_error_header = "slot_binary.c:2, col 25: ";
        const std::string binary_error_frame = "  at " + binary_error_header + binary_error_location.text + "\n"
            + std::string(5 + binary_error_header.size() + binary_error_location.text.find('+'), ' ') + "^";
        if (numeric_machine.error.find(binary_error_frame) == std::string::npos)
        {
            std::print(stderr, "BINARY_DIAGNOSTIC_BEGIN\n{}\nBINARY_DIAGNOSTIC_END\n", numeric_machine.error);
            return false;
        }
        numeric_machine.error.clear();
        conditions.write_payload(0, true);
        conditions.write_payload(1, std::int64_t(0));
        conditions.write_payload(2, -0.5);
        if (!numeric_machine.condition(conditions, 0, nullptr)
            || numeric_machine.condition(conditions, 1, nullptr)
            || !numeric_machine.condition(conditions, 2, nullptr)) return false;
        const CifaBytecode::SourceLocation condition_location;
        if (numeric_machine.condition(conditions, 3, &condition_location)
            || numeric_machine.error.find("condition requires a value") == std::string::npos) return false;
        numeric_machine.error.clear();
        Slots containers(1);
        Slots method_arguments(1);
        Slots method_result(1);
        containers.write_payload(0, std::any(ObjectVector{}));
        method_arguments.write_payload(0, std::int64_t(23));
        const std::vector<CifaBytecode::SourceLocation> method_locations(1);
        CifaBytecode::Machine::NamedValueRef array_receiver{&containers, 0, nullptr, {}, true};
        numeric_machine.call_method(method_result, 0, "push_back", {}, array_receiver, method_locations, method_arguments);
        if (numeric_machine.should_stop() || CifaBytecode::value_get<double>(method_result.payload(0)) != 1) return false;
        const auto stored_values = [&]() -> const std::vector<CifaBytecode::CompactValue>& {
            return std::any_cast<const CifaBytecode::VmArray&>(
                CifaBytecode::value_get<std::any>(containers.resource_payload(0))).values;
        };
        if (stored_values().size() != 1 || CifaBytecode::value_get<std::int64_t>(stored_values().front()) != 23) return false;
        method_arguments.write_payload(0, std::int64_t(23));
        numeric_machine.call_method(method_result, 0, "contains", {}, array_receiver, method_locations, method_arguments);
        if (numeric_machine.should_stop() || CifaBytecode::value_get<double>(method_result.payload(0)) != 1) return false;
        method_arguments.write_payload(0, 4.75);
        array_receiver.element_type = "int";
        numeric_machine.call_method(method_result, 0, "push_back", {}, array_receiver, method_locations, method_arguments);
        if (numeric_machine.should_stop() || stored_values().size() != 2
            || CifaBytecode::value_get<std::int64_t>(stored_values().back()) != 4) return false;
        Slots map_receiver(1);
        map_receiver.write_payload(0, std::any(ObjectMap{{"present", Object(1)}}));
        CifaBytecode::Machine::NamedValueRef map_value{&map_receiver, 0, nullptr, {}, true};
        method_arguments.write_payload(0, std::any(std::string("present")));
        numeric_machine.call_method(method_result, 0, "contains", {}, map_value, method_locations, method_arguments);
        if (numeric_machine.should_stop() || !CifaBytecode::value_get<bool>(method_result.payload(0))) return false;
        numeric_machine.call_method(method_result, 0, "erase", {}, map_value, method_locations, method_arguments);
        if (numeric_machine.should_stop() || CifaBytecode::value_get<double>(method_result.payload(0)) != 0) return false;
        numeric_machine.call_method(method_result, 0, "clear", {}, array_receiver, {}, method_arguments);
        if (numeric_machine.should_stop() || CifaBytecode::value_get<double>(method_result.payload(0)) != 0 || !stored_values().empty()) return false;
        Object typed_value;
        Object typed_text;
        if (!numeric_machine.assign(typed_text, Object(std::string("first")), true, "string", {})) return false;
        Slots text_slots(3);
        Slots string_operations(2);
        string_operations.write_payload(0, std::any(std::string("left")));
        string_operations.write_payload(1, std::any(std::string("right")));
        string_operations.binary_fallback(CifaBytecode::Opcode::Add, 0, 0, 1, numeric_machine, {}, true);
        if (!string_operations.empty(1)
            || std::any_cast<std::string>(CifaBytecode::value_get<std::any>(string_operations.payload(0))) != "leftright") return false;
        string_operations.write_payload(1, std::any(std::string("leftright")));
        string_operations.binary_fallback(CifaBytecode::Opcode::Equal, 1, 0, 1, numeric_machine, {}, false);
        if (!string_operations.empty(0) || !CifaBytecode::value_get<bool>(string_operations.payload(1))) return false;
        Slots other_text_slots(2);
        CifaBytecode callback_host;
        CifaBytecode::Machine callback_machine(callback_host);
        Slots callback_slots(2);
        callback_slots.write_payload(0, std::any(std::string("operand")));
        callback_slots.binary_fallback(CifaBytecode::Opcode::Subtract, 1, 0, 0, callback_machine, {}, false);
        if (!callback_slots.empty(0) || !callback_slots.empty(1)) return false;
        size_t callback_count = 0;
        bool same_argument = false;
        callback_host.user_sub.push_back([&](const Object&, const Object&) { ++callback_count; return Object(); });
        callback_host.user_sub.push_back([&](const Object& left, const Object& right) {
            ++callback_count;
            same_argument = &left == &right && left.isType<std::string>();
            return Object(17);
        });
        callback_host.user_sub.push_back([&](const Object&, const Object&) { ++callback_count; return Object(99); });
        callback_slots.write_payload(0, std::any(std::string("operand")));
        callback_slots.binary_fallback(CifaBytecode::Opcode::Subtract, 0, 0, 0, callback_machine, {}, true);
        if (callback_count != 2 || !same_argument || CifaBytecode::value_get<std::int64_t>(callback_slots.payload(0)) != 17) return false;
        callback_host.user_mod.push_back([&](const Object&, const Object&) { ++callback_count; return Object(99); });
        callback_slots.write_payload(0, 1.5);
        callback_slots.write_payload(1, std::int64_t(2));
        callback_slots.binary_fallback(CifaBytecode::Opcode::Modulo, 0, 0, 1, callback_machine, binary_error_location, false);
        if (callback_count != 2 || callback_machine.error.find("operator % requires integer operands") == std::string::npos
            ) return false;
        const std::pair<CifaBytecode::Opcode, const char*> integer_operations[] = {
            {CifaBytecode::Opcode::Modulo, "%"}, {CifaBytecode::Opcode::BitAnd, "&"},
            {CifaBytecode::Opcode::BitOr, "|"}, {CifaBytecode::Opcode::BitXor, "^"},
            {CifaBytecode::Opcode::ShiftLeft, "<<"}, {CifaBytecode::Opcode::ShiftRight, ">>"}
        };
        for (const auto& [opcode, symbol] : integer_operations)
        {
            callback_machine.error.clear();
            auto operation_location = binary_error_location;
            operation_location.col = 12;
            operation_location.text = std::string("return 1.5 ") + symbol + " 2;";
            callback_slots.write_payload(0, 1.5);
            callback_slots.write_payload(1, std::int64_t(2));
            callback_slots.binary_fallback(opcode, 0, 0, 1, callback_machine, operation_location, false);
            const std::string header = "slot_binary.c:2, col 12: ";
            const std::string frame = "  at " + header + operation_location.text + "\n"
                + std::string(5 + header.size() + 11, ' ') + "^";
            if (callback_machine.error.find(std::string("operator ") + symbol + " requires integer operands") == std::string::npos
                || callback_machine.error.find(frame) == std::string::npos || callback_count != 2
                ) return false;
        }
        Slots constrained_array(1);
        constrained_array.write_payload(0, std::any(ObjectVector{}));
        constrained_array.set_type(0, {typeid(ObjectVector), "array", "int", ""});
        numeric_machine.scopes.emplace_back();
        numeric_machine.scopes.back().bind("constrained", &constrained_array, 0);
        Slots index_arguments(1);
        index_arguments.write_payload(0, std::int64_t(3));
        const size_t index_slots[] = {0};
        auto expanded_element = numeric_machine.indexed("constrained", "", 1, false, false, false, index_arguments, index_slots);
        if (!numeric_machine.assign_indexed(expanded_element, Object(7.75), false, "", {})) return false;
        numeric_machine.read_indexed(other_text_slots, 0, expanded_element, false);
        if (CifaBytecode::value_get<std::int64_t>(other_text_slots.payload(0)) != 7) return false;
        numeric_machine.scopes.back().bind("keyed", &map_receiver, 0);
        index_arguments.write_payload(0, std::any(std::string("new_key")));
        auto keyed_element = numeric_machine.indexed("keyed", "", 1, false, false, false, index_arguments, index_slots);
        numeric_machine.assign_indexed(keyed_element, Object(19), false, "", {});
        if (numeric_machine.should_stop()) return false;
        if (numeric_machine.named_value("constrained").element_type != "int") return false;
        numeric_machine.read_named(other_text_slots, 0, "constrained", "", false, false, false, {});
        if (numeric_machine.should_stop() || other_text_slots.type_pool[other_text_slots.slot_types[0]].element != "int") return false;
        numeric_machine.scopes.pop_back();
        numeric_machine.structures["SlotRecord"] = {StructField{"field", "int"}};
        numeric_machine.scopes.emplace_back();
        numeric_machine.read_named(other_text_slots, 0, "record", "SlotRecord", true, false, true, {});
        auto* record_binding = numeric_machine.find_slot("record");
        if (!record_binding) return false;
        auto& record_field = numeric_machine.resolve_member("record", "field");
        if (!numeric_machine.assign(record_field, Object(8.75), false, "", {}) || record_field.toInt64() != 8) return false;
        numeric_machine.scopes.pop_back();
        text_slots.import_object(0, typed_text);
        if (text_slots.slot_types[0] == 0) return false;
        Slots compatibility_source(1);
        Slots decoded_targets(2);
        compatibility_source.import_object(0, typed_text);
        compatibility_source.set_name(0, "retained_source");
        decoded_targets.copy(0, compatibility_source, 0);
        decoded_targets.move(1, compatibility_source, 0);
        if (!compatibility_source.empty(0)) return false;
        for (size_t slot = 0; slot < 2; ++slot)
            if (decoded_targets.type_pool[decoded_targets.slot_types[slot]].declared != "string"
                || decoded_targets.name_pool[decoded_targets.slot_names[slot]] != "retained_source"
                || std::any_cast<std::string>(CifaBytecode::value_get<std::any>(decoded_targets.payload(slot))) != "first") return false;
        text_slots.copy(1, text_slots, 0);
        if (text_slots.slot_types[1] != text_slots.slot_types[0]) return false;
        other_text_slots.copy(0, text_slots, 1);
        other_text_slots.move(1, text_slots, 0);
        if (text_slots.slot_types[0] != 0 || !text_slots.empty(0)) return false;
        Object text_boundary;
        other_text_slots.export_argument(1, text_boundary);
        if (other_text_slots.slot_types[1] != 0
            || !numeric_machine.assign(text_boundary, Object(std::string("second")), false, "", {})) return false;
        text_slots.import_object(2, text_boundary);
        if (text_slots.type_pool[text_slots.slot_types[2]].declared != "string") return false;
        other_text_slots.copy(0, text_slots, 2);
        other_text_slots.export_argument(0, text_boundary);
        text_slots.import_object(0, text_boundary);
        if (text_slots.type_pool[text_slots.slot_types[0]].declared != "string") return false;
        text_slots.clear(0);
        if (text_slots.slot_types[0] != 0) return false;
        if (!numeric_machine.assign(typed_value, Object(3), true, "int", {})) return false;
        Slots assigned_slots(2);
        Slots assignment_source(2);
        assignment_source.write_payload(0, std::any(std::string("range")));
        if (!numeric_machine.bind_range(assignment_source, 0, "element", "string", {})
            || assignment_source.type_pool[assignment_source.slot_types[0]].declared != "string"
            || assignment_source.name_pool[assignment_source.slot_names[0]] != "element") return false;
        assigned_slots.import_object(0, typed_value);
        assignment_source.write_payload(0, 6.75);
        if (!numeric_machine.assign(assigned_slots, 0, assignment_source, 0, 1, {})
            || assigned_slots.bindings[0] != Slots::NumericBinding::Int
            || CifaBytecode::value_get<std::int64_t>(assigned_slots.payload(0)) != 6 || !assignment_source.empty(1)) return false;
        assigned_slots.set_type(1, {typeid(void), "auto", "", ""});
        assignment_source.write_payload(0, std::any(std::string("inferred")));
        if (!numeric_machine.assign(assigned_slots, 1, assignment_source, 0, 1, {})
            || assigned_slots.type_pool[assigned_slots.slot_types[1]].declared != "string") return false;
        CifaBytecode::Machine rejected_assignment(numeric_host);
        if (rejected_assignment.assign(assigned_slots, 0, assignment_source, 0, 1, {})
            || !rejected_assignment.should_stop()
            || CifaBytecode::value_get<std::int64_t>(assigned_slots.payload(0)) != 6) return false;
        Slots typed(3);
        typed.import_object(0, typed_value);
        if (typed.bindings[0] != Slots::NumericBinding::Int) return false;
        typed.set_name(0, "argument_name");
        typed.copy(2, typed, 0);
        if (typed.name_pool[typed.slot_names[2]] != "argument_name") return false;
        typed.move(1, typed, 2);
        if (typed.slot_names[2] != 0 || typed.name_pool[typed.slot_names[1]] != "argument_name") return false;
        Object named_argument;
        typed.export_argument(1, named_argument);
        if (named_argument.toInt64() != 3 || typed.slot_names[1] != 0) return false;
        typed.import_object(1, named_argument);
        if (typed.name_pool[typed.slot_names[1]] != "argument_name") return false;
        Slots restored_name(1);
        Slots materialized_name(1);
        materialized_name.copy(0, typed, 0);
        Object materialized_boundary;
        materialized_name.export_argument(0, materialized_boundary);
        restored_name.import_object(0, materialized_boundary);
        if (restored_name.name_pool[restored_name.slot_names[0]] != "argument_name" || !materialized_name.empty(0)) return false;
        typed.clear(2);
        typed.set_name(0, "");
        typed.import_object(1, Object(4.75));
        if (!typed.assign_numeric(0, typed, 1) || CifaBytecode::value_get<std::int64_t>(typed.payload(0)) != 4) return false;
        typed.copy(2, typed, 0);
        if (typed.bindings[2] != Slots::NumericBinding::Int) return false;
        typed.move(1, typed, 2);
        if (typed.bindings[1] != Slots::NumericBinding::Int
            || !CifaBytecode::value_holds<std::monostate>(typed.payload(2)) || typed.bindings[2] != Slots::NumericBinding::None) return false;
        typed.move(2, typed, 1);
        typed.move(2, typed, 2);
        Object extracted;
        typed.export_argument(2, extracted);
        if (typed.bindings[2] != Slots::NumericBinding::None || !CifaBytecode::value_holds<std::monostate>(typed.payload(2))) return false;
        if (!numeric_machine.assign(extracted, Object(8.75), false, "", {}) || extracted.toInt64() != 8) return false;
        typed.write_payload(1, 6.75);
        if (!numeric_machine.assign(typed, 0, typed, 1, 2, {}) || CifaBytecode::value_get<std::int64_t>(typed.payload(0)) != 6) return false;
        typed.clear(0);
        if (!CifaBytecode::value_holds<std::monostate>(typed.payload(0))) return false;
        Slots compact(3);
        compact.write_payload(0, -3.75);
        if (!compact.cast_numeric(0, compact, 0, "int")
            || CifaBytecode::value_get<std::int64_t>(compact.payload(0)) != -3) return false;
        compact.write_payload(1, std::numeric_limits<double>::infinity());
        if (compact.cast_numeric(0, compact, 1, "int")
            || CifaBytecode::value_get<std::int64_t>(compact.payload(0)) != -3) return false;
        if (!compact.cast_numeric(1, compact, 1, "bool") || !CifaBytecode::value_get<bool>(compact.payload(1))) return false;
        if (!compact.cast_numeric(1, compact, 1, "double") || CifaBytecode::value_get<double>(compact.payload(1)) != 1) return false;
        compact.import_object(0, Object(7));
        compact.import_object(1, Object(2));
        if (!compact.binary(CifaBytecode::Opcode::Add, 0, 0, 1, numeric_machine, {})
            || CifaBytecode::value_get<std::int64_t>(compact.payload(0)) != 9) return false;
        compact.copy(2, compact, 0);
        compact.export_argument(2, extracted);
        if (extracted.toInt64() != 9 || !CifaBytecode::value_holds<std::monostate>(compact.payload(2))) return false;
        auto resource = std::make_shared<int>(12);
        std::weak_ptr<int> lifetime = resource;
        compact.import_object(2, Object(std::move(resource)));
        resource.reset();
        compact.copy(1, compact, 2);
        compact.clear(2);
        if (lifetime.expired()) return false;
        compact.clear(1);
        if (!lifetime.expired()) return false;
        compact.import_object(2, Object(std::make_shared<int>(13)));
        lifetime = std::any_cast<std::shared_ptr<int>>(CifaBytecode::value_get<std::any>(compact.resource_payload(2)));
        compact.enter(32);
        compact.restore(0, 3, 3);
        if (*std::any_cast<std::shared_ptr<int>>(CifaBytecode::value_get<std::any>(compact.resource_payload(2))) != 13) return false;
        compact.clear(2);
        if (!lifetime.expired() || !compact.empty(2)) return false;
        compact.enter(4);
        compact.write_payload(0, std::any(std::make_shared<int>(21)));
        std::weak_ptr<int> window_resource = std::any_cast<std::shared_ptr<int>>(CifaBytecode::value_get<std::any>(compact.payload(0)));
        compact.import_object(1, Object(std::make_shared<int>(22)));
        std::weak_ptr<int> materialized_resource = std::any_cast<std::shared_ptr<int>>(CifaBytecode::value_get<std::any>(compact.resource_payload(1)));
        const size_t retained_capacity = compact.values.capacity();
        compact.enter(2);
        compact.write_payload(0, std::any(std::make_shared<int>(23)));
        std::weak_ptr<int> nested_resource = std::any_cast<std::shared_ptr<int>>(CifaBytecode::value_get<std::any>(compact.payload(0)));
        compact.restore(3, 4, 7);
        if (!nested_resource.expired() || window_resource.expired() || materialized_resource.expired()) return false;
        compact.restore(0, 3, 3);
        if (!window_resource.expired() || !materialized_resource.expired()
            || compact.values.capacity() != retained_capacity || !compact.empty(2)) return false;
        compact.enter(4);
        for (size_t index = 0; index < compact.size(); ++index)
            if (!CifaBytecode::value_holds<std::monostate>(compact.payload(index))) return false;
        compact.restore(0, 3, 3);
        std::print("Register layout: Object={}, BytecodeValue={}, origin pointer={}\n",
            sizeof(Object), sizeof(CifaBytecode::BytecodeValue), sizeof(const Object*));
        if (!numeric_machine.bind_type(values, 5, "int", CifaBytecode::SourceLocation{})) return false;
        for (double number : {3.75, -3.75, -9223372036854775808.0, 9223372036854774784.0})
        {
            values.import_object(6, Object(number));
            if (!values.assign_numeric(5, values, 6)
                || CifaBytecode::value_get<std::int64_t>(values.payload(5)) != static_cast<std::int64_t>(number)) return false;
        }
        for (double number : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
            std::numeric_limits<double>::quiet_NaN(), 9223372036854775808.0, -18446744073709551616.0})
        {
            const auto previous = CifaBytecode::value_get<std::int64_t>(values.payload(5));
            values.import_object(6, Object(number));
            if (values.assign_numeric(5, values, 6) || CifaBytecode::value_get<std::int64_t>(values.payload(5)) != previous) return false;
        }
        CifaBytecode interpreter;
        interpreter.set_output_error(false);
        interpreter.set_optimization_enabled(false);
        if (!interpreter.compile_script("int sum(int left, int right) { int total = 0; total = left + right; return total; } return sum(2, 3);")) return false;
        const auto& function = *interpreter.module_data->function_code.at("sum").at(2);
        size_t arithmetic = 0;
        size_t reserved_scope_bindings = 0;
        for (const auto& instruction : function.instructions.code)
        {
            if (instruction.opcode == CifaBytecode::Opcode::RegisterBinary) ++arithmetic;
            if (instruction.opcode == CifaBytecode::Opcode::ScopeEnter) reserved_scope_bindings += instruction.operand;
            if (instruction.opcode == CifaBytecode::Opcode::Add) return false;
            if (instruction.input_offset + instruction.input_count > function.instructions.register_inputs.size()) return false;
            for (size_t input = 0; input < instruction.input_count; ++input)
                if (function.instructions.register_inputs[instruction.input_offset + input] >= function.instructions.register_capacity)
                    return false;
        }
        if (arithmetic != 1 || reserved_scope_bindings == 0
            || interpreter.run().toInt64() != 5 || interpreter.has_runtime_error()) return false;

        for (bool optimized : {false, true})
        {
            CifaBytecode compound;
            compound.set_output_error(false);
            compound.set_optimization_enabled(optimized);
            size_t calls = 0;
            compound.user_sub.push_back([&](const Object& left, const Object& right) {
                if (!left.isType<std::string>() || !right.isType<std::string>()) return Object();
                ++calls;
                return Object(left.toString() + right.toString());
            });
            if (!compound.compile_script(R"(
                string exercise(string prefix, string suffix) {
                    string result = prefix + suffix;
                    result += suffix;
                    result -= prefix;
                    string custom = prefix - suffix;
                    result = result + custom;
                    return result;
                }
                return exercise("a", "b");
            )")) return false;
            for (size_t repeat = 0; repeat < 3; ++repeat)
            {
                const auto actual = compound.run();
                if (compound.has_runtime_error() || !actual.isType<std::string>()
                    || actual.toString() != "abbaab" || calls != (repeat + 1) * 2) return false;
            }
        }

        for (bool optimized : {false, true})
        {
            CifaBytecode switch_error;
            CifaBytecode aliases;
            aliases.set_output_error(false);
            aliases.set_optimization_enabled(optimized);
            if (!aliases.compile_script(R"(
                double exercise_alias() {
                    double value = 1.5;
                    double result = 0;
                    {
                        double value;
                        value += 2;
                        result = value++;
                        result += ++value;
                        value = value + 2;
                    }
                    return result + value;
                }
                return exercise_alias();
            )")) return false;
            const auto alias_result = aliases.run();
            if (aliases.has_runtime_error() || !alias_result.isNumber() || alias_result.toDouble() != 16.5) return false;
            switch_error.set_output_error(false);
            switch_error.set_optimization_enabled(optimized);
            size_t effects = 0;
            switch_error.register_function("effect", [&](ObjectVector&) { ++effects; return Object(1); });
            if (!switch_error.compile_script(
                "void missing_switch() {} switch (missing_switch()) { case 1: effect(); break; default: effect(); }")) return false;
            switch_error.run();
            if (!switch_error.has_runtime_error() || effects != 0
                || switch_error.get_runtime_error().find("function 'missing_switch' has no return value") == std::string::npos) return false;
        }

        const std::string nested_arguments = R"(
            int combine(int left, int right) { int total = left * 10 + right; return total; }
            int recurse(int count) {
                if (count == 0) return 1;
                int saved = count;
                return combine(saved, recurse(count - 1));
            }
            int exercise() {
                int total = 0;
                for (int index = 0; index < 12; index++)
                    total += combine(combine(1, 2), combine(3, 4));
                return total + recurse(3);
            }
            return exercise();
        )";
        for (bool optimized : {false, true})
        {
            CifaBytecode nested;
            nested.set_output_error(false);
            nested.set_optimization_enabled(optimized);
            if (!nested.compile_script(nested_arguments)) return false;
            CifaBytecode::Machine shared_machine(nested);
            shared_machine.publish(nested.module_data);
            size_t capacity = 0;
            for (size_t iteration = 0; iteration < 3; ++iteration)
            {
                const auto actual = CifaBytecode::run_module(shared_machine, *nested.module_data);
                if (shared_machine.should_stop() || !actual.isNumber() || actual.toInt64() != 1909) return false;
                const auto& slots = shared_machine.registers;
                if (slots.base() != 0 || slots.size() != 0 || slots.top() != 0) return false;
                if (iteration == 0) capacity = slots.values.capacity();
                else if (capacity != slots.values.capacity()) return false;
                for (size_t index = 0; index < slots.values.size(); ++index)
                    if (!CifaBytecode::value_holds<std::monostate>(slots.payload(index))) return false;
            }
        }

        CifaBytecode reentrant_host;
        reentrant_host.set_optimization_enabled(false);
        for (bool optimized : {false, true})
        {
            CifaBytecode globals;
            globals.set_output_error(false);
            globals.set_optimization_enabled(optimized);
            const auto replace_global = [&](int value) {
                return globals.register_parameter("shared_value", Object(value));
            };
            size_t registered_count = 0;
            globals.register_function("replace_global", [&](ObjectVector&) {
                for (size_t index = 0; index < 1024; ++index)
                    if (!globals.register_parameter("expanded_global_" + std::to_string(registered_count++), Object(7))) return Object();
                replace_global(11);
                return Object(0);
            });
            globals.user_sub.push_back([&](const Object& left, const Object& right) {
                if (!left.isType<std::string>() || !right.isType<std::string>()) return Object();
                replace_global(33);
                return Object(0);
            });
            if (!globals.compile_script(R"script(
                shared_value = 1;
                before_host = shared_value;
                replace_global();
                after_host = shared_value;
                { int shared_value = 77; after_host += shared_value; }
                ignored = "left" - "right";
                return before_host * 10000 + after_host * 100 + shared_value;
            )script")) return false;
            CifaBytecode::Session global_session(globals);
            for (size_t repeat = 0; repeat < 3; ++repeat)
            {
                const auto actual = global_session.run(globals);
                if (globals.has_runtime_error() || !actual.isNumber() || actual.toInt64() != 18833) return false;
            }
        }
        CifaBytecode reentrant_inner;
        reentrant_inner.set_optimization_enabled(false);
        reentrant_host.set_output_error(false);
        reentrant_inner.set_output_error(false);
        if (!reentrant_inner.compile_script("int inner(int value) { return value + 1; } return inner(inner(5));")) return false;
        CifaBytecode::Session reentrant_session(reentrant_host);
        reentrant_host.register_function("host_nested", [&](ObjectVector&)
            { return reentrant_session.run(reentrant_inner); });
        if (!reentrant_host.compile_script(
            "int combine(int left, int right) { return left * 10 + right; } "
            "int outer(int value) { int saved = value; return combine(saved, host_nested()) + saved; } return outer(9);")) return false;
        for (size_t iteration = 0; iteration < 3; ++iteration)
        {
            const auto actual = reentrant_session.run(reentrant_host);
            if (reentrant_host.has_runtime_error() || reentrant_inner.has_runtime_error()
                || !actual.isNumber() || actual.toInt64() != 106) return false;
        }

        for (bool optimized : {false, true})
        {
            CifaBytecode argument_error;
            argument_error.set_output_error(false);
            argument_error.set_optimization_enabled(optimized);
            size_t later_arguments = 0;
            argument_error.register_function("later_argument", [&](ObjectVector&)
                { ++later_arguments; return Object(3); });
            const std::string failing_line = "return accept(1, \"bad\", later_argument());";
            if (!argument_error.compile_script("int accept(int first, int second, int third) { return first; }\n" + failing_line)) return false;
            argument_error.run();
            const auto error = argument_error.get_runtime_error();
            if (!argument_error.has_runtime_error() || later_arguments != 0
                || error.find("cannot convert value to 'int'") == std::string::npos) return false;
            const size_t source_position = error.find(failing_line);
            if (source_position == std::string::npos) return false;
            const size_t source_start = error.rfind('\n', source_position);
            const size_t source_end = error.find('\n', source_position);
            const size_t caret = error.find('^', source_end);
            if (source_end == std::string::npos || caret == std::string::npos) return false;
            const size_t expected_column = source_position - (source_start == std::string::npos ? 0 : source_start + 1)
                + failing_line.find("bad");
            const size_t actual_column = caret - source_end - 1;
            if (actual_column != expected_column)
            {
                std::print("argument alignment expected={} actual={}\n{}\n", expected_column, actual_column, error);
                return false;
            }
        }

        CifaBytecode replaced_call;
        replaced_call.set_output_error(false);
        replaced_call.set_optimization_enabled(false);
        replaced_call.register_function("replace_target", [&](ObjectVector&)
        {
            replaced_call.register_function("target", [](ObjectVector& arguments)
                { return Object(arguments[0].toInt64() * 10 + arguments[1].toInt64()); });
            return Object(7);
        });
        if (!replaced_call.compile_script(
            "int target(int left, int right) { return left + right; } "
            "int outer() { int first = target(4, replace_target()); return first + target(1, 2); } return outer();")) return false;
        const auto replacement_result = replaced_call.run();
        if (replaced_call.has_runtime_error() || !replacement_result.isNumber() || replacement_result.toInt64() != 59) return false;

        const std::string local_initialization = R"(
            int exercise() {
                int value = 3;
                int value = 5;
                int total = 0;
                for (int index = 0; index < 4; index++) {
                    int value = 7;
                    total += value;
                }
                { string value = "abc"; total += 3; }
                { int value = 9; total += value; }
                return total + value;
            }
            return exercise();
        )";
        Cifa ast;
        ast.set_output_error(false);
        const auto expected = ast.run_script(local_initialization);
        if (ast.has_runtime_error() || !expected.isNumber() || expected.toInt64() != 45)
        {
            std::print("local initialization AST: {}\n", ast.get_errors_str());
            return false;
        }
        for (bool optimized : {false, true})
        {
            CifaBytecode locals;
            locals.set_output_error(false);
            locals.set_optimization_enabled(optimized);
            if (!locals.compile_script(local_initialization))
            {
                std::print("local compile optimized={}: {} {}\n", optimized, locals.get_errors_str(), locals.get_translation_error());
                return false;
            }
            for (int iteration = 0; iteration < 2; ++iteration)
            {
                const auto actual = locals.run();
                if (!actual.isNumber() || actual.toInt64() != expected.toInt64() || locals.has_runtime_error())
                {
                    std::print("local run optimized={} iteration={} result={}: {}\n",
                        optimized, iteration, actual.toString(), locals.get_errors_str());
                    return false;
                }
            }
        }

        CifaBytecode math;
        const std::string compound_targets = R"script(
            shared_counter = 3;
            int items[1]; items[0] = 4;
            struct Counter { int value; };
            Counter counter; counter.value = 5;
            first = shared_counter++;
            second = items[0]++;
            third = (counter.value)++;
            shared_counter *= 2;
            items[0] += 7;
            counter.value -= 2;
            return first * 100000 + second * 10000 + third * 1000
                + shared_counter * 100 + items[0] * 10 + counter.value;
        )script";
        Cifa compound_ast;
        compound_ast.set_output_error(false);
        const auto compound_expected = compound_ast.run_script(compound_targets);
        if (compound_ast.has_runtime_error() || !compound_expected.isNumber() || compound_expected.toInt64() != 345924)
        {
            std::print("compound AST result={}: {}\n", compound_expected.toString(), compound_ast.get_errors_str());
            return false;
        }
        for (bool optimized : {false, true})
        {
            CifaBytecode compound_vm;
            compound_vm.set_output_error(false);
            compound_vm.set_optimization_enabled(optimized);
            if (!compound_vm.compile_script(compound_targets))
            {
                std::print("compound compile optimized={}: {} {}\n", optimized, compound_vm.get_errors_str(), compound_vm.get_translation_error());
                return false;
            }
            for (size_t repeat = 0; repeat < 3; ++repeat)
            {
                const auto actual = compound_vm.run();
                if (compound_vm.has_runtime_error() || !actual.isNumber() || actual.toInt64() != compound_expected.toInt64())
                {
                    std::print("compound VM optimized={} repeat={} result={}: {}\n", optimized, repeat, actual.toString(), compound_vm.get_errors_str());
                    return false;
                }
            }
        }
        const std::string nested_methods = R"script(
            values = {10, 20, 30};
            keys = {0, 1, 2};
            values.insert(keys.erase(1), values.contains(20) + 40);
            return values[0] * 1000 + values[1] * 100 + values[2] * 10 + values[3];
        )script";
        Cifa nested_method_ast;
        nested_method_ast.set_output_error(false);
        const auto nested_method_expected = nested_method_ast.run_script(nested_methods);
        if (nested_method_ast.has_runtime_error() || !nested_method_expected.isNumber()) return false;
        for (bool optimized : {false, true})
        {
            CifaBytecode nested_method_vm;
            nested_method_vm.set_output_error(false);
            nested_method_vm.set_optimization_enabled(optimized);
            if (!nested_method_vm.compile_script(nested_methods)) return false;
            for (size_t repeat = 0; repeat < 3; ++repeat)
            {
                const auto actual = nested_method_vm.run();
                if (nested_method_vm.has_runtime_error() || !actual.isNumber()
                    || actual.toInt64() != nested_method_expected.toInt64()) return false;
            }
        }
        const std::string nested_ranges = R"script(
            values = {1, 2, 3};
            total = 0;
            for (outer : values) {
                values[0] = 9;
                for (inner : values) {
                    total += outer * 10 + inner;
                    if (inner == 2) break;
                }
            }
            return total;
        )script";
        Cifa nested_range_ast;
        nested_range_ast.set_output_error(false);
        const auto nested_range_expected = nested_range_ast.run_script(nested_ranges);
        if (nested_range_ast.has_runtime_error() || !nested_range_expected.isNumber()) return false;
        for (bool optimized : {false, true})
        {
            CifaBytecode nested_range_vm;
            nested_range_vm.set_output_error(false);
            nested_range_vm.set_optimization_enabled(optimized);
            if (!nested_range_vm.compile_script(nested_ranges)) return false;
            for (size_t repeat = 0; repeat < 3; ++repeat)
            {
                const auto actual = nested_range_vm.run();
                if (nested_range_vm.has_runtime_error() || !actual.isNumber()
                    || actual.toInt64() != nested_range_expected.toInt64()) return false;
            }
        }
        math.set_output_error(false);
        math.set_optimization_enabled(true);
        if (!math.compile_script("return sqrt(9) + pow(2, 3) + floor(1.9) + fmod(7, 4);")) return false;
        if (math.run().toDouble() != 15.0 || math.has_runtime_error()) return false;

        CifaBytecode override_math;
        override_math.set_output_error(false);
        override_math.set_optimization_enabled(true);
        override_math.register_function("floor", [](ObjectVector&) { return Object(42); });
        return override_math.compile_script("return floor(1.9);")
            && override_math.run().toInt64() == 42 && !override_math.has_runtime_error();
    }
};
}

static double template_square(double x)
{
    return x * x;
}

static double template_add(double a, double b)
{
    return a + b;
}

static int template_trunc(double x)
{
    return int(x);
}

static void template_set_flag(Object flag)
{
    (void)flag;
}

static double template_menu(double x, double y, Object choices, double count)
{
    return x + y + count + (choices.hasValue() ? 0.0 : 1.0);
}

template <typename Backend>
struct BackendTests
{
#define Cifa Backend
bool register_function_test()
{
    Cifa c1;
    c1.register_function("sin", [](ObjectVector& d)
        {
            return sin(d[0]);
        });

    std::string script_code = R"(
    double PI = 3.141592653589793238462643383279;
    double return_val = 0;
    return_val += sin(0);
    return_val += sin(PI * 0.5);
    return_val += sin(PI);
    return return_val;
    )";

    auto o = c1.run_script(script_code);
    if (o.hasValue() && o.isNumber() && o.isType<double>())
    {
        return (std::fabs(o.ref<double>() - 1.0) <= std::numeric_limits<double>::epsilon());
    }
    else
    {
        return false;
    }
}

bool register_function_template_test()
{
    Cifa c;
    c.register_function("square", template_square);
    c.register_function("add", template_add);
    c.register_function("trunc", template_trunc);
    c.register_function("set_flag", template_set_flag);

    auto o = c.run_script(R"(
        set_flag(1);
        return square(3) + add(2, 4) + trunc(1.8);
    )");
    return o.isNumber() && o.toDouble() == 16.0;
}

bool registration_name_validation_test()
{
    Cifa c;
    c.set_output_error(false);
    int context = 0;
    if (!c.register_function("valid_function", template_square)
        || !c.register_parameter("valid_parameter", 1)
        || !c.register_vector("valid_vector", std::vector<int>{ 1, 2 })
        || !c.register_user_data("valid_context", &context))
    {
        return false;
    }

    Cifa invalid;
    invalid.set_output_error(false);
    const bool template_registration_failed = !invalid.register_function("1bad", template_square)
        && invalid.has_runtime_error()
        && invalid.get_runtime_error().find("invalid registration name '1bad'") != std::string::npos;

    Cifa default_output;
    const bool standard_registration_failed = !default_output.register_parameter("bad-key", 1)
        && default_output.has_runtime_error()
        && default_output.get_runtime_error().find("invalid registration name 'bad-key'") != std::string::npos;

    return Cifa::is_valid_key("valid_key")
        && Cifa::is_valid_key("_value2")
        && !Cifa::is_valid_key("1bad")
        && !Cifa::is_valid_key("bad-key")
        && !Cifa::is_valid_key("return")
        && Cifa::revise_key("1bad-key") == "_bad_key"
        && Cifa::revise_key("return") == "return_"
        && Cifa::revise_key("") == "_"
        && template_registration_failed
        && standard_registration_failed;
}

bool exit_function_test()
{
    {
        Cifa c;
        c.run_script("value = 1; exit(); value = 2;");
        auto result = c.run_script("return value;");
        if (!result.isNumber() || result.toDouble() != 1.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        c.run_script("count = 0; running = 1; while (running) { count++; exit(); count++; }");
        auto result = c.run_script("return count;");
        if (!result.isNumber() || result.toDouble() != 1.0)
        {
            return false;
        }
    }
    return true;
}

bool runtime_error_abort_test()
{
    const std::vector<std::string> scripts = {
        "sum = 0; for (int i = 0; i < 10; i++) { sum += missing_value(i); } return sum;",
        "for (int i = !missing_value(); i < 10; i++) { touch(); }",
        "for (int i = 0; missing_value(); i++) { touch(); }",
        "for (int i = 0; i < 10; i += !missing_value()) { }",
        "while (missing_value()) { touch(); }",
        "int i = 0; while (i < 10) { sum += !missing_value(); i++; }",
        "do { sum += !missing_value(); } while (sum < 10);",
        "do { } while (missing_value());",
        "for (item : missing_value()) { touch(); }",
        "values = {1, 2}; for (item : values) { sum += !missing_value(); touch(); }",
        "bad() { sum += !missing_value(); touch(); } bad();",
        "touch(!missing_value());",
        "sum = !missing_value();",
        "run_string(\"bad = {1}; return !bad;\");",
        "for (int i = 0; i < 10; i++) { run_string(\"bad = {1}; return !bad;\"); touch(); }",
        "int i = 0; while (i < 10) { run_string(\"bad = {1}; return !bad;\"); i++; touch(); }",
        "do { run_string(\"bad = {1}; return !bad;\"); touch(); } while (1);",
        "for (; run_string(\"bad = {1}; return !bad;\");) { touch(); }",
        "while (run_string(\"bad = {1}; return !bad;\")) { touch(); }",
        "do {} while (run_string(\"bad = {1}; return !bad;\"));",
        "convert({1});"
    };
    for (const auto& script : scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        int calls = 0;
        interpreter.register_function("touch", [&calls](ObjectVector&) -> Object
            {
                ++calls;
                return 0;
            });
        interpreter.register_function("missing_value", [](ObjectVector&) -> Object { return Object(); });
        interpreter.register_function("convert", [&calls, &interpreter](ObjectVector& arguments) -> Object
            {
                auto value = arguments[0].toDouble();
            if (interpreter.has_runtime_error()) { return Object(); }
                ++calls;
                return value;
            });
        auto result = interpreter.run_script("sum = 7; " + script + " touch();");
        if (result.getSpecialType() != "Error" || !interpreter.has_runtime_error() || calls != 0)
        {
            std::println(stderr, "Runtime abort failed: {} (calls={}, result={})\n{}{}",
                script, calls, result.getSpecialType(), interpreter.get_errors_str(), interpreter.get_runtime_error());
            return false;
        }
        if (script.find("run_string(") != std::string::npos && interpreter.is_exit_requested())
        {
            std::println(stderr, "Nested exit flag leaked: {}", script);
            return false;
        }
        result = interpreter.run_script("touch(); return 42;");
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != 42 || calls != 1)
        {
            return false;
        }
    }

    Cifa interpreter;
    interpreter.set_output_error(false);
    auto exit_result = interpreter.run_script(
        "total = 0; for (int i = 0; i < 3; i++) { run_string(\"exit();\"); total++; } return total;");
    if (interpreter.has_error() || interpreter.has_runtime_error() || interpreter.is_exit_requested()
        || !exit_result.isNumber() || exit_result.toInt() != 3)
    {
        return false;
    }
    for (int attempt = 0; attempt < 2; ++attempt)
    {
        auto result = interpreter.run_script("stored = 7; bad = {1}; !bad; stored = 9;");
        if (result.getSpecialType() != "Error" || !interpreter.has_runtime_error()
            || interpreter.get_runtime_error().find("type conversion failed") == std::string::npos)
        {
            return false;
        }
        result = interpreter.run_script("return stored;");
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != 7)
        {
            return false;
        }
    }
    return true;
}

bool object_conversion_fallback_test()
{
    Object value(std::string("not a number"));
    auto& invalid = value.ref<ObjectMap>();
    if (!invalid.empty())
    {
        return false;
    }
    invalid["discarded"] = 1;
    const Object& constant = value;
    return constant.ref<ObjectMap>().empty() && value.toString() == "not a number";
}

bool typed_function_argument_error_test()
{
    Cifa c;
    c.set_output_error(false);
    c.register_function("menu", template_menu);
    auto o = c.run_script("strs = {1, 2}; menu(85, 100, strs, strs);");
    return o.getSpecialType() == "Error"
        && c.get_runtime_error().find("variable 'strs'") != std::string::npos
        && c.get_runtime_error().find("to double") != std::string::npos;
}

bool object_vector_argument_error_test()
{
    const auto expect_conversion_error = [](const typename Backend::func_type& menu, const std::string& target_type)
        {
            Cifa c;
            c.set_output_error(false);
            c.register_function("menu", menu);
            auto result = c.run_script("strs = {1, 2}; menu(85, 100, strs, strs);");
            return result.getSpecialType() == "Error"
                && c.get_runtime_error().find("variable 'strs'") != std::string::npos
                && c.get_runtime_error().find(target_type) != std::string::npos;
        };

    return expect_conversion_error([](ObjectVector& args) -> Object
        {
            return Object(args[3].toDouble());
        }, "to double")
        && expect_conversion_error([](ObjectVector& args) -> Object
        {
            Object value = args[3];
            return Object(value.toDouble());
        }, "to double")
        && expect_conversion_error([](ObjectVector& args) -> Object
        {
            return Object(args[3].toString());
        }, "to string")
        && expect_conversion_error([](ObjectVector& args) -> Object
        {
            return Object(args[3].ref<ObjectMap>().size());
        }, typeid(ObjectMap).name());
}

bool builtin_math_function_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        double total = 0;
        total += cbrt(27);
        total += log2(8);
        total += atan2(0, -1) > 3;
        total += hypot(3, 4);
        total += fmod(7, 4);
        total += remainder(7, 4);
        total += trunc(1.8);
        total += copysign(2, -1);
        total += fdim(5, 3);
        total += fmax(2, 5);
        total += fmin(2, 5);
        return total;
    )");
    return o.isNumber() && std::fabs(o.toDouble() - 22.0) < 1e-9;
}

bool builtin_type_function_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int empty_value;
        auto pending_value;
        arr = {1, 2};
        m["x"] = 1;
        return type(empty_value) == "int"
            && type(pending_value) == "empty"
            && type(1) == "int"
            && type(1.0f) == "double"
            && type(1.0) == "double"
            && type(true) == "bool"
            && type("abc") == "string"
            && type(arr) == "array"
            && type(m) == "map";
    )");
    return o.isNumber() && o.toDouble() == 1.0;
}

bool loop_math_test()
{
    Cifa c1;
    std::string script_code = R"(
    int i;
    double sum = 0.0, product = 1.0, division = 100.0, difference = 50.0;
    double total_result = 0.0;
    for (i = 1; i <= 5; i++) {
        sum += i;
    }
    while (i <= 6) {
        product *= i;
        i++;
    }
    do {
        difference -= i;
        i++;
    } while (difference > 0);
    division /= 5;
    total_result = sum + product + division + difference;
    return total_result;
    )";

    auto o = c1.run_script(script_code);
    if (o.hasValue() && o.isNumber() && o.isType<double>())
    {
        return (std::fabs(o.ref<double>() - 34) <= std::numeric_limits<double>::epsilon());
    }
    else
    {
        return false;
    }
}

bool loop_control_test()
{
    Cifa c;
    std::string script = R"(
        int sum = 0;
        for (int i = 0; i < 10; i++) {
            if (i % 2 == 0) continue; // 跳过偶数
            if (i > 7) break;         // 遇到 9 跳出
            sum += i;
        }
        return sum; // 1 + 3 + 5 + 7 = 16
    )";
    auto o = c.run_script(script);
    return o.toInt() == 16;
}

bool control_state_test()
{
    Cifa c;
    c.set_output_error(false);
    auto result = c.run_script(R"(
        int sum = 0;
        for (int i = 0; i < 5; i++) {
            switch (i) {
                case 1: continue;
                case 3: break;
                default: sum += i;
            }
            sum += 10;
        }
        {
            goto done;
            sum = 999;
        }
    done:
        return sum;
    )");
    if (c.has_error() || c.has_runtime_error() || result.toInt() != 46) return false;

    const auto rejects = [](const std::string& script, const std::string& message)
        {
            Cifa invalid;
            invalid.set_output_error(false);
            invalid.run_script(script);
            const auto error = invalid.get_errors_str();
            return invalid.has_error() && error.find(message) != std::string::npos
                && error.find(script) != std::string::npos && error.find('^') != std::string::npos;
        };
    if (!rejects("break;", "break statement is not within a loop or switch")
        || !rejects("continue;", "continue statement is not within a loop")
        || !rejects("switch (1) { case 1: continue; }", "continue statement is not within a loop"))
    {
        return false;
    }

    Cifa switch_break;
    switch_break.set_output_error(false);
    const auto switch_result = switch_break.run_script("int value = 1; switch (value) { case 1: value = 7; break; default: value = 9; } return value;");
    return !switch_break.has_error() && !switch_break.has_runtime_error()
        && switch_result.isNumber() && switch_result.toInt() == 7;
}

bool ternary_operator_test()
{    // 测试嵌套三目运算
    Cifa c;
    std::string script = R"(
        int a = 1, b = 0;
        return a > b ? (b > a ? 10 : 20) : 30;
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 20;
}

bool logical_short_circuit_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int a = 0, b = 0, c = 0, d = 0;
        int first = (0 && a++) || (1 && (b++ == 0)) || c++;
        int second = (1 || d++) && (0 || (++d == 1));
        return a + b * 10 + c * 100 + d * 1000 + first * 10000 + second * 100000;
    )");
    return o.isNumber() && o.toInt() == 111010;
}

bool numeric_literal_radix_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        return 42 + 1.5e2 + 0xFF + 0X10 + 0b1010 + 0B11 + 077;
    )");
    return o.isNumber() && o.toInt() == 539;
}

bool switch_case_test()
{    // Switch-Case 完备性测试
    Cifa c;
    std::string script = R"(
        int x = 2;
        int res = 0;
        switch(x) {
            case 1: res = 10; break;
            case 2: res = 20; // 故意不写break看看？
            case 3: res = 30; break;
            default: res = 40;
        }
        return res;
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 30;
}

bool recursion_test()
{    // 递归函数测试
    Cifa c;
    std::string script = R"(
        double factorial(double n) {
            if (n <= 1) return 1;
            return n * factorial(n - 1);
        }
        return factorial(5);
    )";
    auto o = c.run_script(script);
    return o.hasValue() && std::fabs(o.toDouble() - 120.0) < 1e-9;
}

bool script_void_function_test()
{
    Cifa c;
    const std::string script = R"(
        total = 0;
        empty();
        increment();
        add(4);
        void empty() {}
        void increment() { total += 1; }
        void add(int amount) { total += amount; }
        return total;
    )";
    auto result = c.run_script(script);
    if (c.has_error() || c.has_runtime_error() || !result.isNumber() || result.toInt() != 5)
    {
        return false;
    }

    for (int attempt = 0; attempt < 2; ++attempt)
    {
        result = c.run_script(script);
        if (c.has_error() || c.has_runtime_error() || !result.isNumber() || result.toInt() != 5)
        {
            return false;
        }
    }
    result = c.run_script("increment(); add(3); return total;");
    return !c.has_error() && !c.has_runtime_error() && result.isNumber() && result.toInt() == 9;
}

bool script_function_return_check_test()
{
    const std::vector<std::string> invalid_scripts = {
        R"(int sum = 0;
int myrandom(int a) {
}
for (int i = 0; i < 10; i++) {
    sum += myrandom(i);
}
return sum;)",
        "empty() {} empty() + 1;",
        "empty() {} value = empty(); return value + 1;",
        "empty() {} if (empty()) return 1;",
        "empty() {} while (empty()) {}",
        "empty() {} do {} while (empty());",
        "empty() {} for (int index = 0; empty(); index++) {}",
        "empty() {} for (item : empty()) {}",
        "empty() {} values = {1, 2}; return values[empty()];",
        "empty() {} values = {empty()}; return values[0] + 1;",
        "empty() {} return to_number(empty());",
        "empty() {} wrapper() { return empty(); } return wrapper() + 1;",
        "empty() { 42; } return empty() + 1;",
        "empty() { return; } return empty() + 1;",
        "empty() {} empty(value) { return value; } return empty() + 1;",
        "value(number) { if (number > 0) return number; } return value(-1) + 1;",
        "empty() {} addone(value) { return value + 1; } return addone(empty());"
    };
    for (const auto& script : invalid_scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_script(script);
        const auto error = interpreter.get_runtime_error();
        if (result.getSpecialType() != "Error" || !interpreter.has_runtime_error() || interpreter.has_error()
            || error.find("has no return value") == std::string::npos || error.find("^") == std::string::npos)
        {
            std::println(stderr, "NoValue use did not fail: {}\n{}", script, error);
            return false;
        }
        if (&script == &invalid_scripts.front())
        {
            const std::string header = "<script>:5, col 12: ";
            const std::string expected = header + "    sum += myrandom(i);\n"
                + std::string(header.size() + 11, ' ') + "^\n";
            if (error.find(expected) == std::string::npos)
            {
                std::print(stderr, "NoValue call position mismatch:\n{}Expected:\n{}", error, expected);
                return false;
            }
        }
    }

    for (const bool delayed : {false, true})
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        const std::string script = delayed
            ? "empty() {}\nsaved = empty();\nreturn abs(saved);"
            : "empty() {}\nreturn abs(empty());";
        auto result = interpreter.run_script(script);
        const auto error = interpreter.get_runtime_error();
        const std::string origin = delayed ? "<script>:2, col 9:" : "<script>:2, col 12:";
        const std::string source_line = delayed ? "saved = empty();" : "return abs(empty());";
        const std::string expected_source = "No return value originated at:\n" + origin + " " + source_line
            + "\n" + std::string(origin.size() + 1 + (delayed ? 8 : 11), ' ') + "^\n";
        const auto position = error.find(origin);
        if (result.getSpecialType() != "Error" || interpreter.has_error() || !interpreter.has_runtime_error()
            || error.find("function 'empty' has no return value") == std::string::npos || position == std::string::npos
            || error.find(expected_source) == std::string::npos
            || error.find("Call Stack (most recent call first):") == std::string::npos
            || error.find(origin, position + origin.size()) != std::string::npos
            || (delayed && error.find("<script>:3, col 12:") == std::string::npos))
        {
            std::println(stderr, "Invalid NoValue diagnostic frames:\n{}", error);
            return false;
        }
    }

    const std::vector<std::string> valid_scripts = {
        "empty() {} empty(); return 42;",
        "void empty() { return; } empty(); return 42;",
        "empty() {} if (true) empty(); else empty(); return 42;",
        "empty() {} for (int index = 0; index < 2; index++) empty(); return 42;",
        "empty() {} for (empty(); 0; empty()) {} return 42;",
        "empty() {} true ? empty() : empty(); return 42;",
        "empty() {} (empty()); return 42;",
        "empty() {} empty(), empty(); return 42;",
        "empty() {} value = empty(); return 42;",
        "empty() {} values = {empty()}; return 42;",
        "empty() {} ignore(value) { return 42; } return ignore(empty());",
        "empty() {} if (0) return empty() + 1; return 42;",
        "empty() {} 0 && empty(); 1 || empty(); return 42;",
        "empty() {} int i = 0; for (empty(); i < 2; empty()) { i++; } return 42;",
        "value() {} value(number) { return number; } value(); return value(42);",
        "return value(42); value(number) { return number; }",
        "void value() { return 42; } return value();",
        "value(number) { if (number > 0) return number; } value(-1); return value(42);"
    };
    for (const auto& script : valid_scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_script(script);
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != 42)
        {
            std::println(stderr, "Valid return use rejected: {}\n{}{}", script,
                interpreter.get_errors_str(), interpreter.get_runtime_error());
            return false;
        }
    }

    const std::vector<std::string> no_value_scripts = {
        "empty() {} return empty();",
        "empty() { 42; } return empty();",
        "empty() { return; } return empty();",
        "empty() {} value = empty(); return value;",
        "empty() {} values = {empty()}; return values[0];",
        "empty() {} wrapper() { return empty(); } return wrapper();",
        "inner() { return 42; } outer() { inner(); } return outer();",
        "empty() {} return true ? empty() : 1;",
        "empty() {} empty(value) { return value; } return empty();",
        "value(number) { if (number > 0) return number; } return value(-1);"
    };
    for (const auto& script : no_value_scripts)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_script(script);
        if (interpreter.has_error() || interpreter.has_runtime_error() || result.getSpecialType() != "NoValue"
            || result.isNumber() || result.isType<std::string>())
        {
            std::println(stderr, "Missing NoValue result: {}\n{}{}", script,
                interpreter.get_errors_str(), interpreter.get_runtime_error());
            return false;
        }
    }

    Cifa interpreter;
    interpreter.set_output_error(false);
    interpreter.run_script("saved() {}");
    auto result = interpreter.run_script("return saved();");
    if (interpreter.has_error() || interpreter.has_runtime_error() || result.getSpecialType() != "NoValue")
    {
        return false;
    }
    result = interpreter.run_script("return type(saved());");
    if (interpreter.has_error() || interpreter.has_runtime_error()
        || !result.isType<std::string>() || result.toString() != "NoValue")
    {
        return false;
    }
    result = interpreter.run_script("value = saved(); return value + 1;");
    if (result.getSpecialType() != "Error"
        || interpreter.get_runtime_error().find("function 'saved' has no return value") == std::string::npos)
    {
        return false;
    }
    result = interpreter.run_script("saved(); return 42;");
    if (interpreter.has_error() || interpreter.has_runtime_error()
        || !result.isNumber() || result.toInt() != 42)
    {
        std::println(stderr, "Saved function call failed: {}{}", interpreter.get_errors_str(), interpreter.get_runtime_error());
        return false;
    }
    result = interpreter.run_script("saved() { return 42; } return saved();");
    if (interpreter.has_error() || interpreter.has_runtime_error() || !result.isNumber() || result.toInt() != 42)
    {
        return false;
    }
    interpreter.register_parameter("argument", 42);
    for (int argument : {42, -1, 7})
    {
        interpreter.register_parameter("argument", argument);
        result = interpreter.run_script("branch(number) { if (number > 0) return number; } return branch(argument);");
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || (argument > 0 && (!result.isNumber() || result.toInt() != argument))
            || (argument <= 0 && result.getSpecialType() != "NoValue"))
        {
            return false;
        }
    }
    result = interpreter.run_script("stop() { exit(); } return stop() + 1;");
    return !interpreter.has_error() && !interpreter.has_runtime_error() && interpreter.is_exit_requested();
}

bool script_function_argument_count_test()
{
    const auto expect_runtime_error = [](const char* label, const std::string& script, const std::string& expected)
        {
            Cifa c;
            c.set_output_error(false);
            const auto result = c.run_script(script);
            const std::string error = c.get_runtime_error();
            if (result.getSpecialType() != "Error" || error.find(expected) == std::string::npos)
            {
                std::println(stderr, "  {} failed: expected runtime error: {}\n    actual: {}", label, expected, error);
                return false;
            }
            std::println(stderr, "  {}: {}", label, error);
            return true;
        };
    {
        Cifa c;
        auto result = c.run_script(
            "describe() { return 0; } "
            "describe(value) { return value; } "
            "describe(left, right) { return left + right; } "
            "return describe() + describe(2) + describe(3, 4);");
        if (!result.isNumber() || result.toDouble() != 9.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script("same(value) { return value; } same(other) { return other + 10; } return same(1);");
        if (!result.isNumber() || result.toDouble() != 11.0 || c.has_error())
        {
            return false;
        }
        result = c.run_script("same(number) { return number + 100; } return same(1);");
        if (!result.isNumber() || result.toDouble() != 101.0 || c.has_error())
        {
            return false;
        }
    }
    return expect_runtime_error("missing overload", "add(a, b) { return a + b; } return add(2);",
        "no overload for 1 arguments; available: 2")
        && expect_runtime_error("extra argument overload", "add(a, b) { return a + b; } return add(2, 3, 4);",
            "no overload for 3 arguments; available: 2")
        && [&]()
        {
            Cifa c;
            c.set_output_error(false);
            c.run_script("sqrt(value) { return value; }");
            const std::string errors = c.get_errors_str();
            return c.has_error() && errors.find("script function 'sqrt' conflicts with a host function") != std::string::npos
                && errors.find("^") != std::string::npos;
        }();
}

bool script_function_global_scope_test()
{
    Cifa c;
    c.set_output_error(false);
    int captured_value = 0;
    c.register_function("capture", [&captured_value](ObjectVector& args) -> Object
        {
            captured_value = args.empty() ? 0 : args[0].toInt();
            return Object();
        });
    auto global_result = c.run_script(R"(
        b = 304;
        update_b() {
            capture(b);
            b = b + 1;
            return b;
        }
        result = update_b();
        return b * 1000 + result;
    )");
    if (!global_result.isNumber() || global_result.toInt() != 305305 || captured_value != 304)
    {
        return false;
    }

    auto next_script_result = c.run_script("return 7;");
    if (!next_script_result.isNumber() || next_script_result.toInt() != 7 || c.has_error())
    {
        return false;
    }

    auto persisted_function_result = c.run_script("b = 40; return update_b();");
    if (!persisted_function_result.isNumber() || persisted_function_result.toInt() != 41 || c.has_error())
    {
        return false;
    }

    c.run_script("broken() { return missing_function(); }");
    if (!c.has_error())
    {
        return false;
    }
    auto after_failed_definition = c.run_script("return 8;");
    if (!after_failed_definition.isNumber() || after_failed_definition.toInt() != 8 || c.has_error())
    {
        return false;
    }

    auto shadow_result = c.run_script(R"(
        b = 10;
        add_one(b) {
            b = b + 1;
            return b;
        }
        result = add_one(20);
        return b * 100 + result;
    )");
    return shadow_result.isNumber() && shadow_result.toInt() == 1021;
}

bool string_operation_test()
{    // 字符串操作与拼接测试
    Cifa c;
    std::string script = R"(
        string s1 = "Hello ";
        string s2 = "World";
        return s1 + s2;
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.isType<std::string>() && o.toString() == "Hello World";
}

bool string_compare_test()
{    // 字符串比较与跨行逻辑运算测试
    Cifa c;
    std::string script = R"(
        return "abc" == "abc"
            && "abc" != "def";
    )";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 1.0;
}

bool bitwise_operator_test()
{    // 位运算测试
    Cifa c;
    std::string script = R"(
        int a = 5;      // 0101
        int b = 3;      // 0011
        int res1 = a & b;  // 0001 (1)
        int res2 = a | b;  // 0111 (7)
        int res3 = a ^ b;  // 0110 (6)
        int res4 = a << 1; // 1010 (10)
        return res1 + res2 + res3 + res4; // 1 + 7 + 6 + 10 = 24
    )";
    auto o = c.run_script(script);
    return o.toInt() == 24;
}

bool scope_shadowing_test()
{    // 变量作用域遮蔽测试
    Cifa c;
    std::string script = R"(
        int x = 10;
        int inner = 0;
        {
            int x = 15 + 5;
            inner = x;
            if (x == 20) {
                int x = 30;
            }
        }
        return x * 100 + inner;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 1020;    // 融合表达式应写入内层声明，外部作用域不应受影响
}

bool complex_math_priority_test()
{    // 复杂算术优先级测试
    Cifa c;
    std::string script = R"(
        return 2 + 3 * 4 / (1 + 1) - 5 % 2; // 2 + 12 / 2 - 1 = 2 + 6 - 1 = 7
    )";
    auto o = c.run_script(script);
    return o.toInt() == 7;
}

bool same_precedence_left_assoc_test()
{    // 同优先级运算符应按 C++ 规则左结合，即 a/b*c == (a/b)*c，而非 a/(b*c)
    Cifa c;
    bool ok = true;
    // 除后乘：100/10*2 == 20
    ok = ok && c.run_script("return 100/10*2;").toInt() == 20;
    // 乘后除：100*10/2 == 500
    ok = ok && c.run_script("return 100*10/2;").toInt() == 500;
    // 除后模：100/10%3 == 1
    ok = ok && c.run_script("return 100/10%3;").toInt() == 1;
    // 模后除：10%4/2 == 1
    ok = ok && c.run_script("return 10%4/2;").toInt() == 1;
    // 减后加：10-3+1 == 8
    ok = ok && c.run_script("return 10-3+1;").toInt() == 8;
    // lW/2*lH/2 应等于 (lW/2*lH)/2，即 8/2*6/2==12
    ok = ok && c.run_script("double lW=8; double lH=6; return lW/2*lH/2;").toInt() == 12;
    return ok;
}

bool unary_minus_test()
{
    Cifa c;
    bool ok = true;
    ok = ok && c.run_script("return -5;").toDouble() == -5;           // 前置负号 + 常量
    ok = ok && c.run_script("return -(2+3);").toDouble() == -5;       // 前置负号 + 括号表达式
    ok = ok && c.run_script("return -(-3);").toDouble() == 3;         // 双重前置负号
    ok = ok && c.run_script("return 1 - -2;").toDouble() == 3;        // 二元减 + 前置负号
    ok = ok && c.run_script("return 10 - 3 - 2;").toDouble() == 5;    // 二元减左结合
    ok = ok && c.run_script("return -3 + 5;").toDouble() == 2;        // 前置负号 + 加法
    ok = ok && c.run_script("return 2 * -3;").toDouble() == -6;       // 乘以前置负号
    ok = ok && c.run_script("x = 7; return x * -1;").toDouble() == -7; // 变量乘以前置负号
    ok = ok && c.run_script("x = 7; x = x * -1; return x;").toDouble() == -7; // 赋值语境中的前置负号
    ok = ok && c.run_script("x = 7; return x * -1 + x * -2;").toDouble() == -21; // 连续乘法项
    ok = ok && c.run_script("return -(2*3);").toDouble() == -6;       // 前置负号 + 乘法括号
    ok = ok && c.run_script("return -2 + -3;").toDouble() == -5;      // 两个前置负号相加
    ok = ok && c.run_script("return +5;").toDouble() == 5;            // 前置正号 + 常量
    ok = ok && c.run_script("return +(2+3);").toDouble() == 5;        // 前置正号 + 括号表达式
    ok = ok && c.run_script("return 2 * +3;").toDouble() == 6;        // 乘以前置正号
    ok = ok && c.run_script("return 1 + +2;").toDouble() == 3;        // 二元加 + 前置正号
    return ok;
}

bool array_access_test()
{    // 数组/集合模拟测试 (假设Cifa支持类似[]的操作)
    Cifa c;
    std::string script = R"(
        //int arr[3];
        arr[0] = 10;
        {arr[1] = 20;}
        arr[2] = arr[0] + arr[1];
        return arr[2];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 30;
}

bool array_literal_assignment_test()
{    // 数组字面量赋值测试
    Cifa c;
    std::string script = R"(
        array = {1,2,3,4,5};
        return array[0] + array[4];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 6;
}

bool size_of_array_test()
{    // 数组大小测试
    Cifa c;
    std::string script = R"(
        array = {1,2,3,4,5};
        return size(array);
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toInt() == 5;
}

bool register_vector_test()
{
    std::vector<double> v = { 1.2, 1.45, 77.3 };
    Cifa c;
    c.register_vector("v", v);
    std::string script = R"(
        return v[0] + v[1] + v[2];
    )";
    auto o = c.run_script(script);
    return o.hasValue() && o.toDouble() == std::accumulate(v.begin(), v.end(), 0.0);
}

bool register_map_test()
{
    Cifa c;
    c.register_parameter("cfg", std::map<std::string, double>{ { "width", 640 }, { "height", 480 } });
    // m["key"] syntax
    {
        auto o = c.run_script(R"( return cfg["width"] * cfg["height"]; )");
        if (!o.hasValue() || o.toDouble() != 640 * 480)
        {
            return false;
        }
    }
    // m::key syntax
    {
        auto o = c.run_script(R"( return cfg::width + cfg::height; )");
        if (!o.hasValue() || o.toDouble() != 640 + 480)
        {
            return false;
        }
    }
    return true;
}

bool type_promotion_test()
{
    // int/int 保持整数除法，混合 double 后提升为 double。
    Cifa c;
    std::string script = R"(
        int a = 5;
        int b = 2;
        double res = floor(a / b);       // 整数除法，结果可能是 2.0
        double res2 = a / 2.0;    // 提升为浮点，结果应该是 2.5
        return res + res2;        // 4.5
    )";
    auto o = c.run_script(script);
    return std::fabs(o.toDouble() - 4.5) < 1e-9;
}

bool typed_numeric_storage_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int i = 1.9;
        float f = 1.9;
        double d = 1.9f;
        bool t = 0.5;
        bool u = 0.0;
        return i == 1
            && type(i) == "int"
            && type(f) == "double"
            && type(d) == "double"
            && type(t) == "bool"
            && type(abs(-3)) == "int"
            && type(max(1, 2)) == "int"
            && type(max(1, 2.0)) == "double"
            && t && !u;
    )");
    if (!o.hasValue() || !o.toBool())
    {
        std::println(stderr, "typed array and struct: result={}, error={}", o.toString(), c.get_runtime_error());
        return false;
    }
    return true;
}

bool auto_type_inference_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        auto i = 1;
        auto f = 1.5f;
        auto d = 1.5;
        auto b = true;
        int first = 1, second = 2.9;
        auto values = {1.5, 2.5};
        auto last_type = "";
        auto sum = 0.0;
        auto pending;
        pending = 2.5;
        int source = 7;
        untyped = source;
        untyped = "changed";
        for (auto value : values) {
            last_type = type(value);
            sum += value;
        }
        string s = "abc";
        i = 3.9;
        b = 0.0;
        return type(i) == "int" && i == 3
            && type(f) == "double" && type(d) == "double"
            && type(b) == "bool" && !b
            && first == 1 && second == 2
            && last_type == "double" && sum == 4.0
            && type(pending) == "double" && pending == 2.5
            && untyped == "changed"
            && type(s) == "string" && s + "d" == "abcd";
    )");
    return o.hasValue() && o.toBool();
}

bool c_style_cast_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        double a = 1.34;
        int b = (int)a;
        int c = (int)-3.9;
        int e = (int)1.9 + 2;
        int g = (int)(1.9 + 2.1);
        float f = (float)a;
        double d = (double)3;
        bool t = (bool)2.5;
        bool u = (bool)0.0;
        float precision = 0.1;
        return b == 1 && c == -3 && e == 3 && g == 4 && t && !u
            && precision == 0.1
            && type(b) == "int" && type(f) == "double"
            && type(d) == "double" && type(t) == "bool";
    )");
    return o.hasValue() && o.toBool();
}

bool integer_arithmetic_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        return 5 / 2 == 2
            && 5 % 2 == 1
            && 5 / 2.0 == 2.5
            && 1.0f / 2.0f == 0.5f
            && type(5 / 2) == "int"
            && type(5 / 2.0) == "double"
            && type(1.0f / 2.0f) == "double";
    )");
    return o.hasValue() && o.toBool();
}

bool typed_function_conversion_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int truncate(double x) { return x; }
        double half(int x) { return x / 2; }
        int add_int(double a, int b) { return a + b; }
        return truncate(3.9) == 3
            && half(3) == 1.0
            && add_int(1.9, 2.9) == 3
            && type(truncate(3.9)) == "int"
            && type(half(3)) == "double";
    )");
    return o.hasValue() && o.toBool();
}

bool typed_array_and_struct_test()
{
    Cifa c;
    auto o = c.run_script(R"(
        int a[2];
        a[0] = 3.9;
        a[1] = -2.9;
        int matrix[2][2];
        matrix[0][0] = 3.9;
        struct S { int i; float f; bool b; };
        S s;
        s.i = 1.9;
        s.f = 1.9;
        s.b = 2;
        return a[0] == 3 && a[1] == -2
            && matrix[0][0] == 3
            && s.i == 1 && s.b
            && type(a[0]) == "int"
            && type(matrix[0][0]) == "int"
            && type(s) == "S"
            && type(s.i) == "int"
            && type(s.f) == "double"
            && type(s.b) == "bool";
    )");
    return o.hasValue() && o.toBool();
}

bool typed_conversion_error_test()
{
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("int value = \"not a number\";");
        if (!c.has_runtime_error() || c.get_runtime_error().find("cannot convert value to 'int'") == std::string::npos)
        {
            return false;
        }
    }
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("double value = (double)\"not a number\";");
        if (!c.has_runtime_error() || c.get_runtime_error().find("cannot convert value to 'double'") == std::string::npos)
        {
            return false;
        }
    }
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("return 1 / 0;");
        if (!c.has_runtime_error() || c.get_runtime_error().find("integer division by zero") == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

struct RegisteredTestValue
{
    int number = 7;
};

bool registered_type_binding_test()
{
    Cifa c;
    c.set_output_error(false);
    if (!c.register_type<RegisteredTestValue>("Box") || !c.register_type<std::int64_t>("Index")) { return false; }
    c.register_parameter("original", RegisteredTestValue{});
    c.register_function("inspect_box", [](ObjectVector& args) { return Object(args[0].to<RegisteredTestValue>().number); });
    const auto result = c.run_script(R"(
        Box copy = original;
        auto inferred = original;
        Box identity(Box value) { return value; }
        fixed(int value) { value = 3.9; return value; }
        loose(value) { value = 3.9; return value; }
        int source = 1;
        auto pending;
        empty_function() {}
        pending = empty_function();
        pending = 4;
        pending = 5.9;
        dynamic = source;
        dynamic = "changed";
        Index index = 3.9;
        return inspect_box(identity(copy)) == 7 && inspect_box(inferred) == 7
            && type(inferred) == "Box" && type(index) == "int" && index == 3
            && fixed(1) == 3 && loose(source) == 3.9 && pending == 5
            && dynamic == "changed";
    )");
    if (!result.toBool() || c.has_runtime_error())
    {
        std::println("registered type execution: {}", c.get_runtime_error());
        return false;
    }
    c.run_script("inferred = 1;");
    if (!c.has_runtime_error()) { return false; }
    Cifa named_type;
    named_type.set_output_error(false);
    if (!named_type.register_type<RegisteredTestValue>("dynamic")) { return false; }
    named_type.run_script("dynamic value = 1;");
    if (!named_type.has_runtime_error()
        || named_type.get_runtime_error().find("cannot convert value to 'dynamic'") == std::string::npos) { return false; }
    Cifa comma;
    auto comma_result = comma.run_script("count = 0; unused = (count = 1, count += 2); return count;");
    if (comma.has_runtime_error() || comma.has_error() || comma_result.toInt64() != 3) { return false; }
    Cifa invalid;
    invalid.set_output_error(false);
    return !invalid.register_type<int>("bad-name") && invalid.has_runtime_error();
}

bool int64_storage_test()
{
    Cifa c;
    c.register_parameter("wide", std::int64_t{9007199254740993LL});
    c.register_function("identity64", +[](std::int64_t value) { return value; });
    auto result = c.run_script(R"(
        auto exact = 9007199254740993;
        int largest = 9223372036854775807;
        int smallest = -largest - 1;
        return exact == wide && identity64(exact) == wide && exact - 9007199254740992 == 1
            && max(exact, exact - 1) == exact && min(exact, exact - 1) == exact - 1
            && largest + 1 == smallest && smallest % -1 == 0
            && (1 << 40) == 1099511627776 && (1099511627776 >> 40) == 1
            && sprintf("%lld", exact) == "9007199254740993"
            && format("{}", exact) == "9007199254740993"
            && type(1.0f) == "double";
    )");
    if (!result.toBool() || c.has_runtime_error() || !Object(1).isType<std::int64_t>()
        || !Object(1.0f).isType<double>()) { return false; }
    c.set_output_error(false);
    c.run_script("int overflow = 9223372036854775808.0;");
    if (!c.has_runtime_error()) { return false; }
    c.run_script("return 1 << 64;");
    return c.has_runtime_error();
}

bool custom_operator_dispatch_test()
{
    using CallbackList = std::vector<std::function<Object(const Object&, const Object&)>>;
    const std::pair<const char*, CallbackList DirectCifa::*> operations[] = {
        {"+", &DirectCifa::user_add}, {"-", &DirectCifa::user_sub}, {"*", &DirectCifa::user_mul}, {"/", &DirectCifa::user_div},
        {"%", &DirectCifa::user_mod}, {"&", &DirectCifa::user_bit_and}, {"|", &DirectCifa::user_bit_or}, {"^", &DirectCifa::user_bit_xor},
        {"<<", &DirectCifa::user_shift_left}, {">>", &DirectCifa::user_shift_right}, {"==", &DirectCifa::user_equal},
        {"!=", &DirectCifa::user_not_equal}, {"<", &DirectCifa::user_less}, {">", &DirectCifa::user_more},
        {"<=", &DirectCifa::user_less_equal}, {">=", &DirectCifa::user_more_equal}
    };
    for (const auto& [symbol, callbacks] : operations)
    {
        DirectCifa c;
        c.set_output_error(false);
        c.register_parameter("host", RegisteredTestValue{});
        int calls = 0;
        (c.*callbacks).push_back([](const Object&, const Object&) { return Object(); });
        (c.*callbacks).push_back([&calls](const Object&, const Object&) { ++calls; return Object(true); });
        for (const auto& operands : {std::pair{"host", "2"}, std::pair{"2", "host"}})
        {
            auto result = c.run_script(std::format("return {} {} {};", operands.first, symbol, operands.second));
            if (!result.isType<bool>() || !result.toBool() || c.has_runtime_error()) { return false; }
        }
        if (calls != 2) { return false; }
        c.run_script(std::format("empty_function() {{}} unused = empty_function() {} 1; return 7;", symbol));
        if (!c.has_runtime_error() || c.get_runtime_error().find("has no return value") == std::string::npos) { return false; }
    }
    return true;
}

bool empty_statement_test()
{
    Cifa c;
    std::string script = R"(
        int x = 10;;;  // 多重分号
        if (x > 5) {}
        else ;
        while(false){;}
        for(;false;);
        return x;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 10;
}

bool else_if_chain_test()
{
    Cifa c;
    const auto run_branch = [&c](int value)
        {
            return c.run_script(std::format(R"(
                int value = {};
                if (value == 3) {{ return 30; }}
                else if (value == 2) {{ return 20; }}
                else if (value == 1) {{ return 10; }}
                else {{ return 0; }}
            )", value));
        };

    return run_branch(3).toInt() == 30
        && run_branch(2).toInt() == 20
        && run_branch(1).toInt() == 10
        && run_branch(0).toInt() == 0;
}

bool multi_dimensional_array_test()
{
    Cifa c;
    std::string script = R"(
        grid = {{1, 2}, {3, 4}};
        grid[2][1] = 5;
        return grid[1][0] + grid[2][1];
    )";
    auto o = c.run_script(script);
    return o.toInt() == 8;
}

bool compound_assignment_test()
{
    Cifa c;
    std::string script = R"(
        int x = 10;
        x *= 2 + 3; // 应该是 10 * (2 + 3) = 50，而不是 10 * 2 + 3 = 23
        x %= 7;     // 50 % 7 = 1
        return x;
    )";
    auto o = c.run_script(script);
    return o.toInt() == 1;
}

bool c_string_library_test()
{
    Cifa c1;

    // 1. 注册 strlen: 返回字符串长度
    c1.register_function("strlen", [](ObjectVector& d) -> Object
        {
            if (d.empty() || !d[0].isType<std::string>())
            {
                return 0;
            }
            return (double)d[0].toString().length();
        });

    // 2. 注册 strcmp: 比较字符串
    c1.register_function("strcmp", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2)
            {
                return 0;
            }
            int res = d[0].toString().compare(d[1].toString());
            // 标准化为 C 风格的 -1, 0, 1
            return (double)((res > 0) - (res < 0));
        });

    // 3. 注册 strcat: 拼接字符串
    c1.register_function("strcat", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2)
            {
                return "";
            }
            return d[0].toString() + d[1].toString();
        });

    // 4. 注册 strcpy: 模拟赋值
    c1.register_function("strcpy", [](ObjectVector& d) -> Object
        {
            if (d.size() < 2)
            {
                return "";
            }
            return d[1];    // 将第二个参数赋值给第一个
        });

    std::string script_code = R"(
        string s1 = "Cifa";
        string s2 = "Language";
        // 测试 strlen
        int len = strlen(s1); // 4
        // 测试 strcmp
        int cmp_res = strcmp(s1, "Cifa"); // 0
        // 测试 strcat
        string combined = strcat(s1, s2); // "CifaLanguage"
        // 测试 strcpy 逻辑
        string target = "old";
        target = strcpy(target, "new");
        // 期望: 4 + 0 + 12 (combined长度) + 3 (new长度) = 19
        return len + cmp_res + strlen(combined) + strlen(target);
    )";

    auto o = c1.run_script(script_code);

    if (o.hasValue() && o.isNumber())
    {
        // 预期 4 + 0 + 12 + 3 = 19
        return o.toInt() == 19;
    }

    return false;
}

bool runtime_error_stack_test()
{    // 多层脚本函数调用中的运行时错误应传播到顶层。
    Cifa c;
    c.set_output_error(false);
    std::string script = R"(
        string bad = "abc";
        inner(value) { return sqrt(value); }
        middle(value) { return inner(value); }
        outer(value) { return middle(value); }
        return outer(bad);
    )";
    auto o = c.run_script(script);
    const std::string error = c.get_runtime_error();
    return o.getSpecialType() == "Error"
        && error.find("type conversion failed") != std::string::npos
        && error.find("Call Stack (most recent call first):") != std::string::npos
        && error.find("func inner()") != std::string::npos
        && error.find("func middle()") != std::string::npos
        && error.find("func outer()") != std::string::npos;
}

bool uninitialized_variable_runtime_test()
{
    Cifa c;
    auto o = c.run_script("double x; return x * 2;");
    return o.getSpecialType() == "Error"
        && c.get_runtime_error().find("variable 'x' has not been initialized") != std::string::npos;
}

bool nested_execution_state_test()
{
    Cifa c;
    c.set_output_error(false);
    auto success = c.run_script("shared = 10; run_string(\"shared += 1; return shared;\"); run_file(\"unit_test/test_data/nested_increment.cifa\"); return shared;");
    if (!success.isNumber() || success.toInt() != 21 || c.has_error())
    {
        return false;
    }

    c.run_script("run_string(\"return missing_nested_value;\"); return 1;");
    if (!c.has_error() || c.get_errors_str().find("missing_nested_value") == std::string::npos)
    {
        return false;
    }

    c.run_script("run_file(\"unit_test/test_data/not_present_nested.cifa\"); double outer_value; return outer_value;");
    return c.has_error()
        && c.get_errors_str().find("cannot open file") != std::string::npos
        && !c.get_runtime_error().empty()
        && c.get_runtime_error().find("double outer_value; return outer_value;") != std::string::npos;
}

bool nested_error_preservation_test()
{
    Cifa c;
    c.set_output_error(false);
    c.register_function("run_first", [&c](ObjectVector&) -> Object
        {
            return c.run_script("first_missing_function();");
        });
    c.register_function("run_second", [&c](ObjectVector&) -> Object
        {
            return c.run_script("second_missing_function();");
        });
    c.run_script("run_first(); run_second();");
    const std::string errors = c.get_errors_str();
    return c.get_errors().size() == 2
        && errors.find("first_missing_function") != std::string::npos
        && errors.find("second_missing_function") != std::string::npos;
}

    bool nested_static_error_source_test()
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("run_string(\"missing_nested_function();\");");
        const auto errors = c.get_errors();
        return errors.size() == 1
        && errors.front().message == "function 'missing_nested_function' is not defined"
        && errors.front().source_text == "missing_nested_function();"
        && c.get_errors_str().find("missing_nested_function();") != std::string::npos;
    }

bool mixed_array_literal_test()
{    // 混合类型数组字面量：数字、字符串混存
    Cifa c;
    std::string script = R"(
        arr = {1, "hello", 3.14, "world"};
        int i = 2;
        double n = arr[0];
        string s = arr[1];
        double f = arr[i];
        string s2 = arr[3];
        println(s + " " + s2, " ", to_string(f)); // 输出 "hello world 3.14"
        return n + f;  // 1 + 3.14 = 4.14
    )";
    auto o = c.run_script(script);
    return o.hasValue() && std::fabs(o.toDouble() - 4.14) < 1e-9;
}

// ---- 共用测试辅助函数 ----

// 断言脚本产生语法错误，且错误信息包含 keyword；同时验证输出带行号和 ^ 箭头
static bool expect_syntax_error(const std::string& label, const std::string& script, const std::string& keyword)
{
    Cifa c;
    c.set_output_error(false);
    c.run_script(script);
    std::string err = c.get_errors_str();
    std::println(stderr, "  [{}]:", label);
    if (err.find("Syntax Error:") == std::string::npos || err.find(keyword) == std::string::npos)
    {
        std::println(stderr, "    FAIL: expected keyword \"{}\" in error output", keyword);
        if (!err.empty()) { std::print(stderr, "    Got:\n{}", err); }
        else { std::println(stderr, "    (no error produced)"); }
        return false;
    }
    if (err.find("^") == std::string::npos)
    {
        std::println(stderr, "    FAIL: missing caret (^) in error output");
        return false;
    }
    std::print(stderr, "{}", err);
    return true;
}

// 断言脚本不产生任何语法错误
static bool expect_no_syntax_error(const std::string& label, const std::string& script)
{
    Cifa c;
    c.set_output_error(false);
    c.run_script(script);
    std::string err = c.get_errors_str();
    std::print(stderr, "  [{}]: ", label);
    if (!err.empty())
    {
        std::print(stderr, "FAIL unexpected error:\n{}", err);
        return false;
    }
    std::println(stderr, "OK");
    return true;
}

bool static_syntax_error_test()
{
    const auto& expect_error = expect_syntax_error;
    const auto& expect_no_error = expect_no_syntax_error;

    bool ok = true;

    // ==== 应触发静态错误的场景 ====

    // 1. 赋值右侧使用未初始化变量
    ok &= expect_error("uninitialized var in assign",
        R"(int y = undef;)",
        "not been initialized");

    // 3. 调用未定义的函数
    ok &= expect_error("undefined function",
        R"(return foo(1, 2);)",
        "not defined");

    // 4. 括号不匹配（右括号多余）
    ok &= expect_error("unpaired right paren",
        R"(int x = (1 + 2));)",
        "unpaired");

    // 5. 括号不匹配（左括号多余）
    ok &= expect_error("unpaired left paren",
        R"(int x = ((1 + 2);)",
        "unpaired");

    // 6. 赋值给常量
    ok &= expect_error("assign to constant",
        R"(123 = 5;)",
        "cannot be assigned");

    // 7. 赋值给字符串字面量
    ok &= expect_error("assign to string literal",
        "\"hello\" = 5;",
        "cannot be assigned");

    // 8. 独立的 else（没有对应的 if）
    ok &= expect_error("else without if",
        R"(else { int x = 1; })",
        "else has no if");

    // 9. 三元运算符缺少 :
    ok &= expect_error("ternary missing colon",
        R"(int x = 1; int y = x ? 10;)",
        "no :");

    // 10. 非法字符不能被静默忽略，避免 #strs 被解释为 strs
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("return menu(85, 100, strs, #strs);");
        std::string err = c.get_errors_str();
        if (err.find("unexpected character '#'") == std::string::npos
            || err.find("col 29") == std::string::npos)
        {
            std::print(stderr, "  FAIL [unexpected character]: expected '#' at column 29\n    Got:\n{}", err);
            ok = false;
        }
    }

    // 多行错误同时检查正文、源码和插入符。
    {
        Cifa c;
        c.set_output_error(false);
        c.run_script("int x = 10;\nint y = undef;\n");
        std::string err = c.get_errors_str();
        const std::string header = "  at <script>:2, col 9: ";
        const std::string expected = header + "int y = undef;\n"
            + std::string(header.size() + 8, ' ') + "^\n";
        if (!c.has_error() || err.find("not been initialized") == std::string::npos
            || err.find(expected) == std::string::npos)
        {
            std::print(stderr, "  FAIL [error line number]: expected '<script>:2' and 'undef' in error output\n    Got:\n{}", err);
            ok = false;
        }
    }

    // ==== 不应触发静态错误的场景 ====

    // 11. 正常脚本
    ok &= expect_no_error("valid script",
        R"(int x = 10; int y = x + 5; return y;)");

    // 12. 正常 for 循环
    ok &= expect_no_error("valid for loop",
        R"(int s = 0; for (int i = 0; i < 5; i++) { s += i; } return s;)");

    // 13. 正常函数调用
    ok &= expect_no_error("valid function call",
        R"(return abs(-5);)");

    // 14. 正常数组操作
    ok &= expect_no_error("valid array",
        R"(arr = {1,2,3}; return arr[0] + arr[2];)");

    // 15. 正常字符串 Map
    ok &= expect_no_error("valid map",
        R"(dict["k"] = 42; return dict["k"];)");

    // 16. 正常三元运算符
    ok &= expect_no_error("valid ternary",
        R"(int x = 1; return x ? 10 : 20;)");

    // 17. 正常自定义函数
    ok &= expect_no_error("valid user function",
        "myfun(i) { return i * i; }\nreturn myfun(5);");

    // 18. 正常嵌套块
    ok &= expect_no_error("valid nested block",
        R"(int x = 1; { int y = x + 1; x = y; } return x;)");

    // ==== 深层未初始化变量检测 ====

    // 19. 表达式中嵌套使用未初始化变量（加法右侧）
    ok &= expect_error("uninitialized var in expr",
        R"(int x = 1; int y = x + undef;)",
        "not been initialized");

    // 20. 函数调用参数中使用未初始化变量
    ok &= expect_error("uninitialized var in func arg",
        R"(return abs(undef);)",
        "not been initialized");

    // 21. return 语句中使用未初始化变量
    ok &= expect_error("uninitialized var in return",
        R"(return undef;)",
        "not been initialized");

    // 22. 条件表达式中使用未初始化变量
    ok &= expect_error("uninitialized var in condition",
        R"(if (undef) { int x = 1; })",
        "not been initialized");

    // 23. 复合表达式中使用未初始化变量
    ok &= expect_error("uninitialized var in complex expr",
        R"(int x = 1; int y = (x * 2) + undef;)",
        "not been initialized");

    // 24. 正常：已初始化变量在表达式中使用不应报错
    ok &= expect_no_error("valid var in expr",
        R"(int x = 1; int y = 2; int z = x + y; return z;)");

    // 25. 正常：数组下标中使用已初始化变量不应报错
    ok &= expect_no_error("valid var in subscript",
        R"(arr = {10, 20, 30}; int i = 1; return arr[i];)");

    // ==== 空条件检查 ====

    // 26. if 空条件
    ok &= expect_error("if empty condition",
        R"(if () { int x = 1; })",
        "empty condition");

    // 27. while 空条件
    ok &= expect_error("while empty condition",
        R"(while () { int x = 1; })",
        "empty condition");

    ok &= expect_no_error("while(1) with break",
        R"(while (1) { break; })");

    ok &= expect_no_error("while(true) with break",
        R"(while (true) { break; })");

    ok &= expect_no_error("for(;;) with break",
        R"(for (;;) { break; })");

    ok &= expect_no_error("for(;1;) with break",
        R"(int i = 0; for (; 1; i++) { break; })");

    ok &= expect_no_error("for(;true;) with return",
        R"(for (; true;) { return 7; })");

    ok &= expect_no_error("valid while loop",
        R"(int i = 0; while (i < 5) { i++; } return i;)");

    return ok;
}

bool string_key_map_test()
{    // 字符串下标（map 语义）测试
    Cifa c;
    std::string script = R"(
        dict["name"] = "Alice";
        dict["age"] = 30;
        dict["score"] = 95.5;
        string name = "name";
        string n = dict[name];
        string age = "age";
        double a = dict[age];
        string score = "score";
        double s = dict[score];
        println("Name: ", n, ", Age: ", a, ", Score: ", s);
        return a + s;  // 30 + 95.5 = 125.5
    )";
    auto o = c.run_script(script);
    if (!o.hasValue() || std::fabs(o.toDouble() - 125.5) > 1e-9)
    {
        return false;
    }

    // 访问不存在的 key 后尝试输出，应触发 runtime error
    Cifa c2;
    std::string script2 = R"(
        dict["name"] = "Alice";
        n1 = dict["name1"];
        println("Non-existent key: ", n1);
        return 0;
    )";
    auto o2 = c2.run_script(script2);
    return o2.getSpecialType() == "Error";
}

bool loop_and_recursion_execution_test()
{
    const std::pair<const char*, int> cases[] = {
        { "value = 0; while (1) { value++; if (value == 5) break; } return value;", 5 },
        { "while (true) { return 7; }", 7 },
        { "value = 0; for (;;) { value++; if (value == 5) break; } return value;", 5 },
        { "value = 0; for (; 1; value++) { if (value == 5) break; } return value;", 5 },
        { "for (; true;) { return 7; }", 7 },
        { "value = 0; while (value < 500) { value++; } return value;", 500 },
        { "total = 0; for (int index = 0; index < 500; index++) { total += index; } return total;", 124750 },
        { "value = 0; do { value++; } while (value < 500); return value;", 500 },
        { "values = {1, 2, 3, 4, 5}; total = 0; for (value : values) { total += value; } return total;", 15 },
        { "value = 0; again: value++; if (value < 500) goto again; return value;", 500 },
        { "sum_to(depth) { if (depth <= 0) return 0; return depth + sum_to(depth - 1); } return sum_to(4);", 10 },
    };
    for (const auto& [script, expected] : cases)
    {
        Cifa interpreter;
        const auto result = interpreter.run_script(script);
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toInt() != expected)
        {
            std::println(stderr, "  loop/recursion execution failed: {}", script);
            return false;
        }
    }
    return true;
}

bool array_methods_test()
{
    // empty array via {}
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {};
            a.push_back(10);
            a.push_back(20);
            a.push_back(30);
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 3)
        {
            std::println(stderr, "array push_back literal: {}", c1.get_runtime_error());
            return false;
        }
    }
    // empty array via int a[]
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            int a[];
            a.push_back(1);
            a.push_back(2);
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 2)
        {
            std::println(stderr, "array push_back typed: {}", c1.get_runtime_error());
            return false;
        }
    }
    // pop_back
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 2, 3, 4, 5};
            a.pop_back();
            a.pop_back();
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 3)
        {
            std::println(stderr, "array pop_back: {}", c1.get_runtime_error());
            return false;
        }
    }
    // insert
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 3, 4};
            a.insert(1, 2);
            return a[0] * 1000 + a[1] * 100 + a[2] * 10 + a[3];
        )");
        if (!o.hasValue() || o.toInt() != 1234)
        {
            std::println(stderr, "array insert: {}", c1.get_runtime_error());
            return false;
        }
    }
    // erase
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {10, 20, 30, 40};
            a.erase(1);
            return a[0] * 100 + a[1] * 10 + a[2];
        )");
        if (!o.hasValue() || o.toInt() != 1340)
        {
            std::println(stderr, "array erase: {}", c1.get_runtime_error());
            return false;
        }
    }
    // resize
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 2, 3};
            a.resize(5);
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 5)
        {
            std::println(stderr, "array resize: {}", c1.get_runtime_error());
            return false;
        }
    }
    // clear
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {1, 2, 3};
            a.clear();
            return size(a);
        )");
        if (!o.hasValue() || o.toInt() != 0)
        {
            std::println(stderr, "array clear: {}", c1.get_runtime_error());
            return false;
        }
    }
    // contains
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            a = {10, 20, 30};
            int r1 = a.contains(20);
            int r2 = a.contains(99);
            return r1 * 10 + r2;
        )");
        if (!o.hasValue() || o.toInt() != 10)
        {
            std::println(stderr, "array contains: {}", c1.get_runtime_error());
            return false;
        }
    }
    return true;
}

bool range_for_test()
{
    // 循环变量为值副本；修改不会回写数组元素。
    {
        Cifa c;
        auto o = c.run_script(R"(
            values = {1, 2, 3, 4};
            int sum = 0;
            for (int value : values) {
                value *= 10;
                if (value == 20) continue;
                if (value == 40) break;
                sum += value;
            }
            return sum + values[0] + values[1] + values[2] + values[3];
        )");
        if (!o.isNumber() || o.toDouble() != 50)
        {
            return false;
        }
    }
    // auto 形式与传统范围循环作用域。
    {
        Cifa c;
        auto o = c.run_script(R"(
            values = {2, 3, 5};
            int product = 1;
            for (auto value : values) { product *= value; }
            return product;
        )");
        if (!o.isNumber() || o.toDouble() != 30)
        {
            return false;
        }
    }
    return true;
}

bool goto_test()
{
    {
        Cifa c;
        auto result = c.run_script(R"(
            int value = 0;
        again:
            value += 1;
            if (value < 3) goto again;
            return value;
        )");
        if (!result.isNumber() || result.toDouble() != 3.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script(R"(
            int value = 0;
            if (value == 0) {
                value = 1;
            }
        label_after_if:
            value += 1;
            if (value < 3) { goto label_after_if; }
            return value;
        )");
        if (!result.isNumber() || result.toDouble() != 3.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script(R"(
            int value = 0;
            {
                value = 1;
                goto done;
            }
            value = 2;
        done:
            return value;
        )");
        if (!result.isNumber() || result.toDouble() != 1.0)
        {
            return false;
        }
    }
    {
        Cifa c;
        auto result = c.run_script(R"(
            count_to(limit) {
                int value = 0;
            again:
                value += 1;
                if (value < limit) goto again;
                return value;
            }
            return count_to(4);
        )");
        if (!result.isNumber() || result.toDouble() != 4.0)
        {
            return false;
        }
    }

    const auto expect_static_error = [](const std::string& script, const std::string& expected)
        {
            Cifa c;
            c.set_output_error(false);
            c.run_script(script);
            return c.has_error() && c.get_errors_str().find(expected) != std::string::npos;
        };
    return expect_static_error("goto missing;", "goto target 'missing' is not defined")
        && expect_static_error("first: first:", "duplicate label 'first'")
        && expect_static_error("goto inside; { inside: return 1; }", "goto 'inside' jumps into a nested or sibling block")
        && expect_static_error("{ left: goto right; } { right: return 1; }", "goto 'right' jumps into a nested or sibling block");
}

bool map_methods_test()
{
    // contains
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["a"] = 1;
            m["b"] = 2;
            int r1 = m.contains("a");
            int r2 = m.contains("z");
            return r1 * 10 + r2;
        )");
        if (!o.hasValue() || o.toInt() != 10)
        {
            return false;
        }
    }
    // erase
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["x"] = 10;
            m["y"] = 20;
            m["z"] = 30;
            m.erase("y");
            return size(m) * 100 + m.contains("x") * 10 + m.contains("y");
        )");
        if (!o.hasValue() || o.toInt() != 210)
        {
            return false;
        }
    }
    // clear
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["a"] = 1;
            m["b"] = 2;
            m.clear();
            return size(m);
        )");
        if (!o.hasValue() || o.toInt() != 0)
        {
            return false;
        }
    }
    // keys
    {
        Cifa c1;
        auto o = c1.run_script(R"(
            m["alpha"] = 1;
            m["beta"] = 2;
            k = m.keys();
            return size(k);
        )");
        if (!o.hasValue() || o.toInt() != 2)
        {
            return false;
        }
    }
    return true;
}

bool non_block_branch_declaration_test()
{
    // 委托到文件级辅助函数，硬编码搜索关键词 "non-block"
    auto expect_error = [](const std::string& label, const std::string& script) -> bool
    {
        return expect_syntax_error(label, script, "non-block");
    };
    const auto& expect_no_error = expect_no_syntax_error;

    bool ok = true;

    // ==== 应报错：非封闭体内定义变量（显式类型前缀）====

    // if 体内声明并初始化
    ok &= expect_error("if non-block init decl",
        R"(int a = 1; if (a) int x = 0;)");

    // if 体内仅声明（无初始化）
    ok &= expect_error("if non-block bare decl",
        R"(int a = 1; if (a) int x;)");

    // else 体内声明
    ok &= expect_error("else non-block decl",
        R"(int a = 1; if (a) a = 0; else int x = 1;)");

    // while 体内声明
    ok &= expect_error("while non-block decl",
        R"(int i = 5; while (i > 0) int x = i;)");

    // for 体内声明
    ok &= expect_error("for non-block decl",
        R"(for (int i = 0; i < 5; i++) int x = i;)");

    // ==== 应报错：非封闭体内引入新变量（无类型前缀，变量不在表中）====

    // if 体内引入全新变量（无 int 前缀，赋值形式）
    ok &= expect_error("if non-block new var no type",
        R"(int a = 1; if (a) newvar = 0;)");

    // if 体内裸引用未声明变量（x; 形式）
    ok &= expect_error("if non-block bare ref undeclared",
        R"(int a = 1; if (a) newvar;)");

    // while 体内引入全新变量
    ok &= expect_error("while non-block new var no type",
        R"(int i = 5; while (i > 0) newvar = i;)");

    // while 体内裸引用未声明变量
    ok &= expect_error("while non-block bare ref undeclared",
        R"(int i = 5; while (i > 0) newvar;)");

    // for 体内引入全新变量
    ok &= expect_error("for non-block new var no type",
        R"(int s = 0; for (int i = 0; i < 5; i++) newvar = i;)");

    // for 体内裸引用未声明变量
    ok &= expect_error("for non-block bare ref undeclared",
        R"(for (int i = 0; i < 5; i++) newvar;)");

    // ==== 应报错：switch case 体内引入新变量 ====

    ok &= expect_error("switch case non-block new var",
        R"(int x = 1; switch(x) { case 1: newvar = 10; break; })");

    ok &= expect_error("switch case non-block typed decl",
        R"(int x = 1; switch(x) { case 1: int y = 10; break; })");

    // ==== 不应报错：花括号体内定义变量合法 ====

    // if 加花括号
    ok &= expect_no_error("if block decl ok",
        R"(int a = 1; if (a) { int x = 0; })");

    // else 加花括号
    ok &= expect_no_error("else block decl ok",
        R"(int a = 1; if (a) { a = 0; } else { int x = 1; })");

    // while 加花括号
    ok &= expect_no_error("while block decl ok",
        R"(int i = 0; while (i < 3) { int x = i; i++; })");

    // for 加花括号
    ok &= expect_no_error("for block decl ok",
        R"(int s = 0; for (int i = 0; i < 5; i++) { int x = i; s += x; } return s;)");

    // switch case 内用 {} 包裹合法
    ok &= expect_no_error("switch case block decl ok",
        R"(int x = 1; switch(x) { case 1: { int y = 10; } break; })");

    // ==== 不应报错：非封闭体内赋值/引用已有变量合法 ====

    ok &= expect_no_error("if non-block assign ok",
        R"(int x = 0; int a = 1; if (a) x = 1; return x;)");

    // if 体内裸引用已声明变量：合法
    ok &= expect_no_error("if non-block bare ref declared ok",
        R"(int x = 0; int a = 1; if (a) x; return x;)");

    ok &= expect_no_error("else non-block assign ok",
        R"(int x = 0; int a = 0; if (a) x = 1; else x = 2; return x;)");

    ok &= expect_no_error("while non-block assign ok",
        R"(int i = 0; while (i < 3) i++; return i;)");

    ok &= expect_no_error("for non-block assign ok",
        R"(int s = 0; for (int i = 0; i < 5; i++) s += i; return s;)");

    // switch case 内赋值已有变量合法
    ok &= expect_no_error("switch case assign existing ok",
        R"(int x = 1; int r = 0; switch(x) { case 1: r = 10; break; default: r = 20; } return r;)");

    // else if 链（仅2层，无悬空 else）不应误报非封闭声明错误
    ok &= expect_no_error("else if chain ok",
        R"(int a = 2; int r = 0; if (a == 1) r = 10; else if (a == 2) r = 20; return r;)");

    return ok;
}

bool struct_test()
{
    // 基本字段读写
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Point { int x; int y; };
            Point p;
            p.x = 10;
            p.y = 20;
            return p.x + p.y;
        )");
        if (!o.isNumber() || o.toDouble() != 30)
        {
            return false;
        }
    }
    // struct 多字段
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Vec { int x; int y; int z; };
            Vec v;
            v.x = 1; v.y = 2; v.z = 3;
            return v.x * 100 + v.y * 10 + v.z;
        )");
        if (!o.isNumber() || o.toDouble() != 123)
        {
            return false;
        }
    }
    // struct 字段复合赋值
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Counter { int n; };
            Counter cnt;
            cnt.n = 5;
            cnt.n += 3;
            return cnt.n;
        )");
        if (!o.isNumber() || o.toDouble() != 8)
        {
            return false;
        }
    }
    // 函数中使用 struct
    {
        Cifa c;
        auto o = c.run_script(R"(
            struct Rect { int w; int h; };
            area(r) { return r.w * r.h; }
            Rect r;
            r.w = 4; r.h = 5;
            return area(r);
        )");
        if (!o.isNumber() || o.toDouble() != 20)
        {
            return false;
        }
    }
    // 全局 struct 定义在执行后注册到 Cifa，可供后续脚本使用
    {
        Cifa c;
        c.run_script("struct Point { int x; int y; };");
        auto o = c.run_script("Point p; p.x = 3; p.y = 7; return p.x + p.y;");
        if (!o.isNumber() || o.toDouble() != 10)
        {
            return false;
        }
    }
    return true;
}

bool sprintf_format_test()
{
    // --- sprintf ---
    // %s 字符串
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("Hello %s!", "World");)");
        if (!o.isType<std::string>() || o.toString() != "Hello World!")
        {
            return false;
        }
    }
    // %d 整数和 %.2f 浮点
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%d + %.2f = %.2f", 3, 1.5, 4.5);)");
        if (!o.isType<std::string>() || o.toString() != "3 + 1.50 = 4.50")
        {
            return false;
        }
    }
    // 连续说明符和 %% 混用：%% 不消耗参数，其他说明符按顺序各消耗一个参数
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%s%d%.1f%%-%x", "A", 2, 3.5, 255);)");
        if (!o.isType<std::string>() || o.toString() != "A23.5%-ff")
        {
            return false;
        }
    }
    // %% 转义
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("100%%");)");
        if (!o.isType<std::string>() || o.toString() != "100%")
        {
            return false;
        }
    }
    // 奇数个 %：前 12 个组成 6 个 %% ，末尾不完整的 % 被忽略；与 MSVC/UCRT 当前行为一致
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("100%%%%%%%%%%%%%");)");
        if (!o.isType<std::string>() || o.toString() != "100%%%%%%")
        {
            return false;
        }
    }
    // %x 十六进制
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%x", 255);)");
        if (!o.isType<std::string>() || o.toString() != "ff")
        {
            return false;
        }
    }
    // %llu 多字母长度修饰符（用户显式写 ll，应正常工作）
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%llu", 12345);)");
        if (!o.isType<std::string>() || o.toString() != "12345")
        {
            return false;
        }
    }
    // %05.1f 宽度+精度（浮点，无长度修饰符）
    {
        Cifa c;
        auto o = c.run_script(R"(return sprintf("%08.2f", 3.14);)");
        if (!o.isType<std::string>() || o.toString() != "00003.14")
        {
            return false;
        }
    }
    // --- format ---
    // 自动 {}
    {
        Cifa c;
        auto o = c.run_script(R"(return format("Hello {}!", "World");)");
        if (!o.isType<std::string>() || o.toString() != "Hello World!")
        {
            return false;
        }
    }
    // 显式索引 {N}
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{1} and {0}", "B", "A");)");
        if (!o.isType<std::string>() || o.toString() != "A and B")
        {
            return false;
        }
    }
    // 整数数字不带小数点
    {
        Cifa c;
        auto o = c.run_script(R"(return format("x = {}", 42);)");
        if (!o.isType<std::string>() || o.toString() != "x = 42")
        {
            return false;
        }
    }
    // 浮点数字
    {
        Cifa c;
        auto o = c.run_script(R"(return format("pi = {}", 3.14);)");
        if (!o.isType<std::string>() || o.toString() != "pi = 3.14")
        {
            return false;
        }
    }
    // {{ }} 转义
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{{{}}}", "ok");)");
        if (!o.isType<std::string>() || o.toString() != "{ok}")
        {
            return false;
        }
    }
    // {:格式说明符} 自动索引
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:.2f}", 3.14159);)");
        if (!o.isType<std::string>() || o.toString() != "3.14")
        {
            return false;
        }
    }
    // {N:格式说明符} 显式索引
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{0:d} / {1:.1f}", 7, 3.0);)");
        if (!o.isType<std::string>() || o.toString() != "7 / 3.0")
        {
            return false;
        }
    }
    // {:s} 字符串格式
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:s}", "hi");)");
        if (!o.isType<std::string>() || o.toString() != "hi")
        {
            return false;
        }
    }
    // {:.2} 无类型尾缀 — 数字按 %g 保留2位有效数字
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:.2}", 3.14159);)");
        if (!o.isType<std::string>() || o.toString() != "3.1")
        {
            return false;
        }
    }
    // {:>8} 无类型尾缀 — 字符串按 %s 右对齐（snprintf %s 不支持对齐，直接透传）
    {
        Cifa c;
        auto o = c.run_script(R"(return format("{:5}", 42);)");
        // 宽度5，数字补 g → "   42" 或 "42" 均可，只要不崩溃且包含 "42"
        if (!o.isType<std::string>() || o.toString().find("42") == std::string::npos)
        {
            return false;
        }
    }
    return true;
}

bool include_file_cases_test()
{
    const std::pair<const char*, double> cases[] = {
        { "include_simple.cifa", 15 },
        { "include_multi.cifa", 17 },
        { "c.cifa", 201 },
        { "self.cifa", 1 },
        { "cycle_a.cifa", 1 },
        { "include_subdir.cifa", 42 },
        { "angle_bracket.cifa", 10 },
        { "include_in_function.cifa", 25 }
    };
    bool ok = true;
    for (const auto& [file, expected] : cases)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        auto result = interpreter.run_file(std::string("unit_test/test_data/") + file);
        if (interpreter.has_error() || interpreter.has_runtime_error()
            || !result.isNumber() || result.toDouble() != expected)
        {
            std::print(stderr, "Include file failed: {}\n{}{}", file,
                interpreter.get_errors_str(), interpreter.get_runtime_error());
            ok = false;
        }
    }
    return ok;
}

bool include_missing_file_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_file("unit_test/test_data/missing.cifa");
    return c.has_error();
}

bool include_with_parameters_test()
{
    Cifa c;
    c.register_parameter("base", Object(100.0));
    auto o = c.run_file("unit_test/test_data/with_params.cifa");
    return o.isNumber() && o.toDouble() == 110.0;
}

bool include_run_script_include_dir_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data" });
    std::string script = "#include \"simple.cifa\"\nint y = x + 5;\nreturn y;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 15.0;
}

bool include_run_script_default_dir_test()
{
    Cifa c;
    std::string script = "#include \"unit_test/test_data/simple.cifa\"\nreturn x + 5;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 15.0;
}

bool include_run_script_include_dir_multi_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data" });
    std::string script = "#include \"lib_math.cifa\"\nreturn square(3) + cube(2);\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 17.0;
}

bool include_run_script_include_dir_with_params_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data" });
    c.register_parameter("base", Object(100.0));
    std::string script = "#include \"simple.cifa\"\nreturn base + x;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 110.0;
}

bool include_multi_search_dirs_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data/missing_dir", "unit_test/test_data" });
    std::string script = "#include \"simple.cifa\"\nreturn x + 7;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 17.0;
}

bool include_absolute_path_test()
{
    Cifa c;
    c.set_include_dirs({ "unit_test/test_data/missing_dir" });
    std::filesystem::path path = std::filesystem::absolute("unit_test/test_data/simple.cifa");
    std::string path_str = path.generic_string();
    std::string script = "#include \"" + path_str + "\"\nreturn x + 8;\n";
    auto o = c.run_script(script);
    return o.isNumber() && o.toDouble() == 18.0;
}

bool include_error_location_in_included_file_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_file("unit_test/test_data/include_bad_syntax.cifa");
    std::string errors = c.get_errors_str();
    auto error_list = c.get_errors();
    return errors.find("unit_test/test_data/bad_syntax_include.cifa:2") != std::string::npos
        && errors.find("int y = undef_from_include;") != std::string::npos
        && error_list.size() == 1
        && error_list[0].filename == "unit_test/test_data/bad_syntax_include.cifa"
        && error_list[0].line == 2;
}

bool include_error_location_after_include_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_file("unit_test/test_data/include_then_bad_main.cifa");
    std::string errors = c.get_errors_str();
    auto error_list = c.get_errors();
    return errors.find("unit_test/test_data/include_then_bad_main.cifa:3") != std::string::npos
        && errors.find("int b = undef_after_include;") != std::string::npos
        && error_list.size() == 1
        && error_list[0].filename == "unit_test/test_data/include_then_bad_main.cifa"
        && error_list[0].line == 3;
}

bool include_test()
{
    struct IncludeCase
    {
        const char* name;
        bool (BackendTests::*test)();
    };
    const IncludeCase cases[] = {
        { "file cases", &BackendTests::include_file_cases_test },
        { "missing file", &BackendTests::include_missing_file_test },
        { "with parameters", &BackendTests::include_with_parameters_test },
        { "run_script include dir", &BackendTests::include_run_script_include_dir_test },
        { "run_script default dir", &BackendTests::include_run_script_default_dir_test },
        { "run_script include dir multi", &BackendTests::include_run_script_include_dir_multi_test },
        { "run_script include dir with parameters", &BackendTests::include_run_script_include_dir_with_params_test },
        { "multiple search directories", &BackendTests::include_multi_search_dirs_test },
        { "absolute path", &BackendTests::include_absolute_path_test },
        { "error location in included file", &BackendTests::include_error_location_in_included_file_test },
        { "error location after include", &BackendTests::include_error_location_after_include_test },
    };

    bool ok = true;
    for (const auto& include_case : cases)
    {
        if (!(this->*include_case.test)())
        {
            std::println(stderr, "  include case failed: {}", include_case.name);
            ok = false;
        }
    }
    return ok;
}

#undef Cifa
};

using DirectTests = BackendTests<DirectCifa>;
using BytecodeTests = BackendTests<CifaBytecode>;

double generated_perf_function_value(int function_index, int a, int b)
{
    double sum = 0;
    for (int j = 0; j < 3; ++j)
    {
        if ((a + j) % 2 != 0)
        {
            sum += a + b + function_index;
        }
        else
        {
            sum += a - b + function_index;
        }
    }
    return sum;
}

bool large_script_performance_test()
{
    constexpr int function_count = 1000;
    constexpr int calls = 300;
    std::string script;
    double expected = 0;

    for (int function_index = 0; function_index < function_count; ++function_index)
    {
        script += std::format("perf_func_{}(a, b) {{\n", function_index);
        script += "    double sum = 0;\n"
              "    for (int j = 0; j < 3; j++) {\n"
              "        if ((a + j) % 2) {\n";
        script += std::format("            sum += a + b + {};\n", function_index);
        script += "        } else {\n";
        script += std::format("            sum += a - b + {};\n", function_index);
        script += "        }\n"
              "    }\n"
              "    return sum;\n"
              "}\n";
    }

    script += "double total = 0;\n";
    for (int call_index = 0; call_index < calls; ++call_index)
    {
        const int first_function = call_index % function_count;
        const int second_function = (call_index * 7 + 3) % function_count;
        const int argument_b = call_index % 19 + 1;
        script += std::format("if ({0} % 5 == 0) {{ total += perf_func_{1}({0}, {3}); }} else {{ total += perf_func_{2}({0}, {3}); }}\n",
            call_index, first_function, second_function, argument_b);
        expected += generated_perf_function_value(call_index % 5 == 0 ? first_function : second_function,
            call_index, argument_b);
    }
    script += "return total;\n";

    Cifa c;
    c.set_output_error(false);
    const auto ast_started = std::chrono::steady_clock::now();
    auto result = c.run_script(script);
    const auto ast_elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - ast_started).count();

    cifa::CifaBytecode bytecode;
    bytecode.set_output_error(false);
    const auto bytecode_started = std::chrono::steady_clock::now();
    const bool bytecode_compiled = bytecode.compile_script(script);
    const auto bytecode_elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bytecode_started).count();
    if (!bytecode_compiled || bytecode.has_error()) return false;
    const auto bytecode_execute_started = std::chrono::steady_clock::now();
    auto bytecode_result = bytecode.run();
    const auto bytecode_execute_elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - bytecode_execute_started).count();

    std::println("large_script_performance_test: {} script functions, {} call blocks, {} bytes; Cifa source to result {:.6g} ms, source to CifaBytecode {:.6g} ms, CifaBytecode execute {:.6g} ms",
        function_count, calls, script.size(), ast_elapsed, bytecode_elapsed, bytecode_execute_elapsed);
    if (result.getSpecialType() == "Error")
    {
        std::println(stderr, "{}{}", c.get_errors_str(), c.get_runtime_error());
        return false;
    }
    return result.isNumber() && std::fabs(result.toDouble() - expected) < 1e-9
        && bytecode_result.isNumber() && std::fabs(bytecode_result.toDouble() - expected) < 1e-9
        && !bytecode.has_error() && !bytecode.has_runtime_error();
}

bool bytecode_execution_test()
{
    CifaBytecode c;
    c.set_output_error(false);
    int total = 0;
    c.register_function("record", [&total](ObjectVector& arguments) -> Object
        {
            if (arguments.size() != 1 || !arguments[0].isNumber())
            {
                return Object();
            }
            total += arguments[0].toInt();
            return Object();
        });

    if (!c.compile_script("entry_first: record(1); exit();\nentry_second: record(2); exit();\n")) return false;
    c.run("entry_second");
    if (c.has_runtime_error() || total != 2)
    {
        return false;
    }

    c.run("entry_first");
    if (c.has_runtime_error() || total != 3)
    {
        return false;
    }

    c.run();
    if (c.has_runtime_error() || total != 4)
    {
        return false;
    }

    c.run("missing_entry");
    if (!c.has_runtime_error() || c.get_runtime_error().find("missing_entry") == std::string::npos)
    {
        return false;
    }

    CifaBytecode file_code;
    if (!file_code.compile_file("unit_test/test_data/include_simple.cifa")) return false;
    auto file_result = file_code.run();
    return file_result.isNumber() && file_result.toInt() == 15;
}

bool bytecode_optimization_test()
{
    for (const bool enabled : {false, true})
    {
        CifaBytecode order;
        order.set_output_error(false);
        order.set_optimization_enabled(enabled);
        if (!order.compile_script("int global = 2; int change() { global = 9; return 3; } int result = global + change(); return result == 5 && global == 9;")) return false;
        const auto result = order.run();
        if (order.has_runtime_error() || !result.isType<bool>() || !result.toBool()) return false;
    }
    for (const std::string script : {
        "int calc(int left, int right) { int total = 0; total = (left + right) * (left - right); return total; } return calc(7, 3);",
        "int combine(int first, int second) { return first * 100 + second; } int calc(int left, int right) { return combine(left, (left + right) * (left - right)); } return calc(7, 3);",
        "int calc(int left, int right) { return (left + 1) / (right - 2); } return calc(7, 2);",
        "int calc(int right) { int left; return (left + 1) * (right - 2); } return calc(3);"})
    {
        DirectCifa direct;
        direct.set_output_error(false);
        const auto expected = direct.run_script(script);
        for (const bool enabled : {false, true})
        {
            CifaBytecode registers;
            registers.set_output_error(false);
            registers.set_optimization_enabled(enabled);
            if (!registers.compile_script(script))
            {
                std::println(stderr, "Register compile mismatch: {}\n{}", script, registers.get_translation_error());
                return false;
            }
            const auto actual = registers.run();
            if (registers.has_runtime_error() != direct.has_runtime_error())
            {
                std::println(stderr, "Register runtime mismatch: {}\nDirect: {}\nRegisters: {}", script,
                    direct.get_runtime_error(), registers.get_runtime_error());
                return false;
            }
            if (direct.has_runtime_error())
            {
                if (registers.get_runtime_error() != direct.get_runtime_error())
                {
                    std::println(stderr, "Register diagnostic mismatch: {}\nDirect:\n{}\nRegisters:\n{}", script,
                        direct.get_runtime_error(), registers.get_runtime_error());
                    return false;
                }
            }
            else if (actual.getType() != expected.getType() || actual.toInt64() != expected.toInt64())
            {
                std::println(stderr, "Register value mismatch: {}\nDirect: {} ({}, {}) {}\nRegisters: {} ({}, {}) {}", script,
                    std::to_string(expected.toInt64()), expected.getType().name(), expected.getSpecialType(), direct.get_errors_str(),
                    std::to_string(actual.toInt64()), actual.getType().name(), actual.getSpecialType(), registers.get_errors_str());
                return false;
            }
        }
    }
    for (const std::string script : {
        "int calculate(int divisor) { int left = 9; int answer = left / divisor; return answer; } return calculate(0);",
        "int calculate(left, right) { int answer = left + right; return answer; } return calculate(\"a\", \"b\");",
        "int calculate(int right) { int left; return left + right; } return calculate(2);",
        "auto missing() {} int calculate(int right) { auto left = missing(); return left + right; } return calculate(2);"})
    {
        std::string expected;
        for (const bool enabled : {false, true})
        {
            CifaBytecode diagnostics;
            diagnostics.set_output_error(false);
            diagnostics.set_optimization_enabled(enabled);
            if (!diagnostics.compile_script(script)) return false;
            diagnostics.run();
            if (!diagnostics.has_runtime_error()) return false;
            const auto message = diagnostics.get_runtime_error();
            if (!enabled) expected = message;
            else if (message != expected) return false;
        }
    }
    for (const bool enabled : {false, true})
    {
        CifaBytecode registers;
        registers.set_output_error(false);
        registers.set_optimization_enabled(enabled);
        if (!registers.compile_script(R"(
            int accumulate(int count) {
                int total = 0;
                int step = 3;
                for (int index = 0; index < count; index++) { total = total + step; }
                double fraction = 0.5;
                double mixed = total + fraction;
                mixed = mixed + fraction;
                int large = 9223372036854775807;
                int one = 1;
                large = large + one;
                return mixed == 31.0 && total == 30 && large == (-9223372036854775807 - 1);
            }
            return accumulate(10);
        )")) return false;
        const auto value = registers.run();
        if (registers.has_runtime_error() || value.toInt64() != 1) return false;
    }
    CifaBytecode optimized;
    optimized.set_output_error(false);
    optimized.set_optimization_enabled(true);
    if (!optimized.compile_script(R"(
        int folded = (int)(2.5 + 3.5) * 4 + (1 << 40) / 1099511627776;
        double fraction = 5.0 / 2.0;
        bool checked = ((5 > 3) && (2 == 2) && !(0 == 1));
        return folded == 25 && fraction == 2.5 && checked;
    )"))
    {
        return false;
    }
    const auto result = optimized.run();
    if (optimized.has_error() || optimized.has_runtime_error() || !result.isType<bool>() || !result.toBool())
    {
        return false;
    }

    CifaBytecode builtin_calls;
    builtin_calls.set_output_error(false);
    builtin_calls.set_optimization_enabled(true);
    if (!builtin_calls.compile_script(R"(
        int magnitude = abs(-7);
        int selected = ifv(1, min(8, 3, 5), max(2, 9, 4));
        double power = pow(2, 3) + sqrt(16) + sin(0);
        return magnitude == 7 && selected == 3 && power == 12;
    )")) return false;
    const auto builtin_result = builtin_calls.run();
    if (builtin_calls.has_runtime_error() || !builtin_result.isType<bool>() || !builtin_result.toBool()) return false;

    CifaBytecode integer_fast_path;
    integer_fast_path.set_output_error(false);
    integer_fast_path.set_optimization_enabled(true);
    if (!integer_fast_path.compile_script("return ((-7 * 6 + 2) / 5 == -8) && (3 << 4 == 48) && ((7 & 3) == 3);")) return false;
    const auto integer_result = integer_fast_path.run();
    if (integer_fast_path.has_runtime_error() || !integer_result.isType<bool>() || !integer_result.toBool()) return false;

    CifaBytecode dynamic_math;
    dynamic_math.set_output_error(false);
    dynamic_math.set_optimization_enabled(true);
    if (!dynamic_math.compile_script("double value = 7.9; return floor(value) == 7 && abs(fmod(value, 2) - 1.9) < 0.0001;")) return false;
    const auto dynamic_math_result = dynamic_math.run();
    if (dynamic_math.has_runtime_error() || !dynamic_math_result.isType<bool>() || !dynamic_math_result.toBool()) return false;
    for (const bool optimized : {false, true})
    {
        CifaBytecode value_transfer;
        value_transfer.set_output_error(false);
        value_transfer.set_optimization_enabled(optimized);
        if (!value_transfer.compile_script("auto changed(values) { values[0] = 9; return values; } auto forwarded(values) { return changed(values); } original = {1, 2}; result = forwarded(original); result[1] = 7; return original[0] == 1 && original[1] == 2 && result[0] == 9 && result[1] == 7;")) return false;
        for (int iteration = 0; iteration < 2; ++iteration)
        {
            const auto transfer_result = value_transfer.run();
            if (value_transfer.has_runtime_error() || !transfer_result.isType<bool>() || !transfer_result.toBool()) return false;
        }
    }

    CifaBytecode direct_size;
    direct_size.set_output_error(false);
    direct_size.set_optimization_enabled(true);
    if (!direct_size.compile_script("int length(value) { return size(value); } values = {1, 2, 3}; text = \"abc\"; mapping[\"key\"] = 1; return size(values) == 3 && size(text) == 3 && size(mapping) == 1 && length(values) == 3 && size(\"a\" + text) == 4;")) return false;
    const auto size_result = direct_size.run();
    if (direct_size.has_runtime_error() || !size_result.isType<bool>() || !size_result.toBool()) return false;
    CifaBytecode ordinary_size_error;
    CifaBytecode optimized_size_error;
    ordinary_size_error.set_output_error(false);
    optimized_size_error.set_output_error(false);
    optimized_size_error.set_optimization_enabled(true);
    const std::string invalid_size_script = "value = 42; return size(value);";
    if (!ordinary_size_error.compile_script(invalid_size_script) || !optimized_size_error.compile_script(invalid_size_script)) return false;
    ordinary_size_error.run();
    optimized_size_error.run();
    if (!ordinary_size_error.has_runtime_error() || !optimized_size_error.has_runtime_error()
        || ordinary_size_error.get_runtime_error() != optimized_size_error.get_runtime_error())
    {
        std::println(stderr, "Size diagnostic ordinary:\n{}Optimized:\n{}", ordinary_size_error.get_runtime_error(), optimized_size_error.get_runtime_error());
        return false;
    }

    CifaBytecode inlined_script_function;
    inlined_script_function.set_output_error(false);
    inlined_script_function.set_optimization_enabled(true);
    if (!inlined_script_function.compile_script("int difference(left, right) { return left - right; } int a = 9; int b = 4; return difference(a, b) == 5;")) return false;
    const auto inlined_result = inlined_script_function.run();
    if (inlined_script_function.has_runtime_error() || !inlined_result.isType<bool>() || !inlined_result.toBool()) return false;

    CifaBytecode repeated_parameter_function;
    repeated_parameter_function.set_output_error(false);
    repeated_parameter_function.set_optimization_enabled(true);
    if (!repeated_parameter_function.compile_script("int twice(value) { return value + value; } int input = 3; return twice(input) == 6;")) return false;
    const auto repeated_parameter_result = repeated_parameter_function.run();
    if (repeated_parameter_function.has_runtime_error() || !repeated_parameter_result.isType<bool>() || !repeated_parameter_result.toBool()) return false;

    CifaBytecode inlined_return_conversion;
    inlined_return_conversion.set_output_error(false);
    inlined_return_conversion.set_optimization_enabled(true);
    if (!inlined_return_conversion.compile_script("int truncate(value) { return value; } double input = 3.9; return truncate(input) == 3;")) return false;
    const auto return_conversion_result = inlined_return_conversion.run();
    if (inlined_return_conversion.has_runtime_error() || !return_conversion_result.isType<bool>() || !return_conversion_result.toBool()) return false;

    CifaBytecode folded_script_function;
    folded_script_function.set_output_error(false);
    folded_script_function.set_optimization_enabled(true);
    if (!folded_script_function.compile_script("int sum(a, b) { return a * 3 + b; } return sum(4, 2) == 14;")) return false;
    const auto folded_script_result = folded_script_function.run();
    if (folded_script_function.has_runtime_error() || !folded_script_result.isType<bool>() || !folded_script_result.toBool()) return false;
    const auto repeated_folded_script_result = folded_script_function.run();
    if (folded_script_function.has_runtime_error() || !repeated_folded_script_result.isType<bool>() || !repeated_folded_script_result.toBool()) return false;

    CifaBytecode recursive_script_function;
    recursive_script_function.set_output_error(false);
    recursive_script_function.set_optimization_enabled(true);
    if (!recursive_script_function.compile_script("int countdown(n) { return n ? countdown(n - 1) + 1 : 0; } return countdown(4) == 4;")) return false;
    const auto recursive_script_result = recursive_script_function.run();
    if (recursive_script_function.has_runtime_error() || !recursive_script_result.isType<bool>() || !recursive_script_result.toBool()) return false;

    CifaBytecode changed_script_function;
    changed_script_function.set_output_error(false);
    changed_script_function.set_optimization_enabled(true);
    if (!changed_script_function.compile_script(R"(
        int answer() { return 3; }
        run_string("int answer() { return 8; }");
        return answer() == 8;
    )")) return false;
    changed_script_function.run();
    if (!changed_script_function.has_runtime_error()
        || changed_script_function.get_runtime_error().find("script functions changed during optimized bytecode execution") == std::string::npos)
    {
        return false;
    }

    CifaBytecode array_read;
    array_read.set_output_error(false);
    array_read.set_optimization_enabled(true);
    if (!array_read.compile_script("int values[]; values.push_back(4); values.push_back(9); return values[0] + values[1] == 13;")) return false;
    const auto array_result = array_read.run();
    if (array_read.has_runtime_error() || !array_result.isType<bool>() || !array_result.toBool()) return false;

    CifaBytecode array_write;
    array_write.set_output_error(false);
    array_write.set_optimization_enabled(true);
    if (!array_write.compile_script("int values[]; values[2] = 5; values[2] += 4; return values[2] == 9;")) return false;
    const auto array_write_result = array_write.run();
    if (array_write.has_runtime_error() || !array_write_result.isType<bool>() || !array_write_result.toBool()) return false;

    CifaBytecode local_array;
    local_array.set_output_error(false);
    local_array.set_optimization_enabled(true);
    if (!local_array.compile_script(R"(
        int total() {
            int values[];
            values.push_back(4);
            values[1] = 5;
            return values[0] + values[1];
        }
        return total() == 9;
    )")) return false;
    const auto local_array_result = local_array.run();
    if (local_array.has_runtime_error() || !local_array_result.isType<bool>() || !local_array_result.toBool()) return false;

    CifaBytecode array_uninitialized;
    array_uninitialized.set_output_error(false);
    array_uninitialized.set_optimization_enabled(true);
    if (!array_uninitialized.compile_script("int values[]; return values[3];")) return false;
    array_uninitialized.run();
    if (!array_uninitialized.has_runtime_error()
        || array_uninitialized.get_runtime_error().find("array element 'values' has not been initialized") == std::string::npos) return false;

    CifaBytecode integer_overflow;
    integer_overflow.set_output_error(false);
    integer_overflow.set_optimization_enabled(true);
    if (!integer_overflow.compile_script("int least = -9223372036854775807 - 1; return least / -1;")) return false;
    integer_overflow.run();
    if (!integer_overflow.has_runtime_error()
        || integer_overflow.get_runtime_error().find("integer division overflow") == std::string::npos) return false;

    CifaBytecode changed_host;
    changed_host.set_output_error(false);
    changed_host.set_optimization_enabled(true);
    if (!changed_host.compile_script("return abs(-2);")) return false;
    if (!changed_host.register_function("unused_after_compile", [](ObjectVector&) { return Object(0); })) return false;
    changed_host.run();
    if (!changed_host.has_runtime_error()
        || changed_host.get_runtime_error().find("host functions changed after optimized bytecode compilation") == std::string::npos)
    {
        return false;
    }

    CifaBytecode overridden_builtin;
    overridden_builtin.set_output_error(false);
    overridden_builtin.set_optimization_enabled(true);
    if (!overridden_builtin.register_function("sin", [](ObjectVector&) { return Object(9); })) return false;
    if (!overridden_builtin.compile_script("return sin(0);")) return false;
    const auto overridden_result = overridden_builtin.run();
    if (overridden_builtin.has_runtime_error() || !overridden_result.isNumber() || overridden_result.toInt64() != 9) return false;

    CifaBytecode changed_during_run;
    changed_during_run.set_output_error(false);
    changed_during_run.set_optimization_enabled(true);
    int after_change_calls = 0;
    if (!changed_during_run.register_function("change_host", [&changed_during_run](ObjectVector&)
        {
            changed_during_run.register_function("registered_during_run", [](ObjectVector&) { return Object(); });
            return Object();
        })) return false;
    if (!changed_during_run.register_function("after_change", [&after_change_calls](ObjectVector&)
        {
            ++after_change_calls;
            return Object();
        })) return false;
    if (!changed_during_run.compile_script("change_host(); after_change();")) return false;
    changed_during_run.run();
    if (!changed_during_run.has_runtime_error()
        || changed_during_run.get_runtime_error().find("host functions changed during optimized bytecode execution") == std::string::npos
        || after_change_calls != 0)
    {
        return false;
    }

    CifaBytecode runtime_error;
    runtime_error.set_output_error(false);
    runtime_error.set_optimization_enabled(true);
    if (!runtime_error.compile_script("return 1 / 0;"))
    {
        return false;
    }
    runtime_error.run();
    const bool preserved_runtime_error = runtime_error.has_runtime_error()
        && runtime_error.get_runtime_error().find("integer division by zero") != std::string::npos;
    return preserved_runtime_error;
}

bool nested_bytecode_context_test()
{
    CifaBytecode c;
    c.set_output_error(false);
    c.set_optimization_enabled(false);
    int total = 0;
    c.register_function("record", [&total](ObjectVector& arguments) -> Object
        {
            total += arguments[0].toInt();
            return Object();
        });
    c.register_function("compile_replacement", [&c](ObjectVector&) -> Object
        {
            return Object(c.run_script("replacement: record(100); exit();").getSpecialType() != "Error");
        });
    if (!c.compile_script("record(1); compile_replacement(); record(2); exit();")) return false;
    c.run();
    if (c.has_runtime_error() || total != 103)
    {
        return false;
    }

    c.run_script("replacement: record(100); exit();");
    if (c.has_runtime_error() || total != 203)
    {
        return false;
    }

    c.run_script("record(10);");
    c.run_script("replacement: record(100); exit();");
    return !c.has_runtime_error() && total == 313;
}

bool direct_source_map_test()
{
    Cifa c;
    c.set_output_error(false);
    c.run_script("return 1;");
    c.run_script("double cached_value; return cached_value * 2;");
    const std::string error = c.get_runtime_error();
    return error.find("variable 'cached_value' has not been initialized") != std::string::npos
        && error.find("double cached_value; return cached_value * 2;") != std::string::npos;
}

bool nested_ast_function_lookup_test()
{
    Cifa c;
    c.set_output_error(false);
    c.register_function("run_child", [&c](ObjectVector&) -> Object
        {
            return c.run_script("return value(5);");
        });
    auto result = c.run_script("value(n) { return n + 1; } return run_child();");
    return result.isNumber() && result.toInt() == 6 && !c.has_runtime_error();
}

bool global_definition_scope_test()
{
    Cifa c;
    c.set_output_error(false);

    c.run_script("if (1) { nested() { return 1; } }");
    if (!c.has_error() || c.get_errors_str().find("script function 'nested' is only allowed in global scope") == std::string::npos)
    {
        return false;
    }

    c.run_script("if (1) { struct Local { int value; }; }");
    if (!c.has_error() || c.get_errors_str().find("struct 'Local' is only allowed in global scope") == std::string::npos)
    {
        return false;
    }

    c.run_script("global_fn(value) { return value + 1; } struct Global { int value; }; return 0;");
    if (c.has_error() || c.has_runtime_error()) return false;
    auto global_definitions = c.run_script("Global item; item.value = 5; return global_fn(item.value);");
    if (!global_definitions.isNumber() || global_definitions.toInt() != 6)
    {
        return false;
    }

    c.run_script("global_value = 1; { local_value = 2; global_value = 3; }");
    auto global_value = c.run_script("return global_value;");
    if (!global_value.isNumber() || global_value.toInt() != 3)
    {
        return false;
    }
    c.run_script("return local_value;");
    return c.has_error() && c.get_errors_str().find("local_value") != std::string::npos;
}

bool nested_script_global_scope_test()
{
    Cifa c;
    c.set_output_error(false);
    c.register_function("run_child", [&c](ObjectVector&) -> Object
        {
            return c.run_script("child_global = 5; child_function() { return child_global + 1; } exit(); child_global = 99;");
        });

    auto outer_result = c.run_script("outer_global = 10; run_child(); outer_global += 1; return outer_global;");
    if (!outer_result.isNumber() || outer_result.toInt() != 11)
    {
        return false;
    }
    auto persisted_child = c.run_script("return child_global * 100 + child_function();");
    if (!persisted_child.isNumber() || persisted_child.toInt() != 506)
    {
        return false;
    }

    c.register_function("read_outer_local", [&c](ObjectVector&) -> Object
        {
            return c.run_script("return outer_local;");
        });
    c.run_script("{ outer_local = 7; read_outer_local(); }");
    return c.has_error() && c.get_errors_str().find("outer_local") != std::string::npos;
}

bool nested_runtime_reporter_test()
{
    Cifa outer;
    Cifa inner;
    outer.set_output_error(false);
    inner.set_output_error(false);
    outer.register_function("run_inner", [&inner](ObjectVector&) -> Object
        {
            inner.run_script("double inner_value; return inner_value * 2;");
            return Object();
        });

    outer.run_script("run_inner(); double outer_value; return outer_value * 2;");
    const std::string outer_error = outer.get_runtime_error();
    const std::string inner_error = inner.get_runtime_error();
    return outer_error.find("variable 'outer_value'") != std::string::npos
        && inner_error.find("variable 'inner_value'") != std::string::npos;
}

bool diagnostic_position_test()
{
    struct Case
    {
        std::string script;
        std::string token;
        bool syntax;
    };
    const Case cases[] = {
        { "bad = \"abc\"; if (bad) return 1;", "bad)", false },
        { "bad = \"abc\"; while (bad) {}", "bad)", false },
        { "bad = \"abc\"; for (;bad;) {}", "bad;", false },
        { "bad = \"abc\"; do {} while (bad);", "bad)", false },
        { "for (item : 42) {}", "42", false },
        { "values = {1}; values.keys();", "keys", false },
        { "value = 1; value.clear();", "clear", false },
        { "return size(42);", "42", false },
        { "return random(1, 2, 3);", "random", false },
        { "return missing;", "missing", true },
        { "return unknown(1);", "unknown", true },
        { "return #bad;", "#", true },
        { "int x = (1 + 2));", ");", true },
        { "123 = 5;", "123", true }
    };
    for (const auto& test : cases)
    {
        Cifa interpreter;
        interpreter.set_output_error(false);
        interpreter.run_script(test.script);
        const auto error = test.syntax ? interpreter.get_errors_str() : interpreter.get_runtime_error();
        if (test.script == "return missing;"
            && error.find("parameter 'missing' has not been initialized") == std::string::npos)
        {
            return false;
        }
        const auto column = test.script.find(test.token) + 1;
        const std::string header = "  at <script>:1, col " + std::to_string(column) + ": ";
        const std::string expected = header + test.script + "\n"
            + std::string(header.size() + column - 1, ' ') + "^\n";
        if (error.find(expected) == std::string::npos
            || (test.syntax ? !interpreter.has_error() : !interpreter.has_runtime_error()))
        {
            std::print(stderr, "Diagnostic position mismatch:\n{}Expected:\n{}", error, expected);
            return false;
        }
    }
    return true;
}

int main(int argc, char** argv)
{
    if (argc > 1 && (std::string(argv[1]) == "--perf" || std::string(argv[1]) == "--perf-large"))
    {
        return large_script_performance_test() ? 0 : 1;
    }
    if (argc > 1 && std::string(argv[1]) == "--error-checks")
    {
        DirectTests direct;
        return direct.object_conversion_fallback_test() && direct.runtime_error_abort_test()
            && direct.object_vector_argument_error_test() && direct.script_function_return_check_test() ? 0 : 1;
    }

    int total = 0, ok = 0;
    DirectTests direct;
    BytecodeTests bytecode;
    auto run_direct_test = [&total, &ok](std::string name, bool (*test)())
    {
        total++;
        if (test())
        {
            ok++;
            std::println("[PASS] {}. {} success", total, name);
        }
        else
        {
            std::println("[FAIL] {}. {} failed", total, name);
        }
    };

    auto run_common_test = [&total, &ok, &direct, &bytecode](std::string name, auto direct_test, auto bytecode_test)
    {
        total++;
        const bool direct_passed = (direct.*direct_test)();
        const bool bytecode_passed = (bytecode.*bytecode_test)();
        if (direct_passed && bytecode_passed)
        {
            ok++;
            std::println("[PASS] {}. {} success", total, name);
        }
        else
        {
            std::println(stderr, "  backend results: Cifa={}, CifaBytecode={}", direct_passed, bytecode_passed);
            std::println("[FAIL] {}. {} failed", total, name);
        }
    };
    #define RUN_COMMON(name) run_common_test(#name, &DirectTests::name, &BytecodeTests::name)

    run_direct_test("diagnostic_position_test", diagnostic_position_test);
    run_direct_test("direct_source_map_test", direct_source_map_test);
    run_direct_test("nested_ast_function_lookup_test", nested_ast_function_lookup_test);
    run_direct_test("global_definition_scope_test", global_definition_scope_test);
    run_direct_test("nested_script_global_scope_test", nested_script_global_scope_test);
    run_direct_test("nested_runtime_reporter_test", nested_runtime_reporter_test);

    RUN_COMMON(register_function_test);
    RUN_COMMON(register_function_template_test);
    RUN_COMMON(registration_name_validation_test);
    RUN_COMMON(exit_function_test);
    RUN_COMMON(typed_function_argument_error_test);
    RUN_COMMON(object_vector_argument_error_test);
    RUN_COMMON(object_conversion_fallback_test);
    RUN_COMMON(builtin_math_function_test);
    RUN_COMMON(builtin_type_function_test);
    RUN_COMMON(range_for_test);
    RUN_COMMON(goto_test);
    RUN_COMMON(loop_math_test);
    RUN_COMMON(loop_control_test);
    RUN_COMMON(control_state_test);
    RUN_COMMON(ternary_operator_test);
    RUN_COMMON(logical_short_circuit_test);
    RUN_COMMON(numeric_literal_radix_test);
    RUN_COMMON(switch_case_test);
    RUN_COMMON(recursion_test);
    RUN_COMMON(script_void_function_test);
    RUN_COMMON(script_function_return_check_test);
    RUN_COMMON(script_function_argument_count_test);
    RUN_COMMON(script_function_global_scope_test);
    RUN_COMMON(string_operation_test);
    RUN_COMMON(string_compare_test);
    RUN_COMMON(bitwise_operator_test);
    RUN_COMMON(scope_shadowing_test);
    RUN_COMMON(complex_math_priority_test);
    RUN_COMMON(same_precedence_left_assoc_test);
    RUN_COMMON(unary_minus_test);
    RUN_COMMON(array_access_test);
    RUN_COMMON(array_literal_assignment_test);
    RUN_COMMON(size_of_array_test);
    RUN_COMMON(register_vector_test);
    RUN_COMMON(register_map_test);
    RUN_COMMON(type_promotion_test);
    RUN_COMMON(typed_numeric_storage_test);
    RUN_COMMON(auto_type_inference_test);
    RUN_COMMON(c_style_cast_test);
    RUN_COMMON(integer_arithmetic_test);
    RUN_COMMON(typed_function_conversion_test);
    RUN_COMMON(typed_array_and_struct_test);
    RUN_COMMON(typed_conversion_error_test);
    RUN_COMMON(registered_type_binding_test);
    RUN_COMMON(int64_storage_test);
    RUN_COMMON(empty_statement_test);
    RUN_COMMON(else_if_chain_test);
    RUN_COMMON(multi_dimensional_array_test);
    RUN_COMMON(compound_assignment_test);
    RUN_COMMON(c_string_library_test);
    RUN_COMMON(runtime_error_stack_test);
    RUN_COMMON(runtime_error_abort_test);
    RUN_COMMON(uninitialized_variable_runtime_test);
    RUN_COMMON(nested_execution_state_test);
    RUN_COMMON(nested_error_preservation_test);
    RUN_COMMON(nested_static_error_source_test);
    RUN_COMMON(mixed_array_literal_test);
    RUN_COMMON(string_key_map_test);
    RUN_COMMON(static_syntax_error_test);
    RUN_COMMON(loop_and_recursion_execution_test);
    RUN_COMMON(array_methods_test);
    RUN_COMMON(map_methods_test);
    RUN_COMMON(non_block_branch_declaration_test);
    RUN_COMMON(sprintf_format_test);
    RUN_COMMON(struct_test);
    RUN_COMMON(include_test);
    #undef RUN_COMMON

        run_direct_test("custom_operator_dispatch_test", +[]() { DirectTests direct; return direct.custom_operator_dispatch_test(); });

        run_direct_test("bytecode_execution_test", bytecode_execution_test);
        run_direct_test("bytecode_optimization_test", bytecode_optimization_test);
        run_direct_test("register_backend_structure_test", RegisterBackendTest::run);
        run_direct_test("nested_bytecode_context_test", nested_bytecode_context_test);

    std::println("Passed {} out of {} tests.", ok, total);
        return ok == total ? 0 : 1;
}
