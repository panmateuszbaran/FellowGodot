# 0273 — A skinned garment's backstop: kept on the body without colliders

Status: **KEEP** (opt-in per node) · Branch `fellow/geometry` · Code: `modules/fellow_fluid/fellow_cloth_3d.{h,cpp}`
(`skin_backstop`, `skin_backstop_distance`; the skinned normals; the statistics `skin_backstopped`, `skin_behind`) ·
Checks: `bench/cloth_check.gd` (two new lanes), headless and filmed on lavapipe.

## Problem

A garment following the skin (decision 0230) is kept within `skin_max_distance` of its skinned places, in every
direction: a skirt free to swing 12 cm swings 12 cm *into* the body too, and only colliders keep it out — a capsule
for each limb, which fit a body roughly, cost a test per particle each, and leave the cloth on the capsule rather than
on the skin. The readiness assessment listed it: no backstops (what character cloth in engines usually has — NvCloth's
and PhysX's backstop spheres, Unreal's Chaos cloth backstop).

## Change

Each particle's skinned normal is skinned with its place (the source mesh's normal at rest, turned by the same blend
of bones; interpolated over the substeps like the place). With `skin_backstop`, a free particle more than
`skin_backstop_distance` behind its skinned place along that normal is pushed back out along it in each substep, after
the distance limit and before the colliders. A half-space per particle, the skin's own shape: no colliders needed where
the garment's rest shape follows the body.

## Result

`cloth_check.gd`'s skirt on its circling, swaying, twisting body in the flags' gale (lavapipe, headless):

| Skirt following the skin (12 cm) | deepest behind its skinned place | deepest into the hips | the farthest from its skinned place | simulation |
|---|---:|---:|---:|---:|
| with the hips' collider (as before) | 10.8 cm | 0 | 12.0 cm | 311 µs |
| without it | 12.0 cm | 5.9 cm | 12.0 cm | 283 µs |
| **without it, backstop 5 mm** | **0.5 cm** | **0** | 7.3 cm | 290 µs |

Filmed: without the collider the hips show through the skirt as it swings; with the backstop it wraps them as with the
collider. Every other lane as before.

## Decision

KEEP, opt-in (`skin_backstop` off: the cloth as before). A garment whose rest shape follows the body can drop its
colliders for the backstop; colliders stay for what the rest shape does not cover (legs under a skirt, the other arm).

## Rejected / not done

* **Spheres per particle** (NvCloth's: a sphere behind each particle, its radius and offset painted): rounder near a
  convex body, but two numbers per particle to author; the half-space needs one, and a garment's particles are dense
  enough that their planes follow the body's curve.
* **Painted per-particle distances**: one distance per cloth for now (the max distance's paint, decision 0232, could
  carry it later).
