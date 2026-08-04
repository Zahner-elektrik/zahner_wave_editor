function Component()
{
}

Component.prototype.createOperations = function()
{
    component.createOperations();

    if (systemInfo.kernelType === "winnt") {
        component.addOperation("CreateShortcut",
                               "@TargetDir@/bin/ZahnerWaveEditor.exe",
                               "@StartMenuDir@/Zahner Wave Editor.lnk",
                               "workingDirectory=@TargetDir@/bin",
                               "iconPath=@TargetDir@/bin/ZahnerWaveEditor.exe",
                               "iconId=0",
                               "description=Zahner Wave Editor");
        component.addOperation("RegisterFileType",
                               "zwj",
                               "@TargetDir@/bin/ZahnerWaveEditor.exe \"%1\"",
                               "Zahner Wave Editor waveform document",
                               "application/json",
                               "@TargetDir@/bin/ZahnerWaveEditor.exe,0");
    } else if (systemInfo.kernelType === "darwin") {
        component.addOperation("CreateLink",
                               "@ApplicationsDir@/ZahnerWaveEditor.app",
                               "@TargetDir@/ZahnerWaveEditor.app");
    } else if (systemInfo.kernelType === "linux") {
        component.addOperation("CreateDesktopEntry",
                               "zahnerwaveeditor.desktop",
                               "Version=1.0\n" +
                               "Type=Application\n" +
                               "Terminal=false\n" +
                               "Exec=@TargetDir@/bin/zahnerwaveeditor\n" +
                               "Name=Zahner Wave Editor\n" +
                               "Icon=@TargetDir@/resources/waveeditor.png\n" +
                               "Name[en_US]=Zahner Wave Editor");
    }
}
