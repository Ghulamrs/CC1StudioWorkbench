<#
.SYNOPSIS
    Starts ed1gui, optionally builds, and saves a picture of its window.

.DESCRIPTION
    For looking at the editor on a machine you are not sitting at - which is
    how this front end has been checked throughout, since it is built over ssh
    from a Mac.

    Two things about it are deliberate.

    It captures the editor's own window with PrintWindow rather than grabbing
    the screen. That gets the window even when something is in front of it, and
    more to the point it captures nothing else that happens to be on the
    desktop.

    A window needs a desktop, and an ssh session has none - it is session 0,
    and a program started there has nowhere to draw. So from a remote shell
    this has to be run in the logged-on session, which a scheduled task with
    /IT will do:

        schtasks /create /tn ed1shot /f /sc once /st 23:59 /it ^
                 /tr "powershell -NoProfile -ExecutionPolicy Bypass -File C:\path\show.ps1"
        schtasks /run /tn ed1shot
        schtasks /delete /tn ed1shot /f

    Run from the machine itself, it just works.

.EXAMPLE
    .\show.ps1 -Project C:\Users\me\Editor -Files examples\smart.cpp -Build

.EXAMPLE
    .\show.ps1 -Project C:\work\thing -Out C:\temp\thing.png

.EXAMPLE
    .\show.ps1 -Files examples\hello.c -Keys "{F9}{F8}" -Panel Debug
#>
param(
    # The directory holding ed1.json, and where paths are counted from.
    [string]$Project = ".",

    # Files to open, each getting a tab. The last one ends up in front.
    [string[]]$Files = @(),

    # Where to write the picture.
    [string]$Out = "show.png",

    # Press F7 once the window is up, and wait for the compiler.
    [switch]$Build,

    # Anything else to press, in SendKeys spelling - "{F9}{F8}" sets a
    # breakpoint on the caret's line and starts the debugger. Sent after the
    # build, if there is one, and before the panel is chosen.
    [string]$Keys = "",

    # How long to wait after those keys. Starting a debugger takes longer than
    # pressing a key usually does, since it compiles first.
    [int]$KeySeconds = 12,

    # The editor to run. Found beside this script by default.
    [string]$Editor = "",

    # How long to wait for the build, in seconds.
    [int]$BuildSeconds = 10,

    # Grab the screen where the window is, rather than asking the window to
    # draw itself. PrintWindow renders one window and nothing on top of it,
    # which is right nearly always and wrong for a dialog: an About box is its
    # own window, so a picture of the editor with one in front of it has to
    # come off the screen.
    [switch]$WithDialogs,

    # Which of the panel's tabs to leave showing: Console, Debug or Assembly.
    [ValidateSet("", "Console", "Debug", "Assembly")]
    [string]$Panel = ""
)

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms
Add-Type @"
using System;
using System.Runtime.InteropServices;
public class Ed1Window {
    [DllImport("user32.dll")] public static extern bool PrintWindow(IntPtr h, IntPtr dc, uint flags);
    [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr h, out RECT r);
    [DllImport("user32.dll")] public static extern bool EnumWindows(EnumProc callback, IntPtr p);
    [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr h, out uint pid);
    [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr h);
    public delegate bool EnumProc(IntPtr h, IntPtr p);
    [StructLayout(LayoutKind.Sequential)] public struct RECT { public int L, T, R, B; }
}
"@

if ($Editor -eq "") {
    $here = Split-Path -Parent $MyInvocation.MyCommand.Path
    foreach ($guess in @("$here\x64\Release\ed1gui.exe", "$here\x64\Debug\ed1gui.exe")) {
        if (Test-Path $guess) { $Editor = $guess; break }
    }
}
if ($Editor -eq "" -or -not (Test-Path $Editor)) {
    Write-Error "no ed1gui.exe - build winforms\ed1gui.vcxproj, or name one with -Editor"
    exit 1
}

$Project = (Resolve-Path $Project).Path
$arguments = @($Project)
foreach ($file in $Files) {
    # Relative names are taken as relative to the project, which is how anyone
    # naming one at this point is thinking about it.
    $full = if ([System.IO.Path]::IsPathRooted($file)) { $file } else { Join-Path $Project $file }
    $arguments += $full
}

Get-Process ed1gui -ErrorAction SilentlyContinue | Stop-Process -Force
$editorProcess = Start-Process -FilePath $Editor -ArgumentList $arguments -PassThru `
                               -WorkingDirectory $Project
Start-Sleep -Seconds 6

# The one top-level window of that process wide enough to be the editor.
$script:handle = [IntPtr]::Zero
$look = [Ed1Window+EnumProc]{
    param($window, $unused)
    $owner = 0
    [void][Ed1Window]::GetWindowThreadProcessId($window, [ref]$owner)
    if ($owner -eq $editorProcess.Id) {
        $box = New-Object Ed1Window+RECT
        [void][Ed1Window]::GetWindowRect($window, [ref]$box)
        if (($box.R - $box.L) -gt 300) { $script:handle = $window; return $false }
    }
    return $true
}
[void][Ed1Window]::EnumWindows($look, [IntPtr]::Zero)

if ($script:handle -eq [IntPtr]::Zero) {
    Write-Error "ed1gui started but has no window - is this an interactive session?"
    if (-not $editorProcess.HasExited) { $editorProcess.Kill() }
    exit 1
}

[void][Ed1Window]::SetForegroundWindow($script:handle)
Start-Sleep -Milliseconds 700

if ($Build) {
    [System.Windows.Forms.SendKeys]::SendWait("{F7}")
    Start-Sleep -Seconds $BuildSeconds
}

if ($Keys -ne "") {
    [System.Windows.Forms.SendKeys]::SendWait($Keys)
    Start-Sleep -Seconds $KeySeconds
}

if ($Panel -ne "") {
    $which = @{ "Console" = "^1"; "Debug" = "^2"; "Assembly" = "^3" }[$Panel]
    [System.Windows.Forms.SendKeys]::SendWait($which)
    Start-Sleep -Milliseconds 600
}

$box = New-Object Ed1Window+RECT
[void][Ed1Window]::GetWindowRect($script:handle, [ref]$box)

if ($WithDialogs) {
    $wide = $box.R - $box.L
    $tall = $box.B - $box.T
    $shot = New-Object System.Drawing.Bitmap($wide, $tall)
    $onto = [System.Drawing.Graphics]::FromImage($shot)
    $onto.CopyFromScreen($box.L, $box.T, 0, 0, (New-Object System.Drawing.Size($wide, $tall)))
    $shot.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
    $onto.Dispose()
    $shot.Dispose()
    Write-Output "saved $Out"
    if ($editorProcess -ne $null -and -not $editorProcess.HasExited) { $editorProcess.Kill() }
    exit 0
}
$picture = New-Object System.Drawing.Bitmap ($box.R - $box.L), ($box.B - $box.T)
$canvas = New-Object System.Drawing.Graphics
$canvas = [System.Drawing.Graphics]::FromImage($picture)
$deviceContext = $canvas.GetHdc()
# 2 is PW_RENDERFULLCONTENT, which is what gets the text rather than a blank.
[void][Ed1Window]::PrintWindow($script:handle, $deviceContext, 2)
$canvas.ReleaseHdc($deviceContext)
$picture.Save($Out, [System.Drawing.Imaging.ImageFormat]::Png)
$canvas.Dispose()
$picture.Dispose()

if (-not $editorProcess.HasExited) { $editorProcess.Kill() }
Write-Output "$Out written - $((Get-Item $Out).Length) bytes"
