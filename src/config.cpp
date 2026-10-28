#include "config.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <map>
#include <string>
#include <vector>

static std::string Trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string Lower(std::string s) {
    for (auto& c : s) c = (char)tolower((unsigned char)c);
    return s;
}

bool LoadConfig(const char* dllDir, StereoConfig& cfg) {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\rez3d.ini", dllDir);

    FILE* f = nullptr;
    fopen_s(&f, path, "r");
    if (!f) return false;

    std::map<std::string, std::string> kv;
    char line[1024];
    while (fgets(line, sizeof(line), f)) {
        std::string s = Trim(line);
        if (s.empty() || s[0] == ';' || s[0] == '#' || s[0] == '[') continue;
        size_t eq = s.find('=');
        if (eq == std::string::npos) continue;
        kv[Lower(Trim(s.substr(0, eq)))] = Trim(s.substr(eq + 1));
    }
    fclose(f);

    auto get = [&](const char* key, const std::string& def) -> std::string {
        auto it = kv.find(Lower(key));
        return it == kv.end() ? def : it->second;
    };
    auto geti = [&](const char* key, int def) { return atoi(get(key, std::to_string(def)).c_str()); };
    auto getf = [&](const char* key, float def) { return (float)atof(get(key, std::to_string(def)).c_str()); };

    cfg.format = get("format", cfg.format);
    cfg.leiaWindow = Lower(get("leiawindow", cfg.leiaWindow));
    cfg.monitor = geti("monitor", cfg.monitor);
    cfg.fullscreen = geti("fullscreen", cfg.fullscreen);
    cfg.vsync = geti("vsync", cfg.vsync);
    cfg.separation = getf("separation", cfg.separation);
    cfg.menuSeparation = getf("menuseparation", cfg.menuSeparation);
    cfg.convergence = getf("convergence", cfg.convergence);
    cfg.screenWidthMM = geti("screenwidthmm", cfg.screenWidthMM);
    cfg.framePacing = geti("framepacing", cfg.framePacing);
    cfg.gpuComposite = geti("gpucomposite", cfg.gpuComposite);
    cfg.headless = geti("headless", cfg.headless);
    cfg.unmuteOnStart = geti("unmuteonstart", cfg.unmuteOnStart);
    cfg.fovH = getf("fovh", cfg.fovH);
    cfg.renderScale = getf("renderscale", cfg.renderScale);
    cfg.interlaceFilter = getf("interlacefilter", cfg.interlaceFilter);
    cfg.crosstalk = getf("crosstalk", cfg.crosstalk);
    cfg.crosstalkLift = getf("crosstalklift", cfg.crosstalkLift);
    cfg.brightness = getf("brightness", cfg.brightness);
    cfg.gamma = getf("gamma", cfg.gamma);
    cfg.tracking = Lower(get("tracking", cfg.tracking));
    cfg.mouseSensitivity = getf("mousesensitivity", cfg.mouseSensitivity);
    cfg.invertY = geti("inverty", cfg.invertY);
    cfg.fps = geti("fps", cfg.fps);
    cfg.nearZ = getf("nearz", cfg.nearZ);
    cfg.farZ = getf("farz", cfg.farZ);
    cfg.adapter = geti("adapter", cfg.adapter);
    cfg.logFrameCalls = geti("logframecalls", cfg.logFrameCalls);
    cfg.dumpFrames = geti("dumpframes", cfg.dumpFrames);
    cfg.dumpAfter = geti("dumpafter", cfg.dumpAfter);
    cfg.dumpEvery = geti("dumpevery", cfg.dumpEvery);
    cfg.focusEvent = geti("focusevent", cfg.focusEvent);
    return true;
}

bool SaveStereoSettings(const char* dllDir, float separation, float convergence, float crosstalk) {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\rez3d.ini", dllDir);

    std::vector<std::string> lines;
    FILE* f = nullptr;
    if (fopen_s(&f, path, "r") == 0 && f) {
        char line[1024];
        while (fgets(line, sizeof(line), f)) {
            std::string s = line;
            while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) s.pop_back();
            lines.push_back(s);
        }
        fclose(f);
    }

    char sep[64], conv[64], xt[64];
    sprintf_s(sep, "Separation=%.1f", separation);
    sprintf_s(conv, "Convergence=%.3f", convergence);
    sprintf_s(xt, "Crosstalk=%.3f", crosstalk);
    bool haveSep = false, haveConv = false, haveXt = false;
    for (auto& s : lines) {
        std::string t = Trim(s);
        size_t eq = t.find('=');
        if (t.empty() || t[0] == ';' || t[0] == '#' || eq == std::string::npos) continue;
        std::string key = Lower(Trim(t.substr(0, eq)));
        if (key == "separation") {
            s = sep;
            haveSep = true;
        } else if (key == "convergence") {
            s = conv;
            haveConv = true;
        } else if (key == "crosstalk") {
            s = xt;
            haveXt = true;
        }
    }
    if (!haveSep) lines.push_back(sep);
    if (!haveConv) lines.push_back(conv);
    if (!haveXt) lines.push_back(xt);

    if (fopen_s(&f, path, "w") != 0 || !f) return false;
    for (auto& s : lines) fprintf(f, "%s\n", s.c_str());
    fclose(f);
    return true;
}
