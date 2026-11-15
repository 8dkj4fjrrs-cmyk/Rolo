// A folder browser made with Rolo UI, for choosing where projects and
// exported games go, and for opening projects. (Rolo has no system file
// dialog: each operating system's is different, and this one behaves the
// same everywhere.) Folders holding a project are marked with Rolo's face.

#include "Panels.hpp"

#include "rolo/scene/Project.hpp"
#include "rolo/ui/Context.hpp"
#include "rolo/ui/widgets/Dialog.hpp"

#include <algorithm>
#include <cctype>
#include <memory>

namespace rolo::editor {

namespace fs = std::filesystem;

namespace {

bool isProject(const fs::path& folder)
{
    std::error_code ec;
    return isProjectFolder(folder);
}

std::string lower(std::string s)
{
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return s;
}

struct Picker {
    ui::Context* ctx = nullptr;
    fs::path current;
    bool projectsOnly = false;
    std::vector<std::string> fileExtensions;   // not empty: a file is being chosen, of one of these kinds
    std::function<void(const fs::path&)> onFile;
    ui::Column* list = nullptr;
    ui::TextField* field = nullptr;
    ui::Label* note = nullptr;
    ui::Button* choose = nullptr;

    void go(fs::path folder);
    void refresh();
};

void Picker::go(fs::path folder)
{
    std::error_code ec;
    if (!fs::is_directory(folder, ec)) {
        note->setText("There is no folder " + folder.string() + ".");
        return;
    }
    current = fs::weakly_canonical(folder, ec);
    refresh();
}

void Picker::refresh()
{
    field->setText(current.string());
    list->clearChildren();
    const auto addRow = [&](ui::Icon icon, const std::string& text, fs::path target) {
        auto& row = list->emplace<ui::Button>(text);
        row.setIcon(icon);
        row.setStyle(ui::Button::Style::Subtle);
        row.setAlign(ui::TextAlign::Start);
        Picker* self = this;
        // Navigating rebuilds this list, so it happens after the click is done.
        row.setOnClick([self, target] { self->ctx->post([self, target] { self->go(target); }); });
    };
    if (current.has_parent_path() && current.parent_path() != current) {
        addRow(ui::Icon::ChevronLeft, "..  (up to " + current.parent_path().filename().string() + ")",
               current.parent_path());
    }
    std::vector<fs::path> folders;
    std::vector<fs::path> files;
    std::error_code ec;
    for (fs::directory_iterator it(current, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
         it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (!name.empty() && name.front() != '.' && it->is_directory(ec)) {
            folders.push_back(it->path());
        } else if (!fileExtensions.empty() && !name.empty() && name.front() != '.'
                   && std::find(fileExtensions.begin(), fileExtensions.end(), lower(it->path().extension().string()))
                          != fileExtensions.end()) {
            files.push_back(it->path());
        }
        if (folders.size() + files.size() >= 500) {
            break;   // enough to choose from
        }
    }
    std::sort(folders.begin(), folders.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    for (const fs::path& f : folders) {
        addRow(isProject(f) ? ui::Icon::Rolo : ui::Icon::Folder, f.filename().string(), f);
    }
    std::sort(files.begin(), files.end(), [](const fs::path& a, const fs::path& b) {
        return lower(a.filename().string()) < lower(b.filename().string());
    });
    for (const fs::path& f : files) {
        auto& row = list->emplace<ui::Button>(f.filename().string());
        row.setIcon(ui::Icon::Dot);
        row.setStyle(ui::Button::Style::Subtle);
        row.setAlign(ui::TextAlign::Start);
        Picker* self = this;
        row.setOnClick([self, f] {
            if (self->onFile) {
                self->onFile(f);
            }
        });
    }
    if (folders.empty() && files.empty()) {
        list->emplace<ui::Label>(fileExtensions.empty() ? "No folders here." : "No folders or files of that kind here.",
                                 ui::Label::Style::Faint);
    }
    if (!fileExtensions.empty()) {
        note->setText("Click a file to choose it, or open a folder.");
        return;
    }
    const bool here = isProject(current);
    if (projectsOnly) {
        note->setText(here ? "This folder is a Rolo project." : "Choose a folder marked with a flame: a project.");
        choose->setEnabled(here);
    } else {
        note->setText(here ? "This folder is a Rolo project: choose the folder around it instead."
                           : "Choose this folder, or open one of the folders above.");
        choose->setEnabled(!here);
    }
}

} // namespace

void showFolderPicker(ui::Context& ctx, const std::string& title, const fs::path& start,
                      std::function<void(const fs::path&)> onChosen, bool projectsOnly)
{
    auto picker = std::make_shared<Picker>();
    picker->ctx = &ctx;
    picker->projectsOnly = projectsOnly;
    auto dialog = std::make_unique<ui::Dialog>(title, 520.0f);
    ui::Column& body = dialog->body();
    auto& field = body.emplace<ui::TextField>("A folder's path");
    field.setFixedSize({0.0f, 24.0f});
    field.setOnSubmit([picker](const std::string& text) { picker->go(fs::path(text)); });
    auto& scroll = body.emplace<ui::ScrollView>();
    scroll.setFixedSize({0.0f, 280.0f});
    auto& list = scroll.setContent(std::make_unique<ui::Column>(1.0f));
    auto& note = body.emplace<ui::Label>("", ui::Label::Style::Faint);
    note.setWrap(true);
    picker->field = &field;
    picker->list = &list;
    picker->note = &note;
    dialog->addButton("Cancel");
    ui::Button& choose = dialog->addButton(projectsOnly ? "Open Project" : "Choose Folder", ui::Button::Style::Primary);
    picker->choose = &choose;
    ui::Dialog& shown = ui::showDialog(ctx, std::move(dialog));
    choose.setOnClick([picker, &ctx, &shown, onChosen] {
        const fs::path chosen = picker->current;
        ctx.closeOverlay(&shown);
        if (onChosen) {
            ctx.post([onChosen, chosen] { onChosen(chosen); });
        }
    });
    // Start in the given folder, or the nearest one that exists.
    std::error_code ec;
    fs::path folder = start.empty() ? fs::current_path(ec) : start;
    while (!folder.empty() && !fs::is_directory(folder, ec) && folder.has_parent_path() && folder.parent_path() != folder) {
        folder = folder.parent_path();
    }
    picker->go(folder);
}

void showFilePicker(ui::Context& ctx, const std::string& title, const fs::path& start,
                    std::vector<std::string> extensions, std::function<void(const fs::path&)> onChosen)
{
    auto picker = std::make_shared<Picker>();
    picker->ctx = &ctx;
    picker->fileExtensions = std::move(extensions);
    auto dialog = std::make_unique<ui::Dialog>(title, 520.0f);
    ui::Column& body = dialog->body();
    auto& field = body.emplace<ui::TextField>("A folder's path");
    field.setFixedSize({0.0f, 24.0f});
    field.setOnSubmit([picker](const std::string& text) { picker->go(fs::path(text)); });
    auto& scroll = body.emplace<ui::ScrollView>();
    scroll.setFixedSize({0.0f, 280.0f});
    auto& list = scroll.setContent(std::make_unique<ui::Column>(1.0f));
    auto& note = body.emplace<ui::Label>("", ui::Label::Style::Faint);
    note.setWrap(true);
    picker->field = &field;
    picker->list = &list;
    picker->note = &note;
    ui::Button& cancel = dialog->addButton("Cancel");
    picker->choose = &cancel;   // (refresh() only reaches it for folders; kept valid)
    ui::Dialog& shown = ui::showDialog(ctx, std::move(dialog));
    picker->onFile = [picker, &ctx, &shown, onChosen](const fs::path& file) {
        // Closing the dialog destroys the row that was clicked: after the click is done.
        ctx.post([&ctx, &shown, onChosen, file] {
            ctx.closeOverlay(&shown);
            if (onChosen) {
                onChosen(file);
            }
        });
    };
    std::error_code ec;
    fs::path folder = start.empty() ? fs::current_path(ec) : start;
    while (!folder.empty() && !fs::is_directory(folder, ec) && folder.has_parent_path() && folder.parent_path() != folder) {
        folder = folder.parent_path();
    }
    picker->go(folder);
}

} // namespace rolo::editor
