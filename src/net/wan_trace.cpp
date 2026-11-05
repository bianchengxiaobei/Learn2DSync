#include "net/wan_trace.h"

#include <cerrno>
#include <climits>
#include <cstdlib>
#include <fstream>
#include <string_view>

namespace nsync::net {
namespace {

std::string_view trim(std::string_view text) {
    while (!text.empty() &&
           (text.front() == ' ' || text.front() == '\t' ||
            text.front() == '\r' || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (!text.empty() &&
           (text.back() == ' ' || text.back() == '\t' ||
            text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

bool parse_u32(std::string_view text, std::uint32_t& value) {
    text = trim(text);
    if (text.empty()) {
        return false;
    }
    std::string owned(text);
    char* end = nullptr;
    errno = 0;
    const unsigned long parsed = std::strtoul(owned.c_str(), &end, 10);
    if (errno != 0 || end == owned.c_str() || *end != '\0' ||
        parsed > UINT_MAX) {
        return false;
    }
    value = static_cast<std::uint32_t>(parsed);
    return true;
}

} // namespace

bool load_wan_trace(
    const std::string& path,
    std::vector<WanTraceSample>& samples,
    std::string* error) {
    samples.clear();
    std::ifstream input(path.c_str());
    if (!input) {
        if (error) {
            *error = "cannot open WAN trace: " + path;
        }
        return false;
    }

    std::string line;
    std::uint32_t line_number = 0;
    while (std::getline(input, line)) {
        ++line_number;
        std::string_view view(line);
        if (line_number == 1 && view.size() >= 3 &&
            static_cast<unsigned char>(view[0]) == 0xEF &&
            static_cast<unsigned char>(view[1]) == 0xBB &&
            static_cast<unsigned char>(view[2]) == 0xBF) {
            view.remove_prefix(3);
        }
        view = trim(view);
        if (view.empty() || view.front() == '#') {
            continue;
        }
        if (view.front() == 'a' || view.front() == 'A') {
            continue;
        }

        std::string_view fields[4];
        std::size_t field_count = 0;
        std::size_t start = 0;
        while (field_count < 4) {
            const std::size_t comma = view.find(',', start);
            fields[field_count++] = trim(view.substr(
                start,
                comma == std::string_view::npos
                    ? std::string_view::npos
                    : comma - start));
            if (comma == std::string_view::npos) {
                start = view.size();
                break;
            }
            start = comma + 1;
        }
        if (field_count != 4 || start < view.size()) {
            if (error) {
                *error = "WAN trace line " + std::to_string(line_number) +
                    ": expected at_ms,latency_ms,loss_percent,duplicate_percent";
            }
            return false;
        }

        WanTraceSample sample;
        if (!parse_u32(fields[0], sample.at_ms) ||
            !parse_u32(fields[1], sample.latency_ms) ||
            !parse_u32(fields[2], sample.loss_percent) ||
            !parse_u32(fields[3], sample.duplicate_percent)) {
            if (error) {
                *error = "WAN trace line " + std::to_string(line_number) +
                    ": invalid unsigned integer";
            }
            return false;
        }
        if (sample.latency_ms > 5000U || sample.loss_percent > 100U ||
            sample.duplicate_percent > 100U) {
            if (error) {
                *error = "WAN trace line " + std::to_string(line_number) +
                    ": latency/loss/duplicate out of range";
            }
            return false;
        }
        if (!samples.empty() && sample.at_ms <= samples.back().at_ms) {
            if (error) {
                *error = "WAN trace line " + std::to_string(line_number) +
                    ": at_ms must be strictly increasing";
            }
            return false;
        }
        if (samples.empty() && sample.at_ms != 0U) {
            if (error) {
                *error = "WAN trace first sample must start at 0 ms";
            }
            return false;
        }
        samples.push_back(sample);
    }

    if (samples.empty()) {
        if (error) {
            *error = "WAN trace has no samples: " + path;
        }
        return false;
    }
    return true;
}

} // namespace nsync::net
