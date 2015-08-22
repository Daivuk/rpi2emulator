#include <Windows.h>
#include <Windowsx.h>
#include <gl\gl.h>
#include <gl\glu.h>
#include <thread>
#include <cinttypes>
#include <mutex>
#include <cassert>
#include "arm.h"

int SCREEN_W = 1280;
int SCREEN_H = 720;

struct sFrameBuffer
{
    uint32_t physicalWidth;
    uint32_t physicalHeight;
    uint32_t virtualWidth;
    uint32_t virtualHeight;
    uint32_t pitch;
    uint32_t bitDepth;
    uint32_t x;
    uint32_t y;
    uint32_t pointer;
    uint32_t size;
} frameBuffer = {0};

HGLRC hRC = nullptr;  // Permanent Rendering Context
HDC hDC = nullptr;  // Private GDI Device Context
HWND hWnd = nullptr; // Holds Our Window Handle
bool bDone = false;
GLuint glTexture_frameBuffer = 0;
volatile bool bGPIOInitialized = false;
volatile bool bLEDOn = false;

extern uint32_t r[16];
extern uint8_t mailbox[0x24];

uint32_t programSize = 0;
extern uint32_t memoryEnd;
uint8_t *pMemory = nullptr;

GLuint font;

std::mutex frameMutex;

void swapFrameBuffer();
void doMailbox();
void doFrameBufferMail(uint32_t val);

#define ALLOC_SIZE 256 * 1024 * 1024
#define FRAME_BUFFER_MEMORY_LOCATION (64 * 1024 * 1024)

LRESULT CALLBACK WinProc(HWND handle, UINT msg, WPARAM wparam, LPARAM lparam)
{
    if (msg == WM_DESTROY ||
        msg == WM_CLOSE)
    {
        PostQuitMessage(0);
        return 0;
    }
    else if (msg == WM_MOUSEMOVE)
    {
        auto xPos = GET_X_LPARAM(lparam);
        auto yPos = GET_Y_LPARAM(lparam);

        static bool bCursorShowned = true;

        if (xPos >= 128 &&
            xPos <= SCREEN_W - 8 &&
            yPos >= 8 &&
            yPos <= SCREEN_H - 64)
        {
            if (bCursorShowned) ShowCursor(FALSE);
            bCursorShowned = false;
        }
        else
        {
            if (!bCursorShowned) ShowCursor(TRUE);
            bCursorShowned = true;
        }
    }

    return DefWindowProc(handle, msg, wparam, lparam);
}


