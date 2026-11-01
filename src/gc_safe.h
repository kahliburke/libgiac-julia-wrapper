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
    inline void* plain_malloc(size_t n) { return std::malloc(n); }
    inline void* plain_realloc(void* p, size_t, size_t n) { return std::realloc(p, n); }
    inline void plain_free(void* p, size_t) { std::free(p); }
}

/// Make GMP allocate through the C library, once, when the module loads.
///
/// GIAC shares Julia's GMP, whose memory functions Julia sets to its counted allocators. Those may
/// run a collection or reach a safepoint, which a thread in a GC-safe region must not do. The
/// functions are process-wide, so switching them per call would mix the two ways between threads:
/// memory counted when allocated and not when freed, or the reverse, corrupts Julia's heap size
/// estimate. With the C library throughout, GMP memory (Julia's BigInt buffers included) is simply
/// not counted towards Julia's collections; both end in the same malloc, so a buffer allocated
/// before the switch is still freed correctly.
inline void use_plain_gmp_allocation() {
    mp_set_memory_functions(&detail::plain_malloc, &detail::plain_realloc, &detail::plain_free);
}

/// Marks the current thread GC-safe for its lifetime (also when an exception leaves it).
class GCSafeRegion {
public:
    GCSafeRegion() : ptls_(jl_get_current_task()->ptls), state_(jl_gc_safe_enter(ptls_)) {}
    ~GCSafeRegion() { jl_gc_safe_leave(ptls_, state_); }
    GCSafeRegion(const GCSafeRegion&) = delete;
    GCSafeRegion& operator=(const GCSafeRegion&) = delete;

private:
    jl_ptls_t ptls_;
    int8_t state_;
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
