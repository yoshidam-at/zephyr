/*
 * Copyright (c) 2019 Intel Corporation
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#include <ztest.h>
#include <zephyr/types.h>
#include <sys/time_units.h>

#define NUM_RANDOM 100

enum units { UNIT_ticks, UNIT_cyc, UNIT_ms, UNIT_us, UNIT_ns };

enum round { ROUND_floor, ROUND_ceil, ROUND_near };

struct test_rec {
	enum units src;
	enum units dst;
	int precision; /* 32 or 64 */
	enum round round;
	void *func;
};

#define TESTREC(src, dst, round, prec) { \
		UNIT_##src, UNIT_##dst, prec, ROUND_##round,		\
			(void *)k_##src##_to_##dst##_##round##prec	\
	}								\

static struct test_rec tests[] = {
	 TESTREC(ms, cyc, floor, 32),
	 TESTREC(ms, cyc, floor, 64),
	 TESTREC(ms, cyc, near, 32),
	 TESTREC(ms, cyc, near, 64),
	 TESTREC(ms, cyc, ceil, 32),
	 TESTREC(ms, cyc, ceil, 64),
	 TESTREC(ms, ticks, floor, 32),
	 TESTREC(ms, ticks, floor, 64),
	 TESTREC(ms, ticks, near, 32),
	 TESTREC(ms, ticks, near, 64),
	 TESTREC(ms, ticks, ceil, 32),
	 TESTREC(ms, ticks, ceil, 64),
	 TESTREC(us, cyc, floor, 64),
	 TESTREC(us, cyc, near, 64),
	 TESTREC(us, cyc, ceil, 64),
	 TESTREC(us, ticks, floor, 64),
	 TESTREC(us, ticks, near, 64),
	 TESTREC(us, ticks, ceil, 64),
	 TESTREC(cyc, ms, floor, 32),
	 TESTREC(cyc, ms, floor, 64),
	 TESTREC(cyc, ms, near, 32),
	 TESTREC(cyc, ms, near, 64),
	 TESTREC(cyc, ms, ceil, 32),
	 TESTREC(cyc, ms, ceil, 64),
	 TESTREC(cyc, us, floor, 64),
	 TESTREC(cyc, us, near, 64),
	 TESTREC(cyc, us, ceil, 64),
	 TESTREC(cyc, ticks, floor, 32),
	 TESTREC(cyc, ticks, floor, 64),
	 TESTREC(cyc, ticks, near, 32),
	 TESTREC(cyc, ticks, near, 64),
	 TESTREC(cyc, ticks, ceil, 32),
	 TESTREC(cyc, ticks, ceil, 64),
	 TESTREC(ticks, ms, floor, 32),
	 TESTREC(ticks, ms, floor, 64),
	 TESTREC(ticks, ms, near, 32),
	 TESTREC(ticks, ms, near, 64),
	 TESTREC(ticks, ms, ceil, 32),
	 TESTREC(ticks, ms, ceil, 64),
	 TESTREC(ticks, us, floor, 64),
	 TESTREC(ticks, us, near, 64),
	 TESTREC(ticks, us, ceil, 64),
	 TESTREC(ticks, cyc, floor, 32),
	 TESTREC(ticks, cyc, floor, 64),
	 TESTREC(ticks, cyc, near, 32),
	 TESTREC(ticks, cyc, near, 64),
	 TESTREC(ticks, cyc, ceil, 32),
	 TESTREC(ticks, cyc, ceil, 64),
	 TESTREC(ns, cyc, floor, 64),
	 TESTREC(ns, cyc, near, 64),
	 TESTREC(ns, cyc, ceil, 64),
	 TESTREC(ns, ticks, floor, 64),
	 TESTREC(ns, ticks, near, 64),
	 TESTREC(ns, ticks, ceil, 64),
	 TESTREC(cyc, ns, floor, 64),
	 TESTREC(cyc, ns, near, 64),
	 TESTREC(cyc, ns, ceil, 64),
	 TESTREC(ticks, ns, floor, 64),
	 TESTREC(ticks, ns, near, 64),
	 TESTREC(ticks, ns, ceil, 64),
	};

u32_t get_hz(enum units u)
{
	if (u == UNIT_ticks) {
		return CONFIG_SYS_CLOCK_TICKS_PER_SEC;
	} else if (u == UNIT_cyc) {
		return sys_clock_hw_cycles_per_sec();
	} else if (u == UNIT_ms) {
		return 1000;
	} else if (u == UNIT_us) {
		return 1000000;
	} else if (u == UNIT_ns) {
		return 1000000000;
	}
	__ASSERT(0, "");
	return 0;
}

void test_conversion(struct test_rec *t, u64_t val)
{
	u32_t from_hz = get_hz(t->src), to_hz = get_hz(t->dst);
	u64_t result;

	if (t->precision == 32) {
		u32_t (*convert)(u32_t) = (u32_t (*)(u32_t)) t->func;

		result = convert((u32_t) val);

		/* If the input value legitimately overflows, then
		 * there is nothing to test
		 */
		if ((val * to_hz) >= ((((u64_t)from_hz) << 32))) {
			return;
		}
	} else {
		u64_t (*convert)(u64_t) = (u64_t (*)(u64_t)) t->func;

		result = convert(val);
	}

	/* We expect the ideal result to be equal to "val * to_hz /
	 * from_hz", but that division is the source of precision
	 * issues.  So reexpress our equation as:
	 *
	 *    val * to_hz ==? result * from_hz
	 *              0 ==? val * to_hz - result * from_hz
	 *
	 * The difference is allowed to be in the range [0:from_hz) if
	 * we are rounding down, from (-from_hz:0] if we are rounding
	 * up, or [-from_hz/2:from_hz/2] if we are rounding to the
	 * nearest.
	 */
	s64_t diff = (s64_t)(val * to_hz - result * from_hz);
	s64_t maxdiff, mindiff;

	if (t->round == ROUND_floor) {
		maxdiff = from_hz - 1;
		mindiff = 0;
	} else if (t->round == ROUND_ceil) {
		maxdiff = 0;
		mindiff = -(s64_t)(from_hz-1);
	} else {
		maxdiff = from_hz/2;
		mindiff = -(s64_t)(from_hz/2);
	}

	zassert_true(diff <= maxdiff && diff >= mindiff,
		     "Convert %lld from %lldhz to %lldhz (= %lld) failed. "
		     "diff %lld should be in [%lld:%lld]",
		     val, from_hz, to_hz, result, diff, mindiff, maxdiff);
}

void test_time_conversions(void)
{
	for (int i = 0; i < ARRAY_SIZE(tests); i++) {
		test_conversion(&tests[i], 0);
		test_conversion(&tests[i], 1);
		test_conversion(&tests[i], 0x7fffffff);
		test_conversion(&tests[i], 0x80000000);
		if (tests[i].precision == 64) {
			test_conversion(&tests[i], 0xffffffff);
			test_conversion(&tests[i], 0x100000000ULL);
		}

		for (int j = 0; j < NUM_RANDOM; j++) {
			test_conversion(&tests[i], sys_rand32_get());
		}
	}
}
