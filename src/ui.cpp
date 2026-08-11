// ===================================================================
// ui.cpp — Cross-platform ImGui UI rendering
// ===================================================================

#include "ui.h"
#include "window.h"
#include "logging.h"
#include "config.h"
#include "sysinfo.h"
#include "trayicon.h"

#include "imgui.h"
#ifdef _WIN32
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"
#include <dwmapi.h>
#else
#include "imgui_impl_glfw.h"
#include "imgui_impl_opengl3.h"
#include <GLFW/glfw3.h>
#endif

#include <ctime>
#include <cstdio>
#include <algorithm>

#if USE_FILE_TRANSFER
#include "transferstation.h"
static int g_selectedFile = -1;
#endif

// === Helpers ===
static std::string FormatFileSize(uint64_t size) {
    if (size < 1024) return std::to_string(size) + " B";
    if (size < 1024*1024) return std::to_string((int)(size/1024)) + " KB";
    if (size < 1024LL*1024*1024) {
        char b[32]; snprintf(b, sizeof(b), "%.1f MB", (float)size/(1024*1024)); return b;
    }
    char b[32]; snprintf(b, sizeof(b), "%.2f GB", (float)size/(1024.0*1024*1024)); return b;
}

#ifdef _WIN32
static int GetScreenWidth() { return GetSystemMetrics(SM_CXSCREEN); }
static int GetScreenHeight() { return GetSystemMetrics(SM_CYSCREEN); }
#else
static int GetScreenWidth() {
    const GLFWvidmode* m = glfwGetVideoMode(glfwGetPrimaryMonitor());
    return m ? m->width : 1920;
}
static int GetScreenHeight() {
    const GLFWvidmode* m = glfwGetVideoMode(glfwGetPrimaryMonitor());
    return m ? m->height : 1080;
}
#endif

// === IsMouseOverIsland (Windows only) ===
#ifdef _WIN32
bool IsMouseOverIsland() {
    if (!g_islandVisible) return false;
    ImVec2 size = g_islandExpanded ? ImVec2(600,300) : ImVec2(400,80);
    int sw = GetScreenWidth();
    ImVec2 pos((sw - size.x)*0.5f, 20);
    POINT mp; GetCursorPos(&mp);
    RECT r; r.left=(int)pos.x; r.top=0; r.right=(int)(pos.x+size.x); r.bottom=(int)(size.y+30);
    return PtInRect(&r, mp);
}
#endif

