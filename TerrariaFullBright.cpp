#include <iostream>
#include <Windows.h>
#include <tlhelp32.h>
#include <set>
#include <sstream>
#include <vector>
#include <Psapi.h> 

//Terraria's process
HANDLE hProcess = {0};
unsigned long long baseAddress;

//These two are used for the window's dimensions
static const int WIDTH = 300, HEIGHT = 200;

//offsets were found through pointer scans
const std::vector<unsigned short> BRIGHTNESS_INSTRUCTION = {0x8B,0x4D,0xE8, 0x39,0x09,0xE8,0x100, 0x100, 0x100, 0x100,0x85,0xC0,0x74,0x16};

//this is the address for the brightness instruction
unsigned long long getTileLightAddr = 0;
const unsigned char originalInstruction[] = {
    0x8B,0x4D,0xE8, 0x39,0x09
};
//this replaces the original instruction with a jump instruction
unsigned char jump[] = {
     0xE9,0x00,0x00,0x00,0x00
};
//this is what we override it with, what it jumps to
unsigned char opcodes[] = {
    0x56,
    0x8B, 0x75, 0x08,
    0xD9, 0xE8,
    0xD9, 0x1E,
    0xD9, 0xE8,
    0xD9, 0x5E, 0x04,
    0xD9, 0xE8,
    0xD9, 0x5E, 0x08,
    0x5E,
    0xE9, 0x00, 0x00, 0x00, 0x00
};
//toggles if the brightness is enabled or not
bool enabled = false;
unsigned long long containsInMemory(HANDLE& process, const std::vector<unsigned short>& search) {
    if (!search.empty()) {
        //holds what the base address we are on
        long long p = 0;
        //holds the memory information
        MEMORY_BASIC_INFORMATION64 info = {0};

        //run until we looped through every memory block
        while (VirtualQueryEx(process, (void*) p, (PMEMORY_BASIC_INFORMATION) &info, sizeof(info)) == sizeof(info)) {
            //add to move onto next block for next loop
            p += info.RegionSize;
            //create a buffer that holds the data in the spot
            //using a buffer is faster than scanning each address as a single float
            //make sure we can even read/write to the adresss
            if (info.State != MEM_FREE && info.Protect == PAGE_EXECUTE_READWRITE) {
                unsigned char* buffer = new unsigned char[info.RegionSize];
                unsigned long long int bytesRead = 0;
                //read the address and copy to buffer
                if (ReadProcessMemory(process, (void*) info.BaseAddress, buffer, info.RegionSize, &bytesRead)) {
                    if (bytesRead > 0 && search.size() <= info.RegionSize) {
                        //loop through the buffer}
                        for (int j = 0; j <= info.RegionSize; j++) {
                            unsigned char currentChar = 0;
                            bool found = true;
                            for (int i = 0; i < search.size(); i++) {
                                std::memcpy(&currentChar, buffer + j + i, sizeof(unsigned char));
                                if (!(search[i] > 0xFF ||
                                      currentChar == (unsigned char) search[i])) {
                                    found = false;
                                    break;
                                }
                            }
                            if (!found) {
                                continue;
                            }
							delete[] buffer;
                            return info.BaseAddress + j;
                        }
                    }
                }
                delete[] buffer;
            }
        }
    }
    return 0;
}


