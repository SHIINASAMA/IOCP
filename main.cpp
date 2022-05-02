

int main() {
    // socket 初始化
    sockaddr_in serverAddress{};
    serverAddress.sin_family = AF_INET;
    serverAddress.sin_port = htons(8080);
    inet_pton(AF_INET, "0.0.0.0", &serverAddress.sin_addr);

    SOCKET server = WSASocketW(AF_INET, SOCK_STREAM, 0, nullptr, 0, WSA_FLAG_OVERLAPPED);
    if (-1 == bind(server, (const struct sockaddr *) &serverAddress, sizeof(sockaddr))) {
        perror("failed to bind the address");
        return -1;
    }
    listen(server, SOMAXCONN);

    // 创建完成端口
}
