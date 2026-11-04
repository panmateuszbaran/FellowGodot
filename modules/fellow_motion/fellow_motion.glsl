#[versions]

gemm_h16 = "#define MODE_GEMM\n#define WEIGHT_F16\n#define BM 16";
gemm_h32 = "#define MODE_GEMM\n#define WEIGHT_F16\n#define BM 32";
gemm_h64 = "#define MODE_GEMM\n#define WEIGHT_F16\n#define BM 64";
gemm_f16 = "#define MODE_GEMM\n#define BM 16";
gemm_f32 = "#define MODE_GEMM\n#define BM 32";
gemm_f64 = "#define MODE_GEMM\n#define BM 64";
gemmc16 = "#define MODE_GEMM_COOP\n#define BM 16";
gemmc32 = "#define MODE_GEMM_COOP\n#define BM 32";
gemmc64 = "#define MODE_GEMM_COOP\n#define BM 64";
gemmd16 = "#define MODE_GEMM_COOP\n#define A_VEC4\n#define BM 16";
gemmd32 = "#define MODE_GEMM_COOP\n#define A_VEC4\n#define BM 32";
gemmd64 = "#define MODE_GEMM_COOP\n#define A_VEC4\n#define BM 64";
reduce = "#define MODE_REDUCE";
attn = "#define MODE_ATTN";
ln = "#define MODE_LN";
glue = "#define MODE_GLUE";

#[compute]

#version 450

#VERSION_DEFINES

// FellowEngine — neural motion inference (MotionBricks SOMA) on the RenderingDevice.
//
// One float activation buffer holds every tensor, channels-last ([request][row][column]); an op table (48 uints per
// op, written by FellowMotionModel from the .fmb plan) describes each op: GEMMs with row gathers / conv taps and fused
// epilogues (bias, residual, activation, token mask), attention, layer norm and the model's glue. The NumPy executor
// in tools/fmb_plan.py is the reference of every op here.

#if defined(MODE_GEMM_COOP)
#extension GL_KHR_cooperative_matrix : require
#extension GL_KHR_memory_scope_semantics : require
#extension GL_EXT_shader_explicit_arithmetic_types_float16 : require
#extension GL_KHR_shader_subgroup_basic : require
#define WEIGHT_F16
// Four subgroups (SGSIZE, the device's fixed subgroup size, comes with the defines): one per 16-column tile of the 64.
layout(local_size_x = 4 * SGSIZE, local_size_y = 1, local_size_z = 1) in;
#elif defined(MODE_ATTN)
layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;
#else
layout(local_size_x = 256, local_size_y = 1, local_size_z = 1) in;
#endif

layout(set = 0, binding = 0, std430) restrict buffer Act {
	float act[];
};
#ifdef WEIGHT_F16
layout(set = 0, binding = 1, std430) restrict readonly buffer Wt {
	uvec4 wt[];
};
#else
layout(set = 0, binding = 1, std430) restrict readonly buffer Wt {
	vec4 wt[];
};
#endif
layout(set = 0, binding = 2, std430) restrict readonly buffer Par {
	float par[];
};
layout(set = 0, binding = 3, std430) restrict readonly buffer Ops {
	uint ops[];
};
layout(set = 0, binding = 4, std430) restrict buffer Part {
	float part[];
};
#if defined(MODE_GEMM) || defined(MODE_GEMM_COOP)
// The activations again, read-only, for the products' A operand (never the tensor a product writes): read-only loads
// can be cached nearer the cores, where the workgroups of the other column tiles read the same rows.
layout(set = 0, binding = 5, std430) restrict readonly buffer ActIn {
	float act_in[];
};
#ifdef A_VEC4
layout(set = 0, binding = 6, std430) restrict readonly buffer ActIn4 {
	vec4 act_in4[];
};
#endif
#endif

layout(push_constant, std430) uniform Params {
	uint op;
	uint n; // requests in the batch
	uint splits; // GEMM split-K count
	uint pad;
}
pc;

#define OPW 48u
#define NONE 0xFFFFFFFFu
uint O(uint i) {
	return ops[pc.op * OPW + i];
}

// GEMM fields.
#define G_ROWS 2u
#define G_K 3u
#define G_N 4u
#define G_A_OFF 5u
#define G_A_ROWS 6u
#define G_A_LD 7u
#define G_AG 8u
#define G_AS 9u
#define G_AO 10u
#define G_W_OFF 11u
#define G_W_LD 12u
#define G_B_OFF 13u
#define G_O_OFF 14u
#define G_O_ROWS 15u
#define G_O_LD 16u
#define G_OC 17u
#define G_OG 18u
#define G_OS 19u
#define G_OO 20u
#define G_R_OFF 21u
#define G_R_ROWS 22u
#define G_R_LD 23u
#define G_TAPS 24u
#define G_DIL 25u
#define G_UP 26u
#define G_KB 27u
#define G_KE 28u
#define G_APRELU 29u
#define G_ACT 30u
#define G_MASK 31u
#define G_T_OFF 32u

