#include "CifaBytecode.h"
#include <iostream>
#include "test_process.h"
#include <stdexcept>
#include <unordered_map>
#include <stacktrace>

using namespace cifa;
void require(bool value,const char* message) { if(!value) throw std::runtime_error(message); }
class CheckedResource : public std::pmr::memory_resource {
    std::unordered_map<void*,std::pair<size_t,size_t>> live;
    void* do_allocate(size_t bytes,size_t alignment) override {
        if (allocations == fail_after) throw std::bad_alloc();
        void* p=std::pmr::new_delete_resource()->allocate(bytes,alignment);
        require(reinterpret_cast<std::uintptr_t>(p)%alignment==0,"alignment");
        live.emplace(p,std::pair{bytes,alignment}); ++allocations; return p;
    }
    void do_deallocate(void* p,size_t bytes,size_t alignment) override {
        const auto found=live.find(p);
        require(found!=live.end() && found->second==std::pair{bytes,alignment},"wrong allocation domain/size");
        live.erase(found); std::pmr::new_delete_resource()->deallocate(p,bytes,alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this==&other; }
public:
    size_t allocations=0;
    size_t fail_after=std::numeric_limits<size_t>::max();
    bool empty() const { return live.empty(); }
    size_t live_bytes() const { size_t result=0; for(const auto& entry:live) result+=entry.second.first; return result; }
};

struct RejectDefaultResource : std::pmr::memory_resource {
    void* do_allocate(size_t,size_t) override { std::cerr << std::stacktrace::current() << '\n'; throw std::bad_alloc(); }
    void do_deallocate(void*,size_t,size_t) override {}
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this==&other; }
};
struct DefaultResourceGuard {
    RejectDefaultResource rejected;
    std::pmr::memory_resource* previous=std::pmr::set_default_resource(&rejected);
    ~DefaultResourceGuard() { std::pmr::set_default_resource(previous); }
};

