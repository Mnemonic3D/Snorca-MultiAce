; [1] PACK_SOURCE_DIR = compile-time only (e.g. .\build\FOrcaSlicer). [2] INSTALL_DIR_RUNTIME = runtime install dir (default .\ = $EXEDIR).
Unicode true
!include "MUI2.nsh"
!include "FileFunc.nsh"
!include "LogicLib.nsh"

!define PRODUCT_NAME "Snorca MultiAce"
!define PRODUCT_PUBLISHER "Mnemonic3D"
!define PRODUCT_WEB_SITE "https://github.com/Mnemonic3D/Snorca-MultiAce"
!define PRODUCT_UNINST_KEY "Software\Microsoft\Windows\CurrentVersion\Uninstall\${PRODUCT_NAME}"
!define PRODUCT_UNINST_ROOT_KEY "HKLM"
!define PRODUCT_INSTALL_KEY "Software\${PRODUCT_PUBLISHER}\${PRODUCT_NAME}"

!ifndef VERSION
    !searchparse /noerrors /file "version.inc" `set(Snapmaker_VERSION "` VERSION `")`
    !ifndef VERSION
        !error "VERSION is not set. Pass /DVERSION=x.y.z or keep Snapmaker_VERSION in version.inc."
    !endif
!endif
!define PRODUCT_DISPLAY_NAME "${PRODUCT_NAME} V${VERSION}"

!ifndef SOURCE_DIR
    !define SOURCE_DIR ".\build\FOrcaSlicer"
!endif
!define PACK_SOURCE_DIR "${SOURCE_DIR}"

; 64-bit app: use PROGRAMFILES64 so default path is C:\Program Files\FOrcaSlicer, not (x86)
!define INSTALL_DIR_RUNTIME "$PROGRAMFILES64\Snorca MultiAce"
InstallDir "${INSTALL_DIR_RUNTIME}"

!ifndef OUTPUT_FILE
    !define OUTPUT_FILE "Snorca_MultiAce_Windows_Installer_V${VERSION}.exe"
!endif

; License page: show LICENSE.txt from repo root (same dir as this .nsi)
!ifndef LICENSE_FILE
    !define LICENSE_FILE ".\LICENSE.txt"
!endif

RequestExecutionLevel admin

; No /SOLID to avoid "Internal compiler error #12345: error mmapping datablock"
SetCompressor lzma

;SNORCA merge v2.4.1: numeric file version follows version.inc (Snapmaker_VERSION), like the app; build part via /DVERSION_BUILD=n
!ifndef VERSION_BUILD
    !define VERSION_BUILD "0"
!endif
VIProductVersion "${VERSION}.${VERSION_BUILD}"
VIAddVersionKey "ProductName" "${PRODUCT_NAME}"
VIAddVersionKey "Comments" "Snorca MultiAce is an open source slicer for the Snapmaker U1 with multiACE, based on FOrcaSlicer"
VIAddVersionKey "CompanyName" "${PRODUCT_PUBLISHER}"
VIAddVersionKey "LegalCopyright" "Copyright (C) ${PRODUCT_PUBLISHER}"
VIAddVersionKey "FileDescription" "${PRODUCT_DISPLAY_NAME} Installer"
VIAddVersionKey "FileVersion" "${VERSION}"
VIAddVersionKey "ProductVersion" "${VERSION}"
VIAddVersionKey "InternalName" "${PRODUCT_NAME}"
VIAddVersionKey "LegalTrademarks" ""
VIAddVersionKey "OriginalFilename" "${OUTPUT_FILE}"

; Installer and uninstaller icon: set by build_and_pack.bat via /DICON_FILE=path (e.g. FOrcaSlicer.ico or snapmaker.ico)
!ifdef ICON_FILE
    !define MUI_ICON "${ICON_FILE}"
    !define MUI_UNICON "${ICON_FILE}"
!else
    !define MUI_ICON ".\src\FOrcaSlicer.ico"
    !define MUI_UNICON ".\src\FOrcaSlicer.ico"
