// Kosmos Launcher: a small wxWidgets front end that lists scenarios, addons and hi-res packs, sets launch options,
// checks for new builds and starts Kosmos as a separate process. It shares no code with Kosmos or simlib, so a crash in
// either leaves the launcher running.
#include "data.h"
#include "icon_data.h"
#include "json.hpp"

#include <wx/wx.h>
#include <wx/checklst.h>
#include <wx/dir.h>
#include <wx/gauge.h>
#include <wx/listbook.h>
#include <wx/mstream.h>
#include <wx/process.h>
#include <wx/stdpaths.h>
#include <wx/treectrl.h>
#include <wx/webrequest.h>

#include <wx/clipbrd.h>
#include <wx/filename.h>

#include <algorithm>
#include <cstdio>
#include <fstream>
#include <functional>
#include <limits>
#include <map>
#include <memory>

namespace
{
    using namespace launcher;

    wxString W(const std::string &s) { return wxString::FromUTF8(s); }
    std::string U(const wxString &s) { return std::string(s.ToUTF8()); }
    wxString Wp(const fs::path &p) { return wxString::FromUTF8(p.u8string().c_str()); }
    fs::path P(const wxString &s) { return fs::u8path(U(s)); }

    wxString human_size(long long bytes)
    {
        if (bytes <= 0)
            return "unknown size";
        if (bytes > (1LL << 30))
            return wxString::Format("%.1f GB", bytes / double(1LL << 30));
        return wxString::Format("%.0f MB", bytes / double(1LL << 20));
    }

    wxBitmap load_thumbnail(const fs::path &file, int width, int height)
    {
        wxImage image;
        if (!file.empty() && wxFileExists(Wp(file)) && image.LoadFile(Wp(file)) && image.IsOk())
        {
            const double scale = std::min(width / double(image.GetWidth()), height / double(image.GetHeight()));
            image.Rescale(std::max(1, int(image.GetWidth() * scale)), std::max(1, int(image.GetHeight() * scale)), wxIMAGE_QUALITY_HIGH);
            return wxBitmap(image);
        }
        wxBitmap blank(width, height);
        wxMemoryDC dc(blank);
        dc.SetBackground(wxBrush(wxColour(24, 28, 40)));
        dc.Clear();
        dc.SetTextForeground(wxColour(120, 130, 150));
        dc.DrawLabel("No preview", wxRect(0, 0, width, height), wxALIGN_CENTER);
        dc.SelectObject(wxNullBitmap);
        return blank;
    }

    struct IdData : wxTreeItemData
    {
        std::string id;
        explicit IdData(std::string i) : id(std::move(i)) {}
    };

    // One HTTP request at a time: either a small text fetch or a resumable file download with progress.
    class Http : public wxEvtHandler
    {
    public:
        std::function<void(bool ok, const std::string &body_or_error)> on_text;
        std::function<void(long long got, long long total)> on_progress;
        std::function<void(bool ok, const std::string &error)> on_file;

        void Fetch(const std::string &url, std::function<void(bool, const std::string &)> done)
        {
            on_text = std::move(done);
            text_.clear();
            file_.reset();
            Start(url, false);
        }

        void Download(const std::string &url, const fs::path &part, std::function<void(long long, long long)> progress, std::function<void(bool, const std::string &)> done)
        {
            on_progress = std::move(progress);
            on_file = std::move(done);
            std::error_code ec;
            part_ = part;
            const bool exists = fs::exists(part, ec);
            if (!ec && exists)
            {
                const auto size = fs::file_size(part, ec);
                if (!ec && size <= static_cast<std::uintmax_t>(std::numeric_limits<long long>::max()))
                    resume_from_ = static_cast<long long>(size);
                else if (!ec)
                    ec = std::make_error_code(std::errc::file_too_large);
            }
            else
                resume_from_ = 0;
            if (ec)
            {
                auto callback = std::move(on_file);
                callback(false, "Cannot inspect partial download: " + ec.message());
                return;
            }
            file_ = std::make_unique<std::ofstream>(part, std::ios::binary | std::ios::app);
            if (!*file_)
            {
                file_.reset();
                auto callback = std::move(on_file);
                callback(false, "Cannot open partial download (check disk space and permissions)");
                return;
            }
            file_error_.clear();
            Start(url, true);
        }

        void Cancel()
        {
            if (request_.IsOk())
                request_.Cancel();
        }

    private:
        void Start(const std::string &url, bool to_file)
        {
            to_file_ = to_file;
            request_ = wxWebSession::GetDefault().CreateRequest(this, W(url));
            if (!request_.IsOk())
            {
                Finish(false, "Cannot create a web request");
                return;
            }
            if (to_file && resume_from_ > 0)
                request_.SetHeader("Range", wxString::Format("bytes=%lld-", resume_from_));
            request_.SetHeader("User-Agent", "Kosmos-Launcher");
            request_.SetStorage(wxWebRequest::Storage_None);
            Bind(wxEVT_WEBREQUEST_DATA, &Http::OnData, this);
            Bind(wxEVT_WEBREQUEST_STATE, &Http::OnState, this);
            request_.Start();
        }

        void OnData(wxWebRequestEvent &e)
        {
            const char *bytes = static_cast<const char *>(e.GetDataBuffer());
            if (to_file_)
            {
                const int status = request_.GetResponse().GetStatus();
                if (!started_ && status == 206)
                {
                    const std::string range = U(request_.GetResponse().GetHeader("Content-Range"));
                    const std::string prefix = "bytes " + std::to_string(resume_from_) + "-";
                    if (resume_from_ <= 0 || range.compare(0, prefix.size(), prefix) != 0)
                        file_error_ = "Invalid Content-Range in resumed download";
                }
                // A 200 reply to a Range request means the server ignored it; restart the file.
                if (!started_ && resume_from_ > 0 && status == 200)
                {
                    file_->close();
                    file_ = std::make_unique<std::ofstream>(part_path(), std::ios::binary | std::ios::trunc);
                    resume_from_ = 0;
                }
                started_ = true;
                if (status != 200 && status != 206)
                    return;
                if (file_error_.empty())
                {
                    file_->write(bytes, static_cast<std::streamsize>(e.GetDataSize()));
                    if (!*file_)
                        file_error_ = "Cannot write download (check disk space and permissions)";
                }
                received_ += static_cast<long long>(e.GetDataSize());
                if (on_progress)
                {
                    const long long expected = request_.GetBytesExpectedToReceive();
                    on_progress(resume_from_ + received_, expected > 0 ? resume_from_ + expected : 0);
                }
            }
            else
                text_.append(bytes, e.GetDataSize());
        }

        void OnState(wxWebRequestEvent &e)
        {
            switch (e.GetState())
            {
            case wxWebRequest::State_Completed:
                Finish(true, "");
                break;
            case wxWebRequest::State_Failed:
                Finish(false, U(e.GetErrorDescription()));
                break;
            case wxWebRequest::State_Cancelled:
                Finish(false, "Cancelled");
                break;
            default:
                break;
            }
        }

        void Finish(bool ok, const std::string &error)
        {
            Unbind(wxEVT_WEBREQUEST_DATA, &Http::OnData, this);
            Unbind(wxEVT_WEBREQUEST_STATE, &Http::OnState, this);
            bool http_ok = ok && request_.IsOk() && request_.GetResponse().IsOk() && (request_.GetResponse().GetStatus() == 200 || request_.GetResponse().GetStatus() == 206);
            std::string why = error;
            if (ok && !http_ok)
                why = "Server replied " + std::to_string(request_.GetResponse().GetStatus());
            if (http_ok && to_file_)
            {
                const wxString length = request_.GetResponse().GetHeader("Content-Length");
                long long expected = 0;
                if (!length.empty() && (!length.ToLongLong(&expected) || expected < 0 || expected != received_))
                {
                    http_ok = false;
                    why = "Incomplete download: response length does not match";
                }
            }
            started_ = false;
            received_ = 0;
            if (to_file_)
            {
                if (file_)
                {
                    file_->close();
                    if (!*file_ && file_error_.empty())
                        file_error_ = "Cannot finish download (check disk space and permissions)";
                }
                file_.reset();
                if (!file_error_.empty())
                {
                    http_ok = false;
                    why = file_error_;
                }
                if (on_file)
                {
                    auto callback = std::move(on_file);
                    callback(http_ok, why);
                }
            }
            else if (on_text)
            {
                auto callback = std::move(on_text);
                const std::string result = http_ok ? text_ : why;
                callback(http_ok, result);
            }
        }

