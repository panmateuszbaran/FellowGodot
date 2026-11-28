# FellowEngine — milestone 254 report (the game's window and the mouse)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0395.

The owner: when the game starts in a window the mouse is held inside it but the focus does not move to it — open it
in front with the focus, or behind without holding the mouse.

* `platform/windows/display_server_windows.cpp`: a captured or confined mouse takes the cursor only while one of the
  game's windows is in front (`GetForegroundWindow`); set from behind, it waits, looked at once a frame, until the
  window comes to the front (a click, the taskbar, Alt+Tab). Losing the focus lets go of the cursor's clip too. The
  first version trusted the window's activation — but a window refused the foreground is still activated within its
  thread, and the owner saw the same trapped cursor.
* The laptop's launcher (`Showcase - Dachy.bat`) waits for the game's window to be shown (at most 30 s) and hands it
  the focus (`WScript.Shell.AppActivate`) before its console closes (closing at once, the console handed the focus
  back to Explorer and the game opened behind).
* Checked: the Windows build on the RTX laptop compiles and runs the showcase; Linux untouched.
