#include "config.h"

#ifdef _WIN32
#include <windows.h>
#include <shlobj.h>
#endif

#include <fstream>
#include <sstream>
#include <iomanip>

// Simple JSON serialization implementation
// NOTE: a full JSON library would be preferable, but this is
// sufficient for the fixed-format config.json used here.
class SimpleJSON {
    public:
        static std::string EscapeString(const std::string& s) {
            std::string result;
            for (char c : s) {
                switch (c) {
                    case '"': result += "\\\""; break;
                    case '\\': result += "\\\\"; break;
                    case '\b': result += "\\b"; break;
                    case '\f': result += "\\f"; break;
                    case '\n': result += "\\n"; break;
                    case '\r': result += "\\r"; break;
                    case '\t': result += "\\t"; break;
                    default: result += c;
                }
            }
            return result;
        }

        static std::string FormatFloat(float f, int precision = 2) {
            std::ostringstream ss;
            ss << std::fixed << std::setprecision(precision) << f;
            std::string result = ss.str();
            // Trim trailing zeros
            while (result.length() > 1 && result.find('.') != std::string::npos &&
                   (result.back() == '0' || result.back() == '.')) {
                if (result.back() == '.') {
                    result.pop_back();
                    break;
                }
                result.pop_back();
            }
            return result;
        }
};

Config& Config::Instance() {
    static Config instance;
    return instance;
}

std::string Config::GetConfigPath() const {
    return "config.json";
}

void Config::ResetToDefaults() {
    std::lock_guard<std::mutex> lock(mtx);
    island = IslandConfig();
    appearance = AppearanceConfig();
    system = SystemConfig();
    behavior = BehaviorConfig();
}

