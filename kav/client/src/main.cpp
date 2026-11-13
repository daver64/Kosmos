// KosmosAV: the pilot's program for the KosmosAV freight and passenger network. A plain GTK 3 program that watches the game over KosmosLink
// (it needs only the read capability), tells the KosmosAV server where the pilot is, shows the job board and reports deliveries.
// One worker thread owns the Agent and the game link; the GTK thread only reads snapshots and queues requests.
#include "agent.h"
#include "../../../sdk/gtk_schema.h"

#include <kosmos_link.hpp>

#include <gtk/gtk.h>
#include <glib/gstdio.h>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <thread>

namespace
{
    using namespace kav;

    // The flown ship and the nearest base, read through the link. Used only on the worker thread.
    class LinkGame : public GameSource
    {
    public:
        GameState read() override
        {
            GameState g;
            if (!link_ && std::time(nullptr) - last_try_ >= 5)
            {
                last_try_ = std::time(nullptr);
                try
                {
                    link_ = std::make_unique<kosmos::Link>("kosmosav", "KosmosAV", KL_CAP_READ, "0.1");
                }
                catch (const std::exception &)
                {
                }
            }
            if (!link_)
                return g;
            if (!link_->poll(0))
            {
                link_.reset();
                return g;
            }
            for (const kl_ship_info &s : link_->ships(0.0, false))
            {
                if (!(s.flags & KL_SHIP_FLOWN))
                    continue;
                g.connected = true;
                g.ship_name = link_->get_string("/ships/ship[" + std::to_string(s.index) + "]/class");
                if (g.ship_name.empty())
                    g.ship_name = s.name;
                g.landed = (s.flags & KL_SHIP_LANDED) != 0;
                if (g.landed)
                {
                    auto near = link_->facilities(s.latitude, s.longitude, 15.0, "", 1, s.body);
                    if (!near.empty())
                        g.base_id = near[0].id;
                }
            }
            return g;
        }

        void notify(const std::string &text) override
        {
            if (link_)
                link_->notify(text, 8.0);
        }

    private:
        std::unique_ptr<kosmos::Link> link_;
        std::time_t last_try_ = 0;
    };

    struct Snapshot
    {
        Me me;
        GameState game;
        std::string status;
        std::vector<Job> board;
        std::string board_from;
        std::vector<std::string> notes;
        bool registry_ok = false;
    };

    struct App
    {
        Config cfg;
        LinkGame game;
        std::unique_ptr<Agent> agent;
        std::thread worker;
        std::atomic<bool> quit{false};
        std::mutex mu;
        std::condition_variable cv;
        std::deque<std::function<void()>> commands;
        Snapshot snap;
        std::string want_board = "here"; // "here", "all" or a location id
        bool board_dirty = true;
        GtkWidget *window = nullptr, *summary = nullptr, *board_view = nullptr, *mine_view = nullptr, *ship_combo = nullptr, *board_combo = nullptr, *log_view = nullptr;
        GtkListStore *board_store = nullptr, *mine_store = nullptr;
        std::vector<ShipType> shown_ships;
        std::vector<std::string> shown_locations;
        bool registry_shown = false;
    };

    std::string config_path()
    {
        return std::string(g_get_user_config_dir()) + "/kosmosav/config.ini";
    }

    void load_config(Config &c)
    {
        GKeyFile *k = g_key_file_new();
        if (g_key_file_load_from_file(k, config_path().c_str(), G_KEY_FILE_NONE, nullptr))
        {
            if (char *s = g_key_file_get_string(k, "server", "url", nullptr)) { c.server = s; g_free(s); }
            if (char *s = g_key_file_get_string(k, "server", "key", nullptr)) { c.key = s; g_free(s); }
        }
        g_key_file_free(k);
    }

    void save_config(const Config &c)
    {
        GKeyFile *k = g_key_file_new();
        g_key_file_set_string(k, "server", "url", c.server.c_str());
        g_key_file_set_string(k, "server", "key", c.key.c_str());
        gchar *dir = g_path_get_dirname(config_path().c_str());
        g_mkdir_with_parents(dir, 0700);
        g_free(dir);
        g_key_file_save_to_file(k, config_path().c_str(), nullptr);
        g_chmod(config_path().c_str(), 0600); // it holds the API key
        g_key_file_free(k);
    }

