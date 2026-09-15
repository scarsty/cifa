#pragma once
#include <memory>
#include <memory_resource>
#include <vector>
#include <deque>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <limits>
#include <utility>
#include <array>
#include <string>
#include <string_view>
#include <algorithm>
#include <tuple>

namespace cifa::memory {
// Shared ownership is deliberate: COW values and compiled modules may outlive
// the execution that created them. Never retain a pointer to a dead stack arena.
using Resource = std::shared_ptr<std::pmr::memory_resource>;
struct Statistics {
    size_t allocations = 0, deallocations = 0, allocated_bytes = 0;
    size_t outstanding_bytes = 0, peak_bytes = 0;
};
// Counts requests to a resource, not opaque host/Object/std::any allocations.
class CountingResource : public std::pmr::memory_resource {
    Resource upstream;
    Statistics counters;
    void* do_allocate(size_t bytes, size_t alignment) override {
        void* result = upstream->allocate(bytes,alignment);
        ++counters.allocations; counters.allocated_bytes += bytes;
        counters.outstanding_bytes += bytes;
        counters.peak_bytes = (std::max)(counters.peak_bytes,counters.outstanding_bytes);
        return result;
    }
    void do_deallocate(void* pointer,size_t bytes,size_t alignment) override {
        upstream->deallocate(pointer,bytes,alignment);
        ++counters.deallocations; counters.outstanding_bytes -= bytes;
    }
    bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override { return this == &other; }
public:
    explicit CountingResource(Resource value) : upstream(std::move(value)) {}
    Statistics statistics() const { return counters; }
};
inline const Resource& default_resource() {
    static Resource resource(std::pmr::new_delete_resource(), [](auto*) {});
    return resource;
}
// Heterogeneous lookup avoids temporary strings at diagnostic-name boundaries.
struct StringHash {
    using is_transparent = void;
    size_t operator()(std::string_view value) const noexcept { return std::hash<std::string_view>{}(value); }
};
struct StringEqual {
    using is_transparent = void;
    bool operator()(std::string_view left,std::string_view right) const noexcept { return left==right; }
};
}
