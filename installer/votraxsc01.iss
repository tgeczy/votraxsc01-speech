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
#define AppVer "1.1.0"

[Setup]
AppId={{E5A0B7C2-5C01-4F6D-8B2A-90D1C4E7F3A8}
AppName=Votrax SC-01 speech
AppVersion={#AppVer}
AppPublisher=tgeczy
AppSupportURL=https://github.com/tgeczy/votraxsc01-speech
DefaultDirName={autopf}\Votrax SC-01
ArchitecturesInstallIn64BitMode=x64compatible
PrivilegesRequired=admin
Compression=lzma2
SolidCompression=yes
OutputDir={#StageDir}\out
OutputBaseFilename=votraxsc01-{#AppVer}-setup
DisableProgramGroupPage=yes
UninstallDisplayName=Votrax SC-01 speech {#AppVer}
; Full version metadata: unsigned binaries with no VERSIONINFO score
; worse with Defender's reputation heuristics, and this installer was
; flagged as a false positive on day one.  Metadata alone doesn't clear
; it, but every legitimacy signal helps while download reputation builds.
VersionInfoVersion={#AppVer}.0
VersionInfoDescription=Votrax SC-01 speech voices installer
VersionInfoProductName=Votrax SC-01 speech
VersionInfoCompany=tgeczy
VersionInfoCopyright=BSD-3-Clause; engine core (c) MAME project

[Files]
Source: "{#StageDir}\x86\votrax_sapi.dll"; DestDir: "{app}\x86"
Source: "{#StageDir}\x64\votrax_sapi.dll"; DestDir: "{app}\x64"; Check: Is64BitInstallMode
Source: "{#StageDir}\say01.exe"; DestDir: "{app}"
Source: "{#StageDir}\votraxsc01-{#AppVer}.nvda-addon"; DestDir: "{app}"
Source: "..\roms\README.md"; DestDir: "{app}"; DestName: "ROMS-README.md"
; License notices travel with the binaries (BSD-3 for this project and
; MAME's core, BSD-2 for the CMU Pronouncing Dictionary inside the DLLs).
Source: "..\LICENSE"; DestDir: "{app}"; DestName: "LICENSE.txt"
Source: "..\THIRD_PARTY_LICENSES.md"; DestDir: "{app}"
Source: "..\data\LICENSE-CMUdict.txt"; DestDir: "{app}"
; A static, versioned file -- NOT generated at install time.  Writing a
; fresh batch script from installer code and pointing a Start-menu entry
; at it is textbook dropper behavior to an antivirus heuristic, and was
; a likely contributor to the day-one Defender false positive.
Source: "register.cmd"; DestDir: "{app}"
; ROMs staged by the build (release policy: bundles carry them, the git
; repository never does -- see roms/README.md for the orphan-work status).
Source: "{#StageDir}\x86\sc01.bin"; DestDir: "{app}\x86"; Flags: skipifsourcedoesntexist
Source: "{#StageDir}\x86\sc01a.bin"; DestDir: "{app}\x86"; Flags: skipifsourcedoesntexist
Source: "{#StageDir}\x64\sc01.bin"; DestDir: "{app}\x64"; Flags: skipifsourcedoesntexist; Check: Is64BitInstallMode
Source: "{#StageDir}\x64\sc01a.bin"; DestDir: "{app}\x64"; Flags: skipifsourcedoesntexist; Check: Is64BitInstallMode
; ...and any found beside the installer still ride along, for users adding
; their own dumps to a ROM-less build.
Source: "{src}\sc01.bin"; DestDir: "{app}\x86"; Flags: external skipifsourcedoesntexist
Source: "{src}\sc01.bin"; DestDir: "{app}\x64"; Flags: external skipifsourcedoesntexist; Check: Is64BitInstallMode
Source: "{src}\sc01a.bin"; DestDir: "{app}\x86"; Flags: external skipifsourcedoesntexist
Source: "{src}\sc01a.bin"; DestDir: "{app}\x64"; Flags: external skipifsourcedoesntexist; Check: Is64BitInstallMode

[Tasks]
; The one voice option that is not a per-voice setting: the rate model.
Name: "authenticrate"; Description: "Authentic rate: speech gets higher-pitched as it speeds up, like the real 1980 chip (default keeps the pitch constant). Restart your screen reader after install to apply."; GroupDescription: "Voice options:"; Flags: unchecked

[Registry]
; The engine reads Software\votraxsc01\AuthenticRate at load, so a change
; takes effect the next time the host program (screen reader) starts.  The
; engine checks HKCU first, then HKLM, so a user can override this
; machine-wide default per-user without admin rights.
Root: HKLM; Subkey: "Software\votraxsc01"; Flags: uninsdeletekeyifempty
Root: HKLM; Subkey: "Software\votraxsc01"; ValueType: dword; ValueName: "AuthenticRate"; ValueData: "1"; Tasks: authenticrate; Flags: uninsdeletevalue
Root: HKLM; Subkey: "Software\votraxsc01"; ValueType: dword; ValueName: "AuthenticRate"; ValueData: "0"; Tasks: not authenticrate; Flags: uninsdeletevalue

[Icons]
Name: "{autoprograms}\Re-register Votrax voices"; Filename: "{app}\register.cmd"; WorkingDir: "{app}"

[Run]
; regsvr32 invoked directly, not through a shell script.
Filename: "{sys}\regsvr32.exe"; Parameters: "/s ""{app}\x64\votrax_sapi.dll"""; StatusMsg: "Registering 64-bit voices..."; Check: Is64BitInstallMode
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/s ""{app}\x86\votrax_sapi.dll"""; StatusMsg: "Registering 32-bit voices..."

[UninstallRun]
Filename: "{sys}\regsvr32.exe"; Parameters: "/u /s ""{app}\x64\votrax_sapi.dll"""; RunOnceId: "Unreg64"; Check: Is64BitInstallMode
Filename: "{syswow64}\regsvr32.exe"; Parameters: "/u /s ""{app}\x86\votrax_sapi.dll"""; RunOnceId: "Unreg32"
