#include <arpa/inet.h>
#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>
#include <fstream>
#include <unistd.h>

struct User {
    std::string username;
    std::string password;
    bool online = false;
};

struct Message {
    std::string sender;
    std::string receiver;
    std::string content;
    std::string timestamp;
};

class ChatSystem {
  public:
    bool registerUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(mtx);
        if (users.count(username)) return false;
        users[username] = User{username, password, false};
        return true;
    }

    bool loginUser(const std::string& username, const std::string& password) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = users.find(username);
        if (it == users.end() || it->second.password != password) return false;
        it->second.online = true;
        return true;
    }

    bool logoutUser(const std::string& username) {
        std::lock_guard<std::mutex> lock(mtx);
        auto it = users.find(username);
        if (it == users.end()) return false;
        it->second.online = false;
        return true;
    }

    bool sendMessage(const std::string& sender, const std::string& receiver, const std::string& content) {
        std::lock_guard<std::mutex> lock(mtx);
        if (!users.count(sender) || !users.count(receiver)) return false;
        messages.push_back(Message{sender, receiver, content, now()});
        return true;
    }

    std::vector<Message> chatHistory(const std::string& a, const std::string& b) {
        std::lock_guard<std::mutex> lock(mtx);
        std::vector<Message> out;
        for (const auto& m : messages) {
            if ((m.sender == a && m.receiver == b) || (m.sender == b && m.receiver == a)) out.push_back(m);
        }
        return out;
    }

  private:
    std::string now() {
        auto t = std::time(nullptr);
        std::tm tm{};
        gmtime_r(&t, &tm);
        std::ostringstream oss;
        oss << std::put_time(&tm, "%Y-%m-%d %H:%M:%S UTC");
        return oss.str();
    }

    std::unordered_map<std::string, User> users;
    std::vector<Message> messages;
    std::mutex mtx;
};

static ChatSystem chat;

std::string urlDecode(const std::string& s) {
    std::string out;
    char ch;
    int i, ii;
    for (i = 0; i < (int)s.length(); i++) {
        if (int(s[i]) == 37) {
            sscanf(s.substr(i + 1, 2).c_str(), "%x", &ii);
            ch = static_cast<char>(ii);
            out += ch;
            i = i + 2;
        } else if (s[i] == '+') {
            out += ' ';
        } else {
            out += s[i];
        }
    }
    return out;
}

std::map<std::string, std::string> parseForm(const std::string& body) {
    std::map<std::string, std::string> form;
    std::stringstream ss(body);
    std::string pair;
    while (std::getline(ss, pair, '&')) {
        auto pos = pair.find('=');
        if (pos != std::string::npos) {
            form[urlDecode(pair.substr(0, pos))] = urlDecode(pair.substr(pos + 1));
        }
    }
    return form;
}

std::string loadFile(const std::string& path) {
    std::ifstream file(path);
    if (!file) return "";
    std::ostringstream ss;
    ss << file.rdbuf();
    return ss.str();
}

void sendResponse(int fd, const std::string& status, const std::string& type, const std::string& body) {
    std::ostringstream resp;
    resp << "HTTP/1.1 " << status << "\r\n";
    resp << "Content-Type: " << type << "\r\n";
    resp << "Content-Length: " << body.size() << "\r\n";
    resp << "Connection: close\r\n\r\n";
    resp << body;
    send(fd, resp.str().c_str(), resp.str().size(), 0);
}

void handleClient(int fd) {
    char buffer[8192] = {0};
    int readBytes = read(fd, buffer, sizeof(buffer) - 1);
    if (readBytes <= 0) { close(fd); return; }
    std::string req(buffer, readBytes);

    std::istringstream request(req);
    std::string method, path, version;
    request >> method >> path >> version;

    auto bodyPos = req.find("\r\n\r\n");
    std::string body = (bodyPos != std::string::npos) ? req.substr(bodyPos + 4) : "";

    if (method == "GET" && path == "/") {
        auto html = loadFile("public/index.html");
        sendResponse(fd, "200 OK", "text/html", html);
    } else if (method == "GET" && path == "/app.js") {
        sendResponse(fd, "200 OK", "application/javascript", loadFile("public/app.js"));
    } else if (method == "GET" && path == "/styles.css") {
        sendResponse(fd, "200 OK", "text/css", loadFile("public/styles.css"));
    } else if (method == "POST" && path == "/api/register") {
        auto f = parseForm(body);
        bool ok = chat.registerUser(f["username"], f["password"]);
        sendResponse(fd, ok ? "200 OK" : "400 Bad Request", "application/json", ok ? "{\"message\":\"Registered\"}" : "{\"message\":\"User exists\"}");
    } else if (method == "POST" && path == "/api/login") {
        auto f = parseForm(body);
        bool ok = chat.loginUser(f["username"], f["password"]);
        sendResponse(fd, ok ? "200 OK" : "401 Unauthorized", "application/json", ok ? "{\"message\":\"Logged in\"}" : "{\"message\":\"Invalid credentials\"}");
    } else if (method == "POST" && path == "/api/logout") {
        auto f = parseForm(body);
        bool ok = chat.logoutUser(f["username"]);
        sendResponse(fd, ok ? "200 OK" : "400 Bad Request", "application/json", ok ? "{\"message\":\"Logged out\"}" : "{\"message\":\"Unknown user\"}");
    } else if (method == "POST" && path == "/api/send") {
        auto f = parseForm(body);
        bool ok = chat.sendMessage(f["sender"], f["receiver"], f["content"]);
        sendResponse(fd, ok ? "200 OK" : "400 Bad Request", "application/json", ok ? "{\"message\":\"Message sent\"}" : "{\"message\":\"Send failed\"}");
    } else if (method == "POST" && path == "/api/history") {
        auto f = parseForm(body);
        auto history = chat.chatHistory(f["user1"], f["user2"]);
        std::ostringstream json;
        json << "[";
        for (size_t i = 0; i < history.size(); ++i) {
            const auto& m = history[i];
            json << "{\"sender\":\"" << m.sender << "\",\"receiver\":\"" << m.receiver << "\",\"content\":\"" << m.content << "\",\"timestamp\":\"" << m.timestamp << "\"}";
            if (i + 1 < history.size()) json << ",";
        }
        json << "]";
        sendResponse(fd, "200 OK", "application/json", json.str());
    } else {
        sendResponse(fd, "404 Not Found", "text/plain", "Not found");
    }

    close(fd);
}

int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(8080);

    bind(serverFd, (struct sockaddr*)&addr, sizeof(addr));
    listen(serverFd, 10);

    std::cout << "Chat system running at http://localhost:8080\n";

    while (true) {
        sockaddr_in clientAddr{};
        socklen_t len = sizeof(clientAddr);
        int clientFd = accept(serverFd, (struct sockaddr*)&clientAddr, &len);
        std::thread(handleClient, clientFd).detach();
    }
}
