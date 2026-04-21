# C++ Browser Backend with Inline Assembly

This is a minimal browser engine written in C++. It:
- Runs an HTTP server on port 8080
- Fetches any given URL using libcurl
- Extracts the HTML `<title>`
- Converts the title to uppercase using **inline x86 assembly**
- Returns the result as JSON

The frontend (`frontend.html`) sends requests to the backend and displays the uppercase title.

## Requirements

- Linux / macOS / Windows (WSL or MSYS2)
- `g++` compiler with x86 assembly support
- `libcurl` development libraries

## Build & Run

### Install dependencies

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install g++ libcurl4-openssl-dev