int CALLBACK WinMain(
    _In_  HINSTANCE hInstance,
    _In_  HINSTANCE hPrevInstance,
    _In_  LPSTR lpCmdLine,
    _In_  int nCmdShow
    )
{
    // Create window
    WNDCLASS wc = {0};
    wc.style = CS_OWNDC;    // CS_HREDRAW | CS_VREDRAW | CS_OWNDC
    wc.lpfnWndProc = WinProc;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"rpi2emulatorWindow";
    RegisterClass(&wc);
    auto sw = GetSystemMetrics(SM_CXSCREEN);
    auto sh = GetSystemMetrics(SM_CYSCREEN);
    auto w = SCREEN_W;
    auto h = SCREEN_H;
    auto x = (sw - w) / 2;
    auto y = (sh - h) / 2;
    hWnd = CreateWindow(L"rpi2emulatorWindow",
                        L"Raspberry Pi 2 model B Oak Nut OS 1.0 Emulator",
                        WS_POPUP | WS_CAPTION | WS_SYSMENU | WS_VISIBLE,
                        x, y, w, h,
                        nullptr, nullptr, nullptr, nullptr);

    // Initialize OpenGL
    GLuint PixelFormat;
    static PIXELFORMATDESCRIPTOR pfd =  // pfd Tells Windows How We Want Things To Be
    {
        sizeof(PIXELFORMATDESCRIPTOR),  // Size Of This Pixel Format Descriptor
        1,                              // Version Number
        PFD_DRAW_TO_WINDOW |            // Format Must Support Window
        PFD_SUPPORT_OPENGL |            // Format Must Support OpenGL
        PFD_DOUBLEBUFFER,               // Must Support Double Buffering
        PFD_TYPE_RGBA,                  // Request An RGBA Format
        32,                             // Select Our Color Depth
        0, 0, 0, 0, 0, 0,               // Color Bits Ignored
        0,                              // No Alpha Buffer
        0,                              // Shift Bit Ignored
        0,                              // No Accumulation Buffer
        0, 0, 0, 0,                     // Accumulation Bits Ignored
        16,                             // 16Bit Z-Buffer (Depth Buffer)
        0,                              // No Stencil Buffer
        0,                              // No Auxiliary Buffer
        PFD_MAIN_PLANE,                 // Main Drawing Layer
        0,                              // Reserved
        0, 0, 0                         // Layer Masks Ignored
    };
    hDC = GetDC(hWnd);
    PixelFormat = ChoosePixelFormat(hDC, &pfd);
    SetPixelFormat(hDC, PixelFormat, &pfd);
    hRC = wglCreateContext(hDC);
    wglMakeCurrent(hDC, hRC);
    ShowWindow(hWnd, SW_SHOW);  // Show The Window
    SetForegroundWindow(hWnd);  // Slightly Higher Priority
    SetFocus(hWnd);             // Sets Keyboard Focus To The Window

    RECT rect;
    GetClientRect(hWnd, &rect);
    SCREEN_W = rect.right - rect.left;
    SCREEN_H = rect.bottom - rect.top;

    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_ALPHA_TEST);
    glDisable(GL_LIGHTING);
    glEnable(GL_TEXTURE_2D);
    glViewport(0, 0, SCREEN_W, SCREEN_H);
    glMatrixMode(GL_PROJECTION);
    glLoadIdentity();
    glOrtho(0, SCREEN_W, SCREEN_H, 0, -999, 999);
    glMatrixMode(GL_MODELVIEW);
    glLoadIdentity();

    // Create the frame buffer texture
    glGenTextures(1, &glTexture_frameBuffer);
    glBindTexture(GL_TEXTURE_2D, glTexture_frameBuffer);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Font
    glGenTextures(1, &font);
    uint8_t fontData[285 * 5 * 4];
    FILE *fontFic = NULL;
    fopen_s(&fontFic, "font.raw", "rb");
    fread(fontData, 1, 285 * 5 * 4, fontFic);
    fclose(fontFic);
    glBindTexture(GL_TEXTURE_2D, font);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 285, 5, 0, GL_RGBA, GL_UNSIGNED_BYTE, fontData);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

    // Allocate memory
    pMemory = new uint8_t[ALLOC_SIZE]; // 1 gig
    memoryEnd = ALLOC_SIZE;

    // Put the kernel at 0x8000
    FILE *fic = NULL;
    fopen_s(&fic, "C:\\Users\\David\\Documents\\GitHub\\OnutOS\\build\\kernel7.img", "rb");
    fseek(fic, 0, SEEK_END);
    auto fileSize = ftell(fic);
    programSize = (decltype(programSize))fileSize;
    fseek(fic, 0, SEEK_SET);
    fread(pMemory + 0x8000, 1, fileSize, fic);
    fclose(fic);

    // Start the ARM processor thread
    std::thread armThread(armStart);

    MSG msg = {0};
    while (true)
    {
        if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessage(&msg);

            if (msg.message == WM_QUIT)
            {
                bDone = true;
                break;
            }
        }

        doMailbox();
        swapFrameBuffer();
        Sleep(0);
    }

    armThread.join();

    return 0;
}

void drawPin(int x, int y, bool state, float r, float g, float b)
{
    glBegin(GL_QUADS);
    glColor4f(.4f, .4f, .4f, 1);
    glVertex2i(x - 8, y - 8);
    glVertex2i(x - 8, y + 8);
    glVertex2i(x + 8, y + 8);
    glVertex2i(x + 8, y - 8);
    glColor4f(0, 0, 0, 1);
    glVertex2i(x - 7, y - 7);
    glVertex2i(x - 7, y + 7);
    glVertex2i(x + 7, y + 7);
    glVertex2i(x + 7, y - 7);
    if (state)
    {
        glColor4f(r, g, b, 1);
    }
    else
    {
        glColor4f(r * .25f, g * .25f, b * .25f, 1);
    }
    glVertex2i(x - 6, y - 6);
    glVertex2i(x - 6, y + 6);
    glVertex2i(x + 6, y + 6);
    glVertex2i(x + 6, y - 6);
    glEnd();
}