//this method gets the handle for Minecraft
void getTerraiaHandle(const std::wstring& name) {
    PROCESSENTRY32 entry;
    entry.dwSize = sizeof(PROCESSENTRY32);

    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, NULL);

    if (Process32First(snapshot, &entry)) {
        while (Process32Next(snapshot, &entry)) {
            //test if both strings are the same
            if (!std::wstring(entry.szExeFile).compare(name)) {
                hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, entry.th32ProcessID);
            }
        }
    }
    //close out of snapshot to prevent a leak
    CloseHandle(snapshot);

    HMODULE modules[1024];
    unsigned long cbNeeded;
    if (EnumProcessModules(hProcess, modules, sizeof(modules), &cbNeeded)) {
        baseAddress = (DWORDLONG) modules[0];
    }
}
//creates the jump addresses to go to and writes the opcodes into an allocated block
void createJumpAddrs() {
    void* loc = VirtualAllocEx(hProcess, nullptr, 0x20, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    auto jumpAddr = ((unsigned int) loc - (unsigned int) getTileLightAddr - sizeof(jump));
    std::memcpy(&jump[1], &jumpAddr, 4);

    auto offset = getTileLightAddr - ((unsigned int) loc + (sizeof(opcodes) - sizeof(jump)));
    std::memcpy(&opcodes[sizeof(opcodes) - 4], &offset, 4);
    WriteProcessMemory(hProcess, loc, opcodes, sizeof(opcodes), nullptr);
}
void setToMaxBrightness() {
    if (!enabled) {
        WriteProcessMemory(hProcess, (void*) getTileLightAddr, jump, sizeof(jump), nullptr);
    } else {
        WriteProcessMemory(hProcess, (void*) getTileLightAddr, originalInstruction, sizeof(originalInstruction), nullptr);
    }
}
//Everything below here is for the GUI

//This just matches the text to the system font
void changeFont(const HWND& hwnd) {
    NONCLIENTMETRICS metrics = {};
    metrics.cbSize = sizeof(metrics);
    SystemParametersInfo(SPI_GETNONCLIENTMETRICS, metrics.cbSize, &metrics, 0);
    HFONT font = CreateFontIndirect(&metrics.lfCaptionFont);
    SendMessage(hwnd, WM_SETFONT, (WPARAM) font, true);
}
//The next functions are basically the same where it creates objects like buttons and labels (text)
void createFoundObjects(const HWND& hwnd) {
    HWND foundLabel = CreateWindowW(L"static", L"Enjoy Fullbright!", WS_VISIBLE | WS_CHILD, 100, 10, 100, 50, hwnd, nullptr, nullptr, nullptr);
    HWND closeWindowLabel = CreateWindowW(L"static", L"Toggle Using Shift+F6", WS_VISIBLE | WS_CHILD, 90, 35, 150, 20, hwnd, nullptr, nullptr, nullptr);

    changeFont(foundLabel);
    changeFont(closeWindowLabel);
}
void createCantFindObjects(const HWND& hwnd) {
    HWND cantFindLabel = CreateWindowW(L"static", L"Can't find Terraria or Function Memory", WS_VISIBLE | WS_CHILD, 35, 10, 250, 20, hwnd, nullptr, nullptr, nullptr);
    HWND restartLabel = CreateWindowW(L"static", L"Restart both programs and try again", WS_VISIBLE | WS_CHILD, 45, 35, 190, 20, hwnd, nullptr, nullptr, nullptr);

    changeFont(cantFindLabel);
    changeFont(restartLabel);
}
LRESULT CALLBACK windowProcedure(HWND hwnd, UINT msg, WPARAM wparam, LPARAM lparam) {
    switch (msg) {
        case WM_DESTROY:
        {
            CloseHandle(hProcess);
            //default destroy message
            PostQuitMessage(0);
            break;
        }
        //called on start
        case WM_CREATE:
        {
            //get the process for
            getTerraiaHandle(L"Terraria.exe");

            //if the process doesn't exist then show user corresponding message
            if (hProcess) {         
				//get the address of the brightness instruction
				getTileLightAddr = containsInMemory(hProcess, BRIGHTNESS_INSTRUCTION);
				//if the address is not found then show user corresponding message
				if (getTileLightAddr == 0) {
					createCantFindObjects(hwnd);
					break;
				}
                createFoundObjects(hwnd);
				createJumpAddrs();
            } else {
                createCantFindObjects(hwnd);
            }
            break;
        }
        //used for keybind to toggle
        case WM_HOTKEY: {
			//check if the process is open
            if (hProcess) {
                enabled = !enabled;
                setToMaxBrightness();
            }
			break;
        }
    }
    return DefWindowProcW(hwnd, msg, wparam, lparam);
}
int main() {
    //initizalie wc
    WNDCLASSW wc = {0};
    wc.hbrBackground = (HBRUSH) COLOR_WINDOW;
    wc.hInstance = GetModuleHandle(nullptr);
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"fullbright";
    wc.lpfnWndProc = windowProcedure;
    wc.lpszMenuName = L"Full Bright";
    //Adds icon in corner (117 is icon)
    wc.hIcon = (HICON) LoadImageW(wc.hInstance, MAKEINTRESOURCEW(117), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE | LR_SHARED);

    if (!RegisterClassW(&wc)) {
        return -1;
    }

    //Gets screen to center window
    RECT screen;
    GetWindowRect(GetDesktopWindow(), &screen);

    HWND h = CreateWindowW(wc.lpszClassName, wc.lpszMenuName, WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_VISIBLE, screen.right / 2 - WIDTH / 2, screen.bottom / 2 - HEIGHT / 2, WIDTH, HEIGHT, nullptr, nullptr, wc.hInstance, nullptr);
	RegisterHotKey(h, 1, MOD_SHIFT, VK_F6);
    MSG msg = {nullptr};

    //loop over messages
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}