bool Config::Load() {
    std::lock_guard<std::mutex> lock(mtx);

    std::string path = GetConfigPath();
    std::ifstream file(path);
    if (!file.is_open()) {
        // Config file doesn't exist — use defaults
        return false;
    }

    try {
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        file.close();

        // Simple JSON parser for the known config structure

        // Parse "island"
        size_t island_pos = content.find("\"island\":");
        if (island_pos != std::string::npos) {
            size_t start = content.find("{", island_pos);
            size_t end = content.find("}", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string island_content = content.substr(start + 1, end - start - 1);

                // position
                size_t pos_pos = island_content.find("\"position\":");
                if (pos_pos != std::string::npos) {
                    size_t quote_start = island_content.find("\"", pos_pos + 10);
                    size_t quote_end = island_content.find("\"", quote_start + 1);
                    if (quote_start != std::string::npos && quote_end != std::string::npos) {
                        island.position = island_content.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }

                // offset_x
                size_t offset_x_pos = island_content.find("\"offset_x\":");
                if (offset_x_pos != std::string::npos) {
                    size_t value_start = island_content.find_first_of("0123456789", offset_x_pos + 10);
                    size_t value_end = island_content.find_first_of(",}", value_start);
                    if (value_start != std::string::npos && value_end != std::string::npos) {
                        island.offset_x = std::stoi(island_content.substr(value_start, value_end - value_start));
                    }
                }

                // offset_y
                size_t offset_y_pos = island_content.find("\"offset_y\":");
                if (offset_y_pos != std::string::npos) {
                    size_t value_start = island_content.find_first_of("0123456789", offset_y_pos + 10);
                    size_t value_end = island_content.find_first_of(",}", value_start);
                    if (value_start != std::string::npos && value_end != std::string::npos) {
                        island.offset_y = std::stoi(island_content.substr(value_start, value_end - value_start));
                    }
                }
            }
        }

        // Parse "appearance"
        size_t appearance_pos = content.find("\"appearance\":");
        if (appearance_pos != std::string::npos) {
            size_t start = content.find("{", appearance_pos);
            size_t end = content.find("}", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string appearance_content = content.substr(start + 1, end - start - 1);

                // theme
                size_t theme_pos = appearance_content.find("\"theme\":");
                if (theme_pos != std::string::npos) {
                    size_t quote_start = appearance_content.find("\"", theme_pos + 8);
                    size_t quote_end = appearance_content.find("\"", quote_start + 1);
                    if (quote_start != std::string::npos && quote_end != std::string::npos) {
                        appearance.theme = appearance_content.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }

                // opacity
                size_t opacity_pos = appearance_content.find("\"opacity\":");
                if (opacity_pos != std::string::npos) {
                    size_t value_start = appearance_content.find_first_of("0123456789.", opacity_pos + 10);
                    size_t value_end = appearance_content.find_first_of(",}", value_start);
                    if (value_start != std::string::npos && value_end != std::string::npos) {
                        appearance.opacity = std::stof(appearance_content.substr(value_start, value_end - value_start));
                    }
                }

                // style
                size_t style_pos = appearance_content.find("\"style\":");
                if (style_pos != std::string::npos) {
                    size_t quote_start = appearance_content.find("\"", style_pos + 8);
                    size_t quote_end = appearance_content.find("\"", quote_start + 1);
                    if (quote_start != std::string::npos && quote_end != std::string::npos) {
                        appearance.style = appearance_content.substr(quote_start + 1, quote_end - quote_start - 1);
                    }
                }
            }
        }

        // Parse "behavior"
        size_t behavior_pos = content.find("\"behavior\":");
        if (behavior_pos != std::string::npos) {
            size_t start = content.find("{", behavior_pos);
            size_t end = content.find("}", start);
            if (start != std::string::npos && end != std::string::npos) {
                std::string behavior_content = content.substr(start + 1, end - start - 1);

                // start_with_windows
                size_t start_with_windows_pos = behavior_content.find("\"start_with_windows\":");
                if (start_with_windows_pos != std::string::npos) {
                    size_t value_start = start_with_windows_pos + 21;
                    size_t value_end = behavior_content.find_first_of(",}", value_start);
                    if (value_end != std::string::npos) {
                        std::string value = behavior_content.substr(value_start, value_end - value_start);
                        behavior.start_with_windows = (value == "true");
                    }
                }

                // start_minimized
                size_t start_minimized_pos = behavior_content.find("\"start_minimized\":");
                if (start_minimized_pos != std::string::npos) {
                    size_t value_start = start_minimized_pos + 20;
                    size_t value_end = behavior_content.find_first_of(",}", value_start);
                    if (value_end != std::string::npos) {
                        std::string value = behavior_content.substr(value_start, value_end - value_start);
                        behavior.start_minimized = (value == "true");
                    }
                }
            }
        }
    } catch (const std::exception& e) {
        // Parse error — use defaults
        return false;
    }

    return true;
}

bool Config::Save() {
    std::lock_guard<std::mutex> lock(mtx);

    std::string path = GetConfigPath();
    std::ofstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        file << "{\n";

        // island
        file << "  \"island\": {\n";
        file << "    \"position\": \"" << SimpleJSON::EscapeString(island.position) << "\",\n";
        file << "    \"offset_x\": " << island.offset_x << ",\n";
        file << "    \"offset_y\": " << island.offset_y << ",\n";
        file << "    \"idle_width\": " << island.idle_width << ",\n";
        file << "    \"idle_height\": " << island.idle_height << ",\n";
        file << "    \"expanded_width\": " << island.expanded_width << ",\n";
        file << "    \"expanded_height\": " << island.expanded_height << ",\n";
        file << "    \"animation_speed\": " << SimpleJSON::FormatFloat(island.animation_speed) << ",\n";
        file << "    \"auto_hide_delay\": " << SimpleJSON::FormatFloat(island.auto_hide_delay) << ",\n";
        file << "    \"show_seconds\": " << (island.show_seconds ? "true" : "false") << "\n";
        file << "  },\n";

        // appearance
        file << "  \"appearance\": {\n";
        file << "    \"theme\": \"" << SimpleJSON::EscapeString(appearance.theme) << "\",\n";
        file << "    \"accent_color\": \"" << SimpleJSON::EscapeString(appearance.accent_color) << "\",\n";
        file << "    \"opacity\": " << SimpleJSON::FormatFloat(appearance.opacity) << ",\n";
        file << "    \"corner_radius\": " << SimpleJSON::FormatFloat(appearance.corner_radius) << ",\n";
        file << "    \"shadow_enabled\": " << (appearance.shadow_enabled ? "true" : "false") << ",\n";
        file << "    \"blur_enabled\": " << (appearance.blur_enabled ? "true" : "false") << ",\n";
        file << "    \"font_size\": " << appearance.font_size << ",\n";
        file << "    \"font_family\": \"" << SimpleJSON::EscapeString(appearance.font_family) << "\",\n";
        file << "    \"style\": \"" << SimpleJSON::EscapeString(appearance.style) << "\"\n";
        file << "  },\n";

        // system
        file << "  \"system\": {\n";
        file << "    \"update_interval_ms\": " << system.update_interval_ms << ",\n";
        file << "    \"cpu_enabled\": " << (system.cpu_enabled ? "true" : "false") << ",\n";
        file << "    \"gpu_enabled\": " << (system.gpu_enabled ? "true" : "false") << ",\n";
        file << "    \"memory_enabled\": " << (system.memory_enabled ? "true" : "false") << ",\n";
        file << "    \"battery_enabled\": " << (system.battery_enabled ? "true" : "false") << ",\n";
        file << "    \"network_enabled\": " << (system.network_enabled ? "true" : "false") << "\n";
        file << "  },\n";

        // behavior
        file << "  \"behavior\": {\n";
        file << "    \"start_with_windows\": " << (behavior.start_with_windows ? "true" : "false") << ",\n";
        file << "    \"start_minimized\": " << (behavior.start_minimized ? "true" : "false") << ",\n";
        file << "    \"silent_mode\": " << (behavior.silent_mode ? "true" : "false") << ",\n";
        file << "    \"game_mode_detection\": " << (behavior.game_mode_detection ? "true" : "false") << ",\n";
        file << "    \"notification_enabled\": " << (behavior.notification_enabled ? "true" : "false") << ",\n";
        file << "    \"max_notifications\": " << behavior.max_notifications << "\n";
        file << "  }\n";

        file << "}";

        if (!file.good()) {
            return false;
        }

        file.close();
    } catch (const std::exception& e) {
        file.close();
        return false;
    }

    return true;
}
