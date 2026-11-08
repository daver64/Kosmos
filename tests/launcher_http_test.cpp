#define KOSMOS_LAUNCHER_HTTP_TEST
#include "../launcher/main.cpp"

class HttpTestApp : public wxApp
{
public:
    bool OnInit() override
    {
        delete wxLog::SetActiveTarget(new wxLogStderr);
        wxInitAllImageHandlers();
        wxSetAssertHandler([](const wxString &file, int line, const wxString &function,
                              const wxString &condition, const wxString &message) {
            std::fprintf(stderr, "wx assertion %s:%d %s: %s (%s)\n",
                         U(file).c_str(), line, U(function).c_str(), U(condition).c_str(), U(message).c_str());
            std::abort();
        });
        if (argc < 4)
            return false;
        if (human_size(0) != "unknown size" || human_size(1) != "1 byte" ||
            human_size(1024) != "1.0 KB" || human_size(516929) != "504.8 KB" ||
            human_size(1LL << 20) != "1 MB" || human_size(1LL << 30) != "1.0 GB")
            return Fail("incorrect download size formatting");
        DownloadProgress progress;
        const auto start = DownloadProgress::Clock::time_point{};
        progress.start(start);
        progress.update(6 * 1024, 10 * 1024, 1024);
        if (!progress.text(start + std::chrono::seconds(1)).Contains("Speed: 1.0 KB/s  |  Remaining: 4s"))
            return Fail("resume speed or remaining time incorrect");
        progress.update(6 * 1024, 0, 1024);
        if (!progress.text(start + std::chrono::seconds(2)).Contains("Remaining: unknown"))
            return Fail("unknown download length has an estimate");
        if (!progress.text(start + std::chrono::seconds(8)).Contains("Speed: 0 bytes/s  |  Remaining: unknown"))
            return Fail("stalled download retained a speed estimate");
        SetExitOnFrameDelete(false);
        const std::string mode = U(argv[3]);
        if (mode == "queue" || mode == "queue-archive")
        {
            if (argc != 5)
                return false;
            root = P(argv[2]);
            base_url = U(argv[1]);
            checksum = U(argv[4]);
            archive = mode == "queue-archive";
            OpenFrame();
            frame->Show();
            frame->Layout();
            if (frame->thumb_->IsShown() || frame->launch_->IsEnabled())
                return Fail("preview or launch shown with no scenario selected");
            wxImage image(480, 270);
            image.SetRGB(wxRect(0, 0, 480, 270), 200, 30, 40);
            const fs::path thumbnail = root / "preview-test.png";
            if (!image.SaveFile(Wp(thumbnail), wxBITMAP_TYPE_PNG))
                return Fail("could not create preview test image");
            ScenarioInfo with_image;
            with_image.id = "with-image";
            with_image.name = "With image";
            with_image.category = "Test folder";
            with_image.thumbnail = thumbnail;
            ScenarioInfo without_image;
            without_image.id = "without-image";
            without_image.name = "Without image";
            without_image.category = "Test folder";
            frame->scenarios_ = {with_image, without_image};
            frame->FillTree();
            wxTreeItemIdValue cookie;
            const wxTreeItemId folder = frame->tree_->GetFirstChild(frame->tree_->GetRootItem(), cookie);
            wxTreeItemIdValue child_cookie;
            const wxTreeItemId first = frame->tree_->GetFirstChild(folder, child_cookie);
            const wxTreeItemId second = frame->tree_->GetNextChild(folder, child_cookie);
            if (!folder.IsOk() || !first.IsOk() || !second.IsOk())
                return Fail("preview test scenario tree missing items");
            frame->tree_->SelectItem(folder);
            if (frame->thumb_->IsShown() || frame->launch_->IsEnabled() ||
                !(frame->thumb_->GetContainingSizer()->GetItem(frame->thumb_)->GetFlag() & wxRESERVE_SPACE_EVEN_IF_HIDDEN))
                return Fail("folder preview is visible or does not reserve space");
            frame->tree_->SelectItem(first);
            wxImage displayed = frame->thumb_->GetBitmap().ConvertToImage();
            if (!frame->thumb_->IsShown() || !frame->launch_->IsEnabled() ||
                displayed.GetRed(0, 0) != 200 || displayed.GetGreen(0, 0) != 30 || displayed.GetBlue(0, 0) != 40)
                return Fail("scenario thumbnail was not shown");
            frame->tree_->SelectItem(folder);
            if (frame->thumb_->IsShown() || frame->launch_->IsEnabled() || !frame->title_->GetLabel().empty())
                return Fail("folder retained scenario preview");
            frame->tree_->SelectItem(second);
            displayed = frame->thumb_->GetBitmap().ConvertToImage();
            const wxImage placeholder = load_thumbnail({}, 480, 270).ConvertToImage();
            if (!frame->thumb_->IsShown() || !frame->launch_->IsEnabled() ||
                !std::equal(displayed.GetData(), displayed.GetData() + 480 * 270 * 3, placeholder.GetData()))
                return Fail("scenario without thumbnail did not show placeholder");
            frame->scenarios_.clear();
            frame->FillTree();
            fs::remove(thumbnail);
            auto *splitter = dynamic_cast<wxSplitterWindow *>(frame->page_list_->GetParent());
            if (!splitter || !splitter->IsSplit() || frame->page_list_->GetCount() != 7 ||
                frame->page_list_->GetString(2) != "Scenery packs" || frame->page_list_->GetString(6) != "Logs")
                return Fail("launcher page selector is missing");
            splitter->SetSashPosition(frame->FromDIP(230));
            if (splitter->GetSashPosition() != frame->FromDIP(230))
                return Fail("launcher page selector is not resizable");
            frame->page_list_->SetSelection(2);
            wxCommandEvent page_event(wxEVT_LISTBOX, frame->page_list_->GetId());
            page_event.SetInt(2);
            frame->page_list_->GetEventHandler()->ProcessEvent(page_event);
            if (frame->book_->GetSelection() != 2)
                return Fail("launcher page selector did not change page");
            frame->book_->GetCurrentPage()->Layout();
            if (frame->pack_progress_text_->GetSize().GetWidth() <= 0 ||
                frame->pack_progress_text_->GetSize().GetHeight() <= 0)
                return Fail("download progress label has no visible space");
            frame->book_->SetSelection(6);
            if (frame->page_list_->GetSelection() != 6)
                return Fail("launcher page selector did not follow page changes");
            frame->book_->SetSelection(2);
            const auto earth_elev_mod = std::find_if(frame->packs_.begin(), frame->packs_.end(),
                [](const PackInfo &p) { return p.id == "orbiter-hires-Earth-Elev_mod"; });
            if (earth_elev_mod == frame->packs_.end())
                return Fail("Earth elevation modifier pack missing");
            frame->pack_list_->SetSelection(static_cast<int>(earth_elev_mod - frame->packs_.begin()));
            frame->ShowPack();
            if (!frame->pack_text_->GetLabel().Contains("Download size: 504.8 KB"))
                return Fail("Earth elevation modifier size displayed incorrectly");
            frame->pack_list_->SetSelection(0);
            frame->InstallPack();
            frame->pack_list_->SetSelection(1);
            frame->InstallPack();
            download_start = DownloadProgress::Clock::now();
            if (frame->pack_queue_.size() != 2 || !frame->busy_ ||
                !frame->pack_list_->GetString(0).Contains("[downloading]") ||
                !frame->pack_list_->GetString(1).Contains("[queued]"))
                return Fail("queue or active statuses not shown");
            timer.SetOwner(this);
            Bind(wxEVT_TIMER, &HttpTestApp::OnTimer, this);
            timer.Start(20);
            return true;
        }
        http.Download(U(argv[1]), P(argv[2]),
                      [this, mode](long long got, long long, long long) {
                          if (mode == "pause" && got >= 65536 && !cancelled)
                          {
                              cancelled = true;
                              http.Cancel();
                          }
                      },
                      [this, mode](bool ok, const std::string &error) {
                          success = mode == "pause" ? cancelled && !ok
                                    : mode == "invalid" ? !ok && error.find("invalid resume range") != std::string::npos
                                                        : ok;
                          if (!success)
                              std::fprintf(stderr, "HTTP test failed: %s\n", error.c_str());
                          ExitMainLoop();
                      });
        return true;
    }

