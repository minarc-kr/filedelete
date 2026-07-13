/*
 * 내PC클리너 (안전하게 파일 지우기) - Windows 네이티브 GUI
 * ------------------------------------------------------------------
 * 고른 파일을 무작위 데이터로 7회 덮어쓴 뒤 삭제하여 일반 복구
 * 프로그램으로는 되살릴 수 없게 만듭니다. 파이썬 등 어떤 설치도
 * 필요 없이 단독 실행되는 .exe 입니다.
 *
 * 크로스 컴파일:
 *   x86_64-w64-mingw32-gcc secure_delete_win.c -o mypccleaner.exe \
 *     -mwindows -O2 -municode \
 *     -lcomdlg32 -lshell32 -lole32 -lbcrypt -lgdi32 -lcomctl32
 */

#define UNICODE
#define _UNICODE
#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <shlobj.h>
#include <bcrypt.h>
#include <stdlib.h>
#include <string.h>

#define PASSES 7   /* 덮어쓰기 횟수 */

/* 컨트롤 ID */
#define IDC_LIST    1001
#define IDC_PICK    1002
#define IDC_FOLDER  1003
#define IDC_CLEAR   1004
#define IDC_DELETE  1005
#define IDC_STATUS  1006
#define IDC_TITLE   1007
#define IDC_SUB     1008
#define IDC_WARN    1009

/* ---- 전역: 고른 파일 목록 ---- */
static wchar_t **g_files = NULL;
static int g_count = 0;
static int g_cap = 0;

/* ---- 전역: '폴더 통째로'로 고른 최상위 폴더 목록 ---- */
/* 파일을 모두 삭제한 뒤, 남은 빈 폴더(및 이 폴더 자체)를 제거하는 데 사용 */
static wchar_t **g_folders = NULL;
static int g_fcount = 0;
static int g_fcap = 0;

static HWND g_list = NULL;
static HWND g_status = NULL;
static HFONT g_font = NULL;
static HFONT g_titleFont = NULL;

/* 파일명(경로에서 마지막 \ 뒤 부분) 반환 포인터 */
static const wchar_t *base_name(const wchar_t *path) {
    const wchar_t *p = path;
    const wchar_t *last = path;
    for (; *p; p++) {
        if (*p == L'\\' || *p == L'/') last = p + 1;
    }
    return last;
}

/* 목록에 파일 추가 (중복 제외) */
static void add_file(const wchar_t *path) {
    int i;
    for (i = 0; i < g_count; i++) {
        if (lstrcmpiW(g_files[i], path) == 0) return; /* 이미 있음 */
    }
    if (g_count >= g_cap) {
        int ncap = g_cap ? g_cap * 2 : 32;
        wchar_t **nb = (wchar_t **)realloc(g_files, sizeof(wchar_t *) * ncap);
        if (!nb) return;
        g_files = nb;
        g_cap = ncap;
    }
    size_t len = wcslen(path) + 1;
    wchar_t *copy = (wchar_t *)malloc(sizeof(wchar_t) * len);
    if (!copy) return;
    memcpy(copy, path, sizeof(wchar_t) * len);
    g_files[g_count++] = copy;

    SendMessageW(g_list, LB_ADDSTRING, 0, (LPARAM)base_name(path));
}

/* '폴더 통째로'로 고른 폴더 경로 기록 (중복 제외) */
static void add_folder_path(const wchar_t *folder) {
    int i;
    for (i = 0; i < g_fcount; i++)
        if (lstrcmpiW(g_folders[i], folder) == 0) return;
    if (g_fcount >= g_fcap) {
        int ncap = g_fcap ? g_fcap * 2 : 8;
        wchar_t **nb = (wchar_t **)realloc(g_folders, sizeof(wchar_t *) * ncap);
        if (!nb) return;
        g_folders = nb;
        g_fcap = ncap;
    }
    size_t len = wcslen(folder) + 1;
    wchar_t *copy = (wchar_t *)malloc(sizeof(wchar_t) * len);
    if (!copy) return;
    memcpy(copy, folder, sizeof(wchar_t) * len);
    g_folders[g_fcount++] = copy;
}

static void clear_files(void) {
    int i;
    for (i = 0; i < g_count; i++) free(g_files[i]);
    g_count = 0;
    for (i = 0; i < g_fcount; i++) free(g_folders[i]);
    g_fcount = 0;
    SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
}

static void update_count_status(void) {
    wchar_t buf[128];
    wsprintfW(buf, L"고른 파일: %d개", g_count);
    SetWindowTextW(g_status, buf);
}

