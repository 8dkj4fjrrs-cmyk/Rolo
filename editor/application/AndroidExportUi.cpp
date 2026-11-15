// Exporting for Android from the editor's window (ADR 0028, step 2):
// the dialog that asks for the app's details, the one for where Google's
// tools are, the one that makes an upload key, and the progress of the
// export itself, which is built on a worker thread (AndroidExportJob).
//
// What is kept where:
//   the project (project.rolo, "android")   package name, version, orientation, icon
//   the editor's preferences                where the SDK, Java and the key file are
//   nowhere                                 the key's password: typed at every export

#include "EditorApp.hpp"

#include "../model/Commands.hpp"
#include "../panels/Panels.hpp"

#include "rolo/ui/widgets/Dialog.hpp"

#include <algorithm>
#include <charconv>

namespace rolo::editor {

namespace {

std::string joined(const std::vector<std::string>& lines)
{
    std::string text;
    for (const std::string& line : lines) {
        text += (text.empty() ? "" : "\n\n") + line;
    }
    return text;
}

// Where key files are suggested: a folder of their own beside the projects
// (not inside a project, which may be shared or published).
std::filesystem::path suggestedKeyFile()
{
    const std::filesystem::path projects = defaultProjectsDirectory();
    return (projects.empty() ? std::filesystem::path("Rolo Keys") : projects.parent_path() / "Rolo Keys") / "upload-key.jks";
}

} // namespace

void EditorApp::showAndroidToolsDialog(std::function<void()> then)
{
    auto dialog = std::make_unique<ui::Dialog>("Google's Tools for Android", 560.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>("Android apps are built by Google's tools, which come with Android Studio (free, from "
                            "developer.android.com/studio). Install it, open it once and let its setup finish. Rolo "
                            "then finds the tools by itself; choose folders here only if it does not.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    const auto folderRow = [this, &body](const std::string& label, const std::string& placeholder, const char* key,
                                         const std::string& pickerTitle) -> ui::TextField& {
        body.emplace<ui::Label>(label, ui::Label::Style::Dim);
        auto& row = body.emplace<ui::Row>(6.0f);
        auto& field = row.emplace<ui::TextField>(placeholder);
        field.setStretch(1.0f);
        field.setText(m_preferences.getString(key, ""));
        row.emplace<ui::Button>("Browse…", [this, &field, pickerTitle] {
            showFolderPicker(*m_ui, pickerTitle, field.text(),
                             [&field](const std::filesystem::path& chosen) { field.setText(chosen.string()); });
        });
        return field;
    };
    auto& sdk = folderRow("Android SDK folder", "Found by itself", kAndroidSdkKey, "The Android SDK's Folder");
    auto& java = folderRow("Java folder (\"jbr\" inside Android Studio)", "Found by itself", kAndroidJavaKey, "Java's Folder");
    auto& status = body.emplace<ui::Label>("");
    status.setWrap(true);
    const auto check = [this, &sdk, &java, &status] {
        const AndroidSetup setup = findAndCheckAndroidTools(sdk.text(), java.text(), m_resources);
        if (setup.report.ok()) {
            status.setColor(std::nullopt);
            status.setText("Found: the Android SDK in " + setup.tools.sdk.string() + ", and Java in "
                           + setup.tools.javaHome.string() + "."
                           + (setup.report.notes.empty() ? std::string() : "\n\n" + joined(setup.report.notes)));
        } else {
            status.setColor(m_ui->theme().error);
            status.setText(joined(setup.report.problems));
        }
        return setup.report.ok();
    };
    check();
    dialog->addButton("Cancel");
    ui::Button& again = dialog->addButton("Check Again");
    ui::Button& use = dialog->addButton("Use These", ui::Button::Style::Primary);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    again.setOnClick([check] { check(); });
    use.setOnClick([this, &d, &sdk, &java, check, then] {
        if (!check()) {
            return;   // the problems are shown; the dialog stays
        }
        m_preferences.setString(kAndroidSdkKey, sdk.text());
        m_preferences.setString(kAndroidJavaKey, java.text());
        saveSettings();
        m_ui->closeOverlay(&d);
        if (then) {
            m_ui->post(then);
        }
    });
}

void EditorApp::showNewAndroidKeyDialog(std::function<void(const AndroidSigning&)> made)
{
    auto dialog = std::make_unique<ui::Dialog>("New Upload Key", 540.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>("Google Play only accepts updates of a game that are signed with the same key as the "
                            "first upload: the key proves they come from you. Make one key and use it for all your "
                            "games.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    body.emplace<ui::Label>("Key file", ui::Label::Style::Dim);
    auto& fileRow = body.emplace<ui::Row>(6.0f);
    auto& file = fileRow.emplace<ui::TextField>("");
    file.setStretch(1.0f);
    file.setText(suggestedKeyFile().string());
    fileRow.emplace<ui::Button>("Browse…", [this, &file] {
        showFolderPicker(*m_ui, "Keep the Key In", std::filesystem::path(file.text()).parent_path(),
                         [&file](const std::filesystem::path& chosen) {
                             file.setText((chosen / std::filesystem::path(file.text()).filename()).string());
                         });
    });
    body.emplace<ui::Label>("Your name, or your studio's (kept inside the key)", ui::Label::Style::Dim);
    auto& owner = body.emplace<ui::TextField>("");
    body.emplace<ui::Label>("Password (6 characters or more)", ui::Label::Style::Dim);
    auto& password = body.emplace<ui::TextField>("");
    password.setSecret(true);
    body.emplace<ui::Label>("The same password again", ui::Label::Style::Dim);
    auto& again = body.emplace<ui::TextField>("");
    again.setSecret(true);
    body.emplace<ui::Label>("Keep the key file and the password safe, and keep a copy of both somewhere else (not "
                            "only on this computer). Rolo does not store the password. If either is lost, a game "
                            "on Google Play can only be updated after asking Google for a new key. Never put the "
                            "key file in a project you share.",
                            ui::Label::Style::Faint)
        .setWrap(true);
    auto& error = body.emplace<ui::Label>("");
    error.setColor(m_ui->theme().error);
    error.setWrap(true);
    dialog->addButton("Cancel");
    ui::Button& ok = dialog->addButton("Make Key", ui::Button::Style::Primary);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    ok.setOnClick([this, &d, &file, &owner, &password, &again, &error, made] {
        if (password.text() != again.text()) {
            error.setText("The two passwords are not the same. Type them again.");
            return;
        }
        AndroidSigning key;
        key.keystore = file.text();
        key.password = password.text();
        const AndroidTools tools = findAndroidTools(m_preferences.getString(kAndroidSdkKey, ""),
                                                    m_preferences.getString(kAndroidJavaKey, ""), currentOs(), realEnvironment);
        const Status status = createAndroidKey(tools.javaHome, currentOs(), key, owner.text());
        if (!status) {
            error.setText(status.message);
            return;
        }
        log().info("export", "Made the upload key " + key.keystore.string() + ". Keep it and its password safe, with a "
                             "copy somewhere else.");
        m_ui->closeOverlay(&d);
        if (made) {
            m_ui->post([made, key] { made(key); });
        }
    });
}

void EditorApp::showAndroidExportDialog(const std::string& gameName, const std::filesystem::path& into)
{
    if (!m_project) {
        return;
    }
    const AndroidSetup setup = findAndCheckAndroidTools(m_preferences.getString(kAndroidSdkKey, ""),
                                                        m_preferences.getString(kAndroidJavaKey, ""), m_resources);
    if (!setup.report.ok()) {
        // Nothing can be exported yet: say what is missing, and come back here afterwards.
        showAndroidToolsDialog([this, gameName, into] { showAndroidExportDialog(gameName, into); });
        return;
    }
    const ProjectSettings project = m_project->settings();
    const AndroidSettings saved = project.android;

    auto dialog = std::make_unique<ui::Dialog>("Export for Android", 560.0f);
    ui::Column& body = dialog->body();

    body.emplace<ui::Label>("Package name", ui::Label::Style::Dim);
    auto& package = body.emplace<ui::TextField>("com.yourname.yourgame");
    package.setText(saved.package.empty() ? suggestedAndroidPackage(gameName) : saved.package);
    body.emplace<ui::Label>("The app's identity on phones and in Google Play: yours (a name or web address you own, "
                            "backwards), then the game's. Small letters, digits and dots. Once a game is published "
                            "it can never change.",
                            ui::Label::Style::Faint)
        .setWrap(true);

    auto& versions = body.emplace<ui::Row>(8.0f);
    auto& nameColumn = versions.emplace<ui::Column>(4.0f);
    nameColumn.setStretch(1.0f);
    nameColumn.emplace<ui::Label>("Version (what players see)", ui::Label::Style::Dim);
    auto& versionName = nameColumn.emplace<ui::TextField>("1.0");
    versionName.setText(saved.versionName);
    auto& codeColumn = versions.emplace<ui::Column>(4.0f);
    codeColumn.setStretch(1.0f);
    codeColumn.emplace<ui::Label>("Version code (1, 2, 3...)", ui::Label::Style::Dim);
    auto& versionCode = codeColumn.emplace<ui::TextField>("1");
    versionCode.setText(std::to_string(saved.versionCode));
    body.emplace<ui::Label>("Google Play wants a higher version code with every upload.", ui::Label::Style::Faint)
        .setWrap(true);

    auto& choices = body.emplace<ui::Row>(8.0f);
    auto& heldColumn = choices.emplace<ui::Column>(4.0f);
    heldColumn.setStretch(1.0f);
    heldColumn.emplace<ui::Label>("Held", ui::Label::Style::Dim);
    const bool upright = project.height > project.width;
    auto& held = heldColumn.emplace<ui::ChoiceField>(std::vector<std::string>{
        std::string("As the game's size (") + (upright ? "upright" : "sideways") + ")", "Sideways (landscape)", "Upright (portrait)"});
    held.setSelected(saved.orientation == "landscape" ? 1 : saved.orientation == "portrait" ? 2 : 0);
    auto& iconColumn = choices.emplace<ui::Column>(4.0f);
    iconColumn.setStretch(1.0f);
    iconColumn.emplace<ui::Label>("Icon (a square PNG in the project)", ui::Label::Style::Dim);
    // The project's PNG pictures to choose from.
    auto icons = std::make_shared<std::vector<std::string>>();
    std::vector<std::string> iconNames = {"Rolo's icon"};
    if (m_assetDb) {
        for (const AssetRecord* image : m_assetDb->all(AssetType::Image)) {
            if (image->path.size() > 4 && image->path.compare(image->path.size() - 4, 4, ".png") == 0) {
                icons->push_back(image->path);
            }
        }
        std::sort(icons->begin(), icons->end());
        iconNames.insert(iconNames.end(), icons->begin(), icons->end());
    }
    auto& icon = iconColumn.emplace<ui::ChoiceField>(iconNames);
    if (const auto found = std::find(icons->begin(), icons->end(), saved.icon); found != icons->end()) {
        icon.setSelected(static_cast<int>(found - icons->begin()) + 1);
    }

    // Google Play.
    auto& play = body.emplace<ui::Checkbox>("Also make the file for Google Play (.aab), signed with my upload key");
    body.emplace<ui::Label>("Upload key file", ui::Label::Style::Dim);
    auto& keyRow = body.emplace<ui::Row>(6.0f);
    auto& keyFile = keyRow.emplace<ui::TextField>("None yet: make one with New Key…");
    keyFile.setStretch(1.0f);
    keyFile.setText(m_preferences.getString(kAndroidKeyFileKey, ""));
    body.emplace<ui::Label>("The key's password (asked every time; Rolo does not store it)", ui::Label::Style::Dim);
    auto& password = body.emplace<ui::TextField>("");
    password.setSecret(true);
    keyRow.emplace<ui::Button>("Browse…", [this, &keyFile] {
        const std::filesystem::path start = keyFile.text().empty() ? suggestedKeyFile().parent_path()
                                                                   : std::filesystem::path(keyFile.text()).parent_path();
        showFilePicker(*m_ui, "Your Upload Key", start, {".jks", ".keystore", ".p12"},
                       [&keyFile](const std::filesystem::path& chosen) { keyFile.setText(chosen.string()); });
    });
    keyRow.emplace<ui::Button>("New Key…", [this, &keyFile, &password, &play] {
        showNewAndroidKeyDialog([&keyFile, &password, &play](const AndroidSigning& key) {
            keyFile.setText(key.keystore.string());
            password.setText(key.password);
            play.setChecked(true);
        });
    });
    std::error_code ec;
    play.setChecked(!keyFile.text().empty() && std::filesystem::exists(keyFile.text(), ec));

    body.emplace<ui::Label>("The first export downloads Google's build tools (a few hundred MB) and takes several "
                            "minutes; later ones a minute or two. You can go on working meanwhile.",
                            ui::Label::Style::Faint)
        .setWrap(true);
    for (const std::string& note : setup.report.notes) {
        body.emplace<ui::Label>(note, ui::Label::Style::Faint).setWrap(true);
    }
    auto& error = body.emplace<ui::Label>("");
    error.setColor(m_ui->theme().error);
    error.setWrap(true);

    ui::Button& toolsButton = dialog->addButton("Tools…");
    dialog->addButton("Cancel");
    ui::Button& ok = dialog->addButton("Export", ui::Button::Style::Primary);
    ui::Dialog& d = ui::showDialog(*m_ui, std::move(dialog));
    toolsButton.setOnClick([this, &d, gameName, into] {
        m_ui->closeOverlay(&d);
        m_ui->post([this, gameName, into] {
            showAndroidToolsDialog([this, gameName, into] { showAndroidExportDialog(gameName, into); });
        });
    });
    ok.setOnClick([this, &d, &package, &versionName, &versionCode, &held, &icon, icons, &play, &keyFile, &password, &error,
                   gameName, into, tools = setup.tools] {
        AndroidSettings android;
        android.package = package.text();
        std::string why;
        if (!isValidAndroidPackage(android.package, &why)) {
            error.setText("The package name cannot be used: " + why);
            return;
        }
        const std::string& code = versionCode.text();
        const auto [end, bad] = std::from_chars(code.data(), code.data() + code.size(), android.versionCode);
        if (bad != std::errc{} || end != code.data() + code.size() || android.versionCode < 1 || android.versionCode > 2100000000) {
            error.setText("The version code must be a whole number, 1 or more.");
            return;
        }
        android.versionName = versionName.text().empty() ? std::string("1.0") : versionName.text();
        android.orientation = held.selected() == 1 ? "landscape" : held.selected() == 2 ? "portrait" : "";
        if (icon.selected() > 0 && static_cast<std::size_t>(icon.selected()) <= icons->size()) {
            android.icon = (*icons)[static_cast<std::size_t>(icon.selected()) - 1];
        }
        AndroidSigning signing;
        if (play.isChecked()) {
            signing.keystore = keyFile.text();
            signing.password = password.text();
            std::error_code missing;
            if (signing.keystore.empty() || !std::filesystem::is_regular_file(signing.keystore, missing)) {
                error.setText("Choose your upload key file, or make one with New Key… (or untick the Google Play box "
                              "to make only the app to try).");
                return;
            }
            if (signing.password.empty()) {
                error.setText("Type the upload key's password.");
                return;
            }
        }
        // Remembered for the next export: the app's details in the project
        // (one undoable step), the key file's place in the editor's preferences.
        endEdit();
        static_cast<void>(execute(std::make_unique<SetAndroidSettings>(android)));
        endEdit();
        if (play.isChecked()) {
            m_preferences.setString(kAndroidKeyFileKey, keyFile.text());
            saveSettings();
        }
        m_ui->closeOverlay(&d);
        startAndroidExport(androidExportOptions(m_project->folder(), into, gameName, android, tools, m_resources),
                           std::move(signing));
    });
}

void EditorApp::startAndroidExport(AndroidExportOptions options, AndroidSigning signing)
{
    // The same check as every export, here on the main thread (it logs).
    log().info("export", "Checking " + options.project.string() + " before exporting...");
    const ProjectCheck check = checkProject(options.project, m_registry, log());
    if (check.errors > 0) {
        showMessage("Could Not Export",
                    "The game was not exported: the check found " + std::to_string(check.errors)
                        + (check.errors == 1 ? " error" : " errors")
                        + " (each one is listed in the Console). Fix them, then export again.");
        return;
    }
    static_cast<void>(warnAboutModuleComponents(options.project, m_registry, log(), "android"));

    m_androidJob = std::make_unique<AndroidExportJob>(std::move(options), std::move(signing));

    auto dialog = std::make_unique<ui::Dialog>("Exporting for Android", 520.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>("Google's tools are building the app. This takes a few minutes; the first time they are "
                            "downloaded as well (a few hundred MB), which takes longer.")
        .setWrap(true);
    auto& line = body.emplace<ui::Label>("Starting...", ui::Label::Style::Faint);
    line.setWrap(true);
    body.emplace<ui::Label>("You can close this and go on working: the Console shows the progress, and Rolo says "
                            "when the export is done.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    ui::Button& cancel = dialog->addButton("Cancel the Export");
    dialog->addButton("Keep Working", ui::Button::Style::Primary);
    dialog->setOnClosed([this] {
        m_androidProgress = nullptr;
        m_androidProgressLine = nullptr;
    });
    m_androidProgressLine = &line;
    m_androidProgress = &ui::showDialog(*m_ui, std::move(dialog));
    cancel.setOnClick([this] {
        if (m_androidJob) {
            m_androidJob->cancel();
        }
        // (The dialog closes with the click; the export ends shortly after, and says so.)
    });
}

void EditorApp::updateAndroidExport()
{
    if (!m_androidJob) {
        return;
    }
    std::string last;
    for (std::string& said : m_androidJob->takeLines()) {
        if (!said.empty()) {
            log().info("android", said);
            last = std::move(said);
        }
    }
    if (!last.empty() && m_androidProgressLine != nullptr) {
        // One line of it, cut short: Gradle's lines can be very long.
        m_androidProgressLine->setText(last.size() > 160 ? last.substr(0, 160) + "..." : last);
    }
    if (!m_androidJob->finished()) {
        return;
    }
    const AndroidExportResult result = m_androidJob->result();
    m_androidJob.reset();   // (joins the worker, which has ended)
    if (m_androidProgress != nullptr) {
        m_ui->closeOverlay(m_androidProgress);
        m_androidProgress = nullptr;
        m_androidProgressLine = nullptr;
    }
    if (result.cancelled) {
        log().info("export", result.message);
        return;
    }
    if (!result.ok) {
        log().error("export", result.message);
        showMessage("Could Not Export for Android", result.message);
        return;
    }
    log().info("export", result.message);
    auto dialog = std::make_unique<ui::Dialog>("Game Exported for Android", 520.0f);
    ui::Column& body = dialog->body();
    body.emplace<ui::Label>(result.message).setWrap(true);
    body.emplace<ui::Label>("To try it: start an emulator in Android Studio (More Actions > Virtual Device Manager) and "
                            "drag " + result.apk.filename().string() + " onto it; or copy the file to a phone and open "
                            "it there."
                                + (result.aab.empty()
                                       ? std::string()
                                       : " For Google Play, upload " + result.aab.filename().string() + " in the Play Console.")
                                + " README.txt in the folder says more.",
                            ui::Label::Style::Dim)
        .setWrap(true);
    for (const std::string& note : result.project.notes) {
        body.emplace<ui::Label>(note, ui::Label::Style::Faint).setWrap(true);
    }
    const std::filesystem::path where = result.project.folder;
    dialog->addButton("Show in Folder", ui::Button::Style::Normal, [where] { revealInFileManager(where); });
    dialog->addButton("Done", ui::Button::Style::Primary);
    ui::showDialog(*m_ui, std::move(dialog));
}

} // namespace rolo::editor
