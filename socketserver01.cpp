#include <iostream>
#include <winsock.h>
#include <string>
using namespace std;

#define PORT 9909
#define MAX_CLIENTS 5

struct sockaddr_in srv;
fd_set fr, fw, fe;
int nMaxFd;
int nSocket;
int nArrClient[MAX_CLIENTS] = { 0 };
string clientNames[MAX_CLIENTS];

// Function declarations
void BroadcastMessage(string senderId, const string& msg);
void SendPrivateMessage(string senderId, string receiverId, const string& msg);
void RemoveClient(int nClientSocket);
int checkclient(string id);

void ProcessNewMessages(int nClientSocket, string clientId) {
    char buff[512] = { 0 };

    
    int nRet = recv(nClientSocket, buff, 511, 0);

    if (nRet <= 0) {
        cout << "\nClient " << clientId << " Disconnected.";
        RemoveClient(nClientSocket);
        return;
    }

    string message(buff);

    message.erase(message.find_last_not_of("\r\n") + 1); // trim spaces/newlines

    if (message.empty()) return; // ignore empty messages
    cout << "\nMessage from Client " << clientId << ": " << message;

    // Check if message format is "<id>:<msg>"
    size_t colonPos = message.find(':');
    if (colonPos != string::npos) {
        //int targetId = stoi(message.substr(0, colonPos));
        string targetId = message.substr(0,colonPos);
        string msgToSend = message.substr(colonPos + 1);
        SendPrivateMessage(clientId, targetId, msgToSend);
    }
    else {
        // Broadcast if no target specified
        BroadcastMessage(clientId, message);
    }
}

void ProcessTheNewRequest() {
    if (FD_ISSET(nSocket, &fr)) {
        int nLen = sizeof(struct sockaddr);
        int nClientSocket = accept(nSocket, NULL, &nLen);

        if (nClientSocket > 0) {
            int nIndex;
            for (nIndex = 0; nIndex < MAX_CLIENTS; nIndex++) {
                if (nArrClient[nIndex] == 0) {
                    nArrClient[nIndex] = nClientSocket;

                    // Ask for client Name
                    string askName = "Enter your Name: ";
                    send(nClientSocket, askName.c_str(), askName.size(), 0);

                    // Receive the Name
                    char nameBuff[100] = { 0 };
                    int nRet = recv(nClientSocket, nameBuff, 99, 0);

                    if (nRet > 0) {
                        clientNames[nIndex] = string(nameBuff);

                        //remove newline or carriage return
                        clientNames[nIndex].erase(clientNames[nIndex].find_last_not_of("\r\n") + 1);

                        cout << "\nClient connected as : " << clientNames[nIndex];

                        string welcomeMsg = "Welcome  " + clientNames[nIndex] + "! You can now chat.\n";
                        send(nClientSocket, welcomeMsg.c_str(), welcomeMsg.size(), 0);

                        string joinMsg = clientNames[nIndex] + " has joined the chat";
                        BroadcastMessage("Server", joinMsg);

                        
                    }
                    else {
                        cout << "\nFailed to get name. closing connection.";
                        closesocket(nClientSocket);
                        nArrClient[nIndex] = 0;
                    }
                    
                    
                    break;
                }
            }
            if (nIndex == MAX_CLIENTS) {
                cout << "\nNo space for new connection.";
                send(nClientSocket, "Server full", 11, 0);
                closesocket(nClientSocket);
            }
        }
    }
    else {
        for (int nIndex = 0; nIndex < MAX_CLIENTS; nIndex++) {
            if (nArrClient[nIndex] != 0 && FD_ISSET(nArrClient[nIndex], &fr)) {
                ProcessNewMessages(nArrClient[nIndex],clientNames[nIndex]);
            }
        }
    }
}

void BroadcastMessage(string senderId, const string& msg) {
    string fullMsg =  senderId + " (Broadcast): " + msg;
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (nArrClient[i]!=0 && clientNames[i] != senderId) {
            send(nArrClient[i], fullMsg.c_str(), fullMsg.size(), 0);
        }
    }
}

void SendPrivateMessage(string senderId, string receiverId, const string& msg) {

	int receiverIndex = checkclient(receiverId);
	int senderIndex = checkclient(senderId);

    if (receiverIndex ==-1 || nArrClient[receiverIndex] == 0) {
        string errMsg = "Client " + receiverId + " not available.";
        send(nArrClient[senderIndex], errMsg.c_str(), errMsg.size(), 0);
        return;
    }

    string fullMsg = senderId + " (Private): " + msg;
    send(nArrClient[receiverIndex], fullMsg.c_str(), fullMsg.size(), 0);
}

void RemoveClient(int nClientSocket) {
   
    for (int i = 0; i < MAX_CLIENTS; i++) {
        if (nArrClient[i] == nClientSocket) {
            cout << "\nRemoving " << clientNames[i];
            string leaveMsg = clientNames[i] + " has left the chat.";
            BroadcastMessage(clientNames[i], leaveMsg);
            closesocket(nArrClient[i]);
            nArrClient[i] = 0;
            clientNames[i].clear();
            break;
        }
    }
}


int checkclient(string id) {
    for (int i = 0; i < MAX_CLIENTS; i++) {

        if (clientNames[i] == id) {
            return i;
        }
    }
            return -1;
        
    }

int main() {
    WSADATA ws;
    if (WSAStartup(MAKEWORD(2, 2), &ws) < 0) {
        cout << "\nWSA initialization failed.";
        return -1;
    }
    cout << "\nWSA initialized successfully.";

    nSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (nSocket < 0) {
        cout << "\nSocket creation failed.";
        WSACleanup();
        return -1;
    }
    cout << "\nSocket created successfully.";

    srv.sin_family = AF_INET;
    srv.sin_port = htons(PORT);
    srv.sin_addr.s_addr = INADDR_ANY;
    memset(&(srv.sin_zero), 0, 8);

    int nOptVal = 1;
    setsockopt(nSocket, SOL_SOCKET, SO_REUSEADDR, (const char*)&nOptVal, sizeof(nOptVal));

    if (bind(nSocket, (sockaddr*)&srv, sizeof(sockaddr)) < 0) {
        cout << "\nBind failed.";
        WSACleanup();
        return -1;
    }
    cout << "\nBind successful.";

    if (listen(nSocket, MAX_CLIENTS) < 0) {
        cout << "\nListen failed.";
        WSACleanup();
        return -1;
    }
    cout << "\nServer listening on port " << PORT;

    nMaxFd = nSocket;
    struct timeval tv;
    tv.tv_sec = 1;
    tv.tv_usec = 0;

    while (true) {
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

        Sleep(200); // short delay to reduce CPU load
    }

    WSACleanup();
    return 0;
}
