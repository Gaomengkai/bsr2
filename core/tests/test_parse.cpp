#include "bsr/core.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path path;

    static fs::path make_unique_path() {
        static constexpr char kAlphabet[] = "0123456789abcdef";
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<std::size_t> dist(0, sizeof(kAlphabet) - 2);

        std::string suffix = "bsr-core-";
        for (int i = 0; i < 12; ++i) {
            suffix += kAlphabet[dist(gen)];
        }
        return fs::temp_directory_path() / suffix;
    }

    TempDir() : path(make_unique_path()) {
        fs::create_directories(path);
    }

    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

void expect(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write_text(const fs::path& path, const std::string& content) {
    std::ofstream stream(path, std::ios::binary);
    stream << content;
}

void test_match_numbers_prefers_bracket_match() {
    const auto result = bsr::core::match_numbers(
        "[XKsub&SweetSub&VCB-Studio] Sonny Boy [01][Ma10p_1080p][x265_flac].chs");
    expect(result.size() == 1 && result[0] == "01", "Bracket match failed");
}

void test_match_numbers_supports_dash_pattern() {
    const auto result = bsr::core::match_numbers(
        "Kimi wa Houkago Insomnia - 02 (Sentai 1920x1080 AVC AAC MKV) [AD9EEC62]");
    expect(result.size() == 1 && result[0] == "02", "Dash match failed");
}

void test_match_numbers_supports_single_digit_subtitle_names() {
    const auto result = bsr::core::match_numbers("1");
    expect(result.size() == 1 && result[0] == "01", "Single-digit subtitle match failed");
}

void test_reduce_name_uses_most_distinct_column() {
    const std::vector<std::vector<std::string>> terms = {
        {"86", "01", "1080"},
        {"86", "02", "1080"},
        {"86", "03", "1080"},
    };
    const auto result = bsr::core::reduce_name(terms);
    expect(result == std::vector<std::string>({"01", "02", "03"}), "reduce_name failed");
}

void test_finders_group_by_episode() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    write_text(videos / "[VCB-Studio] 86 -Eighty Six- [01][Ma10p_1080p][x265_flac_aac].mkv", "video-1");
    write_text(videos / "[VCB-Studio] 86 -Eighty Six- [02][Ma10p_1080p][x265_flac_aac].mkv", "video-2");
    write_text(subtitles / "[XKsub][86][01][CHS].ass", "sub-1");
    write_text(subtitles / "[XKsub][86][02][CHS].ass", "sub-2");

    const auto video_map = bsr::core::find_videos(videos);
    const auto subtitle_map = bsr::core::find_subtitles(subtitles);

    expect(video_map.size() == 2, "find_videos size mismatch");
    expect(subtitle_map.size() == 2, "find_subtitles size mismatch");
    expect(video_map.count("01") == 1, "Episode 01 video missing");
    expect(subtitle_map.count("02") == 1, "Episode 02 subtitle missing");
}

void test_rename_subtitles_copies_to_both_locations() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const fs::path video = videos / "[VCB-Studio] Sonny Boy [01][Ma10p_1080p][x265_flac].mkv";
    const fs::path subtitle = subtitles / "[XKsub][Sonny Boy][01][CHS].sc.ass";
    write_text(video, "video");
    write_text(subtitle, "subtitle");

    const auto created = bsr::core::rename_subtitles(videos, subtitles, true);
    const fs::path renamed_subtitle = subtitles / "[VCB-Studio] Sonny Boy [01][Ma10p_1080p][x265_flac].CHS.sc.ass";
    const fs::path copied_subtitle = videos / "[VCB-Studio] Sonny Boy [01][Ma10p_1080p][x265_flac].CHS.sc.ass";

    expect(created.size() == 2, "rename_subtitles should report both created files");
    expect(fs::exists(renamed_subtitle), "Renamed subtitle missing");
    expect(fs::exists(copied_subtitle), "Copied subtitle missing");
}

void test_rename_subtitles_silently_skips_same_name_copy() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const fs::path video = videos / "[VCB-Studio] Sonny Boy [01][Ma10p_1080p][x265_flac].mkv";
    const fs::path subtitle =
        subtitles / "[VCB-Studio] Sonny Boy [01][Ma10p_1080p][x265_flac].ass";
    write_text(video, "video");
    write_text(subtitle, "subtitle");

    const auto created = bsr::core::rename_subtitles(videos, subtitles, false);

    expect(created.size() == 1, "Expected a single reported subtitle path");
    expect(created[0] == subtitle, "Expected the same-name subtitle path to be preserved");
    expect(fs::exists(subtitle), "Original same-name subtitle should still exist");
}

void test_rename_subtitles_preserves_cht_tag() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const fs::path video = videos / "[Airota&VCB-Studio] Sagrada Reset [01][Ma10p_1080p][x265_flac].mkv";
    const fs::path subtitle = subtitles / "[Sakurato][Sagrada Reset][01][CHT].tc.ass";
    write_text(video, "video");
    write_text(subtitle, "subtitle");

    const auto created = bsr::core::rename_subtitles(videos, subtitles, false);
    const fs::path renamed_subtitle =
        subtitles / "[Airota&VCB-Studio] Sagrada Reset [01][Ma10p_1080p][x265_flac].CHT.tc.ass";

    expect(created.size() == 1, "rename_subtitles should report the subtitle-directory copy");
    expect(fs::exists(renamed_subtitle), "Renamed CHT subtitle missing");
}

