# test_transfer.jl
# Tests for batched tree transfer (gen_export, gen_import), GC-safe computations and
# interruption

include(joinpath(@__DIR__, "load_wrapper.jl"))

# a gen_import program: each item a tag byte or a value, little-endian, strings length-prefixed
function _program(items...)
    io = IOBuffer()
    for it in items
        it isa AbstractString ? (write(io, UInt32(ncodeunits(it))); write(io, it)) : write(io, it)
    end
    return String(take!(io))
end

_bytes(s) = Vector{UInt8}(codeunits(String(s)))

@testset "GIAC Wrapper Transfer Tests" begin
    @testset "Import" begin
        none = CxxWrap.StdVector{Gen}()
        # x + 2*y: a sum of an identifier and a product
        p = _program(0x0b, UInt32(2), 0x04, "x", 0x0e, 0x01, Int64(2), 0x04, "y")
        @test to_string(gen_import(p, none)) == to_string(Gen("x+2*y"))
        # -2^64 from its magnitude bytes, most significant first
        p = _program(0x02, Int32(-1), UInt32(9), 0x01, zeros(UInt8, 8))
        @test to_string(gen_import(p, none)) == "-18446744073709551616"
        # a call is built unevaluated
        @test to_string(gen_import(_program(0x07, "sin", UInt32(1), 0x01, Int64(0)), none)) == "sin(0)"
        # an object passed through by index
        objects = CxxWrap.StdVector{Gen}()
        push!(objects, Gen("[1,2]"))
        @test to_string(gen_import(_program(0x09, UInt32(0)), objects)) == "[1,2]"
        @test_throws Exception gen_import(_program(0x09, UInt32(1)), objects)
        @test_throws Exception gen_import(_program(0x63), none)
        @test_throws Exception gen_import(_program(0x01, Int64(1), 0x01), none)
    end

    @testset "Export" begin
        @test _bytes(gen_export(Gen("5"), "")) == _bytes(_program(0x01, Int64(5)))
        @test _bytes(gen_export(Gen("x+y"), "")) == _bytes(_program(0x07, "+", UInt32(2), 0x04, "x", 0x04, "y"))
        @test _bytes(gen_export(Gen("[1,2]"), "")) == _bytes(_program(0x09, UInt32(0)))
        @test to_string(gen_export_opaque(Int32(0))) == "[1,2]"
    end

    @testset "Interruption" begin
        giac_clear_interrupt()
        @test !giac_interrupt_pending()
        giac_interrupt()
        @test giac_interrupt_pending()
        giac_clear_interrupt()
        @test !giac_interrupt_pending()
    end

    # Julia's big integers share GIAC's GMP, which allocates through the C library
    @test string(big(2)^100) == "1267650600228229401496703205376"
    @test big(2)^200 == big(2)^100 * big(2)^100

    @testset "GC-safe computation" begin
        if Threads.nthreads() > 1
            slow = "gbasis([x^7+y^6+z^5+w^3-1, x^5+y^7+z^6+w^4-1, x^6+y^5+z^7+w^5-1, x+y+z+w^2-2],[x,y,z,w],plex)"
            job = Threads.@spawn try
                giac_eval(GiacContext(), slow)
            catch e
                e
            end
            sleep(1)
            # another thread collects garbage while GIAC computes, and stops the computation
            @test (@elapsed GC.gc()) < 1
            giac_interrupt()
            @test fetch(job) isa Exception
            giac_clear_interrupt()
            @test giac_eval(GiacContext(), "1+1") == "2"
        else
            @info "Julia has one thread: GC-safe computation not tested"
        end
    end
end
