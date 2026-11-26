# FellowEngine — milestone 240 report (glances)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. Decision 0382. No GPU work: checked on lavapipe.

## 0. Result

The square's talking groups no longer stare into their circle: each person glances at the others — mostly at whoever
is talking —, turning the head part of the way and the eyes the rest, and looks back at the player when it comes near.
The player looks at the person it talks to.

Tried and dropped on the way: a micro-normal map for the skin (pores and creases from noise): at the showcase's
distances it disappears in the mipmaps (61.2 dB against the smooth skin even in a face-filling close-up).

## 1. Changes

* `showcase/scripts/rooftops_crowd.gd`: `_mates` (the groups and the brazier's ring), `_glance` (where each looks, its
  eyes' target at the other's head), the head's share (0.45) through the attention's LookAtModifier3D, `_player_gaze`.
* Docs: decision 0382, the showcase README, the changelog.

## 2. Checks

* A group of four, the player far: 15 glances in 10 s, spread over the talkers, the others and ahead; the eyes within
  their range.
* The crowd with a conversation and a knock-down: no error.
