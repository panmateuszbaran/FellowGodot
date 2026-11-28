# FellowEngine — the three gaps after milestone 239

Status: assessment, 2026-11-26. A follow-up to `fellow_gap_analysis_m230.md`. The owner, after it: "these main gaps
sound like something we must do". This is what was done about each, what it changed in the scores, and what is left
— most of it measurements on the RTX, which wait for the owner's go-ahead (no GPU work since milestone 230).

## 1. What was done

| gap (m230) | milestones | what the engine has now | checked on | left |
|---|---|---|---|---|
| **characters as seen** (the largest visible gap) | 232-236 | MakeHuman people made in the cloud (`tools/fellow_characters`, CC0 assets, seven people), worn over the mannequins (retargeting, the player's hands and feet on the holds by IK); faces with ARKit's 52 shapes, blinking, talking; eyes that look (eye bones, the face's gaze in the head); moods (glad, surprised, in pain, cross); hair blended at its edges and cut under hats | lavapipe renders | 63 people's GPU cost; skin textures stay MakeHuman's (colour only — a micro-normal map was tried, invisible at play distances, dropped) |
| **many shadowed lights** | 231, 238 | omni and spot shadows: static casters cached per face, only movers drawn (exact); a budget of shadow redraws per frame (an option) | lavapipe, bit for bit / 45.6 dB at budget 4 | the RTX frame: the osteria, the square at night |
| **geometric detail at scale** | 237, 239 | Nanite's level-of-detail scheme for large static surfaces: a hierarchy of 128-triangle clusters simplified from each other, drawn as the cut just fine enough on screen, no cracks; instanced copies each cut; one indirect draw per instance; built in parallel and cached | lavapipe: 81-86 % fewer triangles than stock submits at Godot's LOD quality | the RTX frame (CPU walks against GPU triangles); the walk on the GPU, occlusion by the depth pyramid, streaming of the levels |

## 2. The scores again (judgment, as in m230)

| area | m230 | now | why |
|---|---|---|---|
| characters as seen | the largest visible gap | **medium** | people with faces, eyes and moods instead of mannequins; still mid-poly bodies and simple skins, far from MetaHuman's scans |
| many shadowed lights | large | **medium** | a moving crowd under lamps costs the movers, not the scene; MegaLights' hundreds of lights are another class (stochastic, ray-traced) |
| geometric detail at scale | large | **medium** (on paper) | the scheme is Nanite's; without a software rasteriser for tiny triangles, GPU-driven culling and streaming it is not Nanite, and its frame cost is unmeasured |

The others are as in m230: weather, water and fire ahead; GI, sun shadows and materials medium; tools, platforms and
content large.

## 3. What next

1. **Measure on the RTX** (when the owner allows): the cache of the lights' shadows (osteria), the budget (the square
   at night), 63 people with faces, the cluster hierarchy on a dense scene. Every number above is from lavapipe.
2. Depending on those: the hierarchy's walk on the GPU (with occlusion culling) if its CPU cost shows; the budget on by
   default in the showcase if it pays.
3. Then the medium gaps that show in pictures: sun shadows (a virtual shadow map's sharpness at every distance) and
   GI.

## 4. Measured on the RTX (2026-11-27)

The owner allowed the GPU again; milestone 242's build on the RTX laptop at 1920×1080 (decisions 0373, 0374, 0379,
0380, 0384 have the tables):

| what | result |
|---|---|
| the lamps' shadow cache, the osteria at night | shadow draws −43 %, primitives −45 %, GPU 8.85 → 8.19 ms (−7.5 %), render thread −0.45 ms |
| the shadow budget, the square at night | no change: nothing there redraws a shadow every frame — it stays an option |
| 63 people with faces and eyes, the square by day | +0.4 ms GPU, +1.7 ms render thread; the frame under 10 ms |
| the cluster hierarchy, its test scene | GPU 40-46 % of full detail's (3.0 → 1.2 ms on the ground view); the walks +0.15-0.75 ms on the render thread, halved by walking them in parallel (now on by default) |

So the three gaps' engine work holds up on the GPU. The owner's next concern, the people's realism, is being worked on:
the skin by region and wet eyes (milestone 243), then the hair as strands, then the faces' shapes.

## 5. After milestone 247 (2026-11-27)

The medium gaps, without the GPU (the owner: "continue without GPU"):

| gap | milestones | what changed | waits for the RTX |
|---|---|---|---|
| materials | 241 | a sheen lobe (velvet, wool, satin: glTF's `KHR_materials_sheen`); the showcase's suits and hats wear it | its cost (a few ALU on the materials that use it) |
| characters as seen | 243-245, 247 | skin by region (glossy lips and T-zone, an oily second lobe, light through the ears), wet high-poly eyes shaded by the lids, the hair's highlights across its painted strands, each face its own (MakeHuman's face modifiers, a little asymmetry), the small parts' draw calls cut by a quarter; strands traced from the hair (an engine feature) but off in the showcase (a haze at its distances) | the square's 63 people's render thread after 247 (0374: +1.7 ms) |
| sun shadows | 246 | the cascades can be fitted to the depths the view shows (sample distribution shadow maps, an option): against a 16384 atlas a courtyard 25.5 → 29.2 dB, looking down 29.8 → 33.0 dB | its cost; on in the showcase or not (its views do not fit the cloud's memory on lavapipe any more) |

Scores (judgment): characters as seen medium (better faces; still MakeHuman's painted textures and mid-poly bodies —
MetaHumans, usable in Godot since June 2025, would be the next class, on the owner's decision); sun shadows medium
(closer where views end short; not virtual shadow maps' texel per pixel); materials medium (one lobe more; no layered
materials like Substrate); GI medium (unchanged since 167-168). Tools, platforms and content stay large.

## 6. After milestone 251 (2026-11-28)

The owner: "continue the journey to AAA quality" (with the GPU allowed again; its job queue was not running, so the
RTX measurements of 241-251 are queued in `.tools\queue\m251_rtx_job.bat`).

| gap | milestones | what changed |
|---|---|---|
| materials | 248, 250, 251 | glTF's PBR extensions read and written (an import setting, off): sheen with its texture, clear coat, anisotropy, IOR and specular (248); a new thin-film iridescence layer (250: `IRIDESCENCE*`, BaseMaterial3D's Iridescence group); glass — transmission as a refraction tinted by its colour and weighed by 1 − Fresnel, volumes bent by their IOR and absorbing along their thickness (251). Khronos' samples (velvet sofa, ToyCar, brushed metal, abalone, the amber dragon, the candle-holder) as their references, within Godot's ranges |
| characters as seen | 249 | each person's crevices' occlusion baked (Cycles, in its UV layout) and the skin's micro-relief (pores and furrows on a second UV map of one density): the skin less like plastic, its highlights broken close up; hair cards from the traced guides tried and rejected (not better than the painted shell) |

Scores (judgment): materials medium → **medium-small** (glTF's PBR set covered except dispersion and diffuse
transmission; no layer stacking like Substrate's slabs, glass screen-space only, no tinted shadows); characters as seen
medium (skin closer up; hair and the painted textures remain MakeHuman's); GI and sun shadows medium (unchanged; the
depth fit's measurements queued); tools, platforms and content large.

Next: the RTX measurements when the queue runs (the layers' cost at 1440p, the people's small parts and skin on the
square, the depth fit's cost and look); then GI or sharper sun shadows (a virtual shadow map's texel per pixel).