float token_count(uint t_off, uint b) {
	return act[t_off + b * 4u];
}

#if defined(MODE_GEMM) || defined(MODE_GEMM_COOP) || defined(MODE_REDUCE)

// The epilogue: bias, residual, activation, token mask, then the store through the output row map.
void store_epilogue(uint gr, uint n, float v) {
	uint rows = O(G_ROWS);
	uint b = gr / rows;
	uint r = gr % rows;
	uint bo = O(G_B_OFF);
	if (bo != NONE) {
		v += par[bo + n];
	}
	uint ro = O(G_R_OFF);
	if (ro != NONE) {
		v += act[ro + (b * O(G_R_ROWS) + r) * O(G_R_LD) + n];
	}
	uint a = O(G_ACT);
	if (a == 1u) {
		v = max(v, 0.0);
	} else if (a == 2u) {
		v = v >= 0.0 ? v : 0.01 * v;
	}
	uint mk = O(G_MASK);
	if (mk != 0u) {
		if (float(r) >= token_count(O(G_T_OFF), b) * float(mk)) {
			v = 0.0;
		}
	}
	uint og = O(G_OG);
	uint orow = (r / og) * O(G_OS) + O(G_OO) + r % og;
	act[O(G_O_OFF) + (b * O(G_O_ROWS) + orow) * O(G_O_LD) + O(G_OC) + n] = v;
}

#endif

#if defined(MODE_GEMM) || defined(MODE_GEMM_COOP)

// The tiles: BM rows x BN = 64 columns of the output per workgroup, K in steps of BK = 32. Every thread keeps one column
// kk of the A tile and A_SLOTS of its rows for the whole product, with the rows' addresses worked out once; the next K
// step's A and W are fetched into registers while the current one is multiplied (the loads' latency, not the
// arithmetic, bounds these small products).

#define BN 64u
#define BK 32u
#ifdef MODE_GEMM_COOP
#define WG (4u * uint(SGSIZE))
#else
#define WG 256u
#endif
// A_VEC4 (tensor cores with a subgroup of 32 or less, the default there): four consecutive k a thread, one vec4 load
// where the rows and the K range are 4-aligned — a quarter of the loads and of the rows' addresses.
#ifdef A_VEC4
#define A_KPT 4u
#else
#define A_KPT 1u
#endif
#define A_TPR (BK / A_KPT) // threads per row of the A tile
#define A_RSTEP (WG / A_TPR) // rows per pass of the workgroup
#define A_SLOTS (uint(BM) / A_RSTEP)

uint g_rows;
uint g_M;
uint g_kb;
uint g_ke;
uint g_a_off;
uint g_a_ld;
uint g_taps;
int g_dil;
uint g_up;
bool g_aprelu;
uint s_base[A_SLOTS]; // the row's offset in act (no taps) or its request's first row (taps); NONE past the batch
uint s_rr[A_SLOTS]; // the row within its request (taps)
float pa[A_SLOTS * A_KPT];
#ifdef A_VEC4
bool g_avec; // the rows and the K range 4-aligned
#endif

void a_rows_setup(uint m0, uint r0) {
	uint a_rows = O(G_A_ROWS);
	uint ag = O(G_AG);
	uint as_ = O(G_AS);
	uint ao = O(G_AO);
	for (uint i = 0u; i < A_SLOTS; i++) {
		uint gr = m0 + r0 + i * A_RSTEP;
		s_base[i] = NONE;
		s_rr[i] = 0u;
		if (gr < g_M) {
			uint b = gr / g_rows;
			uint rr = gr % g_rows;
			if (g_taps == 0u) {
				uint ar = (rr / ag) * as_ + ao + rr % ag;
				s_base[i] = g_a_off + (b * a_rows + ar) * g_a_ld;
			} else {
				s_base[i] = b * a_rows;
				s_rr[i] = rr;
			}
		}
	}
}