        fs::path part_path() const { return part_; }

        wxWebRequest request_;
        std::string text_, file_error_;
        std::unique_ptr<std::ofstream> file_;
        fs::path part_;
        long long resume_from_ = 0, received_ = 0;
        bool to_file_ = false, started_ = false;

    };

    // Starts Kosmos, keeps its output for the Logs page and reports how it ended.
    class Run : public wxProcess
    {
    public:
        std::function<void(int code)> on_end;
        wxString output;
        long pid = 0;

        explicit Run(wxEvtHandler *owner) : wxProcess(owner) { Redirect(); }

        void Drain()
        {
            for (wxInputStream *in : {GetInputStream(), GetErrorStream()})
                while (in && in->CanRead())
                {
                    char buf[4096];
                    in->Read(buf, sizeof buf);
                    const size_t n = in->LastRead();
                    if (!n)
                        break;
                    output += wxString::FromUTF8(buf, n);
                }
        }

        void OnTerminate(int, int status) override
        {
            Drain();
            if (on_end)
                on_end(status);
        }
    };

    class Frame : public wxFrame
    {
    public:
        Frame(const fs::path &root,
              std::string api_url = "https://api.github.com/repos/daver64/Kosmos/releases",
              std::string asset_url = "https://github.com/daver64/Kosmos/releases/download")
            : wxFrame(nullptr, wxID_ANY, "Kosmos Launcher", wxDefaultPosition, wxSize(1000, 680)),
              root_(root), api_url_(std::move(api_url)), asset_url_(std::move(asset_url))
        {
            {
                wxMemoryInputStream stream(launcher_icon_png, sizeof launcher_icon_png);
                wxImage image(stream, wxBITMAP_TYPE_PNG);
                if (image.IsOk())
                {
                    wxIcon icon;
                    icon.CopyFromBitmap(wxBitmap(image));
                    SetIcon(icon);
                }
            }
            const std::string installed = installed_version(root_);
            SetTitle(installed.empty() ? "Kosmos Launcher" : "Kosmos Launcher - Kosmos " + installed + " installed");
            profile_ = load_profile();
            book_ = new wxListbook(this, wxID_ANY, wxDefaultPosition, wxDefaultSize, wxLB_LEFT);
            BuildPlay();
            BuildAddons();
            BuildPacks();
            BuildGraphics();
            BuildOptions();
            BuildUpdates();
            BuildLogs();
            CreateStatusBar();
            timer_.SetOwner(this);
            Bind(wxEVT_TIMER, [this](wxTimerEvent &) { if (run_) run_->Drain(); });
            Bind(wxEVT_CLOSE_WINDOW, &Frame::OnClose, this);
            Rescan();
            RefreshPreview();
            if (find_kosmos(root_).empty())
                SetStatusText("Kosmos was not found next to the launcher.");
        }

    public:
        void StartDiscovery() { CheckUpdates(); }

        void SelfCheck()
        {
            self_check_ = true;
            CheckUpdates();
        }
        void SelfCheckPack()
        {
            self_check_ = true;
            self_check_pack_ = true;
            CheckUpdates();
        }

