// kosmosavd: the KosmosAV (Kosmos Air Virtual) server. Serves the members' web pages and the heartbeat API on localhost; Apache terminates
// TLS and reverse-proxies /kosmos/kav/app/ to it (see kav/docs/DESIGN.md). Single-threaded, SQLite, no other dependencies except OpenSSL
// (password hashing and random numbers).

#include <sqlite3.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/crypto.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <random>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace
{
    struct Config
    {
        std::string db = "kav.db";
        std::string data = "kav-data"; // uploaded screenshots
        std::string base = "";         // URL prefix as seen by the browser, e.g. /kosmos/kav/app
        std::string site = "/kosmos";  // the static Kosmos site, for the stylesheet and links
        int port = 9480;
        bool secure = false; // set the Secure cookie flag (when served over HTTPS)
        size_t max_body = 6u << 20;
    } cfg;

    sqlite3 *db = nullptr;

    // ---- small helpers ------------------------------------------------------------------------------------------------------------------

    std::string hex(const unsigned char *p, size_t n)
    {
        static const char *d = "0123456789abcdef";
        std::string s;
        for (size_t i = 0; i < n; ++i)
        {
            s += d[p[i] >> 4];
            s += d[p[i] & 15];
        }
        return s;
    }

    std::string random_hex(size_t bytes)
    {
        std::vector<unsigned char> b(bytes);
        if (RAND_bytes(b.data(), (int)bytes) != 1)
        {
            std::fprintf(stderr, "no random numbers\n");
            std::exit(1);
        }
        return hex(b.data(), bytes);
    }

    std::string pbkdf2(const std::string &password, const std::string &salt)
    {
        unsigned char out[32];
        PKCS5_PBKDF2_HMAC(password.c_str(), (int)password.size(), (const unsigned char *)salt.data(), (int)salt.size(), 200000, EVP_sha256(), 32, out);
        return hex(out, 32);
    }

    bool const_eq(const std::string &a, const std::string &b) { return a.size() == b.size() && CRYPTO_memcmp(a.data(), b.data(), a.size()) == 0; }

    std::string esc(const std::string &s)
    {
        std::string o;
        for (char c : s)
        {
            switch (c)
            {
            case '&': o += "&amp;"; break;
            case '<': o += "&lt;"; break;
            case '>': o += "&gt;"; break;
            case '"': o += "&quot;"; break;
            case '\'': o += "&#39;"; break;
            default: o += c;
            }
        }
        return o;
    }

    std::string url_decode(const std::string &s)
    {
        std::string o;
        for (size_t i = 0; i < s.size(); ++i)
        {
            if (s[i] == '+')
                o += ' ';
            else if (s[i] == '%' && i + 2 < s.size() && std::isxdigit((unsigned char)s[i + 1]) && std::isxdigit((unsigned char)s[i + 2]))
            {
                o += (char)std::strtol(s.substr(i + 1, 2).c_str(), nullptr, 16);
                i += 2;
            }
            else
                o += s[i];
        }
        return o;
    }

    std::map<std::string, std::string> parse_form(const std::string &s)
    {
        std::map<std::string, std::string> m;
        size_t i = 0;
        while (i <= s.size())
        {
            size_t e = s.find('&', i);
            if (e == std::string::npos)
                e = s.size();
            std::string kv = s.substr(i, e - i);
            size_t q = kv.find('=');
            if (q != std::string::npos)
                m[url_decode(kv.substr(0, q))] = url_decode(kv.substr(q + 1));
            i = e + 1;
        }
        return m;
    }

    std::string lower(std::string s)
    {
        for (auto &c : s)
            c = (char)std::tolower((unsigned char)c);
        return s;
    }

    long now_s() { return (long)std::time(nullptr); }

    std::string when(long t)
    {
        char b[40];
        std::time_t tt = t;
        std::tm tm;
        gmtime_r(&tt, &tm);
        std::strftime(b, sizeof b, "%Y-%m-%d %H:%M UTC", &tm);
        return b;
    }

    // ---- database -----------------------------------------------------------------------------------------------------------------------

    struct Stmt
    {
        sqlite3_stmt *s = nullptr;
        Stmt(const char *sql, const std::vector<std::string> &args = {})
        {
            if (sqlite3_prepare_v2(db, sql, -1, &s, nullptr) != SQLITE_OK)
            {
                std::fprintf(stderr, "sql error: %s\n  %s\n", sqlite3_errmsg(db), sql);
                s = nullptr;
                return;
            }
            for (size_t i = 0; i < args.size(); ++i)
                sqlite3_bind_text(s, (int)i + 1, args[i].c_str(), -1, SQLITE_TRANSIENT);
        }
        ~Stmt() { sqlite3_finalize(s); }
        bool row() { return s && sqlite3_step(s) == SQLITE_ROW; }
        bool done() { return s && sqlite3_step(s) == SQLITE_DONE; }
        std::string text(int c) { auto p = sqlite3_column_text(s, c); return p ? (const char *)p : ""; }
        long num(int c) { return (long)sqlite3_column_int64(s, c); }
    };

    void exec(const char *sql, const std::vector<std::string> &args = {}) { Stmt st(sql, args); st.done(); }

    long scalar(const char *sql, const std::vector<std::string> &args = {})
    {
        Stmt st(sql, args);
        return st.row() ? st.num(0) : 0;
    }

    void migrate()
    {
        const char *schema[] = {
            "CREATE TABLE IF NOT EXISTS accounts(id INTEGER PRIMARY KEY, callsign TEXT UNIQUE COLLATE NOCASE, email TEXT, salt TEXT, hash TEXT,"
            " api_token TEXT UNIQUE, is_admin INTEGER DEFAULT 0, banned INTEGER DEFAULT 0, created INTEGER, hours REAL DEFAULT 0, deliveries INTEGER DEFAULT 0)",
            "CREATE TABLE IF NOT EXISTS signups(id INTEGER PRIMARY KEY, callsign TEXT, email TEXT, note TEXT, ip TEXT, created INTEGER, status TEXT DEFAULT 'pending')",
            "CREATE TABLE IF NOT EXISTS sessions(token TEXT PRIMARY KEY, account_id INTEGER, csrf TEXT, expires INTEGER)",
            "CREATE TABLE IF NOT EXISTS threads(id INTEGER PRIMARY KEY, title TEXT, account_id INTEGER, created INTEGER, locked INTEGER DEFAULT 0, last_post INTEGER)",
            "CREATE TABLE IF NOT EXISTS posts(id INTEGER PRIMARY KEY, thread_id INTEGER, account_id INTEGER, body TEXT, image TEXT, created INTEGER)",
            "CREATE TABLE IF NOT EXISTS flights(account_id INTEGER PRIMARY KEY, ship TEXT, location TEXT, job TEXT, updated INTEGER)",
            "CREATE INDEX IF NOT EXISTS posts_thread ON posts(thread_id)",
        };
        for (auto s : schema)
            exec(s);
        // Databases made before the economy existed lack these columns.
        std::string cols;
        {
            Stmt ti("PRAGMA table_info(accounts)");
            while (ti.row())
                cols += " " + ti.text(1) + " ";
        }
        if (cols.find(" credits ") == std::string::npos)
        {
            exec("ALTER TABLE accounts ADD COLUMN credits INTEGER DEFAULT 5000");
            exec("ALTER TABLE accounts ADD COLUMN location TEXT DEFAULT 'chippenham'");
            exec("ALTER TABLE accounts ADD COLUMN ship TEXT DEFAULT ''");
        }
        exec("CREATE TABLE IF NOT EXISTS jobs(id INTEGER PRIMARY KEY, origin TEXT, dest TEXT, kind TEXT, qty INTEGER, pay INTEGER, dv REAL, dist REAL, created INTEGER,"
             " deadline INTEGER, status TEXT DEFAULT 'open', account_id INTEGER, ship TEXT, accepted INTEGER, finished INTEGER)");
        exec("CREATE TABLE IF NOT EXISTS ledger(id INTEGER PRIMARY KEY, account_id INTEGER, t INTEGER, amount INTEGER, balance INTEGER, text TEXT)");
        exec("CREATE INDEX IF NOT EXISTS jobs_status ON jobs(status,origin)");
    }

    // ---- HTTP ---------------------------------------------------------------------------------------------------------------------------

    struct Request
    {
        std::string method, path, query, body, ip;
        std::map<std::string, std::string> headers, form, cookies;
        std::string file_data; // one uploaded file (multipart field "image")
    };

    struct Response
    {
        int status = 200;
        std::string type = "text/html; charset=utf-8";
        std::string body;
        std::vector<std::string> headers;
    };

    struct Session
    {
        long account = 0;
        std::string csrf, callsign, token;
        bool admin = false;
    };

    std::string cookie_attrs() { return std::string("; Path=/; HttpOnly; SameSite=Lax") + (cfg.secure ? "; Secure" : ""); }

    Response redirect(const std::string &to)
    {
        Response r;
        r.status = 303;
        r.headers.push_back("Location: " + cfg.base + to);
        return r;
    }

    std::string page(const std::string &title, const std::string &content, const Session *me)
    {
        std::ostringstream o;
        o << "<!DOCTYPE html><html lang=\"en\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">"
          << "<title>" << esc(title) << " - KosmosAV</title><link rel=\"stylesheet\" href=\"" << cfg.site << "/css/site.css\"></head><body>"
          << "<header class=\"topbar\"><a class=\"brand\" href=\"" << cfg.site << "/index.html\">KOS<span>M</span>OS</a><nav>"
          << "<a href=\"" << cfg.site << "/kav/index.html\">KosmosAV</a>"
          << "<a href=\"" << cfg.base << "/jobs\">Jobs</a><a href=\"" << cfg.base << "/board\">Board</a><a href=\"" << cfg.base << "/roster\">Roster</a><a href=\"" << cfg.base << "/flights\">Flights</a>";
        if (me && me->admin)
            o << "<a href=\"" << cfg.base << "/admin\">Admin</a>";
        if (me)
            o << "<a href=\"" << cfg.base << "/account\">" << esc(me->callsign) << "</a><form method=\"post\" action=\"" << cfg.base << "/logout\" style=\"display:inline\"><input type=\"hidden\" name=\"csrf\" value=\"" << me->csrf
              << "\"><button>Log out</button></form>";
        else
            o << "<a href=\"" << cfg.base << "/login\">Log in</a><a href=\"" << cfg.base << "/signup\">Join</a>";
        o << "</nav></header><main class=\"wrap\" style=\"max-width:860px;margin:40px auto;padding:0 24px\">" << content << "</main></body></html>";
        return o.str();
    }

    Response html(const std::string &title, const std::string &content, const Session *me, int status = 200)
    {
        Response r;
        r.status = status;
        r.body = page(title, content, me);
        return r;
    }

    Response message(const std::string &title, const std::string &text, const Session *me, int status = 200)
    {
        return html(title, "<h1>" + esc(title) + "</h1><p>" + esc(text) + "</p>", me, status);
    }

    std::string csrf_field(const Session *me) { return me ? "<input type=\"hidden\" name=\"csrf\" value=\"" + me->csrf + "\">" : ""; }

    bool load_session(const Request &rq, Session &s)
    {
        auto it = rq.cookies.find("kav");
        if (it == rq.cookies.end())
            return false;
        Stmt st("SELECT a.id,a.callsign,a.is_admin,s.csrf FROM sessions s JOIN accounts a ON a.id=s.account_id WHERE s.token=? AND s.expires>? AND a.banned=0",
                {it->second, std::to_string(now_s())});
        if (!st.row())
            return false;
        s.account = st.num(0);
        s.callsign = st.text(1);
        s.admin = st.num(2) != 0;
        s.csrf = st.text(3);
        s.token = it->second;
        return true;
    }

    bool csrf_ok(const Request &rq, const Session &me)
    {
        auto it = rq.form.find("csrf");
        return it != rq.form.end() && const_eq(it->second, me.csrf);
    }

    bool valid_callsign(const std::string &c)
    {
        if (c.size() < 3 || c.size() > 20)
            return false;
        for (char ch : c)
            if (!std::isalnum((unsigned char)ch) && ch != '-' && ch != '_')
                return false;
        return true;
    }

    std::string trunc(std::string s, size_t n)
    {
        if (s.size() > n)
            s.resize(n);
        return s;
    }

    std::string field(const Request &rq, const char *k) { auto it = rq.form.find(k); return it == rq.form.end() ? "" : it->second; }

    // Failed login throttle, kept in memory: 8 failures from one address in 10 minutes lock it out for the rest of the window.
    std::map<std::string, std::vector<long>> login_failures;

    bool login_throttled(const std::string &ip)
    {
        auto &v = login_failures[ip];
        long t = now_s();
        v.erase(std::remove_if(v.begin(), v.end(), [&](long x) { return t - x > 600; }), v.end());
        return v.size() >= 8;
    }

    // ---- registry: bodies, locations and ship types (kav/data/registry.txt, made by kav/tools/make-registry.py) ---------------------------

    struct Body { std::string id, parent; double gm = 0, radius = 0, a = 0; bool atm = false; };
    struct Location { std::string id, body, name; double lat = 0, lon = 0; };
    struct ShipType { std::string id, name; int cargo = 0, seats = 0; double max_dv = 0; int fee = 0; };

    std::map<std::string, Body> bodies;
    std::vector<Location> locations;
    std::vector<ShipType> ship_types;

    bool load_registry(const std::string &path)
    {
        std::ifstream in(path);
        if (!in)
            return false;
        std::string line;
        while (std::getline(in, line))
        {
            if (line.empty() || line[0] == '#')
                continue;
            std::istringstream ls(line);
            std::string kind;
            ls >> kind;
            if (kind == "body")
            {
                Body b;
                int atm = 0;
                ls >> b.id >> b.parent >> b.gm >> b.radius >> b.a >> atm;
                b.atm = atm != 0;
                if (b.parent == "-")
                    b.parent.clear();
                bodies[b.id] = b;
            }
            else if (kind == "location")
            {
                Location l;
                ls >> l.id >> l.body >> l.lat >> l.lon;
                std::getline(ls, l.name);
                l.name.erase(0, l.name.find_first_not_of(' '));
                locations.push_back(l);
            }
            else if (kind == "ship")
            {
                ShipType s;
                ls >> s.id >> s.cargo >> s.seats >> s.max_dv >> s.fee;
                std::getline(ls, s.name);
                s.name.erase(0, s.name.find_first_not_of(' '));
                ship_types.push_back(s);
            }
        }
        return !locations.empty() && !ship_types.empty();
    }

    const Location *find_loc(const std::string &id)
    {
        for (auto &l : locations)
            if (l.id == id)
                return &l;
        return nullptr;
    }

    const ShipType *find_ship(const std::string &id)
    {
        for (auto &s : ship_types)
            if (s.id == id)
                return &s;
        return nullptr;
    }

    std::string loc_name(const std::string &id)
    {
        auto l = find_loc(id);
        return l ? l->name : id;
    }

    // ---- delta-v and distance (rough patched conics; game balance, not navigation) ----------------------------------------------------

    constexpr double pi = 3.14159265358979323846;

    double low_radius(const Body &b) { return b.radius * 1.05; }
    double circ_low(const Body &b) { return std::sqrt(b.gm / low_radius(b)); }
    double escape_cost(double mu, double r, double vinf) { return std::sqrt(vinf * vinf + 2 * mu / r) - std::sqrt(mu / r); }

    void hohmann(double mu, double r1, double r2, double &d1, double &d2)
    {
        double a = (r1 + r2) / 2;
        d1 = std::fabs(std::sqrt(mu * (2 / r1 - 1 / a)) - std::sqrt(mu / r1));
        d2 = std::fabs(std::sqrt(mu / r2) - std::sqrt(mu * (2 / r2 - 1 / a)));
    }

    // The body, its parent, and so on up to the star.
    std::vector<std::string> body_chain(const std::string &id)
    {
        std::vector<std::string> c;
        for (std::string n = id; !n.empty() && bodies.count(n); n = bodies[n].parent)
            c.push_back(n);
        return c;
    }

    // Cost to leave (or, the same the other way, be captured at) a low orbit of `b` and reach the frame of `top` with excess speed vinf.
    double leave_cost(const std::string &b, const std::string &top, double vinf)
    {
        auto c = body_chain(b);
        double cost = 0;
        for (size_t i = 0; i < c.size() && c[i] != top; ++i)
        {
            const Body &bi = bodies[c[i]];
            bool last = i + 1 >= c.size() || c[i + 1] == top;
            double r = i == 0 ? low_radius(bi) : bodies[c[i - 1]].a;
            cost += escape_cost(bi.gm, r, last ? vinf : 0);
        }
        return cost;
    }

    std::string common_parent(const std::string &b1, const std::string &b2)
    {
        auto c1 = body_chain(b1), c2 = body_chain(b2);
        for (auto &x : c1)
            if (std::find(c2.begin(), c2.end(), x) != c2.end())
                return x;
        return "";
    }

    // Orbit radius, in the frame of `top`, of the body that carries `b` (the body itself when it is `top`: its low orbit).
    double frame_radius(const std::string &b, const std::string &top)
    {
        if (b == top)
            return low_radius(bodies[b]);
        auto c = body_chain(b);
        for (size_t i = 0; i < c.size(); ++i)
            if (i + 1 < c.size() && c[i + 1] == top)
                return bodies[c[i]].a;
        return 0;
    }

    struct Route { double dv_kms = 0, dist_km = 0; };

    Route route_between(const Location &l1, const Location &l2)
    {
        Route r;
        const Body &b1 = bodies[l1.body], &b2 = bodies[l2.body];
        if (b1.id == b2.id)
        {
            double p1 = l1.lat * pi / 180, p2 = l2.lat * pi / 180, dl = (l2.lon - l1.lon) * pi / 180;
            double c = std::sin(p1) * std::sin(p2) + std::cos(p1) * std::cos(p2) * std::cos(dl);
            double ang = std::acos(std::max(-1.0, std::min(1.0, c)));
            r.dist_km = ang * b1.radius / 1000;
            r.dv_kms = (100 + 0.5 * circ_low(b1) * ang / pi) / 1000;
            return r;
        }
        std::string top = common_parent(b1.id, b2.id);
        double r1 = frame_radius(b1.id, top), r2 = frame_radius(b2.id, top), d1, d2;
        hohmann(bodies[top].gm, r1, r2, d1, d2);
        double dv = (b1.id == top ? d1 : leave_cost(b1.id, top, d1)) + (b2.id == top ? d2 : leave_cost(b2.id, top, d2));
        dv += 1.25 * circ_low(b1) + (b2.atm ? 0.15 : 1.1) * circ_low(b2); // climb out (with losses) and land (aerobraking where there is air)
        r.dv_kms = dv / 1000;
        r.dist_km = std::fabs(r1 - r2) / 1000;
        return r;
    }

    constexpr int pax_kg = 90;

    // Credits: per kilogram 0.05 + 0.4 per km/s of delta-v + 0.08 per million km, passengers three times that.
    long job_pay(bool pax, int qty, const Route &r)
    {
        double per_kg = 0.05 + 0.4 * r.dv_kms + 0.08 * r.dist_km / 1e6;
        double pay = (pax ? qty * pax_kg * 3.0 : (double)qty) * per_kg;
        return std::max(400L, (long)(pay / 10 + 0.5) * 10);
    }

    // ---- jobs, credits and the rules (the web pages and the API both go through these) ----------------------------------------------------

    std::mt19937_64 rng{std::random_device{}()};
    long last_tick = 0;

    void credit(long account, long amount, const std::string &text)
    {
        exec("UPDATE accounts SET credits=credits+? WHERE id=?", {std::to_string(amount), std::to_string(account)});
        exec("INSERT INTO ledger(account_id,t,amount,balance,text) VALUES(?,?,?,(SELECT credits FROM accounts WHERE id=?),?)",
             {std::to_string(account), std::to_string(now_s()), std::to_string(amount), std::to_string(account), text});
    }

    double max_fleet_dv()
    {
        double m = 0;
        for (auto &s : ship_types)
            m = std::max(m, s.max_dv);
        return m;
    }

    bool some_ship_fits(bool pax, int qty, double dv)
    {
        for (auto &s : ship_types)
            if (s.max_dv >= dv && (pax ? s.seats : s.cargo) >= qty)
                return true;
        return false;
    }

    void make_job(const Location &from)
    {
        std::uniform_real_distribution<double> u(0, 1);
        for (int tries = 0; tries < 40; ++tries)
        {
            const Location &to = locations[rng() % locations.size()];
            if (to.id == from.id)
                continue;
            Route r = route_between(from, to);
            if (r.dv_kms > max_fleet_dv())
                continue;
            bool pax = u(rng) < 0.3;
            int qty = pax ? 1 + (int)(rng() % 8) : (int)(std::pow(2.0, u(rng) * 7.2) * 100 / 50) * 50;
            if (!some_ship_fits(pax, qty, r.dv_kms))
                continue;
            long deadline = now_s() + (long)((3 + std::min(27.0, r.dist_km / 1e6 * 0.5)) * 86400);
            exec("INSERT INTO jobs(origin,dest,kind,qty,pay,dv,dist,created,deadline) VALUES(?,?,?,?,?,?,?,?,?)",
                 {from.id, to.id, pax ? "pax" : "cargo", std::to_string(qty), std::to_string(job_pay(pax, qty, r)), std::to_string(r.dv_kms), std::to_string(r.dist_km),
                  std::to_string(now_s()), std::to_string(deadline)});
            return;
        }
    }

    // Expires late jobs and keeps six open jobs at every location. Cheap, runs at most once every 30 seconds.
    void tick()
    {
        if (now_s() - last_tick < 30)
            return;
        last_tick = now_s();
        exec("UPDATE jobs SET status='expired' WHERE status IN ('open','accepted') AND deadline<?", {std::to_string(now_s())});
        exec("UPDATE accounts SET ship='' WHERE ship<>'' AND id NOT IN (SELECT account_id FROM jobs WHERE status='accepted')");
        exec("DELETE FROM jobs WHERE status IN ('expired','done') AND created<?", {std::to_string(now_s() - 30 * 86400)});
        for (auto &l : locations)
            for (long have = scalar("SELECT COUNT(*) FROM jobs WHERE status='open' AND origin=?", {l.id}); have < 6; ++have)
                make_job(l);
    }

    std::string job_text(long id, const std::string &kind, int qty)
    {
        return "job " + std::to_string(id) + " (" + (kind == "pax" ? std::to_string(qty) + (qty == 1 ? " passenger" : " passengers") : std::to_string(qty) + " kg") + ")";
    }

    // Each returns an empty string when it worked, otherwise the reason it was refused.
    std::string job_accept(long account, long id, const std::string &ship_id)
    {
        Stmt a("SELECT credits,location,ship FROM accounts WHERE id=? AND banned=0", {std::to_string(account)});
        if (!a.row())
            return "No such pilot.";
        long credits = a.num(0);
        std::string where = a.text(1), have_ship = a.text(2);
        Stmt j("SELECT origin,kind,qty,pay,dv,deadline FROM jobs WHERE id=? AND status='open'", {std::to_string(id)});
        if (!j.row())
            return "That job is not available any more.";
        std::string origin = j.text(0), kind = j.text(1);
        int qty = (int)j.num(2);
        double dv = sqlite3_column_double(j.s, 4);
        if (j.num(5) < now_s())
            return "That job has run out of time.";
        if (where != origin)
            return "You are at " + loc_name(where) + "; this job leaves from " + loc_name(origin) + ".";
        const ShipType *ship = find_ship(ship_id);
        if (!ship)
            return "Choose a ship.";
        if (!have_ship.empty() && have_ship != ship->id)
            return "You are already flying jobs in the " + (find_ship(have_ship) ? find_ship(have_ship)->name : have_ship) + ".";
        if (dv > ship->max_dv)
            return "This job needs " + std::to_string((int)(dv + 0.5)) + " km/s of delta-v; the " + ship->name + " manages " + std::to_string((int)ship->max_dv) + ".";
        long loaded = scalar("SELECT COALESCE(SUM(qty),0) FROM jobs WHERE account_id=? AND status='accepted' AND kind=?", {std::to_string(account), kind});
        if (loaded + qty > (kind == "pax" ? ship->seats : ship->cargo))
            return std::string("The ") + ship->name + " cannot take that on top of what you carry (" + (kind == "pax" ? std::to_string(ship->seats) + " seats" : std::to_string(ship->cargo) + " kg") + ").";
        if (credits < ship->fee)
            return "The rental fee is " + std::to_string(ship->fee) + " credits.";
        exec("UPDATE jobs SET status='accepted',account_id=?,ship=?,accepted=? WHERE id=?", {std::to_string(account), ship->id, std::to_string(now_s()), std::to_string(id)});
        exec("UPDATE accounts SET ship=? WHERE id=?", {ship->id, std::to_string(account)});
        credit(account, -ship->fee, "Rented the " + ship->name + " for " + job_text(id, kind, qty));
        return "";
    }

    std::string job_abandon(long account, long id)
    {
        if (scalar("SELECT COUNT(*) FROM jobs WHERE id=? AND account_id=? AND status='accepted'", {std::to_string(id), std::to_string(account)}) == 0)
            return "That is not one of your jobs.";
        exec("UPDATE jobs SET status='open',account_id=NULL,ship=NULL,accepted=NULL WHERE id=?", {std::to_string(id)});
        exec("UPDATE accounts SET ship='' WHERE id=? AND id NOT IN (SELECT account_id FROM jobs WHERE status='accepted')", {std::to_string(account)});
        return "";
    }

    // The KosmosAV program calls this when the game reports the cargo delivered. The pilot's last heartbeat has to be at the destination.
    std::string job_complete(long account, long id)
    {
        Stmt j("SELECT dest,kind,qty,pay,accepted,deadline,ship,dv FROM jobs WHERE id=? AND account_id=? AND status='accepted'", {std::to_string(id), std::to_string(account)});
        if (!j.row())
            return "That is not one of your jobs.";
        std::string dest = j.text(0), kind = j.text(1);
        int qty = (int)j.num(2);
        long pay = j.num(3), accepted = j.num(4);
        if (j.num(5) < now_s())
            return "That job ran out of time.";
        Stmt f("SELECT location,updated,ship FROM flights WHERE account_id=?", {std::to_string(account)});
        std::string job_ship = j.text(6);
        if (!f.row() || f.text(0) != dest || now_s() - f.num(1) > 900)
            return "Your KosmosAV program has not reported you at " + loc_name(dest) + ".";
        if (f.text(2) != job_ship)
            return "You rented the " + (find_ship(job_ship) ? find_ship(job_ship)->name : job_ship) + " for this job but your program reports a different ship.";
        // The program is trusted to report honestly, so a delivery cannot be faster than a plausible burn and coast: 20 s plus 8 s per km/s of delta-v.
        const long least = 20 + (long)(8 * sqlite3_column_double(j.s, 7));
        if (now_s() - accepted < least)
            return "That was too quick to be a delivery.";
        exec("UPDATE jobs SET status='done',finished=? WHERE id=?", {std::to_string(now_s()), std::to_string(id)});
        exec("UPDATE accounts SET location=?,deliveries=deliveries+1,hours=hours+? WHERE id=?",
             {dest, std::to_string((now_s() - accepted) / 3600.0), std::to_string(account)});
        exec("UPDATE accounts SET ship='' WHERE id=? AND id NOT IN (SELECT account_id FROM jobs WHERE status='accepted')", {std::to_string(account)});
        credit(account, pay, "Delivered " + job_text(id, kind, qty) + " to " + loc_name(dest));
        return "";
    }

    // ---- pages --------------------------------------------------------------------------------------------------------------------------

    Response home(const Session *me)
    {
        return html("Home", "<h1>KosmosAV</h1><p>Kosmos Air Virtual: the shared freight and passenger network for Kosmos pilots.</p>"
                            "<ul><li><a href=\"" + cfg.base + "/jobs\">Job board</a></li>" + std::string(me ? "<li><a href=\"" + cfg.base + "/mine\">My jobs and credits</a></li>" : "") + "<li><a href=\"" + cfg.base + "/flights\">Who is flying where</a></li><li><a href=\"" + cfg.base + "/roster\">Roster</a></li>"
                            "<li><a href=\"" + cfg.base + "/board\">Message board</a></li>" +
                                std::string(me ? "" : "<li><a href=\"" + cfg.base + "/signup\">Ask to join</a></li>") + "</ul>", me);
    }

    Response signup(const Request &rq, const Session *me)
    {
        if (rq.method == "POST")
        {
            if (!field(rq, "website").empty()) // honeypot: pretend it worked
                return message("Request received", "Thank you. An administrator will look at your request.", me);
            std::string call = trunc(field(rq, "callsign"), 20), email = trunc(field(rq, "email"), 120), note = trunc(field(rq, "note"), 1000);
            if (!valid_callsign(call) || email.find('@') == std::string::npos)
                return message("Check your details", "The callsign needs 3 to 20 letters, digits, - or _, and the email address must be valid.", me, 400);
            if (scalar("SELECT COUNT(*) FROM signups WHERE ip=? AND created>?", {rq.ip, std::to_string(now_s() - 3600)}) >= 3 ||
                scalar("SELECT COUNT(*) FROM signups WHERE status='pending'") >= 200)
                return message("Too many requests", "Please try again later.", me, 429);
            if (scalar("SELECT COUNT(*) FROM accounts WHERE callsign=?", {call}) > 0 ||
                scalar("SELECT COUNT(*) FROM signups WHERE callsign=? AND status='pending'", {call}) > 0)
                return message("Callsign taken", "Please choose another callsign.", me, 409);
            exec("INSERT INTO signups(callsign,email,note,ip,created) VALUES(?,?,?,?,?)", {call, email, note, rq.ip, std::to_string(now_s())});
            return message("Request received", "Thank you. An administrator will look at your request.", me);
        }
        return html("Join", "<h1>Ask to join</h1><p>Requests are approved by hand. You will be given a login and an API key for the KosmosAV program.</p>"
                            "<form method=\"post\"><p><label>Callsign<br><input name=\"callsign\" maxlength=\"20\" required></label></p>"
                            "<p><label>Email<br><input name=\"email\" type=\"email\" maxlength=\"120\" required></label></p>"
                            "<p><label>How do you fly Kosmos?<br><textarea name=\"note\" rows=\"4\" cols=\"50\" maxlength=\"1000\"></textarea></label></p>"
                            "<p style=\"display:none\"><input name=\"website\" tabindex=\"-1\" autocomplete=\"off\"></p><button>Send request</button></form>", me);
    }

    Response login(const Request &rq, const Session *me)
    {
        if (rq.method == "POST")
        {
            if (login_throttled(rq.ip))
                return message("Too many attempts", "Please wait ten minutes.", me, 429);
            std::string call = field(rq, "callsign"), pass = field(rq, "password");
            Stmt st("SELECT id,salt,hash FROM accounts WHERE callsign=? AND banned=0", {call});
            bool ok = false;
            long id = 0;
            if (st.row())
            {
                id = st.num(0);
                ok = const_eq(pbkdf2(pass, st.text(1)), st.text(2));
            }
            else
                pbkdf2(pass, "0000000000000000"); // keep the timing similar
            if (!ok)
            {
                login_failures[rq.ip].push_back(now_s());
                return message("Log in failed", "Unknown callsign or wrong password.", me, 401);
            }
            std::string tok = random_hex(32), csrf = random_hex(16);
            exec("DELETE FROM sessions WHERE expires<?", {std::to_string(now_s())});
            exec("INSERT INTO sessions VALUES(?,?,?,?)", {tok, std::to_string(id), csrf, std::to_string(now_s() + 14 * 86400)});
            Response r = redirect("/jobs");
            r.headers.push_back("Set-Cookie: kav=" + tok + cookie_attrs() + "; Max-Age=1209600");
            return r;
        }
        return html("Log in", "<h1>Log in</h1><form method=\"post\"><p><label>Callsign<br><input name=\"callsign\" required></label></p>"
                              "<p><label>Password<br><input name=\"password\" type=\"password\" required></label></p><button>Log in</button></form>", me);
    }

    Response board(const Session *me)
    {
        std::ostringstream o;
        o << "<h1>Message board</h1>";
        if (me)
            o << "<p><a href=\"" << cfg.base << "/board/new\">Start a thread</a></p>";
        else
            o << "<p>Log in to post. Anyone can read.</p>";
        o << "<table><tr><th>Thread</th><th>By</th><th>Last post</th></tr>";
        Stmt st("SELECT t.id,t.title,a.callsign,t.last_post FROM threads t JOIN accounts a ON a.id=t.account_id ORDER BY t.last_post DESC LIMIT 100");
        while (st.row())
            o << "<tr><td><a href=\"" << cfg.base << "/thread/" << st.num(0) << "\">" << esc(st.text(1)) << "</a></td><td>" << esc(st.text(2)) << "</td><td>"
              << when(st.num(3)) << "</td></tr>";
        o << "</table>";
        return html("Board", o.str(), me);
    }

    bool sniff_image(const std::string &d, std::string &ext)
    {
        if (d.size() > 8 && d.compare(0, 8, "\x89PNG\r\n\x1a\n") == 0) { ext = "png"; return true; }
        if (d.size() > 3 && (unsigned char)d[0] == 0xFF && (unsigned char)d[1] == 0xD8 && (unsigned char)d[2] == 0xFF) { ext = "jpg"; return true; }
        return false;
    }

    // Stores the post (and screenshot, if any); returns false with `error` set when it was refused.
    bool new_post(const Request &rq, const Session &me, long thread, const std::string &body, std::string &error)
    {
        std::string image, ext;
        if (!rq.file_data.empty())
        {
            if (rq.file_data.size() > (4u << 20) || !sniff_image(rq.file_data, ext))
            {
                error = "Screenshots must be PNG or JPEG files up to 4 MB.";
                return false;
            }
        }
        if (body.empty() && rq.file_data.empty())
        {
            error = "Write something or attach a screenshot.";
            return false;
        }
        if (!rq.file_data.empty())
        {
            fs::create_directories(cfg.data);
            image = random_hex(12) + "." + ext;
            std::ofstream(fs::path(cfg.data) / image, std::ios::binary) << rq.file_data;
        }
        exec("INSERT INTO posts(thread_id,account_id,body,image,created) VALUES(?,?,?,?,?)",
             {std::to_string(thread), std::to_string(me.account), trunc(body, 5000), image, std::to_string(now_s())});
        exec("UPDATE threads SET last_post=? WHERE id=?", {std::to_string(now_s()), std::to_string(thread)});
        return true;
    }

    bool posting_too_fast(const Session &me)
    {
        return scalar("SELECT COUNT(*) FROM posts WHERE account_id=? AND created>?", {std::to_string(me.account), std::to_string(now_s() - 60)}) >= 5;
    }

    Response board_new(const Request &rq, const Session *me)
    {
        if (!me)
            return redirect("/login");
        if (rq.method == "POST")
        {
            if (!csrf_ok(rq, *me))
                return message("Rejected", "The form was not valid. Reload and try again.", me, 403);
            std::string title = trunc(field(rq, "title"), 120);
            if (title.empty())
                return message("Title needed", "Give the thread a title.", me, 400);
            if (posting_too_fast(*me))
                return message("Slow down", "Too many posts in a minute.", me, 429);
            exec("INSERT INTO threads(title,account_id,created,last_post) VALUES(?,?,?,?)", {title, std::to_string(me->account), std::to_string(now_s()), std::to_string(now_s())});
            long id = (long)sqlite3_last_insert_rowid(db);
            std::string err;
            if (!new_post(rq, *me, id, field(rq, "body"), err))
            {
                exec("DELETE FROM threads WHERE id=?", {std::to_string(id)});
                return message("Not posted", err, me, 400);
            }
            return redirect("/thread/" + std::to_string(id));
        }
        return html("New thread", "<h1>New thread</h1><form method=\"post\" enctype=\"multipart/form-data\">" + csrf_field(me) +
                                      "<p><label>Title<br><input name=\"title\" maxlength=\"120\" required></label></p>"
                                      "<p><label>Text<br><textarea name=\"body\" rows=\"6\" cols=\"60\" maxlength=\"5000\"></textarea></label></p>"
                                      "<p><label>Screenshot (PNG or JPEG, up to 4 MB)<br><input type=\"file\" name=\"image\" accept=\"image/png,image/jpeg\"></label></p>"
                                      "<button>Post</button></form>", me);
    }

    Response thread(const Request &rq, const Session *me, long id)
    {
        Stmt t("SELECT title,locked FROM threads WHERE id=?", {std::to_string(id)});
        if (!t.row())
            return message("Not found", "No such thread.", me, 404);
        std::string title = t.text(0);
        bool locked = t.num(1) != 0;
        std::string error;
        if (rq.method == "POST")
        {
            if (!me)
                return redirect("/login");
            if (!csrf_ok(rq, *me))
                return message("Rejected", "The form was not valid. Reload and try again.", me, 403);
            if (rq.form.count("delete") && me->admin)
            {
                exec("DELETE FROM posts WHERE id=? AND thread_id=?", {rq.form.at("delete"), std::to_string(id)});
                return redirect("/thread/" + std::to_string(id));
            }
            if (rq.form.count("lock") && me->admin)
            {
                exec("UPDATE threads SET locked=? WHERE id=?", {locked ? "0" : "1", std::to_string(id)});
                return redirect("/thread/" + std::to_string(id));
            }
            if (locked)
                return message("Locked", "This thread is locked.", me, 403);
            if (posting_too_fast(*me))
                return message("Slow down", "Too many posts in a minute.", me, 429);
            if (new_post(rq, *me, id, field(rq, "body"), error))
                return redirect("/thread/" + std::to_string(id));
        }
        std::ostringstream o;
        o << "<h1>" << esc(title) << (locked ? " (locked)" : "") << "</h1>";
        Stmt st("SELECT p.id,a.callsign,p.body,p.image,p.created FROM posts p JOIN accounts a ON a.id=p.account_id WHERE p.thread_id=? ORDER BY p.id", {std::to_string(id)});
        while (st.row())
        {
            o << "<div style=\"border-top:1px solid #333;padding:10px 0\"><strong>" << esc(st.text(1)) << "</strong> <small>" << when(st.num(4)) << "</small>";
            if (me && me->admin)
                o << "<form method=\"post\" style=\"display:inline\">" << csrf_field(me) << "<input type=\"hidden\" name=\"delete\" value=\"" << st.num(0) << "\"> <button>Delete</button></form>";
            std::string br;
            for (char c : esc(st.text(2)))
                br += c == '\n' ? std::string("<br>") : std::string(1, c);
            o << "<p>" << br << "</p>";
            if (!st.text(3).empty())
                o << "<p><a href=\"" << cfg.base << "/img/" << st.text(3) << "\"><img src=\"" << cfg.base << "/img/" << st.text(3) << "\" style=\"max-width:100%\" alt=\"screenshot\"></a></p>";
            o << "</div>";
        }
        if (!error.empty())
            o << "<p><strong>" << esc(error) << "</strong></p>";
        if (me && !locked)
            o << "<form method=\"post\" enctype=\"multipart/form-data\">" << csrf_field(me)
              << "<p><textarea name=\"body\" rows=\"4\" cols=\"60\" maxlength=\"5000\"></textarea></p><p><input type=\"file\" name=\"image\" accept=\"image/png,image/jpeg\"></p><button>Reply</button></form>";
        if (me && me->admin)
            o << "<form method=\"post\">" << csrf_field(me) << "<input type=\"hidden\" name=\"lock\" value=\"1\"><button>" << (locked ? "Unlock" : "Lock") << " thread</button></form>";
        return html(title, o.str(), me);
    }

    Response not_found_plain()
    {
        Response r;
        r.status = 404;
        r.type = "text/plain";
        r.body = "not found";
        return r;
    }

    Response image(const std::string &name)
    {
        bool good = name.size() > 5 && name.size() < 40;
        for (char c : name)
            good = good && (std::isalnum((unsigned char)c) || c == '.');
        if (!good || name.find("..") != std::string::npos)
            return not_found_plain();
        std::ifstream in(fs::path(cfg.data) / name, std::ios::binary);
        if (!in)
            return not_found_plain();
        Response r;
        std::ostringstream ss;
        ss << in.rdbuf();
        r.body = ss.str();
        r.type = name.size() > 4 && name.compare(name.size() - 4, 4, ".png") == 0 ? "image/png" : "image/jpeg";
        r.headers.push_back("X-Content-Type-Options: nosniff");
        r.headers.push_back("Content-Security-Policy: default-src 'none'");
        return r;
    }

    Response account(const Request &rq, const Session *me)
    {
        if (!me)
            return redirect("/login");
        std::string notice;
        if (rq.method == "POST")
        {
            if (!csrf_ok(rq, *me))
                return message("Rejected", "The form was not valid. Reload and try again.", me, 403);
            std::string act = field(rq, "action");
            if (act == "password")
            {
                Stmt st("SELECT salt,hash FROM accounts WHERE id=?", {std::to_string(me->account)});
                std::string np = field(rq, "new");
                if (!st.row() || !const_eq(pbkdf2(field(rq, "old"), st.text(0)), st.text(1)))
                    notice = "The current password is wrong.";
                else if (np.size() < 8 || np.size() > 200)
                    notice = "The new password needs at least 8 characters.";
                else
                {
                    std::string salt = random_hex(16);
                    exec("UPDATE accounts SET salt=?,hash=? WHERE id=?", {salt, pbkdf2(np, salt), std::to_string(me->account)});
                    exec("DELETE FROM sessions WHERE account_id=? AND token<>?", {std::to_string(me->account), me->token});
                    notice = "Password changed. Other logins were signed out.";
                }
            }
            else if (act == "newkey")
            {
                exec("UPDATE accounts SET api_token=? WHERE id=?", {random_hex(24), std::to_string(me->account)});
                notice = "New API key made. Put it in the KosmosAV program; the old one no longer works.";
            }
        }
        Stmt st("SELECT api_token FROM accounts WHERE id=?", {std::to_string(me->account)});
        std::string key = st.row() ? st.text(0) : "";
        std::ostringstream o;
        o << "<h1>My account</h1>";
        if (!notice.empty())
            o << "<p><strong>" << esc(notice) << "</strong></p>";
        o << "<h2>API key</h2><p>The KosmosAV program uses this to report where you are flying. Keep it private.</p><p><code>" << esc(key) << "</code></p>"
          << "<form method=\"post\">" << csrf_field(me) << "<input type=\"hidden\" name=\"action\" value=\"newkey\"><button>Make a new key</button></form>"
          << "<h2>Change password</h2><form method=\"post\">" << csrf_field(me) << "<input type=\"hidden\" name=\"action\" value=\"password\">"
          << "<p><label>Current password<br><input name=\"old\" type=\"password\" required></label></p>"
          << "<p><label>New password (8 or more characters)<br><input name=\"new\" type=\"password\" minlength=\"8\" required></label></p><button>Change password</button></form>";
        return html("My account", o.str(), me);
    }

    Response roster(const Session *me)
    {
        std::ostringstream o;
        o << "<h1>Roster</h1><table><tr><th>Callsign</th><th>Joined</th><th>Deliveries</th><th>Hours</th></tr>";
        Stmt st("SELECT callsign,created,deliveries,hours FROM accounts WHERE banned=0 ORDER BY deliveries DESC, callsign");
        while (st.row())
        {
            char h[24];
            std::snprintf(h, sizeof h, "%.1f", sqlite3_column_double(st.s, 3));
            o << "<tr><td>" << esc(st.text(0)) << "</td><td>" << when(st.num(1)).substr(0, 10) << "</td><td>" << st.num(2) << "</td><td>" << h << "</td></tr>";
        }
        o << "</table>";
        return html("Roster", o.str(), me);
    }

    Response flights(const Session *me)
    {
        std::ostringstream o;
        o << "<h1>Who is flying where</h1><p>Pilots whose KosmosAV program has reported in the last five minutes.</p>"
             "<table><tr><th>Pilot</th><th>Ship</th><th>Where</th><th>Job</th><th>Updated</th></tr>";
        Stmt st("SELECT a.callsign,f.ship,f.location,f.job,f.updated FROM flights f JOIN accounts a ON a.id=f.account_id WHERE f.updated>? AND a.banned=0 ORDER BY a.callsign",
                {std::to_string(now_s() - 300)});
        int n = 0;
        while (st.row())
        {
            ++n;
            o << "<tr><td>" << esc(st.text(0)) << "</td><td>" << esc(find_ship(st.text(1)) ? find_ship(st.text(1))->name : st.text(1)) << "</td><td>" << (st.text(2).empty() ? "in flight" : esc(loc_name(st.text(2)))) << "</td><td>" << esc(st.text(3)) << "</td><td>"
              << (now_s() - st.num(4)) << " s ago</td></tr>";
        }
        o << "</table>";
        if (!n)
            o << "<p>Nobody is flying right now.</p>";
        return html("Flights", o.str(), me);
    }

    Response admin(const Request &rq, const Session *me)
    {
        if (!me || !me->admin)
            return message("Forbidden", "Administrators only.", me, 403);
        std::string notice;
        if (rq.method == "POST")
        {
            if (!csrf_ok(rq, *me))
                return message("Rejected", "The form was not valid. Reload and try again.", me, 403);
            std::string id = field(rq, "id"), act = field(rq, "action");
            if (act == "approve")
            {
                Stmt st("SELECT callsign,email FROM signups WHERE id=? AND status='pending'", {id});
                if (st.row())
                {
                    std::string call = st.text(0), email = st.text(1);
                    if (scalar("SELECT COUNT(*) FROM accounts WHERE callsign=?", {call}))
                        notice = "That callsign already has an account.";
                    else
                    {
                        std::string pass = random_hex(6), salt = random_hex(16), api = random_hex(24);
                        exec("INSERT INTO accounts(callsign,email,salt,hash,api_token,created) VALUES(?,?,?,?,?,?)", {call, email, salt, pbkdf2(pass, salt), api, std::to_string(now_s())});
                        exec("UPDATE signups SET status='approved' WHERE id=?", {id});
                        notice = "Approved " + call + ". Send them: password " + pass + " and API key " + api + " (shown once).";
                    }
                }
            }
            else if (act == "reject")
            {
                exec("UPDATE signups SET status='rejected' WHERE id=?", {id});
                notice = "Rejected.";
            }
            else if (act == "ban" || act == "unban")
            {
                exec("UPDATE accounts SET banned=? WHERE id=? AND is_admin=0", {act == "ban" ? "1" : "0", id});
                exec("DELETE FROM sessions WHERE account_id=?", {id});
                notice = act == "ban" ? "Banned." : "Unbanned.";
            }
        }
        std::ostringstream o;
        o << "<h1>Admin</h1>";
        if (!notice.empty())
            o << "<p><strong>" << esc(notice) << "</strong></p>";
        o << "<h2>Join requests</h2><table><tr><th>Callsign</th><th>Email</th><th>Note</th><th>When</th><th></th></tr>";
        Stmt st("SELECT id,callsign,email,note,created FROM signups WHERE status='pending' ORDER BY id");
        while (st.row())
            o << "<tr><td>" << esc(st.text(1)) << "</td><td>" << esc(st.text(2)) << "</td><td>" << esc(st.text(3)) << "</td><td>" << when(st.num(4)) << "</td><td>"
              << "<form method=\"post\">" << csrf_field(me) << "<input type=\"hidden\" name=\"id\" value=\"" << st.num(0)
              << "\"><button name=\"action\" value=\"approve\">Approve</button> <button name=\"action\" value=\"reject\">Reject</button></form></td></tr>";
        o << "</table><h2>Pilots</h2><table><tr><th>Callsign</th><th>Email</th><th></th></tr>";
        Stmt ac("SELECT id,callsign,email,banned,is_admin FROM accounts ORDER BY callsign");
        while (ac.row())
        {
            o << "<tr><td>" << esc(ac.text(1)) << (ac.num(4) ? " (admin)" : "") << "</td><td>" << esc(ac.text(2)) << "</td><td>";
            if (!ac.num(4))
                o << "<form method=\"post\">" << csrf_field(me) << "<input type=\"hidden\" name=\"id\" value=\"" << ac.num(0) << "\"><button name=\"action\" value=\""
                  << (ac.num(3) ? "unban\">Unban" : "ban\">Ban") << "</button></form>";
            o << "</td></tr>";
        }
        o << "</table>";
        return html("Admin", o.str(), me);
    }

    std::string load_text(long kg_or_seats, const std::string &kind) { return kind == "pax" ? std::to_string(kg_or_seats) + (kg_or_seats == 1 ? " passenger" : " passengers") : std::to_string(kg_or_seats) + " kg"; }

    std::string ships_that_fit(const std::string &kind, int qty, double dv)
    {
        std::string out;
        for (auto &s : ship_types)
            if (s.max_dv >= dv && (kind == "pax" ? s.seats : s.cargo) >= qty)
                out += (out.empty() ? "" : ", ") + s.name;
        return out;
    }

    Response jobs_page(const Request &rq, const Session *me)
    {
        std::string notice;
        if (rq.method == "POST")
        {
            if (!me)
                return redirect("/login");
            if (!csrf_ok(rq, *me))
                return message("Rejected", "The form was not valid. Reload and try again.", me, 403);
            std::string err = job_accept(me->account, std::atol(field(rq, "id").c_str()), field(rq, "ship"));
            if (err.empty())
                return redirect("/mine");
            notice = err;
        }
        std::string from = field(rq, "from");
        if (from.empty())
        {
            auto q = rq.query.find("from=");
            if (q != std::string::npos)
                from = rq.query.substr(q + 5, rq.query.find('&', q) - q - 5);
        }
        std::string where;
        if (me)
        {
            Stmt a("SELECT location FROM accounts WHERE id=?", {std::to_string(me->account)});
            if (a.row())
                where = a.text(0);
            if (from.empty())
                from = where;
        }
        if (from == "all")
            from.clear();
        std::ostringstream o;
        o << "<h1>Job board</h1>";
        if (!notice.empty())
            o << "<p><strong>" << esc(notice) << "</strong></p>";
        o << "<p>";
        for (auto &l : locations)
            o << "<a href=\"" << cfg.base << "/jobs?from=" << l.id << "\">" << (l.id == from ? "<strong>" : "") << esc(l.name) << (l.id == from ? "</strong>" : "") << "</a> &middot; ";
        o << "<a href=\"" << cfg.base << "/jobs?from=all\">All</a></p>";
        if (me)
            o << "<p>You are at <strong>" << esc(loc_name(where)) << "</strong>. Jobs can only be taken from where you are.</p>";
        o << "<table><tr><th>Job</th><th>From</th><th>To</th><th>Load</th><th>Delta-v</th><th>Pay</th><th>Due</th><th>Ships</th><th></th></tr>";
        Stmt st(from.empty() ? "SELECT id,origin,dest,kind,qty,pay,dv,deadline FROM jobs WHERE status='open' ORDER BY origin,id"
                             : "SELECT id,origin,dest,kind,qty,pay,dv,deadline FROM jobs WHERE status='open' AND origin=? ORDER BY id",
                from.empty() ? std::vector<std::string>{} : std::vector<std::string>{from});
        while (st.row())
        {
            double dv = sqlite3_column_double(st.s, 6);
            std::string kind = st.text(3);
            o << "<tr><td>" << st.num(0) << "</td><td>" << esc(loc_name(st.text(1))) << "</td><td>" << esc(loc_name(st.text(2))) << "</td><td>" << load_text(st.num(4), kind)
              << "</td><td>" << (int)(dv * 10 + 0.5) / 10.0 << " km/s</td><td>" << st.num(5) << "</td><td>" << (st.num(7) - now_s()) / 86400 << " d</td><td>"
              << esc(ships_that_fit(kind, (int)st.num(4), dv)) << "</td><td>";
            if (me && st.text(1) == where)
            {
                o << "<form method=\"post\">" << csrf_field(me) << "<input type=\"hidden\" name=\"id\" value=\"" << st.num(0) << "\"><select name=\"ship\">";
                for (auto &s : ship_types)
                    o << "<option value=\"" << s.id << "\">" << esc(s.name) << " (" << s.fee << ")</option>";
                o << "</select> <button>Take it</button></form>";
            }
            o << "</td></tr>";
        }
        o << "</table><p>The number in brackets is the rental fee for the ship. Pay is worked out from distance, delta-v and mass.</p>";
        return html("Jobs", o.str(), me);
    }

    Response mine(const Request &rq, const Session *me)
    {
        if (!me)
            return redirect("/login");
        std::string notice;
        if (rq.method == "POST")
        {
            if (!csrf_ok(rq, *me))
                return message("Rejected", "The form was not valid. Reload and try again.", me, 403);
            notice = job_abandon(me->account, std::atol(field(rq, "id").c_str()));
            if (notice.empty())
                notice = "Job given back. The rental fee is not refunded.";
        }
        std::ostringstream o;
        Stmt a("SELECT credits,location,ship,hours,deliveries FROM accounts WHERE id=?", {std::to_string(me->account)});
        a.row();
        o << "<h1>" << esc(me->callsign) << "</h1>";
        if (!notice.empty())
            o << "<p><strong>" << esc(notice) << "</strong></p>";
        o << "<p>Credits <strong>" << a.num(0) << "</strong> &middot; at <strong>" << esc(loc_name(a.text(1))) << "</strong> &middot; ship "
          << (a.text(2).empty() ? "none rented" : esc(find_ship(a.text(2)) ? find_ship(a.text(2))->name : a.text(2))) << " &middot; " << a.num(4) << " deliveries</p>"
          << "<h2>Your jobs</h2><table><tr><th>Job</th><th>From</th><th>To</th><th>Load</th><th>Pay</th><th>Due</th><th></th></tr>";
        Stmt st("SELECT id,origin,dest,kind,qty,pay,deadline FROM jobs WHERE account_id=? AND status='accepted' ORDER BY id", {std::to_string(me->account)});
        int n = 0;
        while (st.row())
        {
            ++n;
            o << "<tr><td>" << st.num(0) << "</td><td>" << esc(loc_name(st.text(1))) << "</td><td>" << esc(loc_name(st.text(2))) << "</td><td>" << load_text(st.num(4), st.text(3))
              << "</td><td>" << st.num(5) << "</td><td>" << (st.num(6) - now_s()) / 86400 << " d</td><td><form method=\"post\">" << csrf_field(me)
              << "<input type=\"hidden\" name=\"id\" value=\"" << st.num(0) << "\"><button>Give back</button></form></td></tr>";
        }
        o << "</table>";
        if (!n)
            o << "<p>No jobs. <a href=\"" << cfg.base << "/jobs\">Find one.</a></p>";
        else
            o << "<p>Fly the load to its destination with the KosmosAV program running; it reports the delivery when you arrive.</p>";
        o << "<h2>Ledger</h2><table><tr><th>When</th><th>What</th><th>Amount</th><th>Balance</th></tr>";
        Stmt lg("SELECT t,text,amount,balance FROM ledger WHERE account_id=? ORDER BY id DESC LIMIT 30", {std::to_string(me->account)});
        while (lg.row())
            o << "<tr><td>" << when(lg.num(0)) << "</td><td>" << esc(lg.text(1)) << "</td><td>" << lg.num(2) << "</td><td>" << lg.num(3) << "</td></tr>";
        o << "</table>";
        return html("My jobs", o.str(), me);
    }

    // ---- JSON API -------------------------------------------------------------------------------------------------------------------------

    std::string jesc(const std::string &s)
    {
        std::string o;
        for (unsigned char c : s)
        {
            if (c == '"' || c == '\\')
                o += std::string("\\") + (char)c;
            else if (c < 0x20)
                o += ' ';
            else
                o += (char)c;
        }
        return o;
    }

    Response json(int status, const std::string &body)
    {
        Response r;
        r.status = status;
        r.type = "application/json";
        r.body = body;
        return r;
    }

    std::string job_json(Stmt &st)
    {
        std::ostringstream o;
        o << "{\"id\":" << st.num(0) << ",\"from\":\"" << jesc(st.text(1)) << "\",\"to\":\"" << jesc(st.text(2)) << "\",\"kind\":\"" << jesc(st.text(3)) << "\",\"qty\":" << st.num(4)
          << ",\"pay\":" << st.num(5) << ",\"dv_kms\":" << sqlite3_column_double(st.s, 6) << ",\"deadline\":" << st.num(7) << "}";
        return o.str();
    }

    // Everything under /api/v1/. Reads of the job board are public; the rest needs "Authorization: Bearer <api key>".
    Response api(const Request &rq)
    {
        const std::string &p = rq.path;
        if (p == "/api/v1/jobs" && rq.method == "GET")
        {
            std::string from;
            auto q = rq.query.find("from=");
            if (q != std::string::npos)
                from = rq.query.substr(q + 5, rq.query.find('&', q) - q - 5);
            Stmt st(from.empty() ? "SELECT id,origin,dest,kind,qty,pay,dv,deadline FROM jobs WHERE status='open' ORDER BY id"
                                 : "SELECT id,origin,dest,kind,qty,pay,dv,deadline FROM jobs WHERE status='open' AND origin=? ORDER BY id",
                    from.empty() ? std::vector<std::string>{} : std::vector<std::string>{from});
            std::string o = "{\"jobs\":[";
            for (bool first = true; st.row(); first = false)
                o += (first ? "" : ",") + job_json(st);
            return json(200, o + "]}");
        }
        if (p == "/api/v1/registry" && rq.method == "GET")
        {
            std::string o = "{\"locations\":[";
            for (size_t i = 0; i < locations.size(); ++i)
                o += std::string(i ? "," : "") + "{\"id\":\"" + jesc(locations[i].id) + "\",\"name\":\"" + jesc(locations[i].name) + "\",\"body\":\"" + jesc(locations[i].body) + "\"}";
            o += "],\"ships\":[";
            for (size_t i = 0; i < ship_types.size(); ++i)
                o += std::string(i ? "," : "") + "{\"id\":\"" + jesc(ship_types[i].id) + "\",\"name\":\"" + jesc(ship_types[i].name) + "\",\"cargo_kg\":" + std::to_string(ship_types[i].cargo) +
                     ",\"seats\":" + std::to_string(ship_types[i].seats) + ",\"max_dv_kms\":" + std::to_string((int)ship_types[i].max_dv) + ",\"fee\":" + std::to_string(ship_types[i].fee) + "}";
            return json(200, o + "]}");
        }
        auto a = rq.headers.find("authorization");
        std::string tok = a != rq.headers.end() && a->second.rfind("Bearer ", 0) == 0 ? a->second.substr(7) : "";
        Stmt who("SELECT id,callsign,credits,location,ship FROM accounts WHERE api_token=? AND banned=0", {tok});
        if (tok.empty() || !who.row())
            return json(401, "{\"error\":\"bad api key\"}");
        long account = who.num(0);
        if (p == "/api/v1/heartbeat" && rq.method == "POST")
        {
            exec("INSERT INTO flights VALUES(?,?,?,?,?) ON CONFLICT(account_id) DO UPDATE SET ship=excluded.ship,location=excluded.location,job=excluded.job,updated=excluded.updated",
                 {std::to_string(account), trunc(field(rq, "ship"), 80), trunc(field(rq, "location"), 80), trunc(field(rq, "job"), 80), std::to_string(now_s())});
            return json(200, "{\"ok\":true}");
        }
        if (p == "/api/v1/me" && rq.method == "GET")
        {
            std::string o = "{\"callsign\":\"" + jesc(who.text(1)) + "\",\"credits\":" + std::to_string(who.num(2)) + ",\"location\":\"" + jesc(who.text(3)) + "\",\"ship\":\"" + jesc(who.text(4)) + "\",\"jobs\":[";
            Stmt st("SELECT id,origin,dest,kind,qty,pay,dv,deadline FROM jobs WHERE account_id=? AND status='accepted' ORDER BY id", {std::to_string(account)});
            for (bool first = true; st.row(); first = false)
                o += (first ? "" : ",") + job_json(st);
            return json(200, o + "]}");
        }
        if (rq.method == "POST" && (p == "/api/v1/jobs/accept" || p == "/api/v1/jobs/complete" || p == "/api/v1/jobs/abandon"))
        {
            long id = std::atol(field(rq, "id").c_str());
            std::string err = p == "/api/v1/jobs/accept"     ? job_accept(account, id, field(rq, "ship"))
                              : p == "/api/v1/jobs/complete" ? job_complete(account, id)
                                                             : job_abandon(account, id);
            if (!err.empty())
                return json(409, "{\"error\":\"" + jesc(err) + "\"}");
            Stmt c("SELECT credits FROM accounts WHERE id=?", {std::to_string(account)});
            c.row();
            return json(200, "{\"ok\":true,\"credits\":" + std::to_string(c.num(0)) + "}");
        }
        return json(404, "{\"error\":\"unknown endpoint\"}");
    }

    // ---- request parsing and the main loop ----------------------------------------------------------------------------------------------

    void parse_multipart(Request &rq, const std::string &boundary)
    {
        std::string delim = "--" + boundary;
        size_t pos = rq.body.find(delim);
        while (pos != std::string::npos)
        {
            size_t start = pos + delim.size();
            if (rq.body.compare(start, 2, "--") == 0)
                break;
            size_t hend = rq.body.find("\r\n\r\n", start);
            if (hend == std::string::npos)
                break;
            std::string head = rq.body.substr(start, hend - start);
            size_t next = rq.body.find("\r\n" + delim, hend + 4);
            if (next == std::string::npos)
                break;
            std::string data = rq.body.substr(hend + 4, next - (hend + 4));
            auto nm = head.find("name=\"");
            std::string name = nm == std::string::npos ? "" : head.substr(nm + 6, head.find('"', nm + 6) - nm - 6);
            if (head.find("filename=\"") != std::string::npos)
            {
                if (name == "image")
                    rq.file_data = data;
            }
            else
                rq.form[name] = data;
            pos = next + 2;
        }
    }

    bool read_request(int fd, Request &rq)
    {
        std::string buf;
        char tmp[8192];
        size_t hend;
        while ((hend = buf.find("\r\n\r\n")) == std::string::npos)
        {
            ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n <= 0 || buf.size() > 32768)
                return false;
            buf.append(tmp, (size_t)n);
        }
        std::istringstream hs(buf.substr(0, hend));
        std::string line, target;
        std::getline(hs, line);
        std::istringstream rl(line);
        rl >> rq.method >> target;
        while (std::getline(hs, line))
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            size_t c = line.find(':');
            if (c == std::string::npos)
                continue;
            std::string v = line.substr(c + 1);
            v.erase(0, v.find_first_not_of(" \t"));
            rq.headers[lower(line.substr(0, c))] = v;
        }
        size_t q = target.find('?');
        rq.path = url_decode(target.substr(0, q));
        if (q != std::string::npos)
            rq.query = target.substr(q + 1);
        size_t len = rq.headers.count("content-length") ? (size_t)std::strtoull(rq.headers["content-length"].c_str(), nullptr, 10) : 0;
        if (len > cfg.max_body)
            return false;
        rq.body = buf.substr(hend + 4);
        while (rq.body.size() < len)
        {
            ssize_t n = recv(fd, tmp, sizeof tmp, 0);
            if (n <= 0)
                return false;
            rq.body.append(tmp, (size_t)n);
        }
        rq.body.resize(len);
        if (rq.headers.count("cookie"))
        {
            std::string c = rq.headers["cookie"];
            size_t i = 0;
            while (i < c.size())
            {
                size_t e = c.find(';', i);
                if (e == std::string::npos)
                    e = c.size();
                std::string kv = c.substr(i, e - i);
                kv.erase(0, kv.find_first_not_of(' '));
                size_t eq = kv.find('=');
                if (eq != std::string::npos)
                    rq.cookies[kv.substr(0, eq)] = kv.substr(eq + 1);
                i = e + 1;
            }
        }
        std::string ct = rq.headers.count("content-type") ? rq.headers["content-type"] : "";
        if (ct.rfind("multipart/form-data", 0) == 0)
        {
            size_t b = ct.find("boundary=");
            if (b != std::string::npos)
                parse_multipart(rq, ct.substr(b + 9));
        }
        else
            rq.form = parse_form(rq.body);
        return true;
    }

    Response route(Request &rq)
    {
        tick();
        Session ses;
        bool in = load_session(rq, ses);
        const Session *me = in ? &ses : nullptr;
        const std::string &p = rq.path;
        if (p == "/" || p.empty())
            return home(me);
        if (p == "/signup")
            return signup(rq, me);
        if (p == "/login")
            return login(rq, me);
        if (p == "/logout")
        {
            if (me && rq.method == "POST" && csrf_ok(rq, *me))
                exec("DELETE FROM sessions WHERE token=?", {me->token});
            Response r = redirect("/");
            r.headers.push_back("Set-Cookie: kav=; Max-Age=0" + cookie_attrs());
            return r;
        }
        if (p == "/board")
            return board(me);
        if (p == "/board/new")
            return board_new(rq, me);
        if (p.rfind("/thread/", 0) == 0)
            return thread(rq, me, std::atol(p.c_str() + 8));
        if (p.rfind("/img/", 0) == 0)
            return image(p.substr(5));
        if (p == "/account")
            return account(rq, me);
        if (p == "/roster")
            return roster(me);
        if (p == "/flights")
            return flights(me);
        if (p == "/admin")
            return admin(rq, me);
        if (p == "/jobs")
            return jobs_page(rq, me);
        if (p == "/mine")
            return mine(rq, me);
        if (p.rfind("/api/v1/", 0) == 0)
            return api(rq);
        return message("Not found", "There is nothing here.", me, 404);
    }

    void send_response(int fd, const Response &r)
    {
        static const std::map<int, const char *> reason = {{200, "OK"}, {303, "See Other"}, {400, "Bad Request"}, {401, "Unauthorized"}, {403, "Forbidden"},
                                                           {404, "Not Found"}, {409, "Conflict"}, {429, "Too Many Requests"}};
        std::ostringstream o;
        o << "HTTP/1.1 " << r.status << ' ' << (reason.count(r.status) ? reason.at(r.status) : "OK") << "\r\nContent-Type: " << r.type
          << "\r\nContent-Length: " << r.body.size() << "\r\nConnection: close\r\nX-Frame-Options: DENY\r\nReferrer-Policy: same-origin\r\nCache-Control: no-store\r\n";
        for (auto &h : r.headers)
            o << h << "\r\n";
        o << "\r\n" << r.body;
        std::string s = o.str();
        size_t off = 0;
        while (off < s.size())
        {
            ssize_t n = send(fd, s.data() + off, s.size() - off, MSG_NOSIGNAL);
            if (n <= 0)
                break;
            off += (size_t)n;
        }
    }

    int make_admin(const std::string &call, const std::string &pass)
    {
        if (!valid_callsign(call) || pass.size() < 8)
        {
            std::fprintf(stderr, "callsign needs 3-20 letters/digits, password at least 8 characters\n");
            return 1;
        }
        std::string salt = random_hex(16);
        exec("DELETE FROM accounts WHERE callsign=?", {call});
        exec("INSERT INTO accounts(callsign,email,salt,hash,api_token,is_admin,created) VALUES(?,?,?,?,?,1,?)",
             {call, "", salt, pbkdf2(pass, salt), random_hex(24), std::to_string(now_s())});
        std::printf("admin %s ready\n", call.c_str());
        return 0;
    }
}

