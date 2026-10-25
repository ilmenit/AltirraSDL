//	AltirraSDL - parent-console attachment for the Windows GUI-subsystem build.
//
//	The Windows executable is linked as a GUI application so launching it
//	(Explorer, shortcuts, Start-Process) does not open a console window.
//	When it is started from a console (cmd, PowerShell) with stdout/stderr
//	not redirected, ATAttachParentConsoleIfNeeded() attaches to that console
//	so --help, errors and logs still appear there. Redirected handles (pipes
//	and files set up by a parent, as test harnesses and scripts do) are left
//	untouched, and no new console is ever created.

#pragma once

#ifdef _WIN32
void ATAttachParentConsoleIfNeeded();
#endif
