; Windows installer for mxCut, built with NSIS.
; makensis -DVERSION=0.1.0 -DDIST=path\to\mxCut -DOUTFILE=setup.exe installer.nsi
;
; mxCut's updater runs it with /UPDATE: that skips straight to installing
; over the current copy, once mxCut has closed.

Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"

!ifndef VERSION
  !define VERSION "0.0.0"
!endif
!ifndef DIST
  !define DIST "..\..\dist\mxCut"
!endif
!ifndef OUTFILE
  !define OUTFILE "mxCut-${VERSION}-setup.exe"
!endif

!define APP "mxCut"
!define EXE "mxcut.exe"
; What the app was called before it was renamed, so an update cleans it up.
!define OLD_EXE "video-trimmer.exe"
!define OLD_KEY "Software\VideoTrimmer"
!define OLD_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\VideoTrimmer"
!define UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\mxCut"

Name "${APP}"
OutFile "${OUTFILE}"
InstallDir "$PROGRAMFILES64\${APP}"
InstallDirRegKey HKLM "Software\mxCut" "InstallDir"
RequestExecutionLevel admin
SetCompressor /SOLID lzma
VIProductVersion "${VERSION}.0"
VIAddVersionKey "ProductName" "${APP}"
VIAddVersionKey "FileDescription" "${APP} installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "LegalCopyright" "mxCut contributors"

!define MUI_ICON "..\icons\mxcut.ico"
!define MUI_UNICON "..\icons\mxcut.ico"
!define MUI_ABORTWARNING
!define MUI_FINISHPAGE_RUN "$INSTDIR\${EXE}"

Var Updating

!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipWhenUpdating
!insertmacro MUI_PAGE_WELCOME
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipWhenUpdating
!insertmacro MUI_PAGE_DIRECTORY
!define MUI_PAGE_CUSTOMFUNCTION_PRE SkipWhenUpdating
!insertmacro MUI_PAGE_COMPONENTS
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH
!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES
!insertmacro MUI_LANGUAGE "English"

; The installer and uninstaller replace the whole install folder, so only
; allow a new or empty folder, or one that already holds this app.
; Find an install made before the rename.
Function .onInit
  StrCpy $Updating 0
  ${GetParameters} $R0
  ClearErrors
  ${GetOptions} $R0 "/UPDATE" $R1
  IfErrors +2
    StrCpy $Updating 1
  ReadRegStr $0 HKLM "Software\mxCut" "InstallDir"
  StrCmp $0 "" 0 +4
    ReadRegStr $0 HKLM "${OLD_KEY}" "InstallDir"
    StrCmp $0 "" +2
      StrCpy $INSTDIR $0
FunctionEnd

Function SkipWhenUpdating
  StrCmp $Updating 1 0 +2
    Abort
FunctionEnd

; mxCut quits as the updater starts this; give it up to 15 seconds to let
; go of its files.
Function WaitForApp
  StrCpy $R2 0
  retry:
    IfFileExists "$INSTDIR\${EXE}" 0 free
    ClearErrors
    FileOpen $R3 "$INSTDIR\${EXE}" a
    IfErrors 0 opened
    IntOp $R2 $R2 + 1
    IntCmp $R2 30 gaveup
    Sleep 500
    Goto retry
  opened:
    FileClose $R3
    Goto free
  gaveup:
    MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "mxCut is still running. Close it, then press Retry." IDRETRY 0 IDCANCEL +3
      StrCpy $R2 0
      Goto retry
    Quit
  free:
FunctionEnd

Function .onVerifyInstDir
  IfFileExists "$INSTDIR\${EXE}" ok
  IfFileExists "$INSTDIR\${OLD_EXE}" ok
  FindFirst $0 $1 "$INSTDIR\*.*"
  loop:
    StrCmp $1 "" done
    StrCmp $1 "." next
    StrCmp $1 ".." next
    FindClose $0
    Abort ; holds other files
  next:
    FindNext $0 $1
    Goto loop
  done:
  FindClose $0
  ok:
FunctionEnd

Section "mxCut (required)" SecApp
  SectionIn RO
  Call WaitForApp
  ; Clear out an older version's files first, so no stale DLLs are left behind.
  IfFileExists "$INSTDIR\${EXE}" clear
  IfFileExists "$INSTDIR\${OLD_EXE}" clear keep
  clear:
    RMDir /r "$INSTDIR"
  keep:
  SetOutPath "$INSTDIR"
  File /r "${DIST}\*.*"
  WriteUninstaller "$INSTDIR\uninstall.exe"

  CreateShortcut "$SMPROGRAMS\${APP}.lnk" "$INSTDIR\${EXE}"
  ; Shortcuts from before the app was renamed.
  Delete "$SMPROGRAMS\Video Trimmer.lnk"
  IfFileExists "$DESKTOP\Video Trimmer.lnk" 0 +3
    Delete "$DESKTOP\Video Trimmer.lnk"
    CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}"
  ; Point an existing desktop shortcut at the renamed program.
  IfFileExists "$DESKTOP\${APP}.lnk" 0 +2
    CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}"

  DeleteRegKey HKLM "${OLD_KEY}"
  DeleteRegKey HKLM "${OLD_UNINST_KEY}"
  WriteRegStr HKLM "Software\mxCut" "InstallDir" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayName" "${APP}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayVersion" "${VERSION}"
  WriteRegStr HKLM "${UNINST_KEY}" "DisplayIcon" "$INSTDIR\${EXE}"
  WriteRegStr HKLM "${UNINST_KEY}" "InstallLocation" "$INSTDIR"
  WriteRegStr HKLM "${UNINST_KEY}" "UninstallString" '"$INSTDIR\uninstall.exe"'
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify" 1
  WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair" 1
SectionEnd

Section "Desktop shortcut" SecDesktop
  ; An update keeps the shortcuts the user already has, or has removed.
  StrCmp $Updating 1 0 +2
    Return
  CreateShortcut "$DESKTOP\${APP}.lnk" "$INSTDIR\${EXE}"
SectionEnd

Section "Uninstall"
  Delete "$SMPROGRAMS\${APP}.lnk"
  Delete "$DESKTOP\${APP}.lnk"
  IfFileExists "$INSTDIR\${EXE}" 0 +2
    RMDir /r "$INSTDIR"
  DeleteRegKey HKLM "${UNINST_KEY}"
  DeleteRegKey HKLM "Software\mxCut"
SectionEnd
