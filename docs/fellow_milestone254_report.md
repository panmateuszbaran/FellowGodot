# FellowEngine — milestone 254 report (the game's window and the mouse)

Status: **FINAL** (2026-11-28). Branch `fellow/geometry`. Decision 0395.

The owner: when the game starts in a window the mouse is held inside it but the focus does not move to it — open it
in front with the focus, or behind without holding the mouse.

* `platform/windows/display_server_windows.cpp`: a captured or confined mouse takes the cursor only while one of the
  game's windows is in front; set from behind, it waits for the window's activation (a click, the taskbar, Alt+Tab).
  Losing the focus lets go of the cursor's clip too.
* The laptop's launcher (`Showcase - Dachy.bat`) waits for the game's window (at most 20 s) before its console closes:
  started by the window in front, the game may take the focus (closing at once, the console handed it back to
  Explorer and the game opened behind).
* Checked: the Windows build on the RTX laptop compiles and runs the showcase; Linux untouched.
