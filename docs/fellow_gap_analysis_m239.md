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
