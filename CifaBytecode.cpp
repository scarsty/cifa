#include "CifaBytecode.h"
#include <algorithm>

namespace cifa
{
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

Object CifaBytecode::write_value(Machine& machine, WriteOperation operation, const Object& target, Object right)
{
    switch (operation)
    {
    case WriteOperation::Assign: return right;
    case WriteOperation::Add: case WriteOperation::PostAdd: return machine.binary(Opcode::Add, target, right, {});
    case WriteOperation::Subtract: case WriteOperation::PostSubtract: return machine.binary(Opcode::Subtract, target, right, {});
    case WriteOperation::Multiply: return machine.binary(Opcode::Multiply, target, right, {});
    case WriteOperation::Divide: return machine.binary(Opcode::Divide, target, right, {});
    case WriteOperation::Modulo: return machine.binary(Opcode::Modulo, target, right, {});
    case WriteOperation::BitAnd: return machine.binary(Opcode::BitAnd, target, right, {});
    case WriteOperation::BitOr: return machine.binary(Opcode::BitOr, target, right, {});
    case WriteOperation::BitXor: return machine.binary(Opcode::BitXor, target, right, {});
    case WriteOperation::ShiftLeft: return machine.binary(Opcode::ShiftLeft, target, right, {});
    case WriteOperation::ShiftRight: return machine.binary(Opcode::ShiftRight, target, right, {});
    default: machine.set_error("invalid bytecode write operation"); return Object();
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
    index_sites.push_back({intern_name(node.str), intern_name(node.type_name), node.v.size(),
        node.with_type && node.suffix, !node.v[0].v.empty() && node.v[0].v[0].type == CalUnitType::String,
        node.with_type});
    return site;
}

void CifaBytecode::seal(Instructions& instructions)
{
    for (auto& instruction : instructions.code)
    {
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
    const size_t original_size = instructions.code.size();
    std::vector<size_t> remap(original_size + 1);
    size_t compact_size = 0;
    for (size_t index = 0; index < original_size; ++index)
        if (instructions.code[index].opcode != Opcode::Enter && instructions.code[index].opcode != Opcode::Leave)
            ++compact_size;
    remap[original_size] = compact_size;
    for (size_t index = original_size; index > 0; --index)
    {
        const size_t current = index - 1;
        const auto opcode = instructions.code[current].opcode;
        remap[current] = opcode == Opcode::Enter || opcode == Opcode::Leave ? remap[current + 1] : --compact_size;
    }
    std::vector<Instruction> code;
    std::vector<std::vector<std::pair<size_t, bool>>> diagnostic_frames;
    code.reserve(remap.back());
    diagnostic_frames.reserve(remap.back());
    for (size_t index = 0; index < original_size; ++index)
    {
        auto instruction = instructions.code[index];
        if (instruction.opcode == Opcode::Enter || instruction.opcode == Opcode::Leave) continue;
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
    if (node.str == ",") { opcode = Opcode::Drop; return true; }
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

size_t CifaBytecode::next_local_slot() const
{
    size_t next = 0;
    for (const auto& scope : compile_local_scopes)
        for (const auto& entry : scope)
            if (entry.second >= next) next = entry.second + 1;
    return next;
}

void CifaBytecode::emit(CalUnit& node, std::vector<Instruction>& instructions)
{
    if (node.type == CalUnitType::None || node.type == CalUnitType::Split || node.str == ";")
    {
        instructions.push_back({Opcode::Empty, source_ref(&node)});
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
            instructions.push_back({Opcode::Drop, source_ref(&node)});
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.emplace_back(target, "__goto");
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
        size_t& depth;
        size_t previous;
        ~RestoreDepth() { depth = previous; }
    } restore_depth{compile_traces, trace_before};
    if (node.type == CalUnitType::Key && (node.str == "break" || node.str == "continue") && !compile_loops.empty())
    {
        auto& loop = compile_loops.back();
        if (node.str == "continue" && loop.is_switch)
        {
            instructions.push_back({Opcode::Constant, source_ref(&node), constants.size()});
            constants.emplace_back("continue", "__");
            return;
        }
        instructions.push_back({Opcode::Unwind, source_ref(&node), loop.traces});
        instructions.push_back({Opcode::Jump, source_ref(&node)});
        (node.str == "break" ? loop.breaks : loop.continues).push_back(instructions.size() - 1);
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
                emit(child, instructions);
                instructions.push_back({Opcode::Drop, source_ref(&node)});
                instructions[active + 1].operand = instructions.size();
            }
        }
        for (auto jump : pending_cases) instructions[jump].operand = instructions.size();
        instructions.push_back({Opcode::ScopeLeave, source_ref(&node)});
        --compile_scopes;
        if (compiling_function != nullptr)
        {
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
            emit(node.v[1], instructions);
            instructions.push_back({Opcode::Drop, source_ref(&node)});
            instructions.push_back({Opcode::ScopeLeave, source_ref(&node)});
            --compile_scopes;
            if (compiling_function != nullptr)
            {
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
            emit(node.v[0].v[0], instructions);
            instructions.push_back({Opcode::Drop, source_ref(&node)});
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
        emit(node.v[node.str == "do" ? 0 : 1], instructions);
        instructions.push_back({Opcode::Drop, source_ref(&node)});
        const size_t next = instructions.size();
        if (ordinary_for)
        {
            emit(node.v[0].v[2], instructions);
            instructions.push_back({Opcode::Drop, source_ref(&node)});
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
        if (scope)
        {
            instructions.push_back({Opcode::ScopeEnter, source_ref(&node)});
            ++compile_scopes;
            if (compiling_function != nullptr)
            {
                compile_local_scope_bases.push_back(next_local_slot());
                instructions.back().auxiliary = compile_local_scope_bases.back() + 1;
                compile_local_scopes.emplace_back();
            }
        }
        instructions.push_back({Opcode::Empty, source_ref(&node)});
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
            instructions.push_back({Opcode::Drop, source_ref(&node)});
            emit(child, instructions);
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
        if (node.v.size() > 2) emit(node.v[2], instructions);
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
    instructions.push_back({Opcode::Enter, source_ref(&node)});
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
    else if (opcode == Opcode::Drop)
    {
        instructions.push_back({Opcode::Drop, source_ref(&node)});
        emit(node.v[1], instructions);
        instructions.push_back({Opcode::Drop, source_ref(&node)});
        instructions.push_back({Opcode::Empty, source_ref(&node)});
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
    struct State
    {
        size_t depth = 0;
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
    std::vector<size_t> pending{0};
    states[0].visited = true;
    if (&instructions == &root_instructions)
    {
        for (const auto entry : root_entries)
        {
            if (entry > instructions.code.size())
            { translation_error = "bytecode entry out of range"; return false; }
            states[entry].depth = 1;
            if (!states[entry].visited)
            {
                states[entry].visited = true;
                pending.push_back(entry);
            }
        }
        if (instructions.code.size() > 1)
        {
            states[1].depth = 1;
            states[1].visited = true;
            pending.push_back(1);
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
            if (previous.depth != state.depth || previous.frames != state.frames || previous.diagnostics != state.diagnostics || previous.scopes != state.scopes
                || previous.local_scope_bases != state.local_scope_bases || previous.calls != state.calls || previous.methods != state.methods
                || previous.ranges != state.ranges || previous.switches != state.switches)
            {
                translation_error = "bytecode control flow stack mismatch";
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
        auto& depth = state.depth;
        auto& frames = state.frames;
        const auto& instruction = instructions.code[pc];
        instructions.diagnostic_frames[pc] = state.diagnostics;
        switch (instruction.opcode)
        {
        case Opcode::Exit:
            continue;
        case Opcode::MethodBegin:
            if (instruction.operand >= calls.size()) { translation_error = "invalid method site"; return false; }
            if (calls[instruction.operand].local_slot != 0
                && calls[instruction.operand].local_slot - 1 >= local_slot_count)
            { translation_error = "bytecode local slot out of range"; return false; }
            if (!calls[instruction.operand].arguments.empty()) state.methods.emplace_back(instruction.operand, 0);
            ++depth;
            if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
            break;
        case Opcode::MethodValue:
            if (depth < 2 || instruction.operand >= calls.size()
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
            --depth;
            break;
        case Opcode::MethodPush:
            if (depth == 0 || instruction.operand >= calls.size() || calls[instruction.operand].arguments.size() != 1)
            { translation_error = "invalid bytecode method operand"; return false; }
            break;
        case Opcode::BindArgument:
            if (depth == 0 || instruction.auxiliary >= calls.size()
                || instruction.operand >= calls[instruction.auxiliary].arguments.size())
            { translation_error = "invalid bytecode argument binding"; return false; }
            break;
        case Opcode::RangeBegin:
            if (instruction.operand != pc)
            { translation_error = "invalid bytecode range identifier"; return false; }
            if (depth == 0) { translation_error = "bytecode range stack underflow"; return false; }
            if (std::find(state.ranges.begin(), state.ranges.end(), pc) != state.ranges.end())
            { translation_error = "bytecode range frame mismatch"; return false; }
            state.ranges.push_back(pc);
            --depth;
            break;
        case Opcode::RangeEnd:
        case Opcode::RangeNext:
            if (instruction.operand >= pc || instructions.code[instruction.operand].opcode != Opcode::RangeBegin)
            { translation_error = "invalid bytecode range identifier"; return false; }
            if (state.ranges.empty() || state.ranges.back() != instruction.operand)
            { translation_error = "bytecode range frame mismatch"; return false; }
            if (instruction.opcode == Opcode::RangeEnd) state.ranges.pop_back();
            if (instruction.opcode == Opcode::RangeNext) ++depth;
            if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
            break;
        case Opcode::Index:
            if (instruction.operand == 0 || depth < instruction.operand)
            { translation_error = "invalid bytecode index stack"; return false; }
            if (instruction.auxiliary >= index_sites.size()
                || index_sites[instruction.auxiliary].dimensions != instruction.operand
                || index_sites[instruction.auxiliary].name_id >= names.size()
                || index_sites[instruction.auxiliary].type_id >= names.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            depth = depth - instruction.operand + 1;
            break;
        case Opcode::Array:
            if (depth < instruction.operand) { translation_error = "invalid bytecode array stack"; return false; }
            depth = depth - instruction.operand + 1;
            if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
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
            if (instruction.operand >= calls.size() || depth < calls[instruction.operand].arguments.size())
            { translation_error = "invalid bytecode call stack"; return false; }
            if (state.calls.empty() || state.calls.back() != std::pair<size_t, bool>{instruction.operand, false})
            { translation_error = "bytecode call frame mismatch"; return false; }
            state.calls.back().second = true;
            depth = depth - calls[instruction.operand].arguments.size() + 1;
            if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
            break;
        case Opcode::SwitchMark:
            if (depth == 0) { translation_error = "bytecode switch stack underflow"; return false; }
            if (instruction.operand >= pc || instructions.code[instruction.operand].opcode != Opcode::LoopMark
                || std::find(state.switches.begin(), state.switches.end(), instruction.operand) != state.switches.end())
            { translation_error = "bytecode switch frame mismatch"; return false; }
            state.switches.push_back(instruction.operand);
            --depth;
            break;
        case Opcode::SwitchCase:
        case Opcode::SwitchDefault:
            if (state.switches.empty() || state.switches.back() != instruction.operand || instruction.auxiliary > 1)
            { translation_error = "bytecode switch frame mismatch"; return false; }
            if (instruction.opcode == Opcode::SwitchCase)
            {
                ++depth;
                if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
            }
            else if (instruction.auxiliary != 0)
            {
                if (depth == 0) { translation_error = "bytecode case stack underflow"; return false; }
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
            if (instruction.operand != 0 && depth < index_sites[instruction.operand - 1].dimensions)
            { translation_error = "bytecode store index underflow"; return false; }
            break;
        case Opcode::Store: case Opcode::StoreLocal:
            if (instruction.opcode == Opcode::Store && instruction.operand != 0 && instruction.operand - 1 >= index_sites.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            if (depth < (instruction.opcode == Opcode::Store && instruction.operand != 0
                ? index_sites[instruction.operand - 1].dimensions : 0) + 1)
            { translation_error = "bytecode store operand underflow"; return false; }
            if (instruction.opcode == Opcode::Store && instruction.operand != 0)
                depth -= index_sites[instruction.operand - 1].dimensions;
            break;
        case Opcode::Increment: case Opcode::IncrementLocal:
            if (instruction.opcode == Opcode::Increment && instruction.operand != 0 && instruction.operand - 1 >= index_sites.size())
            { translation_error = "invalid bytecode index descriptor"; return false; }
            if (instruction.opcode == Opcode::IncrementLocal)
            {
                ++depth;
                if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
                break;
            }
            if (instruction.operand != 0)
            {
                if (depth < index_sites[instruction.operand - 1].dimensions)
                { translation_error = "bytecode increment index underflow"; return false; }
                depth -= index_sites[instruction.operand - 1].dimensions - 1;
            }
            else ++depth;
            if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
            break;
        case Opcode::ScopeEnter:
            if (instruction.auxiliary != 0 && instruction.auxiliary - 1 > local_slot_count)
            { translation_error = "bytecode scope slot base out of range"; return false; }
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
        case Opcode::Branch: case Opcode::AndBranch: case Opcode::OrBranch: case Opcode::Drop:
            if (depth == 0)
            {
                translation_error = "bytecode branch/drop stack underflow";
                return false;
            }
            if (instruction.opcode == Opcode::Branch || instruction.opcode == Opcode::Drop) --depth;
            if (instruction.opcode != Opcode::Drop && !merge(instruction.operand, state)) return false;
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
            ++depth;
            if (depth > instructions.stack_capacity) instructions.stack_capacity = depth;
            break;
        case Opcode::Return:
            if (depth == 0 || !state.calls.empty() || !state.methods.empty())
            { translation_error = "invalid bytecode return state"; return false; }
            continue;
        case Opcode::Positive: case Opcode::Negative: case Opcode::LogicalNot: case Opcode::BitNot: case Opcode::Cast:
            if (depth == 0)
            {
                translation_error = "bytecode unary operand stack underflow";
                return false;
            }
            break;
        case Opcode::Add: case Opcode::Subtract: case Opcode::Multiply:
        case Opcode::Divide: case Opcode::Modulo: case Opcode::Less: case Opcode::Greater:
        case Opcode::LessEqual: case Opcode::GreaterEqual: case Opcode::Equal: case Opcode::NotEqual:
        case Opcode::BitAnd: case Opcode::BitOr: case Opcode::BitXor: case Opcode::ShiftLeft: case Opcode::ShiftRight:
        case Opcode::LogicalAnd: case Opcode::LogicalOr:
            if (depth < 2)
            {
                translation_error = "bytecode operand stack underflow";
                return false;
            }
            --depth;
            break;
        default:
            translation_error = "invalid bytecode opcode";
            return false;
        }
        if (!merge(pc + 1, state)) return false;
    }
    if (states.back().visited && (states.back().depth != 1 || !states.back().frames.empty() || states.back().scopes != 0
        || !states.back().calls.empty() || !states.back().methods.empty() || !states.back().ranges.empty() || !states.back().switches.empty()))
    {
        translation_error = "bytecode expression has an invalid final stack state";
        return false;
    }
    return true;
}

CifaBytecode::CifaBytecode() : session(std::make_unique<Session>(*this))
{
}

CifaBytecode::~CifaBytecode() = default;

void CifaBytecode::translate(Cifa& compiler)
{
    source_lines.reserve(compiler.compilation_source_line_infos.size());
    for (auto& line : compiler.compilation_source_line_infos)
        source_lines.push_back({std::move(line.filename), line.line, std::move(line.text)});
    module_data->structures = compiler.compilation_struct_defs;
    compiled_valid = compiler.compiled && !compiler.compile_failed;
    if (compiled_valid)
    {
        root_instructions.code.push_back({Opcode::Empty, source_ref(&compiler.compilation_root)});
        const size_t mark = root_instructions.code.size();
        root_instructions.code.push_back({Opcode::LoopMark, source_ref(&compiler.compilation_root)});
        compile_blocks.push_back({mark, {}, {}});
        for (const auto& child : compiler.compilation_root.v)
            if (child.type == CalUnitType::Label) compile_blocks.back().targets[child.str] = 0;
        for (auto& child : compiler.compilation_root.v)
        {
            root_entries.push_back(root_instructions.code.size());
            if (child.type == CalUnitType::Label)
            {
                entry_labels[child.str] = root_entries.size() - 1;
                compile_blocks.back().targets[child.str] = root_instructions.code.size();
                continue;
            }
            root_instructions.code.push_back({Opcode::Drop, source_ref(&compiler.compilation_root)});
            emit(child, root_instructions.code);
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
    }
    source_ids.clear();
    compile_sources.clear();
}

bool CifaBytecode::execute_instructions(Machine& machine, const Module& module, const Instructions& instructions,
    Object& result, size_t start)
{
    auto& interpreter = machine.host;
    auto& scopes = machine.scopes;
    std::vector<Object> stack;
    stack.reserve(instructions.stack_capacity);
    if (start != 0) stack.emplace_back();
    const size_t scope_base = scopes.size();
    struct RestoreScopes
    {
        ScopeStack& scopes;
        size_t size;
        ~RestoreScopes() { scopes.resize(size); }
    } restore_scopes{scopes, scope_base};
    struct LoopState { size_t stack; size_t scopes; size_t local_scope_bases; std::vector<size_t> ranges; std::vector<size_t> switches; };
    std::vector<std::optional<LoopState>> loop_states(instructions.code.size());
    std::vector<size_t> local_scope_bases;
    if (start != 0) loop_states[1] = {{stack.size(), scopes.size(), local_scope_bases.size()}};
    struct SwitchState { Object condition; bool active = false; };
    std::unordered_map<size_t, SwitchState> switches;
    struct RangeState { ObjectVector values; size_t index = 0; };
    std::unordered_map<size_t, RangeState> ranges;
    std::unordered_map<size_t, ObjectVector> method_arguments;
    struct RangeBinding { std::string name; Object value; size_t slot; };
    std::optional<RangeBinding> range_binding;
    struct Frame
    {
        const Instructions* instructions;
        const SourceLocation* node;
        size_t pc;
        size_t call_pc;
        size_t stack_base;
        std::vector<Object> locals;
        ScopeStack scopes;
        std::vector<size_t> local_scope_bases;
        std::vector<std::optional<LoopState>> loops;
        std::unordered_map<size_t, SwitchState> switches;
        const FunctionCode* function;
        const SourceLocation* call;
        std::unordered_map<size_t, RangeState> ranges;
        std::unordered_map<size_t, ObjectVector> method_arguments;
        const Module* owner;
        std::shared_ptr<const Module> module;
        std::vector<bool> aliases;
    };
    std::vector<Frame> frames;
    struct RestoreCaller
    {
        std::vector<Frame>& frames;
        ScopeStack& scopes;
        ~RestoreCaller()
        {
            if (!frames.empty()) scopes = std::move(frames.front().scopes);
        }
    } restore_caller{frames, scopes};
    const Module* active_owner = &module;
    std::shared_ptr<const Module> active_module;
    const Instructions* active_instructions = &instructions;
    const SourceLocation* active_node = &module.source(module.root_source);
    const FunctionCode* active_function = nullptr;
    const SourceLocation* active_call = nullptr;
    const SourceLocation* current_source = active_node;
    size_t pc = start;
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
            }
            if (instruction_pc < code->code.size() && code->code[instruction_pc].opcode == Opcode::CallBegin)
                destination.emplace_back(&owner->source(code->code[instruction_pc].source), true);
        };
        for (const auto& frame : frames) append(frame.owner, frame.instructions, frame.call_pc, frame.node);
        append(active_owner, active_instructions, pc == 0 ? 0 : pc - 1, active_node);
    };
    machine.append_diagnostic_frames = append_diagnostic_frames;
    struct RestoreDiagnosticFrames
    {
        Machine& machine;
        ~RestoreDiagnosticFrames() { machine.append_diagnostic_frames = {}; }
    } restore_diagnostic_frames{machine};
    Object::set_runtime_error_reporter([&machine, &current_source](const std::string& message, const Object* value)
        {
            if (value != nullptr && value->getSpecialType() == "NoValue") machine.set_no_value_error(*value, current_source);
            else machine.set_error(message, current_source);
        });
    struct RestoreObjectReporter
    {
        ~RestoreObjectReporter() { Object::clear_runtime_error_reporter(); }
    } restore_object_reporter;
    std::vector<Object> active_locals;
    std::vector<bool> active_aliases;
    auto bind_local_storage = [&](const std::string& name, size_t slot, bool current_only)
    {
        active_aliases[slot] = true;
        for (size_t scope = scopes.size(); scope > 0; --scope)
        {
            auto& current = scopes[scope - 1];
            if (const auto found = current.slots.find(name); found != current.slots.end())
            {
                active_aliases[slot] = found->second != &active_locals[slot];
                return;
            }
            if (const auto found = current.find(name); found != current.end())
            {
                active_locals[slot] = std::move(found->second);
                current.erase(found);
                current.slots[name] = &active_locals[slot];
                active_aliases[slot] = false;
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
    auto finish_call = [&](Object value)
    {
        if (active_function && !value.hasValue())
            value = machine.make_no_value(active_function->name, *active_call);
        auto saved = std::move(frames.back());
        frames.pop_back();
        return_states.pop_back();
        active_locals = std::move(saved.locals);
        active_aliases = std::move(saved.aliases);
        scopes = std::move(saved.scopes);
        local_scope_bases = std::move(saved.local_scope_bases);
        stack.resize(saved.stack_base);
        stack.push_back(std::move(value));
        loop_states = std::move(saved.loops);
        switches = std::move(saved.switches);
        ranges = std::move(saved.ranges);
        method_arguments = std::move(saved.method_arguments);
        active_owner = saved.owner;
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
            finish_call(Object());
            continue;
        }
        const auto& instruction = active_instructions->code[pc++];
        current_source = instruction.source.id != 0 ? &active_owner->source(instruction.source) : active_node;
        if (instruction.opcode == Opcode::Exit)
        {
            machine.exit_requested = true;
            result = Object();
            return true;
        }
        if (instruction.opcode == Opcode::Member)
        {
            stack.push_back(machine.resolve_member(active_owner->names[instruction.operand],
                active_owner->names[instruction.auxiliary]));
            if (machine.should_stop()) { result = Object(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::MethodPush)
        {
            const auto& site = active_owner->calls[instruction.operand];
            auto& object = machine.get_or_create(active_owner->names[site.base_name_id]);
            stack.back() = machine.push_back(active_owner->source(site.method_source), object, std::move(stack.back()));
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::MethodBegin || instruction.opcode == Opcode::MethodValue)
        {
            const auto& site = active_owner->calls[instruction.operand];
            const auto& method_name = active_owner->names[site.name_id];
            auto& object = machine.get_or_create(active_owner->names[site.base_name_id]);
            std::vector<SourceLocation> arguments;
            ObjectVector values;
            if (instruction.opcode == Opcode::MethodBegin && !site.arguments.empty())
            {
                stack.emplace_back();
                continue;
            }
            if (instruction.opcode == Opcode::MethodValue)
            {
                auto& pending = method_arguments[instruction.operand];
                pending.push_back(std::move(stack.back()));
                stack.pop_back();
                if (method_name == "insert" && pending.size() < 2)
                {
                    stack.back() = Object(double(object.ref<ObjectVector>().size()));
                    continue;
                }
                values = std::move(pending);
                method_arguments.erase(instruction.operand);
                for (size_t index = 0; index < values.size(); ++index)
                    arguments.push_back(site.arguments[method_name == "insert" ? index : instruction.auxiliary]);
            }
            Object value = machine.call_method(method_name, active_owner->source(site.method_source), object, arguments,
                [&](size_t index) { return std::move(values[index]); });
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            const size_t local_slot = active_owner->calls[instruction.operand].local_slot;
            if (local_slot != 0)
            {
                if (local_slot - 1 >= active_locals.size())
                {
                    machine.set_error("bytecode local slot out of range");
                    result = machine.error_result();
                    return true;
                }
            }
            if (instruction.opcode == Opcode::MethodBegin) stack.push_back(std::move(value));
            else stack.back() = std::move(value);
            continue;
        }
        if (instruction.opcode == Opcode::BindArgument)
        {
            const auto& call = active_owner->calls[instruction.auxiliary];
            const auto& call_name = active_owner->names[call.name_id];
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
                stack.back() = machine.convert_type(stack.back(), parameter.type_name, call.arguments[instruction.operand]);
                if (machine.should_stop()) { result = machine.error_result(); return true; }
            }
            continue;
        }
        if (instruction.opcode == Opcode::MethodNoArgs)
        {
            const auto& site = active_owner->calls[instruction.operand];
            auto& object = machine.get_or_create(active_owner->names[site.base_name_id]);
            std::vector<SourceLocation> arguments;
            stack.push_back(machine.call_method(active_owner->names[site.name_id],
                active_owner->source(site.method_source), object, arguments, [](size_t) { return Object(); }));
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (instruction.auxiliary != 0)
            {
                if (instruction.auxiliary - 1 >= active_locals.size())
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
            if (!machine.range(stack.back(), active_owner->source(instruction.target_source), state.values))
            { result = machine.error_result(); return true; }
            stack.pop_back();
            ranges[instruction.operand] = std::move(state);
            continue;
        }
        if (instruction.opcode == Opcode::RangeNext)
        {
            auto& state = ranges.at(instruction.operand);
            const bool available = state.index < state.values.size();
            if (available)
            {
                Object value = state.values[state.index++];
                const auto& parameter = active_owner->source(instruction.target_source);
                const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
                const auto& name = active_owner->names[site.name_id];
                if (!machine.bind_range(value, name, active_owner->names[site.type_id], parameter))
                { result = machine.error_result(); return true; }
                if (instruction.auxiliary != 0)
                {
                    const size_t slot = instruction.auxiliary - 1;
                    if (slot >= active_locals.size())
                    {
                        machine.set_error("bytecode local slot out of range");
                        result = machine.error_result();
                        return true;
                    }
                    active_locals[slot] = value;
                    active_aliases[slot] = false;
                }
                range_binding.emplace(name, instruction.auxiliary == 0 ? std::move(value) : Object(), instruction.auxiliary);
            }
            stack.emplace_back(available);
            continue;
        }
        if (instruction.opcode == Opcode::RangeEnd)
        {
            ranges.erase(instruction.operand);
            range_binding.reset();
            continue;
        }
        if (instruction.opcode == Opcode::Index)
        {
            ObjectVector indices;
            for (size_t index = instruction.operand; index > 0; --index)
            {
                indices.insert(indices.begin(), std::move(stack.back()));
                stack.pop_back();
            }
            const auto& site = active_owner->index_sites[instruction.auxiliary];
            const auto& name = active_owner->names[site.name_id];
            auto* base = machine.find_object(name);
            const bool map_access = (base != nullptr && base->isType<ObjectMap>()) || site.string_index;
            auto& element = machine.indexed(name, active_owner->names[site.type_id],
                site.dimensions, site.declaration, false, false,
                [&](size_t dimension) { return indices[dimension]; });
            stack.push_back(site.declaration ? element : machine.checked_index(element, map_access));
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Array)
        {
            ObjectVector elements(instruction.operand);
            for (size_t index = elements.size(); index > 0; --index)
            {
                elements[index - 1] = std::move(stack.back());
                stack.pop_back();
            }
            stack.emplace_back(std::move(elements));
            continue;
        }
        if (instruction.opcode == Opcode::CallBegin)
        {
            const auto& call = active_owner->calls[instruction.operand];
            const auto& call_name = active_owner->names[call.name_id];
            if (!interpreter.functions.contains(call_name))
            {
                std::shared_ptr<const Module> function_module;
                if (machine.find_function(call_name, call.arguments.size(), function_module) == nullptr)
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
            }
            continue;
        }
        if (instruction.opcode == Opcode::CallEnd) continue;
        if (instruction.opcode == Opcode::Call)
        {
            auto& call = active_owner->calls[instruction.operand];
            auto& call_source = active_owner->source(call.source);
            const auto& call_name = active_owner->names[call.name_id];
            ObjectVector arguments(call.arguments.size());
            for (size_t index = arguments.size(); index > 0; --index)
            {
                arguments[index - 1] = std::move(stack.back());
                stack.pop_back();
                if (arguments[index - 1].name.empty()) arguments[index - 1].name = call.arguments[index - 1].str;
            }
            if (interpreter.functions.contains(call_name))
            {
                stack.push_back(machine.call_host(call_name, arguments, call.arguments));
            }
            else
            {
                std::shared_ptr<const Module> function_module;
                const auto* cached = machine.find_function(call_name, arguments.size(), function_module);
                if (!cached)
                {
                    machine.set_error("bytecode function was not prepared: " + call_name);
                    result = machine.error_result();
                    return true;
                }
                ScopeStack locals(1);
                std::vector<Object> local_values((std::max)(cached->local_slot_count, arguments.size()));
                for (size_t index = 0; index < arguments.size(); ++index)
                {
                    const auto& parameter = cached->parameters[index];
                    local_values[index] = std::move(arguments[index]);
                    if (!parameter.type_name.empty())
                        machine.bind_type(local_values[index], parameter.type_name, call.arguments[index]);
                    else
                    {
                        local_values[index].bound_type = typeid(void);
                        local_values[index].declared_type_name.clear();
                    }
                    locals.back().slots[parameter.name] = &local_values[index];
                }
                if (machine.should_stop()) { result = machine.error_result(); return true; }
                frames.push_back({active_instructions, active_node, pc, pc - 1, stack.size(),
                    std::move(active_locals), std::move(scopes), std::move(local_scope_bases), std::move(loop_states), std::move(switches), active_function, active_call,
                    std::move(ranges), std::move(method_arguments), active_owner, std::move(active_module), std::move(active_aliases)});
                active_function = cached;
                active_call = &call_source;
                active_node = &function_module->source(cached->body_source);
                active_owner = function_module.get();
                active_module = std::move(function_module);
                active_instructions = &cached->instructions;
                const size_t required_capacity = stack.size() + active_instructions->stack_capacity;
                if (stack.capacity() < required_capacity) stack.reserve(required_capacity);
                active_locals = std::move(local_values);
                active_aliases.assign(active_locals.size(), false);
                scopes = std::move(locals);
                local_scope_bases.clear();
                loop_states.assign(active_instructions->code.size(), std::nullopt);
                switches.clear();
                ranges.clear();
                method_arguments.clear();
                return_states.emplace_back();
                return_states.back().return_type = cached->return_type;
                pc = 0;
            }
            if (machine.exit_requested) { result = Object(); return true; }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::SwitchMark)
        {
            switches[instruction.operand] = {std::move(stack.back()), false};
            stack.pop_back();
            continue;
        }
        if (instruction.opcode == Opcode::SwitchCase)
        {
            stack.emplace_back(switches.at(instruction.operand).active);
            continue;
        }
        if (instruction.opcode == Opcode::SwitchDefault)
        {
            auto& state = switches.at(instruction.operand);
            if (instruction.auxiliary != 0)
            {
                state.active = interpreter.equal(state.condition, stack.back()).toBool();
                stack.back() = Object(state.active);
            }
            else state.active = true;
            if (interpreter.should_stop_execution()) { result = Object(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::SwitchEnd)
        {
            switches.erase(instruction.operand);
            continue;
        }
        if (instruction.opcode == Opcode::LoopMark)
        {
            loop_states[pc - 1] = {{stack.size(), scopes.size(), local_scope_bases.size()}};
            for (const auto& entry : ranges) loop_states[pc - 1]->ranges.push_back(entry.first);
            for (const auto& entry : switches) loop_states[pc - 1]->switches.push_back(entry.first);
            continue;
        }
        if (instruction.opcode == Opcode::Unwind)
        {
            const auto& state = loop_states.at(instruction.operand).value();
            for (auto range = ranges.begin(); range != ranges.end();)
                if (std::find(state.ranges.begin(), state.ranges.end(), range->first) == state.ranges.end()) range = ranges.erase(range);
                else ++range;
            range_binding.reset();
            for (auto current = switches.begin(); current != switches.end();)
                if (std::find(state.switches.begin(), state.switches.end(), current->first) == state.switches.end()) current = switches.erase(current);
                else ++current;
            stack.resize(state.stack);
            scopes.resize(state.scopes);
            while (local_scope_bases.size() > state.local_scope_bases)
            {
                const size_t base = local_scope_bases.back();
                local_scope_bases.pop_back();
                if (base != 0)
                    for (size_t slot = base - 1; slot < active_locals.size(); ++slot)
                    {
                        active_locals[slot] = Object();
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
                machine.assign_named(active_owner->names[site.name_id], active_owner->names[site.type_id],
                    site.with_type, site.with_type, target);
            }
            else
            {
                const auto& site = active_owner->index_sites[instruction.operand - 1];
                ObjectVector indices;
                for (size_t index = site.dimensions; index > 0; --index)
                    indices.insert(indices.begin(), stack[stack.size() - index]);
                machine.indexed(active_owner->names[site.name_id], active_owner->names[site.type_id],
                    site.dimensions, site.declaration, false, site.with_type,
                    [&](size_t dimension) { return indices[dimension]; });
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::DeclareLocal)
        {
            auto& source = active_owner->source(instruction.source);
            if (instruction.operand >= active_locals.size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            auto target = machine.read_named(active_owner->names[site.name_id], active_owner->names[site.type_id],
                site.with_type, false, true, source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            active_locals[instruction.operand] = target;
            bind_local_storage(active_owner->names[site.name_id], instruction.operand, false);
            if (active_aliases[instruction.operand]) active_locals[instruction.operand] = Object();
            stack.push_back(target);
            continue;
        }
        if (instruction.opcode == Opcode::IncrementLocal)
        {
            auto& source = active_owner->source(instruction.source);
            if (instruction.operand >= active_locals.size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& binding = active_owner->variable_sites[instruction.variable_site - 1];
            auto& target = active_aliases[instruction.operand]
                ? machine.get_or_create(active_owner->names[binding.name_id])
                : active_locals[instruction.operand];
            Object old = target;
            Object value = write_value(machine, instruction.write, target, Object(1));
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            machine.assign(target, std::move(value), site.with_type, active_owner->names[site.type_id], source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            auto& bound = machine.get_or_create(active_owner->names[site.name_id]);
            if (&bound != &target) bound = target;
            stack.push_back(instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract ? std::move(old) : target);
            continue;
        }
        if (instruction.opcode == Opcode::StoreLocal)
        {
            auto& source = active_owner->source(instruction.source);
            if (instruction.operand >= active_locals.size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            const auto& binding = active_owner->variable_sites[instruction.variable_site - 1];
            auto& target = !binding.with_type && active_aliases[instruction.operand]
                ? machine.get_or_create(active_owner->names[binding.name_id])
                : active_locals[instruction.operand];
            Object right = std::move(stack.back());
            Object value = write_value(machine, instruction.write, target, std::move(right));
            const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
            machine.assign(target, std::move(value), site.with_type, active_owner->names[site.type_id], source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            auto& bound = machine.get_or_create(active_owner->names[site.name_id], site.with_type);
            if (&bound != &target) bound = target;
            bind_local_storage(active_owner->names[site.name_id], instruction.operand, site.with_type);
            stack.back() = target;
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
            ObjectVector indices;
            if (indexed)
            {
                const size_t offset = increment ? 0 : 1;
                for (size_t index = site->dimensions; index > 0; --index)
                    indices.push_back(stack[stack.size() - offset - index]);
            }
            auto& target = member
                ? machine.resolve_member(
                    active_owner->names[active_owner->member_sites[instruction.member_site - 1].first],
                    active_owner->names[active_owner->member_sites[instruction.member_site - 1].second])
                : indexed
                ? machine.indexed(
                    active_owner->names[site->name_id], active_owner->names[site->type_id],
                    site->dimensions, false, false, increment && site->with_type,
                    [&](size_t dimension) { return indices[dimension]; })
                : machine.assign_named(active_owner->names[variable->name_id], active_owner->names[variable->type_id],
                    variable->with_type, increment && variable->with_type, active_owner->source(instruction.target_source));
            Object old = increment ? target : Object();
            Object right = increment ? Object(1) : std::move(stack.back());
            Object value = write_value(machine, instruction.write, target, std::move(right));
            const bool with_type = variable != nullptr ? variable->with_type : site != nullptr && site->with_type;
            const std::string& type_name = variable != nullptr ? active_owner->names[variable->type_id]
                : site != nullptr ? active_owner->names[site->type_id] : std::string();
            machine.assign(target, std::move(value), with_type, type_name, source);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (instruction.auxiliary != 0)
            {
                const size_t slot = instruction.auxiliary - 1;
                if (slot >= active_locals.size())
                {
                    machine.set_error("bytecode local slot out of range");
                    result = machine.error_result();
                    return true;
                }
            }
            Object stored = increment && (instruction.write == WriteOperation::PostAdd || instruction.write == WriteOperation::PostSubtract) ? std::move(old) : target;
            if (indexed && !increment) stack.pop_back();
            if (indexed)
                for (size_t index = 0; index < site->dimensions - 1; ++index) stack.pop_back();
            if (increment && !indexed) stack.push_back(std::move(stored));
            else stack.back() = std::move(stored);
            continue;
        }
        if (instruction.opcode == Opcode::ScopeEnter)
        {
            scopes.emplace_back();
            local_scope_bases.push_back(instruction.auxiliary);
            if (range_binding)
            {
                if (range_binding->slot != 0)
                    scopes.back().slots[range_binding->name] = &active_locals[range_binding->slot - 1];
                else scopes.back()[range_binding->name] = std::move(range_binding->value);
                range_binding.reset();
            }
            continue;
        }
        if (instruction.opcode == Opcode::ScopeLeave)
        {
            if (instruction.auxiliary != 0)
                for (size_t slot = instruction.auxiliary - 1; slot < active_locals.size(); ++slot)
                {
                    active_locals[slot] = Object();
                    active_aliases[slot] = false;
                }
            scopes.pop_back();
            local_scope_bases.pop_back();
            continue;
        }
        if (instruction.opcode == Opcode::Return)
        {
            auto value = std::move(stack.back());
            const auto& states = return_states;
            if (!states.empty() && !states.back().return_type.empty() && states.back().return_type != "void")
                value = machine.convert_type(value, states.back().return_type, active_owner->source(instruction.source));
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (return_states.empty()) return_states.emplace_back();
            return_states.back().has_value = true;
            return_states.back().value = std::move(value);
            if (!frames.empty())
            {
                finish_call(return_states.back().value);
                continue;
            }
            result = return_states.back().value;
            return true;
        }
        if (instruction.opcode == Opcode::Jump) { pc = instruction.operand; continue; }
        if (instruction.opcode == Opcode::Empty) { stack.emplace_back(); continue; }
        if (instruction.opcode == Opcode::Drop) { stack.pop_back(); continue; }
        if (instruction.opcode == Opcode::Branch || instruction.opcode == Opcode::AndBranch || instruction.opcode == Opcode::OrBranch)
        {
            const SourceLocation* condition_source = instruction.condition_source.id != 0
                ? &active_owner->source(instruction.condition_source) : nullptr;
            const SourceLocation* call_source = &active_owner->source(instruction.source);
            if (condition_source != nullptr) current_source = condition_source;
            const bool condition = condition_source != nullptr
                ? machine.condition(stack.back(), *condition_source) : stack.back().toBool();
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            if (instruction.opcode == Opcode::Branch)
            {
                stack.pop_back();
                if (!condition) pc = instruction.operand;
            }
            else if ((instruction.opcode == Opcode::AndBranch && !condition)
                || (instruction.opcode == Opcode::OrBranch && condition))
            {
                stack.back() = Object(condition ? 1 : 0);
                pc = instruction.operand;
            }
            continue;
        }
        if (instruction.opcode == Opcode::Constant)
        {
            stack.push_back(active_owner->constants[instruction.operand]);
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
        if (instruction.opcode == Opcode::LoadLocal)
        {
            if (instruction.operand >= active_locals.size())
            {
                machine.set_error("bytecode local slot out of range");
                result = machine.error_result();
                return true;
            }
            auto& location = active_owner->source(instruction.source);
            const auto& name = active_owner->names[instruction.auxiliary];
            auto& value = active_aliases[instruction.operand]
                ? machine.get_or_create(name) : active_locals[instruction.operand];
            if (!value.hasValue())
                machine.set_error("variable '" + name + "' has not been initialized", &location);
            stack.push_back(value);
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Load || instruction.opcode == Opcode::Peek)
        {
            auto& source = active_owner->source(instruction.source);
            if (instruction.auxiliary == 1)
            {
                const auto& name = active_owner->names[instruction.operand];
                auto* existing = machine.find_object(name);
                auto& value = machine.get_or_create(name);
                if (instruction.opcode != Opcode::Peek && existing != nullptr && !value.hasValue())
                    machine.set_error("variable '" + name + "' has not been initialized", &source);
                stack.push_back(value);
            }
            else
            {
                const auto& site = active_owner->variable_sites[instruction.variable_site - 1];
                const bool peek = instruction.opcode == Opcode::Peek;
                stack.push_back(machine.read_named(active_owner->names[site.name_id], active_owner->names[site.type_id],
                    site.with_type, peek, !peek, source));
            }
            if (machine.should_stop()) { result = machine.error_result(); return true; }
            continue;
        }
        if (instruction.opcode == Opcode::Positive || instruction.opcode == Opcode::Negative
            || instruction.opcode == Opcode::LogicalNot || instruction.opcode == Opcode::BitNot
            || instruction.opcode == Opcode::Cast)
        {
            auto value = std::move(stack.back());
            switch (instruction.opcode)
            {
            case Opcode::Positive:
                stack.back() = value.isType<bool>() ? Object(value.toInt64()) : std::move(value);
                break;
            case Opcode::Negative: stack.back() = interpreter.sub(Object(0), value); break;
            case Opcode::LogicalNot: stack.back() = Object(!value.toBool()); break;
            case Opcode::BitNot: stack.back() = Object(~value.toInt64()); break;
            case Opcode::Cast:
                stack.back() = machine.convert_type(value, active_owner->names[instruction.operand],
                    active_owner->source(instruction.source));
                break;
            default: break;
            }
        }
        else
        {
            auto right = std::move(stack.back());
            stack.pop_back();
            auto left = std::move(stack.back());
            stack.pop_back();
            Object value = machine.binary(instruction.opcode, left, right, *current_source);
            stack.push_back(std::move(value));
        }
        if (machine.should_stop())
        {
            result = machine.error_result();
            return true;
        }
    }
    result = std::move(stack.back());
    return true;
}

void CifaBytecode::Machine::publish(const std::shared_ptr<const Module>& module)
{
    for (const auto& [name, fields] : module->structures) structures[name] = fields;
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

Object* CifaBytecode::Machine::find_object(const std::string& name)
{
    for (auto scope = scopes.rbegin(); scope != scopes.rend(); ++scope)
    {
        if (const auto slot = scope->slots.find(name); slot != scope->slots.end()) return slot->second;
        if (const auto value = scope->find(name); value != scope->end()) return &value->second;
    }
    const auto global = host.global_variables.find(name);
    return global == host.global_variables.end() ? nullptr : &global->second;
}

Object& CifaBytecode::Machine::get_or_create(const std::string& name, bool current_scope_only)
{
    if (!current_scope_only)
    {
        if (auto* existing = find_object(name)) return *existing;
    }
    if (!scopes.empty()) return scopes.back()[name];
    return host.global_variables[name];
}

Object& CifaBytecode::Machine::resolve_member(const std::string& base_name, const std::string& field_name)
{
    if (auto* base = find_object(base_name); base != nullptr && base->isType<ObjectMap>())
    {
        auto& element = base->ref<ObjectMap>()[field_name];
        element.name = base_name + "." + field_name;
        return element;
    }
    return get_or_create(base_name + "::" + field_name);
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
    auto& value = get_or_create(name, declare_current);
    if (with_type) bind_type(value, type_name, location);
    return value;
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

Object CifaBytecode::Machine::read_named(const std::string& name, const std::string& type_name, bool with_type, bool only_check,
    bool initialize_struct, const SourceLocation& location)
{
    if (initialize_struct && with_type && structures.contains(type_name))
    {
        auto* existing = find_object(name);
        if (existing == nullptr || !existing->isType<ObjectMap>())
        {
            ObjectMap fields;
            for (const auto& field : structures.at(type_name))
            {
                Object field_value;
                bind_type(field_value, field.type_name, location);
                field_value.name = name + "." + field.name;
                fields.emplace(field.name, std::move(field_value));
            }
            auto& value = assign_named(name, type_name, true, true, location);
            value = Object(std::move(fields));
            value.declared_type_name = type_name;
            value.bound_type = typeid(ObjectMap);
            value.name = name;
            return value;
        }
    }
    const bool existed = find_object(name) != nullptr;
    auto& value = get_or_create(name);
    if (with_type) bind_type(value, type_name, location);
    if (!only_check && existed && !with_type && !value.hasValue()) set_error("variable '" + name + "' has not been initialized", &location);
    return value;
}

void CifaBytecode::Machine::set_no_value_error(const Object& value, const SourceLocation* location)
{
    if (!error.empty()) return;
    const auto* no_value = std::any_cast<Object::NoValue>(&value.value);
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

bool CifaBytecode::Machine::condition(Object& value, const SourceLocation& location)
{
    if (value.getSpecialType() == "NoValue")
    {
        set_no_value_error(value, &location);
        return false;
    }
    if (!value.hasValue()) { set_error("condition requires a value", &location); return false; }
    return value.toBool();
}

bool CifaBytecode::Machine::range(Object& value, const SourceLocation& location, ObjectVector& values)
{
    if (value.getSpecialType() == "NoValue")
    {
        set_no_value_error(value, &location);
        return false;
    }
    if (!value.isType<ObjectVector>()) { set_error("range for requires an array", &location); return false; }
    values = value.ref<ObjectVector>();
    return true;
}

bool CifaBytecode::Machine::bind_range(Object& value, const std::string& name, const std::string& type_name, const SourceLocation& location)
{
    if (!bind_type(value, type_name, location)) return false;
    value.name = name;
    return true;
}

Object CifaBytecode::Machine::checked_index(const Object& element, bool map_access)
{
    if (!map_access && !element.hasValue()) set_error("array element '" + element.name + "' has not been initialized");
    return element;
}

Object CifaBytecode::Machine::binary(Opcode opcode, const Object& left, const Object& right, const SourceLocation& location)
{
    if ((opcode == Opcode::Divide || opcode == Opcode::Modulo) && left.isInteger() && right.isInteger() && right.toInt64() == 0)
    {
        set_error(opcode == Opcode::Divide ? "integer division by zero" : "integer modulo by zero", &location);
        return {};
    }
    if ((opcode == Opcode::ShiftLeft || opcode == Opcode::ShiftRight) && right.isInteger()
        && (right.toInt64() < 0 || right.toInt64() >= 64))
    {
        set_error(opcode == Opcode::ShiftLeft ? "left shift count is out of range" : "right shift count is out of range", &location);
        return {};
    }
    switch (opcode)
    {
    case Opcode::Add: return host.add(left, right);
    case Opcode::Subtract: return host.sub(left, right);
    case Opcode::Multiply: return host.mul(left, right);
    case Opcode::Divide: return host.div(left, right);
    case Opcode::Modulo: return host.mod(left, right);
    case Opcode::Less: return host.less(left, right);
    case Opcode::Greater: return host.more(left, right);
    case Opcode::LessEqual: return host.less_equal(left, right);
    case Opcode::GreaterEqual: return host.more_equal(left, right);
    case Opcode::Equal: return host.equal(left, right);
    case Opcode::NotEqual: return host.not_equal(left, right);
    case Opcode::BitAnd: return host.bit_and(left, right);
    case Opcode::BitOr: return host.bit_or(left, right);
    case Opcode::BitXor: return host.bit_xor(left, right);
    case Opcode::ShiftLeft: return host.shift_left(left, right);
    case Opcode::ShiftRight: return host.shift_right(left, right);
    case Opcode::LogicalAnd: return host.logic_and(left, right);
    case Opcode::LogicalOr: return host.logic_or(left, right);
    default: set_error("invalid bytecode binary operation", &location); return {};
    }
}

Object& CifaBytecode::Machine::indexed(const std::string& name, const std::string& type_name, size_t dimensions, bool is_decl_array,
    bool only_check, bool declare_current, const std::function<Object(size_t)>& index_value)
{
    auto& base = get_or_create(name, declare_current);
    if (is_decl_array)
    {
        const Object size_value = dimensions == 0 ? Object() : index_value(0);
        const auto requested = size_value.hasValue() ? size_value.toInt64() : 0;
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
        return base;
    }
    const Object first_key = dimensions == 0 ? Object() : index_value(0);
    if (!base.isType<ObjectVector>() && !base.isType<ObjectMap>())
    {
        base = first_key.isType<std::string>() ? Object(ObjectMap{}) : Object(ObjectVector{});
    }
    Object* current = &base;
    for (size_t index = 0; index < dimensions; ++index)
    {
        Object key = index == 0 ? first_key : index_value(index);
        if (!current->isType<ObjectVector>() && !current->isType<ObjectMap>())
        {
            const auto element_type = current->declared_type_name;
            *current = key.isType<std::string>() ? Object(ObjectMap{}) : Object(ObjectVector{});
            current->element_type_name = element_type;
        }
        if (current->isType<ObjectMap>()) current = &current->ref<ObjectMap>()[key.toString()];
        else
        {
            const auto offset = key.toInt64();
            if (offset < 0) { set_error("array index is out of range"); return *current; }
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
    current->name = name;
    return *current;
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

Object CifaBytecode::Machine::call_method(const std::string& name, const SourceLocation& location, Object& object,
    const std::vector<SourceLocation>& locations, const std::function<Object(size_t)>& argument_value)
{
    if (object.isType<ObjectVector>())
    {
        auto& values = object.ref<ObjectVector>();
        if (name == "push_back")
        {
            for (size_t index = 0; index < locations.size(); ++index)
            {
                auto value = argument_value(index);
                if (!object.element_type_name.empty()) value = convert_type(value, object.element_type_name, locations[index]);
                if (should_stop()) return {};
                values.push_back(std::move(value));
            }
            return Object(double(values.size()));
        }
        if (name == "pop_back")
        {
            if (!values.empty()) values.pop_back();
            return Object(double(values.size()));
        }
        if (name == "resize")
        {
            if (!locations.empty()) values.resize(static_cast<size_t>(argument_value(0).toInt64()));
            return Object(double(values.size()));
        }
        if (name == "insert")
        {
            if (locations.size() >= 2)
            {
                const auto requested = argument_value(0).toInt64();
                const auto position = static_cast<size_t>(std::clamp<int64_t>(requested, 0, static_cast<int64_t>(values.size())));
                auto value = argument_value(1);
                if (!object.element_type_name.empty()) value = convert_type(value, object.element_type_name, locations[1]);
                if (should_stop()) return {};
                values.insert(values.begin() + position, std::move(value));
            }
            return Object(double(values.size()));
        }
        if (name == "erase")
        {
            if (!locations.empty())
            {
                const auto index = argument_value(0).toInt64();
                if (index >= 0 && static_cast<size_t>(index) < values.size()) values.erase(values.begin() + index);
            }
            return Object(double(values.size()));
        }
        if (name == "clear") { values.clear(); return Object(0.0); }
        if (name == "contains")
        {
            if (!locations.empty())
            {
                const auto sought = argument_value(0);
                for (const auto& value : values) if (host.equal(value, sought).toBool()) return Object(1.0);
            }
            return Object(0.0);
        }
        if (name == "keys") set_error("keys() is not supported on arrays", &location);
        else set_error(name + "() is not supported on arrays", &location);
        return {};
    }
    if (object.isType<ObjectMap>())
    {
        auto& values = object.ref<ObjectMap>();
        if (name == "erase")
        {
            if (!locations.empty()) values.erase(argument_value(0).toString());
            return Object(double(values.size()));
        }
        if (name == "clear") { values.clear(); return Object(0.0); }
        if (name == "contains") return Object(!locations.empty() && values.contains(argument_value(0).toString()));
        if (name == "keys")
        {
            ObjectVector keys;
            for (const auto& [key, value] : values) keys.emplace_back(key);
            return Object(std::move(keys));
        }
        set_error(name + "() is not supported on maps", &location);
        return {};
    }
    set_error(name + "() requires an array or map", &location);
    return {};
}

Object CifaBytecode::Machine::push_back(const SourceLocation& location, Object& object, Object value)
{
    if (!object.isType<ObjectVector>())
    {
        set_error("push_back() requires an array or map", &location);
        return {};
    }
    if (!object.element_type_name.empty()) value = convert_type(value, object.element_type_name, location);
    if (should_stop()) return {};
    auto& values = object.ref<ObjectVector>();
    values.push_back(std::move(value));
    return Object(double(values.size()));
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

bool CifaBytecode::compile_script(std::string script)
{
    if (compiled_valid || !translation_error.empty()) return false;
    prepare_compile_visibility();
    const bool parsed = compile_script_internal(std::move(script));
    clear_compile_visibility();
    if (parsed) translate(*this);
    return valid() && !has_error();
}

bool CifaBytecode::compile_file(const std::string& filename)
{
    if (compiled_valid || !translation_error.empty()) return false;
    prepare_compile_visibility();
    const bool parsed = compile_file_internal(filename);
    clear_compile_visibility();
    if (parsed) translate(*this);
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
    prepare_compile_visibility();
    const bool parsed = compile_script_internal(std::move(script));
    clear_compile_visibility();
    if (!parsed || has_error()) return Object("", "Error");
    nested->translate(*this);
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
    prepare_compile_visibility();
    const bool parsed = compile_file_internal(filename);
    clear_compile_visibility();
    if (!parsed || has_error()) return Object("", "Error");
    nested->translate(*this);
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