// This thread's A elements of the K step starting at k0 (gathered through the row map or the conv taps, zero outside).
void a_fetch(uint k) {
#ifdef A_VEC4
	if (g_avec && k + 3u < g_ke) {
		// Four columns of one tap (the channel count is a multiple of 4).
		uint tap = 0u;
		uint c = k;
		if (g_taps != 0u) {
			tap = k / g_a_ld;
			c = k % g_a_ld;
		}
		int shift = (int(tap) - int(g_taps / 2u)) * g_dil;
		for (uint i = 0u; i < A_SLOTS; i++) {
			vec4 v = vec4(0.0);
			if (s_base[i] != NONE) {
				if (g_taps == 0u) {
					v = act_in4[(s_base[i] + k) >> 2u];
				} else {
					int tt = int(s_rr[i]) + shift;
					if (tt >= 0 && tt < int(g_rows)) {
						v = act_in4[(g_a_off + (s_base[i] + uint(tt) / g_up) * g_a_ld + c) >> 2u];
					}
				}
			}
			if (g_aprelu) {
				v = max(v, vec4(0.0));
			}
			pa[i * 4u + 0u] = v.x;
			pa[i * 4u + 1u] = v.y;
			pa[i * 4u + 2u] = v.z;
			pa[i * 4u + 3u] = v.w;
		}
		return;
	}
	for (uint j = 0u; j < A_KPT; j++) {
		uint kj = k + j;
		bool kv = kj < g_ke;
		uint tap = 0u;
		uint c = kj;
		if (g_taps != 0u) {
			tap = kj / g_a_ld;
			c = kj % g_a_ld;
		}
		int shift = (int(tap) - int(g_taps / 2u)) * g_dil;
		for (uint i = 0u; i < A_SLOTS; i++) {
			float v = 0.0;
			if (kv && s_base[i] != NONE) {
				if (g_taps == 0u) {
					v = act_in[s_base[i] + kj];
				} else {
					int tt = int(s_rr[i]) + shift;
					if (tt >= 0 && tt < int(g_rows)) {
						v = act_in[g_a_off + (s_base[i] + uint(tt) / g_up) * g_a_ld + c];
					}
				}
			}
			pa[i * A_KPT + j] = g_aprelu ? max(v, 0.0) : v;
		}
	}
#else
	bool kv = k < g_ke;
	uint tap = 0u;
	uint c = k;
	if (g_taps != 0u) {
		tap = k / g_a_ld;
		c = k % g_a_ld;
	}
	int shift = (int(tap) - int(g_taps / 2u)) * g_dil;
	for (uint i = 0u; i < A_SLOTS; i++) {
		float v = 0.0;
		if (kv && s_base[i] != NONE) {
			if (g_taps == 0u) {
				v = act_in[s_base[i] + k];
			} else {
				int tt = int(s_rr[i]) + shift;
				if (tt >= 0 && tt < int(g_rows)) {
					v = act_in[g_a_off + (s_base[i] + uint(tt) / g_up) * g_a_ld + c];
				}
			}
		}
		pa[i] = g_aprelu ? max(v, 0.0) : v;
	}
#endif
}

void gemm_setup() {
	g_rows = O(G_ROWS);
	g_M = g_rows * pc.n;
	g_kb = O(G_KB);
	g_ke = O(G_KE);
	g_a_off = O(G_A_OFF);
	g_a_ld = O(G_A_LD);
	g_taps = O(G_TAPS);
	g_dil = int(O(G_DIL));
	g_up = O(G_UP);
	g_aprelu = O(G_APRELU) != 0u;
#ifdef A_VEC4
	g_avec = ((g_a_off | g_a_ld | g_kb) & 3u) == 0u;
#endif
}

#endif

#ifdef MODE_GEMM

#define RM (BM / 16)

shared float As[BM * (BK + 1u)];
shared vec4 Ws[BK * (BN / 4u)];

