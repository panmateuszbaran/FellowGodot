# 0059 — While a specialization compiles: a compiled sibling instead of the ubershader

Status: **measuring** (`fellow144`) · Branch `fellow/geometry` · Setting `rendering/fellow/frame/pipeline_fallback`
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

Pending (`m12_f170_fallback`).