/* 폴더 안의 모든 파일을 재귀적으로 추가 */
static void add_folder_recursive(const wchar_t *folder) {
    wchar_t pattern[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wsprintfW(pattern, L"%s\\*", folder);
    h = FindFirstFileW(pattern, &fd);
    if (h == INVALID_HANDLE_VALUE) return;
    do {
        if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
            continue;
        wchar_t full[MAX_PATH * 2];
        wsprintfW(full, L"%s\\%s", folder, fd.cFileName);
        if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
            /* 재분석 지점(심볼릭 링크 등)은 건너뜀 */
            if (!(fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT))
                add_folder_recursive(full);
        } else {
            add_file(full);
        }
    } while (FindNextFileW(h, &fd));
    FindClose(h);
}

/* ---- 핵심: 파일 한 개 완전삭제 ---- */
/* 성공 0, 실패 시 0이 아닌 값 반환 */
static int secure_delete_one(const wchar_t *path) {
    /* 읽기전용/숨김 속성 해제 */
    SetFileAttributesW(path, FILE_ATTRIBUTE_NORMAL);

    HANDLE h = CreateFileW(path, GENERIC_WRITE, 0, NULL,
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH,
                           NULL);
    if (h == INVALID_HANDLE_VALUE) {
        return 1;
    }

    LARGE_INTEGER size;
    if (!GetFileSizeEx(h, &size)) {
        CloseHandle(h);
        return 2;
    }

    const DWORD CHUNK = 1024 * 1024; /* 1MB */
    BYTE *buf = (BYTE *)malloc(CHUNK);
    if (!buf) { CloseHandle(h); return 3; }

    int pass;
    for (pass = 0; pass < PASSES; pass++) {
        LARGE_INTEGER zero;
        zero.QuadPart = 0;
        SetFilePointerEx(h, zero, NULL, FILE_BEGIN);

        long long remaining = size.QuadPart;
        while (remaining > 0) {
            DWORD n = (DWORD)((remaining < (long long)CHUNK) ? remaining : CHUNK);
            /* 매 조각마다 새 난수 채우기 */
            if (BCryptGenRandom(NULL, buf, n,
                    BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
                /* 실패 시 최소한의 대체 난수 */
                DWORD i;
                for (i = 0; i < n; i++) buf[i] = (BYTE)(rand() & 0xFF);
            }
            DWORD written = 0;
            if (!WriteFile(h, buf, n, &written, NULL) || written != n) {
                free(buf);
                CloseHandle(h);
                return 4;
            }
            remaining -= n;
        }
        FlushFileBuffers(h);
    }
    free(buf);

    /* 파일 크기 정보 제거: 길이 0으로 */
    LARGE_INTEGER zero;
    zero.QuadPart = 0;
    SetFilePointerEx(h, zero, NULL, FILE_BEGIN);
    SetEndOfFile(h);
    FlushFileBuffers(h);
    CloseHandle(h);

    /* 파일 이름 흔적 줄이기: 무작위 이름으로 여러 번 변경 */
    wchar_t dir[MAX_PATH * 2];
    lstrcpynW(dir, path, MAX_PATH * 2);
    {
        wchar_t *p = dir + wcslen(dir);
        while (p > dir && *p != L'\\' && *p != L'/') p--;
        *p = 0; /* 디렉터리만 남김 */
    }

    wchar_t current[MAX_PATH * 2];
    lstrcpynW(current, path, MAX_PATH * 2);
    int r;
    for (r = 0; r < 3; r++) {
        BYTE rnd[12];
        wchar_t rndname[64];
        wchar_t newpath[MAX_PATH * 2];
        if (BCryptGenRandom(NULL, rnd, sizeof(rnd),
                BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0) {
            int i; for (i = 0; i < 12; i++) rnd[i] = (BYTE)(rand() & 0xFF);
        }
        int i;
        for (i = 0; i < 12; i++)
            wsprintfW(rndname + i * 2, L"%02x", rnd[i]);
        wsprintfW(newpath, L"%s\\%s", dir, rndname);
        if (MoveFileW(current, newpath)) {
            lstrcpynW(current, newpath, MAX_PATH * 2);
        } else {
            break;
        }
    }

    if (!DeleteFileW(current)) return 5;
    return 0;
}

/* GetOpenFileName 결과(다중 선택 포함) 처리 */
static void handle_pick_files(HWND hwnd) {
    /* 넉넉한 버퍼 (여러 파일 대비) */
    const DWORD BUFSZ = 1 << 16; /* 65536 wchar */
    wchar_t *buf = (wchar_t *)calloc(BUFSZ, sizeof(wchar_t));
    if (!buf) return;

    OPENFILENAMEW ofn;
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = hwnd;
    ofn.lpstrFilter = L"모든 파일\0*.*\0";
    ofn.lpstrFile = buf;
    ofn.nMaxFile = BUFSZ;
    ofn.lpstrTitle = L"지울 파일 고르기 (Ctrl/Shift로 여러 개 선택 가능)";
    ofn.Flags = OFN_EXPLORER | OFN_ALLOWMULTISELECT |
                OFN_FILEMUSTEXIST | OFN_HIDEREADONLY | OFN_NOCHANGEDIR;

    if (GetOpenFileNameW(&ofn)) {
        /* 결과 파싱: 첫 문자열 뒤에 또 문자열이 있으면 다중 선택 모드
           (첫 문자열 = 디렉터리, 이후 = 각 파일명) */
        wchar_t *dir = buf;
        wchar_t *p = dir + wcslen(dir) + 1;
        if (*p == 0) {
            /* 한 개만 선택: dir 자체가 전체 경로 */
            add_file(dir);
        } else {
            while (*p) {
                wchar_t full[MAX_PATH * 2];
                wsprintfW(full, L"%s\\%s", dir, p);
                add_file(full);
                p += wcslen(p) + 1;
            }
        }
        update_count_status();
    }
    free(buf);
}

/* 폴더 선택 */
static void handle_pick_folder(HWND hwnd) {
    BROWSEINFOW bi;
    ZeroMemory(&bi, sizeof(bi));
    bi.hwndOwner = hwnd;
    bi.lpszTitle = L"폴더 고르기 (안의 모든 파일이 삭제됩니다)";
    bi.ulFlags = BIF_RETURNONLYFSDIRS | BIF_NEWDIALOGSTYLE;
    LPITEMIDLIST pidl = SHBrowseForFolderW(&bi);
    if (pidl) {
        wchar_t folder[MAX_PATH];
        if (SHGetPathFromIDListW(pidl, folder)) {
            add_folder_recursive(folder);
            add_folder_path(folder); /* 삭제 후 폴더 자체도 제거하도록 기록 */
            update_count_status();
        }
        CoTaskMemFree(pidl);
    }
}

/* 폴더 트리 제거: 파일을 모두 지운 뒤 남은 (빈) 하위 폴더와
   폴더 자체를 아래에서 위로 제거한다. 제거한 폴더 수를 누적한다. */
static void remove_dir_tree(const wchar_t *folder, int *removedCount) {
    wchar_t pattern[MAX_PATH * 2];
    WIN32_FIND_DATAW fd;
    HANDLE h;
    wsprintfW(pattern, L"%s\\*", folder);
    h = FindFirstFileW(pattern, &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do {
            if (wcscmp(fd.cFileName, L".") == 0 || wcscmp(fd.cFileName, L"..") == 0)
                continue;
            wchar_t full[MAX_PATH * 2];
            wsprintfW(full, L"%s\\%s", folder, fd.cFileName);
            if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (fd.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) {
                    /* 정션/심볼릭 링크: 대상 내용을 따라가지 않고 링크만 제거 */
                    RemoveDirectoryW(full);
                } else {
                    remove_dir_tree(full, removedCount);
                }
            } else {
                /* 혹시 남아있는 파일(안전삭제 실패분 등)은 일반 삭제로 정리 */
                SetFileAttributesW(full, FILE_ATTRIBUTE_NORMAL);
                DeleteFileW(full);
            }
        } while (FindNextFileW(h, &fd));
        FindClose(h);
    }
    SetFileAttributesW(folder, FILE_ATTRIBUTE_NORMAL);
    if (RemoveDirectoryW(folder)) {
        if (removedCount) (*removedCount)++;
    }
}

/* 삭제 실행 */
static void handle_delete(HWND hwnd) {
    if (g_count == 0 && g_fcount == 0) {
        MessageBoxW(hwnd, L"먼저 지울 파일이나 폴더를 골라 주세요. 😊",
                    L"잠깐!", MB_OK | MB_ICONINFORMATION);
        return;
    }
    wchar_t msg[400];
    if (g_fcount > 0) {
        wsprintfW(msg,
            L"고른 파일 %d개를 완전히 지우고,\n"
            L"선택한 폴더 %d개(하위 폴더 포함)도 함께 제거합니다.\n\n"
            L"한 번 지우면 다시 살릴 수 없어요.\n정말 지울까요?",
            g_count, g_fcount);
    } else {
        wsprintfW(msg,
            L"고른 파일 %d개를 완전히 지웁니다.\n\n"
            L"한 번 지우면 다시 살릴 수 없어요.\n정말 지울까요?",
            g_count);
    }
    if (MessageBoxW(hwnd, msg, L"정말 지울까요?",
                    MB_YESNO | MB_ICONWARNING | MB_DEFBUTTON2) != IDYES) {
        return;
    }

    int ok = 0, fail = 0;
    int total = g_count;
    /* 뒤에서부터 처리하면서 성공 시 목록에서 제거 */
    int idx;
    int done = 0;
    for (idx = 0; idx < total; idx++) {
        wchar_t st[256];
        wsprintfW(st, L"지우는 중...  (%d/%d)", done + 1, total);
        SetWindowTextW(g_status, st);
        UpdateWindow(g_status);

        int r = secure_delete_one(g_files[idx]);
        if (r == 0) ok++;
        else { fail++; }
        done++;
    }

    /* 목록 정리(성공/실패 상관없이 사라진 파일 제거를 위해 재검사) */
    {
        int i, w = 0;
        SendMessageW(g_list, LB_RESETCONTENT, 0, 0);
        for (i = 0; i < g_count; i++) {
            DWORD attr = GetFileAttributesW(g_files[i]);
            if (attr == INVALID_FILE_ATTRIBUTES) {
                free(g_files[i]); /* 실제로 지워짐 */
            } else {
                g_files[w++] = g_files[i];
                SendMessageW(g_list, LB_ADDSTRING, 0,
                             (LPARAM)base_name(g_files[i]));
            }
        }
        g_count = w;
    }

    /* '폴더 통째로'로 고른 폴더들: 파일 삭제 후 남은 빈 폴더와
       폴더 자체를 제거한다. */
    int folders_removed = 0;
    {
        int i;
        if (g_fcount > 0) {
            SetWindowTextW(g_status, L"폴더 정리 중...");
            UpdateWindow(g_status);
        }
        for (i = 0; i < g_fcount; i++) {
            remove_dir_tree(g_folders[i], &folders_removed);
            free(g_folders[i]);
        }
        g_fcount = 0;
    }

    update_count_status();

    wchar_t res[320];
    if (fail == 0) {
        if (folders_removed > 0)
            wsprintfW(res, L"파일 %d개와 폴더 %d개를 안전하게 지웠어요! 🎉",
                      ok, folders_removed);
        else
            wsprintfW(res, L"파일 %d개를 안전하게 지웠어요! 🎉", ok);
        MessageBoxW(hwnd, res, L"완료!", MB_OK | MB_ICONINFORMATION);
    } else {
        wsprintfW(res, L"지운 파일: %d개\n못 지운 파일: %d개\n"
                       L"제거한 폴더: %d개\n\n"
                       L"(다른 프로그램에서 사용 중이거나 권한이 없을 수 있어요)",
                  ok, fail, folders_removed);
        MessageBoxW(hwnd, res, L"일부 실패", MB_OK | MB_ICONWARNING);
    }
}

static void set_font(HWND h, HFONT f) {
    SendMessageW(h, WM_SETFONT, (WPARAM)f, TRUE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        /* 폰트 (한글: 맑은 고딕) */
        g_font = CreateFontW(-16, 0, 0, 0, FW_NORMAL, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");
        g_titleFont = CreateFontW(-28, 0, 0, 0, FW_BOLD, 0, 0, 0,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
            CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Malgun Gothic");

        HWND title = CreateWindowW(L"STATIC",
            L"🧹 내PC클리너",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 16, 664, 44, hwnd, (HMENU)IDC_TITLE, NULL, NULL);
        set_font(title, g_titleFont);

        HWND sub = CreateWindowW(L"STATIC",
            L"안전하게 파일 지우기 · ① 파일을 고르고  ②  지우기 버튼을 누르세요. 한 번 지우면 되살릴 수 없어요.",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 62, 664, 24, hwnd, (HMENU)IDC_SUB, NULL, NULL);
        set_font(sub, g_font);

        HWND bPick = CreateWindowW(L"BUTTON", L"📁 파일 고르기",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 96, 200, 44, hwnd, (HMENU)IDC_PICK, NULL, NULL);
        set_font(bPick, g_font);

        HWND bFolder = CreateWindowW(L"BUTTON", L"🗂 폴더 통째로",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            240, 96, 200, 44, hwnd, (HMENU)IDC_FOLDER, NULL, NULL);
        set_font(bFolder, g_font);

        HWND bClear = CreateWindowW(L"BUTTON", L"🧽 목록 비우기",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            470, 96, 200, 44, hwnd, (HMENU)IDC_CLEAR, NULL, NULL);
        set_font(bClear, g_font);

        g_list = CreateWindowW(L"LISTBOX", L"",
            WS_CHILD | WS_VISIBLE | WS_BORDER | WS_VSCROLL |
            LBS_NOSEL | LBS_NOINTEGRALHEIGHT,
            30, 152, 640, 250, hwnd, (HMENU)IDC_LIST, NULL, NULL);
        set_font(g_list, g_font);

        g_status = CreateWindowW(L"STATIC", L"고른 파일: 0개",
            WS_CHILD | WS_VISIBLE | SS_LEFT,
            30, 410, 640, 24, hwnd, (HMENU)IDC_STATUS, NULL, NULL);
        set_font(g_status, g_font);

        HWND bDel = CreateWindowW(L"BUTTON", L"🗑  지우기",
            WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
            30, 440, 640, 56, hwnd, (HMENU)IDC_DELETE, NULL, NULL);
        set_font(bDel, g_titleFont);

        HWND warn = CreateWindowW(L"STATIC",
            L"※ USB·SD카드·SSD는 기술적 특성상 완전삭제가 100% 보장되지 않을 수 있어요.",
            WS_CHILD | WS_VISIBLE | SS_CENTER,
            20, 506, 664, 22, hwnd, (HMENU)IDC_WARN, NULL, NULL);
        set_font(warn, g_font);
        return 0;
    }
    case WM_SIZE: {
        int cw = LOWORD(lp), ch = HIWORD(lp);
        if (cw <= 0 || ch <= 0) return 0;
        int bw = (cw - 60 - 20) / 3;              /* 버튼 3개: 좌우 30, 간격 10 */
        int warnY = ch - 35;
        int delY  = ch - 101;
        int stY   = delY - 30;
        MoveWindow(GetDlgItem(hwnd, IDC_TITLE),  20, 16,  cw - 40, 44, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_SUB),    20, 62,  cw - 40, 24, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_PICK),   30, 96,  bw, 44, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_FOLDER), 30 + bw + 10, 96, bw, 44, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_CLEAR),  30 + 2*(bw + 10), 96, bw, 44, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_LIST),   30, 152, cw - 60, stY - 8 - 152, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_STATUS), 30, stY, cw - 60, 24, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_DELETE), 30, delY, cw - 60, 56, TRUE);
        MoveWindow(GetDlgItem(hwnd, IDC_WARN),   20, warnY, cw - 40, 22, TRUE);
        InvalidateRect(hwnd, NULL, TRUE);
        return 0;
    }
    case WM_GETMINMAXINFO: {
        MINMAXINFO* mmi = (MINMAXINFO*)lp;
        mmi->ptMinTrackSize.x = 560;
        mmi->ptMinTrackSize.y = 480;
        return 0;
    }
    case WM_COMMAND: {
        switch (LOWORD(wp)) {
        case IDC_PICK:   handle_pick_files(hwnd);  return 0;
        case IDC_FOLDER: handle_pick_folder(hwnd); return 0;
        case IDC_CLEAR:  clear_files(); update_count_status(); return 0;
        case IDC_DELETE: handle_delete(hwnd); return 0;
        }
        return 0;
    }
    case WM_DESTROY:
        clear_files();
        if (g_font) DeleteObject(g_font);
        if (g_titleFont) DeleteObject(g_titleFont);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE hPrev,
                    PWSTR pCmdLine, int nCmdShow) {
    (void)hPrev; (void)pCmdLine;
    InitCommonControls();
    CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);

    WNDCLASSW wc;
    ZeroMemory(&wc, sizeof(wc));
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = L"SecureEraserWnd";
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(101));
    if (!wc.hIcon) wc.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(L"SecureEraserWnd",
        L"내PC클리너 v1.1.0",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 720, 580,
        NULL, NULL, hInst, NULL);
    if (!hwnd) return 1;

    ShowWindow(hwnd, nCmdShow);
    UpdateWindow(hwnd);

    MSG m;
    while (GetMessageW(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageW(&m);
    }
    CoUninitialize();
    return 0;
}
