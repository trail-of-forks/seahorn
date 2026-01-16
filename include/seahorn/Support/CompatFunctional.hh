#pragma once

#include <functional>

// Provide std::unary_function on libc++ where it is not available.
// This keeps existing code compatible with both libstdc++ and libc++.
// Only enable in C++17 builds where std::unary_function was removed.
#if __cplusplus >= 201703L && defined(_LIBCPP_VERSION)
#ifndef SEAHORN_COMPAT_FUNCTIONAL_REDEFINED
#define SEAHORN_COMPAT_FUNCTIONAL_REDEFINED
namespace std {
template <typename Arg, typename Result> struct unary_function {
  using argument_type = Arg;
  using result_type = Result;
};

template <typename Arg1, typename Arg2, typename Result> struct binary_function {
  using first_argument_type = Arg1;
  using second_argument_type = Arg2;
  using result_type = Result;
};

} // namespace std
#endif
#endif
