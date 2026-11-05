#include "core/fixed_test.h"

#include "core/fix32.h"
#include "core/fix_trig.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>

namespace nsync::core {
namespace {

int g_failures = 0;

void check(bool condition, const char* expression, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, expression);
        ++g_failures;
    }
}

bool near_raw(Fix32 value, Fix32 expected, std::int32_t tolerance) {
    const std::int64_t diff = static_cast<std::int64_t>(value.raw()) - expected.raw();
    return diff >= -tolerance && diff <= tolerance;
}

} // namespace

bool run_fixed_tests() {
    g_failures = 0;

    const Fix32 one = Fix32::one();
    const Fix32 two = Fix32::from_int(2);
    const Fix32 half = Fix32::from_ratio(1, 2);

    check(one.raw() == 65536, "one.raw() == 65536", __LINE__);
    check(half.raw() == 32768, "half.raw() == 32768", __LINE__);
    check((one + two) == Fix32::from_int(3), "1 + 2 == 3", __LINE__);
    check((two - one) == one, "2 - 1 == 1", __LINE__);
    check((Fix32::from_int(3) * Fix32::from_int(7)) == Fix32::from_int(21), "3 * 7 == 21", __LINE__);
    check((Fix32::from_int(-3) * Fix32::from_int(7)) == Fix32::from_int(-21), "-3 * 7 == -21", __LINE__);
    check((Fix32::from_int(3) / Fix32::from_int(2)) == Fix32::from_ratio(3, 2), "3 / 2", __LINE__);
    check(near_raw(Fix32::from_ratio(1, 3) * Fix32::from_int(3), Fix32::from_int(1), 1), "(1/3) * 3 ~= 1", __LINE__);

    check(Fix32::sqrt(Fix32::from_int(4)) == Fix32::from_int(2), "sqrt(4) == 2", __LINE__);
    check(Fix32::sqrt(Fix32::from_int(9)) == Fix32::from_int(3), "sqrt(9) == 3", __LINE__);
    check(near_raw(Fix32::sqrt(Fix32::from_int(2)), Fix32::from_ratio(141421, 100000), 2), "sqrt(2)", __LINE__);
    check(Fix32::sqrt(Fix32::from_int(-1)) == Fix32::zero(), "sqrt(-1) == 0", __LINE__);

    check(Fix32::from_int(7).to_int_trunc() == 7, "to_int_trunc", __LINE__);
    check(Fix32::from_ratio(7, 2).to_int_round() == 4, "to_int_round", __LINE__);
    check(Fix32::from_ratio(-7, 2).to_int_round() == -4, "negative to_int_round", __LINE__);

    check(sin_deg(0) == Fix32::zero(), "sin(0) == 0", __LINE__);
    check(near_raw(sin_deg(90), Fix32::one(), 1), "sin(90) == 1", __LINE__);
    check(near_raw(sin_deg(180), Fix32::zero(), 1), "sin(180) == 0", __LINE__);
    check(near_raw(sin_deg(270), Fix32::from_int(-1), 1), "sin(270) == -1", __LINE__);
    check(near_raw(sin_deg(360), Fix32::zero(), 1), "sin(360) == 0", __LINE__);
    check(near_raw(sin_deg(-90), Fix32::from_int(-1), 1), "sin(-90) == -1", __LINE__);
    check(near_raw(sin_deg(30), Fix32::from_ratio(1, 2), 300), "sin(30) ~= 0.5", __LINE__);
    check(near_raw(cos_deg(0), Fix32::one(), 1), "cos(0) == 1", __LINE__);
    check(near_raw(cos_deg(60), Fix32::from_ratio(1, 2), 300), "cos(60) ~= 0.5", __LINE__);
    check(near_raw(cos_deg(180), Fix32::from_int(-1), 1), "cos(180) == -1", __LINE__);

    const Vec2 v3_4{Fix32::from_int(3), Fix32::from_int(4)};
    check(v3_4.length() == Fix32::from_int(5), "length(3,4) == 5", __LINE__);
    check(v3_4.dot(v3_4) == Fix32::from_int(25), "dot((3,4),(3,4)) == 25", __LINE__);
    const Vec2 normalized = v3_4.normalized();
    check(near_raw(normalized.length(), Fix32::one(), 2), "normalized length == 1", __LINE__);

    if (g_failures == 0) {
        std::printf("fixed-point tests: PASS\n");
        return true;
    }

    std::fprintf(stderr, "fixed-point tests: %d failure(s)\n", g_failures);
    return false;
}

} // namespace nsync::core
