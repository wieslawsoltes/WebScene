#pragma once

#include <algorithm>
#include <cstdlib>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
// Do not import legacy winsock.h: callers also use IXWebSocket/winsock2.h.
#include <windows.h>
#endif

namespace webscene::embed_fallback {

// Intentionally a small provider allow-list, not a general URL redirector.
inline std::optional<std::string> youtube_video_id(std::string_view url)
{
    const auto scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) return std::nullopt;
    auto scheme = std::string(url.substr(0, scheme_end));
    const auto ascii_lower = [](unsigned char c) -> char {
        return c >= 'A' && c <= 'Z' ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
    };
    std::transform(scheme.begin(), scheme.end(), scheme.begin(), ascii_lower);
    if (scheme != "https" && scheme != "http") return std::nullopt;
    const auto path_start = url.find('/', scheme_end + 3);
    if (path_start == std::string_view::npos) return std::nullopt;
    auto authority = std::string(url.substr(scheme_end + 3, path_start - scheme_end - 3));
    std::transform(authority.begin(), authority.end(), authority.begin(), ascii_lower);
    const auto default_port = scheme == "https" ? ":443" : ":80";
    if (authority.ends_with(default_port)) authority.resize(authority.size() - std::char_traits<char>::length(default_port));
    if (authority != "youtube.com" && authority != "www.youtube.com"
        && authority != "youtube-nocookie.com" && authority != "www.youtube-nocookie.com") return std::nullopt;
    auto path = url.substr(path_start);
    path = path.substr(0, path.find_first_of("?#"));
    if (!path.starts_with("/embed/")) return std::nullopt;
    const auto id = path.substr(7);
    if (id == "videoseries" || id.size() != 11 || !std::all_of(id.begin(), id.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')
            || (c >= '0' && c <= '9') || c == '-' || c == '_';
    })) return std::nullopt;
    return std::string(id);
}

inline bool enabled()
{
#if defined(_WIN32)
    // The V8 DLL uses /MT: its private CRT environment can be stale when a
    // managed host (or another CRT) changes the process environment.
    char value[2]{};
    return GetEnvironmentVariableA("WEBSCENE_YOUTUBE_EMBED_FALLBACK", value, sizeof(value)) != 1
        || value[0] != '0';
#else
    const auto* value = std::getenv("WEBSCENE_YOUTUBE_EMBED_FALLBACK");
    return value == nullptr || std::string_view(value) != "0";
#endif
}

// A finite chain also handles YouTube's successful HTTP responses containing
// a tiny placeholder instead of the requested high-resolution thumbnail.
inline std::optional<std::string> next_thumbnail(std::string_view source)
{
    constexpr std::string_view prefix = "https://i.ytimg.com/vi/";
    if (!source.starts_with(prefix)) return std::nullopt;
    const auto path = source.substr(prefix.size());
    const auto slash = path.find('/');
    if (slash != 11 || !youtube_video_id("https://youtube.com/embed/" + std::string(path.substr(0, slash))))
        return std::nullopt;
    const auto variant = path.substr(slash + 1);
    const auto* next = variant == "maxresdefault.jpg" ? "hq720.jpg"
        : variant == "hq720.jpg" ? "hqdefault.jpg" : nullptr;
    if (!next) return std::nullopt;
    return std::string(prefix) + std::string(path.substr(0, slash + 1)) + next;
}

inline std::string escape_html(std::string_view value)
{
    std::string result;
    for (const auto c : value) {
        switch (c) {
        case '&': result += "&amp;"; break;
        case '<': result += "&lt;"; break;
        case '>': result += "&gt;"; break;
        case '"': result += "&quot;"; break;
        case '\'': result += "&#39;"; break;
        default: result += c; break;
        }
    }
    return result;
}

// This is a replacement document, never an overlay on a running player.
// Keep the link independent of image loading (offline/private/deleted videos).
inline std::optional<std::string> document(std::string_view source, std::string_view title)
{
    const auto id = youtube_video_id(source);
    if (!id) return std::nullopt;
    const auto label = escape_html(title.empty() ? "YouTube video" : title);
    return R"HTML(<!doctype html><html><head><meta charset="utf-8"><style>
html,body{margin:0;width:100%;height:100%;overflow:hidden;background:#181818;color:white}
#webscene-watch{display:block;position:relative;box-sizing:border-box;width:100%;height:100%;overflow:hidden;color:white;background:#181818;text-decoration:none;cursor:pointer;font-family:Arial,sans-serif;font-size:14px}
#webscene-watch:focus{outline:3px solid #6ca8ff;outline-offset:-3px}
img{position:absolute;left:0;top:0;width:100%;height:100%;object-fit:contain}
#webscene-title{position:absolute;left:0;right:0;top:0;padding:10px 12px;background:rgba(0,0,0,.8);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
#webscene-action{position:absolute;left:0;right:0;bottom:0;padding:12px;background:#181818;text-align:center;font-weight:700}
</style></head><body><a id="webscene-watch" target="_blank" rel="noopener noreferrer" href="https://www.youtube.com/watch?v=)HTML"
        + *id + "\" aria-label=\"Watch on YouTube: " + label + "\">"
        + "<img data-webscene-thumbnail=\"youtube\" alt=\"\" referrerpolicy=\"no-referrer\" src=\"https://i.ytimg.com/vi/" + *id + "/maxresdefault.jpg\">"
        + "<span id=\"webscene-title\">" + label + "</span>"
        + "<span id=\"webscene-action\">Watch on YouTube &#8599;</span></a></body></html>";
}

} // namespace webscene::embed_fallback