!endif

!define MUI_WELCOMEPAGE_TITLE "Welcome to ${PRODUCT_DISPLAY_NAME} Setup"
!define MUI_WELCOMEPAGE_TEXT "This wizard will guide you through the installation of ${PRODUCT_DISPLAY_NAME}.$\r$\n$\r$\nClick Next to continue."
!insertmacro MUI_PAGE_WELCOME

!ifdef LICENSE_FILE
    !define MUI_LICENSEPAGE_CHECKBOX
    !insertmacro MUI_PAGE_LICENSE "${LICENSE_FILE}"
!endif

!insertmacro MUI_PAGE_COMPONENTS

!define MUI_DIRECTORYPAGE_TEXT_TOP "Choose the folder in which to install ${PRODUCT_DISPLAY_NAME}."
!insertmacro MUI_PAGE_DIRECTORY

!insertmacro MUI_PAGE_INSTFILES

!define MUI_FINISHPAGE_RUN
!define MUI_FINISHPAGE_RUN_TEXT "Run ${PRODUCT_NAME}"
!define MUI_FINISHPAGE_RUN_FUNCTION "LaunchApp"
!define MUI_FINISHPAGE_LINK "Visit ${PRODUCT_NAME} website"
!define MUI_FINISHPAGE_LINK_LOCATION "${PRODUCT_WEB_SITE}"
!insertmacro MUI_PAGE_FINISH

!insertmacro MUI_UNPAGE_INSTFILES

!insertmacro MUI_LANGUAGE "SimpChinese"
!insertmacro MUI_LANGUAGE "English"

LangString UNINSTALL_CONFIRM_TEXT ${LANG_SIMPCHINESE} "确定要彻底卸载 ${PRODUCT_DISPLAY_NAME} 及其全部组件吗？"
LangString UNINSTALL_CONFIRM_TEXT ${LANG_ENGLISH} "Are you sure you want to completely uninstall ${PRODUCT_DISPLAY_NAME} and all of its components?"
LangString UNINSTALLER_MISSING_TEXT ${LANG_SIMPCHINESE} "检测到本机已安装 ${PRODUCT_NAME}，但找不到卸载程序。$\r$\n$\r$\n卸载路径：$R8$\r$\n安装目录：$INSTDIR$\r$\n$\r$\n请先手动删除旧安装目录后再运行本安装程序。"
LangString UNINSTALLER_MISSING_TEXT ${LANG_ENGLISH} "An existing ${PRODUCT_NAME} installation was found, but the uninstaller is missing.$\r$\n$\r$\nUninstaller: $R8$\r$\nInstall dir: $INSTDIR$\r$\n$\r$\nRemove the old folder manually, then run this installer again."

Name "${PRODUCT_DISPLAY_NAME}"
OutFile "${OUTPUT_FILE}"