int main(int argc, char **argv)
{
    std::string admin_call, admin_pass;
    bool list_admins = false, dv_table = false;
    std::string registry = "kav/data/registry.txt";
    for (int i = 1; i < argc; ++i)
    {
        std::string a = argv[i];
        auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
        if (a == "--db") cfg.db = next();
        else if (a == "--data") cfg.data = next();
        else if (a == "--base") cfg.base = next();
        else if (a == "--site") cfg.site = next();
        else if (a == "--port") cfg.port = std::atoi(next().c_str());
        else if (a == "--secure") cfg.secure = true;
        else if (a == "--make-admin") { admin_call = next(); admin_pass = next(); }
        else if (a == "--list-admins") list_admins = true;
        else if (a == "--registry") registry = next();
        else if (a == "--dv-table") dv_table = true;
        else
        {
            std::printf("kosmosavd [--db FILE] [--data DIR] [--port N] [--base /url/prefix] [--site /kosmos] [--secure]\n"
                        "          --registry FILE   locations and ships (default kav/data/registry.txt);  --dv-table  print the delta-v table and exit\n"
                        "          --make-admin CALLSIGN PASSWORD   create or reset an administrator and exit\n");
            return a == "--help" ? 0 : 1;
        }
    }
    if (sqlite3_open(cfg.db.c_str(), &db) != SQLITE_OK)
    {
        std::fprintf(stderr, "cannot open %s\n", cfg.db.c_str());
        return 1;
    }
    exec("PRAGMA journal_mode=WAL");
    migrate();
    if (!admin_call.empty())
        return make_admin(admin_call, admin_pass);
    if (list_admins)
    {
        Stmt st("SELECT callsign FROM accounts WHERE is_admin=1");
        while (st.row())
            std::printf("%s\n", st.text(0).c_str());
        return 0;
    }

    if (!load_registry(registry))
    {
        std::fprintf(stderr, "cannot read the registry %s (use --registry FILE)\n", registry.c_str());
        return 1;
    }
    if (dv_table)
    {
        for (auto &a : locations)
            for (auto &b : locations)
                if (a.id != b.id)
                {
                    Route r = route_between(a, b);
                    std::printf("%-12s -> %-12s dv %6.2f km/s  dist %12.0f km  1000kg pays %ld\n", a.id.c_str(), b.id.c_str(), r.dv_kms, r.dist_km, job_pay(false, 1000, r));
                }
        return 0;
    }
    signal(SIGPIPE, SIG_IGN);
    int ls = socket(AF_INET, SOCK_STREAM, 0);
    int one = 1;
    setsockopt(ls, SOL_SOCKET, SO_REUSEADDR, &one, sizeof one);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // only Apache on this machine may connect
    addr.sin_port = htons((uint16_t)cfg.port);
    if (bind(ls, (sockaddr *)&addr, sizeof addr) != 0 || listen(ls, 32) != 0)
    {
        std::perror("bind");
        return 1;
    }
    std::printf("kosmosavd listening on 127.0.0.1:%d (db %s)\n", cfg.port, cfg.db.c_str());
    std::fflush(stdout);
    for (;;)
    {
        int fd = accept(ls, nullptr, nullptr);
        if (fd < 0)
            continue;
        timeval tv{5, 0};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof tv);
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof tv);
        Request rq;
        if (read_request(fd, rq))
        {
            // Apache is the only peer, so its forwarded address is the visitor's.
            auto f = rq.headers.find("x-forwarded-for");
            rq.ip = f != rq.headers.end() ? f->second.substr(0, f->second.find(',')) : "local";
            send_response(fd, route(rq));
        }
        close(fd);
    }
}
