# runtests.jl
# Main test runner for GIAC Julia wrapper.
#
# Each `test_*.jl` includes `load_wrapper.jl` at its top; `load_wrapper.jl`
# is guarded against re-loading so the `@wrapmodule` call only fires once
# per Julia session. That makes both `julia tests/julia/runtests.jl` and
# `julia tests/julia/test_<one>.jl` work, sharing the same loaded module.
# Runtests itself does NOT load the wrapper — the first included test file
# does, and the other 11 no-op through the guard.

using Test

@testset "GIAC Julia Wrapper" begin
    include("test_eval.jl")
    include("test_context.jl")
    include("test_gen.jl")
    include("test_warnings.jl")
    include("test_introspection.jl")
    include("test_indexing.jl")
    include("test_iteration.jl")
    include("test_to_julia.jl")
    include("test_display.jl")
    include("test_symbolic.jl")
    include("test_highlevel.jl")
    include("test_convert.jl")
    include("test_transfer.jl")
end
