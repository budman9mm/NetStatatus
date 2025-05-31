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
    bool pollingEnabled = true;
    HWND checkboxHandle = nullptr; // Handle to the checkbox control
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

    const char* pingMsg = "$pi";
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
            if (dev.ip == ipStr && std::string(buffer) == "$pong") {
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
                if (!dev.pollingEnabled) {
                    continue; // Skip polling for this device
                }
                dev.isOnline = SendHeartbeatAndCheckResponse(sock, dev);
            }
            InvalidateRect(hwndMain, NULL, TRUE);
        }
		Sleep(60000);  // Poll every 60 seconds
    }

    closesocket(sock);
    return 0;
}

LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        CREATESTRUCT* pCreate = reinterpret_cast<CREATESTRUCT*>(lParam);
        HINSTANCE hInstanceLocal = pCreate->hInstance;

        int y = 10;
        for (size_t i = 0; i < remoteDevices.size(); ++i) {
            HWND hCheckbox = CreateWindowExA(
                0, "BUTTON", "", WS_CHILD | WS_VISIBLE | BS_CHECKBOX,
                10, y, 20, 20, hwnd, (HMENU)(1000 + i), hInstanceLocal, NULL
            );
            remoteDevices[i].checkboxHandle = hCheckbox;
            y += 30;
        }
        break;
    }

    case WM_COMMAND: {
        int id = LOWORD(wParam);
        if (id >= 1000 && id < 1000 + remoteDevices.size()) {
            size_t index = id - 1000;
            LRESULT state = SendMessage(remoteDevices[index].checkboxHandle, BM_GETCHECK, 0, 0);
            remoteDevices[index].pollingEnabled = (state == BST_CHECKED);
            InvalidateRect(hwnd, NULL, TRUE); // Trigger a repaint
        }
        break;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        SetBkMode(hdc, TRANSPARENT);

        int y = 10;
        for (const auto& dev : remoteDevices) {
            std::string label = dev.name + "  " + dev.ip + ":" + std::to_string(dev.port);
            TextOutA(hdc, 40, y + 2, label.c_str(), static_cast<int>(label.size()));

            COLORREF color;
            if (!dev.pollingEnabled) {
                color = RGB(255, 255, 0); // Yellow
            }
            else {
                color = dev.isOnline ? RGB(0, 200, 0) : RGB(200, 0, 0); // Green or Red
            }

            HBRUSH brush = CreateSolidBrush(color);
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