void print(int x, int y, char *text, float r = 1, float g = 1, float b = 1)
{
    int orgX = x;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font);
    glColor4f(r, g, b, 1);
    glBegin(GL_QUADS);

    for (; *text; ++text)
    {
        auto c = *text;
        if (c == '\n')
        {
            y += 6;
            x = orgX;
            continue;
        }
        if (c < ' ' || c > '~') continue;
        auto i = c - ' ';
        i *= 3;
        glTexCoord2f((float)i / 285.f, 0);
        glVertex2i(x, y);
        glTexCoord2f((float)i / 285.f, 1);
        glVertex2i(x, y + 5);
        glTexCoord2f((float)(i + 3) / 285.f, 1);
        glVertex2i(x + 3, y + 5);
        glTexCoord2f((float)(i + 3) / 285.f, 0);
        glVertex2i(x + 3, y);
        x += 4;
    }

    glEnd();
    glDisable(GL_BLEND);
}

void printBig(int x, int y, char *text, float r = 1, float g = 1, float b = 1)
{
    int orgX = x;
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, font);
    glColor4f(r, g, b, 1);
    glBegin(GL_QUADS);

    for (; *text; ++text)
    {
        auto c = *text;
        if (c == '\n')
        {
            y += 12;
            x = orgX;
            continue;
        }
        if (c < ' ' || c > '~') continue;
        auto i = c - ' ';
        i *= 3;
        glTexCoord2f((float)i / 285.f, 0);
        glVertex2i(x, y);
        glTexCoord2f((float)i / 285.f, 1);
        glVertex2i(x, y + 10);
        glTexCoord2f((float)(i + 3) / 285.f, 1);
        glVertex2i(x + 6, y + 10);
        glTexCoord2f((float)(i + 3) / 285.f, 0);
        glVertex2i(x + 6, y);
        x += 8;
    }

    glEnd();
    glDisable(GL_BLEND);
}

