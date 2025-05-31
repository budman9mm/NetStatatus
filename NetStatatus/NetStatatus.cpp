// WinMain.cpp
#define WIN32_LEAN_AND_MEAN

#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <string>
#include <vector>
#include <thread>
#include <chrono>

#pragma comment(lib, "ws2_32.lib")

struct RemoteDevice {
    std::string name;
    std::string ip;
    int port;
    bool isOnline;
};

std::vector<RemoteDevice> remoteDevices = {
    {"FWDOH1",    "192.168.1.130", 47028},
    {"FWDOH2",    "192.168.1.120", 47032},
    {"AFTOH3",    "192.168.1.121", 47020},
    {"MCP4",      "192.168.1.140", 47022},
    {"EFISL5",    "192.168.1.144", 47024},
    {"COMML7",    "192.168.1.154", 47026},
    {"CDU0KP9",   "192.168.1.134", 47034},
    {"CHRONOL10", "192.168.1.123", 47036},
};

bool pollingEnabled = true;
HWND hwndMain;

bool SendHeartbeatAndCheckResponse(SOCKET sock, const RemoteDevice& dev, DWORD timeoutMs = 150) {
    sockaddr_in targetAddr = {};
    targetAddr.sin_family = AF_INET;
    targetAddr.sin_port = htons(dev.port);
    inet_pton(AF_INET, dev.ip.c_str(), &targetAddr.sin_addr);

    const char* pingMsg = "#ping";
    sendto(sock, pingMsg, static_cast<int>(strlen(pingMsg)), 0, (SOCKADDR*)&targetAddr, sizeof(targetAddr));

    fd_set readSet;
    FD_ZERO(&readSet);
    FD_SET(sock, &readSet);

    timeval tv = {};
    tv.tv_sec = 0;
    tv.tv_usec = timeoutMs * 1000;

    if (select(0, &readSet, NULL, NULL, &tv) > 0) {
        char buffer[64] = {};
        sockaddr_in fromAddr = {};
        int fromLen = sizeof(fromAddr);
        int bytes = recvfrom(sock, buffer, sizeof(buffer) - 1, 0, (SOCKADDR*)&fromAddr, &fromLen);
        if (bytes > 0) {
            buffer[bytes] = '\0';
            char ipStr[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &fromAddr.sin_addr, ipStr, sizeof(ipStr));
            if (dev.ip == ipStr && std::string(buffer) == "#pong") {
                return true;
            }
        }
    }

    return false;
}

DWORD WINAPI PollingThread(LPVOID) {
    SOCKET sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sock == INVALID_SOCKET) return 1;

    sockaddr_in localAddr = {};
    localAddr.sin_family = AF_INET;
    localAddr.sin_addr.s_addr = INADDR_ANY;
    localAddr.sin_port = htons(50505);
    bind(sock, (SOCKADDR*)&localAddr, sizeof(localAddr));

    while (true) {
        if (pollingEnabled) {
            for (auto& dev : remoteDevices) {
                dev.isOnline = SendHeartbeatAndCheckResponse(sock, dev);
            }
            InvalidateRect(hwndMain, NULL, TRUE);
        }
        Sleep(2000);
    }

    closesocket(sock);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_COMMAND:
    {
        if (LOWORD(wParam) == 1) {
            pollingEnabled = !pollingEnabled;
            InvalidateRect(hwnd, NULL, TRUE);
        }
        break;
    }
    case WM_PAINT:
	{
        PAINTSTRUCT ps;
        HDC hdc;
        hdc = BeginPaint(hwnd, &ps);
        SetBkMode(hdc, TRANSPARENT);

        int y = 10;
        for (const auto& dev : remoteDevices) {
            std::string label = dev.name + "  " + dev.ip + ":" + std::to_string(dev.port);
            TextOutA(hdc, 30, y, label.c_str(), static_cast<int>(label.size()));
            HBRUSH brush = CreateSolidBrush(dev.isOnline ? RGB(0, 200, 0) : RGB(200, 0, 0));
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, brush);
            Ellipse(hdc, 10, y + 2, 25, y + 17);
            SelectObject(hdc, oldBrush);
            DeleteObject(brush);
            y += 30;
        }

        EndPaint(hwnd, &ps);
        break;
	}
    case WM_DESTROY:
    {
        PostQuitMessage(0);
        break;
    }
    default:
        return DefWindowProc(hwnd, msg, wParam, lParam);
    }
    return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE, LPSTR, int nCmdShow) {
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    const char CLASS_NAME[] = "NetStatusWin";
    WNDCLASS wc = {};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;

    RegisterClass(&wc);

    hwndMain = CreateWindowEx(0, CLASS_NAME, "Remote Device Network Status",
        WS_OVERLAPPEDWINDOW ^ WS_THICKFRAME ^ WS_MAXIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 400, 300,
        NULL, NULL, hInstance, NULL);

    CreateWindow("BUTTON", "Toggle Polling", WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
        250, 10, 120, 30, hwndMain, (HMENU)1, hInstance, NULL);

    ShowWindow(hwndMain, nCmdShow);

    CreateThread(NULL, 0, PollingThread, NULL, 0, NULL);

    MSG msg = {};
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    WSACleanup();
    return 0;
}
