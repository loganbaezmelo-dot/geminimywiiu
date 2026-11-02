#include <coreinit/thread.h>
#include <coreinit/time.h>
#include <whb/proc.h>
#include <whb/log.h>
#include <whb/log_console.h>
#include <vpad/input.h>
#include <curl/curl.h>
#include <string>

// Callback to grab response chunks
static size_t WriteCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    ((std::string*)userp)->append((char*)contents, size * nmemb);
    return size * nmemb;
}

std::string ask_gemini(const std::string& prompt, const std::string& apiKey) {
    CURL* curl = curl_easy_init();
    std::string readBuffer;

    if (!curl) return "Failed to init curl";

    // Target Gemini 3.5 Flash Lite or 3.8 Flash
    std::string model = "gemini-3.5-flash-lite"; 
    std::string url = "https://generativelanguage.googleapis.com/v1beta/models/" + model + ":generateContent?key=" + apiKey;

    std::string jsonPayload = "{\"contents\": [{\"parts\": [{\"text\": \"" + prompt + "\"}]}]}";

    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Content-Type: application/json");

    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, jsonPayload.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &readBuffer);
    
    // Disable strict cert verification if the Wii U root cert store complains
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    curl_slist_free_all(headers);

    if (res != CURLE_OK) {
        return "Curl error: " + std::string(curl_easy_strerror(res));
    }
    return readBuffer;
}

int main(int argc, char** argv) {
    WHBProcInit();
    WHBLogConsoleInit();

    std::string apiKey = "YOUR_GEMINI_API_KEY";
    std::string response = ask_gemini("Say hello to the Nintendo Wii U community!", apiKey);

    while (WHBProcIsRunning()) {
        VPADStatus status;
        VPADReadError error;
        VPADRead(VPAD_CHAN_0, &status, 1, &error);

        WHBLogConsoleDraw();
        WHBLogConsolePrint("=== geminimywiiu ===\n\n");
        WHBLogConsolePrint(response.c_str());
        WHBLogConsolePrint("\n\nPress HOME to exit.");

        OSSleepTicks(OSMillisecondsToTicks(100));
    }

    WHBLogConsoleFree();
    WHBProcShutdown();
    return 0;
}