    void post(App &a, std::function<void()> fn)
    {
        {
            std::lock_guard<std::mutex> lock(a.mu);
            a.commands.push_back(std::move(fn));
        }
        a.cv.notify_one();
    }

    void note(App &a, const std::string &text)
    {
        std::lock_guard<std::mutex> lock(a.mu);
        a.snap.notes.push_back(text);
    }

    // Everything here runs on the worker thread.
    void refresh_board(App &a)
    {
        std::string from;
        {
            std::lock_guard<std::mutex> lock(a.mu);
            from = a.want_board == "here" ? a.agent->me().location : a.want_board == "all" ? "" : a.want_board;
            a.board_dirty = false;
        }
        std::vector<Job> jobs;
        if (a.agent->fetch_jobs(from, jobs))
        {
            std::lock_guard<std::mutex> lock(a.mu);
            a.snap.board = jobs;
            a.snap.board_from = from;
        }
    }

    void worker_loop(App &a)
    {
        long last_board = 0;
        bool registry = false;
        while (!a.quit)
        {
            std::deque<std::function<void()>> todo;
            {
                std::unique_lock<std::mutex> lock(a.mu);
                a.cv.wait_for(lock, std::chrono::milliseconds(1000), [&] { return a.quit.load() || !a.commands.empty(); });
                todo.swap(a.commands);
            }
            for (auto &c : todo)
                c();
            long now = (long)std::time(nullptr);
            if (a.cfg.server.empty() || a.cfg.key.empty())
            {
                std::lock_guard<std::mutex> lock(a.mu);
                a.snap.status = "Enter the server address and your API key in Settings.";
                continue;
            }
            if (!registry)
                registry = a.agent->load_registry();
            std::vector<std::string> notes;
            if (registry)
            {
                if (a.agent->me().callsign.empty())
                {
                    Me first;
                    a.agent->fetch_me(first);
                }
                notes = a.agent->tick(now);
            }
            bool dirty;
            {
                std::lock_guard<std::mutex> lock(a.mu);
                dirty = a.board_dirty;
            }
            if (registry && (dirty || now - last_board >= 20))
            {
                refresh_board(a);
                last_board = now;
            }
            std::lock_guard<std::mutex> lock(a.mu);
            a.snap.me = a.agent->me();
            a.snap.game = a.agent->game_state();
            a.snap.registry_ok = registry;
            a.snap.status = a.agent->server_ok() ? "" : a.agent->last_error();
            for (auto &n : notes)
                a.snap.notes.push_back(n);
        }
    }

    void append_log(App &a, const std::string &text)
    {
        GtkTextBuffer *b = gtk_text_view_get_buffer(GTK_TEXT_VIEW(a.log_view));
        GtkTextIter end;
        gtk_text_buffer_get_end_iter(b, &end);
        char stamp[16];
        std::time_t t = std::time(nullptr);
        std::strftime(stamp, sizeof stamp, "%H:%M ", std::localtime(&t));
        gtk_text_buffer_insert(b, &end, (std::string(stamp) + text + "\n").c_str(), -1);
        gtk_text_buffer_get_end_iter(b, &end);
        GtkTextMark *m = gtk_text_buffer_create_mark(b, nullptr, &end, FALSE);
        gtk_text_view_scroll_to_mark(GTK_TEXT_VIEW(a.log_view), m, 0, FALSE, 0, 0);
        gtk_text_buffer_delete_mark(b, m);
    }

    std::string days(long deadline)
    {
        long left = deadline - (long)std::time(nullptr);
        if (left < 0)
            return "late";
        return left >= 86400 ? std::to_string(left / 86400) + " d" : std::to_string(left / 3600) + " h";
    }

