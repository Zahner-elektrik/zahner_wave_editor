// Controller script of the standalone Zahner Wave Editor installer. Besides the
// cosmetics on the introduction page it removes a previous installation before a
// new one is written: the Installer Framework refuses to install into a
// directory that still holds a maintenance tool, and an older copy in a
// different directory would otherwise simply stay behind.
//
// The script runs in a QJSEngine that only exposes the objects listed in the
// Installer Framework's "Scripting API" documentation. QDir and QFile are not
// among them, and naming one throws a ReferenceError that aborts the callback it
// was thrown in without any visible error, so directory contents have to be
// probed through installer.fileExists(). systemInfo has no currentLanguage
// property either; the wizard language comes from installer.value("UILanguage").
//
// The generated maintenance tool embeds this script as well, so everything that
// removes an installation must be guarded by installer.isInstaller(). Without
// that guard the maintenance tool would purge the installation it was started
// from.

// Component names this installer has shipped under. Used to recognize one of
// our own installations by the components.xml of the previous install.
var kComponentNames = [
    "de.zahner.waveeditor.standalone",
    "ZahnerWaveEditor.Application"
];

// DisplayName values of our Windows uninstall registry entries, whitespace
// removed and lower-cased. The entry is written from <Name> in config.xml,
// which CPack fills from CPACK_IFW_PACKAGE_NAME.
var kProductKeys = ["zahnerwaveeditor"];

// Executable names of the editor, for the "please close the application" check.
var kProcessNames = [
    "ZahnerWaveEditor.exe",
    "ZahnerWaveEditor",
    "zahnerwaveeditor"
];

// Uninstall registry hives to scan for an installation outside the target
// directory. Installer Framework registers itself under a random GUID, so the
// key cannot be addressed directly and has to be searched for.
var kUninstallKeys = [
    "HKEY_CURRENT_USER\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    "HKEY_LOCAL_MACHINE\\Software\\Microsoft\\Windows\\CurrentVersion\\Uninstall",
    "HKEY_LOCAL_MACHINE\\Software\\WOW6432Node\\Microsoft\\Windows\\CurrentVersion\\Uninstall"
];

// Search terms for that scan. The DisplayName decides in the end, these only
// narrow the registry walk down to a handful of candidate keys.
var kRegistrySearchTerms = ["ZahnerWaveEditor", "Zahner Wave Editor"];

// Polls, a second apart, while waiting for the detached helper that finishes an
// uninstall. The short budget applies when the maintenance tool already reported
// a failure, so a broken uninstall does not freeze the wizard for a minute.
var kCleanupRetries = 60;
var kFailedCleanupRetries = 5;

function Controller()
{
    // Page callbacks never run for a command line installation, so the only
    // chance to clean up during an unattended install is right here. Note that
    // --root has not been applied to TargetDir yet at this point: for a command
    // line install into a non-default directory only the registry lookup below
    // finds the previous installation.
    if (installer.isInstaller() && installer.isCommandLineInstance()) {
        prepareTargetDirectory(installer.value("TargetDir"), false);
    }
}

function controllerTr(en, de)
{
    var language = installer.value("UILanguage");
    return language && language.toLowerCase().indexOf("de") === 0 ? de : en;
}

Controller.prototype.IntroductionPageCallback = function()
{
    var version = installer.value("ProductVersion");
    if (!version) {
        return;
    }

    var page = gui.currentPageWidget();
    if (!page) {
        return;
    }

    var label = gui.findChild(page, "MessageLabel");
    if (!label) {
        return;
    }

    var versionLine = "<b>Version " + version + "</b>";
    if (label.text.indexOf(versionLine) === -1) {
        var current = label.text;
        label.setText(versionLine + (current ? "<br><br>" + current : ""));
    }
}