Section "Main program" SecMain
    SectionIn RO

    Call EnsureSnapmakerNotRunning

    SetOutPath "$INSTDIR"
    
    DetailPrint "Installing ${PRODUCT_NAME}..."
    DetailPrint "Target dir: $INSTDIR"
    
    DetailPrint "Copying files..."

    ; PACK_SOURCE_DIR = compile time only. At runtime this File extracts from embedded payload to $INSTDIR.
    File /r /x "*.pdb" /x "*.ilk" /x "*.exp" /x "*.lib" /x "*.obj" /x "*.idb" /x "*.tlog" /x "*.h" /x "*.hpp" /x "*.c" /x "*.cpp" /x "*.cxx" /x "*.cc" /x "*.vcxproj" /x "*.vcxproj.filters" /x "*.sln" /x "*.cmake" /x "*.py" /x "*.md" /x "*.vcxproj.user" /x "CMakeFiles" /x "RelWithDebInfo" /x "Debug" /x "MinSizeRel" /x ".vs" /x "vcpkg_installed" /x "*.dir" /x "include\*" /x "lib\*" "${PACK_SOURCE_DIR}\*.*"

    IfFileExists "$INSTDIR\FOrcaSlicer.exe" 0 extract_error
    
    DetailPrint "Creating uninstaller..."
    WriteUninstaller "$INSTDIR\Uninstall.exe"
    
    DetailPrint "Writing registry..."
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayName" "${PRODUCT_NAME}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "UninstallString" "$INSTDIR\Uninstall.exe"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "InstallLocation" "$INSTDIR"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayIcon" "$INSTDIR\FOrcaSlicer.exe"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "Publisher" "${PRODUCT_PUBLISHER}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "DisplayVersion" "${VERSION}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "URLInfoAbout" "${PRODUCT_WEB_SITE}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "HelpLink" "${PRODUCT_WEB_SITE}"
    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "NoModify" 1
    WriteRegDWORD ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}" "NoRepair" 1

    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_INSTALL_KEY}" "Version" "${VERSION}"
    WriteRegStr ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_INSTALL_KEY}" "InstallPath" "$INSTDIR"

    ; URL protocols (same as macOS CFBundleURLSchemes): FOrcaSlicer:// and FOrcaSlicer://
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): DetailPrint "Registering URL protocols (FOrcaSlicer, FOrcaSlicer)..."
    SetRegView 64
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): WriteRegStr HKLM "Software\Classes\FOrcaSlicer" "" "URL:Snapmaker Orca"
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): WriteRegStr HKLM "Software\Classes\FOrcaSlicer" "URL Protocol" ""
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): WriteRegStr HKLM "Software\Classes\FOrcaSlicer\shell\open\command" "" '"$INSTDIR\FOrcaSlicer.exe" "%1"'
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): WriteRegStr HKLM "Software\Classes\FOrcaSlicer" "" "URL:Snapmaker Orca"
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): WriteRegStr HKLM "Software\Classes\FOrcaSlicer" "URL Protocol" ""
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): WriteRegStr HKLM "Software\Classes\FOrcaSlicer\shell\open\command" "" '"$INSTDIR\FOrcaSlicer.exe" "%1"'
    SetRegView 32

    DetailPrint "Installation complete!"
    Goto end_section
    
    extract_error:
        MessageBox MB_OK|MB_ICONSTOP "Installation failed: FOrcaSlicer.exe was not found in the package. The installer may be corrupted."
        Abort
    
    end_section:
SectionEnd

Section "Desktop shortcut" SecDesktop
    DetailPrint "Creating desktop shortcut..."
    SetShellVarContext current
    CreateShortcut "$DESKTOP\${PRODUCT_NAME}.lnk" "$INSTDIR\FOrcaSlicer.exe" "" "$INSTDIR\FOrcaSlicer.exe" 0
    SetShellVarContext all
SectionEnd

Section "Start menu shortcut" SecStartMenu
    DetailPrint "Creating start menu shortcut..."
    CreateDirectory "$SMPROGRAMS\${PRODUCT_NAME}"
    CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\${PRODUCT_NAME}.lnk" "$INSTDIR\FOrcaSlicer.exe" "" "$INSTDIR\FOrcaSlicer.exe" 0
    CreateShortcut "$SMPROGRAMS\${PRODUCT_NAME}\Uninstall.lnk" "$INSTDIR\Uninstall.exe" "" "$INSTDIR\Uninstall.exe" 0
SectionEnd

!insertmacro MUI_FUNCTION_DESCRIPTION_BEGIN
    !insertmacro MUI_DESCRIPTION_TEXT ${SecMain} "Install ${PRODUCT_DISPLAY_NAME} and all required files."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecDesktop} "Create a desktop shortcut for ${PRODUCT_DISPLAY_NAME}."
    !insertmacro MUI_DESCRIPTION_TEXT ${SecStartMenu} "Create a start menu shortcut for ${PRODUCT_DISPLAY_NAME}."
!insertmacro MUI_FUNCTION_DESCRIPTION_END

