This program will allow lists of windows to be tracked.

All windows on the desktop of the window containing this program will get tracked in the list.

You can make more lists in to switch between.

Hotkeys can move windows off the current desktop and on to another such that they won't be tracked for the current time item in the list.

 Current Hotkeys

    ALT+Q - Move all windows on this virtual desktop to another desktop.
	ALT+X - Move the top window on this virutal desktop to another desktop.
	ALT+Z - Move the last window moved off this virt desktop, back to this virt desktop.
	ALT+P - Swap all windows not on this virt desktop with all windows on this virt desktop.
	ALT+R - Move all windows not on this virt desktop to this virt desktop
	ALT+3 - Move virtual desktop previous (same as Win+Tab pick previous)
	ALT+4 - Move virtual desktop next (same as Win+Tab pick next)
	ALT+1 - WinGroup prev, all windows track into top group, move top group down one, move bottom of stack to top.
	ALT+2 - WinGroup next, all windows track into top groiup, move top group to bottom of stack, move 2nd group to top
	ALT+T - Add new win group. (Slow click name to name it)
	ALT+D - Delete Top win group.
	
This tool is mainly to organize windows, into named groups, then be able to flip through them to keep context.

Windows can be in multiple lists at once. If you want a window in multiple groups and its not in this one yet.
Flip to the other VirtDesktop ALT+3 find the window and hit ALT+X it'll move it to the other desktop. *NOTE* assumes we're using 2 virt desktops.

----

Hacking around on a weekend.

Found some interesting undocument VirtDesktop COM control code.

Found an old MSDN archive of a ListViewControl example.

Made this to manage virt virt desktops.

Will be MIT

