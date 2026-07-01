#include "bsr/core.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <regex>
#include <stdexcept>
#include <system_error>

namespace bsr::core {
namespace {

const std::vector<std::string> kVideoSuffixes = {"mp4", "mkv"};
const std::vector<std::string> kSubtitleSuffixes = {"ass", "srt"};
const std::vector<std::string> kResolutionTokens = {
    "1080p", "720p", "2160p", "1920", "1080", "1280",
    "720",   "2560", "1440",  "3840", "2160"};
const std::vector<std::string> kEncodingTokens = {
    "h264",  "H264",  "x264",  "X264",  "h265",  "H265",
    "x265",  "X265",  "h.264", "H.264", "x.264", "X.264",
    "h.265", "H.265", "x.265", "X.265"};
const std::vector<std::string> kBitTokens = {"10bit", "8bit", "10p"};

std::string to_lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

bool contains(const std::vector<std::string>& values, const std::string& needle) {
    return std::find(values.begin(), values.end(), needle) != values.end();
}

std::string path_extension_lower(const std::filesystem::path& path) {
    std::string suffix = path.extension().string();
    if (!suffix.empty() && suffix.front() == '.') {
        suffix.erase(suffix.begin());
    }
    return to_lower(std::move(suffix));
}

bool is_regular_supported_file(
    const std::filesystem::path& path,
    const std::vector<std::string>& suffixes) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec)) {
        return false;
    }
    return contains(suffixes, path_extension_lower(path));
}

void ensure_file_exists(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
        throw std::filesystem::filesystem_error(
            "File does not exist",
            path,
            std::make_error_code(std::errc::no_such_file_or_directory));
    }
}

std::string path_to_utf8_string(const std::filesystem::path& path) {
    return path.u8string();
}

std::string normalize_episode_token(std::string token) {
    if (token.size() == 1 && std::isdigit(static_cast<unsigned char>(token.front()))) {
        token.insert(token.begin(), '0');
    }
    return token;
}

bool is_same_path(
    const std::filesystem::path& left,
    const std::filesystem::path& right) {
    std::error_code ec;
    if (std::filesystem::equivalent(left, right, ec)) {
        return true;
    }

    return left.lexically_normal() == right.lexically_normal();
}

void replace_all(std::string& text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return;
    }
    std::size_t pos = 0;
    while ((pos = text.find(from, pos)) != std::string::npos) {
        text.replace(pos, from.size(), to);
    }
}

std::string trim_ascii_whitespace(std::string value) {
    auto is_not_space = [](unsigned char ch) {
        return !std::isspace(ch);
    };

    const auto begin = std::find_if(value.begin(), value.end(), is_not_space);
    if (begin == value.end()) {
        return {};
    }

    const auto end = std::find_if(value.rbegin(), value.rend(), is_not_space).base();
    return std::string(begin, end);
}

std::string normalize_extra_suffix(std::string value) {
    value = trim_ascii_whitespace(std::move(value));
    while (!value.empty() && value.front() == '.') {
        value.erase(value.begin());
    }
    return value;
}

bool is_preserved_subtitle_suffix_token(const std::string& token) {
    static const std::regex kSubtitleLanguagePattern("[SsTt][Cc]");
    return token.size() <= 4 || std::regex_search(token, kSubtitleLanguagePattern);
}

std::vector<std::string> extract_preserved_language_tokens(const std::filesystem::path& subtitle_path) {
    static const std::regex kBracketLanguagePattern(R"(\[((?:CHS|CHT))\])");
    std::string stem = path_to_utf8_string(subtitle_path.stem());
    std::vector<std::string> tags;
    for (std::sregex_iterator it(stem.begin(), stem.end(), kBracketLanguagePattern), end; it != end; ++it) {
        tags.push_back((*it)[1].str());
    }
    return tags;
}

