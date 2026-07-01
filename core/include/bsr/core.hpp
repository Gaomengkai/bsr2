#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace bsr::core {

// A single subtitle rename/copy action matched to one video episode.
struct RenameOperation {
    std::string key;
    std::filesystem::path video_path;
    std::filesystem::path subtitle_path;
    std::filesystem::path renamed_subtitle_path;
    std::filesystem::path copied_subtitle_path;
};

// Optional behaviors that affect how renamed subtitle filenames are generated.
struct RenameOptions {
    bool will_copy = true;
    std::string extra_suffix;
};

using VideoMap = std::unordered_map<std::string, std::filesystem::path>;
using SubtitleMap = std::unordered_map<std::string, std::vector<std::filesystem::path>>;

// Extract episode-like tokens from a filename stem.
std::vector<std::string> match_numbers(const std::string& text);

// Choose the most distinctive token position from parsed episode candidates.
std::vector<std::string> reduce_name(
    const std::vector<std::vector<std::string>>& terms);

// Convenience helper that runs match_numbers + reduce_name on raw names.
std::vector<std::string> name_reducer(const std::vector<std::string>& names);

// Collect supported video files from one directory without matching them yet.
std::vector<std::filesystem::path> list_video_files(const std::filesystem::path& path);

// Collect supported subtitle files from one directory without matching them yet.
std::vector<std::filesystem::path> list_subtitle_files(const std::filesystem::path& path);

// Match one explicit set of video files by reduced episode key.
VideoMap find_videos(const std::vector<std::filesystem::path>& files);

// Scan one directory for video files and map reduced episode keys to paths.
VideoMap find_videos(const std::filesystem::path& path);

// Match one explicit set of subtitle files by reduced episode key.
SubtitleMap find_subtitles(const std::vector<std::filesystem::path>& files);

// Scan one directory for subtitle files and group them by reduced episode key.
SubtitleMap find_subtitles(const std::filesystem::path& path);

// Build rename/copy operations from explicit file selections without touching the filesystem.
std::vector<RenameOperation> plan_renames(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    const RenameOptions& options);

std::vector<RenameOperation> plan_renames(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    bool will_copy = true);

// Build rename/copy operations without touching the filesystem.
std::vector<RenameOperation> plan_renames(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    const RenameOptions& options);

std::vector<RenameOperation> plan_renames(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    bool will_copy = true);

// Execute planned subtitle copies from explicit file selections.
std::vector<std::filesystem::path> rename_subtitles(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    const RenameOptions& options);

std::vector<std::filesystem::path> rename_subtitles(
    const std::vector<std::filesystem::path>& video_files,
    const std::vector<std::filesystem::path>& subtitle_files,
    bool will_copy = true);

// Execute planned subtitle copies in the subtitle directory and optionally next to videos.
std::vector<std::filesystem::path> rename_subtitles(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    const RenameOptions& options);

std::vector<std::filesystem::path> rename_subtitles(
    const std::filesystem::path& video_path,
    const std::filesystem::path& subtitle_path,
    bool will_copy = true);

}  // namespace bsr::core
