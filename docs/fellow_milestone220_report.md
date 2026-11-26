# FellowEngine — milestone 220 report (the gas volumes' steps recorded together)

Status: **FINAL** (2026-11-26). Branch `fellow/geometry`. GPU: the RTX measured the rooftops with the setting on and off
(job f619). Decision 0362.

## 0. Result

* **`rendering/fellow/fluid/gas_batch_steps`** (on): the gas volumes' steps of one physics frame are recorded
  together — the k-th pass of every volume in one compute list — so the render graph handles about 80 compute lists for
  the rooftops' five volumes instead of 385. Exact (the same picture byte for byte, the regression suite unchanged).
* On a slow CPU (the cloud's 2 vCPUs) 0.2 ms less a frame of recording and of the render graph's end with five
  volumes; on the RTX laptop no difference is measurable.

## 1. Open

* The rooftops' render thread spends 2-3 ms a frame; the gas's recording is a small part of it on a fast CPU.
