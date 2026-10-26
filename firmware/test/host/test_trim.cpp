// Host test for src/drive/StraightTrim.h. Build and run with the project's host toolchain:
//   zig c++ -std=c++17 test/host/test_trim.cpp -o diagnostics/trim-tests.exe
#include <cstdio>
#include <cstdlib>

#include "../../src/drive/StraightTrim.h"

static int failures = 0;
#define CHECK(name, condition)                                 \
  do {                                                         \
    if (!(condition)) { std::printf("FAIL %s\n", name); ++failures; } \
    else std::printf("  PASS  %s\n", name);                    \
  } while (0)

using drive::TrimTable;

static void run(const TrimTable &f, const TrimTable &r, int l, int rt, int &ol, int &orr, bool od = false) {
  int8_t a = static_cast<int8_t>(l), b = static_cast<int8_t>(rt);
  drive::applyStraightTrim(a, b, f, r, od);
  ol = a;
  orr = b;
}

int main() {
  const TrimTable flat12 = {{12, 12, 12, 12}};
  const TrimTable none = {{0, 0, 0, 0}};
  int l, r;

  run(flat12, none, 50, 50, l, r);
  CHECK("12% at 50 reproduces the calibrated 44/56", l == 44 && r == 56);
  run(flat12, none, 100, 100, l, r);
  CHECK("12% at 100 pins the strong track and keeps the ratio (79/100)", l == 79 && r == 100);
  run(none, flat12, -50, -50, l, r);
  CHECK("reverse is applied to magnitudes with the sign kept", l == -44 && r == -56);
  run(flat12, none, 50, 40, l, r);
  CHECK("turns (unequal tracks) are untouched", l == 50 && r == 40);
  run(flat12, flat12, 0, 0, l, r);
  CHECK("zero is untouched", l == 0 && r == 0);
  run(flat12, flat12, 100, 100, l, r, true);
  CHECK("overdrive is untouched", l == 100 && r == 100);

  const TrimTable neg = {{-12, -12, -12, -12}};
  run(neg, none, 50, 50, l, r);
  CHECK("a negative entry holds the right track back", l == 56 && r == 44);

  const TrimTable ramp = {{20, 10, 5, 0}};
  CHECK("flat below the first point", drive::trimPercentAt(ramp, 10) == 20);
  CHECK("exact points", drive::trimPercentAt(ramp, 50) == 10 && drive::trimPercentAt(ramp, 100) == 0);
  CHECK("interpolates between points", drive::trimPercentAt(ramp, 38) == 15 && drive::trimPercentAt(ramp, 88) == 2);
  run(none, ramp, -100, -100, l, r);
  CHECK("reverse table entry of 0 at 100 leaves -100/-100", l == -100 && r == -100);
  run(ramp, none, 100, 100, l, r);
  CHECK("zero percent at 100 leaves 100/100", l == 100 && r == 100);

  bool monotone = true;
  for (int p = 1; p <= 100; ++p) {
    int8_t a = static_cast<int8_t>(p), b = a;
    drive::applyStraightTrim(a, b, flat12, flat12, false);
    if (a < 0 || b < 0 || a > 100 || b > 100 || b < a) monotone = false;
  }
  CHECK("every power stays in range with the right track not below the left", monotone);

  std::printf(failures ? "\nTrim tests FAILED\n" : "\nTrim tests passed\n");
  return failures ? 1 : 0;
}
