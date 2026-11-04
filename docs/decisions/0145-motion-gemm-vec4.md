# 0145 — Neural motion's tensor-core products: vec4 A loads, the epilogue a row at a time

Status: **KEEP** (the same results bit for bit) · Branch `fellow/geometry` · `modules/fellow_motion/fellow_motion.glsl`
(`gemmc16/32/64`, `gemmd16/32/64`), `fellow_motion_model.cpp` · Diagnostic: `FELLOW_MOTION_GEMM=scalar`.

## Problem

Milestone 43's tensor-core GEMMs ran at 9.3 TFLOPS at a batch of 16 on the RTX 5070 Ti Laptop, far below its
tensor cores. Each thread of a workgroup loaded its share of the A tile (fp32 activations, gathered through the op's
row map or convolution taps) one float at a time: 16 loads and 16 row addresses a thread and K step for a 64-row
tile. Its epilogue worked out, for each of the tile's 4 096 outputs, the output's and the residual's addresses from
the op table (a dozen table reads and two integer divisions an element).

## Change

With a subgroup of 32 or less (the workgroup's four subgroups make 128 threads or fewer), a thread takes four
consecutive k of its rows: one `vec4` load through a read-only vec4 view of the activation buffer (binding 6) where
the rows and the K range are 4-aligned (every transformer and decoder product here), four scalar loads otherwise — a
quarter of the A loads and of the rows' addresses. The K steps (32), the W tile, the shared memory and the epilogue
are the earlier kernel's, so the products are the same to the bit. On by default where the subgroup allows;
`FELLOW_MOTION_GEMM=scalar` (a diagnostic) takes the scalar loads.

**The epilogue a row at a time** (both tensor-core kernels): one thread a row of the tile works out the row's output
and residual offsets and whether the token mask zeroes it, into shared memory (`row_o`, `row_r`, `row_masked`); the
bias and the activation are read from the op table once; each element then adds, activates and stores in
`store_epilogue`'s order — the same results to the bit.

## Verification

* RTX: the clip requests' outputs bit for bit the scalar kernel's (`m44_f326`), which are milestone 43's; the final
  code's (`m44_f333`) bit for bit milestone 43's on tensor cores and in fp32.
* lavapipe (no cooperative matrices): every shader version compiles at load (glslang), the fp32 products' results
  bit for bit milestone 43's.

## Results

RTX 5070 Ti Laptop at night (02:42-02:44, the laptop presumably idle), `fmb_gpu_run.gd`, fp16 weights, the medians
of runs 2-12, both orders (`m44_f326`):

| requests at once | scalar A loads | vec4 A loads | change |
|---|---:|---:|---:|
| 1 | 4.12 ms | 4.10 ms | −0.5 % |
| 2 | 5.09 | 4.58 | −10 % |
| 4 | 6.46 | 5.65 | −13 % |
| 8 | 9.37 | 7.58 | −19 % |
| 16 | 15.23 | 12.86 | −16 % |
| 32 | 26.65 | 22.38 | −16 % |
| 64 | 51.55 | 42.33 | −18 % |

The GPU's time alone: a request 3.37 → 2.93 ms, 16 at once 12.59 → 10.34 ms.

The row-at-a-time epilogue against the per-element one (`m44_f332`, alternating, vec4 loads both): 32 requests
20.93 against 22.35 ms (−6 %), 64 40.77 against 43.45 (−6 %), 8 and 16 −1 %, the GPU's time at 16 9.86 against
10.36 ms and at 1 3.20 against 3.32.

The milestone's final code (`m44_f333`, 03:22, the laptop presumably idle), the medians of runs 2-12:

| requests at once | milestone 43's kernel (`m44_f326`, scalar) | milestone 44 (`m44_f333`) | change |
|---|---:|---:|---:|
| 1 | 4.12 ms | **4.03 ms** | −2 % |
| 2 | 5.09 | **4.44** | −13 % |
| 4 | 6.46 | **5.57** | −14 % |
| 8 | 9.37 | **7.76** | −17 % |
| 16 | 15.23 | **12.60** | −17 % |
| 32 | 26.65 | **20.88** | −22 % |
| 64 | 51.55 | **40.95** | −21 % |

The GPU's time at 16 requests 9.52 ms (milestone 43's kernel 12.59). The fp32 products (no tensor cores, unchanged):
1 request 5.50 ms, 16 26.28, 64 82.28. The results on tensor cores and in fp32 bit for bit milestone 43's
(`v44_coop_case2`, `v44_fp32_case2` against `q_*_case2`).

## Tried and not kept

* **Packed shared stores** (`m44_f327`: the A tile written to shared memory four halves at a time, the W tile eight,
  read back by the cooperative-matrix loads as `uvec2`/`uvec4` arrays): 6 stores a thread and step instead of 32, the
  same results, 0.99-1.02 of vec4's time at every batch. The stores are not what bounds the products.
* **128 columns a workgroup** (`m44_f328`: two 16-column tiles a subgroup, each A tile multiplying two W tiles, the
  activations read by half as many workgroups): twice as slow from 8 requests on (16: 24.6 against 13.2 ms). Half the
  workgroups each doing twice the work take twice as long: the products are bound by each workgroup's own latency
  through its K steps, not by the reads the workgroups share.
* **Fewer rows a workgroup** (`m44_f329`: at most 32 or 16 instead of 64, more and smaller workgroups): 32 rows
  0.89-1.07 of 64's time (8 requests 7.05 against 7.92 ms, 32 requests 24.06 against 22.51), 16 rows 1.01-1.14: no
  consistent gain either way.
* **Two stages of shared memory** (`m44_f330`: a K step multiplies one stage while the next step's tiles go into
  the other, one barrier a step instead of two): 1.14-1.30 of vec4's time from 4 requests on (16: 16.5 against
  12.8 ms; the GPU's time at 16 13.8 against 9.4 ms), the same results. Twice the tiles' shared memory (37 KB a
  workgroup instead of 27) leaves fewer workgroups on a multiprocessor — and with the wide tiles' result, fewer
  workgroups in flight is what these products lose most from.
* **Each subgroup's own epilogue** (`m44_f331`: the accumulators through a 16 × 20 corner of shared memory per
  subgroup, 15 KB a workgroup instead of 27, the opposite of two stages): twice as slow from 16 requests on (16:
  23.1 against 13.3 ms), the same results — the epilogue, done by a subgroup at a time over 64-byte rows instead of by
  the whole workgroup over 256-byte rows, is a large part of these products' time.
* **Split-K's target of 192 workgroups** instead of 96 (`m44_f326`): 1 request 4.08 against 4.13 ms, 16 16.8 against
  15.8 ms; kept at 96.

## Decision

**KEEP** vec4 A loads and the row-at-a-time epilogue. What the experiments say about these products: more
workgroups in flight help, more work or more shared memory a workgroup hurts, and the epilogue counts. Next: an
epilogue that stores straight from the accumulators (no shared memory round trip) and fewer, larger dispatches for a
batch of one.
