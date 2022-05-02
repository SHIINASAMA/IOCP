#pragma once
#include <WinSock2.h>
#pragma comment(lib, "ws2_32.lib")
#include <WS2tcpip.h>
#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

constexpr static size_t MaxBufferSize = 1024 * 1;
constexpr static size_t NumberOfThreads = 1;

SOCKET server;
HANDLE hIOCP;
std::atomic_bool isShutdown{false};

enum class OperationType {
    Send,
    Recv
};

struct IOContext {
    OVERLAPPED overlapped{};
    SOCKET socket = INVALID_SOCKET;
    WSABUF wsaBuf{MaxBufferSize, buffer};
    char buffer[MaxBufferSize]{};
    OperationType operationType{};
    DWORD totalBytes = 0;
};

void workerThread() {
    IOContext *ioContext;
    DWORD nBytes = MaxBufferSize;
    DWORD dwFlags = 0;
    DWORD dwIoContextSize;
    void *lpCompletionKey = nullptr;
    while (!isShutdown) {
        GetQueuedCompletionStatus(hIOCP, &dwIoContextSize, (PULONG_PTR) &lpCompletionKey, (LPOVERLAPPED *) &ioContext, INFINITE);
        if (dwIoContextSize == 0) {
            closesocket(ioContext->socket);
            delete ioContext;
            continue;
        }

        int32_t nRet;
        switch (ioContext->operationType) {
            case OperationType::Send:
                nRet = WSASend(ioContext->socket,
                               &(ioContext->wsaBuf),
                               1,
                               &nBytes,
                               dwFlags,
                               &(ioContext->overlapped),
                               nullptr);
                if (nRet == SOCKET_ERROR && ERROR_IO_PENDING != WSAGetLastError()) {
                    perror("not io pending!!!");
                    closesocket(ioContext->socket);
                    delete ioContext;
                }
                break;
            case OperationType::Recv:
                nRet = WSARecv(
                        ioContext->socket,
                        &ioContext->wsaBuf,
                        1,
                        &nBytes,
                        &dwFlags,
                        &ioContext->overlapped,
                        nullptr);
                if (nRet == SOCKET_ERROR && ERROR_IO_PENDING != WSAGetLastError()) {
                    perror("not io pending!!!");
                    closesocket(ioContext->socket);
                    delete ioContext;
                }
                break;
        }
    }
}

int main() {
    // WSA 初始化
    WSAData wsaData{};
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    // socket 初始化
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    inet_pton(AF_INET, "0.0.0.0", &serverAddress.sin_addr);

    server = WSASocketW(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (-1 == bind(server, (const struct sockaddr *) &serverAddress, sizeof(sockaddr))) {
        perror("failed to bind the address");
        return -1;
    }
    listen(server, SOMAXCONN);

    // 创建 IOCP
    hIOCP = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, NumberOfThreads);
    if (hIOCP == INVALID_HANDLE_VALUE) {
        perror("failed to create hIOCP");
        return -1;
    }

    // 创建对应线程
    std::vector<std::thread> threadGroup;
    for (int32_t i = 0; i < NumberOfThreads; i++) {
        threadGroup.emplace_back(std::thread(workerThread));
    }

    while (true) {
        SOCKET client = accept(server, nullptr, nullptr);

        if (CreateIoCompletionPort((HANDLE) client, hIOCP, 0, 0) == nullptr) {
            perror("failed to bind the client to an existing iocp");
            closesocket(client);
            continue;
        }

        auto data = new IOContext;
        data->socket = client;
        data->operationType = OperationType::Recv;

        DWORD nBytes = MaxBufferSize;
        DWORD dwFlags = 0;

        int32_t nRet = WSARecv(client, &data->wsaBuf, 1, &nBytes, &dwFlags, &data->overlapped, nullptr);
        if (nRet == SOCKET_ERROR && (ERROR_IO_PENDING != WSAGetLastError())) {
            perror("not io pending!!!");
            delete data;
            continue;
        }

        setbuf(stdout, nullptr);
        printf("%s\n\n", data->buffer);
        fflush(stdout);
        closesocket(client);
//        delete data;
    }
}