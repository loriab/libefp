/*-
 * Copyright (c) 2012 Ilya Kaliman
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "test_common.h"
#include "geometry_1.h"

static enum efp_result
st_integrals_fn(const struct efp_st_block *block, int compute_derivatives,
		struct efp_st_data *st, void *user_data)
{
	static const int expected_size_i = 140;
	static const int expected_size_j = 140;
	static const char *s_path = ABS_TOP_SRCDIR "/tests/data/sint_1";
	static const char *t_path = ABS_TOP_SRCDIR "/tests/data/tint_1";

	return st_integrals_from_file(block, compute_derivatives, st, user_data,
			expected_size_i, expected_size_j, s_path, t_path);
}

static const double ref_gradient[] = { /* from Q-Chem 4.0 */
	0.0, 0.0, 0.0, 0.0, 0.0, 0.0,
	0.0, 0.0, 0.0, 0.0, 0.0, 0.0
};

static const struct test_data test_data = {
	.potential_files = potential_files,
	.fragname = fragname,
	.geometry_xyzabc = xyzabc,
	.ref_energy = 0.000013466610, /* from Q-Chem 4.0 */
	.do_gradient = 0,
	.ref_gradient = ref_gradient,
	.opts = {
		.terms = EFP_TERM_XR
	},
	.callbacks = {
		.get_st_integrals = st_integrals_fn
	}
};

DEFINE_TEST(test_data)
