#include <iostream>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <iphlpapi.h>
#include <set>
#include <vector>
#include <string.h>
#include <tchar.h>

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "iphlpapi.lib")

// 检测失败的端口号
std::set<int> detectHiddenUDPPorts() {
    std::set<int> failedPorts;

    for (int port = 1; port <= 65535; ++port) {
        SOCKET sock = socket(AF_INET, SOCK_DGRAM, 0);
        if (sock == INVALID_SOCKET) continue;

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            failedPorts.insert(port);
        }

        closesocket(sock);
    }

    return failedPorts;
}

// 获取系统当前的 UDP 监听端口号
std::set<int> getSystemLocalUDPPorts() {
    std::set<int> systemPorts;

    ULONG bufferSize = 0;
    GetExtendedUdpTable(nullptr, &bufferSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0);

    std::vector<char> buffer(bufferSize);
    PMIB_UDPTABLE_OWNER_PID udpTable = reinterpret_cast<PMIB_UDPTABLE_OWNER_PID>(buffer.data());

    if (GetExtendedUdpTable(udpTable, &bufferSize, FALSE, AF_INET, UDP_TABLE_OWNER_PID, 0) == NO_ERROR) {
        for (DWORD i = 0; i < udpTable->dwNumEntries; ++i) {
            systemPorts.insert(ntohs((u_short)udpTable->table[i].dwLocalPort));
        }
    }

    return systemPorts;
}

std::set<int> detectHiddenTCPPorts() {
    std::set<int> failedPorts;

    for (int port = 1; port <= 65535; ++port) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) continue;

        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(port);

        if (bind(sock, (sockaddr*)&addr, sizeof(addr)) == SOCKET_ERROR) {
            failedPorts.insert(port);
        }

        closesocket(sock);
    }

    return failedPorts;
}

std::set<int> getSystemLocalTCPPorts() {
    std::set<int> systemPorts;

    ULONG bufferSize = 0;
    GetExtendedTcpTable(nullptr, &bufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0);

    std::vector<char> buffer(bufferSize);
    PMIB_TCPTABLE_OWNER_PID tcpTable = reinterpret_cast<PMIB_TCPTABLE_OWNER_PID>(buffer.data());

    if (GetExtendedTcpTable(tcpTable, &bufferSize, FALSE, AF_INET, TCP_TABLE_OWNER_PID_ALL, 0) == NO_ERROR) {
        for (DWORD i = 0; i < tcpTable->dwNumEntries; ++i) {
            systemPorts.insert(ntohs((u_short)tcpTable->table[i].dwLocalPort));
        }
    }

    return systemPorts;
}

constexpr int MAX_CONCURRENT_SOCKETS = 256;
constexpr int CONNECTION_TIMEOUT_MS = 666;

struct SocketContext {
    SOCKET socket;
    int port;
    WSAEVENT event;
    bool isConnected;
    DWORD tick;
};

// 异步检测端口连接
std::set<int> filterConnectablePortsAsync(const char *ip, const std::set<int>& ports, DWORD timeout, int verb) {
    std::set<int> connectablePorts;
    std::vector<SocketContext> contexts;

    auto addSocketContext = [&](int port) {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, 0);
        if (sock == INVALID_SOCKET) {
            return;
        }

        // 配置套接字非阻塞模式
        u_long nonBlockingMode = 1;
        ioctlsocket(sock, FIONBIO, &nonBlockingMode);

        // 绑定地址
        sockaddr_in addr = {};
        addr.sin_family = AF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = inet_addr(ip);

        // 创建事件并关联套接字
        WSAEVENT event = WSACreateEvent();
        WSAEventSelect(sock, event, FD_CONNECT);

        // 异步连接
        connect(sock, (sockaddr*)&addr, sizeof(addr));

        // 添加到上下文列表
        contexts.push_back({ sock, port, event, false, GetTickCount() });
    };

    auto closeSocketContext = [&](SocketContext& context) {
        closesocket(context.socket);
        WSACloseEvent(context.event);
    };

    auto removeSockets = [&](bool all) {
        if (all) {
            for (auto& it : contexts) {
                closeSocketContext(it);
            }
            contexts.clear();
            return;
        }
        for (auto it = contexts.begin(); it != contexts.end(); /* no increment here */) {
            if (it->isConnected || it->tick == -1) {
                closeSocketContext(*it);
                it = contexts.erase(it);
            }
            else {
                ++it;
            }
        }
    }; 
    auto removeSocketByIndex = [&](size_t index) {
        if (index < contexts.size()) {
            closeSocketContext(*(contexts.begin() + index));
            contexts.erase(contexts.begin() + index);
        }
    };

    int pos = 0;
    // 分批处理端口
    for (auto it = ports.begin(); ;) {
        // 添加一批套接字上下文

        while (it != ports.end() && contexts.size() < MAX_CONCURRENT_SOCKETS) {
            addSocketContext(*it);
            ++it;
            ++pos;
            continue;
        }

        if (verb) {
            fprintf(stderr, "%d/%d, opens:%d       \r", pos, (int)ports.size(), (int)connectablePorts.size());
        }

        if (contexts.size() == 0) {
            break;
        }

        size_t totalEvents = contexts.size();
        size_t currentIndex = 0;

        while (currentIndex < totalEvents) {
            // 计算当前批次的事件数量
            size_t batchSize = (totalEvents - currentIndex > MAXIMUM_WAIT_OBJECTS)
                ? MAXIMUM_WAIT_OBJECTS
                : totalEvents - currentIndex;

            // 创建一个临时数组存储当前批次的事件句柄
            std::vector<WSAEVENT> batchEvents;
            for (size_t j = 0; j < batchSize; j++) {
                batchEvents.push_back(contexts[currentIndex+j].event);
            }

            DWORD waitResult = WSAWaitForMultipleEvents(
                batchSize, &batchEvents[0], FALSE, 100, FALSE);

            if (waitResult >= WSA_WAIT_EVENT_0 && waitResult <= WSA_WAIT_EVENT_0 + batchEvents.size())
            {
                DWORD i = currentIndex + waitResult - WSA_WAIT_EVENT_0;
                WSANETWORKEVENTS netEvents;
                WSAEnumNetworkEvents(contexts[i].socket, contexts[i].event, &netEvents);

                if (netEvents.lNetworkEvents & FD_CONNECT) {
                    if (netEvents.iErrorCode[FD_CONNECT_BIT] == 0) {
                        contexts[i].isConnected = true;
                        connectablePorts.insert(contexts[i].port);
                    }
                }
                contexts[i].tick = -1; //标志待删除
            }

            for (size_t i = currentIndex; i < currentIndex + batchSize; i++) {
                if (GetTickCount() - contexts[i].tick >= timeout) {
                    contexts[i].tick = -1;
                }
            }

            // 更新当前索引，准备处理下一批次
            currentIndex += batchSize;
        }

        // 清理已处理的套接字上下文
        removeSockets(false);
    }

    return connectablePorts;
}

