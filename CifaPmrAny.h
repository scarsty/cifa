#pragma once
#include <any>
#include <concepts>
#include <cstddef>
#include <cstring>
#include <initializer_list>
#include <memory>
#include <memory_resource>
#include <type_traits>
#include <typeinfo>
#include <utility>

namespace cifa::memory {

// 资源为借用指针，必须比 any 及其副本存活更久；不隐式持有栈 arena。
class PmrAny {
public:
    using allocator_type = std::pmr::polymorphic_allocator<std::byte>;
    static constexpr std::size_t inline_capacity = 4 * sizeof(void*);
    static constexpr std::size_t inline_alignment = alignof(std::max_align_t);

private:
    struct Operations {
        const std::type_info& (*type)() noexcept;
        void (*destroy)(PmrAny&) noexcept;
        void (*copy)(const PmrAny&, PmrAny&);
        void (*relocate)(PmrAny&, PmrAny&) noexcept;
        bool local;
    };
    alignas(inline_alignment) std::byte buffer_[inline_capacity]{};
    std::pmr::memory_resource* resource_;
    const Operations* operations_ = nullptr;

    template<class T> static constexpr bool local = sizeof(T) <= inline_capacity
        && alignof(T) <= inline_alignment && std::is_nothrow_move_constructible_v<T>;

    void* pointer() noexcept {
        if (operations_->local) return buffer_;
        void* result;
        std::memcpy(&result, buffer_, sizeof(result));
        return result;
    }
    const void* pointer() const noexcept { return const_cast<PmrAny*>(this)->pointer(); }

    template<class T> static const Operations* operations() {
        static const Operations value{
            []() noexcept -> const std::type_info& { return typeid(T); },
            [](PmrAny& self) noexcept {
                auto* object = static_cast<T*>(self.pointer());
                if constexpr (local<T>) std::destroy_at(object);
                else self.get_allocator().delete_object(object);
            },
            [](const PmrAny& source, PmrAny& destination) {
                destination.construct<T>(*static_cast<const T*>(source.pointer()));
            },
            [](PmrAny& source, PmrAny& destination) noexcept {
                if constexpr (local<T>) {
                    // 同资源转移：普通无异常 move 保留载荷已有的分配器。
                    std::construct_at(reinterpret_cast<T*>(destination.buffer_),
                        std::move(*static_cast<T*>(source.pointer())));
                    std::destroy_at(static_cast<T*>(source.pointer()));
                } else {
                    std::memcpy(destination.buffer_, source.buffer_, sizeof(void*));
                }
                destination.operations_ = source.operations_;
                source.operations_ = nullptr;
            },
            local<T>
        };
        return &value;
    }

    template<class T, class... Args> T& construct(Args&&... args) {
        T* object;
        if constexpr (local<T>) {
            object = std::uninitialized_construct_using_allocator(
                reinterpret_cast<T*>(buffer_), get_allocator(), std::forward<Args>(args)...);
        } else {
            object = get_allocator().template new_object<T>(std::forward<Args>(args)...);
            std::memcpy(buffer_, &object, sizeof(object));
        }
        operations_ = operations<T>();
        return *object;
    }

    void take(PmrAny& source) noexcept {
        if (source.operations_) source.operations_->relocate(source, *this);
    }

public:
    explicit PmrAny(std::pmr::memory_resource* resource = std::pmr::get_default_resource()) noexcept
        : resource_(resource ? resource : std::pmr::get_default_resource()) {}
    PmrAny(std::allocator_arg_t, allocator_type allocator) noexcept : PmrAny(allocator.resource()) {}

    // 普通复制保留源资源；指定资源的复制通过 uses-allocator 重新构造载荷。
    PmrAny(const PmrAny& source) : PmrAny(source, source.resource_) {}
    PmrAny(const PmrAny& source, std::pmr::memory_resource* resource) : PmrAny(resource) {
        if (source.operations_) source.operations_->copy(source, *this);
    }
    PmrAny(std::allocator_arg_t, allocator_type allocator, const PmrAny& source)
        : PmrAny(source, allocator.resource()) {}

