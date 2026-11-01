/**
 * @file gc_safe.h
 * @brief Running GIAC computations in a GC-safe region
 *
 * A thread inside a long GIAC computation never reaches a Julia safepoint, so every other Julia
 * thread that needs a garbage collection waits for it. While a thread is in a GC-safe region,
 * Julia collects without it. The region covers only C++ work: arguments are converted before it
 * starts and the result is boxed for Julia after it ends, by CxxWrap, outside these wrappers.
 * Nothing called inside may touch Julia objects.
 */

#pragma once

#include <cstdlib>
#include <utility>
#include <gmp.h>
#include <julia.h>

namespace giac_julia {

namespace detail {
    // GMP's allocation through the C library, for while Julia's own functions must not be used
    inline void* plain_malloc(size_t n) { return std::malloc(n); }
    inline void* plain_realloc(void* p, size_t, size_t n) { return std::realloc(p, n); }
    inline void plain_free(void* p, size_t) { std::free(p); }
}

/// Marks the current thread GC-safe for its lifetime (also when an exception leaves it).
///
/// GIAC shares Julia's GMP, whose memory functions Julia sets to its counted allocators. Those
/// may run a collection or reach a safepoint, which a GC-safe thread must not do, so GMP
/// allocates through the C library for the duration. Both end in the same malloc, so memory
/// allocated one way may be freed the other; only Julia's count of GMP memory is approximate.
class GCSafeRegion {
public:
    GCSafeRegion() : ptls_(jl_get_current_task()->ptls) {
        mp_get_memory_functions(&alloc_, &realloc_, &free_);
        mp_set_memory_functions(&detail::plain_malloc, &detail::plain_realloc, &detail::plain_free);
        state_ = jl_gc_safe_enter(ptls_);
    }
    ~GCSafeRegion() {
        jl_gc_safe_leave(ptls_, state_);
        mp_set_memory_functions(alloc_, realloc_, free_);
    }
    GCSafeRegion(const GCSafeRegion&) = delete;
    GCSafeRegion& operator=(const GCSafeRegion&) = delete;

private:
    jl_ptls_t ptls_;
    int8_t state_ = 0;
    void* (*alloc_)(size_t) = nullptr;
    void* (*realloc_)(void*, size_t, size_t) = nullptr;
    void (*free_)(void*, size_t) = nullptr;
};

// Each returns a lambda with the wrapped function's exact signature, which CxxWrap registers like
// the function itself.

/// A function run in a GC-safe region.
template <typename R, typename... Args>
auto gc_safe(R (*f)(Args...)) {
    return [f](Args... args) -> R {
        GCSafeRegion region;
        return f(std::forward<Args>(args)...);
    };
}

/// A const member function run in a GC-safe region, taking the object as its first argument.
template <typename R, typename C, typename... Args>
auto gc_safe(R (C::*f)(Args...) const) {
    return [f](const C& obj, Args... args) -> R {
        GCSafeRegion region;
        return (obj.*f)(std::forward<Args>(args)...);
    };
}

/// A member function run in a GC-safe region, taking the object as its first argument.
template <typename R, typename C, typename... Args>
auto gc_safe(R (C::*f)(Args...)) {
    return [f](C& obj, Args... args) -> R {
        GCSafeRegion region;
        return (obj.*f)(std::forward<Args>(args)...);
    };
}

}  // namespace giac_julia
