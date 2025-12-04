#include <WinSock2.h> //Windows Socket API 主要头文件
#include <WS2tcpip.h> //Socket扩展功能(getAddinfo等)
#include <windows.h>  // windows API核心头文件
#include <iostream>   // 标准输入输出流
#include <vector>     // 动态数组容器
#include <thread>     // 多线程支持
#include <atomic>     // 原子操作支持

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT "8080"
#define DEFAULT_BUFLEN 1024
#define MAX_CLIENTS 1000
class IOCPSever
{
private:
    SOCKET listenSocket;                    // 监听socket句柄
    HANDLE comletionPort;                   // IOCP完成端口句柄
    std::atomic<bool> running;              // 原子布尔值，用于线程安全的状态控制
    std::vector<std::thread> workerThreads; // 工作者线程容器
    // 客户端上下文结构体，存储每个连接的信息
    struct ClientContext
    {
        OVERLAPPED overlapped;       // 重叠I/O结构，用于异步操作
        SOCKET socket;               // 客户端socket句柄
        WSABUF wsaBuf;               // windows Socket异步缓冲区结构
        char buffer[DEFAULT_BUFLEN]; // 数据缓冲区
        int bytesTransferred;        // 传输的字节数
    };

public:
    IOCPSever() : listenSocket(INVALID_SOCKET), comletionPort(NULL), running(false) {}
    bool Initialize()
    {
        // 初始化winSock
        WSADATA wsaData;
        if (WSAStartup(MAKEWORD(2, 2), &wsaData) != 0)
        {
            std::cerr << "WSAStartup failed:" << WSAGetLastError() << std::endl;
            return false;
        }

        // 创建完成端口
        comletionPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (comletionPort == NULL)
        {
            std::cerr << "CreateIoCompleteionPort failed:" << GetLastError() << std::endl;
            return false;
        }

        // 创建监听socket
        struct addrinfo *result = NULL, hints;
        ZeroMemory(&hints, sizeof(hints));
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        hints.ai_flags = AI_PASSIVE;
        if (getaddrinfo(NULL, DEFAULT_PORT, &hints, &result) != 0)
        {
            std::cerr << "getaddrinfo failed:" << WSAGetLastError() << std::endl;
            return false;
        }

        listenSocket = socket(result->ai_family, result->ai_socktype, result->ai_protocol);
        if (listenSocket == INVALID_SOCKET)
        {
            std::cerr << "socket failed:" << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            return false;
        }

        // 绑定socket
        if (bind(listenSocket, result->ai_addr, (int)result->ai_addrlen) == SOCKET_ERROR)
        {
            std::cerr << "bind failed:" << WSAGetLastError() << std::endl;
            freeaddrinfo(result);
            closesocket(listenSocket);
            return false;
        }

        // 开始监听
        if (listen(listenSocket, SOMAXCONN) == SOCKET_ERROR)
        {
            std::cerr << "listem failed:" << WSAGetLastError() << std::endl;
            closesocket(listenSocket);
            return false;
        }
        return true;
    }

    void StartWorkerThreads(int numThreads)
    {
        for (int i = 0; i < numThreads; ++i)
        {
            workerThreads.emplace_back(&IOCPSever::workerThreads, this);
        }
    }
    void WorkerThread()
    {
        while (running)
        {
            DWORD bytesTransferred = 0;
            ULONG_PTR completionKey = 0;
            OVERLAPPED *overlapped = nullptr;
            // 等待I/O完成
            BOOL success = GetQueuedCompletionStatus(
                comletionPort,
                &bytesTransferred,
                &completionKey,
                &overlapped,
                INFINITE);
            if (!success)
            {
                if (overlapped)
                {
                    ClientContext *context = CONTAINING_RECORD(overlapped, ClientContext, overlapped);
                    std::cout << "Client disconnected: " << context->socket << std::endl;
                    closesocket(context->socket);
                    delete context;
                }
                continue;
            }
            if (completionKey == 0)
            {
                // 新连接
                AcceptConnections();
            }
            else
            {
                // 数据处理
                ClientContext *context = (ClientContext *)completionKey;
                if (bytesTransferred == 0)
                {
                    // 连接关闭
                    std::cout << "Client disconnected: " << context->socket << std::endl;
                    closesocket(context->socket);
                    delete context;
                }
                else
                {
                    // 处理接收到的数据
                }
            }
        }
    }
    void AcceptConnections()
    {
        SOCKET clientSocket = accept(listenSocket, NULL, NULL);
        if (clientSocket == INVALID_SOCKET)
        {
            std::cerr << "accept failed:" << WSAGetLastError() << std::endl;
            return;
        }

        // 创建客户端上下文
        ClientContext *context = new ClientContext();
        ZeroMemory(context, sizeof(ClientContext));
        context->socket = clientSocket;
        context->wsaBuf.buf = context->buffer;
    }
    void PostRecv(ClientContext *context)
    {
        DWORD flages = 0;
        DWORD bytesRecv = 0;
        int result = WSARecv(
            context->socket,
            &context->wsaBuf,
            1,
            &bytesRecv,
            0,
            NULL,
            NULL);
    }
    void ProcessData(ClientContext *context, DWORD bytesTransferred)
    {
        // 简单回显服务器逻辑
        std::cout << "Received " << bytesTransferred << " bytes from client" << context->socket << ": " << std::string(context->buffer, bytesTransferred) << std::endl;

        // 回显数据
        WSABUF wsaBuf;
        wsaBuf.buf = context->buffer;
        wsaBuf.len = bytesTransferred;

        DWORD bytesSend = 0;
        int result = WSASend(
            context->socket,
            &wsaBuf,
            1,
            &bytesSend,
            0,
            NULL,
            NULL);
        if (result == SOCKET_ERROR)
        {
            std::cerr << "WSASend failed: " << WSAGetLastError() << std::endl;
        }
    }
    void Run()
    {
        running = true;

        // 启动工作者线程(通常为CPU核心数*2)
        int numThreads = std::thread::hardware_concurrency() * 2;
        StartWorkerThreads(numThreads);

        std::cout << "Server started on port " << DEFAULT_PORT
                  << " with" << numThreads << " worker threads" << std::endl;

        // 投递初始的接收连接操作
        PostQueuedCompletionStatus(comletionPort, 0, 0, NULL);

        // 等待工作者线程
        for (auto &thread : workerThreads)
        {
            if (thread.joinable())
            {
                thread.join();
            }
        }
    }
    void Stop()
    {
        running = false;
        // 通知所有工作者线程退出
        for (size_t i = 0; i < workerThreads.size(); ++i)
        {
            PostQueuedCompletionStatus(comletionPort, 0, 0, NULL);
        }
        closesocket(listenSocket);
        CloseHandle(comletionPort);
        WSACleanup();
    }
    ~IOCPSever()
    {
        Stop();
    }
};

int main(int, char **)
{
    IOCPSever server;
    if (!server.Initialize())
    {
        return 1;
    }
    server.Run();
    return 0;
}