Controller.prototype.TargetDirectoryPageCallback = function()
{
    // Runs when the page is entered, which is before the Installer Framework
    // validates the directory. Anything left over from a previous installation
    // has to be gone by the time the user presses Next, or that validation
    // rejects the directory with "already contains an installation".
    prepareTargetDirectory(installer.value("TargetDir"), true);
}

function prepareTargetDirectory(targetDir, interactive)
{
    if (!installer.isInstaller()) {
        return;
    }

    var installations = previousInstallations(targetDir);
    if (installations.length > 0 && confirmRemoval(installations, interactive)
            && ensureAppClosed(interactive)) {
        for (var i = 0; i < installations.length; ++i) {
            removeInstallation(installations[i], interactive);
        }
    }

    // Debris of an uninstall that never finished. Skipped while the directory
    // still holds a working installation, which is the case when the user
    // declined the removal above - that answer must not lead to a wipe.
    if (targetDir && !isOwnInstallation(targetDir)) {
        removeLeftovers(targetDir, interactive);
    }
}

// ---- platform helpers ----

function isWindows()
{
    return systemInfo.kernelType === "winnt";
}

function isMacOs()
{
    return systemInfo.kernelType === "darwin";
}

function toSlashes(path)
{
    return path.replace(/\\/g, "/");
}

function normalizePath(path)
{
    var normalized = toSlashes(path);
    while (normalized.length > 1 && normalized.charAt(normalized.length - 1) === "/") {
        normalized = normalized.substring(0, normalized.length - 1);
    }
    return normalized;
}

// Key for comparing two paths for equality. Windows and macOS install onto
// case insensitive file systems, so "C:/Program Files" and "c:/program files"
// name the same directory there.
function pathKey(path)
{
    var normalized = normalizePath(path);
    return isWindows() || isMacOs() ? normalized.toLowerCase() : normalized;
}

function firstExistingPath(paths)
{
    for (var i = 0; i < paths.length; ++i) {
        if (installer.fileExists(paths[i])) {
            return paths[i];
        }
    }
    return "";
}

// Elevation is only available from the wizard. A command line instance aborts
// the whole run with "Cannot elevate access rights while running from command
// line", so an unattended install has to be started with the rights it needs.
// Returns whether rights were gained and have to be dropped again afterwards.
function tryGainAdminRights()
{
    if (installer.isCommandLineInstance() || installer.hasAdminRights()) {
        return false;
    }

    try {
        return installer.gainAdminRights();
    } catch (error) {
        console.log("[controlscript] Could not elevate: " + error);
        return false;
    }
}

function sleepASecond()
{
    if (isWindows()) {
        installer.execute("cmd", ["/c", "ping", "-n", "2", "127.0.0.1"]);
    } else {
        installer.execute("sleep", ["1"]);
    }
}

// ---- recognizing an existing installation ----

function findMaintenanceTool(targetDir)
{
    var dir = normalizePath(targetDir);
    if (isWindows()) {
        return firstExistingPath([dir + "/maintenancetool.exe", dir + "/MaintenanceTool.exe"]);
    }
    if (isMacOs()) {
        return firstExistingPath([
            dir + "/maintenancetool.app/Contents/MacOS/maintenancetool",
            dir + "/MaintenanceTool.app/Contents/MacOS/MaintenanceTool"
        ]);
    }
    return firstExistingPath([dir + "/maintenancetool", dir + "/MaintenanceTool"]);
}

function hasInstallationArtifacts(targetDir)
{
    return findMaintenanceTool(targetDir) !== ""
        || installer.fileExists(normalizePath(targetDir) + "/components.xml");
}

function hasOwnComponentRegistration(targetDir)
{
    var componentsXml = normalizePath(targetDir) + "/components.xml";
    if (!installer.fileExists(componentsXml)) {
        return false;
    }

    var content = installer.readFile(componentsXml, "UTF-8");
    if (!content) {
        return false;
    }

    for (var i = 0; i < kComponentNames.length; ++i) {
        if (content.indexOf(kComponentNames[i]) !== -1) {
            return true;
        }
    }
    return false;
}

