#include <WinSock2.h>
#include <WS2tcpip.h>
#include <windows.h>
#include <iostream>
#include <vector>
#include <thread>
#include <atomic>

#pragma comment(lib, "ws2_32.lib")

#define DEFAULT_PORT "8080"
#define DEFAULT_BUFLEN 1024
#define MAX_CLIENTS 1000
class IOCPSever
{
private:
    SOCKET listenSocket;
    HANDLE comletionPort;
    std::atomic<bool> running;
    std::vector<std::thread> workerThreads;
    struct ClientContext
    {
        OVERLAPPED overlapped;
        SOCKET socket;
        WSABUF wsaBuf;
        char buffer[DEFAULT_BUFLEN];
        int bytesTransferred;
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
        for(int i=0;i<numThreads;++i)
        {
            workerThreads.emplace_back(&IOCPSever::workerThreads,this);
        }
    }
    void WorkerThread()
    {
        while(running)
        {
            DWORD bytesTransferred=0;
            ULONG_PTR completionKey=0;
            OVERLAPPED* overlapped=nullptr;
            //等待I/O完成
            BOOL success=GetQueuedCompletionStatus(
                comletionPort,
                &bytesTransferred,
                &completionKey,
                &overlapped,
                INFINITE
            );
            if(!success)
            {
                if(overlapped)
                {
                    ClientContext* context=CONTAINING_RECORD(overlapped,ClientContext,overlapped);
                    std::cout<<"Client disconnected: "<<context->socket<<std::endl;
                    
                }
            }
        }
    }
};
int main(int, char **)
{
    std::cout << "Hello, from CSocketSeverIoEpoll!\n";
}
