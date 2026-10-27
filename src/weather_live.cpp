#include "weather_live.h"

#include <algorithm>
#include <chrono>
#include <cstdlib>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <set>
#include <sstream>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winhttp.h>
#elif defined(KOSMOS_HAVE_CURL)
#include <curl/curl.h>
#endif

namespace kosmos
{
    double real_unix_time()
    {
        return std::chrono::duration<double>(std::chrono::system_clock::now().time_since_epoch()).count();
    }

#if defined(_WIN32)
    bool http_available() { return true; }

    bool http_get(const std::string &url, std::string &body, int timeout_seconds)
    {
        const std::wstring wide(url.begin(), url.end());
        URL_COMPONENTS parts{};
        parts.dwStructSize = sizeof parts;
        wchar_t host[256] = {}, path[2048] = {};
        parts.lpszHostName = host;
        parts.dwHostNameLength = 255;
        parts.lpszUrlPath = path;
        parts.dwUrlPathLength = 2047;
        parts.dwExtraInfoLength = static_cast<DWORD>(-1);
        if (!WinHttpCrackUrl(wide.c_str(), 0, 0, &parts))
            return false;
        std::wstring target(parts.lpszUrlPath, parts.dwUrlPathLength);
        if (parts.lpszExtraInfo && parts.dwExtraInfoLength)
            target.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
        bool ok = false;
        HINTERNET session = WinHttpOpen(L"Kosmos", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
        if (!session)
            return false;
        const int ms = timeout_seconds * 1000;
        WinHttpSetTimeouts(session, ms, ms, ms, ms);
        if (HINTERNET connection = WinHttpConnect(session, std::wstring(parts.lpszHostName, parts.dwHostNameLength).c_str(), parts.nPort, 0))
        {
            const DWORD flags = parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0;
            if (HINTERNET request = WinHttpOpenRequest(connection, L"GET", target.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags))
            {
                if (WinHttpSendRequest(request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(request, nullptr))
                {
                    DWORD status = 0, size = sizeof status;
                    WinHttpQueryHeaders(request, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &status, &size, WINHTTP_NO_HEADER_INDEX);
                    body.clear();
                    for (;;)
                    {
                        DWORD available = 0;
                        if (!WinHttpQueryDataAvailable(request, &available) || available == 0)
                            break;
                        std::string chunk(available, '\0');
                        DWORD read = 0;
                        if (!WinHttpReadData(request, &chunk[0], available, &read) || read == 0)
                            break;
                        body.append(chunk, 0, read);
                    }
                    ok = status == 200;
                }
                WinHttpCloseHandle(request);
            }
            WinHttpCloseHandle(connection);
        }
        WinHttpCloseHandle(session);
        return ok;
    }
#elif defined(KOSMOS_HAVE_CURL)
    namespace
    {
        std::size_t collect(char *data, std::size_t size, std::size_t count, void *out)
        {
            static_cast<std::string *>(out)->append(data, size * count);
            return size * count;
        }
    }

    bool http_available() { return true; }

    bool http_get(const std::string &url, std::string &body, int timeout_seconds)
    {
        CURL *curl = curl_easy_init();
        if (!curl)
            return false;
        body.clear();
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_USERAGENT, "Kosmos");
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, static_cast<long>(timeout_seconds));
        curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &body);
        const CURLcode result = curl_easy_perform(curl);
        long status = 0;
        curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
        curl_easy_cleanup(curl);
        return result == CURLE_OK && status == 200;
    }
#else
    bool http_available() { return false; }
    bool http_get(const std::string &, std::string &, int) { return false; }
#endif

    LiveWeather::~LiveWeather()
    {
        if (worker_.joinable())
            worker_.join();
    }

    void LiveWeather::configure(std::vector<std::string> ids, const std::string &cache_file)
    {
        ids_ = std::move(ids);
        cache_ = cache_file;
        std::error_code ec;
        std::filesystem::create_directories(std::filesystem::path(cache_).parent_path(), ec);
        load_cache();
    }

    void LiveWeather::load_cache()
    {
        std::ifstream in(cache_);
        std::string line;
        if (!in || !std::getline(in, line) || line.size() < 3 || line[0] != '#')
            return;
        const double when = std::atof(line.c_str() + 1);
        std::vector<std::string> lines;
        while (std::getline(in, line))
            if (!line.empty() && line[0] != '#')
                lines.push_back(line);
        if (lines.empty())
            return;
        std::lock_guard<std::mutex> lock(mutex_);
        pending_ = std::move(lines);
        fetched_ = when;
    }

    void LiveWeather::poll(double now_unix)
    {
        if (busy_ || ids_.empty() || !http_available())
            return;
        const double age = now_unix - fetched_.load();
        if (fetched_.load() > 0.0 && age < refresh_seconds)
            return;
        if (failed_ && now_unix - last_attempt_ < retry_seconds)
            return;
        if (!failed_ && last_attempt_ > 0.0 && now_unix - last_attempt_ < retry_seconds)
            return;
        if (worker_.joinable())
            worker_.join();
        last_attempt_ = now_unix;
        busy_ = true;
        worker_ = std::thread([this, now_unix] { run(now_unix); });
    }

    void LiveWeather::run(double now_unix)
    {
        std::string url = "https://aviationweather.gov/api/data/metar?format=raw&hours=2&ids=";
        for (std::size_t i = 0; i < ids_.size(); ++i)
            url += (i ? "," : "") + ids_[i];
        std::string body;
        std::vector<std::string> lines;
        if (http_get(url, body, 30))
        {
            // Each station can appear more than once; the newest is listed first.
            std::set<std::string> seen;
            std::istringstream in(body);
            for (std::string line; std::getline(in, line);)
            {
                while (!line.empty() && (line.back() == '\r' || line.back() == ' '))
                    line.pop_back();
                std::istringstream words(line);
                std::string first, second;
                words >> first >> second;
                const std::string id = (first == "METAR" || first == "SPECI") ? second : first;
                if (id.size() == 4 && seen.insert(id).second)
                    lines.push_back(line);
            }
        }
        failed_ = lines.empty();
        if (!lines.empty())
        {
            const std::string temp = cache_ + ".tmp";
            {
                std::ofstream out(temp);
                out << "# " << static_cast<long long>(now_unix) << "\n";
                for (const std::string &line : lines)
                    out << line << "\n";
            }
            std::error_code ec;
            std::filesystem::rename(temp, cache_, ec);
            std::lock_guard<std::mutex> lock(mutex_);
            pending_ = std::move(lines);
            fetched_ = now_unix;
        }
        busy_ = false;
    }

    bool LiveWeather::take(std::vector<std::string> &lines)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (pending_.empty())
            return false;
        lines = std::move(pending_);
        pending_.clear();
        return true;
    }

    std::string LiveWeather::status(double now_unix) const
    {
        if (!http_available())
            return "no web support in this build";
        if (busy_)
            return "fetching...";
        const double when = fetched_.load();
        if (when <= 0.0)
            return failed_ ? "unavailable, retrying" : "waiting to fetch";
        const std::time_t t = static_cast<std::time_t>(when);
        std::tm utc{};
#if defined(_WIN32)
        gmtime_s(&utc, &t);
#else
        gmtime_r(&t, &utc);
#endif
        char buffer[96];
        const int next = std::max(0, static_cast<int>((refresh_seconds - (now_unix - when)) / 60.0));
        std::snprintf(buffer, sizeof buffer, "updated %02d:%02dZ, next in %d min", utc.tm_hour, utc.tm_min, next);
        return buffer;
    }
}
