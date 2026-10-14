# 0046 — The light loop reads the next light while it processes the current one

Status: **under test** (`fellow118`, job `m9_f135`) · Branch `fellow/geometry` · Toggle
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

## Measured

Pending (`m9_f135`: pipeline statistics, images against 0, in-process A/B of 0-4).