void main() {
	uint t = gl_LocalInvocationIndex;
	gemm_setup();
	uint Nout = O(G_N);
	uint m0 = gl_WorkGroupID.y * uint(BM);
	uint n0 = gl_WorkGroupID.x * BN;
	uint split = gl_WorkGroupID.z;
	uint ktiles = (g_ke - g_kb + BK - 1u) / BK;
	uint per = (ktiles + pc.splits - 1u) / pc.splits;
	uint kt0 = split * per;
	uint kt1 = min(ktiles, kt0 + per);

	uint kk = t % BK;
	uint r0 = t / BK;
	a_rows_setup(m0, r0);
	// W: thread t loads 8 consecutive k of column nn.
	uint nn = t & 63u;
	uint kc = t >> 6u;
	uint gn = n0 + nn;
	bool wv = gn < Nout;
#ifdef WEIGHT_F16
	uint w_idx = (O(G_W_OFF) + gn * O(G_W_LD) + g_kb + kc * 8u) >> 3u;
	uvec4 pw = uvec4(0u);
#else
	uint w_idx = (O(G_W_OFF) + gn * O(G_W_LD) + g_kb + kc * 8u) >> 2u;
	vec4 pw0 = vec4(0.0);
	vec4 pw1 = vec4(0.0);
#endif

	vec4 acc[RM];
	for (uint i = 0u; i < uint(RM); i++) {
		acc[i] = vec4(0.0);
	}
	uint tx = t & 15u;
	uint ty = t >> 4u;

	if (kt0 < kt1) {
		a_fetch(g_kb + kt0 * BK + kk);
		if (wv) {
#ifdef WEIGHT_F16
			pw = wt[w_idx + kt0 * (BK / 8u)];
#else
			pw0 = wt[w_idx + kt0 * (BK / 4u)];
			pw1 = wt[w_idx + kt0 * (BK / 4u) + 1u];
#endif
		}
	}
	for (uint kt = kt0; kt < kt1; kt++) {
		for (uint i = 0u; i < A_SLOTS; i++) {
			As[(r0 + i * A_RSTEP) * (BK + 1u) + kk] = pa[i];
		}
		{
#ifdef WEIGHT_F16
			vec4 w0 = vec4(unpackHalf2x16(pw.x), unpackHalf2x16(pw.y));
			vec4 w1 = vec4(unpackHalf2x16(pw.z), unpackHalf2x16(pw.w));
#else
			vec4 w0 = pw0;
			vec4 w1 = pw1;
#endif
			uint base = (kc * 8u) * (BN / 4u) + (nn >> 2u);
			uint comp = nn & 3u;
			Ws[base + 0u * (BN / 4u)][comp] = w0.x;
			Ws[base + 1u * (BN / 4u)][comp] = w0.y;
			Ws[base + 2u * (BN / 4u)][comp] = w0.z;
			Ws[base + 3u * (BN / 4u)][comp] = w0.w;
			Ws[base + 4u * (BN / 4u)][comp] = w1.x;
			Ws[base + 5u * (BN / 4u)][comp] = w1.y;
			Ws[base + 6u * (BN / 4u)][comp] = w1.z;
			Ws[base + 7u * (BN / 4u)][comp] = w1.w;
		}
		barrier();
		// The next step's loads go out before this step's arithmetic.
		if (kt + 1u < kt1) {
			a_fetch(g_kb + (kt + 1u) * BK + kk);
			if (wv) {
#ifdef WEIGHT_F16
				pw = wt[w_idx + (kt + 1u) * (BK / 8u)];
#else
				pw0 = wt[w_idx + (kt + 1u) * (BK / 4u)];
				pw1 = wt[w_idx + (kt + 1u) * (BK / 4u) + 1u];
#endif
			}
		}
		for (uint k2 = 0u; k2 < BK; k2++) {
			vec4 w = Ws[k2 * (BN / 4u) + tx];
			for (uint i = 0u; i < uint(RM); i++) {
				acc[i] += As[(ty * uint(RM) + i) * (BK + 1u) + k2] * w;
			}
		}
		barrier();
	}

	for (uint i = 0u; i < uint(RM); i++) {
		uint gr = m0 + ty * uint(RM) + i;
		if (gr >= g_M) {
			continue;
		}
		for (uint c = 0u; c < 4u; c++) {
			uint on = n0 + tx * 4u + c;
			if (on >= Nout) {
				continue;
			}
			if (pc.splits > 1u) {
				part[(split * g_M + gr) * Nout + on] = acc[i][c];
			} else {
				store_epilogue(gr, on, acc[i][c]);
			}
		}
	}
}

#endif // MODE_GEMM

#ifdef MODE_GEMM_COOP

// The same product on tensor cores: the A tile (gathered as in MODE_GEMM) and the W tile go to shared memory as fp16,
// each subgroup multiplies its 16 columns by the tile's 16-row blocks with 16x16x16 cooperative matrices accumulated
// in fp32, and the accumulators go through shared memory to the same epilogue (or the split-K partials).

#define RT (BM / 16)
#define AST (BK + 8u) // shared row strides (fp16 elements), padded
#define CST (BN + 4u)
#define W_SLOTS ((BN * (BK / 8u)) / WG) // uvec4 of 8 weights per thread per K step

shared float16_t As[BM * AST];
shared float16_t Bs[BN * AST];
shared float Cs[BM * CST];
// The epilogue's addresses a row at a time: each of the tile's rows' output and residual offsets and token mask,
// worked out once by one thread instead of for each of the row's 64 elements (m44_f332: 5 % of the GPU's time at a
// batch of 16).
shared uint row_o[BM]; // NONE past the batch
shared uint row_r[BM]; // NONE without a residual
shared uint row_masked[BM];

