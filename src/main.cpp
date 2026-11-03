#include <coreinit/core.h>
#include <coreinit/screen.h>
#include <coreinit/time.h>
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

// Bring up the official Wii U software keyboard
std::string get_user_prompt() {
    size_t kbdWorkSize = nn::swkbd::GetWorkMemorySize(0);
    void *kbdWorkMem = malloc(kbdWorkSize);
    if (!kbdWorkMem) return "";

    nn::swkbd::CreateArg createArg;
    createArg.workMemory = kbdWorkMem;
    createArg.regionType = nn::swkbd::RegionType::USA;
    nn::swkbd::Create(createArg);

    nn::swkbd::AppearArg appearArg;
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
        cInfo.vpad = &vpad;

        nn::swkbd::Calc(cInfo);

        // When user taps OK / Enter on the GamePad
        if (nn::swkbd::IsDecideOkButton(nullptr)) {
            const char16_t *resU16 = nn::swkbd::GetInputFormString();
            if (resU16) {
                std::u16string u16str(resU16);
                for (char16_t c : u16str) {
                    if (c < 0x80) {
                        resultStr += (char)c;
                    } else {
                        resultStr += '?';
                    }
                }
            }
            running = false;
        } else if (nn::swkbd::IsDecideCancelButton(nullptr)) {
            running = false;
        }

        nn::swkbd::DrawTV();
        nn::swkbd::DrawDRC();
    }

    nn::swkbd::DisappearInputForm();
    nn::swkbd::Destroy();
    free(kbdWorkMem);

    return resultStr;
}

int main(int argc, char **argv) {
    WHBProcInit();
    WHBLogConsoleInit();

    WHBLogConsoleSetColor(0x000000FF);
    WHBLogPrintf("=== geminimywiiu ===");
    WHBLogPrintf("Press (A) on GamePad to ask Gemini");
    WHBLogPrintf("Press (HOME) to exit");
    WHBLogConsoleDraw();

    std::string apiKey = load_api_key();
    if (apiKey.empty()) {
        WHBLogPrintf("\nWarning: api_key.txt not found in app folder!");
        WHBLogPrintf("Place it at sd:/wiiu/apps/geminimywiiu/api_key.txt");
        WHBLogConsoleDraw();
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    while (WHBProcIsRunning()) {
        VPADStatus status;
        VPADReadError error;
        VPADRead(VPAD_CHAN_0, &status, 1, &error);

        // Tap (A) on the GamePad
        if (status.trigger & VPAD_BUTTON_A) {
            WHBLogPrintf("\nOpening GamePad Keyboard...");
            WHBLogConsoleDraw();

            std::string userPrompt = get_user_prompt();

            if (userPrompt.empty()) {
                WHBLogPrintf("\nPrompt cancelled. Press (A) to try again.");
                WHBLogConsoleDraw();
                continue;
            }

            WHBLogPrintf("\nYou: %s", userPrompt.c_str());
            WHBLogPrintf("Querying Gemini 3.5 Flash Lite...");
            WHBLogConsoleDraw();

            if (apiKey.empty()) {
                apiKey = load_api_key(); // Retry loading in case it was uploaded while running
                if (apiKey.empty()) {
                    WHBLogPrintf("\nError: Missing API Key! Add api_key.txt to SD card.");
                    WHBLogConsoleDraw();
                    continue;
                }
            }

            // Using gemini-3.5-flash-lite endpoint
            std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-3.5-flash-lite:generateContent?key=" + apiKey;

            // Escape quotes and newlines for the JSON payload
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
                    WHBLogPrintf("\nCurl failed: %s", curl_easy_strerror(res));
                } else {
                    WHBLogPrintf("\n%s\n", chunk.memory);
                }

                curl_slist_free_all(headers);
                free(chunk.memory);
                curl_easy_cleanup(curl);
            }

            WHBLogPrintf("\nPress (A) to ask again, or (HOME) to exit.");
            WHBLogConsoleDraw();
        }

        OSSleepTicks(OSMillisecondsToTicks(50));
    }

    curl_global_cleanup();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
