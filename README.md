# MutliThread-WebPage-Downloader

A multithreaded C++ command-line application for downloading web pages from a list of URLs, leveraging `libcurl` for HTTP requests and `std::thread` for concurrent processing. This tool is designed to handle large sets of URLs (100+), with features like retry logic, rate limiting, and error logging for robust performance.

## Features
- **Concurrent Downloads**: Downloads multiple web pages simultaneously using a dynamic thread pool (up to 10 threads based on URL count and hardware concurrency).
- **Robust Error Handling**: Implements retry logic (up to 3 attempts) for failed downloads and logs errors to `errors.log` for debugging.
- **Rate Limiting**: Enforces low-speed limits and delays to prevent server overload, ensuring reliable downloads.
- **Progress Tracking**: Displays real-time progress with percentage completion and status updates.
- **Thread Safety**: Utilizes mutexes for thread-safe operations and RAII for managing file and `libcurl` resources.
- **Persistent Output**: Saves downloaded pages as individual HTML files (`page1.html`, `page2.html`, etc.).

## Installation

### Prerequisites
- **C++ Compiler**: `g++` or any C++11-compatible compiler.
- **libcurl**: For HTTP/HTTPS requests.
- **Operating System**: Tested on Linux (e.g., Ubuntu, Fedora), but compatible with any system supporting C++11 and `libcurl`.

### Steps
1. **Clone the Repository**:
   ```bash
   git clone https://github.com/your-username/WebPageDownloader.git
   cd WebPageDownloader
2. **Install Dependcies And Run**
    #For Windows(Install MSYS2 (if not already installed))
  ```bash
    pacman -Syu
    pacman -Syu  # Run twice if prompted to close and reopen terminal
    pacman -S mingw-w64-x86_64-gcc
    pacman -S mingw-w64-x86_64-curl
    pacman -S mingw-w64-x86_64-winpthreads
    g++ --version
    curl-config --version
     # Create urls.txt with URLs, e.g.:
     echo https://example.com > urls.txt
     echo https://www.wikipedia.org >> urls.txt
     echo https://www.github.com >> urls.txt
    g++ -std=c++11 WebPageDownloader.cpp -lcurl -pthread -o downloader
    ./downloader.exe
    rm downloader.exe *.html errors.log
```
##For Linux
  ```Terminal
    sudo apt update
    sudo apt install g++ libcurl4-openssl-dev libc6-dev

    # For Fedora (use instead of apt commands)
    # sudo dnf install gcc-c++ libcurl-devel glibc-devel

    # For Arch Linux (use instead of apt commands)
    # sudo pacman -S gcc curl glibc
     # Create urls.txt with URLs, e.g.:
     echo https://example.com > urls.txt
     echo https://www.wikipedia.org >> urls.txt
     echo https://www.github.com >> urls.txt
     g++ -std=c++11 WebPageDownloader.cpp -lcurl -pthread -o downloader

     # Run the program
     ./downloader

     # Optional: Redirect verbose output for debugging
     ./downloader > debug.log

  # Clean up generated files
  rm downloader *.html errors.log
```

  
