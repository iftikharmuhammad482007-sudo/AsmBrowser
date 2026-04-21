#include <iostream>
#include <string>
#include <cstring>
#include <sstream>
#include <thread>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <curl/curl.h>
#include <algorithm>

// ---------- Inline assembly function (x86) ----------
char to_upper_asm(char c) {
    char result;
    __asm__ volatile (
        "movb %1, %%al\n"
        "cmpb $'a', %%al\n"
        "jb  .Lend\n"
        "cmpb $'z', %%al\n"
        "ja  .Lend\n"
        "subb $32, %%al\n"
        ".Lend:\n"
        "movb %%al, %0\n"
        : "=r"(result)
        : "r"(c)
        : "al"
    );
    return result;
}

void str_to_upper_asm(char* str) {
    for (int i = 0; str[i]; ++i)
        str[i] = to_upper_asm(str[i]);
}

// ---------- Fetch webpage using libcurl ----------
std::string fetch_webpage(const std::string& url) {
    CURL* curl = curl_easy_init();
    std::string response;
    if (curl) {
        curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
        curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, 
            [](char* data, size_t size, size_t nmemb, std::string* writer) -> size_t {
                writer->append(data, size * nmemb);
                return size * nmemb;
            });
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);
        CURLcode res = curl_easy_perform(curl);
        if (res != CURLE_OK)
            response.clear();
        curl_easy_cleanup(curl);
    }
    return response;
}

// ---------- Extract title from HTML ----------
std::string extract_title(const std::string& html) {
    size_t start = html.find("<title>");
    if (start == std::string::npos) return "";
    start += 7;
    size_t end = html.find("</title>", start);
    if (end == std::string::npos) return "";
    std::string title = html.substr(start, end - start);
    // Trim whitespace
    title.erase(title.begin(), std::find_if(title.begin(), title.end(), [](unsigned char ch) {
        return !std::isspace(ch);
    }));
    title.erase(std::find_if(title.rbegin(), title.rend(), [](unsigned char ch) {
        return !std::isspace(ch);
    }).base(), title.end());
    return title;
}

// ---------- Build HTTP response ----------
std::string http_response(const std::string& body, int status = 200) {
    std::ostringstream oss;
    oss << "HTTP/1.1 " << status << " OK\r\n"
        << "Content-Type: application/json\r\n"
        << "Access-Control-Allow-Origin: *\r\n"
        << "Access-Control-Allow-Methods: GET\r\n"
        << "Content-Length: " << body.size() << "\r\n"
        << "Connection: close\r\n\r\n"
        << body;
    return oss.str();
}

// ---------- Handle one client request ----------
void handle_client(int client_fd) {
    char buffer[4096];
    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) {
        close(client_fd);
        return;
    }
    buffer[n] = '\0';

    std::string request(buffer);
    
    // Handle CORS preflight (OPTIONS)
    if (request.find("OPTIONS") == 0) {
        std::string resp = "HTTP/1.1 204 No Content\r\n"
                           "Access-Control-Allow-Origin: *\r\n"
                           "Access-Control-Allow-Methods: GET, OPTIONS\r\n"
                           "Access-Control-Allow-Headers: Content-Type\r\n"
                           "Connection: close\r\n\r\n";
        write(client_fd, resp.c_str(), resp.size());
        close(client_fd);
        return;
    }

    // Parse GET /fetch?url=...
    std::string url;
    if (request.find("GET /fetch?url=") != std::string::npos) {
        size_t start = request.find("GET /fetch?url=") + 15;
        size_t end = request.find(" ", start);
        url = request.substr(start, end - start);
        // Simple URL decode (only needed for %3A etc. - optional)
        // For simplicity, we keep raw; curl handles most.
    }

    if (url.empty()) {
        std::string err = R"({"error":"No URL provided"})";
        std::string resp = http_response(err, 400);
        write(client_fd, resp.c_str(), resp.size());
        close(client_fd);
        return;
    }

    // Fetch webpage
    std::string html = fetch_webpage(url);
    if (html.empty()) {
        std::string err = R"({"error":"Failed to fetch URL. Check URL or network."})";
        std::string resp = http_response(err, 500);
        write(client_fd, resp.c_str(), resp.size());
        close(client_fd);
        return;
    }

    // Extract title
    std::string title = extract_title(html);
    if (title.empty()) title = "[No title found]";

    // Convert to uppercase using INLINE ASSEMBLY
    char* buf = new char[title.size() + 1];
    strcpy(buf, title.c_str());
    str_to_upper_asm(buf);
    title = buf;
    delete[] buf;

    // Return JSON
    // Escape double quotes in title
    size_t pos = 0;
    while ((pos = title.find('"', pos)) != std::string::npos) {
        title.replace(pos, 1, "\\\"");
        pos += 2;
    }
    std::string json = R"({"title":")" + title + R"("})";
    std::string resp = http_response(json);
    write(client_fd, resp.c_str(), resp.size());
    close(client_fd);
}

int main() {
    curl_global_init(CURL_GLOBAL_ALL);

    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        std::cerr << "Socket creation failed\n";
        return 1;
    }
    int opt = 1;
    setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    addr.sin_family = AF_INET;
    addr.sin_port = htons(8080);
    addr.sin_addr.s_addr = INADDR_ANY;

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
        std::cerr << "Bind failed. Port 8080 might be in use.\n";
        close(server_fd);
        return 1;
    }

    if (listen(server_fd, 10) < 0) {
        std::cerr << "Listen failed\n";
        close(server_fd);
        return 1;
    }

    std::cout << "✅ C++ backend running on http://localhost:8080\n";
    std::cout << "⏳ Waiting for frontend requests...\n";

    while (true) {
        int client_fd = accept(server_fd, nullptr, nullptr);
        if (client_fd >= 0)
            std::thread(handle_client, client_fd).detach();
    }

    curl_global_cleanup();
    close(server_fd);
    return 0;
}
