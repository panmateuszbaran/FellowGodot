# FellowEngine — milestone 36 report (ice and hoarfrost)

Status: **measured** — lavapipe and the RTX 5070 Ti Laptop (`m36_f313`, `m36_s19`, `m36_s23`, `m36_s25`, 2026-11-03).
Branch `fellow/geometry`. Decision record `docs/decisions/0136-ice-and-frost.md`.

## 0. Result

Below freezing the weather's ground freezes: the puddles turn to dark clear ice that no longer ripples, white at
their thin edges, and the wet film to a glaze; on a clear cold night hoarfrost grows on what faces the sky — fine
grey-white grains spreading into a veil the cobbles show through, glinting in the sun — and both thaw when it warms.

## 1. Development

* `FellowRain3D.freeze` and `.frost` (0-1) go to the renderer with the wetness and the snow; the weather's
  specialization bit is on while either is above zero.
* The ice: the ripples times `1 − freeze`; the puddles 4 % milky, roughness 0.05, a white rough rim at their edges.
* The frost: on what faces up (a third on walls, none underneath), where the rain would reach; grains from the
  puddles' noise at 5 and 20 cm, a veil of 20-50 % grey white, matte, one crystal in 14 glinting.
* A FellowWeather3D's ground model below 0 °C freezes the water in the rain node's `freezing_time` (600 s at −5 °C)
  and grows the frost when nothing falls (a fifth as fast under a full deck); both go above 0 °C. It creates its rain
  node in the cold and settles the ground frozen and frosted when ready.
* The first frost (patches of 20 cm, white over 85 %) was patchy snow (lavapipe). The first RTX look (`m36_s19`) turned
  Bistro's street white as snow — the sky mirrored at a grazing angle by crystals at roughness 0.5: matte and thinner
  since. The frozen puddles' first rims (half white over much of the puddle) whitened whole puddles (`m36_s23`):
  narrowed to the edges.

## 2. Verification

* Off is the previous image (lavapipe, against milestone 35's build): Rain Lab raining without drops, Water Lab, the
  wind meadow under snow — 0 pixels differing.
* Lavapipe, the test scene: frozen puddles dark with pale rims; frost at 0.4, 0.6 and 1 — grains, a speckled cover, a
  veil. The ground model at −5 °C under a clear sky: frost 0.147 / 0.587 / 1 at 0.5 / 2 / 4 s, 200 times as fast, as
  expected (0.293 per second).

## 3. Performance

Bistro exterior clear at −4 °C, the ground held, wet against frozen and frosted, 1080p, 2 runs each
(`m36_f313_ice`, fellow234): the depth pre-pass +0.06 ms; the frame not measurable (the first build's runs noisy).

## 4. Showcase

Bistro on a frosty morning, the sun 9° up (`m36_s23_bistro_frost`, `m36_s25_bistro_ice`; `bistro_frost_morning.jpg`):
hoarfrost over the cobbles, the pavement bare by the walls under the awnings, the frozen puddles dark with pale edges.
The thaw (`m36_s25_bistro_thaw_loop`; `loop_bistro_thaw.webp`): the weather turning warm, the frost going in grains,
the ice after it, the street left wet.
