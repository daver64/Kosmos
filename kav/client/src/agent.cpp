#include "agent.h"

#include "http.h"
#include "json.h"

#include <algorithm>

namespace kav
{
    namespace
    {
        Job read_job(const Json &j)
        {
            Job b;
            b.id = j["id"].integer();
            b.from = j["from"].str();
            b.to = j["to"].str();
            b.kind = j["kind"].str();
            b.qty = j["qty"].integer();
            b.pay = j["pay"].integer();
            b.dv = j["dv_kms"].num();
            b.deadline = j["deadline"].integer();
            return b;
        }
    }

    std::string Agent::location_name(const std::string &id) const
    {
        for (auto &l : locations_)
            if (l.id == id)
                return l.name;
        return id;
    }

    std::string Agent::ship_name(const std::string &id) const
    {
        for (auto &s : ships_)
            if (s.id == id)
                return s.name;
        return id;
    }

    bool Agent::load_registry()
    {
        HttpResult r = http_request("GET", cfg_.server + "/api/v1/registry", "", "");
        Json j;
        if (r.status != 200 || !parse_json(r.body, j))
        {
            error_ = r.status ? "The server answered " + std::to_string(r.status) + " for the registry." : "Cannot reach the server: " + r.error;
            ok_ = false;
            return false;
        }
        locations_.clear();
        ships_.clear();
        for (auto &l : j["locations"].a)
            locations_.push_back({l["id"].str(), l["name"].str(), l["body"].str()});
        for (auto &s : j["ships"].a)
            ships_.push_back({s["id"].str(), s["name"].str(), s["cargo_kg"].integer(), s["seats"].integer(), s["max_dv_kms"].integer(), s["fee"].integer()});
        ok_ = true;
        return true;
    }

    bool Agent::fetch_me(Me &out)
    {
        HttpResult r = http_request("GET", cfg_.server + "/api/v1/me", cfg_.key, "");
        Json j;
        if (r.status == 401)
        {
            error_ = "The server does not know that API key. Copy it again from your account page.";
            ok_ = false;
            return false;
        }
        if (r.status != 200 || !parse_json(r.body, j))
        {
            error_ = r.status ? "The server answered " + std::to_string(r.status) + "." : "Cannot reach the server: " + r.error;
            ok_ = false;
            return false;
        }
        out = Me{};
        out.callsign = j["callsign"].str();
        out.credits = j["credits"].integer();
        out.location = j["location"].str();
        out.ship = j["ship"].str();
        for (auto &b : j["jobs"].a)
            out.jobs.push_back(read_job(b));
        me_ = out;
        ok_ = true;
        return true;
    }

    bool Agent::fetch_jobs(const std::string &from, std::vector<Job> &out)
    {
        HttpResult r = http_request("GET", cfg_.server + "/api/v1/jobs" + (from.empty() ? "" : "?from=" + url_encode(from)), "", "");
        Json j;
        if (r.status != 200 || !parse_json(r.body, j))
        {
            error_ = r.status ? "The server answered " + std::to_string(r.status) + "." : "Cannot reach the server: " + r.error;
            return false;
        }
        out.clear();
        for (auto &b : j["jobs"].a)
            out.push_back(read_job(b));
        return true;
    }

    std::string Agent::post(const std::string &path, const std::string &form, long *credits)
    {
        HttpResult r = http_request("POST", cfg_.server + path, cfg_.key, form);
        Json j;
        if (r.status == 0)
            return "Cannot reach the server: " + r.error;
        if (!parse_json(r.body, j))
            return "The server answered " + std::to_string(r.status) + ".";
        if (r.status != 200)
            return j["error"].str("The server answered " + std::to_string(r.status) + ".");
        if (credits)
            *credits = j["credits"].integer();
        return "";
    }

    std::string Agent::accept(long id, const std::string &ship) { return post("/api/v1/jobs/accept", "id=" + std::to_string(id) + "&ship=" + url_encode(ship)); }
    std::string Agent::abandon(long id) { return post("/api/v1/jobs/abandon", "id=" + std::to_string(id)); }

    std::vector<std::string> Agent::tick(long now)
    {
        std::vector<std::string> notes;
        state_ = game_ ? game_->read() : GameState{};
        if (!state_.connected)
            return notes;

        std::string ship;
        for (auto &s : ships_)
            if (s.name == state_.ship_name)
                ship = s.id;
        std::string where;
        if (state_.landed)
            for (auto &l : locations_)
                if (l.id == state_.base_id)
                    where = l.id;

        bool changed = ship != last_ship_ || where != last_location_;
        if (changed || now - last_beat_ >= 30)
        {
            std::string jobs;
            for (auto &j : me_.jobs)
                jobs += (jobs.empty() ? "" : ",") + std::to_string(j.id);
            std::string err = post("/api/v1/heartbeat", "ship=" + url_encode(ship.empty() ? state_.ship_name : ship) + "&location=" + url_encode(where) + "&job=" + url_encode(jobs));
            ok_ = err.empty();
            if (!ok_)
                error_ = err;
            last_beat_ = now;
            last_ship_ = ship;
            last_location_ = where;
            if (ok_ && (now - last_me_ >= 30 || changed))
            {
                fetch_me(me_);
                last_me_ = now;
            }
        }

        // Deliveries: landed at a job's destination in the ship it was rented for. The server checks it all again.
        if (ok_ && !where.empty())
            for (auto j : me_.jobs)
            {
                if (j.to != where)
                    continue;
                auto t = std::find_if(tried_.begin(), tried_.end(), [&](auto &p) { return p.first == j.id; });
                if (t != tried_.end() && now - t->second < 10)
                    continue;
                if (t == tried_.end())
                    tried_.push_back({j.id, now});
                else
                    t->second = now;
                long credits = 0;
                std::string err = post("/api/v1/jobs/complete", "id=" + std::to_string(j.id), &credits);
                if (err.empty())
                {
                    std::string text = "Delivered job " + std::to_string(j.id) + " to " + location_name(j.to) + ": +" + std::to_string(j.pay) + " credits.";
                    notes.push_back(text);
                    if (game_)
                        game_->notify(text);
                    fetch_me(me_);
                    last_me_ = now;
                    break; // me_.jobs changed under the loop
                }
                if (err.find("too quick") == std::string::npos && err != last_complete_error_)
                    notes.push_back("Job " + std::to_string(j.id) + ": " + err);
                last_complete_error_ = err;
            }
        return notes;
    }
}
