#include "CifaBytecode.h"
#include "../../unit_test/test_process.h"
extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"
}
#include <algorithm>
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <stdexcept>
#include <vector>

using Digits = std::vector<std::int64_t>;
static Digits multiply(const Digits& a) {
    Digits result;
    std::int64_t carry = 0;
    for (auto digit : a) {
        auto value = carry + digit * 239;
        result.push_back(value % 10000);
        carry = value / 10000;
    }
    while (carry) { result.push_back(carry % 10000); carry /= 10000; }
    return result;
}
static Digits divide(const Digits& a) {
    Digits result(a.size());
    std::int64_t remainder = 0;
    for (size_t i = a.size(); i-- > 0;) {
        auto value = remainder * 10000 + a[i];
        result[i] = value / 239;
        remainder = value % 239;
    }
    while (result.size() > 1 && result.back() == 0) result.pop_back();
    return result;
}
static std::string expected(int mode, int rounds) {
    if (mode >= 3) {
        const std::int64_t first = mode == 6 ? 19 : 9007199254740993LL;
        if (mode == 3 || mode == 7) return std::to_string(((first + 128) % 1000003) * rounds);
        auto output = std::to_string(((first % 1000003) + ((first + 127) % 1000003)) * rounds);
        for (int i = 0; i < 128; ++i) output += ":" + std::to_string(first + i);
        return output;
    }
    Digits a;
    for (int i = 0; i < 128; ++i) a.push_back((i * 73 + 19) % 10000);
    auto result = mode == 0 ? multiply(a) : mode == 1 ? divide(a) : divide(multiply(a));
    if (mode == 2 && result != a) throw std::runtime_error("Oracle roundtrip failed");
    auto output = std::to_string((result.front() + result.back()) * rounds);
    for (auto digit : result) output += ":" + std::to_string(digit);
    return output;
}
static std::string read(const std::string& path) {
    std::ifstream input(path);
    if (!input) throw std::runtime_error("Cannot read " + path + "; run from repository root");
    return {std::istreambuf_iterator<char>(input), {}};
}
int main(int argc, char** argv) try {
    configure_test_process();
    const std::string engine = argc > 1 ? argv[1] : "pool";
    const int mode = argc > 2 ? std::stoi(argv[2]) : 2;
    const int rounds = argc > 3 ? std::stoi(argv[3]) : 1000;
    const int samples = argc > 4 ? std::stoi(argv[4]) : 15;
    if ((engine != "cifa" && engine != "pool" && engine != "lua") || mode < 0 || mode > 7 || rounds < 1 || samples < 1)
        throw std::runtime_error("Usage: big_mult_div [pool(default)|cifa|lua] [0=mul|1=div|2=roundtrip|3=scalar|4=copy|5=append-large|6=append-small|7=scalar-no-scope] [rounds>0] [samples>0]");
    const auto reference = expected(mode, rounds);
    auto resource = cifa::memory::default_resource();
    if (engine == "pool") resource = std::make_shared<std::pmr::unsynchronized_pool_resource>(resource.get());
    cifa::CifaBytecode vm(resource);
    std::unique_ptr<lua_State, decltype(&lua_close)> lua(luaL_newstate(), &lua_close);
    if (!lua) throw std::bad_alloc();
    int function = LUA_NOREF;
    const std::string entry = engine == "lua" || mode < 3 ? "benchmark"
        : mode == 3 ? "scalar_benchmark" : mode == 7 ? "scalar_no_scope_benchmark"
        : mode == 4 ? "copy_benchmark" : "append_benchmark";
    const auto invocation = "return " + entry + "(" + std::to_string(mode) + "," + std::to_string(rounds) + ")";
    if (engine == "lua") {
        luaL_openlibs(lua.get());
        const auto script = read("benchmarks/big_mult_div/workload.lua") + "\n" + invocation;
        if (luaL_loadbuffer(lua.get(), script.data(), script.size(), "big_mult_div"))
            throw std::runtime_error(lua_tostring(lua.get(), -1));
        function = luaL_ref(lua.get(), LUA_REGISTRYINDEX);
    } else {
        const auto script = read(mode < 3 ? "benchmarks/big_mult_div/workload.c" : "benchmarks/big_mult_div/storage.c") + "\n" + invocation + ";";
        if (!vm.compile_script(script)) throw std::runtime_error("Cifa compile failed: " + vm.get_errors_str() + vm.get_translation_error());
        cifa::Cifa direct;
        auto result = direct.run_script(script).toString();
        if (direct.has_runtime_error() || result != reference) throw std::runtime_error("AST oracle mismatch: " + direct.get_runtime_error());
    }
    std::vector<double> times;
    for (int sample = -1; sample < samples; ++sample) {
        if (engine == "lua") lua_rawgeti(lua.get(), LUA_REGISTRYINDEX, function);
        const auto start = std::chrono::steady_clock::now();
        cifa::Object value;
        int status = 0;
        if (engine == "lua") status = lua_pcall(lua.get(), 0, 1, 0);
        else value = vm.run();
        const double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
        if (status) throw std::runtime_error(lua_tostring(lua.get(), -1));
        if (engine != "lua" && vm.has_runtime_error()) throw std::runtime_error(vm.get_runtime_error());
        std::string output;
        if (engine == "lua") {
            size_t length = 0;
            const auto* text = lua_tolstring(lua.get(), -1, &length);
            if (!text) throw std::runtime_error("Lua returned non-string");
            output.assign(text, length);
            lua_pop(lua.get(), 1);
        } else output = value.toString();
        if (output != reference) throw std::runtime_error("Native oracle mismatch");
        if (sample >= 0) { times.push_back(ms); std::cout << "sample=" << sample + 1 << " execute_ms=" << ms << '\n'; }
    }
    std::sort(times.begin(), times.end());
    const double median = (times[(samples - 1) / 2] + times[samples / 2]) / 2;
    std::cout << "PASS engine=" << engine << " mode=" << mode << " rounds=" << rounds
        << " samples=" << samples << " median_ms=" << median << " us_per_round=" << median * 1000 / rounds
        << " result_characters=" << reference.size() << '\n';
    return 0;
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