function hasOwnExecutable(targetDir)
{
    var dir = normalizePath(targetDir);
    return firstExistingPath([
        dir + "/bin/ZahnerWaveEditor.exe",
        dir + "/bin/ZahnerWaveEditor",
        dir + "/bin/zahnerwaveeditor",
        dir + "/ZahnerWaveEditor.app/Contents/MacOS/ZahnerWaveEditor"
    ]) !== "";
}

// A maintenance tool alone is not enough to justify uninstalling: the user may
// well have pointed the installer at some unrelated product's directory, and
// purging that would destroy a foreign installation.
function isOwnInstallation(targetDir)
{
    return hasOwnComponentRegistration(targetDir) || hasOwnExecutable(targetDir);
}

// ---- finding installations outside the target directory ----

function isOwnProductName(displayName)
{
    if (!displayName) {
        return false;
    }
    var normalized = displayName.replace(/\s+/g, "").toLowerCase();
    return kProductKeys.indexOf(normalized) !== -1;
}

// "C:\...\maintenancetool.exe" --start-uninstaller -> C:/.../
function installDirFromUninstallString(uninstallString)
{
    if (!uninstallString) {
        return "";
    }

    var quoted = uninstallString.match(/^\s*"([^"]+)"/);
    var toolPath = normalizePath(quoted ? quoted[1] : uninstallString.replace(/\s+-.*$/, ""));
    var separator = toolPath.lastIndexOf("/");
    return separator > 0 ? toolPath.substring(0, separator) : "";
}

function registryCandidateKeys()
{
    var keys = [];
    var seen = {};

    for (var root = 0; root < kUninstallKeys.length; ++root) {
        for (var term = 0; term < kRegistrySearchTerms.length; ++term) {
            var query = installer.execute("reg",
                                          ["query", kUninstallKeys[root], "/s", "/f", kRegistrySearchTerms[term]]);
            if (!query || query[1] !== 0 || !query[0]) {
                continue;
            }

            // Registry paths are case insensitive and reg.exe is free to echo
            // the queried prefix in its own casing, so compare lower-cased.
            var prefix = (kUninstallKeys[root] + "\\").toLowerCase();
            var lines = query[0].split(/\r?\n/);
            for (var i = 0; i < lines.length; ++i) {
                var line = lines[i].replace(/^\s+|\s+$/g, "");
                // Key lines carry the full path; the matched value lines below
                // them are indented and only name the value.
                var lowered = line.toLowerCase();
                if (lowered.indexOf(prefix) !== 0 || seen[lowered]) {
                    continue;
                }
                seen[lowered] = true;
                keys.push(line);
            }
        }
    }
    return keys;
}

function registeredInstallDirs()
{
    if (!isWindows()) {
        return [];
    }

    var dirs = [];
    var keys = registryCandidateKeys();
    for (var i = 0; i < keys.length; ++i) {
        if (!isOwnProductName(installer.value(keys[i] + "\\DisplayName"))) {
            continue;
        }

        var dir = installer.value(keys[i] + "\\InstallLocation");
        if (!dir) {
            dir = installDirFromUninstallString(installer.value(keys[i] + "\\UninstallString"));
        }
        if (dir) {
            dirs.push(normalizePath(dir));
        }
    }
    return dirs;
}

function previousInstallations(targetDir)
{
    var candidates = [];
    if (targetDir) {
        candidates.push(normalizePath(targetDir));
    }
    candidates = candidates.concat(registeredInstallDirs());

    var installations = [];
    var seen = {};
    for (var i = 0; i < candidates.length; ++i) {
        var dir = candidates[i];
        var key = pathKey(dir);
        if (seen[key]) {
            continue;
        }
        seen[key] = true;

        if (findMaintenanceTool(dir) !== "" && isOwnInstallation(dir)) {
            installations.push(dir);
        }
    }
    return installations;
}

