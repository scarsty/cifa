#include "CifaBytecode.h"
#include <algorithm>
#include "../unit_test/test_process.h"
#include <chrono>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <thread>

using Clock = std::chrono::steady_clock;
double elapsed(Clock::time_point start) { return std::chrono::duration<double, std::milli>(Clock::now()-start).count(); }
int main(int argc, char** argv) try {
    configure_test_process();
    const int samples = argc > 1 ? std::stoi(argv[1]) : 7;
    if (samples < 1) throw std::runtime_error("sample count must be positive");
    const std::string workload = argc > 2 ? argv[2] : "pi";
    bool profile = false, pooled = true, count_allocations = false, listing = false, opcode_profile = false;
    for (int i=3;i<argc;++i) {
        const std::string flag=argv[i];
        if (flag=="--vm-only") profile=true;
        else if (flag=="--pool") pooled=true;
        else if (flag=="--no-pool") pooled=false;
        else if (flag=="--allocations") count_allocations=true;
        else if (flag=="--listing") listing=true;
        else if (flag=="--opcode-profile") opcode_profile=true;
        else if (flag=="--wait") std::this_thread::sleep_for(std::chrono::seconds(5));
        else throw std::runtime_error("Unknown option: "+flag);
    }
    std::string script;
    if (workload == "pi") {
        std::ifstream input("cifa/calc-pi.c");
        if (!input) throw std::runtime_error("Run from repository root");
        script.assign(std::istreambuf_iterator<char>(input), {});
        // Exclude console I/O from VM timings, retaining the returned PI string.
        for (size_t pos = 0; (pos = script.find("println(", pos)) != std::string::npos;) {
            const auto end = script.find('\n', pos);
            script.erase(pos, end == std::string::npos ? script.size()-pos : end-pos);
        }
    } else if (workload == "calls") {
        script = "add(a,b) { return a+b; } int total=0; for(int i=0;i<20000;i++) { total += add(i,1); } return total;";
    } else if (workload == "strings") {
        script = "echo(value) { return value+\"::tail\"; } base=\"" + std::string(128, 'x')
            + "\"; int total=0; for(int i=0;i<10000;i++) { total+=size(format(\"{0}:{1}\",echo(base),i)); } return total;";
    } else if (workload == "increment") {
        script = "int loop() { int value = 0; for (int i = 0; i < 1000000; i++) { value++; } return value; } return loop();";
    } else if (workload == "add") {
        script = "int loop() { int value = 0; for (int i = 0; i < 1000000; i++) { value = value + 1; } return value; } return loop();";
    } else if (workload == "addloop") {
        script = "int loop() { int value = 0; for (int i = 0; i < 1000000; i++) { value = value + i; } return value; } return loop();";
    } else if (workload == "empty") {
        script = "int loop() { for (int i = 0; i < 1000000; i++) { } return 1000000; } return loop();";
    } else if (workload == "incrementf") {
        script = "double loop() { double value = 0; for (double i = 0; i < 1000000; i++) { value++; } return value; } return loop();";
    } else if (workload == "intcompare") {
        script = "int loop() { int left = 1; int right = 2; int total = 0; for (int i = 0; i < 1000000; i++) { if (left < right) total++; } return total; } return loop();";
    } else if (workload == "intcompare_dynamic") {
        script = "int loop(left, right) { int total = 0; for (int i = 0; i < 1000000; i++) { if (left < right) total++; } return total; } return loop(1, 2);";
    } else throw std::runtime_error("workload must be pi, calls, strings, empty, increment, add, addloop, incrementf, intcompare, or intcompare_dynamic");
    auto upstream = std::make_shared<cifa::memory::CountingResource>(cifa::memory::default_resource());
    cifa::memory::Resource resource = count_allocations ? upstream : cifa::memory::default_resource();
    // Upstream is declared first and outlives this standard PMR pool.
    if (pooled) resource=std::make_shared<std::pmr::unsynchronized_pool_resource>(resource.get());
    auto requests = std::make_shared<cifa::memory::CountingResource>(resource);
    cifa::CifaBytecode vm(count_allocations ? requests : resource);
    vm.register_function("println", [](cifa::ObjectVector&) -> cifa::Object { return {}; });
    auto start=Clock::now();
    if (!vm.compile_script(script)) throw std::runtime_error(vm.get_errors_str());
    const double compile_ms=elapsed(start);
    const auto key = [](const cifa::Object& value) {
        return value.isNumber() ? std::to_string(value.toDouble()) : value.toString();
    };
    auto expected=key(vm.run());
    if(vm.has_runtime_error()) throw std::runtime_error(vm.get_runtime_error());
    if (workload == "calls" && expected != "200010000.000000") throw std::runtime_error("Incorrect call sum");
    if (workload == "strings" && expected != "1388890.000000") throw std::runtime_error("Incorrect string length sum");
    if (workload == "increment" && expected != "1000000.000000") throw std::runtime_error("Incorrect increment sum");
    if (workload == "add" && expected != "1000000.000000") throw std::runtime_error("Incorrect add sum");
    if (workload == "addloop" && expected != "499999500000.000000") throw std::runtime_error("Incorrect addloop sum");
    if (workload == "incrementf" && expected != "1000000.000000") throw std::runtime_error("Incorrect float increment sum");
    if (workload == "intcompare" && expected != "1000000.000000") throw std::runtime_error("Incorrect int compare sum");
    if (workload == "intcompare_dynamic" && expected != "1000000.000000") throw std::runtime_error("Incorrect dynamic int compare sum");
    if (workload == "pi" && (expected.size()!=502 || !expected.starts_with("3.141592653589793238462643383279"))) throw std::runtime_error("Incorrect PI result");
    if (opcode_profile) {
        vm.reset_opcode_profile();
        vm.set_opcode_profile_enabled(true);
        const auto profiled = vm.run();
        vm.set_opcode_profile_enabled(false);
        if (vm.has_runtime_error() || key(profiled) != expected) throw std::runtime_error("Opcode profile result mismatch");
        std::cout << "opcode_profile_begin\n" << vm.get_opcode_profile() << "opcode_profile_end\n";
    }
    if (!profile) {
        cifa::Cifa direct;
        direct.register_function("println", [](cifa::ObjectVector&) -> cifa::Object { return {}; });
        start=Clock::now();
        auto reference=key(direct.run_script(script));
        const double direct_ms=elapsed(start);
        if(direct.has_runtime_error() || reference!=expected) throw std::runtime_error("Direct/VM mismatch");
        std::cout << "direct_parse_execute_ms=" << direct_ms << '\n';
    }
    if (listing) std::cout << vm.dump_instruction_listing();
    std::cout << "workload=" << workload << " allocator=" << (pooled ? "pool" : "direct")
        << " compile_ms=" << compile_ms << " result_characters=" << expected.size() << '\n';
    std::vector<double> times;
    const auto before_requests=requests->statistics();
    const auto before_upstream=upstream->statistics();
    for(int i=0;i<samples;++i) {
        start=Clock::now();
        auto result=vm.run();
        const double ms=elapsed(start);
        if(vm.has_runtime_error() || key(result)!=expected) throw std::runtime_error("VM result mismatch");
        times.push_back(ms);
        std::cout << "sample=" << i+1 << " execute_ms=" << ms << '\n';
    }
    std::sort(times.begin(),times.end());
    std::cout << "PASS samples=" << samples << " median_ms=" << (times[(samples-1)/2]+times[samples/2])/2 << " min_ms=" << times.front() << " max_ms=" << times.back() << '\n';
    if (count_allocations) {
        const auto after=requests->statistics();
        const auto heap=upstream->statistics();
        std::cout << "allocator=" << (pooled ? "pool" : "direct")
            << " measured_resource_requests=" << after.allocations-before_requests.allocations
            << " measured_upstream_allocations=" << heap.allocations-before_upstream.allocations
            << " measured_upstream_bytes=" << heap.allocated_bytes-before_upstream.allocated_bytes
            << " upstream_peak_bytes=" << heap.peak_bytes << '\n';
    }
    return 0;
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
