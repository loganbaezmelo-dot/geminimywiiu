#include <coreinit/core.h>
#include <coreinit/screen.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <vpad/input.h>
#include <nn/swkbd.h>

#include <curl/curl.h>
#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <algorithm>
#include <malloc.h>

// Memory buffer for curl responses
struct MemoryStruct {
    char *memory;
    size_t size;
};

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t realsize = size * nmemb;
    struct MemoryStruct *mem = (struct MemoryStruct *)userp;

    char *ptr = (char *)realloc(mem->memory, mem->size + realsize + 1);
    if (!ptr) return 0;

    mem->memory = ptr;
    memcpy(&(mem->memory[mem->size]), contents, realsize);
    mem->size += realsize;
    mem->memory[mem->size] = 0;

    return realsize;
}

// Read API key from SD card
std::string load_api_key() {
    std::ifstream file("fs:/vol/external01/wiiu/apps/geminimywiiu/api_key.txt");
    if (!file.is_open()) {
        file.open("wiiu/apps/geminimywiiu/api_key.txt");
    }
    if (file.is_open()) {
        std::string key;
        std::getline(file, key);
        while (!key.empty() && (key.back() == '\r' || key.back() == '\n' || key.back() == ' ')) {
            key.pop_back();
        }
        return key;
    }
    return "";
}

// Quick JSON parser to pull the assistant text response
std::string extract_gemini_text(const std::string &rawJson) {
    std::string needle = "\"text\": \"";
    size_t pos = rawJson.find(needle);
    if (pos == std::string::npos) {
        return rawJson;
    }

    pos += needle.length();
    std::string result = "";
    bool escaped = false;

    for (size_t i = pos; i < rawJson.length(); ++i) {
        char c = rawJson[i];
        if (escaped) {
            if (c == 'n') result += '\n';
            else if (c == 'r') result += '\r';
            else if (c == 't') result += '\t';
            else if (c == '"') result += '"';
            else if (c == '\\') result += '\\';
            else { result += '\\'; result += c; }
            escaped = false;
        } else if (c == '\\') {
            escaped = true;
        } else if (c == '"') {
            break;
        } else {
            result += c;
        }
    }
    return result;
}

// Scrollable chat state
std::vector<std::string> g_chat_lines;
int g_scroll_offset = 0;
const int VISIBLE_LINES = 16;
const size_t LINE_WIDTH = 64;

void add_message(const std::string &sender, const std::string &text) {
    g_chat_lines.push_back("[" + sender + "]");

    std::istringstream stream(text);
    std::string rawLine;
    while (std::getline(stream, rawLine)) {
        if (!rawLine.empty() && rawLine.back() == '\r') {
            rawLine.pop_back();
        }

        if (rawLine.empty()) {
            g_chat_lines.push_back("");
            continue;
        }

        while (rawLine.length() > LINE_WIDTH) {
            size_t breakPos = rawLine.rfind(' ', LINE_WIDTH);
            if (breakPos == std::string::npos || breakPos < 20) {
                breakPos = LINE_WIDTH;
            }
            g_chat_lines.push_back("  " + rawLine.substr(0, breakPos));
            size_t nextPos = (rawLine[breakPos] == ' ') ? breakPos + 1 : breakPos;
            rawLine = rawLine.substr(nextPos);
        }
        g_chat_lines.push_back("  " + rawLine);
    }

    g_chat_lines.push_back("--------------------------------------------------");

    // Auto-scroll down to latest response
    if ((int)g_chat_lines.size() > VISIBLE_LINES) {
        g_scroll_offset = (int)g_chat_lines.size() - VISIBLE_LINES;
    } else {
        g_scroll_offset = 0;
    }
}

void render_screen(const std::string &status_bar = "") {
    WHBLogConsoleFree();
    WHBLogConsoleInit();
    WHBLogConsoleSetColor(0x000000FF);

    WHBLogPrintf("=== geminimywiiu | D-Pad: Scroll | (A): Ask | (HOME): Exit ===");

    if (!status_bar.empty()) {
        WHBLogPrintf(">> %s", status_bar.c_str());
    }

    int totalLines = (int)g_chat_lines.size();
    int start = g_scroll_offset;
    int end = std::min(totalLines, start + VISIBLE_LINES);

    for (int i = start; i < end; ++i) {
        WHBLogPrintf("%s", g_chat_lines[i].c_str());
    }

    WHBLogConsoleDraw();
}