void swapFrameBuffer()
{
    if (frameBuffer.pointer)
    {
        static uint8_t *pConverted = nullptr;
        auto len = frameBuffer.virtualWidth * frameBuffer.virtualHeight * (frameBuffer.bitDepth / 4);
        if (!pConverted)
        {
            pConverted = new uint8_t[len];
        }
        memcpy(pConverted, pMemory + frameBuffer.pointer, len);

        glEnable(GL_TEXTURE_2D);
        glBindTexture(GL_TEXTURE_2D, glTexture_frameBuffer);
        if (frameBuffer.bitDepth == 24)
        {
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, frameBuffer.virtualWidth, frameBuffer.virtualHeight, 0, GL_RGB, GL_UNSIGNED_BYTE, pConverted);
        }
        else if (frameBuffer.bitDepth == 16)
        {
            assert(false);
        }
        else if (frameBuffer.bitDepth == 32)
        {
            for (decltype(len) i = 0; i < len; i += 4)
            {
                auto tmp = pConverted[i + 0];
                pConverted[i + 0] = pConverted[i + 2];
                pConverted[i + 2] = tmp;
            }
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, frameBuffer.virtualWidth, frameBuffer.virtualHeight, 0, GL_RGBA, GL_UNSIGNED_BYTE, pConverted);
        }
    }

    glViewport(0, 0, SCREEN_W, SCREEN_H);

    glClearColor(0, 0, 0, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glEnable(GL_TEXTURE_2D);
    glBindTexture(GL_TEXTURE_2D, glTexture_frameBuffer);
    glColor4f(1, 1, 1, 1);
    glBegin(GL_QUADS);
    glTexCoord2f(0, 0);
    glVertex2i(128, 8);
    glTexCoord2f(0, 1);
    glVertex2i(128, SCREEN_H - 64);
    glTexCoord2f(1, 1);
    glVertex2i(SCREEN_W - 8, SCREEN_H - 64);
    glTexCoord2f(1, 0);
    glVertex2i(SCREEN_W - 8, 8);
    glEnd();

    // GPIO
    print(4, 21 - 7, "INIT");
    print(86, 21 - 7, "LED");
    print(4, 64 + 21 * 0 - 7, "01 3.3v\nDC Power");
    print(86, 64 + 21 * 0 - 7, "02 DC\nPower 5v");
    print(4, 64 + 21 * 1 - 7, "03 GPIO02\nSDA1, I2C");
    print(86, 64 + 21 * 1 - 7, "04 DC\nPower 5v");
    print(4, 64 + 21 * 2 - 7, "05 GPIO03\nSCL1, I2C");
    print(86, 64 + 21 * 2 - 7, "06 Ground");
    print(4, 64 + 21 * 3 - 7, "07 GPIO04\nGPIO_GCLK");
    print(86, 64 + 21 * 3 - 7, "08 GPIO14\nTXD0");
    print(4, 64 + 21 * 4 - 7, "09 Ground");
    print(86, 64 + 21 * 4 - 7, "10 GPIO15\nRXD0");
    print(4, 64 + 21 * 5 - 7, "11 GPIO17\nGPIO_GEN0");
    print(86, 64 + 21 * 5 - 7, "12 GPIO18\nGPIO_GEN1");
    print(4, 64 + 21 * 6 - 7, "13 GPIO27\nGPIO_GEN2");
    print(86, 64 + 21 * 6 - 7, "14 Ground");
    print(4, 64 + 21 * 7 - 7, "15 GPIO22\nGPIO_GEN3");
    print(86, 64 + 21 * 7 - 7, "16 GPIO23\nGPIO_GEN4");
    print(4, 64 + 21 * 8 - 7, "17 3.3v\nDC Power");
    print(86, 64 + 21 * 8 - 7, "18 GPIO24\nGPIO_GEN5");
    print(4, 64 + 21 * 9 - 7, "19 GPIO10\nSPI_MOSI");
    print(86, 64 + 21 * 9 - 7, "20 Ground");
    print(4, 64 + 21 * 10 - 7, "21 GPIO09\nSPI_MISO");
    print(86, 64 + 21 * 10 - 7, "22 GPIO25\nGPIO_GEN6");
    print(4, 64 + 21 * 11 - 7, "23 GPIO11\nSPI_CLK");
    print(86, 64 + 21 * 11 - 7, "24 GPIO08\nSPI_CE0_N");
    print(4, 64 + 21 * 12 - 7, "25 Ground");
    print(86, 64 + 21 * 12 - 7, "26 GPIO07\nSPI_CE1_N");
    print(4, 64 + 21 * 13 - 7, "27 ID_SD\nI2C ID EEP");
    print(86, 64 + 21 * 13 - 7, "28 ID_SC\nI2C ID EEP");
    print(4, 64 + 21 * 14 - 7, "29 GPIO05");
    print(86, 64 + 21 * 14 - 7, "30 Ground");
    print(4, 64 + 21 * 15 - 7, "31 GPIO06");
    print(86, 64 + 21 * 15 - 7, "32 GPIO12");
    print(4, 64 + 21 * 16 - 7, "33 GPIO13");
    print(86, 64 + 21 * 16 - 7, "34 Ground");
    print(4, 64 + 21 * 17 - 7, "35 GPIO19");
    print(86, 64 + 21 * 17 - 7, "36 GPIO16");
    print(4, 64 + 21 * 18 - 7, "37 GPIO26");
    print(86, 64 + 21 * 18 - 7, "38 GPIO20");
    print(4, 64 + 21 * 19 - 7, "39 Ground");
    print(86, 64 + 21 * 19 - 7, "40 GPIO21");

    // Registers
    static char text[32] = {0};
    for (int i = 0; i < 10; ++i)
    {
        sprintf_s(text, "R%i = 0x%x", i, r[i]);
        printBig(5, SCREEN_H - 200 + 12 * i, text);
    }
    sprintf_s(text, "R10= 0x%x", r[10]);
    printBig(5, SCREEN_H - 200 + 12 * 10, text);
    sprintf_s(text, "R11= 0x%x", r[11]);
    printBig(5, SCREEN_H - 200 + 12 * 11, text);
    sprintf_s(text, "R12= 0x%x", r[12]);
    printBig(5, SCREEN_H - 200 + 12 * 12, text);
    sprintf_s(text, "SP = 0x%x", r[13]);
    printBig(5, SCREEN_H - 200 + 12 * 13, text);
    sprintf_s(text, "LR = 0x%x", r[14]);
    printBig(5, SCREEN_H - 200 + 12 * 14, text);
    sprintf_s(text, "PC = 0x%x", r[15]);
    printBig(5, SCREEN_H - 200 + 12 * 15, text);

    glDisable(GL_TEXTURE_2D);
    drawPin(54, 21, bGPIOInitialized, 0, 1, 0);
    drawPin(74, 21, bLEDOn, 0, 1, 0);

    // Pins
    drawPin(54, 64 + 21 * 0, false, 1, 0, 0);
    drawPin(74, 64 + 21 * 0, false, 1, 0, 0);
    drawPin(54, 64 + 21 * 1, false, 0, 1, 1);
    drawPin(74, 64 + 21 * 1, false, 1, 0, 0);
    drawPin(54, 64 + 21 * 2, false, 0, 1, 1);
    drawPin(74, 64 + 21 * 2, false, 0, 0, 0);
    drawPin(54, 64 + 21 * 3, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 3, false, 1, .75f, 0);
    drawPin(54, 64 + 21 * 4, false, 0, 0, 0);
    drawPin(74, 64 + 21 * 4, false, 1, .75f, 0);
    drawPin(54, 64 + 21 * 5, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 5, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 6, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 6, false, 0, 0, 0);
    drawPin(54, 64 + 21 * 7, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 7, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 8, false, 1, 0, 0);
    drawPin(74, 64 + 21 * 8, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 9, false, 1, 0, 1);
    drawPin(74, 64 + 21 * 9, false, 0, 0, 0);
    drawPin(54, 64 + 21 * 10, false, 1, 0, 1);
    drawPin(74, 64 + 21 * 10, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 11, false, 1, 0, 1);
    drawPin(74, 64 + 21 * 11, false, 1, 0, 1);
    drawPin(54, 64 + 21 * 12, false, 0, 0, 0);
    drawPin(74, 64 + 21 * 12, false, 1, 0, 1);
    drawPin(54, 64 + 21 * 13, false, 1, 1, 0);
    drawPin(74, 64 + 21 * 13, false, 1, 1, 0);
    drawPin(54, 64 + 21 * 14, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 14, false, 0, 0, 0);
    drawPin(54, 64 + 21 * 15, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 15, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 16, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 16, false, 0, 0, 0);
    drawPin(54, 64 + 21 * 17, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 17, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 18, false, 0, 1, 0);
    drawPin(74, 64 + 21 * 18, false, 0, 1, 0);
    drawPin(54, 64 + 21 * 19, false, 0, 0, 0);
    drawPin(74, 64 + 21 * 19, false, 0, 1, 0);

    // Mail
    printBig(150, SCREEN_H - 50 - 6, "MAILBOX", .75f, .65f, .5f);
    for (int i = 0x0; i <= 0x20; i += 4)
    {
        auto val = *(uint32_t*)(mailbox + i);
        sprintf_s(text, "0x%.2x = 0x%x", i, val);
        printBig(150 + (i % 0x10) * 40, SCREEN_H - 50 + 6 + (i / 0x10) * 12, text);
    }

    SwapBuffers(hDC);
}