void test_rename_subtitles_adds_custom_suffix() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const std::vector<std::string> episodes = {"01", "03", "05"};
    for (const auto& episode : episodes) {
        write_text(
            videos / ("Slime Taoshite 300-nen S2 - " + episode + ".mkv"),
            "video-" + episode);
        write_text(
            subtitles / ("[Sakurato] Slime Taoshite 300-nen, Shiranai Uchi ni Level Max ni Nattemashita Sono Ni [" +
                          episode + "].ass"),
            "subtitle-" + episode);
    }

    bsr::core::RenameOptions options;
    options.will_copy = false;
    options.extra_suffix = "  .CHS  ";

    const auto created = bsr::core::rename_subtitles(videos, subtitles, options);
    expect(created.size() == episodes.size(), "Custom suffix rename should report every subtitle");

    for (const auto& episode : episodes) {
        const fs::path renamed_subtitle =
            subtitles / ("Slime Taoshite 300-nen S2 - " + episode + ".CHS.ass");
        expect(fs::exists(renamed_subtitle), "Renamed subtitle with custom suffix missing");
    }
}

// Subtitle: [Sakurato] Slime Taoshite 300-nen, Shiranai Uchi ni Level Max ni Nattemashita Sono Ni [01][HEVC-10bit 1080p AAC][CHS].ass
// Video: Slime Taoshite 300-nen S2 - 01.mkv
// Expected renamed subtitle: Slime Taoshite 300-nen S2 - 01.CHS.ass
void test_generated_slime_s2_subtitles() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const std::vector<std::string> episodes = {"01", "03", "05"};
    for (const auto& episode : episodes) {
        write_text(
            videos / ("Slime Taoshite 300-nen S2 - " + episode + ".mkv"),
            "video-" + episode);
        write_text(
            subtitles / ("[Sakurato] Slime Taoshite 300-nen, Shiranai Uchi ni Level Max ni Nattemashita Sono Ni [" +
                          episode + "][HEVC-10bit 1080p AAC][CHS].ass"),
            "subtitle-" + episode);
    }

    const auto created = bsr::core::rename_subtitles(videos, subtitles, false);
    expect(created.size() == episodes.size(), "Expected one renamed subtitle per sampled episode");

    for (const auto& episode : episodes) {
        const fs::path expected = subtitles / ("Slime Taoshite 300-nen S2 - " + episode + ".CHS.ass");
        expect(fs::exists(expected), "Expected renamed sample subtitle missing");
    }
}

void test_single_digit_subtitles_match_zero_padded_videos() {
    TempDir root;
    const fs::path videos = root.path / "videos";
    const fs::path subtitles = root.path / "subtitles";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const std::vector<std::string> episodes = {"01", "02", "03"};
    for (std::size_t i = 0; i < episodes.size(); ++i) {
        write_text(
            videos / ("[VCB-S&ANK-Raws&Liuyun] Noragami [" + episodes[i] + "][Hi10p_1080p][x264_flac].mkv"),
            "video-" + episodes[i]);
        write_text(
            subtitles / (std::to_string(i + 1) + ".ass"),
            "subtitle-" + std::to_string(i + 1));
    }

    const auto created = bsr::core::rename_subtitles(videos, subtitles, false);
    expect(created.size() == episodes.size(), "Expected one renamed subtitle per Noragami episode");

    for (const auto& episode : episodes) {
        const fs::path expected =
            subtitles / ("[VCB-S&ANK-Raws&Liuyun] Noragami [" + episode + "][Hi10p_1080p][x264_flac].ass");
        expect(fs::exists(expected), "Expected renamed Noragami subtitle missing");
    }
}

void test_unicode_bangumi_names_on_windows() {
    TempDir root;
    const fs::path videos = root.path / LR"(【恋如雨止】[DMG] 恋は雨上がりのように [BDBOX][Ⅰ-Ⅱ])";
    const fs::path subtitles = root.path / LR"(subs)";
    fs::create_directories(videos);
    fs::create_directories(subtitles);

    const std::vector<std::string> episodes = {"01", "02", "03"};
    for (const auto& episode : episodes) {
        write_text(
            videos / fs::path(std::wstring(LR"(【恋如雨止】[DMG] 恋は雨上がりのように - )") +
                              std::wstring(episode.begin(), episode.end()) + L".mkv"),
            "video-" + episode);
        write_text(
            subtitles / fs::path(std::wstring(episode.begin(), episode.end()) + L".ass"),
            "subtitle-" + episode);
    }

    const auto created = bsr::core::rename_subtitles(videos, subtitles, false);
    expect(created.size() == episodes.size(), "Expected one renamed subtitle per unicode episode");

    for (const auto& episode : episodes) {
        const fs::path expected =
            subtitles / fs::path(std::wstring(LR"(【恋如雨止】[DMG] 恋は雨上がりのように - )") +
                                 std::wstring(episode.begin(), episode.end()) + L".ass");
        expect(fs::exists(expected), "Expected renamed unicode subtitle missing");
    }
}

}  // namespace

int main() {
    try {
        test_match_numbers_prefers_bracket_match();
        test_match_numbers_supports_dash_pattern();
        test_match_numbers_supports_single_digit_subtitle_names();
        test_reduce_name_uses_most_distinct_column();
        test_finders_group_by_episode();
        test_rename_subtitles_copies_to_both_locations();
        test_rename_subtitles_silently_skips_same_name_copy();
        test_rename_subtitles_preserves_cht_tag();
        test_rename_subtitles_adds_custom_suffix();
        test_generated_slime_s2_subtitles();
        test_single_digit_subtitles_match_zero_padded_videos();
        test_unicode_bangumi_names_on_windows();
    } catch (const std::exception& ex) {
        std::cerr << ex.what() << '\n';
        return 1;
    }

    return 0;
}
