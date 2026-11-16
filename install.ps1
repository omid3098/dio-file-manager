# Installs Dio File Manager for the current Windows user (no administrator rights) and makes it the program
# Windows opens folders with, Win+E included. Run it again to update.
#   irm https://github.com/omid3098/dio-file-manager/releases/latest/download/install.ps1 | iex
# To give folders and Win+E back to Explorer:
#   & "$env:LOCALAPPDATA\Programs\Dio File Manager\dio-file-manager.exe" --default-fm off

& {
    $ErrorActionPreference = 'Stop'
    $ProgressPreference = 'SilentlyContinue'   # the progress bar slows the download in Windows PowerShell 5.1
    [Net.ServicePointManager]::SecurityProtocol = [Net.ServicePointManager]::SecurityProtocol -bor [Net.SecurityProtocolType]::Tls12

    $url = 'https://github.com/omid3098/dio-file-manager/releases/latest/download/dio-file-manager.exe'
    $dir = Join-Path $env:LOCALAPPDATA 'Programs\Dio File Manager'
    $exe = Join-Path $dir 'dio-file-manager.exe'
    $new = "$exe.download"

    New-Item -ItemType Directory -Force $dir | Out-Null
    Write-Host 'Downloading Dio File Manager...'
    Invoke-WebRequest -UseBasicParsing -Uri $url -OutFile $new

    # A running copy cannot be replaced, but it can be renamed; it keeps running until its window closes.
    if (Test-Path $exe) {
        try { Remove-Item $exe -Force }
        catch {
            $old = Join-Path $dir 'dio-file-manager.old.exe'
            try { Remove-Item $old -Force -ErrorAction SilentlyContinue } catch {}
            if (Test-Path $old) { $old = Join-Path $dir ('dio-file-manager.old-{0:yyyyMMddHHmmss}.exe' -f (Get-Date)) }
            Move-Item $exe $old
        }
    }
    Move-Item $new $exe

    $lnk = Join-Path ([Environment]::GetFolderPath('Programs')) 'Dio File Manager.lnk'
    $shortcut = (New-Object -ComObject WScript.Shell).CreateShortcut($lnk)
    $shortcut.TargetPath = $exe
    $shortcut.WorkingDirectory = $dir
    $shortcut.IconLocation = "$exe,0"
    $shortcut.Save()

    # Not Start-Process -Wait: turning it on leaves a small Win+E helper running, and -Wait would wait for it too.
    $p = Start-Process -FilePath $exe -ArgumentList '--default-fm', 'on' -PassThru -WindowStyle Hidden
    $p.WaitForExit()
    if ($p.ExitCode -ne 0) { throw "Dio File Manager is installed in $dir, but Windows did not take it as the folder program (exit code $($p.ExitCode))." }

    Write-Host "Dio File Manager is installed in $dir and opens folders now. Press Win+E to try it."
    Write-Host "To give folders back to Explorer: & `"$exe`" --default-fm off"
}