// 检测隐藏端口并输出
void detectHiddenPorts(bool initsock, std::set<int> &hiddenPortsU, std::set<int> &hiddenListenPortT, int verb) {

    WSADATA wsaData;
    if (initsock && WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        if (verb)
            std::cerr << "WSAStartup failed.\n";
        return;
    }

    std::set<int> systemPortsU = getSystemLocalUDPPorts();
    if (verb)
        std::cout << "Detecting UDP ports:\n";
    std::set<int> failedPortsU = detectHiddenUDPPorts();
    ;

    for (int port : failedPortsU) {
        if (systemPortsU.find(port) == systemPortsU.end()) {
            hiddenPortsU.insert(port);
        }
    }

    if (verb)
    {
        if (hiddenPortsU.size()) {
            std::cout << "Suspicious Hidden UDP ports:\n";
            for (int port : hiddenPortsU) {
                std::cout << "Port " << port << " might be hidden.\n";
            }
        }
        else {
            std::cout << "No found hidden UDP ports\n";
        }
    }


    std::set<int> systemPortsT = getSystemLocalTCPPorts();
    if (verb)
        std::cout << "Detecting TCP ports:\n";
    std::set<int> failedPortsT = detectHiddenTCPPorts();
    std::set<int> hiddenPortsT;
    //systemPortsT.erase(135);
    //systemPortsT.erase(3389);

    for (int port : failedPortsT) {
        if (systemPortsT.find(port) == systemPortsT.end()) {
            hiddenPortsT.insert(port);
        }
    }
    
    hiddenListenPortT = filterConnectablePortsAsync("127.6.6.6", hiddenPortsT, 1000, 0);

    if (verb)
    {
        if (hiddenListenPortT.size()) {
            std::cout << "Suspicious Hidden TCP ports:\n";
            for (int port : hiddenListenPortT) {
                std::cout << "Port " << port << " might be hidden.\n";
            }
        }
        else {
            std::cout << "No found hidden TCP ports\n";
        }
    }

    if (initsock) {
        WSACleanup();
    }
}

void test_port_scan()
{
    std::set<int> ports;
    for (int n = 1; n <= 10000; n++) {
        ports.insert(n);
    }
    std::set<int> listenPortT = filterConnectablePortsAsync("127.6.6.6", ports, CONNECTION_TIMEOUT_MS, 1);

    for (auto port : listenPortT) {
        std::cout << "Port " << port << " open.\n";
    }
}

int _tmain(int argc, TCHAR **argv)
{
    _ftprintf(stderr, _T("atrk-win: ports v1.0.0\n"));

    LPCTSTR output_csv = NULL;
    int n;
    for (n = 1; n < argc; n++) {
        if (_tcsicmp(argv[n], _T("-ocsv")) == 0 && (n + 1 < argc)) {
            output_csv = argv[n + 1];
            n++;
        }
    }

    std::set<int> hiddenPortsU;
    std::set<int> hiddenListenPortT;

    detectHiddenPorts(true, hiddenPortsU, hiddenListenPortT, 1);

    if (output_csv) {
        FILE* file = nullptr;
        if (_tfopen_s(&file, output_csv, _T("w")) != 0) {
            _ftprintf(stderr, _T("Failed to open file: %s\n"), output_csv);
            return 1;
        }

        _ftprintf(file, _T("proto,port\n"));
        for (int port : hiddenPortsU) {
            _ftprintf(file, _T("UDP,%d\n"), port);
        }
        for (int port : hiddenListenPortT) {
            _ftprintf(file, _T("TCP,%d\n"), port);
        }

        fclose(file);
    }

    return 0;
}
