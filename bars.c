#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_BARS 100
#define MAX_VISIBLE_BARS 5
#define MAX_TITLE_LENGTH 80
#define JSON_BUFFER_SIZE 10240

typedef struct {
    char title[MAX_TITLE_LENGTH];
    int current;
    int total;
    COLORREF color;
} Bar;

HBRUSH unfilled_brush = NULL;
HBRUSH bar_brushes[MAX_BARS] = {NULL};
Bar bars[MAX_BARS];
int barCount = 0;
int scrollOffset = 0; // Used for vertical scrolling
const int barSpacing = 40; // Space between bars
const int barHeight = 40;   // Height of each bar
const char *defaultContent = "[
    {\"title\": \"Bar 1\", \"current\": 50, \"total\": 100, \"color\": \"#FF5733\"},
    {\"title\": \"Bar 2\", \"current\": 75, \"total\": 100, \"color\": \"#33FF57\"},
    {\"title\": \"Bar 3\", \"current\": 25, \"total\": 100, \"color\": \"#3357FF\"},
    {\"title\": \"Bar 4\", \"current\": 80, \"total\": 100, \"color\": \"#F1C40F\"},
    {\"title\": \"Bar 5\", \"current\": 40, \"total\": 100, \"color\": \"#9B59B6\"}
]";

LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
void LoadBarsFromJSON(const char *filename);
COLORREF HexToColor(const char *hex);
void DrawBarChart(HDC hdc, int clientWidth, int clientHeight);
void DisplayError(const char *message);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    LoadBarsFromJSON("bars.json");

    unfilled_brush = CreateSolidBrush(RGB(240, 240, 240));
    for(int i = 0; i < barCount; i++) {
        bar_brushes[i] = CreateSolidBrush(bars[i].color);
    }

    const char CLASS_NAME[] = "ProgressBarDashboardApp";
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = CLASS_NAME;
    wc.style = CS_HREDRAW | CS_VREDRAW;

    if (!RegisterClass(&wc)) {
        DisplayError("Failed to register window class.");
        return 0;
    }

    // Set initial window size
    int initialWidth = 480;
    int initialHeight = (MAX_VISIBLE_BARS * (barHeight + barSpacing)) + 2 * barSpacing;

    // Get the desktop's work area (the area excluding the taskbar)
    RECT workArea;
    SystemParametersInfo(SPI_GETWORKAREA, 0, &workArea, 0);

    // Calculate the position for bottom-right placement with offsets
    int posX = workArea.right - initialWidth - 20; // 20 pixels from the right edge
    int posY = workArea.bottom - initialHeight - 20; // 20 pixels from the bottom edge

    HWND hwnd = CreateWindowEx(
        0,
        CLASS_NAME,
        "Progress Bars Dashboard",
        WS_OVERLAPPEDWINDOW | WS_VSCROLL, // Enable vertical scroll bar
        posX, posY, initialWidth, initialHeight,
        NULL, NULL, hInstance, NULL
    );

    if (!hwnd) {
        DisplayError("Failed to create window.");
        return 0;
    }

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    return 0;
}

// Display error messages
void DisplayError(const char *message) {
    MessageBox(NULL, message, "Error", MB_OK | MB_ICONERROR);
}

// Load bars from the JSON file
void LoadBarsFromJSON(const char *filename) {
    FILE *file = fopen(filename, "r");
    if (file == NULL) {

    // Ask the user if they want to create a default file using a dialog box
        int response = MessageBox(NULL, "The file 'bars.json' does not exist. \nDo you want to create it using some initial values?", 
                                   "Could not open bars.json", MB_YESNO | MB_ICONQUESTION);

        if (response == IDYES) {
            // Create the file and write default content
            file = fopen(filename, "w"); // Open the file for writing
            if (file) {
                fprintf(file, "%s", defaultContent); // Write default content to the file
                fclose(file); // Close the file after writing
                MessageBox(NULL, "Default file 'bars.json' created successfully.", "Success", MB_OK | MB_ICONINFORMATION);
                LoadBarsFromJSON(filename);
                return;
            } else {
                DisplayError("Could not create default file.");
            }
        } else {
                ExitProcess(0);
        }
    }

    char jsonBuffer[JSON_BUFFER_SIZE];
    size_t bytesRead = fread(jsonBuffer, sizeof(char), JSON_BUFFER_SIZE - 1, file);
    jsonBuffer[bytesRead] = '\0';
    fclose(file);

    char *start = jsonBuffer;
    while (*start && barCount < MAX_BARS) {
        start = strstr(start, "{");
        if (!start) break;

        Bar newBar = {"", 0, 0, RGB(0, 0, 0)};

        // Read title
        char *titlePtr = strstr(start, "\"title\"");
        if (titlePtr) {
            titlePtr = strchr(titlePtr, ':') + 1;
            sscanf(titlePtr, " \"%49[^\"]\"", newBar.title);
        }

        // Read current
        char *currentPtr = strstr(start, "\"current\"");
        if (currentPtr) {
            sscanf(currentPtr, "\"current\": %d", &newBar.current);
        }

        // Read total
        char *totalPtr = strstr(start, "\"total\"");
        if (totalPtr) {
            sscanf(totalPtr, "\"total\": %d", &newBar.total);
        }

        // Read color
        char *colorPtr = strstr(start, "\"color\"");
        if (colorPtr) {
            char hexColor[8];
            sscanf(colorPtr, "\"color\": \"%7[^\"]\"", hexColor);
            newBar.color = HexToColor(hexColor);
        }

        bars[barCount++] = newBar;
        
        // Move to after this object
        start = strstr(start, "}");
        if (!start) break;
        start++;
    }

    if (barCount == 0) {
        DisplayError("No bars loaded from JSON.");
    }
}

// Convert hex color to COLORREF
COLORREF HexToColor(const char *hex) {
    int r, g, b;
    if (sscanf(hex, "#%02x%02x%02x", &r, &g, &b) != 3) {
        DisplayError("Invalid color format in JSON.");
        return RGB(0, 0, 0);
    }
    return RGB(r, g, b);
}

// Draw the bar chart with classic GDI
void DrawBarChart(HDC hdc, int clientWidth, int clientHeight) {
    if (barCount <= 0) return;

    int margin = 20; // Margin from the left and right
    int maxWidth = clientWidth - 2 * margin;

    // Ensure the scroll offset is within the bounds of the content
    if (scrollOffset < 0) {
        scrollOffset = 0;
    }

    // Draw each bar
    for (int i = 0; i < barCount; i++) {
        int yOffset = barSpacing + (i * (barHeight + barSpacing)) - scrollOffset; // Inverse scrolling
        if (yOffset < 0 || yOffset > clientHeight) continue; // Only draw visible bars

        // Calculate bar dimensions
        int barWidth = (bars[i].current * maxWidth) / bars[i].total;
        int unfilledWidth = maxWidth - barWidth;

        // Draw the unfilled part
        RECT unfilledRect = { margin, yOffset, margin + maxWidth, yOffset + barHeight };
        FillRect(hdc, &unfilledRect, unfilled_brush);

        // Draw the filled part
        RECT filledRect = { margin, yOffset, margin + barWidth, yOffset + barHeight };
        FillRect(hdc, &filledRect, bar_brushes[i]);

        // Draw title text above the bar with fixed distance from the top
        SetBkMode(hdc, TRANSPARENT); // Make background transparent
        TextOut(hdc, margin, yOffset - 20, bars[i].title, strlen(bars[i].title));

        // Display percentage progress in the middle of the bar
        char progressText[10];
        sprintf(progressText, "%.0f%% (%d/%d)", (float)bars[i].current / bars[i].total * 100, bars[i].current, bars[i].total);
        
        SIZE textSize;
        GetTextExtentPoint32(hdc, progressText, strlen(progressText), &textSize);
        TextOut(hdc, margin + (maxWidth - textSize.cx) / 2, yOffset + (barHeight - textSize.cy) / 2, progressText, strlen(progressText));
    }
}

// Function to get the height of a window by its handle
int getWindowHeight(HWND hwnd) {
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        return rect.bottom - rect.top; // Height = Bottom - Top
    }
    return -1; // Return -1 on error
}

int offsetMax(HWND hwnd, int scrollOffset) {
    if (scrollOffset > (barCount + 1) * (barHeight + barSpacing) - getWindowHeight(hwnd)) {
        scrollOffset = (barCount + 1) * (barHeight + barSpacing) - getWindowHeight(hwnd);
    }
    return scrollOffset;
}

// Window procedure to handle messages
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        int clientHeight;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            // Clear the background with a solid color
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            FillRect(hdc, &clientRect, (HBRUSH)(COLOR_WINDOW + 1)); // Use system window color

            DrawBarChart(hdc, clientRect.right - clientRect.left, clientRect.bottom - clientRect.top);
            EndPaint(hwnd, &ps);
        }
        break;

        case WM_SIZE: {
            scrollOffset = offsetMax(hwnd, scrollOffset);
            InvalidateRect(hwnd, NULL, TRUE); // Force redraw
        }
        break;

        case WM_VSCROLL: {
            // Handle scrollbar movement
            int scrollPos = GetScrollPos(hwnd, SB_VERT);
            switch (LOWORD(wParam)) {
                case SB_LINEUP:
                    scrollOffset -= barHeight + barSpacing; // Scroll up
                    break;
                case SB_LINEDOWN:
                    scrollOffset += barHeight + barSpacing; // Scroll down
                    break;
                case SB_THUMBPOSITION:
                case SB_THUMBTRACK:
                    scrollOffset = HIWORD(wParam);
                    break;
            }
            // Ensure scrollOffset is within bounds
            scrollOffset = max(scrollOffset, 0);
            scrollOffset = offsetMax(hwnd, scrollOffset);
            InvalidateRect(hwnd, NULL, TRUE); // Request a redraw
        }
        break;

        case WM_MOUSEWHEEL: {
            int delta = GET_WHEEL_DELTA_WPARAM(wParam);
            scrollOffset -= delta / WHEEL_DELTA * (barHeight + barSpacing); // Scroll based on mouse wheel
            scrollOffset = offsetMax(hwnd, scrollOffset);
            InvalidateRect(hwnd, NULL, TRUE); // Force redraw
        }
        break;

        case WM_ERASEBKGND:
            return 1; // Prevent flickering

        case WM_DESTROY:
            if(unfilled_brush) DeleteObject(unfilled_brush);
            for(int i = 0; i < barCount; i++) {
                if(bar_brushes[i]) DeleteObject(bar_brushes[i]);
            }
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}