    int OnExit() override { return success ? 0 : 1; }

private:
    void OpenFrame()
    {
        frame = new Frame(root, false);
        frame->profile_.server = base_url;
        for (int i = 1; i <= 2; ++i)
        {
            PackInfo pack;
            pack.id = "test-" + std::to_string(i);
            pack.name = pack.id;
            pack.marker = "data/pack" + std::to_string(i) + (archive ? ".marker" : ".tree");
            pack.file = "pack" + std::to_string(i) + (archive ? ".tar" : ".tree");
            pack.url = base_url + (archive ? "/archive-slow" : "/range-slow");
            pack.sha256 = archive ? "" : checksum;
            pack.size = 256 * 8192;
            pack.tree = !archive;
            frame->packs_remote_.push_back(pack);
        }
        frame->RefreshPacks();
    }

    bool Fail(const char *message)
    {
        std::fprintf(stderr, "Queue test failed: %s\n", message);
        success = false;
        if (frame)
            frame->Destroy();
        ExitMainLoop();
        return false;
    }

    void OnTimer(wxTimerEvent &)
    {
        std::error_code ec;
        const fs::path first = archive ? user_dir() / "downloads/pack1.tar.part" : root / "data/pack1.tree.part";
        const std::uintmax_t bytes = fs::exists(first, ec) ? fs::file_size(first, ec) : 0;
        if (phase == 0 && bytes >= 65536 &&
            DownloadProgress::Clock::now() - download_start >= std::chrono::milliseconds(600))
        {
            frame->ShowDownloadProgress();
            if (!frame->pack_progress_text_->GetLabel().Contains("/s  |  Remaining: ") ||
                frame->pack_progress_text_->GetLabel().Contains("Remaining: unknown"))
            {
                std::fprintf(stderr, "Progress: %s\n", U(frame->pack_progress_text_->GetLabel()).c_str());
                Fail("download speed or remaining time not shown");
                return;
            }
            wxCommandEvent event(wxEVT_BUTTON);
            frame->pack_pause_->GetEventHandler()->ProcessEvent(event);
            phase = 1;
        }
        else if (phase == 1 && !frame->busy_)
        {
            first_size = bytes;
            if (!frame->pack_paused_ || first_size == 0 ||
                !frame->pack_list_->GetString(0).Contains("[paused / queued]") ||
                !frame->pack_progress_text_->GetLabel().empty())
            {
                Fail("pause did not retain partial download");
                return;
            }
            wxCommandEvent event(wxEVT_BUTTON);
            frame->pack_pause_->GetEventHandler()->ProcessEvent(event);
            phase = 2;
        }
        else if (phase == 2 && bytes > first_size + 65536)
        {
            frame->Bind(wxEVT_DESTROY, [this](wxWindowDestroyEvent &event) {
                if (event.GetEventObject() == frame)
                    frame = nullptr;
                event.Skip();
            });
            frame->Close();
            phase = 3;
        }
        else if (phase == 3 && !frame)
        {
            OpenFrame();
            if (!frame->pack_paused_ || frame->pack_queue_.size() != 2 ||
                !frame->pack_list_->GetString(0).Contains("[paused / queued]"))
            {
                Fail("queue did not survive launcher close");
                return;
            }
            wxCommandEvent event(wxEVT_BUTTON);
            frame->pack_pause_->GetEventHandler()->ProcessEvent(event);
            phase = 4;
        }
        else if (phase == 4 && !frame->busy_ && frame->pack_queue_.empty())
        {
            const bool installed = archive
                ? fs::is_regular_file(root / "data/payload.bin") && fs::file_size(root / "data/payload.bin") == 256 * 8192
                : fs::is_regular_file(root / "data/pack1.tree") && fs::is_regular_file(root / "data/pack2.tree") &&
                  sha256_file(root / "data/pack1.tree") == checksum && sha256_file(root / "data/pack2.tree") == checksum;
            if (!installed)
            {
                Fail("queued packs did not install correctly");
                return;
            }
            if (!frame->SavePackQueue({"missing-pack"}))
            {
                Fail("could not save unavailable pack");
                return;
            }
            frame->pack_queue_ = {"missing-pack"};
            frame->pack_paused_ = false;
            frame->RefreshPacks();
            frame->StartNextPack();
            const auto missing = std::find_if(frame->packs_.begin(), frame->packs_.end(),
                                              [](const PackInfo &p) { return p.id == "missing-pack"; });
            if (missing == frame->packs_.end() || !frame->pack_paused_ || frame->busy_)
            {
                Fail("unavailable queued pack not shown");
                return;
            }
            const int index = static_cast<int>(missing - frame->packs_.begin());
            frame->pack_list_->SetSelection(index);
            frame->ShowPack();
            if (!frame->pack_list_->GetString(index).Contains("[paused / queued]") || !frame->pack_dequeue_->IsEnabled())
            {
                Fail("unavailable queued pack cannot be removed");
                return;
            }
            wxCommandEvent event(wxEVT_BUTTON);
            frame->pack_dequeue_->GetEventHandler()->ProcessEvent(event);
            std::vector<std::string> saved;
            std::string error;
            if (!frame->pack_queue_.empty() ||
                !load_pack_queue(root / ".kosmos-pack-queue.json", saved, error) || !saved.empty())
            {
                Fail("unavailable queued pack was not removed");
                return;
            }
            timer.Stop();
            success = true;
            frame->Destroy();
            frame = nullptr;
            ExitMainLoop();
        }
    }

    Http http;
    bool cancelled = false, success = false;
    bool archive = false;
    Frame *frame = nullptr;
    fs::path root;
    std::string base_url, checksum;
    wxTimer timer;
    std::uintmax_t first_size = 0;
    DownloadProgress::Clock::time_point download_start = DownloadProgress::Clock::now();
    int phase = 0;
};

wxIMPLEMENT_APP(HttpTestApp);
