#include "shared/update_check.h"

#include "core/json.h"
#include "shared/version.h"

#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>
#endif

namespace nsync::shared {
namespace {

constexpr std::size_t kMaxManifestBytes = 256U * 1024U;

bool starts_with_ci(std::string_view text, std::string_view prefix) {
    if (text.size() < prefix.size()) {
        return false;
    }
    for (std::size_t index = 0; index < prefix.size(); ++index) {
        const char left = text[index];
        const char right = prefix[index];
        const char left_lower =
            left >= 'A' && left <= 'Z' ? static_cast<char>(left - 'A' + 'a')
                                       : left;
        const char right_lower =
            right >= 'A' && right <= 'Z' ? static_cast<char>(right - 'A' + 'a')
                                         : right;
        if (left_lower != right_lower) {
            return false;
        }
    }
    return true;
}

bool read_local_file(
    std::string_view source,
    std::string& body,
    std::string* error) {
    std::ifstream input(
        std::filesystem::path(std::string(source)),
        std::ios::binary);
    if (!input) {
        if (error != nullptr) {
            *error = "cannot open update manifest: " + std::string(source);
        }
        return false;
    }
    body.assign(
        std::istreambuf_iterator<char>(input),
        std::istreambuf_iterator<char>());
    if (input.bad()) {
        if (error != nullptr) {
            *error = "cannot read update manifest: " + std::string(source);
        }
        return false;
    }
    if (body.size() > kMaxManifestBytes) {
        if (error != nullptr) {
            *error = "update manifest too large";
        }
        return false;
    }
    return true;
}

#ifdef _WIN32
std::wstring widen(std::string_view text) {
    if (text.empty()) {
        return {};
    }
    const int length = MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        nullptr,
        0);
    if (length <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        result.data(),
        length);
    return result;
}

std::string winhttp_error(const char* step) {
    return std::string(step) + " failed (winhttp error " +
        std::to_string(GetLastError()) + ")";
}

bool fetch_http(
    std::string_view source,
    std::string& body,
    std::string* error) {
    const std::wstring url = widen(source);
    if (url.empty()) {
        if (error != nullptr) {
            *error = "invalid update URL";
        }
        return false;
    }

    URL_COMPONENTS parts{};
    parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = static_cast<DWORD>(-1);
    parts.dwUrlPathLength = static_cast<DWORD>(-1);
    parts.dwExtraInfoLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), 0, 0, &parts)) {
        if (error != nullptr) {
            *error = winhttp_error("WinHttpCrackUrl");
        }
        return false;
    }
    if (parts.dwHostNameLength == 0) {
        if (error != nullptr) {
            *error = "update URL has no host";
        }
        return false;
    }

    std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path = parts.dwUrlPathLength > 0
        ? std::wstring(parts.lpszUrlPath, parts.dwUrlPathLength)
        : L"/";
    if (parts.dwExtraInfoLength > 0) {
        path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    }
    const bool secure = parts.nScheme == INTERNET_SCHEME_HTTPS;

    struct Handle {
        HINTERNET value = nullptr;
        ~Handle() {
            if (value != nullptr) {
                WinHttpCloseHandle(value);
            }
        }
    };

    Handle session;
    session.value = WinHttpOpen(
        L"NSYNC update-check",
        WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
        WINHTTP_NO_PROXY_NAME,
        WINHTTP_NO_PROXY_BYPASS,
        0);
    if (session.value == nullptr) {
        if (error != nullptr) {
            *error = winhttp_error("WinHttpOpen");
        }
        return false;
    }
    WinHttpSetTimeouts(session.value, 3000, 3000, 3000, 5000);

    Handle connection;
    connection.value = WinHttpConnect(
        session.value,
        host.c_str(),
        parts.nPort,
        0);
    if (connection.value == nullptr) {
        if (error != nullptr) {
            *error = winhttp_error("WinHttpConnect");
        }
        return false;
    }

    Handle request;
    request.value = WinHttpOpenRequest(
        connection.value,
        L"GET",
        path.c_str(),
        nullptr,
        WINHTTP_NO_REFERER,
        WINHTTP_DEFAULT_ACCEPT_TYPES,
        secure ? WINHTTP_FLAG_SECURE : 0);
    if (request.value == nullptr) {
        if (error != nullptr) {
            *error = winhttp_error("WinHttpOpenRequest");
        }
        return false;
    }

    if (!WinHttpSendRequest(
            request.value,
            WINHTTP_NO_ADDITIONAL_HEADERS,
            0,
            WINHTTP_NO_REQUEST_DATA,
            0,
            0,
            0) ||
        !WinHttpReceiveResponse(request.value, nullptr)) {
        if (error != nullptr) {
            *error = winhttp_error("WinHttp request");
        }
        return false;
    }

    DWORD status = 0;
    DWORD status_size = sizeof(status);
    if (!WinHttpQueryHeaders(
            request.value,
            WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
            WINHTTP_HEADER_NAME_BY_INDEX,
            &status,
            &status_size,
            WINHTTP_NO_HEADER_INDEX)) {
        if (error != nullptr) {
            *error = winhttp_error("WinHttpQueryHeaders");
        }
        return false;
    }
    if (status != 200) {
        if (error != nullptr) {
            *error = "update manifest HTTP status " + std::to_string(status);
        }
        return false;
    }

    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.value, &available)) {
            if (error != nullptr) {
                *error = winhttp_error("WinHttpQueryDataAvailable");
            }
            return false;
        }
        if (available == 0) {
            break;
        }
        if (body.size() + available > kMaxManifestBytes) {
            if (error != nullptr) {
                *error = "update manifest too large";
            }
            return false;
        }
        std::string chunk(available, '\0');
        DWORD read = 0;
        if (!WinHttpReadData(
                request.value,
                chunk.data(),
                available,
                &read)) {
            if (error != nullptr) {
                *error = winhttp_error("WinHttpReadData");
            }
            return false;
        }
        body.append(chunk.data(), read);
    }
    return true;
}
#endif

