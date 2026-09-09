#pragma once
// A minimal test runner with no external dependencies: the same build has to work
// under both AppleClang and MSVC, so the less third-party code the better.
#include <cstdio>
#include <ostream>
#include <sstream>
#include <string>
#include <type_traits>

namespace obf2test {

inline int g_failures = 0;

template <typename T>
std::string show(const T& v) {
  if constexpr (std::is_same_v<T, bool>) {
    return v ? "true" : "false";
  } else if constexpr (requires(std::ostream& os, const T& x) { os << x; }) {
    std::ostringstream os;
    os << v;
    return os.str();
  } else if constexpr (std::is_enum_v<T>) {
    return std::to_string(static_cast<long long>(v));
  } else {
    return "<value not printable>";
  }
}

}  // namespace obf2test

#define CHECK(expr)                                                              \
  do {                                                                           \
    if (!(expr)) {                                                               \
      std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #expr);       \
      ++obf2test::g_failures;                                                    \
    }                                                                            \
  } while (false)

#define CHECK_EQ(a, b)                                                           \
  do {                                                                           \
    const auto _a = (a);                                                         \
    const auto _b = (b);                                                         \
    if (!(_a == _b)) {                                                           \
      std::fprintf(stderr, "FAIL %s:%d: %s == %s\n  got:      %s\n  expected: %s\n", \
                   __FILE__, __LINE__, #a, #b, obf2test::show(_a).c_str(),       \
                   obf2test::show(_b).c_str());                                  \
      ++obf2test::g_failures;                                                    \
    }                                                                            \
  } while (false)

#define TEST_MAIN(body)                                                          \
  int main() {                                                                   \
    body;                                                                        \
    if (obf2test::g_failures != 0) {                                             \
      std::fprintf(stderr, "\n%d checks failed\n", obf2test::g_failures);        \
      return 1;                                                                  \
    }                                                                            \
    std::puts("OK");                                                             \
    return 0;                                                                    \
  }
