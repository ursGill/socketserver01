#include <iostream>
#include <winsock.h>
#include <thread>
#include <string>
#include <fstream>
#include <ctime>
#include <mutex>

using namespace std;

#define PORT 9909
#define MAX_CLIENTS 5
#define BROADCAST_PORT 9910

struct sockaddr_in srv;
fd_set fr, fw, fe;
int nMaxFd;
int nSocket;
int nArrClient[MAX_CLIENTS] = { 0 };
string clientNames[MAX_CLIENTS];
bool serverRunning = true;
std::mutex logMutex; // Protect file access

// Function declarations
void BroadcastServerIP();
void BroadcastMessage(string senderId, const string& msg);
void SendPrivateMessage(string senderId, string receiverId, const string& msg);
void RemoveClient(int nClientSocket);
int checkclient(string id);
void LogToFile(const string& data);
void AdminConsole();
string GetTimestamp(); // helper

// Helper: returns timestamp string using ctime_s
string GetTimestamp() {
    time_t now = time(nullptr);
    char buffer[26] = { 0 }; // 26 is enough for ctime format
    errno_t err = ctime_s(buffer, sizeof(buffer), &now);
    if (err != 0) {
        return string("unknown-time");
    }
    // remove trailing newline if present
    string ts(buffer);
    if (!ts.empty() && ts.back() == '\n') ts.pop_back();
    return ts;
}

// Function to log data into a file (thread-safe)
void LogToFile(const string& data) {
    string timestamp = GetTimestamp();
    std::lock_guard<std::mutex> lock(logMutex);
    ofstream file("chat_log.txt", ios::app);
    if (file.is_open()) {
        file << "[" << timestamp << "] " << data << endl;
        file.close();
    }
    else {
        // If logging fails, print to console (non-fatal)
        cerr << "Failed to open chat_log.txt for writing." << endl;
    }
}

// Function to broadcast server IP automatically
void BroadcastServerIP() {
    int udpSocket = socket(AF_INET, SOCK_DGRAM, 0);
    if (udpSocket < 0) return;

    BOOL broadcastEnable = TRUE;
    setsockopt(udpSocket, SOL_SOCKET, SO_BROADCAST, (char*)&broadcastEnable, sizeof(broadcastEnable));

    sockaddr_in broadcastAddr;
    broadcastAddr.sin_family = AF_INET;
    broadcastAddr.sin_port = htons(BROADCAST_PORT);
    broadcastAddr.sin_addr.s_addr = INADDR_BROADCAST;

    char hostname[256];
    gethostname(hostname, sizeof(hostname));
    struct hostent* host = gethostbyname(hostname);
    string serverIP = "127.0.0.1";
    if (host && host->h_addr) {
        serverIP = inet_ntoa(*(struct in_addr*)host->h_addr);
    }
    string msg = "SERVER_IP:" + serverIP;

    cout << "\n[Auto-Broadcast] Server IP is " << serverIP << endl;

    while (serverRunning) {
        sendto(udpSocket, msg.c_str(), (int)msg.size(), 0, (sockaddr*)&broadcastAddr, sizeof(broadcastAddr));
        Sleep(5000);
    }
    closesocket(udpSocket);
}

// Handle messages from clients
void ProcessNewMessages(int nClientSocket, string clientId) {
    char buff[512] = { 0 };
    int nRet = recv(nClientSocket, buff, 511, 0);

    if (nRet <= 0) {
        cout << "\nClient " << clientId << " disconnected." << endl;
        LogToFile("Client " + clientId + " disconnected.");
        RemoveClient(nClientSocket);
        return;
    }

    string message(buff);
    message.erase(message.find_last_not_of("\r\n") + 1);
    if (message.empty()) return;

    cout << "\nMessage from " << clientId << ": " << message << endl;
    LogToFile(clientId + " sent: " + message);

    size_t colonPos = message.find(':');
    if (colonPos != string::npos) {
        string targetId = message.substr(0, colonPos);
        string msgToSend = message.substr(colonPos + 1);
        SendPrivateMessage(clientId, targetId, msgToSend);
    }
    else {
        BroadcastMessage(clientId, message);
    }
}

// Handle new connections and requests
void ProcessTheNewRequest() {
    if (FD_ISSET(nSocket, &fr)) {
        int nLen = sizeof(struct sockaddr);
        int nClientSocket = accept(nSocket, NULL, &nLen);

        if (nClientSocket > 0) {
            int nIndex;
            for (nIndex = 0; nIndex < MAX_CLIENTS; nIndex++) {
                if (nArrClient[nIndex] == 0) {
                    nArrClient[nIndex] = nClientSocket;

                    string askName = "Enter your Name: ";
                    send(nClientSocket, askName.c_str(), (int)askName.size(), 0);

                    char nameBuff[100] = { 0 };
                    int nRet = recv(nClientSocket, nameBuff, 99, 0);

                    if (nRet > 0) {
                        clientNames[nIndex] = string(nameBuff);
                        clientNames[nIndex].erase(clientNames[nIndex].find_last_not_of("\r\n") + 1);

                        cout << "\nClient connected as: " << clientNames[nIndex] << endl;
                        LogToFile("Client connected: " + clientNames[nIndex]);

                        string welcomeMsg = "Welcome " + clientNames[nIndex] + "! You can now chat.\n";
                        send(nClientSocket, welcomeMsg.c_str(), (int)welcomeMsg.size(), 0);

                        string joinMsg = clientNames[nIndex] + " has joined the chat";
                        BroadcastMessage("Server", joinMsg);
                    }
                    else {
                        cout << "\nFailed to get name. Closing connection." << endl;
                        closesocket(nClientSocket);
                        nArrClient[nIndex] = 0;
                    }
                    break;
                }
            }
            if (nIndex == MAX_CLIENTS) {
                cout << "\nNo space for new connection." << endl;
                send(nClientSocket, "Server full", 11, 0);
                closesocket(nClientSocket);
            }
        }
    }
    else {
        for (int nIndex = 0; nIndex < MAX_CLIENTS; nIndex++) {
            if (nArrClient[nIndex] != 0 && FD_ISSET(nArrClient[nIndex], &fr)) {
                ProcessNewMessages(nArrClient[nIndex], clientNames[nIndex]);
            }
        }
    }
}