void main() {
	uint t = gl_LocalInvocationIndex;
	uint sg = gl_SubgroupID;
	gemm_setup();
	uint Nout = O(G_N);
	uint m0 = gl_WorkGroupID.y * uint(BM);
	uint n0 = gl_WorkGroupID.x * BN;
	uint split = gl_WorkGroupID.z;
	uint ktiles = (g_ke - g_kb + BK - 1u) / BK;
	uint per = (ktiles + pc.splits - 1u) / pc.splits;
	uint kt0 = split * per;
	uint kt1 = min(ktiles, kt0 + per);

	uint kk = (t % A_TPR) * A_KPT;
	uint r0 = t / A_TPR;
	a_rows_setup(m0, r0);
	// W: slot j of thread t is column t / 4 + j * WG / 4, k chunk t % 4 (8 weights).
	uint kc = t % (BK / 8u);
	uint w_idx[W_SLOTS];
	uvec4 pw[W_SLOTS];
	for (uint j = 0u; j < W_SLOTS; j++) {
		uint gn = n0 + t / (BK / 8u) + j * (WG / (BK / 8u));
		w_idx[j] = gn < Nout ? (O(G_W_OFF) + gn * O(G_W_LD) + g_kb + kc * 8u) >> 3u : NONE;
		pw[j] = uvec4(0u);
	}

	coopmat<float, gl_ScopeSubgroup, 16, 16, gl_MatrixUseAccumulator> acc[RT];
	for (uint r = 0u; r < uint(RT); r++) {
		acc[r] = coopmat<float, gl_ScopeSubgroup, 16, 16, gl_MatrixUseAccumulator>(0.0);
	}

	if (kt0 < kt1) {
		a_fetch(g_kb + kt0 * BK + kk);
		for (uint j = 0u; j < W_SLOTS; j++) {
			if (w_idx[j] != NONE) {
				pw[j] = wt[w_idx[j] + kt0 * (BK / 8u)];
			}
		}
	}
	for (uint kt = kt0; kt < kt1; kt++) {
		for (uint i = 0u; i < A_SLOTS; i++) {
			for (uint j = 0u; j < A_KPT; j++) {
				As[(r0 + i * A_RSTEP) * AST + kk + j] = float16_t(clamp(pa[i * A_KPT + j], -65504.0, 65504.0));
			}
		}
		for (uint j = 0u; j < W_SLOTS; j++) {
			uint base = (t / (BK / 8u) + j * (WG / (BK / 8u))) * AST + kc * 8u;
			f16vec2 p0 = unpackFloat2x16(pw[j].x);
			f16vec2 p1 = unpackFloat2x16(pw[j].y);
			f16vec2 p2 = unpackFloat2x16(pw[j].z);
			f16vec2 p3 = unpackFloat2x16(pw[j].w);
			Bs[base + 0u] = p0.x;
			Bs[base + 1u] = p0.y;
			Bs[base + 2u] = p1.x;
			Bs[base + 3u] = p1.y;
			Bs[base + 4u] = p2.x;
			Bs[base + 5u] = p2.y;
			Bs[base + 6u] = p3.x;
			Bs[base + 7u] = p3.y;
		}
		barrier();
		if (kt + 1u < kt1) {
			a_fetch(g_kb + (kt + 1u) * BK + kk);
			for (uint j = 0u; j < W_SLOTS; j++) {
				if (w_idx[j] != NONE) {
					pw[j] = wt[w_idx[j] + (kt + 1u) * (BK / 8u)];
				}
			}
		}
		for (uint ks = 0u; ks < BK; ks += 16u) {
			coopmat<float16_t, gl_ScopeSubgroup, 16, 16, gl_MatrixUseB> mb;
			coopMatLoad(mb, Bs, sg * 16u * AST + ks, AST, gl_CooperativeMatrixLayoutColumnMajor);
			for (uint r = 0u; r < uint(RT); r++) {
				coopmat<float16_t, gl_ScopeSubgroup, 16, 16, gl_MatrixUseA> ma;
				coopMatLoad(ma, As, r * 16u * AST + ks, AST, gl_CooperativeMatrixLayoutRowMajor);
				acc[r] = coopMatMulAdd(ma, mb, acc[r]);
			}
		}
		barrier();
	}
	for (uint r = 0u; r < uint(RT); r++) {
		coopMatStore(acc[r], Cs, r * 16u * CST + sg * 16u, CST, gl_CooperativeMatrixLayoutRowMajor);
	}
	if (t < uint(BM)) {
		uint gr = m0 + t;
		row_o[t] = NONE;
		row_r[t] = NONE;
		row_masked[t] = 0u;
		if (gr < g_M) {
			uint b = gr / g_rows;
			uint r = gr % g_rows;
			uint og = O(G_OG);
			uint orow = (r / og) * O(G_OS) + O(G_OO) + r % og;
			row_o[t] = pc.splits > 1u ? (split * g_M + gr) * Nout : O(G_O_OFF) + (b * O(G_O_ROWS) + orow) * O(G_O_LD) + O(G_OC);
			uint ro = O(G_R_OFF);
			if (ro != NONE) {
				row_r[t] = ro + (b * O(G_R_ROWS) + r) * O(G_R_LD);
			}
			uint mk = O(G_MASK);
			if (mk != 0u && float(r) >= token_count(O(G_T_OFF), b) * float(mk)) {
				row_masked[t] = 1u;
			}
		}
	}
	barrier();
	uint bo = O(G_B_OFF);
	uint act_mode = O(G_ACT);
	for (uint e = t; e < uint(BM) * BN; e += WG) {
		uint r = e / BN;
		uint c = e % BN;
		uint on = n0 + c;
		uint ob = row_o[r];
		if (ob == NONE || on >= Nout) {
			continue;
		}
		float v = Cs[r * CST + c];
		if (pc.splits > 1u) {
			part[ob + on] = v;
			continue;
		}
		// store_epilogue's arithmetic, in its order.
		if (bo != NONE) {
			v += par[bo + on];
		}
		if (row_r[r] != NONE) {
			v += act[row_r[r] + on];
		}
		if (act_mode == 1u) {
			v = max(v, 0.0);
		} else if (act_mode == 2u) {
			v = v >= 0.0 ? v : 0.01 * v;
		}
		if (row_masked[r] != 0u) {
			v = 0.0;
		}
		act[ob + on] = v;
	}
}

