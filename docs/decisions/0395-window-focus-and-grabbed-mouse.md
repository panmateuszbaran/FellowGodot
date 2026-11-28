# 0395 — The game's window: to the front, and the mouse not trapped behind it

Status: **KEEP** · Branch `fellow/geometry` · Code: `platform/windows/display_server_windows.cpp`
(`fellow_foreground_is_ours`, `_set_mouse_mode_impl`, `_process_activate_event`) · The laptop's launcher
(`.tools\Showcase - Dachy.bat`)

## Problem

The owner, on Windows: when the game starts in a window the mouse is held inside the window, but the focus does not
move to it — either open it in front with the focus, or open it behind without holding the mouse.

Two things together. The launcher starts the game (`start "" godot...`) and closes at once; Windows gives a new window
the focus only if what started it is still in front, so with the launcher's console gone the focus went back to
Explorer and the game opened behind it (its `SetForegroundWindow` refused, its taskbar button flashing). Then the
showcase captured the mouse at its start (`Input.mouse_mode = MOUSE_MODE_CAPTURED`), and stock Godot's Windows display
server clips the cursor to the window (`ClipCursor`) whether or not the window is in front: the cursor trapped over a
window that got no keys and no clicks until the owner found it in the taskbar.

## Change

* **The engine** (Windows): a grabbed mouse mode (captured, confined, confined hidden) takes the cursor only while one
  of the process's windows is in front (`GetForegroundWindow`'s process is this one). Set while the game is behind, the
  mode is kept but nothing is clipped or captured and the cursor not hidden; when the window comes to the front (a
  click on it, the taskbar, Alt+Tab) `_process_activate_event` sets the mode again and the grab takes hold. Losing the
  focus now also lets go of the cursor's clip (`ClipCursor(nullptr)` beside stock's `ReleaseCapture`). Nothing changes
  for a window in front, the usual case.
* **The launcher**: its console waits (at most 20 s, polling `tasklist` for the window's title) until the game's
  window is up before it closes, so the game, started by the window in front, may take the focus itself.

Not behind a setting: the stock behaviour this changes is a cursor trapped by a window without the focus, which no
project wants; the frame and every other platform are untouched.

## Checks

* The Windows build (the RTX laptop's queue, milestone 254): compiles, the showcase imports and runs.
* The Linux build is untouched (the change is in the Windows display server only).

## Decision

KEEP.

## Rejected

* **Forcing the focus** (`AttachThreadInput` to the foreground window's thread, or a synthetic Alt press before
  `SetForegroundWindow`): it defeats Windows' guard against windows stealing the focus; the launcher waiting gives the
  game the right to it the ordinary way.
