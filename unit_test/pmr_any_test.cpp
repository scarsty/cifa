#include "../CifaBytecode.h"
#include "test_process.h"
#include <array>
#include <cstdint>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

using cifa::memory::PmrAny;
using cifa::memory::any_cast;
void require(bool condition, const char* message) { if (!condition) throw std::runtime_error(message); }
struct CheckedResource : std::pmr::memory_resource {
    std::unordered_map<void*, std::pair<size_t, size_t>> live;
    size_t allocations = 0;
    size_t fail_after = SIZE_MAX;
    void* do_allocate(size_t bytes, size_t alignment) override {
        if (allocations == fail_after) throw std::bad_alloc();
        auto* result = std::pmr::new_delete_resource()->allocate(bytes, alignment);
        live.emplace(result, std::pair(bytes, alignment));
        ++allocations;
        return result;
    }
    void do_deallocate(void* pointer, size_t bytes, size_t alignment) override {
        auto it = live.find(pointer);
        require(it != live.end() && it->second == std::pair(bytes, alignment), "deallocation resource/size/alignment");
        live.erase(it);
        std::pmr::new_delete_resource()->deallocate(pointer, bytes, alignment);
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
};
struct Small {
    static inline int instances = 0;
    int value;
    explicit Small(int v) : value(v) { ++instances; }
    Small(const Small& other) : Small(other.value) {}
    Small(Small&& other) noexcept : Small(other.value) {}
    ~Small() { --instances; }
};
struct Large { std::array<int, 100> data{}; };
struct alignas(128) Aligned { int value = 42; };
struct ThrowingMove {
    int value = 9;
    ThrowingMove() = default;
    ThrowingMove(const ThrowingMove&) = default;
    ThrowingMove(ThrowingMove&&) { throw std::runtime_error("move must not be called"); }
};
struct ThrowingCopy {
    static inline bool fail = false;
    int value = 7;
    ThrowingCopy() = default;
    ThrowingCopy(const ThrowingCopy& other) : value(other.value) { if (fail) throw std::runtime_error("copy"); }
    ThrowingCopy(ThrowingCopy&&) noexcept = default;
};
// 验证 inline 载荷也能收到分配器，复制时不依赖普通复制构造函数。
struct Aware {
    using allocator_type = PmrAny::allocator_type;
    std::pmr::memory_resource* resource;
    int value;
    Aware(std::allocator_arg_t, allocator_type a, int v) : resource(a.resource()), value(v) {}
    Aware(const Aware&) = default;
    Aware(Aware&&) noexcept = default;
    Aware(std::allocator_arg_t, allocator_type a, const Aware& other) : Aware(std::allocator_arg, a, other.value) {}
};
struct RejectDefault {
    std::pmr::memory_resource* previous = std::pmr::set_default_resource(std::pmr::null_memory_resource());
    ~RejectDefault() { std::pmr::set_default_resource(previous); }
};

int main() try {
    configure_test_process();
    CheckedResource first, second;
    RejectDefault guard;
    {
        PmrAny a(&first);
        require(!a.has_value() && a.type() == typeid(void), "empty");
        require(any_cast<int>(&a) == nullptr && any_cast<int>(static_cast<PmrAny*>(nullptr)) == nullptr, "empty cast");
        a.emplace<Small>(42);
        require(a.uses_inline_storage() && first.allocations == 0, "small value must not allocate");
        PmrAny b(a);
        PmrAny c(std::move(a));
        require(!a.has_value() && Small::instances == 2 && any_cast<Small&>(c).value == 42, "inline copy/move");
        b = b;
        b = std::move(b);
        require(Small::instances == 2, "self assignment");
        bool bad_cast = false;
        try { (void)any_cast<double>(b); } catch (const std::bad_any_cast&) { bad_cast = true; }
        require(bad_cast, "wrong cast");
        const PmrAny& view = b;
        require(any_cast<const Small&>(view).value == 42, "const cast");
    }
    require(Small::instances == 0 && first.live.empty(), "inline destruction");
    {
        PmrAny a(std::in_place_type<Large>, &first);
        require(!a.uses_inline_storage() && first.live.size() == 1, "large allocation");
        auto* pointer = any_cast<Large>(&a);
        PmrAny b(std::move(a));
        require(any_cast<Large>(&b) == pointer && !a.has_value(), "heap move steals storage");
        auto c = b.clone(&second);
        require(any_cast<Large>(&c) != pointer && second.live.size() == 1, "heap copy destination");
        a.emplace<Aligned>();
        require(!a.uses_inline_storage() && reinterpret_cast<uintptr_t>(any_cast<Aligned>(&a)) % 128 == 0, "overaligned allocation");
        a.emplace<ThrowingMove>();
        require(!a.uses_inline_storage(), "throwing move must be out of line");
        PmrAny d(std::move(a));
        require(any_cast<ThrowingMove&>(d).value == 9, "no payload move for heap transfer");
    }
    require(first.live.empty() && second.live.empty(), "heap destruction");
    {
        PmrAny a(&first);
        a.emplace<Aware>(11);
        require(a.uses_inline_storage() && any_cast<Aware&>(a).resource == &first, "inline allocator injection");
        auto b = a.clone(&second);
        require(any_cast<Aware&>(b).resource == &second, "inline allocator copy");
        a.emplace<std::pmr::string>(400, 'x');
        require(any_cast<std::pmr::string&>(a).get_allocator().resource() == &first, "string allocator");
        b = a;
        require(b.get_allocator().resource() == &second && any_cast<std::pmr::string&>(b).get_allocator().resource() == &second, "copy assignment preserves destination resource");
        b = std::move(a);
        require(!a.has_value() && any_cast<std::pmr::string&>(b).size() == 400, "cross resource move");
        a.emplace<std::pmr::vector<std::pmr::string>>();
        any_cast<std::pmr::vector<std::pmr::string>&>(a).emplace_back(400, 'y');
        b = a;
        auto& nested = any_cast<std::pmr::vector<std::pmr::string>&>(b);
        require(nested.get_allocator().resource() == &second && nested[0].get_allocator().resource() == &second, "nested allocator propagation");
        std::pmr::vector<PmrAny> values(&second);
        values.push_back(a);
        values.emplace_back(std::in_place_type<Aware>, 17);
        require(values[0].get_allocator().resource() == &second && any_cast<Aware&>(values[1]).resource == &second, "PMR container propagation into any");
        for (int i = 0; i < 100; ++i) values.push_back(a);
        for (const auto& value : values) require(value.get_allocator().resource() == &second, "vector growth allocator");
        a.emplace<std::pmr::vector<int>>({1,2,3});
        require(any_cast<std::pmr::vector<int>&>(a)[2] == 3, "initializer list");
    }
    require(first.live.empty() && second.live.empty(), "nested destruction");
    {
        PmrAny destination(&second);
        {
            alignas(std::max_align_t) std::byte buffer[2048];
            std::pmr::monotonic_buffer_resource scratch(buffer, sizeof(buffer), std::pmr::null_memory_resource());
            PmrAny temporary(&scratch);
            temporary.emplace<std::pmr::string>(500, 'z');
            destination = std::move(temporary);
        }
        require(any_cast<std::pmr::string&>(destination) == std::pmr::string(500, 'z', &second), "escape from destroyed scratch");
    }
    {
        PmrAny source(&first), target(&second);
        source.emplace<ThrowingCopy>();
        target.emplace<int>(123);
        ThrowingCopy::fail = true;
        bool caught = false;
        try { target = std::move(source); } catch (const std::runtime_error&) { caught = true; }
        ThrowingCopy::fail = false;
        require(caught && any_cast<int>(target) == 123 && source.has_value(), "failed transfer preserves source and destination");
    }
    size_t copy_allocations;
    {
        CheckedResource probe;
        PmrAny source(&first);
        source.emplace<std::pmr::string>(500, 'q');
        auto copy = source.clone(&probe);
        copy_allocations = probe.allocations;
    }
    for (size_t budget = 0; budget < copy_allocations; ++budget) {
        CheckedResource failing;
        {
            PmrAny source(&first), target(&failing);
            source.emplace<std::pmr::string>(500, 'q');
            target.emplace<int>(123);
            failing.fail_after = budget;
            bool caught = false;
            try { target = source; } catch (const std::bad_alloc&) { caught = true; }
            require(caught && any_cast<int>(target) == 123 && failing.live.empty(), "allocation failure cleanup");
        }
    }
    {
        PmrAny a(&first), b(&first), c(&second);
        a.emplace<Small>(1);
        b.emplace<Large>();
        auto* original = any_cast<Large>(&b);
        auto allocations = first.allocations;
        swap(a, b);
        require(any_cast<Large>(&a) == original && any_cast<Small&>(b).value == 1 && first.allocations == allocations, "same resource swap without allocation");
        c.emplace<Aware>(2);
        swap(b, c);
        require(any_cast<Aware&>(b).resource == &first && any_cast<Small&>(c).value == 1, "cross resource swap");
        a.emplace<int>(3);
        b.emplace<ThrowingCopy>();
        c.emplace<int>(4);
        ThrowingCopy::fail = true;
        bool caught = false;
        try { swap(b, c); } catch (const std::runtime_error&) { caught = true; }
        ThrowingCopy::fail = false;
        require(caught && b.type() == typeid(ThrowingCopy) && any_cast<int>(c) == 4, "failed swap preserves values");
        bool emplace_failed = false;
        try { a.emplace<std::pmr::string>(SIZE_MAX, 'x'); } catch (const std::exception&) { emplace_failed = true; }
        require(emplace_failed && !a.has_value(), "failed emplace leaves empty");
    }
    require(Small::instances == 0, "all small payloads destroyed");
    require(first.live.empty() && second.live.empty(), "all resources released");
    std::cout << "PASS PMR any: inline/heap, alignment, allocator propagation, transfers, exceptions\n";
} catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