std::filesystem::path build_subtitle_name(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    const RenameOptions& options) {
    std::vector<std::string> suffix_tokens;
    std::vector<std::string> language_tokens = extract_preserved_language_tokens(subtitle_path);
    std::string filename = path_to_utf8_string(subtitle_path.filename());
    std::size_t start = 0;
    while (true) {
        std::size_t dot = filename.find('.', start);
        if (dot == std::string::npos) {
            break;
        }
        start = dot + 1;
        std::size_t next_dot = filename.find('.', start);
        std::string token = filename.substr(start, next_dot == std::string::npos
                                                       ? std::string::npos
                                                       : next_dot - start);
        if (is_preserved_subtitle_suffix_token(token)) {
            suffix_tokens.push_back(token);
        }
        if (next_dot == std::string::npos) {
            break;
        }
        start = next_dot;
    }

    if (suffix_tokens.empty()) {
        std::string extension = subtitle_path.extension().string();
        if (!extension.empty() && extension.front() == '.') {
            extension.erase(extension.begin());
        }
        if (!extension.empty()) {
            suffix_tokens.push_back(extension);
        }
    }

    suffix_tokens.insert(
        suffix_tokens.begin(),
        language_tokens.begin(),
        language_tokens.end());

    const std::string extra_suffix = normalize_extra_suffix(options.extra_suffix);
    if (!extra_suffix.empty()) {
        suffix_tokens.insert(suffix_tokens.begin(), extra_suffix);
    }

    std::string joined_suffix;
    for (std::size_t i = 0; i < suffix_tokens.size(); ++i) {
        if (i != 0) {
            joined_suffix += '.';
        }
        joined_suffix += suffix_tokens[i];
    }

    std::filesystem::path result = video_path.stem();
    result += ".";
    result += joined_suffix;
    return result;
}

template <typename MapT>
void ensure_directory_exists(const std::filesystem::path& path, const MapT&) {
    if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
        throw std::filesystem::filesystem_error(
            "Directory does not exist",
            path,
            std::make_error_code(std::errc::no_such_file_or_directory));
    }
}

std::vector<std::filesystem::path> collect_supported_files(
    const std::filesystem::path& path,
    const std::vector<std::string>& suffixes) {
    ensure_directory_exists(path, 0);

    std::vector<std::filesystem::path> files;
    for (const auto& entry : std::filesystem::directory_iterator(path)) {
        const std::filesystem::path entry_path = entry.path();
        if (is_regular_supported_file(entry_path, suffixes)) {
            files.push_back(entry_path);
        }
    }
    return files;
}

struct MatchedFiles {
    std::vector<std::string> keys;
    std::vector<std::filesystem::path> files;
};

MatchedFiles collect_matched_files(
    const std::vector<std::filesystem::path>& files,
    const std::vector<std::string>& suffixes,
    const char* no_match_message) {
    std::vector<std::vector<std::string>> matched_non_empty;
    std::vector<std::filesystem::path> files_non_empty;
    matched_non_empty.reserve(files.size());
    files_non_empty.reserve(files.size());

    for (const auto& file : files) {
        ensure_file_exists(file);
        if (!std::filesystem::is_regular_file(file)) {
            continue;
        }
        if (!contains(suffixes, path_extension_lower(file))) {
            continue;
        }

        std::vector<std::string> matches = match_numbers(path_to_utf8_string(file.stem()));
        if (!matches.empty()) {
            matched_non_empty.push_back(std::move(matches));
            files_non_empty.push_back(file);
        }
    }

    if (matched_non_empty.empty()) {
        throw std::runtime_error(no_match_message);
    }

    MatchedFiles result;
    result.keys = reduce_name(matched_non_empty);
    result.files = std::move(files_non_empty);
    return result;
}

template <typename ResultMap, typename Inserter>
ResultMap build_matched_map(
    const MatchedFiles& matched,
    Inserter&& inserter) {
    ResultMap result;
    for (std::size_t i = 0; i < matched.keys.size(); ++i) {
        inserter(result, matched.keys[i], matched.files[i]);
    }
    return result;
}

bool copy_if_needed(
    const std::filesystem::path& source,
    const std::filesystem::path& destination) {
    if (is_same_path(source, destination)) {
        return false;
    }

    std::error_code ec;
    const bool copied = std::filesystem::copy_file(
        source,
        destination,
        std::filesystem::copy_options::skip_existing,
        ec);
    if (ec) {
        throw std::filesystem::filesystem_error(
            "Failed to copy subtitle file",
            source,
            destination,
            ec);
    }
    return copied;
}

}  // namespace