// Broadcast messages to all clients
void BroadcastMessage(string senderId, const string& msg) {
    string fullMsg = senderId + " (Broadcast): " + msg;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (nArrClient[i] != 0 && clientNames[i] != senderId) {
            send(nArrClient[i], fullMsg.c_str(), (int)fullMsg.size(), 0);
        }
    }
    LogToFile("Broadcast from " + senderId + ": " + msg);
}

// Send private messages
void SendPrivateMessage(string senderId, string receiverId, const string& msg) {
    int receiverIndex = checkclient(receiverId);
    int senderIndex = checkclient(senderId);

    if (senderIndex == -1) {
        // If sender not found (shouldn't happen), ignore
        return;
    }

    if (receiverIndex == -1 || nArrClient[receiverIndex] == 0) {
        string errMsg = "Client " + receiverId + " not available.";
        send(nArrClient[senderIndex], errMsg.c_str(), (int)errMsg.size(), 0);
        LogToFile("Private message failed: " + senderId + " -> " + receiverId);
        return;
    }

    string fullMsg = senderId + " (Private): " + msg;
    send(nArrClient[receiverIndex], fullMsg.c_str(), (int)fullMsg.size(), 0);
    LogToFile("Private message: " + senderId + " -> " + receiverId + ": " + msg);
}

// Remove disconnected clients
void RemoveClient(int nClientSocket) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (nArrClient[i] == nClientSocket) {
            cout << "\nRemoving " << clientNames[i] << endl;
            string leaveMsg = clientNames[i] + " has left the chat.";
            BroadcastMessage("Server", leaveMsg);
            LogToFile("Client removed: " + clientNames[i]);
            closesocket(nArrClient[i]);
            nArrClient[i] = 0;
            clientNames[i].clear();
            break;
        }
    }
}

// Find client by ID
int checkclient(string id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (clientNames[i] == id) {
            return i;
        }
    }
    return -1;
}

// Admin command console (runs in separate thread)
void AdminConsole() {
    string command;
    while (serverRunning) {
        getline(cin, command);

        if (command == "/logs") {
            std::lock_guard<std::mutex> lock(logMutex);
            ifstream file("chat_log.txt");
            if (!file.is_open()) {
                cout << "\nNo chat logs found." << endl;
            }
            else {
                cout << "\n--- Chat Logs ---\n";
                string line;
                while (getline(file, line)) {
                    cout << line << endl;
                }
                cout << "------------------\n";
                file.close();
            }
        }
        else if (command == "/clearlogs") {
            {
                std::lock_guard<std::mutex> lock(logMutex);
                // Truncate the file
                ofstream ofs("chat_log.txt", ios::trunc);
                if (ofs.is_open()) {
                    ofs.close();
                    cout << "\nChat logs cleared." << endl;
                    LogToFile("Chat logs cleared by admin."); // This will re-open file and append an entry
                }
                else {
                    cout << "\nFailed to clear chat logs." << endl;
                }
            }
        }
        else if (command == "/exit") {
            cout << "\nShutting down server..." << endl;
            LogToFile("Server stopped by admin.");
            serverRunning = false;
            // Close listening socket to break select loop
            closesocket(nSocket);
            WSACleanup();
            exit(0);
        }
        else if (!command.empty()) {
            cout << "\nUnknown command. Available commands: /logs, /clearlogs, /exit" << endl;
        }
    }
}

int main() {
    WSADATA ws;
    cout << "Server Starting..." << endl;

    if (WSAStartup(MAKEWORD(2, 2), &ws) < 0) {
        cout << "\nWSA initialization failed." << endl;
        return -1;
    }

    nSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nSocket < 0) {
        cout << "\nSocket creation failed." << endl;
        WSACleanup();
        return -1;
    }

    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = INADDR_ANY;
    memset(&(srv.sin_zero), 0, 8);

    int nOptVal = 1;
    setsockopt(nSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOptVal, sizeof(nOptVal));

    if (bind(nSocket, (sockaddr*)&srv, sizeof(sockaddr)) < 0) {
        cout << "\nBind failed." << endl;
        WSACleanup();
        return -1;
    }

    if (listen(nSocket, MAX_CLIENTS) < 0) {
        cout << "\nListen failed." << endl;
        WSACleanup();
        return -1;
    }

    cout << "\nServer running on port " << PORT << endl;
    LogToFile("Server started on port " + to_string(PORT));

    // Run background threads
    thread broadcaster(BroadcastServerIP);
    thread admin(AdminConsole);
    broadcaster.detach();
    admin.detach();

    // Main server loop
    nMaxFd = nSocket;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    while (serverRunning) {
        FD_ZERO(&fr);
        FD_ZERO(&fw);
        FD_ZERO(&fe);

        FD_SET(nSocket, &fr);
        FD_SET(nSocket, &fe);

        for (int i = 0; i < MAX_CLIENTS; i++) {
            if (nArrClient[i] != 0) {
                FD_SET(nArrClient[i], &fr);
                FD_SET(nArrClient[i], &fe);
            }
        }

        int nRet = select(0, &fr, &fw, &fe, &tv);
        if (nRet > 0) {
            ProcessTheNewRequest();
        }

        Sleep(200);
    }

    WSACleanup();
    return 0;
}