// === Draw island UI ===
void DrawIslandUI(bool isMouseOver, bool isFullscreen, float animationY, float) {
    int sw = GetScreenWidth();
    ImVec2 size((float)(g_islandExpanded ? 600 : 400), (float)(g_islandExpanded ? 300 : 80));

#ifdef _WIN32
    // Windows: island rendered inside fullscreen overlay
    ImVec2 pos((sw - size.x)*0.5f, animationY);

    // Clip input region to island bounds
    POINT tl = {(LONG)pos.x, (LONG)pos.y};
    ScreenToClient((HWND)GetMainWindowHandle(), &tl);
    HRGN rgn = CreateRectRgn(tl.x, tl.y, tl.x+(LONG)size.x, tl.y+(LONG)size.y);
    SetWindowRgn((HWND)GetMainWindowHandle(), rgn, TRUE);

    ImGui::SetNextWindowPos(pos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(size);
#else
    // Linux: window IS the island; render full-window at (0,0)
    (void)sw; (void)animationY;
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(size);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
#endif

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 1.0f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, size.y * 0.5f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(0,0,0,0));

    ImGuiWindowFlags wf = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoNav |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoBackground;
#ifdef _WIN32
    // On Windows, title must be "DynamicIsland" — don't add NoTitleBar
#else
    wf |= ImGuiWindowFlags_NoTitleBar;
#endif

    bool open = true;
    if (ImGui::Begin(
#ifdef _WIN32
        "DynamicIsland"
#else
        "##IslandContent"
#endif
        , &open, wf)) {

        auto& app = g_config.GetAppearance();

        // --- Click target ---
        ImGui::SetCursorPos(ImVec2(0,0));
        ImGui::InvisibleButton("##islandClick", size);
        if (ImGui::IsItemClicked(ImGuiMouseButton_Left))
            g_islandExpanded = !g_islandExpanded;
        if (ImGui::IsItemClicked(ImGuiMouseButton_Right))
            ImGui::OpenPopup("##islandCtxMenu");

        if (ImGui::BeginPopup("##islandCtxMenu")) {
            if (ImGui::MenuItem(g_islandExpanded ? "Collapse" : "Expand"))
                g_islandExpanded = !g_islandExpanded;
            if (ImGui::MenuItem(g_islandVisible ? "Hide Island" : "Show Island"))
                g_islandVisible = !g_islandVisible;
            ImGui::Separator();
            if (ImGui::MenuItem("Settings")) g_showSettings = true;
            ImGui::Separator();
            if (ImGui::MenuItem("Exit")) g_running = false;
            ImGui::EndPopup();
        }

        // --- Background ---
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ImVec2 p0 = ImGui::GetWindowPos();
        ImU32 bg = (app.style == "white") ? IM_COL32(240,240,240,200) : IM_COL32(30,30,40,200);
        dl->AddRectFilled(p0, ImVec2(p0.x+size.x, p0.y+size.y), bg, size.y*0.5f);

        // --- Status dot ---
        float cpu = g_sysinfo.GetCpuUsage();
        ImU32 dc = (cpu<50) ? IM_COL32(0,255,0,255) : (cpu<80) ? IM_COL32(255,255,0,255) : IM_COL32(255,0,0,255);
        dl->AddCircleFilled(ImVec2(p0.x+20, p0.y+size.y*0.5f), 6, dc);

        // --- Time ---
        char tbuf[16];
        time_t now = time(nullptr); struct tm lt;
#ifdef _WIN32
        localtime_s(&lt, &now);
#else
        localtime_r(&now, &lt);
#endif
        snprintf(tbuf, sizeof(tbuf), "%02d:%02d:%02d", lt.tm_hour, lt.tm_min, lt.tm_sec);
        ImGui::SetCursorPos(ImVec2(40, 15));
        ImU32 tc = (app.style=="white") ? IM_COL32(0,200,100,255) : IM_COL32(0,255,100,255);
        ImGui::PushStyleColor(ImGuiCol_Text, tc);
        ImGui::GetFont()->Scale = 1.2f; ImGui::TextUnformatted(tbuf); ImGui::GetFont()->Scale = 1.0f;
        ImGui::PopStyleColor();

        ImU32 txt = (app.style=="white") ? IM_COL32(30,30,40,255) : IM_COL32(240,240,240,255);
        ImGui::PushStyleColor(ImGuiCol_Text, txt);

        // === Collapsed ===
        if (!g_islandExpanded) {
            ImGui::SetCursorPos(ImVec2(40, 45));
            float cu = g_sysinfo.GetCpuUsage();
            auto mm = g_sysinfo.GetMemoryInfo();
            ImGui::Text("CPU: %.1f%% | Mem: %.1f/%.1f GB", cu,
                        mm.used_bytes/(1024.f*1024*1024), mm.total_bytes/(1024.f*1024*1024));
            ImGui::SetCursorPos(ImVec2(280, 30));
            ImGui::Text("Batt: %.0f%%", g_sysinfo.GetBatteryPercent());
            auto bi = g_sysinfo.GetBatteryInfo();
            if (bi.is_plugged) { ImGui::SetCursorPos(ImVec2(280,45)); ImGui::Text("Charging"); }
#if USE_FILE_TRANSFER
            size_t fc = g_transferstation.GetFileCount();
            if (fc > 0) { ImGui::SetCursorPos(ImVec2(280,60)); ImGui::Text("%zu files | %s", fc, FormatFileSize(g_transferstation.GetTotalSize()).c_str()); }
#endif
        }
        // === Expanded ===
        else {
            float cu = g_sysinfo.GetCpuUsage();
            auto mm = g_sysinfo.GetMemoryInfo();
            auto bi = g_sysinfo.GetBatteryInfo();
            ImGui::SetCursorPos(ImVec2(40,42));
            ImGui::Text("CPU %.1f%%  |  Mem %.1f/%.1f GB  |  Batt %d%%%s",
                        cu, mm.used_bytes/(1024.f*1024*1024), mm.total_bytes/(1024.f*1024*1024),
                        bi.percent, bi.is_plugged ? " \xE2\x9A\xA1" : "");
            auto gp = g_sysinfo.GetGPUInfo();
            auto nt = g_sysinfo.GetNetworkInfo();
            if (gp.available) { ImGui::SetCursorPos(ImVec2(40,62)); ImGui::Text("GPU %.1f%% %s", gp.usage_percent, gp.name.c_str()); }
            if (nt.is_connected) { ImGui::SetCursorPos(ImVec2(340,62)); ImGui::Text("\xE2\x86\x93%.1f \xE2\x86\x91%.1f Mbps", nt.download_speed_mbps, nt.upload_speed_mbps); }

            // Separator
            ImGui::SetCursorPos(ImVec2(20,88));
            ImVec2 sp = ImGui::GetCursorScreenPos();
            dl->AddLine(ImVec2(sp.x, sp.y), ImVec2(sp.x+size.x-40, sp.y), IM_COL32(255,255,255,40), 1.0f);

#if USE_FILE_TRANSFER
            // === File Transfer Station ===
            ImGui::SetCursorPos(ImVec2(25,95));
            size_t fc = g_transferstation.GetFileCount();
            ImGui::Text("\xF0\x9F\x93\x81 File Transfer Station  \xc2\xb7  %zu files  \xc2\xb7  %s",
                        fc, FormatFileSize(g_transferstation.GetTotalSize()).c_str());

            ImGui::SetCursorPos(ImVec2(20,120));
            ImGui::BeginChild("##FileList", ImVec2(size.x-40, 130), ImGuiChildFlags_Borders);
            auto files = g_transferstation.GetFiles();
            if (files.empty()) {
                ImGui::SetCursorPos(ImVec2(120,45));
                ImGui::TextDisabled("Drop files here or click Import to add");
            } else {
                for (size_t i = 0; i < files.size(); i++) {
                    const auto& f = files[i];
                    std::string ext = f.name.substr(f.name.find_last_of('.')+1);
                    const char* icon = "\xF0\x9F\x93\x84 "; // default
                    if (ext=="jpg"||ext=="jpeg"||ext=="png"||ext=="gif"||ext=="bmp"||ext=="webp") icon="\xF0\x9F\x96\xBC ";
                    else if (ext=="mp3"||ext=="wav"||ext=="flac"||ext=="ogg") icon="\xF0\x9F\x8E\xB5 ";
                    else if (ext=="mp4"||ext=="avi"||ext=="mov"||ext=="mkv") icon="\xF0\x9F\x8E\xAC ";
                    else if (ext=="zip"||ext=="rar"||ext=="7z"||ext=="tar"||ext=="gz") icon="\xF0\x9F\x93\xA6 ";
                    else if (ext=="pdf") icon="\xF0\x9F\x93\x95 ";
                    else if (ext=="txt"||ext=="md"||ext=="log") icon="\xF0\x9F\x93\x9D ";
                    else if (ext=="cpp"||ext=="h"||ext=="py"||ext=="js"||ext=="c") icon="\xF0\x9F\x92\xBB ";

                    char lbl[512];
                    snprintf(lbl, sizeof(lbl), "%s %s##f%zu", icon, f.name.c_str(), i);

                    bool sel = ((int)i == g_selectedFile);
                    if (ImGui::Selectable(lbl, &sel, ImGuiSelectableFlags_AllowDoubleClick, ImVec2(size.x-60,0))) {
                        g_selectedFile = (int)i;
                        if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
                            g_transferstation.OpenFile(i);
                    }
                    ImGui::SameLine(size.x-200);
                    ImGui::TextDisabled("%s", FormatFileSize(f.size).c_str());
                    time_t tt = std::chrono::system_clock::to_time_t(f.added_time);
                    struct tm lt2;
#ifdef _WIN32
                    localtime_s(&lt2, &tt);
#else
                    localtime_r(&tt, &lt2);
#endif
                    char ts[32]; snprintf(ts, sizeof(ts), "%02d-%02d %02d:%02d", lt2.tm_mon+1, lt2.tm_mday, lt2.tm_hour, lt2.tm_min);
                    ImGui::SameLine(size.x-120); ImGui::TextDisabled("%s", ts);

                    char cid[64]; snprintf(cid, sizeof(cid), "##ctx%zu", i);
                    if (ImGui::BeginPopupContextItem(cid)) {
                        g_selectedFile = (int)i;
                        if (ImGui::MenuItem("Open")) g_transferstation.OpenFile(i);
                        if (ImGui::MenuItem("Delete from station")) { g_transferstation.RemoveFile(i); if (g_selectedFile>=(int)files.size()) g_selectedFile=-1; }
                        ImGui::Separator();
#ifdef _WIN32
                        if (ImGui::MenuItem("Copy path")) { /* clipboard */ }
#else
                        if (ImGui::MenuItem("Copy path"))
                            glfwSetClipboardString((GLFWwindow*)GetMainWindowHandle(), f.path.c_str());
#endif
                        ImGui::EndPopup();
                    }
                }
            }
            ImGui::EndChild();

            // Action buttons
            ImGui::SetCursorPos(ImVec2(25,260));
            if (ImGui::Button("Import Files", ImVec2(110,24))) {
#ifdef _WIN32
                OPENFILENAMEW ofn = {}; ofn.lStructSize = sizeof(ofn); ofn.Flags = OFN_ALLOWMULTISELECT | OFN_FILEMUSTEXIST | OFN_EXPLORER;
                wchar_t buf[8192] = {};
                ofn.lpstrFile = buf; ofn.nMaxFile = 8192;
                if (GetOpenFileNameW(&ofn)) {
                    wchar_t* p = buf + ofn.nFileOffset;
                    std::wstring dir(buf, ofn.nFileOffset);
                    if (*p) {
                        while (*p) {
                            std::wstring fp = dir + L"\\" + p;
                            char mb[512]; WideCharToMultiByte(CP_UTF8,0,fp.c_str(),-1,mb,512,nullptr,nullptr);
                            g_transferstation.AddFile(mb);
                            p += wcslen(p) + 1;
                        }
                    } else {
                        char mb[512]; WideCharToMultiByte(CP_UTF8,0,buf,-1,mb,512,nullptr,nullptr);
                        g_transferstation.AddFile(mb);
                    }
                }
#else
                FILE* fp = popen("zenity --file-selection --multiple --separator='\\n' --title='Select files' 2>/dev/null", "r");
                if (fp) { char p[4096]; while (fgets(p,sizeof(p),fp)) { size_t l=strlen(p); if(l&&p[l-1]=='\n')p[l-1]=0; if(l) g_transferstation.AddFile(p); } pclose(fp); }
#endif
            }
            ImGui::SameLine();
            if (ImGui::Button("Open Selected", ImVec2(110,24))) {
                if (g_selectedFile>=0 && g_selectedFile<(int)files.size()) g_transferstation.OpenFile(g_selectedFile);
            }
            ImGui::SameLine();
            if (ImGui::Button("Remove Selected", ImVec2(120,24))) {
                if (g_selectedFile>=0 && g_selectedFile<(int)files.size()) { g_transferstation.RemoveFile(g_selectedFile); g_selectedFile=-1; }
            }
            ImGui::SameLine();
            if (fc>0 && ImGui::Button("Clear All", ImVec2(90,24))) { g_transferstation.Clear(); g_selectedFile=-1; }
#endif // USE_FILE_TRANSFER
        }

        ImGui::PopStyleColor(); // text
    }
    ImGui::End();
    ImGui::PopStyleColor(); // WindowBg
    ImGui::PopStyleVar(3);
#ifndef _WIN32
    ImGui::PopStyleVar(); // WindowPadding on Linux
#endif
}