    std::string load_text(const Job &j) { return j.kind == "pax" ? std::to_string(j.qty) + (j.qty == 1 ? " passenger" : " passengers") : std::to_string(j.qty) + " kg"; }

    void fill_combos(App &a, const Snapshot &s)
    {
        if (a.registry_shown || !s.registry_ok)
            return;
        a.registry_shown = true;
        a.shown_ships = a.agent->ships();
        for (auto &sh : a.shown_ships)
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.ship_combo), (sh.name + " (" + std::to_string(sh.fee) + " cr)").c_str());
        gtk_combo_box_set_active(GTK_COMBO_BOX(a.ship_combo), 0);
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.board_combo), "Here");
        gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.board_combo), "Everywhere");
        a.shown_locations = {"here", "all"};
        for (auto &l : a.agent->locations())
        {
            gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(a.board_combo), l.name.c_str());
            a.shown_locations.push_back(l.id);
        }
        gtk_combo_box_set_active(GTK_COMBO_BOX(a.board_combo), 0);
    }

    // The agent's name tables are written once at start up and are safe to read from here.
    gboolean on_tick(gpointer data)
    {
        App &a = *static_cast<App *>(data);
        Snapshot s;
        {
            std::lock_guard<std::mutex> lock(a.mu);
            s = a.snap;
            a.snap.notes.clear();
        }
        fill_combos(a, s);
        std::string text = s.me.callsign.empty() ? "Not signed in" : s.me.callsign + "   " + std::to_string(s.me.credits) + " credits   at " + (s.registry_ok ? a.agent->location_name(s.me.location) : s.me.location);
        if (!s.me.ship.empty())
            text += "   renting " + a.agent->ship_name(s.me.ship);
        text += "\nGame: ";
        text += !s.game.connected ? "not running (or no ship flown)" : s.game.ship_name + (s.game.landed ? (s.game.base_id.empty() ? ", landed away from a base" : ", landed at " + a.agent->location_name(s.game.base_id)) : ", flying");
        if (!s.status.empty())
            text += "\n" + s.status;
        gtk_label_set_text(GTK_LABEL(a.summary), text.c_str());

        gtk_list_store_clear(a.board_store);
        for (auto &j : s.board)
        {
            GtkTreeIter it;
            gtk_list_store_append(a.board_store, &it);
            gtk_list_store_set(a.board_store, &it, 0, (gint)j.id, 1, a.agent->location_name(j.from).c_str(), 2, a.agent->location_name(j.to).c_str(), 3, load_text(j).c_str(),
                               4, (gint)j.pay, 5, (std::to_string((int)(j.dv * 10 + 0.5) / 10.0).substr(0, 4) + " km/s").c_str(), 6, days(j.deadline).c_str(), -1);
        }
        gtk_list_store_clear(a.mine_store);
        for (auto &j : s.me.jobs)
        {
            GtkTreeIter it;
            gtk_list_store_append(a.mine_store, &it);
            gtk_list_store_set(a.mine_store, &it, 0, (gint)j.id, 1, a.agent->location_name(j.from).c_str(), 2, a.agent->location_name(j.to).c_str(), 3, load_text(j).c_str(),
                               4, (gint)j.pay, 5, "", 6, days(j.deadline).c_str(), -1);
        }
        for (auto &n : s.notes)
            append_log(a, n);
        return G_SOURCE_CONTINUE;
    }

    long selected_id(GtkWidget *view)
    {
        GtkTreeModel *model;
        GtkTreeIter it;
        if (!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)), &model, &it))
            return 0;
        gint id = 0;
        gtk_tree_model_get(model, &it, 0, &id, -1);
        return id;
    }

    void on_take(GtkButton *, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        long id = selected_id(a.board_view);
        int ship = gtk_combo_box_get_active(GTK_COMBO_BOX(a.ship_combo));
        if (!id || ship < 0 || ship >= (int)a.shown_ships.size())
        {
            append_log(a, "Select a job and a ship first.");
            return;
        }
        std::string ship_id = a.shown_ships[(size_t)ship].id;
        post(a, [&a, id, ship_id] {
            std::string err = a.agent->accept(id, ship_id);
            note(a, err.empty() ? "Took job " + std::to_string(id) + "." : "Cannot take job " + std::to_string(id) + ": " + err);
            Me m;
            a.agent->fetch_me(m);
            std::lock_guard<std::mutex> lock(a.mu);
            a.board_dirty = true;
        });
        a.cv.notify_one();
    }

    void on_give_back(GtkButton *, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        long id = selected_id(a.mine_view);
        if (!id)
            return;
        post(a, [&a, id] {
            std::string err = a.agent->abandon(id);
            note(a, err.empty() ? "Gave back job " + std::to_string(id) + "." : err);
            Me m;
            a.agent->fetch_me(m);
            std::lock_guard<std::mutex> lock(a.mu);
            a.board_dirty = true;
        });
    }

    void on_board_changed(GtkComboBox *box, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        int i = gtk_combo_box_get_active(box);
        if (i < 0 || i >= (int)a.shown_locations.size())
            return;
        std::lock_guard<std::mutex> lock(a.mu);
        a.want_board = a.shown_locations[(size_t)i];
        a.board_dirty = true;
        a.cv.notify_one();
    }

    void on_settings(GtkButton *, gpointer data)
    {
        App &a = *static_cast<App *>(data);
        GtkWidget *d = gtk_dialog_new_with_buttons("KosmosAV settings", GTK_WINDOW(a.window), GTK_DIALOG_MODAL, "Cancel", GTK_RESPONSE_CANCEL, "Save", GTK_RESPONSE_OK, nullptr);
        GtkWidget *grid = gtk_grid_new();
        gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
        gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
        gtk_container_set_border_width(GTK_CONTAINER(grid), 12);
        GtkWidget *url = gtk_entry_new(), *key = gtk_entry_new();
        gtk_entry_set_text(GTK_ENTRY(url), a.cfg.server.c_str());
        gtk_entry_set_text(GTK_ENTRY(key), a.cfg.key.c_str());
        gtk_entry_set_width_chars(GTK_ENTRY(url), 48);
        gtk_entry_set_visibility(GTK_ENTRY(key), FALSE);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Server"), 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), url, 1, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("API key"), 0, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), key, 1, 1, 1, 1);
        gtk_grid_attach(GTK_GRID(grid), gtk_label_new("The address ends in /kosmos/kav/app. Your API key is on the website's account page."), 0, 2, 2, 1);
        gtk_container_add(GTK_CONTAINER(gtk_dialog_get_content_area(GTK_DIALOG(d))), grid);
        gtk_widget_show_all(d);
        if (gtk_dialog_run(GTK_DIALOG(d)) == GTK_RESPONSE_OK)
        {
            Config c{gtk_entry_get_text(GTK_ENTRY(url)), gtk_entry_get_text(GTK_ENTRY(key))};
            save_config(c);
            append_log(a, "Settings saved. Restart KosmosAV to use them.");
        }
        gtk_widget_destroy(d);
    }

    GtkWidget *make_view(GtkListStore *store, bool pay_column_dv)
    {
        GtkWidget *view = gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));
        const char *titles[] = {"Job", "From", "To", "Load", "Pay", "Delta-v", "Due"};
        for (int i = 0; i < 7; ++i)
        {
            if (i == 5 && !pay_column_dv)
                continue;
            GtkCellRenderer *r = gtk_cell_renderer_text_new();
            GtkTreeViewColumn *c = gtk_tree_view_column_new_with_attributes(titles[i], r, "text", i, nullptr);
            gtk_tree_view_append_column(GTK_TREE_VIEW(view), c);
        }
        return view;
    }

    void build_window(App &a)
    {
        a.window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
        gtk_window_set_default_size(GTK_WINDOW(a.window), 820, 560);
        GtkWidget *header = gtk_header_bar_new();
        gtk_header_bar_set_show_close_button(GTK_HEADER_BAR(header), TRUE);
        gtk_header_bar_set_title(GTK_HEADER_BAR(header), "KosmosAV");
        GtkWidget *settings = gtk_button_new_with_label("Settings");
        g_signal_connect(settings, "clicked", G_CALLBACK(on_settings), &a);
        gtk_header_bar_pack_start(GTK_HEADER_BAR(header), settings);
        gtk_window_set_titlebar(GTK_WINDOW(a.window), header);

        GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        gtk_container_set_border_width(GTK_CONTAINER(box), 10);
        a.summary = gtk_label_new("Connecting...");
        gtk_label_set_xalign(GTK_LABEL(a.summary), 0);
        gtk_box_pack_start(GTK_BOX(box), a.summary, FALSE, FALSE, 0);

        a.board_store = gtk_list_store_new(7, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
        a.mine_store = gtk_list_store_new(7, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);
        a.board_view = make_view(a.board_store, true);
        a.mine_view = make_view(a.mine_store, false);

        GtkWidget *notebook = gtk_notebook_new();
        GtkWidget *board = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *bar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
        a.board_combo = gtk_combo_box_text_new();
        a.ship_combo = gtk_combo_box_text_new();
        GtkWidget *take = gtk_button_new_with_label("Take job");
        gtk_box_pack_start(GTK_BOX(bar), gtk_label_new("Jobs from"), FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(bar), a.board_combo, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(bar), take, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(bar), a.ship_combo, FALSE, FALSE, 0);
        gtk_box_pack_end(GTK_BOX(bar), gtk_label_new("Rent"), FALSE, FALSE, 0);
        GtkWidget *scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(scroll), a.board_view);
        gtk_box_pack_start(GTK_BOX(board), bar, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(board), scroll, TRUE, TRUE, 0);
        g_signal_connect(take, "clicked", G_CALLBACK(on_take), &a);
        g_signal_connect(a.board_combo, "changed", G_CALLBACK(on_board_changed), &a);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), board, gtk_label_new("Job board"));

        GtkWidget *mine = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
        GtkWidget *give = gtk_button_new_with_label("Give back selected job");
        g_signal_connect(give, "clicked", G_CALLBACK(on_give_back), &a);
        GtkWidget *scroll2 = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_container_add(GTK_CONTAINER(scroll2), a.mine_view);
        gtk_box_pack_start(GTK_BOX(mine), scroll2, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(mine), give, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(mine), gtk_label_new("Land at the destination base in the rented ship and the delivery is reported for you."), FALSE, FALSE, 0);
        gtk_notebook_append_page(GTK_NOTEBOOK(notebook), mine, gtk_label_new("My jobs"));
        gtk_box_pack_start(GTK_BOX(box), notebook, TRUE, TRUE, 0);

        a.log_view = gtk_text_view_new();
        gtk_text_view_set_editable(GTK_TEXT_VIEW(a.log_view), FALSE);
        GtkWidget *scroll3 = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_widget_set_size_request(scroll3, -1, 90);
        gtk_container_add(GTK_CONTAINER(scroll3), a.log_view);
        gtk_box_pack_start(GTK_BOX(box), scroll3, FALSE, FALSE, 0);
        gtk_container_add(GTK_CONTAINER(a.window), box);
        g_signal_connect(a.window, "destroy", G_CALLBACK(gtk_main_quit), nullptr);
        gtk_widget_show_all(a.window);
    }
}

int main(int argc, char **argv)
{
    App app;
    load_config(app.cfg);
    for (int i = 1; i + 1 < argc; i += 2)
    {
        if (!std::strcmp(argv[i], "--server")) app.cfg.server = argv[i + 1];
        else if (!std::strcmp(argv[i], "--key")) app.cfg.key = argv[i + 1];
    }
    kosmos_gtk_schemas("kosmosav");
    gtk_init(&argc, &argv);
    app.agent = std::make_unique<Agent>(app.cfg, &app.game);
    build_window(app);
    app.worker = std::thread(worker_loop, std::ref(app));
    g_timeout_add(500, on_tick, &app);
    gtk_main();
    app.quit = true;
    app.cv.notify_all();
    app.worker.join();
    return 0;
}