// ---- removing them ----

function confirmRemoval(installations, interactive)
{
    if (!interactive) {
        console.log("[controlscript] Removing previous installation(s): " + installations.join(", "));
        return true;
    }

    var answer = QMessageBox.question(
        "waveeditor.removepreviousinstallation",
        controllerTr("Remove previous installation?", "Vorherige Installation entfernen?"),
        controllerTr(
            "An existing Zahner Wave Editor installation was found in:\n\n" + installations.join("\n")
                + "\n\nUninstall it before continuing?",
            "Eine bestehende Installation von Zahner Wave Editor wurde gefunden in:\n\n" + installations.join("\n")
                + "\n\nVor dem Fortfahren deinstallieren?"
        ),
        QMessageBox.Yes | QMessageBox.No
    );
    return answer === QMessageBox.Yes;
}

function isAppRunning()
{
    for (var i = 0; i < kProcessNames.length; ++i) {
        if (installer.isProcessRunning(kProcessNames[i])) {
            return true;
        }
    }
    return false;
}

function ensureAppClosed(interactive)
{
    if (!isAppRunning()) {
        return true;
    }

    if (!interactive) {
        // Uninstalling underneath a running editor leaves locked files behind
        // and a half removed installation, which is worse than not installing.
        console.log("[controlscript] Zahner Wave Editor is still running, aborting.");
        installer.setCanceled();
        return false;
    }

    while (isAppRunning()) {
        var answer = QMessageBox.warning(
            "waveeditor.applicationrunning",
            controllerTr("Zahner Wave Editor is running", "Zahner Wave Editor wird noch ausgeführt"),
            controllerTr(
                "Please close Zahner Wave Editor before continuing.",
                "Bitte schließen Sie Zahner Wave Editor, bevor Sie fortfahren."
            ),
            QMessageBox.Retry | QMessageBox.Cancel
        );
        if (answer === QMessageBox.Cancel) {
            return false;
        }
    }
    return true;
}

function removeInstallation(targetDir, interactive)
{
    var maintenanceTool = findMaintenanceTool(targetDir);
    if (!maintenanceTool) {
        return true;
    }

    installer.setMessageBoxAutomaticAnswer("TargetDirectoryInUse", QMessageBox.Ok);

    var elevated = tryGainAdminRights();
    var result = installer.execute(maintenanceTool, ["purge", "--confirm-command", "--accept-messages"]);
    if (elevated) {
        installer.dropAdminRights();
    }

    var exitCode = result && result.length > 1 ? result[1] : -1;
    console.log("[controlscript] purge " + maintenanceTool + " exit code: " + exitCode);
    if (result && result[0]) {
        console.log("[controlscript] purge output: " + result[0]);
    }

    // The maintenance tool cannot delete itself while it runs, so on every
    // platform it hands the last step to a detached helper and returns early.
    // Wait for that helper, otherwise the target directory validation still
    // sees the old maintenance tool.
    var budget = exitCode === 0 ? kCleanupRetries : kFailedCleanupRetries;
    var retries = 0;
    while (hasInstallationArtifacts(targetDir) && retries < budget) {
        sleepASecond();
        ++retries;
    }

    if (!hasInstallationArtifacts(targetDir)) {
        return true;
    }

    // The uninstall got far enough to break the installation but left files
    // behind. Those would make the target directory validation reject the
    // directory, so offer to drop them.
    if (!isOwnInstallation(targetDir)) {
        removeLeftovers(targetDir, interactive);
        if (!hasInstallationArtifacts(targetDir)) {
            return true;
        }
    }

    console.log("[controlscript] Previous installation was not removed completely: " + targetDir);
    if (interactive) {
        QMessageBox.warning(
            "waveeditor.cleanupincomplete",
            controllerTr("Cleanup warning", "Warnung bei Bereinigung"),
            controllerTr(
                "The previous installation in " + targetDir + " could not be removed completely.\n\n"
                    + "Please uninstall it manually and run this installer again.",
                "Die vorherige Installation in " + targetDir + " konnte nicht vollständig entfernt werden.\n\n"
                    + "Bitte deinstallieren Sie sie manuell und starten Sie diesen Installer erneut."
            )
        );
    }
    return false;
}

