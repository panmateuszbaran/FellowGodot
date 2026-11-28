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
  mode is kept but nothing is clipped or captured and the cursor not hidden; the grab waits (`fellow_grab_pending`,
  looked at once a frame in `process_events`) and takes hold when one of the game's windows comes to the front (a
  click on it, the taskbar, Alt+Tab). Losing the focus also lets go of the cursor's clip (`ClipCursor(nullptr)` beside
  stock's `ReleaseCapture`) and the grab waits again. Nothing changes for a window in front, the usual case.
* **The launcher**: its console waits (at most 30 s) until the game's window is shown, then hands it the focus
  (`WScript.Shell.AppActivate`, from the console still in front: Windows lets the window in front pass the focus on),
  then closes. If Windows refuses all the same, the game is behind with the mouse free.

Not behind a setting: the stock behaviour this changes is a cursor trapped by a window without the focus, which no
project wants; the frame and every other platform are untouched.

## Checks

* The Windows build (the RTX laptop's queue, milestone 254): compiles, the showcase imports and runs.
* The Linux build is untouched (the change is in the Windows display server only).

## Decision

KEEP.

## Rejected

* **Trusting the window's activation** (the first version): refused the foreground, a window shown is still activated
  within its own thread (`WM_ACTIVATE` with `WA_ACTIVE`) while another process's window stays in front — the first
  version grabbed on that activation, and the owner saw the same trapped cursor. The grab now asks Windows which
  window is in front.
* **Polling for the window's title** (`tasklist /v`) and closing: the title exists before the window is shown, the
  console closed too early to pass on the focus.

* **Forcing the focus** (`AttachThreadInput` to the foreground window's thread, or a synthetic Alt press before
  `SetForegroundWindow`): it defeats Windows' guard against windows stealing the focus; the launcher waiting gives the
  game the right to it the ordinary way.
