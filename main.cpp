
#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <thread>
#include <mutex>
#include <queue>
#include <regex>
#include <curl/curl.h>
#include <chrono>
#include <iomanip>

using namespace std;

mutex mtx;
int completed = 0;
ofstream errorLog;

vector<string> loadURLs(const string& filename) {
    vector<string> urls;
    ifstream file(filename);
    if (!file) {
        lock_guard<mutex> lock(mtx);
        cout << "Error opening file: " << filename << "\n";
        errorLog << "Error opening file: " << filename << "\n";
        return urls;
    }
    string url;
    regex urlRegex(R"(https?://[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}(/[^\s]*)?)");
    while (getline(file, url)) {
        if (regex_match(url, urlRegex)) {
            urls.push_back(url);
        } else {
            lock_guard<mutex> lock(mtx);
            cout << "Invalid URL skipped: " << url << "\n";
            errorLog << "Invalid URL skipped: " << url << "\n";
        }
    }
    return urls;
}

void downloadPage(const string& url, const string& filename, size_t total_urls) {
    const int MAX_RETRIES = 3;
    int retries = 0;
    CURLcode res;
    do {
        CURL* curl = curl_easy_init();
        if (!curl) {
            lock_guard<mutex> lock(mtx);
            cout << "Error initializing CURL for " << url << "\n";
            errorLog << "Error initializing CURL for " << url << "\n";
            return;
        }

        FILE* file = fopen(filename.c_str(), "w");
        if (!file) {
            lock_guard<mutex> lock(mtx);
            cout << "Error opening file: " << filename << "\n";
            errorLog << "Error opening file: " << filename << "\n";
            curl_easy_cleanup(curl);
            return;
        }

        // RAII for file and curl cleanup
        struct Cleanup {
            FILE* file;
            CURL* curl;
            ~Cleanup() { fclose(file); curl_easy_cleanup(curl); }
        } cleanup{file, curl};

        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, file);
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
        // Rate limiting: low speed limit to ensure we don't overwhelm servers
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_LIMIT, 100L);
        curl_easy_setopt(curl, CURLOPT_LOW_SPEED_TIME, 10L);

        res = curl_easy_perform(curl);

        if (res == CURLE_OK) break;
        retries++;
        if (retries < MAX_RETRIES) {
            lock_guard<mutex> lock(mtx);
            cout << "Retrying " << url << " (" << retries << "/" << MAX_RETRIES << ")\n";
            errorLog << "Retrying " << url << " (" << retries << "/" << MAX_RETRIES << ")\n";
            this_thread::sleep_for(chrono::milliseconds(100)); // Small delay before retry
        }
    } while (retries < MAX_RETRIES);

    lock_guard<mutex> lock(mtx);
    if (res != CURLE_OK) {
        cout << "Download failed for " << url << ": " << curl_easy_strerror(res) << "\n";
        errorLog << "Download failed for " << url << ": " << curl_easy_strerror(res) << "\n";
    } else {
        completed++;
        double percentage = (static_cast<double>(completed) / total_urls) * 100;
        cout << "Downloaded " << completed << "/" << total_urls << " (" << fixed << setprecision(2) << percentage << "%): " << url << "\n";
        errorLog << "Downloaded " << completed << "/" << total_urls << " (" << fixed << setprecision(2) << percentage << "%): " << url << "\n";
    }
}

void worker(queue<pair<string, string>>& tasks, size_t total_urls) {
    while (true) {
        pair<string, string> task;
        {
            lock_guard<mutex> lock(mtx);
            if (tasks.empty()) return;
            task = tasks.front();
            tasks.pop();
        }
        downloadPage(task.first, task.second, total_urls);
        this_thread::sleep_for(chrono::milliseconds(100)); // Rate limiting between requests
    }
}

void downloadAll(const vector<string>& urls) {
    const int NUM_THREADS = min(max(4, static_cast<int>(urls.size()) / 10), static_cast<int>(thread::hardware_concurrency()));
    queue<pair<string, string>> tasks;
    for (size_t i = 0; i < urls.size(); ++i) {
        string filename = "page" + to_string(i + 1) + ".html";
        tasks.emplace(urls[i], filename);
    }

    vector<thread> threads;
    for (int i = 0; i < NUM_THREADS; ++i) {
        threads.emplace_back(worker, ref(tasks), urls.size());
    }

    for (auto& t : threads) {
        t.join();
    }
}

int main() {
    errorLog.open("errors.log");
    if (!errorLog) {
        cout << "Error opening error log file\n";
        return 1;
    }

    curl_global_init(CURL_GLOBAL_ALL);
    vector<string> urls = loadURLs("urls.txt");
    if (urls.empty()) {
        lock_guard<mutex> lock(mtx);
        cout << "No valid URLs found.\n";
        errorLog << "No valid URLs found.\n";
        errorLog.close();
        curl_global_cleanup();
        return 1;
    }

    downloadAll(urls);
    lock_guard<mutex> lock(mtx);
    cout << "Download complete! " << completed << " pages downloaded.\n";
    errorLog << "Download complete! " << completed << " pages downloaded.\n";

    errorLog.close();
    curl_global_cleanup();
    return 0;
}
