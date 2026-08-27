; license:BSD-3-Clause
; Votrax SC-01 SAPI voices -- installer.
;
; What it does: places both bitnesses of the engine DLL, registers them
; (each writes its own registry view, so 32-bit JAWS-era hosts and 64-bit
; hosts both see the voices), and drops the NVDA add-on bundle where the
; user can reach it.
;
; ROMs: the chip's 512-byte internal mask ROM is not ours to ship.  If
; sc01.bin / sc01a.bin sit NEXT TO THE INSTALLER when it runs, they are
; copied in and the voices register immediately.  Otherwise, copy them into
; the x86 and x64 folders later and use the Start-menu "Re-register Votrax
; voices" entry -- registration only creates tokens for ROMs it finds, so
; it is safe to run any number of times.
;
; Build:  .\build.ps1 -Target all   then   ISCC installer\votraxsc01.iss

#ifndef StageDir
#define StageDir "..\build"
#endif
#define AppVer "0.1.0"

[Setup]
AppId={{E5A0B7C2-5C01-4F6D-8B2A-90D1C4E7F3A8}
AppName=Votrax SC-01 speech
AppVersion={#AppVer}
AppPublisher=tgeczy
AppSupportURL=https://github.com/tgeczy/votraxsc01-nvda
DefaultDirName={autopf}\Votrax SC-01
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
OutputDir={#StageDir}\out
OutputBaseFilename=votraxsc01-{#AppVer}-setup
DisableProgramGroupPage=yes
UninstallDisplayName=Votrax SC-01 speech {#AppVer}

[Files]
Source: "{#StageDir}\x86\votrax_sapi.dll"; DestDir: "{app}\x86"
Source: "{#StageDir}\x64\votrax_sapi.dll"; DestDir: "{app}\x64"; Check: Is64BitInstallMode
Source: "{#StageDir}\say01.exe"; DestDir: "{app}"
Source: "{#StageDir}\votraxsc01-{#AppVer}.nvda-addon"; DestDir: "{app}"
Source: "..\roms\README.md"; DestDir: "{app}"; DestName: "ROMS-README.md"
; ROMs found beside the installer ride along into both engine folders.
Source: "{src}\sc01.bin"; DestDir: "{app}\x86"; Flags: external skipifsourcedoesntexist
Source: "{src}\sc01.bin"; DestDir: "{app}\x64"; Flags: external skipifsourcedoesntexist; Check: Is64BitInstallMode
Source: "{src}\sc01a.bin"; DestDir: "{app}\x86"; Flags: external skipifsourcedoesntexist
Source: "{src}\sc01a.bin"; DestDir: "{app}\x64"; Flags: external skipifsourcedoesntexist; Check: Is64BitInstallMode

[Icons]
Name: "{autoprograms}\Re-register Votrax voices"; Filename: "{app}\register.cmd"; WorkingDir: "{app}"

[Run]
Filename: "{app}\register.cmd"; StatusMsg: "Registering Votrax voices..."; Flags: runhidden

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\x64\votrax_sapi.dll"""; RunOnceId: "Unreg64"; Check: Is64BitInstallMode
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/u /s ""{app}\x86\votrax_sapi.dll"""; RunOnceId: "Unreg32"

[Code]
procedure CurStepChanged(CurStep: TSetupStep);
var
  Lines: TArrayOfString;
begin
  if CurStep = ssPostInstall then begin
    // register.cmd re-registers both views; kept as a file so the
    // Start-menu entry can re-run it after ROMs are added later.
    SetArrayLength(Lines, 4);
    Lines[0] := '@echo off';
    Lines[1] := 'if exist "%~dp0x64\votrax_sapi.dll" %windir%\System32\regsvr32.exe /s "%~dp0x64\votrax_sapi.dll"';
    Lines[2] := 'if exist "%~dp0x86\votrax_sapi.dll" if exist %windir%\SysWOW64\regsvr32.exe %windir%\SysWOW64\regsvr32.exe /s "%~dp0x86\votrax_sapi.dll"';
    Lines[3] := 'exit /b 0';
    SaveStringsToFile(ExpandConstant('{app}\register.cmd'), Lines, False);
  end;
end;