Section "Uninstall"
    IfSilent un_confirm_done
    MessageBox MB_ICONQUESTION|MB_YESNO|MB_DEFBUTTON2 "$(UNINSTALL_CONFIRM_TEXT)" IDYES un_confirm_done
    Abort
    un_confirm_done:

    DetailPrint "Uninstalling ${PRODUCT_NAME}..."
    
    DetailPrint "Checking for running processes..."
    nsExec::ExecToLog 'taskkill /F /IM FOrcaSlicer.exe /T'
    Pop $0
    Sleep 500
    
    DetailPrint "Removing desktop shortcut..."
    SetShellVarContext current
    Delete "$DESKTOP\FOrcaSlicer.lnk"
    Delete "$DESKTOP\${PRODUCT_NAME}.lnk"
    SetShellVarContext all
    
    DetailPrint "Removing start menu shortcut..."
    RMDir /r "$SMPROGRAMS\${PRODUCT_NAME}"
    
    DetailPrint "Removing install directory..."
    RMDir /r /REBOOTOK "$INSTDIR"
    
    RMDir "$INSTDIR"
    
    DetailPrint "Removing registry entries..."
    SetRegView 64
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): DeleteRegKey HKLM "Software\Classes\FOrcaSlicer"
    ; Snorca: not registered (would clash with an upstream FOrcaSlicer install): DeleteRegKey HKLM "Software\Classes\FOrcaSlicer"
    SetRegView 32
    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_UNINST_KEY}"
    DeleteRegKey ${PRODUCT_UNINST_ROOT_KEY} "${PRODUCT_INSTALL_KEY}"
    DeleteRegKey HKCU "${PRODUCT_INSTALL_KEY}"
    
    DetailPrint "Uninstall complete!"
SectionEnd

Function LaunchApp
    ExecShell "open" "$INSTDIR\FOrcaSlicer.exe"
FunctionEnd

; Prevent overwriting locked DLLs when FOrcaSlicer (or legacy FOrcaSlicer.exe) is still running.
Function EnsureSnapmakerNotRunning
    snapmaker_check_loop:
        nsExec::Exec 'cmd.exe /c tasklist /FI "IMAGENAME eq FOrcaSlicer.exe" 2>nul | find /i "FOrcaSlicer.exe" >nul'
        Pop $0
        IntCmp $0 0 snapmaker_in_use snapmaker_try_legacy snapmaker_try_legacy
    snapmaker_try_legacy:
        nsExec::Exec 'cmd.exe /c tasklist /FI "IMAGENAME eq FOrcaSlicer.exe" 2>nul | find /i "FOrcaSlicer.exe" >nul'
        Pop $0
        IntCmp $0 0 snapmaker_in_use snapmaker_idle snapmaker_idle
    snapmaker_in_use:
        IfSilent snapmaker_silent snapmaker_prompt
    snapmaker_silent:
        SetErrorLevel 7
        Quit
    snapmaker_prompt:
        MessageBox MB_RETRYCANCEL|MB_ICONEXCLAMATION "A slicer using FOrcaSlicer.exe (Snorca MultiAce or FOrcaSlicer) is still running.$\r$\nClose the program, then click Retry, or Cancel to exit the installer." IDRETRY snapmaker_check_loop
        Abort
    snapmaker_idle:
FunctionEnd

; Sets $R9 to a usable Uninstall.exe path. Aborts if an old install exists without an uninstaller.
Function FindExistingUninstaller
    StrCpy $R9 ""
    ; Snorca: only our own uninstall key; a Snapmaker Orca install is a different product and is left alone.
    ReadRegStr $R8 HKLM "${PRODUCT_UNINST_KEY}" "UninstallString"
    StrCmp $R8 "" 0 find_uninst_have_str
    ReadRegStr $R8 HKCU "${PRODUCT_UNINST_KEY}" "UninstallString"
    StrCmp $R8 "" 0 find_uninst_have_str
    IfFileExists "$INSTDIR\Uninstall.exe" 0 find_uninst_check_app
    StrCpy $R8 "$INSTDIR\Uninstall.exe"
    Goto find_uninst_have_str
    find_uninst_check_app:
        IfFileExists "$INSTDIR\FOrcaSlicer.exe" find_uninst_missing find_uninst_done
    find_uninst_have_str:
        StrCpy $0 $R8 1 0
        StrCmp $0 "$\"" 0 find_uninst_check_last
        StrCpy $R8 $R8 "" 1
    find_uninst_check_last:
        StrCpy $0 $R8 1 -1
        StrCmp $0 "$\"" 0 find_uninst_exists
        StrCpy $R8 $R8 -1
    find_uninst_exists:
        IfFileExists "$R8" 0 find_uninst_missing
        StrCpy $R9 $R8
        Goto find_uninst_done
    find_uninst_missing:
        MessageBox MB_OK|MB_ICONSTOP "$(UNINSTALLER_MISSING_TEXT)"
        Abort
    find_uninst_done:
FunctionEnd

; Sets $R7 to the already-installed version from ARP DisplayVersion / Version.
Function ReadInstalledVersion
    StrCpy $R7 ""
    SetRegView 64
    Call ReadInstalledVersionFromView
    StrCmp $R7 "" 0 read_ver_strip
    SetRegView 32
    Call ReadInstalledVersionFromView
    read_ver_strip:
        SetRegView 32
        StrCmp $R7 "" read_ver_done
        StrCpy $0 $R7 1
        StrCmp $0 "V" read_ver_strip_one
        StrCmp $0 "v" read_ver_strip_one read_ver_done
    read_ver_strip_one:
        StrCpy $R7 $R7 "" 1
    read_ver_done:
FunctionEnd

Function ReadInstalledVersionFromView
    ReadRegStr $R7 HKLM "${PRODUCT_UNINST_KEY}" "DisplayVersion"
    StrCmp $R7 "" 0 read_ver_view_done
    ReadRegStr $R7 HKCU "${PRODUCT_UNINST_KEY}" "DisplayVersion"
    StrCmp $R7 "" 0 read_ver_view_done
    ReadRegStr $R7 HKLM "${PRODUCT_INSTALL_KEY}" "Version"
    StrCmp $R7 "" 0 read_ver_view_done
    ReadRegStr $R7 HKCU "${PRODUCT_INSTALL_KEY}" "Version"
    read_ver_view_done:
FunctionEnd

Function .onInit

    Call EnsureSnapmakerNotRunning
    Call FindExistingUninstaller
    StrCmp $R9 "" done
    StrCpy $R0 $R9
    Call ReadInstalledVersion

    StrCmp $R7 "" already_no_ver already_with_ver
    already_with_ver:
        MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
        "${PRODUCT_NAME} V$R7 is already installed.$\n$\nClick OK to uninstall the old version, or Cancel to abort." \
        /SD IDOK IDOK uninst
        Abort
    already_no_ver:
        MessageBox MB_OKCANCEL|MB_ICONEXCLAMATION \
        "${PRODUCT_NAME} is already installed.$\n$\nClick OK to uninstall the old version, or Cancel to abort." \
        /SD IDOK IDOK uninst
        Abort

    uninst:
        ClearErrors
        StrLen $R1 "Uninstall.exe"
        StrCpy $R2 $R0 -$R1
    uninst_trim_slash:
        StrCpy $R1 $R2 1 -1
        StrCmp $R1 "\" uninst_do_trim uninst_slash_done
    uninst_do_trim:
        StrCpy $R2 $R2 -1
        Goto uninst_trim_slash
    uninst_slash_done:
        IfFileExists "$R0" 0 no_remove_uninstaller
        ; /S skips the new uninstall confirm dialog during upgrade.
        ExecWait '"$R0" /S _?=$R2' $R3
        IfErrors no_remove_uninstaller
        IntCmp $R3 0 done no_remove_uninstaller no_remove_uninstaller

    no_remove_uninstaller:
        MessageBox MB_OK|MB_ICONSTOP "Uninstall failed.$\nUninstaller path: $R0$\nWorking dir: $R2"
        Abort

    done:
FunctionEnd