void doMailbox()
{
    uint32_t mail = *(uint32_t*)(mailbox + 0x20);
    if (mail)
    {
        auto channel = mail & 0xF;
        auto value = mail & 0xFFFFFFF0;

        if (channel == 0x1)
        {
            doFrameBufferMail(value);
        }
        else
        {
            assert(false);
        }

    }
}

void doFrameBufferMail(uint32_t val)
{
    val -= 0xC0000000;

    memcpy(&frameBuffer, pMemory + val, sizeof(sFrameBuffer));

    frameBuffer.virtualWidth = 1128;
    frameBuffer.virtualHeight = 609;

    auto bbp = (frameBuffer.bitDepth / 8);
    frameBuffer.pitch = frameBuffer.virtualWidth * bbp;
    frameBuffer.size = frameBuffer.virtualWidth * frameBuffer.physicalHeight * bbp;
    frameBuffer.pointer = FRAME_BUFFER_MEMORY_LOCATION + 0xC0000000;

    memcpy(pMemory + val, &frameBuffer, sizeof(sFrameBuffer));

    frameBuffer.pointer = FRAME_BUFFER_MEMORY_LOCATION;

    memset(pMemory + frameBuffer.pointer, 0, frameBuffer.size);

    // Write back the response
    *(uint32_t*)(mailbox + 0x20) = 0x0; // Read
    *(uint32_t*)(mailbox + 0x0) = 0x1; // Write channel with 0 for success
}
