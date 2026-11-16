One file for Windows 11, written in C.

Install it, or update it, from PowerShell. This also makes it the program Windows opens folders with, Win+E included:

```powershell
irm https://github.com/omid3098/dio-file-manager/releases/latest/download/install.ps1 | iex
```

Or download `dio-file-manager.exe` below, put it in a folder of your own and run it. The file is not signed, so Windows may warn you the first time.

To give folders and Win+E back to Explorer, run `dio-file-manager.exe --default-fm off`.
