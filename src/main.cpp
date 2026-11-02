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
    // Allocate work memory required by the software keyboard
    size_t kbdWorkSize = nn::swkbd::GetWorkMemorySize(0);
    void *kbdWorkMem = malloc(kbdWorkSize);
    if (!kbdWorkMem) return "";

    nn::swkbd::CreateArg createArg;
    createArg.workMemory = kbdWorkMem;
    createArg.regionType = nn::swkbd::RegionType::USA;
    nn::swkbd::Create(createArg);

    nn::swkbd::AppearArg appearArg;
    nn::swkbd::KeyboardArg &kArg = appearArg.keyboardArg;
    
    // Configure standard full keyboard with predictive dictionary
    kArg.configArg.keyboardMode = nn::swkbd::KeyboardMode::Full;
    kArg.configArg.languageType = nn::swkbd::LanguageType::English;

    // Set initial hints & button labels
    std::u16string hint = u"Ask Gemini anything...";
    std::u16string okBtn = u"Send";
    kArg.configArg.hintText = (const char16_t*)hint.c_str();
    kArg.configArg.okText = (const char16_t*)okBtn.c_str();

    nn::swkbd::AppearInputForm(appearArg);

    std::string resultStr = "";
    bool running = true;

    while (running && WHBProcessIsRunning()) {
        VPADStatus vpad;
        VPADReadError vErr;
        VPADRead(VPAD_CHAN_0, &vpad, 1, &vErr);

        nn::swkbd::ControllerInfo cInfo;
        cInfo.vpad = &vpad;

        nn::swkbd::Calc(cInfo);

        // When the user taps 'Send' on screen
        if (nn::swkbd::IsDecideOkButton(nullptr)) {
            const char16_t *resU16 = nn::swkbd::GetInputFormString();
            if (resU16) {
                // Convert UTF-16 to UTF-8
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

        // Draw the keyboard frame to the GamePad
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

    WHBLogConsoleSetColor(0x000000FF, 0xFFFFFFFF); // Clean black & white console
    WHBLogPrintf("=== geminimywiiu ===");
    WHBLogPrintf("Press (A) on GamePad to type a prompt");
    WHBLogPrintf("Press (HOME) to exit");
    WHBLogConsoleDraw();

    std::string apiKey = load_api_key();
    if (apiKey.empty()) {
        WHBLogPrintf("\nWarning: api_key.txt not found in app folder!");
        WHBLogPrintf("Create sd:/wiiu/apps/geminimywiiu/api_key.txt");
        WHBLogConsoleDraw();
    }

    CURLcode curlInitRes = curl_global_init(CURL_GLOBAL_DEFAULT);

    while (WHBProcessIsRunning()) {
        VPADStatus status;
        VPADReadError error;
        VPADRead(VPAD_CHAN_0, &status, 1, &error);

        // Tap (A) to pop open the keyboard
        if (status.trigger & VPAD_BUTTON_A) {
            WHBLogConsoleClear();
            WHBLogPrintf("Opening Wii U GamePad Keyboard...");
            WHBLogConsoleDraw();

            std::string userPrompt = get_user_prompt();

            // Re-render console screen once keyboard closes
            WHBLogConsoleClear();
            WHBLogPrintf("=== geminimywiiu ===");

            if (userPrompt.empty()) {
                WHBLogPrintf("Prompt cancelled. Press (A) to try again.");
                WHBLogConsoleDraw();
                continue;
            }

            WHBLogPrintf("Prompt: %s", userPrompt.c_str());
            WHBLogPrintf("\nQuerying Gemini 3.5 Flash Lite...");
            WHBLogConsoleDraw();

            if (apiKey.empty()) {
                WHBLogPrintf("\nError: Missing API Key! Add api_key.txt to SD card.");
                WHBLogConsoleDraw();
                continue;
            }

            // Build request URL
            std::string url = "https://generativelanguage.googleapis.com/v1beta/models/gemini-2.5-flash-lite:generateContent?key=" + apiKey;

            // Simple JSON escaping for prompt
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
                    WHBLogPrintf("\n%s", chunk.memory);
                }

                curl_slist_free_all(headers);
                free(chunk.memory);
                curl_easy_cleanup(curl);
            }

            WHBLogPrintf("\nPress (A) to ask another, or (HOME) to exit.");
            WHBLogConsoleDraw();
        }

        OSSleepTicks(OSMillisecondsToTicks(50));
    }

    curl_global_cleanup();
    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
