; SENTINEL Installer Script
; NSIS 3.x

Unicode True

!define APP_NAME        "SENTINEL"
!define APP_VERSION     "1.0.0"
!define APP_PUBLISHER   "Odin Loch"
!define APP_URL         "https://github.com/odin-loki/SENTINEL"
!define APP_EXE         "sentinel.exe"
!define INSTALL_DIR     "$PROGRAMFILES64\SENTINEL"
!define UNINST_KEY      "Software\Microsoft\Windows\CurrentVersion\Uninstall\SENTINEL"

;-------------------------------------------------------------------
; General
;-------------------------------------------------------------------
Name            "${APP_NAME} ${APP_VERSION}"
OutFile         "..\SENTINEL-${APP_VERSION}-Windows-x64.exe"
InstallDir      "${INSTALL_DIR}"
InstallDirRegKey HKLM "${UNINST_KEY}" "InstallLocation"
RequestExecutionLevel admin
SetCompressor    /SOLID lzma
SetCompress      auto

;-------------------------------------------------------------------
; MUI
;-------------------------------------------------------------------
!include "MUI2.nsh"

!define MUI_ABORTWARNING

!define MUI_WELCOMEFINISHPAGE_BITMAP_NOSTRETCH
!define MUI_WELCOMEPAGE_TITLE    "Welcome to SENTINEL ${APP_VERSION}"
!define MUI_WELCOMEPAGE_TEXT     "SENTINEL is a C++23/Qt6 crime analytics and predictive threat assessment system.$\r$\n$\r$\nThis installer will guide you through the installation.$\r$\n$\r$\nClick Next to continue."
!define MUI_FINISHPAGE_RUN       "$INSTDIR\${APP_EXE}"
!define MUI_FINISHPAGE_RUN_TEXT  "Launch SENTINEL"

!insertmacro MUI_PAGE_WELCOME
!insertmacro MUI_PAGE_DIRECTORY
!insertmacro MUI_PAGE_INSTFILES
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_CONFIRM
!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "English"

;-------------------------------------------------------------------
; Installer Sections
;-------------------------------------------------------------------
Section "SENTINEL (required)" SecMain
    SectionIn RO
    SetOutPath "$INSTDIR"

    ; Main executable
    File "deploy\sentinel.exe"

    ; Qt DLLs
    File "deploy\Qt6Charts.dll"
    File "deploy\Qt6Core.dll"
    File "deploy\Qt6Gui.dll"
    File "deploy\Qt6Network.dll"
    File "deploy\Qt6OpenGL.dll"
    File "deploy\Qt6OpenGLWidgets.dll"
    File "deploy\Qt6Sql.dll"
    File "deploy\Qt6Svg.dll"
    File "deploy\Qt6Widgets.dll"

    ; MinGW runtime
    File "deploy\libgcc_s_seh-1.dll"
    File "deploy\libstdc++-6.dll"
    File "deploy\libwinpthread-1.dll"

    ; Qt plugin directories
    SetOutPath "$INSTDIR\platforms"
    File /r "deploy\platforms\*"

    SetOutPath "$INSTDIR\imageformats"
    File /r "deploy\imageformats\*"

    SetOutPath "$INSTDIR\iconengines"
    File /r "deploy\iconengines\*"

    SetOutPath "$INSTDIR\sqldrivers"
    File /r "deploy\sqldrivers\*"

    SetOutPath "$INSTDIR\styles"
    File /r "deploy\styles\*"

    SetOutPath "$INSTDIR\tls"
    File /r "deploy\tls\*"

    SetOutPath "$INSTDIR\networkinformation"
    File /r "deploy\networkinformation\*"

    SetOutPath "$INSTDIR\generic"
    File /r "deploy\generic\*"

    ; Write registry keys for Add/Remove Programs
    SetOutPath "$INSTDIR"
    WriteRegStr   HKLM "${UNINST_KEY}" "DisplayName"      "${APP_NAME} ${APP_VERSION}"
    WriteRegStr   HKLM "${UNINST_KEY}" "DisplayVersion"   "${APP_VERSION}"
    WriteRegStr   HKLM "${UNINST_KEY}" "Publisher"        "${APP_PUBLISHER}"
    WriteRegStr   HKLM "${UNINST_KEY}" "URLInfoAbout"     "${APP_URL}"
    WriteRegStr   HKLM "${UNINST_KEY}" "InstallLocation"  "$INSTDIR"
    WriteRegStr   HKLM "${UNINST_KEY}" "UninstallString"  '"$INSTDIR\Uninstall.exe"'
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoModify"         1
    WriteRegDWORD HKLM "${UNINST_KEY}" "NoRepair"         1

    ; Write uninstaller
    WriteUninstaller "$INSTDIR\Uninstall.exe"

    ; Start Menu shortcut
    CreateDirectory "$SMPROGRAMS\SENTINEL"
    CreateShortcut  "$SMPROGRAMS\SENTINEL\SENTINEL.lnk"   "$INSTDIR\${APP_EXE}"
    CreateShortcut  "$SMPROGRAMS\SENTINEL\Uninstall.lnk"  "$INSTDIR\Uninstall.exe"

    ; Desktop shortcut
    CreateShortcut "$DESKTOP\SENTINEL.lnk" "$INSTDIR\${APP_EXE}"

SectionEnd

;-------------------------------------------------------------------
; Uninstaller
;-------------------------------------------------------------------
Section "Uninstall"
    Delete "$INSTDIR\sentinel.exe"
    Delete "$INSTDIR\Qt6*.dll"
    Delete "$INSTDIR\lib*.dll"
    Delete "$INSTDIR\Uninstall.exe"

    RMDir /r "$INSTDIR\platforms"
    RMDir /r "$INSTDIR\imageformats"
    RMDir /r "$INSTDIR\iconengines"
    RMDir /r "$INSTDIR\sqldrivers"
    RMDir /r "$INSTDIR\styles"
    RMDir /r "$INSTDIR\tls"
    RMDir /r "$INSTDIR\networkinformation"
    RMDir /r "$INSTDIR\generic"
    RMDir    "$INSTDIR"

    Delete "$SMPROGRAMS\SENTINEL\SENTINEL.lnk"
    Delete "$SMPROGRAMS\SENTINEL\Uninstall.lnk"
    RMDir  "$SMPROGRAMS\SENTINEL"
    Delete "$DESKTOP\SENTINEL.lnk"

    DeleteRegKey HKLM "${UNINST_KEY}"
SectionEnd