bool fetch_manifest(
    std::string_view source,
    std::string& body,
    std::string* error) {
    if (starts_with_ci(source, "http://") ||
        starts_with_ci(source, "https://")) {
#ifdef _WIN32
        return fetch_http(source, body, error);
#else
        if (error != nullptr) {
            *error = "HTTP update checks are only supported on Windows";
        }
        return false;
#endif
    }
    if (starts_with_ci(source, "file://")) {
        source.remove_prefix(7);
    }
    return read_local_file(source, body, error);
}

bool read_string_field(
    const core::JsonValue& root,
    std::string_view key,
    std::string& value) {
    const core::JsonValue* field = root.find(key);
    if (field == nullptr || field->type != core::JsonValue::Type::String) {
        return false;
    }
    value = field->string_value;
    return true;
}

} // namespace

bool check_for_update(
    std::string_view manifest_source,
    std::string_view current_version,
    UpdateCheckResult& result,
    std::string* error) {
    result = {};
    result.current = std::string(current_version);
    if (!valid_version(current_version)) {
        if (error != nullptr) {
            *error = "invalid current version";
        }
        return false;
    }
    if (manifest_source.empty()) {
        if (error != nullptr) {
            *error = "update manifest source is empty";
        }
        return false;
    }

    std::string text;
    if (!fetch_manifest(manifest_source, text, error)) {
        return false;
    }
    if (text.size() >= 3 &&
        static_cast<unsigned char>(text[0]) == 0xEF &&
        static_cast<unsigned char>(text[1]) == 0xBB &&
        static_cast<unsigned char>(text[2]) == 0xBF) {
        text.erase(0, 3);
    }
    core::JsonValue root;
    if (!core::parse_json(text, root, error)) {
        return false;
    }
    if (root.type != core::JsonValue::Type::Object) {
        if (error != nullptr) {
            *error = "update manifest must be a JSON object";
        }
        return false;
    }
    if (!read_string_field(root, "version", result.latest)) {
        if (error != nullptr) {
            *error = "update manifest has no version";
        }
        return false;
    }
    if (!valid_version(result.latest)) {
        if (error != nullptr) {
            *error = "update manifest version is invalid";
        }
        return false;
    }
    read_string_field(root, "url", result.url);
    read_string_field(root, "notes", result.notes);
    result.update_available =
        compare_versions(result.latest, result.current) > 0;
    return true;
}

} // namespace nsync::shared
