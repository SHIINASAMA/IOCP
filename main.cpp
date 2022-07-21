/// \author kaoru
/// \file main.cpp
/// \version 0.2
/// \date 2022.7.21
/// \brief IOCP 示例

#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <WS2tcpip.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

constexpr static size_t MaxBufferSize = 1024 * 1;
constexpr static size_t NumberOfThreads = 1;

HANDLE hIOCP = INVALID_HANDLE_VALUE;
SOCKET serverSocket = INVALID_SOCKET;
std::vector<std::thread> threadGroup;
std::atomic_bool isShutdown{false};

// 此线程用于不断接收连接，并 Post 一次 Read 事件
void AcceptWorkerThread();
// 此线程用于不断处理 AcceptWorkerThread 所 Post 过来的事件
void EventWorkerThread();

// 用于标识事件的类型
enum class IOType {
    Read,
    Write
};

struct IOContext {
    OVERLAPPED overlapped{};
    WSABUF wsaBuf{MaxBufferSize, buffer};
    CHAR buffer[MaxBufferSize]{};
    IOType type{};
    SOCKET socket = INVALID_SOCKET;
    DWORD nBytes = 0;
};

int main() {
    // 初始化 Windows 网络库
    WSAData data{};
    WSAStartup(MAKEWORD(2, 2), &data);

    struct sockaddr_in address {};
    address.sin_family = AF_INET;
    address.sin_port = htons(8080);
    inet_pton(AF_INET, "127.0.0.1", &address.sin_addr);

    // 初始化 Socket
    unsigned long ul = 1;
    serverSocket = WSASocketW(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (INVALID_SOCKET == serverSocket) {
        perror("FAILED TO CREATE SERVER SOCKET");
        closesocket(serverSocket);
        exit(-1);
    }
    if (SOCKET_ERROR == ioctlsocket(serverSocket, FIONBIO, &ul)) {
        perror("FAILED TO SET NONBLOCKING SOCKET");
        closesocket(serverSocket);
        exit(-2);
    }
    if (SOCKET_ERROR == bind(serverSocket, (const struct sockaddr *) &address, sizeof(address))) {
        perror("FAILED TO BIND ADDRESS");
        closesocket(serverSocket);
        exit(-3);
    }
    if (SOCKET_ERROR == listen(serverSocket, SOMAXCONN)) {
        perror("FAILED TO LISTEN SOCKET");
        closesocket(serverSocket);
        exit(-4);
    }

    // 初始化 IOCP
    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, NumberOfThreads);
    if (INVALID_HANDLE_VALUE == hIOCP) {
        perror("FAILED TO CREATE IOCP HANDLE");
        closesocket(serverSocket);
        exit(-5);
    }

    // 初始化工作线程
    for (size_t i = 0; i < NumberOfThreads; i++) {
        threadGroup.emplace_back(std::thread(EventWorkerThread));
    }

    void *lpCompletionKey = nullptr;
    auto acceptThread = std::thread(AcceptWorkerThread);
    getchar();
    // 按任意键进入退出程序
    isShutdown = true;
    // 有多少个线程就发送 post 多少次，让工作线程收到事件并主动退出
    for (size_t i = 0; i < NumberOfThreads; i++) {
        PostQueuedCompletionStatus(hIOCP, -1, (ULONG_PTR) lpCompletionKey, nullptr);
    }
    acceptThread.join();
    for (auto &thread: threadGroup) {
        thread.join();
    }

    WSACleanup();
    return 0;
}

void AcceptWorkerThread() {
    while (!isShutdown) {
        // 开始监听接入
        SOCKET clientSocket = accept(serverSocket, nullptr, nullptr);
        if (INVALID_SOCKET == clientSocket) continue;

        unsigned long ul = 1;
        if (SOCKET_ERROR == ioctlsocket(clientSocket, FIONBIO, &ul)) {
            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            continue;
        }

        if (nullptr == CreateIoCompletionPort((HANDLE) clientSocket, hIOCP, 0, 0)) {
            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            continue;
        }

        DWORD nBytes = MaxBufferSize;
        DWORD dwFlags = 0;
        auto ioContext = new IOContext;
        ioContext->socket = clientSocket;
        ioContext->type = IOType::Read;
        auto rt = WSARecv(clientSocket, &ioContext->wsaBuf, 1, &nBytes, &dwFlags, &ioContext->overlapped, nullptr);
        auto err = WSAGetLastError();
        if (SOCKET_ERROR == rt && ERROR_IO_PENDING != err) {
            // 发生不为 ERROR_IO_PENDING 的错误
            shutdown(clientSocket, SD_BOTH);
            closesocket(clientSocket);
            delete ioContext;
        }
    }
}

void EventWorkerThread() {
    IOContext *ioContext = nullptr;
    DWORD lpNumberOfBytesTransferred = 0;
    void *lpCompletionKey = nullptr;

    DWORD dwFlags = 0;
    DWORD nBytes = MaxBufferSize;

    while (true) {
        BOOL bRt = GetQueuedCompletionStatus(
                hIOCP,
                &lpNumberOfBytesTransferred,
                (PULONG_PTR) &lpCompletionKey,
                (LPOVERLAPPED *) &ioContext,
                INFINITE);

        if (!bRt) continue;

        // 收到 PostQueuedCompletionStatus 发出的退出指令
        if (lpNumberOfBytesTransferred == -1) break;

        if (lpNumberOfBytesTransferred == 0) continue;

        // 读到，或者写入的字节总数
        ioContext->nBytes = lpNumberOfBytesTransferred;
        // 处理对应的事件
        switch (ioContext->type) {
            case IOType::Read: {
                int nRt = WSARecv(
                        ioContext->socket,
                        &ioContext->wsaBuf,
                        1,
                        &nBytes,
                        &dwFlags,
                        &(ioContext->overlapped),
                        nullptr);
                auto e = WSAGetLastError();
                if (SOCKET_ERROR == nRt && e != WSAGetLastError()) {
                    // 读取发生错误
                    closesocket(ioContext->socket);
                    delete ioContext;
                    ioContext = nullptr;
                } else {
                    // 输出读取到的内容
                    setbuf(stdout, nullptr);
                    puts(ioContext->buffer);
                    fflush(stdout);
                    closesocket(ioContext->socket);
                    delete ioContext;
                    ioContext = nullptr;
                }
            }
            case IOType::Write: {
                // 此项目没有这方面的需求，故不处理
                break;
            }
        }
    }
}