// ---- leftovers of a failed uninstall ----

// Only ever wipe a directory that is unmistakably ours.
function isSafeTargetDirectory(targetDir)
{
    if (!targetDir || targetDir.length < 8) {
        return false;
    }

    var normalized = normalizePath(targetDir).toLowerCase();
    if (normalized === "/" || normalized.match(/^[a-z]:$/)) {
        return false;
    }

    var homeDir = installer.value("HomeDir");
    if (homeDir && normalized === normalizePath(homeDir).toLowerCase()) {
        return false;
    }

    return normalized.indexOf("zahnerwaveeditor") !== -1
        || normalized.indexOf("zahner wave editor") !== -1;
}

// Files a broken or interrupted uninstall leaves behind. Installing on top of
// those works, but the stale files are never cleaned up afterwards, so offer to
// drop them. Anything not listed here keeps the directory untouched.
function findLeftovers(targetDir)
{
    var dir = normalizePath(targetDir);
    var found = [];

    // Usually the only leftover: the maintenance tool is deleted last, by the
    // detached helper, and it is also what makes the target directory
    // validation reject the directory.
    var maintenanceTool = findMaintenanceTool(dir);
    if (maintenanceTool) {
        found.push(maintenanceTool.substring(dir.length + 1));
    }

    var relative = [
        "components.xml",
        "installer.dat",
        "maintenancetool.dat",
        "maintenancetool.ini",
        "InstallationLog.txt",
        "network.xml",
        "installerResources",
        "bin/ZahnerWaveEditor.exe",
        "bin/ZahnerWaveEditor",
        "bin/zahnerwaveeditor",
        "ZahnerWaveEditor.app"
    ];

    for (var i = 0; i < relative.length; ++i) {
        if (installer.fileExists(dir + "/" + relative[i])) {
            found.push(relative[i]);
        }
    }
    return found;
}

function removeLeftovers(targetDir, interactive)
{
    if (!isSafeTargetDirectory(targetDir)) {
        return;
    }

    var leftovers = findLeftovers(targetDir);
    if (leftovers.length === 0) {
        return;
    }

    if (interactive) {
        var answer = QMessageBox.question(
            "waveeditor.removeleftovers",
            controllerTr("Replace existing files?", "Bestehende Dateien ersetzen?"),
            controllerTr(
                "The target directory still contains files of an earlier installation that was not "
                    + "uninstalled cleanly:\n\n" + leftovers.join("\n")
                    + "\n\nDelete the directory contents and continue?",
                "Das Zielverzeichnis enthält noch Dateien einer früheren Installation, die nicht "
                    + "vollständig deinstalliert wurde:\n\n" + leftovers.join("\n")
                    + "\n\nInhalt des Verzeichnisses löschen und fortfahren?"
            ),
            QMessageBox.Yes | QMessageBox.No
        );
        if (answer !== QMessageBox.Yes) {
            return;
        }
    } else {
        console.log("[controlscript] Removing leftovers in " + targetDir + ": " + leftovers.join(", "));
    }

    var dir = normalizePath(targetDir);
    var elevated = tryGainAdminRights();
    var result;
    if (isWindows()) {
        result = installer.execute("cmd", ["/c", "rmdir", "/s", "/q", installer.toNativeSeparators(dir)]);
    } else {
        result = installer.execute("rm", ["-rf", dir]);
    }
    if (elevated) {
        installer.dropAdminRights();
    }

    console.log("[controlscript] Leftover cleanup exit code: " + (result ? result[1] : "n/a"));
}
