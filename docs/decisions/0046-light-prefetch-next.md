# 0046 — The light loop reads the next light while it processes the current one

Status: **REJECT** (`fellow118`, `m9_f135`: exact, but 2-11 % slower; off, code kept for the record) · Branch
`fellow/geometry` · Toggle
`rendering/fellow/lights/prefetch_next` (runtime; 0 off, 1 the range test's fields, 2 all the hot fields, 3 and 4 the same
and the next cluster word; specialization bits 21-23; with `lights/fields_early`) · Code: `scene_forward_clustered.glsl`
(the omni and spot loops: `fellow_nx_*`, `fellow_wn_*`), `scene_forward_clustered_inc.glsl`
(`sc_fellow_light_prefetch_next/all/words`), `render_forward_clustered.cpp`, `fellow_settings.*`.

## Problem

After 0045 the light loop reads a light's layer mask, bake mode and hot fields together, right after it takes the
light's bit from the warp's mask. They still arrive one memory latency after the bit is known, and the range test (and
everything after it) waits for them: each light the loop visits costs at least one load latency. In Lights Lab the loop
and the range tests were 0.63 ms of the lights' 0.92 ms (`m9_f132_loop_sizing`, before 0045's v2), and the loop visits
10-16 lights per pixel, of which 3-8 are within range.

The order of the lights is the warp's: `merged_mask` is the subgroup's union of the pixels' masks, the same in every
lane, and the loop takes its bits from the highest down. The next light's index is known as soon as the current one's
bit is cleared, before the current light is processed.

## Change

With the option, at the top of each iteration the loop takes the current light's values from registers filled one
iteration earlier and issues the loads of the next light's (the next bit of `merged_mask`): the layer mask, the bake
mode, the position and the inverse radius (1, 3), or all the hot fields of 0045 (2, 4; omni 13 values, spot 18). The
loads are in flight while the current light is tested, shadowed and shaded. The first light of a cluster word is read
when the word's mask is known.

3 and 4 also read the next cluster word: the next word's mask (the per-pixel load, the clip mask and the subgroup
union) is computed when the current word starts, and when the current word's last light is taken the next word's first
light is read, so a word's first light does not wait for the word's mask and then for its own fields.

### Why it is exact

The same values from the same buffers reach the same expressions; only when they are loaded changes. The subgroup
union of the next word is computed with the same active lanes as at the top of that word's iteration (the loops over
words and bits are uniform; lanes that skip a light rejoin before the next bit).

## Verification (lavapipe)

Lights Lab captures with 1, 2, 3 and 4, City Lab and PBR Lab with 4: **0 differing pixels** against 0. Sabotage
checks: the prefetched colour scaled by 1/4 changes 1.4 million pixels of Lights Lab's four captures (the prefetched
values are the ones used); the next word's first light taken from the lowest bit instead of the highest changes 56 342
pixels (the cross-word path runs: Lights Lab has 192 omni lights, six cluster words).

## Measured (`fellow118`, `m9_f135`)

* Images (`m9_f135_temporal`, 60-frame sequences, 2 and 4 against 0): Bistro interior 67.1 dB, Lights Lab 91.4-97.3 dB
  (their floors), City Lab bit for bit with 2, 101.18 dB (a few pixels one level off) with 4.
* Registers (`FELLOW_PIPELINE_STATS`, Bistro interior's and Lights Lab's main colour variants): 0 has **96 registers
  and 48-64 bytes of local memory**; every mode, even 1 (six values carried over the iteration), has **124-127
  registers and none**. The driver stops spilling and allocates more registers, which leaves fewer warps per SM (a
  scene shader that waits on memory).
* In one process (`m9_f135_pf_ab`, autotune off, blocks of 30 frames, five configurations taking turns):

| scene | 1 | 2 | 3 | 4 | opaque pass 0 → 4 |
|---|---:|---:|---:|---:|---:|
| Lights Lab | +5.6 % | +8.9 % | +5.6 % | +10.7 % | 1.065 → 1.379 ms |
| Bistro interior | +2.3 % | +3.8 % | +2.0 % | +4.7 % | 1.417 → 1.594 |
| PBR Lab | +1.7 % | +4.4 % | +0.8 % | +4.9 % | 0.756 → 0.875 |
| City Lab | −0.6 % | −1.3 % | −1.3 % | −1.0 % (all n.s.) | 0.469 → 0.539 |
| Bistro exterior | 0.0 % | 0.0 % | +0.1 % | +0.1 % (n.s.) | 1.576 → 1.574 |

(all p < 10^-9 except City Lab and Bistro exterior.) The opaque pass is slower in every scene with positional lights,
most where the loop is longest.

## Decision

**REJECT** (off; the code stays as the record of the measurement). Hiding the light data's latency costs more in
occupancy than it hides: the loads are not what the loop waits for once the registers that carry them cost a fifth of
the warps. Decision 0048 collects what this and the other `m9_f135` experiments say about the scene shader's register
budget.
