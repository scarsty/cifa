#pragma once

#include "Cifa.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace cifa {

struct Proto;
struct Program;

// Compiles Cifa's checked AST directly to a Lua 5.4 binary chunk and executes
// it using Lua's register VM.
class CifaBytecode : public Cifa {
public:
    CifaBytecode() = default;
    ~CifaBytecode() = default;

    CifaBytecode(const CifaBytecode&) = delete;
    CifaBytecode& operator=(const CifaBytecode&) = delete;

    bool compile_script(std::string script);
    bool compile_file(const std::string& filename);
    Object run(const std::string& entry_label = {});
    Object run_script(std::string script);
    Object run_file(const std::string& filename);

    bool valid() const { return program_ != nullptr && translation_error_.empty(); }
    const std::string& get_translation_error() const { return translation_error_; }
    std::string get_runtime_error() const { return runtime_error_; }
    bool has_runtime_error() const { return !runtime_error_.empty(); }
    bool is_exit_requested() const { return Cifa::is_exit_requested(); }
    const std::vector<std::uint8_t>& emitted_chunk() const { return chunk_; }

private:
    class FunctionCompiler;

    bool emit_chunk();
    Object execute_chunk();
    std::shared_ptr<Proto> compile_lua_program(std::vector<std::uint8_t>& chunk, std::string& error) const;

    std::string translation_error_;
    std::string runtime_error_;
    std::vector<std::uint8_t> chunk_;
    std::shared_ptr<Program> program_;
};

} // namespace cifa