#endif // MODE_GEMM_COOP

#ifdef MODE_REDUCE

void main() {
	uint rows = O(G_ROWS);
	uint M = rows * pc.n;
	uint Nout = O(G_N);
	uint id = gl_GlobalInvocationID.x;
	if (id >= M * Nout) {
		return;
	}
	uint gr = id / Nout;
	uint nn = id % Nout;
	float v = 0.0;
	for (uint s = 0u; s < pc.splits; s++) {
		v += part[(s * M + gr) * Nout + nn];
	}
	store_epilogue(gr, nn, v);
}

#endif // MODE_REDUCE

#ifdef MODE_ATTN

#define A_ROWS 2u
#define A_D 3u
#define A_HEADS 4u
#define A_QKV 5u
#define A_OUT 6u
#define A_MASK 7u
#define A_T_OFF 8u
#define MAX_S 32u
#define MAX_HD 64u

shared float Qs[MAX_S * MAX_HD];
shared float Ks[MAX_S * MAX_HD];
shared float Vs[MAX_S * MAX_HD];

void main() {
	uint t = gl_LocalInvocationIndex;
	uint S = O(A_ROWS);
	uint d = O(A_D);
	uint H = O(A_HEADS);
	uint hd = d / H;
	uint b = gl_WorkGroupID.x / H;
	uint h = gl_WorkGroupID.x % H;
	uint qkv = O(A_QKV);
	for (uint e = t; e < S * hd; e += 64u) {
		uint j = e / hd;
		uint c = e % hd;
		uint base = qkv + (b * S + j) * 3u * d + h * hd + c;
		Qs[e] = act[base];
		Ks[e] = act[base + d];
		Vs[e] = act[base + 2u * d];
	}
	barrier();
	uint ms = O(A_MASK);
	float T = ms != NONE ? token_count(O(A_T_OFF), b) : 0.0;
	float scale = 1.0 / sqrt(float(hd));
	for (uint q = t; q < S; q += 64u) {
		float sc[MAX_S];
		float mx = -3.4e38;
		for (uint j = 0u; j < S; j++) {
			bool valid = ms == NONE || j < ms || float(j - ms) < T;
			float s = -3.4e38;
			if (valid) {
				s = 0.0;
				for (uint c = 0u; c < hd; c++) {
					s += Qs[q * hd + c] * Ks[j * hd + c];
				}
				s *= scale;
				mx = max(mx, s);
			}
			sc[j] = s;
		}
		float sum = 0.0;
		for (uint j = 0u; j < S; j++) {
			bool valid = ms == NONE || j < ms || float(j - ms) < T;
			float e = valid ? exp(sc[j] - mx) : 0.0;
			sc[j] = e;
			sum += e;
		}
		float inv = 1.0 / sum;
		uint ob = O(A_OUT) + (b * S + q) * d + h * hd;
		for (uint c = 0u; c < hd; c++) {
			float o = 0.0;
			for (uint j = 0u; j < S; j++) {
				o += sc[j] * Vs[j * hd + c];
			}
			act[ob + c] = o * inv;
		}
	}
}

#endif // MODE_ATTN

#ifdef MODE_LN

#define L_ROWS 2u
#define L_D 3u
#define L_X 4u
#define L_G 5u
#define L_B 6u

shared float red[256];

float block_sum(float v) {
	uint t = gl_LocalInvocationIndex;
	red[t] = v;
	barrier();
	for (uint s = 128u; s > 0u; s >>= 1u) {
		if (t < s) {
			red[t] += red[t + s];
		}
		barrier();
	}
	float r = red[0];
	barrier();
	return r;
}

void main() {
	uint t = gl_LocalInvocationIndex;
	uint row = gl_WorkGroupID.x;
	uint d = O(L_D);
	uint x = O(L_X) + row * d;
	float v[4];
	float s = 0.0;
	for (uint i = 0u; i < 4u; i++) {
		uint c = t + i * 256u;
		v[i] = c < d ? act[x + c] : 0.0;
		s += v[i];
	}
	float mean = block_sum(s) / float(d);
	float q = 0.0;
	for (uint i = 0u; i < 4u; i++) {
		uint c = t + i * 256u;
		float dv = c < d ? v[i] - mean : 0.0;
		q += dv * dv;
	}
	float var = block_sum(q) / float(d);
	float inv = 1.0 / sqrt(var + 1e-5);
	uint g = O(L_G);
	uint bb = O(L_B);
	for (uint i = 0u; i < 4u; i++) {
		uint c = t + i * 256u;
		if (c < d) {
			act[x + c] = (v[i] - mean) * inv * par[g + c] + par[bb + c];
		}
	}
}