int main() try {
    configure_test_process();
    DefaultResourceGuard explicit_resources_only;
    auto upstream=std::make_shared<CheckedResource>();
    for(bool pooled : {false,true}) {
        memory::Resource domain=upstream;
        if(pooled) domain=std::make_shared<std::pmr::unsynchronized_pool_resource>(upstream.get());
        std::weak_ptr<std::pmr::memory_resource> weak=domain;
        Object escaped;
        {
            CifaBytecode vm(domain);
            vm.set_output_error(false);
            if(!vm.compile_script("a={1,2,3}; b=a; b[0]=9; result={a,b}; return result;"))
                throw std::runtime_error(vm.get_errors_str());
            escaped=vm.run();
            require(!vm.has_runtime_error(),"COW run");
            require(escaped.isType<ObjectVector>(),"host array type");
            const auto& values=escaped.ref<ObjectVector>();
            require(values[0].ref<ObjectVector>()[0].toInt()==1 && values[1].ref<ObjectVector>()[0].toInt()==9,"COW isolation");
            // Nested execution re-enters the same machine and constructs new scopes/registers.
            vm.register_native_function("child",[&](CifaBytecode::NativeCallContext& context) {
                context.set_result(std::int64_t(vm.run_script("shared=shared+1; return shared;").toInt()));
            });
            auto nested_result=vm.run_script("shared=1; int f(n) { if(n==0) return child(); return f(n-1)+1; } return f(40);");
            if(nested_result.toInt()!=42) throw std::runtime_error("nested recursive execution: "+vm.get_errors_str()+vm.get_runtime_error());
            require(!vm.has_runtime_error(),"nested error");
            vm.run_script("int missing; return missing+1;");
            require(vm.has_runtime_error(),"error path");
            require(vm.run_script("return 7;").toInt()==7,"error recovery");
            CifaBytecode repeated(domain);
            require(repeated.compile_script("add(a,b) { return a+b; } int total=0; for(int i=0;i<20000;i++) { total+=add(i,1); } return total;"),"repeated call compile");
            require(repeated.run().toInt()==200010000,"call result");
            const auto retained=upstream->live_bytes();
            for(int i=0;i<4;++i) require(repeated.run().toInt()==200010000,"repeated call result");
            require(upstream->live_bytes()==retained,"execution scratch released / pool stabilizes");
        }
        domain.reset();
        if(pooled) require(weak.expired(),"exported host arrays must not retain VM pool");
        require(escaped.ref<ObjectVector>()[1].ref<ObjectVector>()[0].toInt()==9,"result outlives VM");
        require(upstream->empty(),"outstanding VM allocations");
    }
    for (bool pooled : {false, true}) {
        memory::Resource resource = upstream;
        if (pooled) resource = std::make_shared<std::pmr::unsynchronized_pool_resource>(upstream.get());
        const std::string long_text(512, 'x');
        Object result;
        {
            CifaBytecode vm(resource);
            vm.register_parameter("input", Object(long_text));
            vm.register_parameter("lookup", ObjectMap{{long_text, Object(long_text)}});
            vm.register_native_function("echo_nested", [&](CifaBytecode::NativeCallContext& context) {
                require(context.is_string(0), "native PMR string type");
                const auto saved = context.to_string(0);
                vm.run_script("nested_marker=42; return nested_marker;");
                context.set_result(saved + "!");
            });
            result = vm.run_script(R"(
                a={input}; b=a; b[0]=b[0]+"!";
                lookup_copy=lookup; lookup_copy[input]=b[0];
                pattern="{0}:{1}"; pattern=format(pattern,a[0],b[0]);
                native_result=echo_nested(a[0]);
                auto inferred=input; string typed=input;
                result={a[0],b[0],lookup[input],lookup_copy[input],pattern,native_result,type(inferred),typed};
                return result;
            )");
            require(!vm.has_error() && !vm.has_runtime_error(), "long string script");
            require(vm.run_script("return 7;").toInt()==7, "replace string module");
        }
        resource.reset();
        const auto& values = result.ref<ObjectVector>();
        require(values.size()==8 && values[0].toString()==long_text && values[1].toString()==long_text+"!", "long string COW");
        require(values[2].toString()==long_text && values[3].toString()==long_text+"!", "PMR map keys and COW");
        require(values[4].toString()==long_text+":"+long_text+"!" && values[5].toString()==long_text+"!", "format views and reentrant native string");
        require(values[6].toString()=="string" && values[7].toString()==long_text, "string metadata and public export");
        require(upstream->empty(), "escaped public strings release VM resource");
    }
    require(upstream->allocations>100 && upstream->empty(),"allocation coverage and release");
    {
        auto resource=std::make_shared<CheckedResource>();
        std::weak_ptr<CheckedResource> weak=resource;
        auto vm=std::make_unique<CifaBytecode>(resource);
        resource.reset();
        require(!weak.expired(),"VM retains resource");
        require(vm->run_script("a={1,2,3}; return a[1];").toInt()==2,"retained resource usable");
        vm.reset();
        require(weak.expired(),"VM releases resource");
    }
    // MSVC's checked-iterator _Hash_vec constructor is noexcept but allocates a
    // proxy. Injecting failure there terminates in the STL before we can unwind.
    // Exercise root failure in checked builds, deeper VM failures in Release.
#if defined(_MSC_VER) && _ITERATOR_DEBUG_LEVEL > 0
    const size_t failure_limits[] = {0,1};
#else
    const size_t failure_limits[] = {0,1,16,100};
#endif
    for(size_t limit : failure_limits) {
        auto failing=std::make_shared<CheckedResource>();
        failing->fail_after=limit;
        bool caught=false;
        try {
            CifaBytecode vm(failing);
            vm.compile_script("data={1,2,3}; return data;");
        } catch(const std::bad_alloc&) { caught=true; }
        require(caught && failing->empty(),"allocation failure cleanup");
    }
    std::cout << "PASS allocator ownership, nesting, COW, error recovery, alignment, and release\n";
} catch(const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