    PmrAny(PmrAny&& source) noexcept : PmrAny(source.resource_) { take(source); }
    PmrAny(PmrAny&& source, std::pmr::memory_resource* resource) : PmrAny(resource) {
        if (resource_ == source.resource_) take(source);
        else {
            // 不同资源不能借走源存储。复制成功后才清空源，失败时源保持原值。
            if (source.operations_) source.operations_->copy(source, *this);
            source.reset();
        }
    }
    PmrAny(std::allocator_arg_t, allocator_type allocator, PmrAny&& source)
        : PmrAny(std::move(source), allocator.resource()) {}

    template<class T, class... Args> requires (std::same_as<T, std::decay_t<T>> && std::is_copy_constructible_v<T>)
    explicit PmrAny(std::in_place_type_t<T>, std::pmr::memory_resource* resource, Args&&... args)
        : PmrAny(resource) { construct<T>(std::forward<Args>(args)...); }
    template<class T, class... Args> requires (std::same_as<T, std::decay_t<T>> && std::is_copy_constructible_v<T>)
    PmrAny(std::allocator_arg_t, allocator_type allocator, std::in_place_type_t<T>, Args&&... args)
        : PmrAny(std::in_place_type<T>, allocator.resource(), std::forward<Args>(args)...) {}

    ~PmrAny() { reset(); }
    PmrAny& operator=(const PmrAny& source) {
        if (this != &source) {
            PmrAny replacement(source, resource_);
            reset();
            take(replacement);
        }
        return *this;
    }
    PmrAny& operator=(PmrAny&& source) {
        if (this != &source) {
            PmrAny replacement(std::move(source), resource_);
            reset();
            take(replacement);
        }
        return *this;
    }

    template<class T, class... Args> requires std::is_copy_constructible_v<std::decay_t<T>>
    std::decay_t<T>& emplace(Args&&... args) {
        reset();
        return construct<std::decay_t<T>>(std::forward<Args>(args)...);
    }
    template<class T, class U, class... Args> requires std::is_copy_constructible_v<std::decay_t<T>>
    std::decay_t<T>& emplace(std::initializer_list<U> values, Args&&... args) {
        reset();
        return construct<std::decay_t<T>>(values, std::forward<Args>(args)...);
    }
    void reset() noexcept {
        if (operations_) { operations_->destroy(*this); operations_ = nullptr; }
    }
    bool has_value() const noexcept { return operations_ != nullptr; }
    bool uses_inline_storage() const noexcept { return operations_ && operations_->local; }
    const std::type_info& type() const noexcept { return operations_ ? operations_->type() : typeid(void); }
    allocator_type get_allocator() const noexcept { return allocator_type(resource_); }
    PmrAny clone(std::pmr::memory_resource* resource) const { return PmrAny(*this, resource); }

    // 分配器不交换。不同资源时先完成两份复制，保证异常不改变任一原值。
    void swap(PmrAny& other) {
        if (this == &other) return;
        if (resource_ == other.resource_) {
            PmrAny temporary(std::move(*this));
            take(other);
            other.take(temporary);
        } else {
            PmrAny left(other, resource_);
            PmrAny right(*this, other.resource_);
            reset();
            other.reset();
            take(left);
            other.take(right);
        }
    }
    friend void swap(PmrAny& left, PmrAny& right) { left.swap(right); }

    template<class T> T* get_if() noexcept {
        static_assert(std::is_object_v<T>);
        return type() == typeid(T) ? static_cast<T*>(pointer()) : nullptr;
    }
    template<class T> const T* get_if() const noexcept {
        return const_cast<PmrAny*>(this)->template get_if<T>();
    }
};

template<class T> T* any_cast(PmrAny* value) noexcept { return value ? value->template get_if<T>() : nullptr; }
template<class T> const T* any_cast(const PmrAny* value) noexcept { return value ? value->template get_if<T>() : nullptr; }
template<class T> T any_cast(PmrAny& value) {
    using U = std::remove_cvref_t<T>;
    if (auto* object = any_cast<U>(&value)) return static_cast<T>(*object);
    throw std::bad_any_cast();
}
template<class T> T any_cast(const PmrAny& value) {
    using U = std::remove_cvref_t<T>;
    if (auto* object = any_cast<U>(&value)) return static_cast<T>(*object);
    throw std::bad_any_cast();
}
template<class T> T any_cast(PmrAny&& value) {
    using U = std::remove_cvref_t<T>;
    if (auto* object = any_cast<U>(&value)) return static_cast<T>(std::move(*object));
    throw std::bad_any_cast();
}
}
