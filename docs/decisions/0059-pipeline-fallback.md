# 0059 — While a specialization compiles: a compiled sibling instead of the ubershader

Status: **REJECT** (left in, off; measured with `fellow144`) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/pipeline_fallback`
(runtime, off until measured) · Code: `render_forward_clustered.cpp` (`fellow_fallback_specializations`,
`_render_list_template`), `fellow_settings.*` (stats `pipeline_fallback_draws`, `pipeline_ubershader_draws`);
`FELLOW_PIPELINE_LOG` lists the misses.

## Problem

Godot draws with the ubershader (one pipeline per material reading the specialization from push constants; the colour
pass's has 168 registers against 72-96) while a draw's specialized pipeline compiles in the background. Stock asks for
the same specializations all the time, and compiles nothing during the benchmark's measured flythroughs; FellowEngine's
builds compile 20-130 there (`m11_f168`; milestone 10's too). Some of FellowEngine's specialization bits follow the
frame: 0057's six (what the light cluster holds), light tile culling (0049), the light fields early (0045, with the
frame's positional lights), and autotune's evaluations switch the tile culling and the shadow pyramids' bits (0005) on
and off. Each new combination costs every material in view a compilation, drawn with the ubershader meanwhile: slower
frames, and on a first run (cold pipeline cache) long ones.

## Change

When a draw's pipeline is not compiled yet (its compilation is requested as before), the draw first looks for a compiled
pipeline that differs only in bits whose other value gives the same image in any frame:

* 0057's bits clear (the code is compiled and its loops run no iteration);
* light tile culling off (the light loops visit every light of the cell; 0049 is exact);
* the shadow pyramids' early-outs off (0005; exact);
* the light fields early the other way (0045; the same values reach the same expressions);
* all of those at the defaults.

Not the bits that change what the CPU writes for the shader (persistent instances) or that need data a frame may not
have (the pyramids or the tile masks on). When none is compiled, the last candidate (everything per-frame at its
default, valid in any frame) is requested too, so that later changes of the bits find it. Only the serial recording
path; parallel recording goes on falling back to it as before.

## Verification

lavapipe: PBR Lab captures and the Lights Lab sequence, on against off: 0 differing pixels; the sequence uses
fallbacks after its first compilations.

## Measured

`m12_f170_fallback` (defaults with autotune on, Godot's pipeline cache off so that every run compiles; off / on, two
runs each, between processes):

| scene | ubershader binds off → on | fallback binds | compilations during measurement off → on | frame off → on (ms) |
|---|---|---|---|---|
| PBR Lab | 134, 217 → 0, 14 | 850, 65 | 40, 31 → 53, 52 | 2.19, 2.30 → 2.23, 2.22 |
| Lights Lab | 69, 6 → 0, 0 | 9, 11 | 8, 3 → 6, 6 | 2.16, 2.28 → 2.17, 2.17 |
| Bistro interior | 1, 1 → 0, 0 | 1, 1 | 1 → 1 | 3.01, 3.06 → 3.01, 3.08 |
| Foliage Lab | 16, 32 → 13, 17 | 18-19 | 9, 21 → 30, 30 | 2.82, 2.74 → 2.84, 2.79 |
| Sponza | 275, 282 → **3357**, 287 | 4 | 69 → **134** | 6.00, 5.94 → **6.37**, 6.01 |
| Stress Lab | 137, 97 → **2069**, 112 | 10-72 | 40-64 → 70-94 | 4.57, 4.52 → 4.60, 4.32 |

Where the bits change after their pipelines were compiled (PBR Lab, Lights Lab) the fallback replaces the ubershader
entirely. In Sponza and Stress Lab the draws that miss are new materials and passes coming into view, for which nothing
is compiled yet; the all-defaults pipeline the fallback then asks for doubles their compilations and delays the ones
they need (Sponza's first run: 3357 ubershader binds against 275).

## Decision

REJECT (left in, off). The frames it saves are few, and asking for the extra pipelines costs more where materials are
compiled for the first time. Compiling the right specializations earlier (at load time, with the bits the frame will
use) is what would remove those frames; the bench's runs with a warm pipeline cache (every measurement since milestone
2) do not show them as a cost.