#endif // MODE_LN

#ifdef MODE_GLUE

#define GLUE_WORK 47u
#define GLUE_T 46u

#define GM_ROOT_SELECT 1u
#define GM_SEQ1 2u
#define GM_NTOK_ARGMAX 3u
#define GM_SEQ2 4u
#define GM_MASK_COPY 5u
#define GM_ROOT_DENSE 6u
#define GM_REPLACE 7u
#define GM_ROOT_TO_LOCAL 8u
#define GM_POSE_DENSE 9u
#define GM_POSE_SELECT 10u
#define GM_POSE_IN 11u
#define GM_POSE_TOKENS 12u

// Which input slot (0-7) a frame of the 64 holds: context frames 0-3, target frames at (T - 1) * 4 + i; -1 none.
int slot_of(uint f, float T) {
	if (f < 4u) {
		return int(f);
	}
	int tf = (int(T) - 1) * 4;
	int d = int(f) - tf;
	return (d >= 0 && d < 4) ? 4 + d : -1;
}

float wrap_atan(float y, float x) {
	return atan(y, x);
}

void main() {
	uint work = O(GLUE_WORK);
	uint id = gl_GlobalInvocationID.x;
	if (id >= work * pc.n) {
		return;
	}
	uint b = id / work;
	uint i = id % work;
	uint mode = O(1u);
	float T = token_count(O(GLUE_T), b);

	if (mode == GM_ROOT_SELECT) {
		// emb [8, 384]: pose 0-255 (flag 2), local root 256-319 (flag 1), global root 320-383 (flag 0).
		uint f = i / 384u;
		uint c = i % 384u;
		uint fl = c < 256u ? 2u : (c < 320u ? 1u : 0u);
		if (act[O(3u) + (b * 8u + f) * 4u + fl] < 0.5) {
			act[O(2u) + (b * 8u + f) * 384u + c] = par[O(4u) + c];
		}
	} else if (mode == GM_SEQ1) {
		// s [9, 512]: row 0 the masked token-count embedding, rows 1-8 the positioned frame embeddings.
		uint r = i / 512u;
		uint c = i % 512u;
		float v;
		if (r == 0u) {
			v = par[O(6u) + c];
		} else {
			uint f = r - 1u;
			v = act[O(2u) + (b * 8u + f) * 512u + c] + par[O(5u) + f * 512u + c];
			act[O(3u) + (b * 8u + f) * 512u + c] = v;
		}
		act[O(4u) + (b * 9u + r) * 512u + c] = v;
	} else if (mode == GM_NTOK_ARGMAX) {
		uint lg = O(2u) + b * 12u;
		uint al = O(3u) + b * 12u;
		int best = 0;
		float bv = -3.4e38;
		bool any = false;
		for (int k = 0; k < 11; k++) {
			if (act[al + uint(k)] > 0.5) {
				float v = act[lg + uint(k)];
				if (!any || v > bv) {
					bv = v;
					best = k;
					any = true;
				}
			}
		}
		act[O(4u) + b * 4u] = float(best + 6);
	} else if (mode == GM_SEQ2) {
		// s [25, 512]: the chosen token count's embedding, the positioned frames, the positions.
		uint r = i / 512u;
		uint c = i % 512u;
		float v;
		if (r == 0u) {
			v = par[O(4u) + uint(int(T) - 6) * 512u + c];
		} else if (r < 9u) {
			v = act[O(2u) + (b * 8u + r - 1u) * 512u + c];
		} else {
			v = par[O(5u) + (r - 9u) * 512u + c];
		}
		act[O(3u) + (b * 25u + r) * 512u + c] = v;
	} else if (mode == GM_MASK_COPY) {
		uint rows = O(6u);
		uint cols = O(7u);
		uint r = i / cols;
		uint c = i % cols;
		float v = act[O(2u) + (b * O(3u) + O(4u) + r) * cols + c];
		if (float(r) >= T * float(O(8u))) {
			v = 0.0;
		}
		act[O(5u) + (b * rows + r) * cols + c] = v;
	} else if (mode == GM_ROOT_DENSE) {
		uint f = i / 512u;
		uint c = i % 512u;
		int s = slot_of(f, T);
		float v = par[O(5u) + c];
		if (s >= 0 && act[O(3u) + (b * 8u + uint(s)) * 4u + 3u] > 0.5) {
			v = act[O(2u) + (b * 8u + uint(s)) * 512u + c];
		}
		act[O(6u) + (b * 64u + f) * 512u + c] = v;
		if (c < 5u) {
			act[O(7u) + (b * 64u + f) * 5u + c] = s >= 0 ? act[O(4u) + (b * 8u + uint(s)) * 5u + c] : 0.0;
		}
		if (c == 0u) {
			act[O(8u) + b * 64u + f] = s >= 0 ? act[O(3u) + (b * 8u + uint(s)) * 4u] : 0.0;
		}
	} else if (mode == GM_REPLACE) {
		uint cols = O(5u);
		uint f = i / cols;
		uint c = i % cols;
		if (act[O(4u) + b * 64u + f] > 0.5) {
			act[O(2u) + (b * 64u + f) * cols + c] = act[O(3u) + (b * 64u + f) * cols + c];
		}
	} else if (mode == GM_ROOT_TO_LOCAL) {
		// One thread per request: global root (64 frames) -> local root velocities, as the reference.
		uint src = O(2u) + b * 64u * 5u;
		uint dst = O(5u) + b * 64u * 4u;
		uint st = O(6u);
		int last = int(T) * 4 - 1;
		float prev_av = 0.0;
		vec3 prev_vel = vec3(0.0);
		for (uint f = 0u; f < 64u; f++) {
			float g[5];
			float gn[5];
			for (uint k = 0u; k < 5u; k++) {
				g[k] = act[src + f * 5u + k] * par[st + 5u + k] + par[st + k];
			}
			float a0 = atan(g[4], g[3]);
			a0 = atan(sin(a0 * 0.5), cos(a0 * 0.5)) * 2.0;
			float av = 0.0;
			vec3 vel = vec3(0.0);
			if (f < 63u) {
				for (uint k = 0u; k < 5u; k++) {
					gn[k] = act[src + (f + 1u) * 5u + k] * par[st + 5u + k] + par[st + k];
				}
				float a1 = atan(gn[4], gn[3]);
				a1 = atan(sin(a1 * 0.5), cos(a1 * 0.5)) * 2.0;
				float c0 = cos(a0), s0 = sin(a0), c1 = cos(a1), s1 = sin(a1);
				av = 30.0 * atan(s1 * c0 - c1 * s0, c1 * c0 + s1 * s0);
				vel = 30.0 * (vec3(gn[0], gn[1], gn[2]) - vec3(g[0], g[1], g[2]));
			}
			if (int(f) == last) {
				av = prev_av;
				vel = prev_vel;
			}
			float loc[4] = float[4](av, vel.x, vel.z, g[1]);
			for (uint k = 0u; k < 4u; k++) {
				act[dst + f * 4u + k] = (loc[k] - par[st + 10u + k]) / par[st + 14u + k];
			}
			prev_av = av;
			prev_vel = vel;
		}
		if (last >= 0 && last < 64 && act[O(4u) + (b * 8u + 7u) * 4u + 1u] > 0.5) {
			for (uint k = 0u; k < 4u; k++) {
				act[dst + uint(last) * 4u + k] = act[O(3u) + (b * 8u + 7u) * 4u + k];
			}
		}
	} else if (mode == GM_POSE_DENSE) {
		uint f = i / 268u;
		uint c = i % 268u;
		int s = slot_of(f, T);
		act[O(4u) + (b * 64u + f) * 268u + c] = s >= 0 ? act[O(2u) + (b * 8u + uint(s)) * 268u + c] : 0.0;
		if (c == 0u) {
			act[O(5u) + b * 64u + f] = s >= 0 ? act[O(3u) + (b * 8u + uint(s)) * 4u + 2u] : 0.0;
		}
	} else if (mode == GM_POSE_SELECT) {
		uint f = i / 160u;
		uint c = i % 160u;
		float v = act[O(3u) + b * 64u + f] > 0.5 ? act[O(2u) + (b * 64u + f) * 160u + c] : par[O(5u) + (f % 4u) * 160u + c];
		act[O(4u) + (b * 64u + f) * 160u + c] = v;
	} else if (mode == GM_POSE_IN) {
		uint r = i / 1024u;
		uint c = i % 1024u;
		uint x = O(2u) + (b * 16u + r) * 1024u + c;
		act[x] = max(act[x] + par[O(3u) + uint(int(T) - 6) * 1024u + c], 0.0) + par[O(4u) + r * 1024u + c];
	} else if (mode == GM_POSE_TOKENS) {
		uint p = i / 160u;
		uint j = i % 160u;
		uint h = j / 32u;
		uint c = j % 32u;
		uint lg = O(2u) + (b * 16u + p) * 80u + h * 16u;
		uint best = 0u;
		float bv = act[lg];
		for (uint q = 1u; q < 16u; q++) {
			float v = act[lg + q];
			if (v > bv) {
				bv = v;
				best = q;
			}
		}
		float v = float(p) < T ? par[O(4u) + (h * 16u + best) * 32u + c] : 0.0;
		act[O(3u) + (b * 16u + p) * 160u + j] = v;
	}
}

#endif // MODE_GLUE
