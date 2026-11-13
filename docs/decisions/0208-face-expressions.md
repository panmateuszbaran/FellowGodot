# 0208 — Faces with expressions, brows that follow the voice

Status: **KEEP** (`FellowFaceAnimator`: `set_expression()`, `expressions`, `speech_brows` 0.5) · Branch
`fellow/geometry` · Code: `modules/fellow_animation/fellow_face_animator.{h,cpp}` · Checks:
`benchmarks/project/bench/face_check.gd` (two lanes).

## Problem

Decision 0191's face speaks, blinks and (0197, 0202) looks: its brows, cheeks and the mouth's corners never move. A
character in a cutscene or a conversation shows what it feels — a smile, a frown, surprise — and speech moves more
than the mouth: the brows rise on stressed syllables (one of the commonest cues in procedural speech animation:
JALI's, Audio2Face's emotion layers). The readiness assessment listed "no emotions, brows".

## Change

* **Seventeen channels more**, on ARKit's shapes by default: the brows (inner up, down, outer up, each side), the
  mouth's corners (smile, frown), the cheeks' and eyes' squint, the eyes widened, the nose's sneer.
* **Expressions**: named sets of channel weights, faded in and out (`set_expression(name, weight, fade)`,
  `clear_expressions(fade)`), several at once (added, each channel at most 1, on top of the lip sync, the blinks and
  the gaze). Six built in, after the action units Ekman and Friesen give for the basic emotions — happy (AU 6+12),
  sad (1+4+15), angry (4+5+7+23), surprised (1+2+5+26), afraid (1+2+4+5+20+26), disgusted (9+15+16) — and the game's
  own (`expressions`: a name and its channels' weights; a built-in name there replaces the built-in one).
* **The brows on stressed syllables**: where the mouth reads the voice, its level rising `speech_emphasis_db` (6 dB)
  above its last half second's (a syllable after a pause, a stressed one) raises the inner brows by `speech_brows`
  (0.5) and the outer ones by 60 % of it, fading in 0.3 s, at most every quarter second; the signal `emphasized`
  (its strength) lets the game nod the head or gesture there.

## Checks (cloud: the CPU)

`face_check.gd` headless, the editor build and the release template the same:

| | expected | result |
|---|---|---|
| happy faded in (0.3 s) | smile 0.8, cheeks 0.5, eyes' squint 0.25, no brows | 0.8, 0.8, 0.5, 0.25, 0 |
| crossfaded to sad (0.5 s), a quarter second in | each at half: smile 0.4, inner brows 0.35 | happy 0.5, sad 0.5; 0.4, 0.35 |
| sad | inner brows 0.7, brows down 0.3, frown 0.6, no smile | 0.7, 0.3, 0.6, 0 (one expression left) |
| a custom wink, at once | the left lid closed (the right open), a little smile 0.3, sad's brows still there | 1.0, 0, 0.3, 0.7; listed with the six built-in ones |
| everything cleared (0.2 s) | no channel moved | the largest 0; no expression left |
| the brows on the test voice (six sounds after silences, from a timeline) | six raises (to 0.5), one at each sound's start, none in the first silence | six `emphasized` at 0.283, 0.883, 1.483, 2.083, 2.683, 3.283 s (each sound's start less the 0.04 s the mouth reads ahead, strength 1); each sound's peak 0.473 (0.5 eased in its first frame); the first silence 0 |

The earlier lanes (the analysis, the level, the blinks, the gaze, the vergence) the same; under Movie Maker the mouth
following the voice as before (/a/, /i/, /u/ at 0.997 of their shapes); the regression scene `face` exact (its test face
has none of the new shapes). A first version raised the brows twice on each sound (a held vowel stays above the
level's half-second average past the quarter second): now once a rise, again only after the level has come back within
half the threshold of its average.

## Cost

A face's frame 0.37-0.43 µs with the new channels and the level read for the brows (0.37-0.39 before; 100 faces
0.04 ms a frame, the release template on the cloud); on the laptop (job f401) 0.41-0.44 µs, 0.28 at milestone 96. The
laptop's lanes and listening: the same results.

## Decision

Keep: faces that show what the character feels, and brows that move with what it says.

## Rejected / not done

* **Emotion from the voice** (an analysis of its prosody choosing the expression): the game chooses it.
* **Head motion**: the `emphasized` signal; the head's bones are the animation's (or a `LookAtModifier3D`'s).
* **Asymmetric expressions** built in: a custom one can be (a wink).