std::vector<std::string> match_numbers(const std::string& text) {
    static const std::regex kBracketPattern(R"(\[(SP\d*|OVA\d*|OAD\d*|\d{2,3}V*\d*)\])");
    static const std::regex kDashPattern(R"(- ([0-9]{2}) )");
    static const std::regex kStandalonePattern(R"(^(SP\d*|OVA\d*|OAD\d*|\d{1,2}\.?[5]?)$)");
    static const std::regex kCrc32Pattern(R"([0-9a-fA-F]{8})");
    static const std::regex kWidePattern(R"(SP\d*|OVA\d*|OAD\d*|[0-9]{2}\.?[5]?)");

    std::vector<std::string> matches;
    for (std::sregex_iterator it(text.begin(), text.end(), kBracketPattern), end; it != end; ++it) {
        matches.push_back(normalize_episode_token((*it)[1].str()));
    }
    if (!matches.empty()) {
        return matches;
    }

    for (std::sregex_iterator it(text.begin(), text.end(), kDashPattern), end; it != end; ++it) {
        matches.push_back(normalize_episode_token((*it)[1].str()));
    }
    if (!matches.empty()) {
        return matches;
    }

    if (std::smatch standalone_match; std::regex_match(text, standalone_match, kStandalonePattern)) {
        return {normalize_episode_token(standalone_match[1].str())};
    }

    std::string reduced = std::regex_replace(text, kCrc32Pattern, "");
    for (const auto& token : kResolutionTokens) {
        replace_all(reduced, token, "");
    }
    for (const auto& token : kEncodingTokens) {
        replace_all(reduced, token, "");
    }
    for (const auto& token : kBitTokens) {
        replace_all(reduced, token, "");
    }

    for (std::sregex_iterator it(reduced.begin(), reduced.end(), kWidePattern), end; it != end; ++it) {
        std::string value = it->str();
        value.erase(std::remove(value.begin(), value.end(), '.'), value.end());
        matches.push_back(normalize_episode_token(value));
    }
    return matches;
}

std::vector<std::string> reduce_name(
    const std::vector<std::vector<std::string>>& terms) {
    if (terms.empty()) {
        return {};
    }
    if (terms.front().empty()) {
        return {};
    }
    if (terms.front().size() == 1) {
        std::vector<std::string> result;
        result.reserve(terms.size());
        for (const auto& term : terms) {
            if (term.empty()) {
                throw std::invalid_argument("Mismatched empty term in reduce_name");
            }
            result.push_back(term.front());
        }
        return result;
    }

    std::size_t min_length = terms.front().size();
    for (const auto& term : terms) {
        min_length = std::min(min_length, term.size());
    }
    if (min_length == 0) {
        return {};
    }

    std::vector<int> scores(min_length, 0);
    for (std::size_t column = 0; column < min_length; ++column) {
        for (std::size_t i = 0; i < terms.size(); ++i) {
            for (std::size_t j = i + 1; j < terms.size(); ++j) {
                if (terms[i][column] == terms[j][column]) {
                    ++scores[column];
                }
            }
        }
    }

    auto min_it = std::min_element(scores.begin(), scores.end());
    std::size_t index = static_cast<std::size_t>(std::distance(scores.begin(), min_it));

    std::vector<std::string> result;
    result.reserve(terms.size());
    for (const auto& term : terms) {
        result.push_back(term[index]);
    }
    return result;
}

std::vector<std::string> name_reducer(const std::vector<std::string>& names) {
    std::vector<std::vector<std::string>> parsed;
    parsed.reserve(names.size());
    for (const auto& name : names) {
        parsed.push_back(match_numbers(name));
    }
    return reduce_name(parsed);
}

std::vector<std::filesystem::path> list_video_files(const std::filesystem::path& path) {
    return collect_supported_files(path, kVideoSuffixes);
}

std::vector<std::filesystem::path> list_subtitle_files(const std::filesystem::path& path) {
    return collect_supported_files(path, kSubtitleSuffixes);
}