    private:
        // ---- Play ----------------------------------------------------------------------------------------------
        void BuildPlay()
        {
            auto *page = new wxPanel(book_);
            auto *row = new wxBoxSizer(wxHORIZONTAL);
            tree_ = new wxTreeCtrl(page, wxID_ANY, wxDefaultPosition, wxSize(300, -1), wxTR_HIDE_ROOT | wxTR_HAS_BUTTONS | wxTR_SINGLE);
            auto *right = new wxBoxSizer(wxVERTICAL);
            thumb_ = new wxStaticBitmap(page, wxID_ANY, load_thumbnail({}, 480, 270));
            title_ = new wxStaticText(page, wxID_ANY, "");
            title_->SetFont(title_->GetFont().Bold().Scaled(1.4f));
            detail_ = new wxStaticText(page, wxID_ANY, "");
            blurb_ = new wxStaticText(page, wxID_ANY, "");
            launch_ = new wxButton(page, wxID_ANY, "Launch");
            launch_->SetFont(launch_->GetFont().Bold().Scaled(1.3f));
            resume_ = new wxButton(page, wxID_ANY, "Resume last scenario");
            right->Add(thumb_, 0, wxALL, 8);
            right->Add(title_, 0, wxLEFT | wxRIGHT, 8);
            right->Add(detail_, 0, wxALL, 8);
            right->Add(blurb_, 1, wxLEFT | wxRIGHT | wxEXPAND, 8);
            auto *buttons = new wxBoxSizer(wxHORIZONTAL);
            buttons->Add(launch_, 0, wxALL, 8);
            buttons->Add(resume_, 0, wxALL, 8);
            mission_ = new wxButton(page, wxID_ANY, "KosmosMission");
            mission_->SetToolTip("Plan a mission (transfers, burns, payloads). It starts Kosmos itself when you send a leg to the game.");
            mission_->Enable(!find_addon_program(root_, "kosmosmission", "kosmosmission").empty());
            buttons->Add(mission_, 0, wxALL, 8);
            mission_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { LaunchMission(); });
            right->Add(buttons, 0);
            row->Add(tree_, 0, wxEXPAND | wxALL, 8);
            row->Add(right, 1, wxEXPAND);
            page->SetSizer(row);
            book_->AddPage(page, "Play");
            tree_->Bind(wxEVT_TREE_SEL_CHANGED, [this](wxTreeEvent &) { ShowScenario(); });
            tree_->Bind(wxEVT_TREE_ITEM_ACTIVATED, [this](wxTreeEvent &) { LaunchSelected(); });
            launch_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { LaunchSelected(); });
            resume_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) {
                std::error_code ec;
                Launch(fs::exists(user_dir() / "scenarios" / "last_session.json", ec) ? "last_session" : profile_.last_scenario);
            });
            // Text wraps to the width left beside the scenario tree, so a long description or ship list stays inside its box.
            page->Bind(wxEVT_SIZE, [this](wxSizeEvent &e) { WrapScenarioText(); e.Skip(); });
        }

        void WrapScenarioText()
        {
            const int width = std::max(200, blurb_->GetParent()->GetClientSize().x - tree_->GetSize().x - 48);
            wxStaticText *labels[3] = {title_, detail_, blurb_};
            for (int i = 0; i < 3; ++i)
            {
                labels[i]->SetLabel(scenario_text_[i]);
                labels[i]->Wrap(width);
                // The sizer keeps the height of the previous text unless told, which let a wrapped ship list run into the description.
                labels[i]->InvalidateBestSize();
                labels[i]->SetMinSize(labels[i]->GetBestSize());
            }
            blurb_->GetContainingSizer()->Layout();
        }

        const ScenarioInfo *SelectedScenario() const
        {
            const wxTreeItemId item = tree_->GetSelection();
            if (!item.IsOk())
                return nullptr;
            auto *data = dynamic_cast<IdData *>(tree_->GetItemData(item));
            if (!data)
                return nullptr;
            for (const ScenarioInfo &s : scenarios_)
                if (s.id == data->id)
                    return &s;
            return nullptr;
        }

        void ShowScenario()
        {
            const ScenarioInfo *s = SelectedScenario();
            launch_->Enable(s != nullptr && !run_);
            if (!s)
                return;
            thumb_->SetBitmap(load_thumbnail(s->thumbnail, 480, 270));
            scenario_text_[0] = W(s->name);
            wxString ships;
            for (const std::string &n : s->ships)
                ships += (ships.empty() ? "" : ", ") + W(n);
            scenario_text_[1] = wxString::Format("Ship: %s    Location: %s", ships, W(s->body.empty() ? "n/a" : s->body));
            scenario_text_[2] = W(s->description);
            WrapScenarioText();
            Layout();
        }

        void FillTree()
        {
            tree_->DeleteAllItems();
            const wxTreeItemId root = tree_->AddRoot("");
            std::map<std::string, wxTreeItemId> groups; // keyed by folder path, so nested folders become nested nodes
            wxTreeItemId select;
            auto group_for = [&](const std::string &path) {
                wxTreeItemId parent = root;
                std::string key;
                std::size_t start = 0;
                while (start <= path.size())
                {
                    std::size_t end = path.find('/', start);
                    if (end == std::string::npos)
                        end = path.size();
                    const std::string part = path.substr(start, end - start);
                    key += (key.empty() ? "" : "/") + part;
                    if (!groups.count(key))
                        groups[key] = tree_->AppendItem(parent, W(part));
                    parent = groups[key];
                    start = end + 1;
                }
                return parent;
            };
            for (const ScenarioInfo &s : scenarios_)
            {
                const wxTreeItemId parent = group_for(s.category.empty() ? "Standard" : s.category);
                const wxTreeItemId item = tree_->AppendItem(parent, W(s.name), -1, -1, new IdData(s.id));
                if (s.id == profile_.last_scenario)
                    select = item;
            }
            if (select.IsOk())
            {
                tree_->EnsureVisible(select);
                tree_->SelectItem(select);
            }
            else if (!groups.empty())
                tree_->Expand(groups.begin()->second);
            resume_->Enable(!profile_.last_scenario.empty());
        }

        // ---- Addons --------------------------------------------------------------------------------------------
        void BuildAddons()
        {
            auto *page = new wxPanel(book_);
            auto *row = new wxBoxSizer(wxHORIZONTAL);
            addon_list_ = new wxCheckListBox(page, wxID_ANY, wxDefaultPosition, wxSize(300, -1));
            auto *right = new wxBoxSizer(wxVERTICAL);
            addon_thumb_ = new wxStaticBitmap(page, wxID_ANY, load_thumbnail({}, 480, 270));
            addon_title_ = new wxStaticText(page, wxID_ANY, "");
            addon_title_->SetFont(addon_title_->GetFont().Bold().Scaled(1.4f));
            addon_text_ = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
            auto *open = new wxButton(page, wxID_ANY, "Open addons folder");
            auto *install = new wxButton(page, wxID_ANY, "Install addon from zip...");
            auto *buttons = new wxBoxSizer(wxHORIZONTAL);
            buttons->Add(open, 0, wxALL, 8);
            buttons->Add(install, 0, wxALL, 8);
            right->Add(addon_thumb_, 0, wxALL, 8);
            right->Add(addon_title_, 0, wxLEFT | wxRIGHT, 8);
            right->Add(addon_text_, 1, wxALL | wxEXPAND, 8);
            right->Add(buttons, 0);
            row->Add(addon_list_, 0, wxEXPAND | wxALL, 8);
            row->Add(right, 1, wxEXPAND);
            page->SetSizer(row);
            book_->AddPage(page, "Addons");
            addon_list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { ShowAddon(); });
            addon_list_->Bind(wxEVT_CHECKLISTBOX, [this](wxCommandEvent &e)
                              {
                                  const int i = e.GetInt();
                                  addons_[i].enabled = addon_list_->IsChecked(i);
                                  save_disabled_addons(addons_);
                                  scenarios_ = scan_scenarios(root_, addons_);
                                  FillTree();
                                  SetStatusText("Addon changes apply the next time Kosmos starts.");
                              });
            open->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { wxLaunchDefaultApplication(Wp(root_ / "addons")); });
            install->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { InstallAddon(); });
        }

        void ShowAddon()
        {
            const int i = addon_list_->GetSelection();
            if (i == wxNOT_FOUND)
                return;
            const AddonInfo &a = addons_[i];
            addon_thumb_->SetBitmap(load_thumbnail(a.thumbnail, 480, 270));
            addon_title_->SetLabel(W(a.name + "  v" + a.version));
            wxString ships;
            for (const std::string &n : a.ships)
                ships += "  " + W(n) + "\n";
            wxString text;
            if (!a.author.empty())
                text += "By " + W(a.author) + "\n\n";
            text += W(a.long_description.empty() ? a.description : a.long_description);
            if (text.empty())
                text = "No description.";
            if (!ships.empty())
                text += "\n\nShips:\n" + ships;
            addon_text_->SetValue(text);
            Layout();
        }

        void InstallAddon()
        {
            wxFileDialog dlg(this, "Choose an addon zip", "", "", "Zip files (*.zip)|*.zip", wxFD_OPEN | wxFD_FILE_MUST_EXIST);
            if (dlg.ShowModal() != wxID_OK)
                return;
            const fs::path temp = fs::temp_directory_path() / "kosmos-addon-install";
            std::error_code ec;
            fs::remove_all(temp, ec);
            fs::create_directories(temp, ec);
            if (!Extract(P(dlg.GetPath()), temp))
            {
                wxMessageBox("Could not unpack the zip.", "Install addon", wxICON_ERROR);
                return;
            }
            fs::path found;
            for (fs::recursive_directory_iterator it(temp, ec), end; it != end; it.increment(ec))
                if (it->path().filename() == "addon.json")
                {
                    found = it->path().parent_path();
                    break;
                }
            if (found.empty())
            {
                wxMessageBox("The zip has no addon.json, so it is not a Kosmos addon.", "Install addon", wxICON_ERROR);
                return;
            }
            const fs::path dest = root_ / "addons" / found.filename();
            if (fs::exists(dest, ec) && wxMessageBox("An addon named " + Wp(found.filename()) + " is already installed. Replace it?", "Install addon", wxYES_NO) != wxYES)
                return;
            fs::remove_all(dest, ec);
            fs::copy(found, dest, fs::copy_options::recursive, ec);
            fs::remove_all(temp, ec);
            if (ec)
                wxMessageBox("Could not copy the addon: " + wxString(ec.message()), "Install addon", wxICON_ERROR);
            else
                SetStatusText("Installed " + Wp(found.filename()));
            Rescan();
        }

        // ---- Packs ---------------------------------------------------------------------------------------------
        void BuildPacks()
        {
            auto *page = new wxPanel(book_);
            auto *col = new wxBoxSizer(wxVERTICAL);
            col->Add(new wxStaticText(page, wxID_ANY, "Optional downloads. They install into the Kosmos folder and are checked against the published SHA-256 checksum."), 0, wxALL, 8);
            pack_list_ = new wxListBox(page, wxID_ANY);
            pack_text_ = new wxStaticText(page, wxID_ANY, "");
            pack_gauge_ = new wxGauge(page, wxID_ANY, 1000);
            pack_install_ = new wxButton(page, wxID_ANY, "Download and install");
            pack_remove_ = new wxButton(page, wxID_ANY, "Remove");
            auto *buttons = new wxBoxSizer(wxHORIZONTAL);
            buttons->Add(pack_install_, 0, wxALL, 8);
            buttons->Add(pack_remove_, 0, wxALL, 8);
            col->Add(pack_list_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
            col->Add(pack_text_, 0, wxALL, 8);
            col->Add(pack_gauge_, 0, wxEXPAND | wxLEFT | wxRIGHT, 8);
            col->Add(buttons, 0);
            page->SetSizer(col);
            book_->AddPage(page, "Scenery packs");
            pack_list_->Bind(wxEVT_LISTBOX, [this](wxCommandEvent &) { ShowPack(); });
            pack_install_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { InstallPack(); });
            pack_remove_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &)
                               {
                                   const int i = pack_list_->GetSelection();
                                   if (i == wxNOT_FOUND || packs_[i].marker.empty())
                                       return;
                                   if (wxMessageBox("Delete " + W(packs_[i].marker) + "?", "Remove pack", wxYES_NO) != wxYES)
                                       return;
                                   std::error_code ec;
                                   fs::remove_all(root_ / packs_[i].marker, ec);
                                   RefreshPacks();
                               });
        }

        void RefreshPacks()
        {
            const int keep = pack_list_->GetSelection();
            packs_ = packs_remote_;
            std::error_code ec;
            for (PackInfo &p : packs_)
                p.installed = !p.marker.empty() && fs::exists(root_ / p.marker, ec);
            pack_list_->Clear();
            for (const PackInfo &p : packs_)
                pack_list_->Append(W(p.name) + (p.installed ? "   [installed]" : ""));
            if (!packs_.empty())
                pack_list_->SetSelection(keep == wxNOT_FOUND ? 0 : std::min<int>(keep, int(packs_.size()) - 1));
            ShowPack();
        }

        void ShowPack()
        {
            const int i = pack_list_->GetSelection();
            const bool ok = i != wxNOT_FOUND;
            pack_install_->Enable(ok && !busy_ && !release_.tag.empty());
            pack_remove_->Enable(ok && packs_[i].installed && !busy_);
            if (ok)
            {
                pack_text_->SetLabel(W(packs_[i].description) + "\n" + (packs_[i].installed ? "Installed." : "Not installed.") + "  Download size: " + human_size(packs_[i].size));
                pack_text_->Wrap(600);
            }
            Layout();
        }

        void InstallPack()
        {
            const int i = pack_list_->GetSelection();
            if (i == wxNOT_FOUND || busy_)
                return;
            if (static_cast<std::size_t>(i) >= manifest_.packs.size())
                return;
            busy_ = true;
            ShowPack();
            DownloadPack(manifest_.packs[i], 0);
        }

        fs::path DownloadDir() const { return user_dir() / "downloads" / release_.tag; }

        void DownloadAsset(const ReleaseAsset &asset, wxGauge *gauge, std::function<void(bool, const std::string &)> done)
        {
            std::error_code ec;
            if (user_dir().empty())
            {
                done(false, "Cannot find the user configuration directory");
                return;
            }
            const fs::path dir = DownloadDir();
            fs::create_directories(dir, ec);
            if (ec)
            {
                done(false, "Cannot create download directory: " + ec.message());
                return;
            }
            const fs::path file = dir / asset.file;
            const bool exists = fs::exists(file, ec);
            std::uintmax_t size = 0;
            if (!ec && exists)
                size = fs::file_size(file, ec);
            if (ec)
            {
                done(false, "Cannot inspect download: " + ec.message());
                return;
            }
            if (size >= static_cast<std::uintmax_t>(asset.size))
            {
                if (size == static_cast<std::uintmax_t>(asset.size) && sha256_file(file) == asset.sha256)
                {
                    done(true, "");
                    return;
                }
                fs::remove(file, ec);
                done(false, ec ? "Cannot discard invalid download: " + ec.message() :
                                 "Invalid download discarded; retry to download it again");
                return;
            }
            const auto space = fs::space(dir, ec);
            if (ec || space.available < static_cast<std::uintmax_t>(asset.size) - size)
            {
                done(false, ec ? "Cannot check free disk space: " + ec.message() : "Not enough disk space for the download");
                return;
            }
            const std::string url = asset_url_ + "/" + release_.tag + "/" + asset.file;
            http_.Download(url, file,
                           [this, gauge, asset](long long got, long long)
                           {
                               gauge->SetValue(std::min(1000, int(1000.0 * got / asset.size)));
                               SetStatusText(W("Downloading " + asset.file) + ": " + human_size(got) + " of " + human_size(asset.size));
                           },
                           [this, file, asset, gauge, done](bool ok, const std::string &error)
                           {
                               gauge->SetValue(0);
                               if (!ok)
                               {
                                   done(false, error + " (retry to resume)");
                                   return;
                               }
                               std::error_code e;
                               const auto size = fs::file_size(file, e);
                               if (e || size != static_cast<std::uintmax_t>(asset.size) || sha256_file(file) != asset.sha256)
                               {
                                   fs::remove(file, e);
                                   done(false, e ? "Invalid download; cannot discard it: " + e.message() :
                                                   "Download size or checksum mismatch; retry");
                                   return;
                               }
                               done(true, "");
                           });
        }

        void DownloadPack(const SplitPack &pack, std::size_t index)
        {
            if (index < pack.parts.size())
            {
                DownloadAsset(pack.parts[index], pack_gauge_, [this, pack, index](bool ok, const std::string &error)
                              {
                                  if (!ok)
                                  {
                                      busy_ = false;
                                      SetStatusText(W("Pack failed: " + error));
                                      RefreshPacks();
                                      if (self_check_pack_)
                                      {
                                          std::printf("Pack ERROR: %s\n", error.c_str());
                                          Close();
                                      }
                                      return;
                                  }
                                  CallAfter([this, pack, index] { DownloadPack(pack, index + 1); });
                              });
                return;
            }
            std::error_code ec;
            std::string error;
            const fs::path archive = DownloadDir() / pack.pack.file;
            fs::remove(archive, ec);
            if (ec)
                error = "Cannot clear previous reconstructed archive: " + ec.message();
            const auto space = fs::space(DownloadDir(), ec);
            if (error.empty() && (ec || space.available < static_cast<std::uintmax_t>(pack.pack.size)))
                error = ec ? "Cannot check disk space: " + ec.message() : "Not enough disk space to reconstruct the pack";
            bool installed = false;
            if (error.empty() && reconstruct_pack(pack, DownloadDir(), archive, error))
            {
                SetStatusText("Unpacking pack...");
                if (!Extract(archive, root_))
                    error = "Could not unpack the pack";
                else
                {
                    installed = true;
                    fs::remove(archive, ec);
                    if (ec)
                        error = "Could not remove temporary archive: " + ec.message();
                    for (const ReleaseAsset &part : pack.parts)
                    {
                        fs::remove(DownloadDir() / part.file, ec);
                        if (ec && error.empty())
                            error = "Could not remove downloaded part: " + ec.message();
                    }
                }
            }
            busy_ = false;
            SetStatusText(error.empty() ? "Pack installed." : W((installed ? "Pack installed; cleanup warning: " : "Pack failed: ") + error));
            RefreshPacks();
            if (self_check_pack_)
            {
                std::printf("Pack %s%s%s\n", error.empty() ? "OK" : "ERROR", error.empty() ? "" : ": ", error.c_str());
                Close();
            }
        }

        // Unpacks a .zip or .tar.gz with tar (bsdtar ships with Windows 10 and later).
        bool Extract(const fs::path &archive, const fs::path &dest)
        {
            wxArrayString out;
            const wxString cmd = "tar -xf \"" + Wp(archive) + "\" -C \"" + Wp(dest) + "\"";
            return wxExecute(cmd, out, out, wxEXEC_SYNC | wxEXEC_NODISABLE | wxEXEC_HIDE_CONSOLE) == 0;
        }

        // ---- Graphics (kosmos settings.json) --------------------------------------------------------------------
        fs::path SettingsFile() const { return user_dir() / "settings.json"; }

        void BuildGraphics()
        {
            auto *page = new wxPanel(book_);
            auto *grid = new wxFlexGridSizer(2, 8, 8);
            grid->AddGrowableCol(1);
            gfx_style_ = new wxChoice(page, wxID_ANY);
            for (const char *s : {"Normal", "Flat shaded (fast)", "Wireframe (fastest)"})
                gfx_style_->Append(s);
            gfx_clouds_ = new wxCheckBox(page, wxID_ANY, "Clouds");
            gfx_relief_ = new wxCheckBox(page, wxID_ANY, "Earth relief and sea glint from orbit (slower)");
            weather_ = new wxCheckBox(page, wxID_ANY, "Weather (wind, rain, cloud and visibility)");
            weather_live_ = new wxCheckBox(page, wxID_ANY, "Real weather for Earth (NOAA METARs, downloaded at most hourly)");
            weather_rain_ = new wxCheckBox(page, wxID_ANY, "Draw rain");
            weather_preset_ = new wxChoice(page, wxID_ANY);
            for (const char *s : {"Automatic (simulated or real)", "Clear skies", "Scattered cloud", "Overcast", "Rain", "Fog", "Thunderstorm", "Custom METAR"})
                weather_preset_->Append(s);
            weather_metar_ = new wxTextCtrl(page, wxID_ANY);
            weather_metar_->SetHint("e.g. KOSM 011200Z 27015G25KT 3SM TS BKN010CB 15/12 Q1005");
            gfx_terrain_ = new wxChoice(page, wxID_ANY);
            for (const char *s : {"Ultra", "High", "Medium", "Low"})
                gfx_terrain_->Append(s);
            gfx_radius_ = new wxSlider(page, wxID_ANY, 8000, 500, 20000, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL | wxSL_LABELS);
            gfx_fps_ = new wxSlider(page, wxID_ANY, 60, 10, 240, wxDefaultPosition, wxDefaultSize, wxSL_HORIZONTAL | wxSL_LABELS);
            auto row = [&](const wxString &label, wxWindow *w)
            {
                grid->Add(new wxStaticText(page, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
                grid->Add(w, 1, wxEXPAND);
            };
            row("Render style", gfx_style_);
            row("", gfx_clouds_);
            row("", gfx_relief_);
            row("Weather", weather_);
            row("", weather_live_);
            row("", weather_rain_);
            row("Weather preset", weather_preset_);
            row("Custom METAR", weather_metar_);
            row("Terrain detail", gfx_terrain_);
            row("Base light radius (m)", gfx_radius_);
            row("Maximum frame rate", gfx_fps_);
            auto *col = new wxBoxSizer(wxVERTICAL);
            col->Add(grid, 0, wxEXPAND | wxALL, 12);
            col->Add(new wxStaticText(page, wxID_ANY, "For slow or software graphics (such as VNC) choose Flat shaded or Wireframe, switch clouds and weather off and lower the terrain detail and light radius. These are saved in Kosmos's settings and can also be changed in game (F3)."), 0, wxALL, 12);
            page->SetSizer(col);
            book_->AddPage(page, "Graphics");
            ShowGraphics();
            auto sync = [this](wxCommandEvent &) { WriteGraphics(); };
            gfx_style_->Bind(wxEVT_CHOICE, sync);
            gfx_terrain_->Bind(wxEVT_CHOICE, sync);
            gfx_clouds_->Bind(wxEVT_CHECKBOX, sync);
            gfx_relief_->Bind(wxEVT_CHECKBOX, sync);
            weather_->Bind(wxEVT_CHECKBOX, [this, sync](wxCommandEvent &e) { UpdateWeatherEnabled(); sync(e); });
            weather_live_->Bind(wxEVT_CHECKBOX, sync);
            weather_rain_->Bind(wxEVT_CHECKBOX, sync);
            weather_preset_->Bind(wxEVT_CHOICE, [this, sync](wxCommandEvent &e) { UpdateWeatherEnabled(); sync(e); });
            weather_metar_->Bind(wxEVT_TEXT, sync);
            gfx_radius_->Bind(wxEVT_SLIDER, sync);
            gfx_fps_->Bind(wxEVT_SLIDER, sync);
        }

        void ShowGraphics()
        {
            const GraphicsSettings g = load_graphics(SettingsFile());
            loading_ = true;
            gfx_style_->SetSelection(g.style);
            gfx_clouds_->SetValue(g.clouds != 0);
            gfx_relief_->SetValue(g.earth_relief != 0);
            weather_->SetValue(g.weather != 0);
            weather_live_->SetValue(g.weather_live != 0);
            weather_rain_->SetValue(g.weather_rain != 0);
            int preset = 0;
            for (std::size_t i = 0; i < weather_preset_ids().size(); ++i)
                if (g.weather_preset == weather_preset_ids()[i])
                    preset = static_cast<int>(i);
            weather_preset_->SetSelection(preset);
            weather_metar_->ChangeValue(g.weather_metar);
            UpdateWeatherEnabled();
            gfx_terrain_->SetSelection(g.terrain_detail);
            gfx_radius_->SetValue(g.light_radius);
            gfx_fps_->SetValue(g.max_fps);
            loading_ = false;
        }

        void UpdateWeatherEnabled()
        {
            const bool on = weather_->GetValue();
            weather_live_->Enable(on);
            weather_rain_->Enable(on);
            weather_preset_->Enable(on);
            weather_metar_->Enable(on && weather_preset_->GetSelection() == static_cast<int>(weather_preset_ids().size()) - 1);
        }

        static const std::vector<std::string> &weather_preset_ids()
        {
            static const std::vector<std::string> ids = {"auto", "clear", "scattered", "overcast", "rain", "fog", "storm", "custom"};
            return ids;
        }

        void WriteGraphics()
        {
            if (loading_)
                return;
            GraphicsSettings g;
            g.style = std::max(0, gfx_style_->GetSelection());
            g.clouds = gfx_clouds_->GetValue() ? 1 : 0;
            g.earth_relief = gfx_relief_->GetValue() ? 1 : 0;
            g.weather = weather_->GetValue() ? 1 : 0;
            g.weather_live = weather_live_->GetValue() ? 1 : 0;
            g.weather_rain = weather_rain_->GetValue() ? 1 : 0;
            g.weather_preset = weather_preset_ids()[static_cast<std::size_t>(std::max(0, weather_preset_->GetSelection()))];
            g.weather_metar = weather_metar_->GetValue().ToStdString();
            g.terrain_detail = std::max(0, gfx_terrain_->GetSelection());
            g.light_radius = gfx_radius_->GetValue();
            g.max_fps = gfx_fps_->GetValue();
            save_graphics(SettingsFile(), g);
        }

        // ---- Options -------------------------------------------------------------------------------------------
        void BuildOptions()
        {
            auto *page = new wxPanel(book_);
            auto *grid = new wxFlexGridSizer(2, 8, 8);
            grid->AddGrowableCol(1);
            backend_ = new wxChoice(page, wxID_ANY);
            for (const char *b : {"Automatic", "OpenGL", "Vulkan", "Direct3D 12 (Windows)"})
                backend_->Append(b);
            fullscreen_ = new wxCheckBox(page, wxID_ANY, "Start full screen");
            max_texture_ = new wxChoice(page, wxID_ANY);
            for (const char *t : {"Default", "2048", "4096", "8192", "16384"})
                max_texture_->Append(t);
            world_texture_ = new wxChoice(page, wxID_ANY);
            for (const char *t : {"Automatic", "2048", "4096", "8192", "16384"})
                world_texture_->Append(t);
            extra_ = new wxTextCtrl(page, wxID_ANY);
            command_ = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_READONLY);
            auto row = [&](const wxString &label, wxWindow *w)
            {
                grid->Add(new wxStaticText(page, wxID_ANY, label), 0, wxALIGN_CENTER_VERTICAL);
                grid->Add(w, 1, wxEXPAND);
            };
            row("Graphics backend", backend_);
            row("", fullscreen_);
            row("Largest texture", max_texture_);
            row("Invented-world texture", world_texture_);
            row("Extra options", extra_);
            row("Command line", command_);
            auto *col = new wxBoxSizer(wxVERTICAL);
            col->Add(grid, 0, wxEXPAND | wxALL, 12);
            col->Add(new wxStaticText(page, wxID_ANY, "Texture sizes need more graphics memory when larger. If Kosmos glitches or crashes on your GPU, try another backend."), 0, wxALL, 12);
            page->SetSizer(col);
            book_->AddPage(page, "Options");
            auto sync = [this](wxCommandEvent &) { ReadOptions(); };
            backend_->Bind(wxEVT_CHOICE, sync);
            max_texture_->Bind(wxEVT_CHOICE, sync);
            world_texture_->Bind(wxEVT_CHOICE, sync);
            fullscreen_->Bind(wxEVT_CHECKBOX, sync);
            extra_->Bind(wxEVT_TEXT, sync);
        }

        static int texture_index(int v)
        {
            return v == 2048 ? 1 : v == 4096 ? 2 : v == 8192 ? 3 : v == 16384 ? 4 : 0;
        }
        static int texture_value(int i) { return i == 1 ? 2048 : i == 2 ? 4096 : i == 3 ? 8192 : i == 4 ? 16384 : 0; }

        void ShowOptions()
        {
            loading_ = true;
            backend_->SetSelection(profile_.backend == "gl" ? 1 : profile_.backend == "vulkan" ? 2 : profile_.backend == "d3d12" ? 3 : 0);
            fullscreen_->SetValue(profile_.fullscreen);
            max_texture_->SetSelection(texture_index(profile_.max_texture));
            world_texture_->SetSelection(texture_index(profile_.world_texture));
            extra_->SetValue(W(profile_.extra_args));
            loading_ = false;
            RefreshPreview();
        }

        void ReadOptions()
        {
            if (loading_)
                return;
            static const char *names[] = {"auto", "gl", "vulkan", "d3d12"};
            profile_.backend = names[std::max(0, backend_->GetSelection())];
            profile_.fullscreen = fullscreen_->GetValue();
            profile_.max_texture = texture_value(max_texture_->GetSelection());
            profile_.world_texture = texture_value(world_texture_->GetSelection());
            profile_.extra_args = U(extra_->GetValue());
            save_profile(profile_);
            RefreshPreview();
        }

        void RefreshPreview()
        {
            if (!command_)
                return;
            const ScenarioInfo *s = SelectedScenario();
            wxString text = "kosmos";
            for (const std::string &a : build_arguments(profile_, s ? s->id : std::string()))
                text += " " + (a.find(' ') != std::string::npos ? "\"" + W(a) + "\"" : W(a));
            command_->SetValue(text);
        }

        // ---- Updates -------------------------------------------------------------------------------------------
        void BuildUpdates()
        {
            auto *page = new wxPanel(book_);
            auto *col = new wxBoxSizer(wxVERTICAL);
            update_label_ = new wxStaticText(page, wxID_ANY, "");
            update_label_->SetFont(update_label_->GetFont().Bold().Scaled(1.2f));
            notes_ = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxTE_WORDWRAP);
            update_gauge_ = new wxGauge(page, wxID_ANY, 1000);
            auto *check = new wxButton(page, wxID_ANY, "Check for updates");
            update_button_ = new wxButton(page, wxID_ANY, "Download the new version");
            update_button_->Disable();
            auto *buttons = new wxBoxSizer(wxHORIZONTAL);
            buttons->Add(check, 0, wxALL, 8);
            buttons->Add(update_button_, 0, wxALL, 8);
            col->Add(update_label_, 0, wxALL, 12);
            col->Add(notes_, 1, wxEXPAND | wxLEFT | wxRIGHT, 12);
            col->Add(update_gauge_, 0, wxEXPAND | wxALL, 12);
            col->Add(buttons, 0);
            page->SetSizer(col);
            book_->AddPage(page, "Updates");
            check->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { CheckUpdates(); });
            update_button_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { InstallUpdate(); });
            const std::string have = installed_version(root_);
            update_label_->SetLabel(have.empty() ? "Installed version unknown" : W("Installed version " + have));
        }

        void FetchReleasePage(int page, nlohmann::json releases, std::function<void(bool, const std::string &)> done)
        {
            const std::string url = api_url_ + "?per_page=100&page=" + std::to_string(page);
            http_.Fetch(url, [this, page, releases = std::move(releases), done](bool ok, const std::string &body) mutable
                        {
                            if (!ok)
                            {
                                done(false, "GitHub releases request failed: " + body);
                                return;
                            }
                            const auto entries = nlohmann::json::parse(body, nullptr, false);
                            if (!entries.is_array() || entries.size() > 100)
                            {
                                done(false, "Invalid GitHub releases response");
                                return;
                            }
                            for (const auto &entry : entries)
                                releases.push_back(entry);
                            if (entries.size() == 100)
                            {
                                if (page >= 100)
                                    done(false, "Too many GitHub release pages to inspect");
                                else
                                    FetchReleasePage(page + 1, std::move(releases), done);
                            }
                            else
                                done(true, releases.dump());
                        });
        }

        void CheckUpdates()
        {
            if (busy_)
                return;
            busy_ = true;
            latest_.clear();
            release_ = {};
            manifest_ = {};
            packs_remote_.clear();
            update_button_->Disable();
            notes_->Clear();
            update_label_->SetLabel("Checking GitHub releases...");
            RefreshPacks();
            auto fail = [this](const std::string &error)
            {
                busy_ = false;
                update_label_->SetLabel(W(error));
                SetStatusText(W(error));
                RefreshPacks();
                if (self_check_)
                {
                    std::printf("GitHub release ERROR: %s\n", error.c_str());
                    Close();
                }
            };
            FetchReleasePage(1, nlohmann::json::array(), [this, fail](bool ok, const std::string &body)
                             {
                                 std::string error;
                                 GitHubRelease selected;
                                 if (!ok || !select_github_release(body, selected, error))
                                 {
                                     fail(ok ? error : body);
                                     return;
                                 }
                                 selected.manifest_url = asset_url_ + "/" + selected.tag + "/manifest.json";
                                 http_.Fetch(selected.manifest_url, [this, fail, selected](bool ok2, const std::string &text)
                                             {
                                                 ReleaseManifest manifest;
                                                 std::string error;
                                                 if (!ok2 || !parse_release_manifest(text, manifest, error) || manifest.version != selected.version)
                                                 {
                                                     fail(!ok2 ? "Cannot download release manifest: " + text :
                                                          error.empty() ? "Release tag and manifest version differ" : error);
                                                     return;
                                                 }
                                                 release_ = selected;
                                                 manifest_ = std::move(manifest);
                                                 latest_ = release_.version;
                                                 for (const SplitPack &pack : manifest_.packs)
                                                     packs_remote_.push_back(pack.pack);
                                                 DownloadAsset(manifest_.notes, update_gauge_, [this](bool notes_ok, const std::string &notes_error)
                                                               {
                                                                   notes_->SetValue(notes_ok ? W(read_text(DownloadDir() / manifest_.notes.file)) : W("Could not download verified release notes: " + notes_error));
                                                                   busy_ = false;
                                                                   const std::string have = installed_version(root_);
                                                                   const bool newer = have.empty() || version_newer(latest_, have);
                                                                   update_label_->SetLabel(W(newer ? "Version " + latest_ + " is available (you have " + (have.empty() ? "an unknown version" : have) + ")" : "You are up to date (" + have + ")"));
                                                                   update_button_->Enable(newer);
                                                                   SetStatusText(notes_ok ? "GitHub release information loaded." : W("Release notes error: " + notes_error));
                                                                   RefreshPacks();
                                                                   if (self_check_)
                                                                   {
                                                                       std::printf("GitHub version: %s, installed: %s; packs: %zu\n", latest_.c_str(), have.c_str(), packs_.size());
                                                                       if (!notes_ok)
                                                                           std::printf("Release notes ERROR: %s\n", notes_error.c_str());
                                                                       if (self_check_pack_ && notes_ok && !packs_.empty())
                                                                           InstallPack();
                                                                       else
                                                                           Close();
                                                                   }
                                                               });
                                             });
                             });
        }

        void InstallUpdate()
        {
            if (latest_.empty() || busy_)
                return;
            // Unpack beside the current install so the running copy is never overwritten.
            const fs::path dest = root_.parent_path() / ("kosmos-" + latest_);
            update_button_->Disable();
            busy_ = true;
            ShowPack();
#ifdef _WIN32
            const ReleaseAsset archive = manifest_.windows_archive;
            const fs::path install = dest / ("kosmos-beta-" + latest_ + "-Windows");
#else
            const ReleaseAsset archive = manifest_.linux_archive;
            const fs::path install = dest / ("kosmos-beta-" + latest_ + "-Linux");
#endif
            DownloadAsset(archive, update_gauge_, [this, dest, install, archive](bool ok, const std::string &msg)
                            {
                                std::error_code ec;
                                if (ok)
                                    fs::create_directories(dest, ec);
                                const bool unpacked = ok && !ec && Extract(DownloadDir() / archive.file, dest);
                                busy_ = false;
                                ShowPack();
                                if (unpacked)
                                {
                                    update_label_->SetLabel(W("Version " + latest_ + " unpacked to " + install.string()));
                                    wxMessageBox("Version " + W(latest_) + " is in\n" + Wp(install) + "\n\nYour current install was not changed. Start the new launcher from that folder.", "Update ready");
                                }
                                else
                                {
                                    SetStatusText(W("Update failed: " + (!ok ? msg : ec ? ec.message() : "Could not unpack the archive")));
                                    update_button_->Enable();
                                }
                            });
        }

        // ---- Logs ----------------------------------------------------------------------------------------------
        void BuildLogs()
        {
            auto *page = new wxPanel(book_);
            auto *col = new wxBoxSizer(wxVERTICAL);
            log_choice_ = new wxChoice(page, wxID_ANY);
            log_text_ = new wxTextCtrl(page, wxID_ANY, "", wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxTE_READONLY | wxHSCROLL);
            log_text_->SetFont(wxFont(wxFontInfo(10).Family(wxFONTFAMILY_TELETYPE)));
            auto *copy = new wxButton(page, wxID_ANY, "Copy to clipboard");
            retry_ = new wxButton(page, wxID_ANY, "Retry with another backend");
            retry_->Hide();
            report_ = new wxButton(page, wxID_ANY, "Report this log...");
            auto *buttons = new wxBoxSizer(wxHORIZONTAL);
            buttons->Add(copy, 0, wxALL, 8);
            buttons->Add(report_, 0, wxALL, 8);
            buttons->Add(retry_, 0, wxALL, 8);
            col->Add(log_choice_, 0, wxEXPAND | wxALL, 8);
            col->Add(log_text_, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
            col->Add(buttons, 0);
            page->SetSizer(col);
            book_->AddPage(page, "Logs");
            log_choice_->Bind(wxEVT_CHOICE, [this](wxCommandEvent &) { ShowLog(); });
            copy->Bind(wxEVT_BUTTON, [this](wxCommandEvent &)
                       {
                           if (wxTheClipboard->Open())
                           {
                               wxTheClipboard->SetData(new wxTextDataObject(log_text_->GetValue()));
                               wxTheClipboard->Close();
                           }
                       });
            report_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &) { ReportLog(); });
            retry_->Bind(wxEVT_BUTTON, [this](wxCommandEvent &)
                         {
                             profile_.backend = profile_.backend == "vulkan" ? "gl" : profile_.backend == "gl" ? "vulkan" : "gl";
                             save_profile(profile_);
                             ShowOptions();
                             Launch(last_run_scenario_);
                         });
            RefreshLogList();
        }

        fs::path log_dir() const { return user_dir() / "logs"; }

        // The user reviews the report and submits it themselves in a browser.
        void ReportLog()
        {
            const int i = log_choice_->GetSelection();
            if (i == wxNOT_FOUND)
            {
                wxMessageBox("There is no log to report yet. Run Kosmos first.", "Report", wxICON_INFORMATION);
                return;
            }
            const std::string home = U(wxGetHomeDir());
            const std::string raw = read_text(log_files_[i]);
            std::string report = "What happened?\n\n\nKosmos: " + installed_version(root_) +
                                 "\nScenario: " + log_files_[i].stem().string() +
                                 "\nExit code: " + log_exit_code(raw) +
                                 "\nSystem: " + U(wxGetOsDescription()) + " " + U(wxPlatformInfo::Get().GetBitnessName()) +
                                 "\nBackend setting: " + profile_.backend + "\n";
            std::string options = "Command line options: ";
            for (const std::string &a : build_arguments(profile_, ""))
                options += a + " ";
            report += scrub_paths(options, home) + "\n\nLog:\n" + log_tail(scrub_paths(raw, home), 400000);

            wxDialog dlg(this, wxID_ANY, "Report this log", wxDefaultPosition, wxSize(720, 640), wxDEFAULT_DIALOG_STYLE | wxRESIZE_BORDER);
            auto *col = new wxBoxSizer(wxVERTICAL);
            col->Add(new wxStaticText(&dlg, wxID_ANY, "Edit and review this report before sharing. Paths are scrubbed where possible,\nbut logs may contain other private information. Nothing is uploaded automatically."), 0, wxALL, 8);
            const wxString report_text(report.c_str(), wxMBConvUTF8(wxMBConvUTF8::MAP_INVALID_UTF8_TO_OCTAL), report.size());
            auto *preview = new wxTextCtrl(&dlg, wxID_ANY, report_text, wxDefaultPosition, wxDefaultSize, wxTE_MULTILINE | wxHSCROLL);
            preview->SetFont(wxFont(wxFontInfo(9).Family(wxFONTFAMILY_TELETYPE)));
            col->Add(preview, 1, wxEXPAND | wxLEFT | wxRIGHT, 8);
            col->Add(new wxStaticText(&dlg, wxID_ANY, "Copy the report, then paste it into a GitHub issue. Very long logs may need shortening."), 0, wxALL, 8);
            col->Add(dlg.CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxEXPAND | wxALL, 8);
            if (auto *ok = dlg.FindWindow(wxID_OK))
                static_cast<wxButton *>(ok)->SetLabel("Copy and open GitHub");
            dlg.SetSizer(col);
            for (;;)
            {
                if (dlg.ShowModal() != wxID_OK)
                    return;
                if (!wxTheClipboard->Open())
                {
                    wxMessageBox("Could not open the clipboard. The report was not copied.", "Report", wxICON_ERROR, &dlg);
                    continue;
                }
                const bool copied = wxTheClipboard->SetData(new wxTextDataObject(preview->GetValue()));
                wxTheClipboard->Close();
                if (copied)
                    break;
                wxMessageBox("Could not copy the report to the clipboard.", "Report", wxICON_ERROR, &dlg);
            }
            if (!wxLaunchDefaultBrowser("https://github.com/daver64/Kosmos/issues/new"))
                wxMessageBox("The report was copied, but the browser could not be opened.\nVisit https://github.com/daver64/Kosmos/issues/new", "Report", wxICON_WARNING);
            else
                SetStatusText("Report copied. Paste and submit it in GitHub if you choose.");
        }

        void RefreshLogList()
        {
            log_files_.clear();
            std::error_code ec;
            for (const fs::directory_entry &e : fs::directory_iterator(log_dir(), ec))
                if (e.path().extension() == ".log")
                    log_files_.push_back(e.path());
            std::sort(log_files_.rbegin(), log_files_.rend());
            if (log_files_.size() > 20)
            {
                for (size_t i = 20; i < log_files_.size(); ++i)
                    fs::remove(log_files_[i], ec);
                log_files_.resize(20);
            }
            log_choice_->Clear();
            for (const fs::path &f : log_files_)
                log_choice_->Append(Wp(f.stem()));
            if (!log_files_.empty())
            {
                log_choice_->SetSelection(0);
                ShowLog();
            }
        }

        void ShowLog()
        {
            const int i = log_choice_->GetSelection();
            if (i != wxNOT_FOUND)
                log_text_->SetValue(W(read_text(log_files_[i])));
        }

        // ---- Running Kosmos ------------------------------------------------------------------------------------
        void LaunchSelected()
        {
            if (const ScenarioInfo *s = SelectedScenario())
            {
                std::error_code ec;
                if (!fs::is_regular_file(s->file, ec))
                {
                    Rescan();
                    wxMessageBox("This scenario is no longer available. The scenario list has been refreshed.", "Launch", wxICON_WARNING);
                    return;
                }
                Launch(s->id);
            }
        }

        void LaunchMission()
        {
            const fs::path exe = find_addon_program(root_, "kosmosmission", "kosmosmission");
            if (exe.empty())
            {
                wxMessageBox("KosmosMission was not found in addons/kosmosmission/bin.", "KosmosMission", wxICON_ERROR);
                return;
            }
            wxExecuteEnv env;
            env.cwd = Wp(exe.parent_path());
            wxGetEnvMap(&env.env);
            env.env["KOSMOS_ROOT"] = Wp(root_);
            if (wxExecute("\"" + Wp(exe) + "\"", wxEXEC_ASYNC, nullptr, &env) == 0)
                wxMessageBox("KosmosMission could not be started.", "KosmosMission", wxICON_ERROR);
        }

        void Launch(const std::string &scenario)
        {
            if (run_)
                return;
            const fs::path exe = find_kosmos(root_);
            if (exe.empty())
            {
                wxMessageBox("Kosmos was not found. Put the launcher in the Kosmos folder.", "Launch", wxICON_ERROR);
                return;
            }
            std::string args;
            for (const std::string &a : build_arguments(profile_, scenario))
                args += " \"" + a + "\"";
            if (scenario != "last_session")
                profile_.last_scenario = scenario;
            last_run_scenario_ = scenario;
            save_profile(profile_);
            resume_->Enable();
            wxExecuteEnv env;
            env.cwd = Wp(exe.parent_path());
            run_ = new Run(this);
            run_->on_end = [this](int code) { Ended(code); };
            started_ = wxDateTime::Now();
            const long pid = wxExecute("\"" + Wp(exe) + "\"" + W(args), wxEXEC_ASYNC | wxEXEC_SHOW_CONSOLE, run_, &env);
            if (!pid)
            {
                delete run_;
                run_ = nullptr;
                wxMessageBox("Could not start Kosmos.", "Launch", wxICON_ERROR);
                return;
            }
            timer_.Start(150);
            launch_->Disable();
            SetStatusText("Kosmos is running.");
        }

        void Ended(int code)
        {
            timer_.Stop();
            std::error_code ec;
            fs::create_directories(log_dir(), ec);
            const wxString name = started_.Format("%Y%m%d-%H%M%S") + "-" + W(last_run_scenario_.empty() ? "default" : last_run_scenario_);
            std::ofstream out(log_dir() / (U(name) + ".log"));
            out << "Exit code " << code << "\n"
                << "Command: kosmos";
            for (const std::string &a : build_arguments(profile_, last_run_scenario_))
                out << " " << a;
            out << "\n\n" << U(run_->output);
            out.close();
            // A normal quit returns 0; anything else, or a very short run, is worth showing.
            const bool failed = code != 0 || (wxDateTime::Now() - started_).GetSeconds() < 3;
            run_->Detach();
            run_ = nullptr;
            launch_->Enable();
            ShowScenario();
            RefreshLogList();
            retry_->Show(failed);
            report_->SetLabel(failed ? "Report this problem..." : "Report this log...");
            log_text_->GetParent()->Layout();
            SetStatusText(failed ? "Kosmos ended unexpectedly; see the Logs page." : "Kosmos closed.");
            if (failed)
                book_->SetSelection(5);
        }

        void OnClose(wxCloseEvent &e)
        {
            if (run_)
                run_->Detach(); // Kosmos keeps running; the launcher just stops watching it
            e.Skip();
        }

        void Rescan()
        {
            addons_ = scan_addons(root_);
            scenarios_ = scan_scenarios(root_, addons_);
            addon_list_->Clear();
            for (const AddonInfo &a : addons_)
                addon_list_->Append(W(a.name));
            for (size_t i = 0; i < addons_.size(); ++i)
                addon_list_->Check(i, addons_[i].enabled);
            if (!addons_.empty())
            {
                addon_list_->SetSelection(0);
                ShowAddon();
            }
            FillTree();
            RefreshPacks();
            ShowOptions();
        }

        fs::path root_;
        std::string api_url_, asset_url_;
        Profile profile_;
        std::vector<ScenarioInfo> scenarios_;
        std::vector<AddonInfo> addons_;
        std::vector<PackInfo> packs_, packs_remote_;
        std::vector<fs::path> log_files_;
        std::string latest_, last_run_scenario_;
        bool busy_ = false, loading_ = false, self_check_ = false, self_check_pack_ = false;
        GitHubRelease release_;
        ReleaseManifest manifest_;
        Http http_;
        Run *run_ = nullptr;
        wxDateTime started_;
        wxTimer timer_;

        wxListbook *book_ = nullptr;
        wxTreeCtrl *tree_ = nullptr;
        wxStaticBitmap *thumb_ = nullptr, *addon_thumb_ = nullptr;
        wxString scenario_text_[3];
        wxStaticText *title_ = nullptr, *detail_ = nullptr, *blurb_ = nullptr, *addon_title_ = nullptr, *pack_text_ = nullptr, *update_label_ = nullptr;
        wxButton *launch_ = nullptr, *resume_ = nullptr, *mission_ = nullptr, *pack_install_ = nullptr, *pack_remove_ = nullptr, *update_button_ = nullptr, *retry_ = nullptr, *report_ = nullptr;
        wxCheckListBox *addon_list_ = nullptr;
        wxTextCtrl *addon_text_ = nullptr, *extra_ = nullptr, *command_ = nullptr, *notes_ = nullptr, *log_text_ = nullptr;
        wxListBox *pack_list_ = nullptr;
        wxGauge *pack_gauge_ = nullptr, *update_gauge_ = nullptr;
        wxChoice *backend_ = nullptr, *max_texture_ = nullptr, *world_texture_ = nullptr, *log_choice_ = nullptr;
        wxCheckBox *fullscreen_ = nullptr, *gfx_clouds_ = nullptr, *gfx_relief_ = nullptr, *weather_ = nullptr, *weather_live_ = nullptr, *weather_rain_ = nullptr;
        wxChoice *weather_preset_ = nullptr;
        wxTextCtrl *weather_metar_ = nullptr;
        wxChoice *gfx_style_ = nullptr, *gfx_terrain_ = nullptr;
        wxSlider *gfx_radius_ = nullptr, *gfx_fps_ = nullptr;
    };

    class App : public wxApp
    {
    public:
        bool OnInit() override
        {
            wxInitAllImageHandlers();
            if (argc == 4 && wxString(argv[1]) == "--check-http")
            {
                auto *frame = new wxFrame(nullptr, wxID_ANY, "HTTP check");
                SetTopWindow(frame);
                const std::string url = U(argv[2]);
                const fs::path file = P(argv[3]);
                CallAfter([this, frame, url, file]
                          {
                              test_http_.Download(url, file, {}, [frame](bool ok, const std::string &error)
                                                  {
                                                      std::printf("HTTP %s%s%s\n", ok ? "OK" : "ERROR", error.empty() ? "" : ": ", error.c_str());
                                                      frame->Close();
                                                  });
                          });
                return true;
            }
            const fs::path here = P(wxFileName(wxStandardPaths::Get().GetExecutablePath()).GetPath());
            const bool check_pack = argc == 5 && wxString(argv[1]) == "--check-release-pack";
            const bool check_release = argc == 4 && wxString(argv[1]) == "--check-release";
            auto *frame = check_pack ? new Frame(P(argv[4]), U(argv[2]), U(argv[3])) :
                          check_release ? new Frame(find_root(here), U(argv[2]), U(argv[3])) : new Frame(find_root(here));
            if (check_pack)
                frame->SelfCheckPack();
            else if (check_release || (argc > 1 && wxString(argv[1]) == "--check"))
                frame->SelfCheck();
            else
            {
                frame->Show();
                frame->StartDiscovery();
            }
            return true;
        }

    private:
        Http test_http_;
    };
}

wxIMPLEMENT_APP(App);
