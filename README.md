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
   git clone https://github.com/sagili-manoj/WebPageDownloader.git
   cd WebPageDownloader
   ```
2. **Install Dependcies And Run**
   #For Windows
```bash
# Install MSYS2 (if not already installed)
# Download and run the MSYS2 installer from https://www.msys2.org/
# Open MSYS2 MinGW 64-bit terminal
# Update MSYS2 packages
pacman -Syu
pacman -Syu  # Run twice if prompted to close and reopen terminal

# Install g++ (C++ compiler)
pacman -S mingw-w64-x86_64-gcc

# Install libcurl (for HTTP requests)
pacman -S mingw-w64-x86_64-curl

# Install winpthreads (included with gcc, but ensure it's present)
pacman -S mingw-w64-x86_64-winpthreads

# Verify installations
g++ --version
curl-config --version

# Save WebPageDownloader.cpp (copy code to file)
# Create urls.txt with URLs, e.g.:
echo https://example.com > urls.txt
echo https://www.wikipedia.org >> urls.txt
echo https://www.github.com >> urls.txt

# Compile the program
g++ -std=c++11 WebPageDownloader.cpp -lcurl -pthread -o downloader

# Run the program
./downloader.exe

# Optional: Redirect verbose output for debugging
./downloader.exe > debug.log

# Clean up generated files
rm downloader.exe *.html errors.log
```
##For Linux
  ```Terminal
# Install dependencies (Ubuntu/Debian)
sudo apt update
sudo apt install g++ libcurl4-openssl-dev libc6-dev

# For Fedora (use instead of apt commands)
# sudo dnf install gcc-c++ libcurl-devel glibc-devel

# For Arch Linux (use instead of apt commands)
# sudo pacman -S gcc curl glibc

# Verify installations
g++ --version
curl-config --version

# Save WebPageDownloader.cpp (copy code to file)
# Create urls.txt with URLs, e.g.:
echo "https://example.com" > urls.txt
echo "https://www.wikipedia.org" >> urls.txt
echo "https://www.github.com" >> urls.txt

# Compile the program
g++ -std=c++11 WebPageDownloader.cpp -lcurl -pthread -o downloader

# Run the program
./downloader

# Optional: Redirect verbose output for debugging
./downloader > debug.log

# Clean up generated files
rm downloader *.html errors.log

# Optional: Ensure file descriptor limit for 100+ URLs
ulimit -n 1024
```

## 📷 Example Output
![Screenshot 2025-05-21 125927](https://github.com/user-attachments/assets/2402c649-1e1f-4380-b67c-3abe12e4557a)


Downloaded web pages will be saved in the same folder that the file is currently running on.