VideoMap find_videos(const std::vector<std::filesystem::path>& files) {
    const MatchedFiles matched =
        collect_matched_files(files, kVideoSuffixes, "No matched numbers found in video file names.");
    return build_matched_map<VideoMap>(
        matched,
        [](VideoMap& result, const std::string& key, const std::filesystem::path& file) {
            auto it = result.find(key);
            if (it != result.end()) {
                throw std::runtime_error(
                    "Multiple video files matched the same episode key \"" + key +
                    "\": " + path_to_utf8_string(it->second.filename()) + " and " +
                    path_to_utf8_string(file.filename()));
            }
            result[key] = file;
        });
}

VideoMap find_videos(const std::filesystem::path& path) {
    return find_videos(list_video_files(path));
}

SubtitleMap find_subtitles(const std::vector<std::filesystem::path>& files) {
    const MatchedFiles matched = collect_matched_files(
        files,
        kSubtitleSuffixes,
        "No matched numbers found in subtitle file names.");
    return build_matched_map<SubtitleMap>(
        matched,
        [](SubtitleMap& result, const std::string& key, const std::filesystem::path& file) {
            result[key].push_back(file);
        });
}

SubtitleMap find_subtitles(const std::filesystem::path& path) {
    return find_subtitles(list_subtitle_files(path));
}

std::vector<RenameOperation> plan_renames(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    const RenameOptions& options) {
    VideoMap videos = find_videos(video_files);
    SubtitleMap subtitles = find_subtitles(subtitle_files);

    std::vector<RenameOperation> operations;
    for (const auto& [key, video] : videos) {
        auto subtitle_it = subtitles.find(key);
        if (subtitle_it == subtitles.end()) {
            continue;
        }
        for (const auto& subtitle : subtitle_it->second) {
            std::filesystem::path file_name = build_subtitle_name(video, subtitle, options);
            RenameOperation operation;
            operation.key = key;
            operation.video_path = video;
            operation.subtitle_path = subtitle;
            operation.renamed_subtitle_path = subtitle.parent_path() / file_name;
            if (options.will_copy) {
                operation.copied_subtitle_path = video.parent_path() / file_name;
            }
            operations.push_back(std::move(operation));
        }
    }
    return operations;
}

std::vector<RenameOperation> plan_renames(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    bool will_copy) {
    RenameOptions options;
    options.will_copy = will_copy;
    return plan_renames(video_files, subtitle_files, options);
}

std::vector<RenameOperation> plan_renames(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    const RenameOptions& options) {
    return plan_renames(
        list_video_files(video_path),
        list_subtitle_files(subtitle_path),
        options);
}

std::vector<RenameOperation> plan_renames(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    bool will_copy) {
    RenameOptions options;
    options.will_copy = will_copy;
    return plan_renames(video_path, subtitle_path, options);
}

std::vector<std::filesystem::path> rename_subtitles(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    const RenameOptions& options) {
    std::vector<RenameOperation> operations = plan_renames(video_files, subtitle_files, options);
    std::vector<std::filesystem::path> created_paths;
    created_paths.reserve(operations.size() * (options.will_copy ? 2U : 1U));

    for (const auto& operation : operations) {
        if (is_same_path(operation.subtitle_path, operation.renamed_subtitle_path)) {
            created_paths.push_back(operation.renamed_subtitle_path);
        } else if (copy_if_needed(operation.subtitle_path, operation.renamed_subtitle_path)) {
            created_paths.push_back(operation.renamed_subtitle_path);
        }

        if (options.will_copy && !operation.copied_subtitle_path.empty()) {
            if (copy_if_needed(operation.subtitle_path, operation.copied_subtitle_path)) {
                created_paths.push_back(operation.copied_subtitle_path);
            }
        }
    }

    return created_paths;
}

std::vector<std::filesystem::path> rename_subtitles(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    bool will_copy) {
    RenameOptions options;
    options.will_copy = will_copy;
    return rename_subtitles(video_files, subtitle_files, options);
}

std::vector<std::filesystem::path> rename_subtitles(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    const RenameOptions& options) {
    return rename_subtitles(
        list_video_files(video_path),
        list_subtitle_files(subtitle_path),
        options);
}

std::vector<std::filesystem::path> rename_subtitles(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    bool will_copy) {
    RenameOptions options;
    options.will_copy = will_copy;
    return rename_subtitles(video_path, subtitle_path, options);
}

}  // namespace bsr::core
