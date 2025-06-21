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
#include <functional> // For std::function
#include <atomic>     // For std::atomic

// --- 1. RAII Wrappers for Resources ---

// RAII wrapper for CURL* handle
class CurlHandle {
public:
    explicit CurlHandle(CURL* handle_ptr = nullptr) : handle_(handle_ptr) {}

    // No copying allowed (CURL handles are not copyable)
    CurlHandle(const CurlHandle&) = delete;
    CurlHandle& operator=(const CurlHandle&) = delete;

    // Move constructor
    CurlHandle(CurlHandle&& other) noexcept : handle_(other.handle_) {
        other.handle_ = nullptr;
    }

    // Move assignment operator
    CurlHandle& operator=(CurlHandle&& other) noexcept {
        if (this != &other) {
            curl_easy_cleanup(handle_); // Clean up current handle
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
    operator bool() const { return handle_ != nullptr; } // Check if handle is valid

private:
    CURL* handle_;
};

// RAII wrapper for FILE* handle
class FileHandle {
public:
    explicit FileHandle(FILE* file_ptr = nullptr) : file_(file_ptr) {}

    // No copying allowed (FILE handles are not copyable)
    FileHandle(const FileHandle&) = delete;
    FileHandle& operator=(const FileHandle&) = delete;

    // Move constructor
    FileHandle(FileHandle&& other) noexcept : file_(other.file_) {
        other.file_ = nullptr;
    }

    // Move assignment operator
    FileHandle& operator=(FileHandle&& other) noexcept {
        if (this != &other) {
            if (file_) fclose(file_); // Close current file
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
    operator bool() const { return file_ != nullptr; } // Check if file is valid

private:
    FILE* file_;
};

// --- 2. Simple Thread-Safe Logger ---

class Logger {
public:
    // Global static instance for easy access
    static Logger& getInstance() {
        static Logger instance; // Guaranteed to be destroyed correctly at program exit.
                                // Instantiated on first use.
        return instance;
    }

    // Deleted copy constructor and assignment operator to prevent multiple instances
    Logger(Logger const&) = delete;
    void operator=(Logger const&) = delete;

    void openLogFile(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mtx_);
        if (logFile_.is_open()) {
            logFile_.close();
        }
        logFile_.open(filename, std::ios::app); // Append mode
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

    // For specific error messages
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


// --- 3. Thread Pool for Task Management ---

class ThreadPool {
public:
    ThreadPool(size_t num_threads) : stop_(false) {
        for (size_t i = 0; i < num_threads; ++i) {
            workers_.emplace_back([this] {
                while (true) {
                    std::function<void()> task;
                    {
                        std::unique_lock<std::mutex> lock(queue_mutex_);
                        // Wait until there's a task or the pool is stopped
                        condition_.wait(lock, [this] { return stop_ || !tasks_.empty(); });

                        // If pool is stopped and no more tasks, exit
                        if (stop_ && tasks_.empty()) {
                            return;
                        }
                        task = std::move(tasks_.front());
                        tasks_.pop();
                    }
                    task(); // Execute the task
                }
            });
        }
    }

    // Enqueue a task
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
        condition_.notify_one(); // Notify one waiting worker
    }

    // Destructor: Stop all workers and join them
    ~ThreadPool() {
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            stop_ = true;
        }
        condition_.notify_all(); // Notify all workers to wake up and exit
        for (std::thread& worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

private:
    std::vector<std::thread> workers_;
    std::queue<std::function<void()>> tasks_; // Queue of tasks (functions)

    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};


// --- Core Download Logic ---

// Atomic counter for completed downloads for thread-safe updates
std::atomic<int> g_completed_downloads(0);

// Callback function for curl to write data to file
size_t write_data(void *ptr, size_t size, size_t nmemb, FILE *stream) {
    size_t written = fwrite(ptr, size, nmemb, stream);
    return written;
}

// Function to download a single page using libcurl
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

        FileHandle file_handle(fopen(filename.c_str(), "w")); // "w" will overwrite. Consider "wb" for binary or check existence.
        if (!file_handle) {
            logger.logError("Error opening file: " + filename);
            return;
        }

        curl_easy_setopt(curl_handle.get(), CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEFUNCTION, write_data);
        curl_easy_setopt(curl_handle.get(), CURLOPT_WRITEDATA, file_handle.get());
        curl_easy_setopt(curl_handle.get(), CURLOPT_FOLLOWLOCATION, 1L); // Follow HTTP 3xx redirects
        curl_easy_setopt(curl_handle.get(), CURLOPT_TIMEOUT, 30L);      // Max time in seconds for the entire operation
        // Consider a more adaptive rate limiting if needed, this is still a blunt instrument
        curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_LIMIT, 10L); // Bytes/sec
        curl_easy_setopt(curl_handle.get(), CURLOPT_LOW_SPEED_TIME, 5L);  // For this many seconds
        curl_easy_setopt(curl_handle.get(), CURLOPT_FAILONERROR, 1L); // Fail on HTTP 4xx/5xx errors

        res = curl_easy_perform(curl_handle.get());

        if (res == CURLE_OK) break; // Success, break retry loop

        retries++;
        if (retries < MAX_RETRIES) {
            logger.log("Retrying " + url + " (" + std::to_string(retries) + "/" + std::to_string(MAX_RETRIES) + ")");
            std::this_thread::sleep_for(std::chrono::milliseconds(100 * retries)); // Exponential backoff for retries
        }
    } while (retries < MAX_RETRIES);

    if (res != CURLE_OK) {
        logger.logError("Download failed for " + url + ": " + curl_easy_strerror(res));
    } else {
        int current_completed = ++g_completed_downloads; // Atomically increment
        double percentage = (static_cast<double>(current_completed) / total_urls) * 100;
        std::string msg = "Downloaded " + std::to_string(current_completed) + "/" + std::to_string(total_urls) +
                          " (" + std::to_string(percentage) + "%): " + url;
        logger.log(msg);
    }
}

// Function to load URLs from a file
std::vector<std::string> loadURLs(const std::string& filename) {
    Logger& logger = Logger::getInstance();
    std::vector<std::string> urls;
    std::ifstream file(filename);
    if (!file) {
        logger.logError("Error opening file: " + filename);
        return urls;
    }
    std::string url;
    // Basic URL regex. For production, consider a more robust URL parsing library.
    std::regex urlRegex(R"(https?://[a-zA-Z0-9\-\.]+\.[a-zA-Z]{2,}(/[^\s]*)?)");
    while (std::getline(file, url)) {
        url.erase(0, url.find_first_not_of(" \t\n\r\f\v")); // Trim leading whitespace
        url.erase(url.find_last_not_of(" \t\n\r\f\v") + 1); // Trim trailing whitespace
        if (std::regex_match(url, urlRegex)) {
            urls.push_back(url);
        } else {
            logger.log("Invalid URL skipped: " + url);
        }
    }
    return urls;
}

// Main function to orchestrate the download process
void downloadAll(const std::vector<std::string>& urls) {
    Logger& logger = Logger::getInstance();
    // Determine optimal number of threads, min 4, max 2 * hardware_concurrency
    const size_t NUM_THREADS = std::min(std::max(4U, static_cast<unsigned int>(urls.size() / 5)), std::thread::hardware_concurrency() * 2);
    logger.log("Starting download with " + std::to_string(NUM_THREADS) + " threads.");

    ThreadPool pool(NUM_THREADS);

    // Enqueue all download tasks
    for (size_t i = 0; i < urls.size(); ++i) {
        // Capture 'urls' by value for thread safety, and construct filename inside lambda or capture it by value
        // The total_urls_count is captured by value for the lambda
        pool.enqueue([url = urls[i], filename_str = "page" + std::to_string(i + 1) + ".html", total_urls_count = urls.size()]() {
            downloadPage(url, filename_str, total_urls_count);
        });
    }
    // The ThreadPool's destructor will ensure all tasks are completed and threads are joined.
}


int main() {
    Logger& logger = Logger::getInstance();
    logger.openLogFile("errors_and_logs.log");

    curl_global_init(CURL_GLOBAL_ALL); // Initialize libcurl global environment

    std::vector<std::string> urls = loadURLs("urls.txt");
    if (urls.empty()) {
        logger.log("No valid URLs found. Exiting.");
        curl_global_cleanup();
        return 1;
    }

    downloadAll(urls);

    logger.log("All download tasks dispatched. Waiting for completion...");
    // g_completed_downloads is atomic, so we can poll it to know completion.
    // However, for a production setup, it's better if downloadAll returns a future
    // or provides a way to await completion directly from the ThreadPool itself.
    // For this quick update, we'll just wait for the ThreadPool destructor in main.

    curl_global_cleanup(); // Clean up libcurl global environment
    logger.log("Program finished.");
    return 0;
}
