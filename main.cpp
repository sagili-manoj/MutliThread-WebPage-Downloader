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
#include <condition_variable>
#include <functional>
#include <atomic>

class CurlHandle {
public:
    explicit CurlHandle(CURL* handle_ptr = nullptr) : handle_(handle_ptr) {}
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;
    CurlHandle(CurlHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    CurlHandle& operator=(CurlHandle&& other) noexcept {
        if (this != &other) {
            curl_easy_cleanup(handle_);
            handle_ = other.handle_;
            other.handle_ = nullptr;
        }
        return *this;
    }

    ~CurlHandle() {
        if (handle_) {
            curl_easy_cleanup(handle_);
        }
    }

    CURL* get() const { return handle_; }
    operator bool() const { return handle_ != nullptr; }

private:
    CURL* handle_;
};
class FileHandle {
public:
    explicit FileHandle(FILE* file_ptr = nullptr) : file_(file_ptr) {}
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    FileHandle(FileHandle&& other) noexcept : file_(other.file_) {
        other.file_ = nullptr;
    }

    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            if (file_) fclose(file_);
            file_ = other.file_;
            other.file_ = nullptr;
        }
        return *this;
    }

    ~FileHandle() {
        if (file_) {
            fclose(file_);
        }
    }

    FILE* get() const { return file_; }
    operator bool() const { return file_ != nullptr; }

private:
    FILE* file_;
};

class Logger {
public:
    static Logger& getInstance() {
        static Logger instance; 
        return instance;
    }

    Logger(Logger const&) = delete;
    void operator=(Logger const&) = delete;

    void openLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (logFile_.is_open()) {
            logFile_.close();
        }
        logFile_.open(filename, std::ios::app);
        if (!logFile_) {
            std::cerr << "Error: Could not open log file: " << filename << std::endl;
        }
    }

    void log(const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cout << message << std::endl;
        if (logFile_.is_open()) {
            logFile_ << message << std::endl;
        }
    }

    void logError(const std::string& message) {
        std::lock_guard<std::mutex> lock(mtx_);
        std::cerr << "ERROR: " << message << std::endl;
        if (logFile_.is_open()) {
            logFile_ << "ERROR: " << message << std::endl;
        }
    }

    ~Logger() {
        if (logFile_.is_open()) {
            logFile_.close();
        }
    }

private:
    Logger() = default;
    std::mutex mtx_;
    std::ofstream logFile_;
};


class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop_(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task();
                }
            });
        }
    }
    template <class F>
    void enqueue(F&& f) {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            if (stop_) {
                Logger::getInstance().logError("enqueue on stopped ThreadPool");
                return;
            }
            tasks_.emplace(std::forward<F>(f));
        }
        condition_.notify_one();
    }

    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all();
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_;

    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

std::atomic<int> g_completed_downloads(0);
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

void downloadPage(const std::string& url, const std::string& filename, size_t total_urls) {
    Logger& logger = Logger::getInstance();
    const int MAX_RETRIES = 3;
    int retries = 0;
    CURLcode res;

    do {
        CurlHandle curl_handle(curl_easy_init());
        if (!curl_handle) {
            logger.logError("Error initializing CURL for " + url);
            return;
        }

        FileHandle file_handle(fopen(filename.c_str(), "w")); 
        if (!file_handle) {
            logger.logError("Error opening file: " + filename);
            return;
        }

        curl_easy_setopt(curl_handle.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, file_handle.get());
        curl_easy_setopt(curl_handle.get(), CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl_handle.get(), CURLOPT_TIMEOUT, 30L);      
        curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_LIMIT, 10L); 
        curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_TIME, 5L);  
        curl_easy_setopt(curl_handle.get(), CURLOPT_FAILONERROR, 1L); 
        res = curl_easy_perform(curl_handle.get());

        if (res == CURLE_OK) break;

        retries++;
        if (retries < MAX_RETRIES) {
            logger.log("Retrying " + url + " (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + ")");
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * retries));
        }
    } while (retries < MAX_RETRIES);

    if (res != CURLE_OK) {
        logger.logError("Download failed for " + url + ": " + curl_easy_strerror(res));
    } else {
        int current_completed = ++g_completed_downloads;
        double percentage = (static_cast<double>(current_completed) / total_urls) * 100;
        std::string msg = "Downloaded " + std::to_string(current_completed) + "/" + std::to_string(total_urls) +
                          " (" + std::to_string(percentage) + "%): " + url;
        logger.log(msg);
    }
}
std::vector<std::string> loadURLs(const std::string& filename) {
    Logger& logger = Logger::getInstance();
    std::vector<std::string> urls;
    std::ifstream file(filename);
    if (!file) {
        logger.logError("Error opening file: " + filename);
        return urls;
    }
    std::string url;
    std::regex urlRegex(R"(https?://[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}(/[^\s]*)?)");
    while (std::getline(file, url)) {
        url.erase(0, url.find_first_not_of(" \t\n\r\f\v"));
        url.erase(url.find_last_not_of(" \t\n\r\f\v") + 1);
        if (std::regex_match(url, urlRegex)) {
            urls.push_back(url);
        } else {
            logger.log("Invalid URL skipped: " + url);
        }
    }
    return urls;
}

void downloadAll(const std::vector<std::string>& urls) {
    Logger& logger = Logger::getInstance();
    const size_t NUM_THREADS = std::min(std::max(4U, static_cast<unsigned int>(urls.size() / 5)), std::thread::hardware_concurrency() * 2);
    logger.log("Starting download with " + std::to_string(NUM_THREADS) + " threads.");

    ThreadPool pool(NUM_THREADS);

    for (size_t i = 0; i < urls.size(); ++i) {
        pool.enqueue([url = urls[i], filename_str = "page" + std::to_string(i + 1) + ".html", total_urls_count = urls.size()]() {
            downloadPage(url, filename_str, total_urls_count);
        });
    }
}


int main() {
    Logger& logger = Logger::getInstance();
    logger.openLogFile("errors_and_logs.log");

    curl_global_init(CURL_GLOBAL_ALL);

    std::vector<std::string> urls = loadURLs("urls.txt");
    if (urls.empty()) {
        logger.log("No valid URLs found. Exiting.");
        curl_global_cleanup();
        return 1;
    }

    downloadAll(urls);

    logger.log("All download tasks dispatched. Waiting for completion...");
    curl_global_cleanup(); 
    logger.log("Program finished.");
    return 0;
}