// Bring up the official Wii U software keyboard safely
std::string get_user_prompt() {
    // 1. Release console display to prevent frame buffer collisions
    WHBLogConsoleFree();

    size_t kbdWorkSize = nn::swkbd::GetWorkMemorySize(0);
    void *kbdWorkMem = memalign(4096, kbdWorkSize);
    if (!kbdWorkMem) {
        WHBLogConsoleInit();
        return "";
    }

    nn::swkbd::CreateArg createArg;
    memset(&createArg, 0, sizeof(createArg));
    createArg.workMemory = kbdWorkMem;
    createArg.regionType = nn::swkbd::RegionType::USA;

    if (!nn::swkbd::Create(createArg)) {
        free(kbdWorkMem);
        WHBLogConsoleInit();
        return "";
    }

    nn::swkbd::AppearArg appearArg;
    memset(&appearArg, 0, sizeof(appearArg));
    appearArg.keyboardArg.configArg.keyboardMode = nn::swkbd::KeyboardMode::Full;
    appearArg.keyboardArg.configArg.languageType = nn::swkbd::LanguageType::English;

    nn::swkbd::AppearInputForm(appearArg);

    std::string resultStr = "";
    bool running = true;

    while (running && WHBProcIsRunning()) {
        VPADStatus vpad;
        VPADReadError vErr;
        VPADRead(VPAD_CHAN_0, &vpad, 1, &vErr);

        nn::swkbd::ControllerInfo cInfo;
        memset(&cInfo, 0, sizeof(cInfo));
        cInfo.vpad = &vpad;

        nn::swkbd::Calc(cInfo);

        if (nn::swkbd::IsDecideOkButton(nullptr)) {
            const char16_t *resU16 = nn::swkbd::GetInputFormString();
            if (resU16) {
                std::u16string u16str(resU16);
                for (char16_t c : u16str) {
                    if (c < 0x80) resultStr += (char)c;
                    else resultStr += '?';
                }
            }
            running = false;
        } else if (nn::swkbd::IsDecideCancelButton(nullptr)) {
            running = false;
        }

        nn::swkbd::DrawTV();
        nn::swkbd::DrawDRC();

        OSScreenFlipBuffersEx(SCREEN_TV);
        OSScreenFlipBuffersEx(SCREEN_DRC);
    }

    nn::swkbd::DisappearInputForm();
    nn::swkbd::Destroy();
    free(kbdWorkMem);

    // 2. Re-establish console display
    WHBLogConsoleInit();
    WHBLogConsoleSetColor(0x000000FF);

    return resultStr;
}

int main(int argc, char **argv) {
    WHBProcInit();
    WHBLogConsoleInit();
    WHBLogConsoleSetColor(0x000000FF);

    std::string apiKey = load_api_key();
    if (apiKey.empty()) {
        add_message("System", "Warning: api_key.txt not found! Put it at sd:/wiiu/apps/geminimywiiu/api_key.txt");
    } else {
        add_message("System", "Ready! Press (A) to ask Gemini 3.5 Flash Lite.");
    }
    render_screen();

    curl_global_init(CURL_GLOBAL_DEFAULT);

    while (WHBProcIsRunning()) {
        VPADStatus status;
        VPADReadError error;
        VPADRead(VPAD_CHAN_0, &status, 1, &error);

        bool needs_render = false;

        // D-Pad Up: Scroll back up
        if ((status.hold & VPAD_BUTTON_UP) && g_scroll_offset > 0) {
            g_scroll_offset--;
            needs_render = true;
        }

        // D-Pad Down: Scroll down toward latest
        if ((status.hold & VPAD_BUTTON_DOWN) && (g_scroll_offset + VISIBLE_LINES < (int)g_chat_lines.size())) {
            g_scroll_offset++;
            needs_render = true;
        }

        if (needs_render) {
            render_screen();
            usleep(30000);
            continue;
        }

        // Tap (A) on GamePad to trigger keyboard
        if (status.trigger & VPAD_BUTTON_A) {
            render_screen("Opening Keyboard on GamePad...");

            std::string userPrompt = get_user_prompt();
            if (userPrompt.empty()) {
                render_screen("Cancelled.");
                continue;
            }

            add_message("You", userPrompt);
            render_screen("Thinking (Querying Gemini 3.5 Flash Lite)...");

            if (apiKey.empty()) {
                apiKey = load_api_key();
                if (apiKey.empty()) {
                    add_message("System", "Error: Missing API Key in api_key.txt!");
                    render_screen();
                    continue;
                }
            }

            std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent?key=" + apiKey;

            std::string cleanPrompt = "";
            for (char c : userPrompt) {
                if (c == '"') cleanPrompt += "\\\"";
                else if (c == '\n') cleanPrompt += "\\n";
                else cleanPrompt += c;
            }
            std::string jsonData = "{\"contents\":[{\"parts\":[{\"text\":\"" + cleanPrompt + "\"}]}]}";

            CURL *curl = curl_easy_init();
            if (curl) {
                struct MemoryStruct chunk;
                chunk.memory = (char *)malloc(1);
                chunk.size = 0;

                struct curl_slist *headers = NULL;
                headers = curl_slist_append(headers, "Content-Type: application/json");

                curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonData.c_str());
                curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
                curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
                curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

                CURLcode res = curl_easy_perform(curl);
                if (res != CURLE_OK) {
                    add_message("Error", curl_easy_strerror(res));
                } else {
                    std::string parsedText = extract_gemini_text(chunk.memory);
                    add_message("Gemini", parsedText);
                }

                curl_slist_free_all(headers);
                free(chunk.memory);
                curl_easy_cleanup(curl);
            }

            render_screen();
        }

        usleep(40000);
    }

    curl_global_cleanup();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
