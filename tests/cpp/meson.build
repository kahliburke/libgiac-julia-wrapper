# tests/cpp/meson.build
# Test configuration for C++ unit tests

test_names = [
  'test_eval',
  'test_context',
  'test_gen',
  'test_warnings',
  'test_predicates',
  'test_extraction',
  'test_transfer',
]

foreach t : test_names
  exe = executable(t,
    t + '.cpp',
    dependencies: [giac_wrapper_dep, jlcxx_dep],
    link_with: giac_wrapper_lib,
  )
  test(t, exe, timeout: 60)
endforeach
