#include "http.h"

#ifdef _WIN32
#include <windows.h>
#include <winhttp.h>
#else
#include <curl/curl.h>
#endif

#include <cctype>
#include <string>

namespace kav
{
#ifndef _WIN32
    namespace
    {
        size_t collect(char *data, size_t size, size_t n, void *user)
        {
            auto *s = static_cast<std::string *>(user);
            if (s->size() + size * n > (4u << 20))
                return 0;
            s->append(data, size * n);
            return size * n;
        }
    }
#endif

    std::string url_encode(const std::string &s)
    {
        static const char *hex = "0123456789ABCDEF";
        std::string o;
        for (unsigned char c : s)
        {
            if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~')
                o += (char)c;
            else
            {
                o += '%';
                o += hex[c >> 4];
                o += hex[c & 15];
            }
        }
        return o;
    }


#ifdef _WIN32
    namespace
    {
        std::wstring widen(const std::string &s)
        {
            if (s.empty())
                return L"";
            int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
            std::wstring w(n, L'\0');
            MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), &w[0], n);
            return w;
        }

        struct Handle
        {
            HINTERNET h;
            explicit Handle(HINTERNET x = nullptr) : h(x) {}
            ~Handle() { if (h) WinHttpCloseHandle(h); }
            Handle(const Handle &) = delete;
            Handle &operator=(const Handle &) = delete;
        };
    }

    HttpResult http_request(const std::string &method, const std::string &url, const std::string &api_key, const std::string &form)
    {
        HttpResult r;
        std::wstring wurl = widen(url);
        URL_COMPONENTS uc{};
        uc.dwStructSize = sizeof uc;
        uc.dwSchemeLength = uc.dwHostNameLength = uc.dwUrlPathLength = uc.dwExtraInfoLength = (DWORD)-1;
        if (!WinHttpCrackUrl(wurl.c_str(), 0, 0, &uc))
        {
            r.error = "bad server address";
            return r;
        }
        std::wstring host(uc.lpszHostName, uc.dwHostNameLength);
        std::wstring path(uc.lpszUrlPath, uc.dwUrlPathLength + uc.dwExtraInfoLength);
        Handle session(WinHttpOpen(L"KosmosAV", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, nullptr, nullptr, 0));
        if (!session.h)
        {
            r.error = "WinHTTP failed to start";
            return r;
        }
        WinHttpSetTimeouts(session.h, 8000, 8000, 15000, 15000);
        Handle conn(WinHttpConnect(session.h, host.c_str(), uc.nPort, 0));
        if (!conn.h)
        {
            r.error = "cannot connect";
            return r;
        }
        const bool tls = uc.nScheme == INTERNET_SCHEME_HTTPS;
        Handle req(WinHttpOpenRequest(conn.h, widen(method).c_str(), path.c_str(), nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, tls ? WINHTTP_FLAG_SECURE : 0));
        if (!req.h)
        {
            r.error = "cannot open request";
            return r;
        }
        DWORD no_redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
        WinHttpSetOption(req.h, WINHTTP_OPTION_REDIRECT_POLICY, &no_redirect, sizeof no_redirect);
        std::wstring headers;
        if (!api_key.empty())
            headers += L"Authorization: Bearer " + widen(api_key) + L"\r\n";
        if (method == "POST")
            headers += L"Content-Type: application/x-www-form-urlencoded\r\n";
        const void *data = method == "POST" && !form.empty() ? form.data() : nullptr;
        const DWORD len = data ? (DWORD)form.size() : 0;
        if (!WinHttpSendRequest(req.h, headers.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : headers.c_str(), headers.empty() ? 0 : (DWORD)-1L, (LPVOID)data, len, len, 0) || !WinHttpReceiveResponse(req.h, nullptr))
        {
            r.error = "request failed (error " + std::to_string(GetLastError()) + ")";
            return r;
        }
        DWORD code = 0, size = sizeof code;
        WinHttpQueryHeaders(req.h, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)code;
        for (;;)
        {
            DWORD avail = 0;
            if (!WinHttpQueryDataAvailable(req.h, &avail) || avail == 0)
                break;
            std::string chunk(avail, '\0');
            DWORD got = 0;
            if (!WinHttpReadData(req.h, &chunk[0], avail, &got) || got == 0)
                break;
            r.body.append(chunk, 0, got);
            if (r.body.size() > (4u << 20))
            {
                r.status = 0;
                r.error = "reply too large";
                r.body.clear();
                break;
            }
        }
        return r;
    }
#else
    HttpResult http_request(const std::string &method, const std::string &url, const std::string &api_key, const std::string &form)
    {
        HttpResult r;
        CURL *c = curl_easy_init();
        if (!c)
        {
            r.error = "curl failed to start";
            return r;
        }
        curl_slist *headers = nullptr;
        if (!api_key.empty())
            headers = curl_slist_append(headers, ("Authorization: Bearer " + api_key).c_str());
        curl_easy_setopt(c, CURLOPT_URL, url.c_str());
        curl_easy_setopt(c, CURLOPT_HTTPHEADER, headers);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION, collect);
        curl_easy_setopt(c, CURLOPT_WRITEDATA, &r.body);
        curl_easy_setopt(c, CURLOPT_TIMEOUT, 15L);
        curl_easy_setopt(c, CURLOPT_CONNECTTIMEOUT, 8L);
        curl_easy_setopt(c, CURLOPT_PROTOCOLS_STR, "http,https");
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 0L);
        if (method == "POST")
        {
            curl_easy_setopt(c, CURLOPT_POST, 1L);
            curl_easy_setopt(c, CURLOPT_POSTFIELDS, form.c_str());
            curl_easy_setopt(c, CURLOPT_POSTFIELDSIZE, (long)form.size());
        }
        CURLcode rc = curl_easy_perform(c);
        if (rc != CURLE_OK)
            r.error = curl_easy_strerror(rc);
        else
        {
            long code = 0;
            curl_easy_getinfo(c, CURLINFO_RESPONSE_CODE, &code);
            r.status = (int)code;
        }
        curl_slist_free_all(headers);
        curl_easy_cleanup(c);
        return r;
    }
#endif
}
