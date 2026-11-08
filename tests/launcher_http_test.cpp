#define KOSMOS_LAUNCHER_HTTP_TEST
#include "../launcher/main.cpp"

class HttpTestApp : public wxApp
{
public:
    bool OnInit() override
    {
        delete wxLog::SetActiveTarget(new wxLogStderr);
        wxSetAssertHandler([](const wxString &file, int line, const wxString &function,
                              const wxString &condition, const wxString &message) {
            std::fprintf(stderr, "wx assertion %s:%d %s: %s (%s)\n",
                         U(file).c_str(), line, U(function).c_str(), U(condition).c_str(), U(message).c_str());
            std::abort();
        });
        if (argc < 4)
            return false;
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
            frame->pack_list_->SetSelection(0);
            frame->InstallPack();
            frame->pack_list_->SetSelection(1);
            frame->InstallPack();
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
                      [this, mode](long long got, long long) {
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
            pack.url = base_url + (archive ? "/archive" : "/range");
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
        if (phase == 0 && bytes >= 65536)
        {
            wxCommandEvent event(wxEVT_BUTTON);
            frame->pack_pause_->GetEventHandler()->ProcessEvent(event);
            phase = 1;
        }
        else if (phase == 1 && !frame->busy_)
        {
            first_size = bytes;
            if (!frame->pack_paused_ || first_size == 0 ||
                !frame->pack_list_->GetString(0).Contains("[paused / queued]"))
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
    int phase = 0;
};

wxIMPLEMENT_APP(HttpTestApp);