// === Draw settings window ===
void DrawSettingsWindow() {
#ifdef _WIN32
    if (!g_showSettings) return;
    int sw = GetScreenWidth(), sh = GetScreenHeight();
    ImVec2 ssz(600,400), spos((sw-ssz.x)*0.5f, (sh-ssz.y)*0.5f);
    ImGui::SetNextWindowPos(spos, ImGuiCond_Always);
    ImGui::SetNextWindowSize(ssz, ImGuiCond_Always);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, IM_COL32(40,40,50,255));
    ImGuiWindowFlags sf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
#else
    if (!g_settingsWindow) return;
    int ww, wh; glfwGetWindowSize(g_settingsWindow, &ww, &wh);
    ImGui::SetNextWindowPos(ImVec2(0,0), ImGuiCond_Always);
    ImGui::SetNextWindowSize(ImVec2((float)ww,(float)wh), ImGuiCond_Always);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    ImGuiWindowFlags sf = ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoTitleBar;
#endif

    bool open = true;
    if (ImGui::Begin(
#ifdef _WIN32
        "\xe8\xae\xbe\xe7\xbd\xae"
#else
        "##SettingsContent"
#endif
        , &open, sf)) {
        ImGui::Text("DynamicIsland Settings");
        ImGui::Separator(); ImGui::Spacing();

        static int cat = 0;
        const char* cats[] = {"General","Appearance","Notifications","File Station","Advanced","About"};

        ImGui::BeginChild("##LP", ImVec2(150,0), ImGuiChildFlags_Borders);
        for (int i=0;i<6;i++) { char l[64]; snprintf(l,sizeof(l),"%s##c%d",cats[i],i);
            if (ImGui::Selectable(l, cat==i)) cat=i;
            if (i==0 && ImGui::IsWindowAppearing()) ImGui::SetKeyboardFocusHere(); }
        ImGui::EndChild(); ImGui::SameLine();

        ImGui::BeginChild("##RP", ImVec2(0,0), ImGuiChildFlags_Borders);
        ImGui::Text("%s Settings", cats[cat]); ImGui::Separator(); ImGui::Spacing();
        switch (cat) {
        case 0: { static bool sw=g_config.GetBehavior().start_with_windows, sm=g_config.GetBehavior().start_minimized; static int rr=1000;
            if(ImGui::Checkbox("Start with system",&sw)){g_config.GetBehavior().start_with_windows=sw;g_config.Save();}
            if(ImGui::Checkbox("Start minimized",&sm)){g_config.GetBehavior().start_minimized=sm;g_config.Save();}
            ImGui::Spacing(); ImGui::Text("Refresh: %d ms",rr); ImGui::SliderInt("##rr",&rr,500,5000,"%d ms"); break; }
        case 1: { static float op=g_config.GetAppearance().opacity; static int si=(g_config.GetAppearance().style=="white")?1:0; const char* ss[]={"Dark","Light"};
            if(ImGui::SliderFloat("Opacity##op",&op,0.3f,1.0f,"%.2f")){g_config.GetAppearance().opacity=op;g_config.Save();}
            if(ImGui::Combo("Style",&si,ss,2)){g_config.GetAppearance().style=(si==1)?"white":"frosted";g_config.Save();} break; }
        case 2: { static bool ne=g_config.GetBehavior().notification_enabled; static int mn=g_config.GetBehavior().max_notifications;
            if(ImGui::Checkbox("Enable",&ne)){g_config.GetBehavior().notification_enabled=ne;g_config.Save();}
            ImGui::Spacing(); ImGui::Text("Max: %d",mn); ImGui::SliderInt("##mn",&mn,1,20); break; }
        case 3: ImGui::Text("File Transfer Station"); ImGui::BulletText("Max files: 100"); ImGui::BulletText("Max size: 1 GB"); break;
        case 4: { static bool db=true; ImGui::Checkbox("Debug console",&db); ImGui::Spacing();
            if(ImGui::Button("Reset to defaults")){g_config.ResetToDefaults();g_config.Save();} break; }
        case 5: ImGui::Text("DynamicIsland v1.0"); ImGui::Text("Cross-platform system monitor"); ImGui::Spacing(); ImGui::Separator();
            ImGui::BulletText("Real-time CPU, Memory, GPU, Network"); ImGui::BulletText("File Transfer Station"); ImGui::BulletText("Auto-start support"); break;
        }
        ImGui::EndChild();

        if (!open) g_showSettings = false;
    }
    ImGui::End();
#ifdef _WIN32
    ImGui::PopStyleColor();
#else
    ImGui::PopStyleVar();
#endif
}
