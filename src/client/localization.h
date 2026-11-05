#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace nsync::client {

enum class UiLanguage : std::uint8_t {
    English,
    Chinese,
};

[[nodiscard]] UiLanguage parse_language(std::string_view code);
[[nodiscard]] const char* language_code(UiLanguage language);
[[nodiscard]] const char* language_label(UiLanguage language);

class Localization {
public:
    [[nodiscard]] UiLanguage language() const { return language_; }
    void set_language(UiLanguage language) { language_ = language; }

    [[nodiscard]] std::string text(std::string_view key) const;
    [[nodiscard]] std::string text_or(
        std::string_view key,
        std::string_view fallback) const;

private:
    UiLanguage language_ = UiLanguage::English;
};

} // namespace nsync::client
