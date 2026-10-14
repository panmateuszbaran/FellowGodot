# 0048 — The scene shader's register budget (what `m9_f135` measured)

Status: **finding; two experiments REJECT** · Branch `fellow/geometry` · Diagnostics `debug/scene_shader_experiments`
1048576 (positional PCF taps in one batch of 8), 2097152 (omni/spot shadow fields read after the range test) · Code:
`scene_forward_lights_inc.glsl` (`FELLOW_TAP_BATCH_PCF`, `FELLOW_SHO`, `FELLOW_SHS`), `render_forward_clustered.cpp`,
`fellow_settings.cpp`.

## What the driver does with the colour pass's main variants

`FELLOW_PIPELINE_STATS` (`m9_f135_pstats_*`, `fellow118`, Bistro interior and Lights Lab, the four main colour
variants of each):

| build | registers | local memory |
|---|---:|---:|
| defaults (0045 v2) | 96 | 48-64 bytes |
| 0047 compact light copy (storage or uniform buffer) | 114-117 | 0 |
| 0046 next-light prefetch (any mode) | 124-127 | 0 |
| 1048576: positional PCF in one batch of 8 | 128 | 0 |
| 2097152: shadow fields after the range test | 96 | 48-64 bytes |

The shipped shader sits at 96 registers and spills a few values. When a change keeps more values live, even six (0046
mode 1), the driver does not spill a little more: it stops spilling and allocates 114-128 registers. An SM then holds
16-17 warps of this shader instead of 20, and the opaque pass, which waits on memory (0040), gets slower. Every
experiment that crossed the line was slower in the scenes it touched, by more than it could save (0046: up to
+10.7 % in Lights Lab; 0047: up to +2.4 %; PCF in one batch: below). The one that stayed at 96 registers (2097152) changed
nothing measurable.

## The two experiments

* **1048576: the positional PCF's 8 taps in one batch** (soft medium; the PCSS loops keep batches of 4). One round
  trip of texture latency per shadowed light instead of two. 128 registers. Opaque pass (`m9_f135_exp`, between
  processes, async compute off): Bistro interior +0.062 ms, Lights Lab +0.097, PBR Lab +0.113.
* **2097152: the omni and spot shadows' fields (atlas rectangle, shadow matrix, biases, soft shadow size and scale)
  read right after the range test**, before the attenuation, the size and the back-facing test, instead of where the
  shadow lookup uses them. 96 registers. Opaque pass: Bistro interior −0.007 ms (noise), Lights Lab +0.076 and PBR Lab
  +0.080 in runs whose GPU clock moved by up to 600 MHz (the other passes moved with them).

Both are exact (lavapipe: Lights Lab and PBR Lab captures 0 differing pixels; RTX `m9_f135_temporal`: Bistro interior
67.1 dB, Lights Lab 97.6 dB, City Lab bit for bit or 101.18 dB, their floors). Both **REJECT**; the bits stay as
diagnostics.

## What follows

Work on the colour pass's shader has a budget: the default variants are at the edge where the driver switches from 96
registers with spills to 115-128 without. A change that adds live values has to save more than a sixth of the
warps. Conversely, values that are live across the light loops only to be used after them are candidates to move:
the indirect lighting (ambient, reflections, SSIL, SSR) is computed before the directional, omni and spot loops and
carried through them.
