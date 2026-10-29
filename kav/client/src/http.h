#pragma once
#include <string>

namespace kav
{
    struct HttpResult
    {
        int status = 0; // 0: the request never got an answer
        std::string body, error;
    };

    // One blocking request (libcurl). `form` is an application/x-www-form-urlencoded body for POST. The API key, when not empty, is sent as a Bearer token.
    HttpResult http_request(const std::string &method, const std::string &url, const std::string &api_key, const std::string &form);

    std::string url_encode(const std::string &s);
}
