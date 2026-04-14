; Inno Setup 安装脚本 - PDD Device Manager
; 需要先安装 Inno Setup: https://jrsoftware.org/isinfo.php

#define MyAppName "PDD Device Manager"
#define MyAppVersion "1.0.0"
#define MyAppPublisher "PDD"
#define MyAppURL ""
#define MyAppExeName "PDD Device Manager.exe"

[Setup]
AppId={{8F2B3C4D-5E6F-7A8B-9C0D-1E2F3A4B5C6D}
AppName={#MyAppName}
AppVersion={#MyAppVersion}
AppPublisher={#MyAppPublisher}
AppPublisherURL={#MyAppURL}
AppSupportURL={#MyAppURL}
AppUpdatesURL={#MyAppURL}
DefaultDirName={autopf}\{#MyAppName}
DisableProgramGroupPage=yes
LicenseFile=LICENSE.txt
OutputDir=installer_output
OutputBaseFilename=PDD-Device-Manager-Setup
SetupIconFile=icon.ico
Compression=lzma
SolidCompression=yes
WizardStyle=modern
PrivilegesRequired=admin

[Languages]
Name: "chinesesimplified"; MessagesFile: "compiler:Languages\ChineseSimplified.isl"

[Tasks]
Name: "desktopicon"; Description: "{cm:CreateDesktopIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked
Name: "quicklaunchicon"; Description: "{cm:CreateQuickLaunchIcon}"; GroupDescription: "{cm:AdditionalIcons}"; Flags: unchecked; OnlyBelowVersion: 6.1; Check: not IsAdminInstallMode

[Files]
; Python 安装包
Source: "python-3.10.11-amd64.exe"; DestDir: "{tmp}"; Flags: deleteafterinstall
; 应用程序文件
Source: "dist\PDD Device Manager\*"; DestDir: "{app}"; Flags: ignoreversion recursesubdirs createallsubdirs
; 图标文件
Source: "icon.ico"; DestDir: "{app}"; Flags: ignoreversion

[Icons]
Name: "{autoprograms}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"
Name: "{autodesktop}\{#MyAppName}"; Filename: "{app}\{#MyAppExeName}"; Tasks: desktopicon

[Run]
; 安装 Python
Filename: "{tmp}\python-3.10.11-amd64.exe"; Parameters: "/quiet InstallAllUsers=1 PrependPath=1"; StatusMsg: "正在安装 Python 3.10..."; Check: NeedsPython
; 安装依赖
Filename: "{app}\install_dependencies.bat"; StatusMsg: "正在安装依赖..."; Flags: runhidden waituntilterminated
; 启动应用
Filename: "{app}\{#MyAppExeName}"; Description: "{cm:LaunchProgram,{#StringChange(MyAppName, '&', '&&')}}"; Flags: nowait postinstall skipifsilent

[Code]
function NeedsPython(): Boolean;
begin
  Result := not RegKeyExists(HKLM, 'SOFTWARE\Python\PythonCore\3.10');
end;

procedure InitializeWizard();
begin
  WizardForm.WelcomeLabel1.Caption := '欢迎使用 PDD Device Manager 安装向导';
  WizardForm.WelcomeLabel2.Caption := '本向导将指导您完成 PDD Device Manager 的安装。' + #13#10 + #13#10 +
    'PDD Device Manager 是一个 iOS 设备管理工具，用于连接和管理 iPhone/iPad 设备。' + #13#10 + #13#10 +
    '点击"下一步"继续安装。';
end;
