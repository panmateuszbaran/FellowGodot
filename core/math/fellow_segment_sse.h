/**************************************************************************/
/*  fellow_segment_sse.h                                                  */
/**************************************************************************/
/*                         This file is part of:                          */
/*                             GODOT ENGINE                               */
/*                        https://godotengine.org                         */
/**************************************************************************/
/* Copyright (c) 2014-present Godot Engine contributors (see AUTHORS.md). */
/* Copyright (c) 2007-2014 Juan Linietsky, Ariel Manzur.                  */
/*                                                                        */
/* Permission is hereby granted, free of charge, to any person obtaining  */
/* a copy of this software and associated documentation files (the        */
/* "Software"), to deal in the Software without restriction, including    */
/* without limitation the rights to use, copy, modify, merge, publish,    */
/* distribute, sublicense, and/or sell copies of the Software, and to     */
/* permit persons to whom the Software is furnished to do so, subject to  */
/* the following conditions:                                              */
/*                                                                        */
/* The above copyright notice and this permission notice shall be         */
/* included in all copies or substantial portions of the Software.        */
/*                                                                        */
/* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,        */
/* EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF     */
/* MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. */
/* IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY   */
/* CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,   */
/* TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE      */
/* SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.                 */
/**************************************************************************/

#pragma once

// FellowEngine (milestone 17): AABB::intersects_segment()'s answer alone, vectorised (decision 0094), shared by
// AABB::intersects_segment() and the BVH's segment cull (decision 0099, which calls it inline on a node's box).

#include "core/math/vector3.h"

#if (defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)) && !defined(REAL_T_IS_DOUBLE)
#define FELLOW_AABB_SEGMENT_SSE
#include <emmintrin.h>

// FellowEngine (milestone 17, decision 0094): the answer of intersects_segment() alone, the three axes at once. The same
// subtractions and the same IEEE divisions (packed division rounds as scalar division), each quotient used where the
// scalar code would compute it (and only those: a quotient the scalar code would not compute is masked to its 0 or 1),
// a NaN treated as the scalar comparisons treat it (it never wins one). The scalar code's early returns only stop it
// sooner: it rejects when an axis rejects or when the running interval empties, which the final interval shows.
// Checked against the scalar code on 60 million random segments and boxes (zero, negative and infinite sizes, NaN and
// infinite coordinates, coincident coordinates): the same answers.
static _FORCE_INLINE_ __m128 _fellow_blend(__m128 p_mask, __m128 p_a, __m128 p_b) {
	return _mm_or_ps(_mm_and_ps(p_mask, p_a), _mm_andnot_ps(p_mask, p_b));
}

static _FORCE_INLINE_ bool _fellow_intersects_segment_sse(const Vector3 &p_position, const Vector3 &p_size, const Vector3 &p_from, const Vector3 &p_to) {
	const __m128 f = _mm_setr_ps(p_from.x, p_from.y, p_from.z, 0.0f);
	const __m128 t = _mm_setr_ps(p_to.x, p_to.y, p_to.z, 0.0f);
	const __m128 b = _mm_setr_ps(p_position.x, p_position.y, p_position.z, 0.0f);
	const __m128 e = _mm_add_ps(b, _mm_setr_ps(p_size.x, p_size.y, p_size.z, 0.0f));
	const __m128 fwd = _mm_cmplt_ps(f, t);
	const __m128 rej = _fellow_blend(fwd, _mm_or_ps(_mm_cmpgt_ps(f, e), _mm_cmplt_ps(t, b)), _mm_or_ps(_mm_cmpgt_ps(t, e), _mm_cmplt_ps(f, b)));
	if (_mm_movemask_ps(rej) & 7) {
		return false;
	}
	const __m128 len = _mm_sub_ps(t, f);
	const __m128 qb = _mm_div_ps(_mm_sub_ps(b, f), len);
	const __m128 qe = _mm_div_ps(_mm_sub_ps(e, f), len);
	const __m128 zero = _mm_setzero_ps();
	const __m128 one = _mm_set1_ps(1.0f);
	__m128 cmin = _fellow_blend(fwd, _fellow_blend(_mm_cmplt_ps(f, b), qb, zero), _fellow_blend(_mm_cmpgt_ps(f, e), qe, zero));
	__m128 cmax = _fellow_blend(fwd, _fellow_blend(_mm_cmpgt_ps(t, e), qe, one), _fellow_blend(_mm_cmplt_ps(t, b), qb, one));
	cmin = _mm_and_ps(cmin, _mm_cmpord_ps(cmin, cmin));
	cmax = _fellow_blend(_mm_cmpord_ps(cmax, cmax), cmax, one);
	alignas(16) float mn[4];
	alignas(16) float mx[4];
	_mm_store_ps(mn, cmin);
	_mm_store_ps(mx, cmax);
	float lo = 0.0f;
	float hi = 1.0f;
	for (int i = 0; i < 3; i++) {
		if (mn[i] > lo) {
			lo = mn[i];
		}
		if (mx[i] < hi) {
			hi = mx[i];
		}
	}
	return !(hi < lo);
}
#endif
