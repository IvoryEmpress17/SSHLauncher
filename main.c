/* ==========================================================================
 * SSH Terminal Launcher - main.c  (ANSI / Multi-Byte, C11)
 *
 * 编译命令 (MinGW GCC 4.9.2, C11):
 *   gcc -std=c11 -O2 -mwindows main.c -o SSHLauncher.exe \
 *       -luser32 -lgdi32 -lshell32 -lcomctl32 -lcomdlg32
 *   (若用到网络相关可再加 -lws2_32)
 *
 * 说明：
 *   - 全部使用 Windows ANSI (A 后缀) API：MessageBoxA / SetWindowTextA /
 *     CreateProcessA / ShellExecuteA / SendMessageA ...
 *   - 本文件交付编码为 UTF-8；若你的 MinGW 环境按 GBK/ANSI 解析源码字符串，
 *     请把文件另存为 ANSI(GBK) 后再编译（中文注释与界面文案不受影响）。
 * ========================================================================== */

#ifndef WINVER
#define WINVER 0x0600
#endif
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0600
#endif

#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <ctype.h>
#include <stdbool.h>

#include "resource.h"

#ifdef _MSC_VER
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#endif

/* ==========================================================================
 * 一、通用工具：动态字符串 Str
 *     （C 版替代 std::string）
 * ========================================================================== */

typedef struct {
    char*  data;
    size_t len;
    size_t cap;
} Str;

static int str_reserve(Str* s, size_t need) {
    size_t newcap;
    char* p;
    if (s->cap > need) return 1;
    newcap = s->cap ? s->cap : 32;
    while (newcap <= need) newcap *= 2;
    p = (char*)realloc(s->data, newcap);
    if (!p) return 0;
    s->data = p;
    s->cap  = newcap;
    return 1;
}

void str_init(Str* s) {
    s->data = NULL;
    s->len  = 0;
    s->cap  = 0;
}

void str_clear(Str* s) {
    if (s->data) s->data[0] = '\0';
    s->len = 0;
}

void str_free(Str* s) {
    free(s->data);
    s->data = NULL;
    s->len  = 0;
    s->cap  = 0;
}

/* 取 C 字符串，永不返回 NULL */
static const char* StrCStr(const Str* s) {
    return s->data ? s->data : "";
}

int str_set(Str* s, const char* src) {
    size_t n;
    if (!src) src = "";
    n = strlen(src);
    if (!str_reserve(s, n)) return 0;
    memcpy(s->data, src, n + 1);
    s->len = n;
    return 1;
}

int str_set_n(Str* s, const char* src, size_t n) {
    if (!str_reserve(s, n)) return 0;
    if (n) memcpy(s->data, src, n);
    s->data[n] = '\0';
    s->len = n;
    return 1;
}

int str_append(Str* s, const char* src) {
    size_t n;
    if (!src) return 1;
    n = strlen(src);
    if (!str_reserve(s, s->len + n)) return 0;
    memcpy(s->data + s->len, src, n + 1);
    s->len += n;
    return 1;
}

int str_appendf(Str* s, const char* fmt, ...) {
    va_list ap;
    char buf[1024];
    char* big;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;

    if ((size_t)n < sizeof(buf)) return str_append(s, buf);

    big = (char*)malloc((size_t)n + 1);
    if (!big) return 0;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    str_append(s, big);
    free(big);
    return 1;
}

int str_setf(Str* s, const char* fmt, ...) {
    va_list ap;
    char buf[1024];
    char* big;
    int n;

    va_start(ap, fmt);
    n = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;

    if ((size_t)n < sizeof(buf)) return str_set(s, buf);

    big = (char*)malloc((size_t)n + 1);
    if (!big) return 0;
    va_start(ap, fmt);
    vsnprintf(big, (size_t)n + 1, fmt, ap);
    va_end(ap);
    str_set(s, big);
    free(big);
    return 1;
}

/* 去掉首尾空白（原地修改） */
void str_trim(Str* s) {
    size_t start = 0, end, i;
    if (!s->data || s->len == 0) return;

    while (start < s->len) {
        char c = s->data[start];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        start++;
    }
    end = s->len;
    while (end > start) {
        char c = s->data[end - 1];
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n') break;
        end--;
    }
    if (start > 0) {
        for (i = start; i < end; i++) s->data[i - start] = s->data[i];
    }
    s->data[end - start] = '\0';
    s->len = end - start;
}

/* 去掉尾部 \r \n（原地修改） */
void str_chomp(Str* s) {
    if (!s->data) return;
    while (s->len > 0 && (s->data[s->len - 1] == '\n' || s->data[s->len - 1] == '\r')) {
        s->len--;
        s->data[s->len] = '\0';
    }
}

static char* MyStrDup(const char* s) {
    size_t n;
    char* p;
    if (!s) s = "";
    n = strlen(s);
    p = (char*)malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n + 1);
    return p;
}

/* 不区分大小写比较（纯 ASCII 部分） */
static bool StrEqCI(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    if (strlen(a) != strlen(b)) return false;
    for (; *a; a++, b++) {
        if (tolower((unsigned char)*a) != tolower((unsigned char)*b)) return false;
    }
    return true;
}

/* 不区分大小写的子串查找（替代 transform + find） */
static bool StrContainsCI(const char* hay, const char* needle) {
    size_t i, n1, n2;
    if (!hay || !needle) return false;
    n1 = strlen(hay);
    n2 = strlen(needle);
    if (n2 == 0) return true;
    if (n2 > n1) return false;
    for (i = 0; i + n2 <= n1; i++) {
        size_t j;
        bool ok = true;
        for (j = 0; j < n2; j++) {
            if (tolower((unsigned char)hay[i + j]) != tolower((unsigned char)needle[j])) {
                ok = false;
                break;
            }
        }
        if (ok) return true;
    }
    return false;
}

/* ==========================================================================
 * 二、通用工具：字符串列表 StrList
 *     （C 版替代 std::vector<std::string>）
 * ========================================================================== */

typedef struct {
    char** items;
    size_t count;
    size_t cap;
} StrList;

void sl_init(StrList* l) {
    l->items = NULL;
    l->count = 0;
    l->cap   = 0;
}

void sl_free(StrList* l) {
    size_t i;
    for (i = 0; i < l->count; i++) free(l->items[i]);
    free(l->items);
    l->items = NULL;
    l->count = 0;
    l->cap   = 0;
}

int sl_push(StrList* l, const char* s) {
    char* dup;
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 8;
        char** p = (char**)realloc(l->items, nc * sizeof(char*));
        if (!p) return 0;
        l->items = p;
        l->cap   = nc;
    }
    dup = MyStrDup(s);
    if (!dup) return 0;
    l->items[l->count++] = dup;
    return 1;
}

static int sl_cmp(const void* a, const void* b) {
    return strcmp(*(const char* const*)a, *(const char* const*)b);
}

void sl_sort(StrList* l) {
    if (l->count > 1) qsort(l->items, l->count, sizeof(char*), sl_cmp);
}

/* 去除相邻重复项（需先 sl_sort） */
void sl_unique(StrList* l) {
    size_t i, w = 0;
    for (i = 0; i < l->count; i++) {
        /* 与"已保留的最后一项"比较，不能与 l->items[i-1] 比较（它可能已被释放） */
        if (w > 0 && strcmp(l->items[i], l->items[w - 1]) == 0) {
            free(l->items[i]);
            continue;
        }
        l->items[w++] = l->items[i];
    }
    l->count = w;
}

/* 大小写不敏感包含判断 */
bool sl_contains_ci(const StrList* l, const char* s) {
    size_t i;
    for (i = 0; i < l->count; i++) {
        if (StrEqCI(l->items[i], s)) return true;
    }
    return false;
}

/* 大小写不敏感删除所有匹配项 */
void sl_remove_ci(StrList* l, const char* s) {
    size_t i, w = 0;
    for (i = 0; i < l->count; i++) {
        if (StrEqCI(l->items[i], s)) {
            free(l->items[i]);
            continue;
        }
        l->items[w++] = l->items[i];
    }
    l->count = w;
}

/* ==========================================================================
 * 三、结构体定义
 * ========================================================================== */

struct SSHConfig {
    Str  host;
    Str  port;
    Str  username;
    Str  timeout;
    Str  extraArgs;
    Str  keyFile;
    int  authType;      /* 0=password, 1=key */
    bool compress;
    bool x11Forward;
    bool verbose;
    bool remember;
    Str  encryption;
    Str  localFwd;
    Str  remoteFwd;
    Str  dynamicFwd;
    bool keepAlive;
    Str  proxyJump;
};
typedef struct SSHConfig SSHConfig;

void sshcfg_init(SSHConfig* c) {
    str_init(&c->host);
    str_init(&c->port);
    str_init(&c->username);
    str_init(&c->timeout);
    str_init(&c->extraArgs);
    str_init(&c->keyFile);
    str_init(&c->encryption);
    str_init(&c->localFwd);
    str_init(&c->remoteFwd);
    str_init(&c->dynamicFwd);
    str_init(&c->proxyJump);
    c->authType   = 0;
    c->compress   = false;
    c->x11Forward = false;
    c->verbose    = false;
    c->remember   = false;
    c->keepAlive  = false;
}

void sshcfg_free(SSHConfig* c) {
    str_free(&c->host);
    str_free(&c->port);
    str_free(&c->username);
    str_free(&c->timeout);
    str_free(&c->extraArgs);
    str_free(&c->keyFile);
    str_free(&c->encryption);
    str_free(&c->localFwd);
    str_free(&c->remoteFwd);
    str_free(&c->dynamicFwd);
    str_free(&c->proxyJump);
}

struct CtrlHandles {
    HWND hEditHost;
    HWND hEditPort;
    HWND hEditUser;
    HWND hEditTimeout;
    HWND hEditExtraArgs;
    HWND hEditKeyFile;
    HWND hRadioPassword;
    HWND hRadioKey;
    HWND hChkCompress;
    HWND hChkX11Forward;
    HWND hChkVerbose;
    HWND hChkRemember;
    HWND hComboEnc;
    HWND hStaticStatus;
    HWND hBtnConnect;
    HWND hEditLocalFwd;
    HWND hEditRemoteFwd;
    HWND hEditDynamicFwd;
    HWND hChkKeepAlive;
    HWND hEditProxyJump;
};
typedef struct CtrlHandles CtrlHandles;

/* ==========================================================================
 * 四、全局变量
 * ========================================================================== */

HINSTANCE g_hInstance = NULL;
HWND      g_hMainDlg  = NULL;
HBRUSH    g_hCustomBgBrush = NULL;

static HBITMAP* g_menuBitmaps     = NULL;   /* 替代 std::vector<HBITMAP> */
static size_t   g_menuBitmapCount = 0;
static size_t   g_menuBitmapCap   = 0;

Str  g_userSelectedSSHPath;                 /* 替代 std::string */

/* 下拉框每一项的「真实生效路径」（绝对路径），与 ComboBox 的项一一对应。
 *
 * 这是「显示相对路径、生效绝对路径」的落点：下拉框里显示的是
 * PathToDisplay() 算出来的短文本，而点确定时取的是这里存的绝对路径。
 * 二者各走一条路，显示怎么改都不影响实际连接用的路径。
 *
 * 为什么必须分开存：原来生效路径是靠解析显示文本得到的（"[自定义] "
 * 的 9 字节偏移、".\" 前缀、含"环境变量"子串），显示文本一改就会连带
 * 把生效逻辑改坏 —— 这正是「改成相对路径」这个需求会踩的坑。 */
static StrList g_sshComboReal;
HWND g_hTooltip = NULL;
bool g_tooltipInitialized = false;

CtrlHandles g_ctrl;

/* ==========================================================================
 * 五、函数原型
 * ========================================================================== */

Str   ExecHidden(const char* cmd);
void  LogStatus(const char* msg);
void  ShowInfo(const char* msg, const char* title);
void  ShowError(const char* msg, const char* title);

char** GetCipherArray(size_t* outCount);
void   FreeCipherArray(char** arr, size_t count);

void  InitControls(HWND hDlg);
void  ShowBalloon(HWND hCtrl, const char* title, const char* text, DWORD icon);
Str   GetEditText(HWND hEdit);
Str   GetWindowTextTrim(HWND hCtrl);
void  ReadConfigFromUI(HWND hDlg, SSHConfig* cfg);
void  WriteConfigToUI(HWND hDlg, const SSHConfig* cfg);
bool  ValidateConfig(const SSHConfig* cfg, Str* errMsg);

Str   BuildSSHCommand(const SSHConfig* cfg);
bool  IsSSHAvailable(void);
bool  IsFileExist(const char* path);
const char* GetSSHPath(void);
Str   GetKeygenPath(void);
bool  CopyTextToClipboard(const char* text);
Str   GetSshDir(void);
Str   GetAppDirectory(void);
void  CollectAllSSHCandidates(StrList* out);
void  GetSupportedCiphers(StrList* out);
Str   GetSSHVersion(const char* sshPath);
bool  LaunchSSHViaPowerShell(const char* sshCmd);
void  RefreshAfterSSHChange(HWND hDlg);

void  OnConnect(HWND hDlg);
void  OnBrowseKeyFile(void);
void  OnOpenSshDir(HWND hDlg);
void  OnGenerateKeyPair(HWND hDlg);
void  OnCopyPublicKey(HWND hDlg);
void  OnSSHConnectionTest(HWND hDlg);
void  OnAddHostKey(HWND hDlg);
void  ExportConfigToBatch(HWND hDlg);

Str   GetUserSSHIniPath(void);
void  LoadCustomSSHPaths(StrList* out);
bool  IsCustomRealPath(const char* realPath);
const char* ComboRealPath(HWND hCombo, int idx);   /* 显示文本 → 真实绝对路径 */
int   ComboFindRealPath(HWND hCombo, const char* realPath);
bool  SaveCustomSSHPaths(const StrList* paths);
bool  IsCustomRealPath(const char* realPath);
void  OnAddCustomSSH(HWND hDlg);
void  OnDeleteCustomSSH(HWND hDlg);

Str   GetSSHPathFilePath(void);
void  SaveSSHPathConfig(const char* sshPath, bool remember);
bool  LoadSSHPathConfig(Str* outPath);

Str   GetConfigFilePath(void);
void  IniGetString(const char* file, const char* section, const char* key, const char* defVal, Str* out);
int   IniGetInt(const char* file, const char* section, const char* key, int defVal);
void  IniWriteString(const char* file, const char* section, const char* key, const char* val);
void  IniWriteInt(const char* file, const char* section, const char* key, int val);
void  SaveConfigToIni(HWND hDlg);
void  LoadConfigFromIni(HWND hDlg);

void  PopulateSSHCombo(HWND hCombo, HWND hOkBtn);
INT_PTR CALLBACK SelectSSHDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);
HBITMAP HIconToMenuBitmap(HICON hIcon, int size);
INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam);

/* 小工具：读取 ComboBox 指定项文本 */
void ComboGetLBText(HWND hCombo, int index, Str* out);
/* 小工具：一次性读取整个文件 */
char* ReadFileAll(const char* path, size_t* outLen);

/* 高 DPI 适配（做法与 easygl.h 的 InputBox 一致） */
int   getdpi(void);         /* 系统 DPI：96 / 120 / 144 / 192 */
int   gxS(int v);           /* 96 dpi 常数 → 当前 DPI 像素 */
HFONT gxUiFont(void);       /* 系统消息字体，随 DPI 重建 */
void  gxDpiRestyle(HWND hDlg);   /* 换字体 + 按对话框单位重算几何 */

/* ==========================================================================
 * 六、工具实现
 * ========================================================================== */

/* ==========================================================================
 * 六b、高 DPI 适配
 *
 * 做法完全照抄 easygl.h 里 InputBox 那一套，因为两个程序面对的是同一个
 * 问题：窗口和控件要跟着系统 DPI 长个儿，而不是在 150% 屏幕上缩成一团
 * 或者被 Windows 位图拉伸糊掉。
 *
 *   getdpi()      - 库里的 getdpi()，GetDeviceCaps(LOGPIXELSY)，一个进程
 *                   里只有这一处读 DPI，要换更细的源只改这里。
 *   gxS(v)        - 库里的 gxIbS()，MulDiv(v, dpi, 96)。所有尺寸写成
 *                   96 dpi 的常数再过一遍它，150% 下 20 就是 30。
 *   gxUiFont()    - 库里的 gxUiFont()，NONCLIENTMETRICS.lfMessageFont
 *                   （MessageBox 和所有公共对话框用的那个），按 DPI 缓存
 *                   重建；拿不到就退到 MS Shell Dlg，再退到 DEFAULT_GUI_FONT。
 *   gxDpiRestyle()- 把字体装到对话框和每个控件上，并按对话框单位把几何
 *                   重算一遍。库里是自己建窗口所以直接给像素；这里的窗口
 *                   来自资源模板，只能这样换算。
 *
 * 为什么要重算几何：对话框模板的坐标单位是 DLU（对话框单位，一个单位
 * 是字体平均字符宽的 1/4、字高的 1/8），不是像素。系统建窗口时按"当时
 * 那个字体"把 DLU 换成像素，所以模板字体一换，同样的 DLU 就该换成
 * 不同的像素。MapDialogRect() 给的就是当前的换算基数，所以取两次——
 * 换字体前一次、换字体后一次——就能把每个控件的位置还原成 DLU、再按新
 * 基数换回去。模板字体本来就是系统字体时两次基数相同，这一步是 1:1，
 * 不会把已经缩放好的窗口再放大一遍。
 * ========================================================================== */

/* LOGPIXELSY：wingdi.h 常量，值由 Windows 定死，瘦 SDK 可能没有。 */
#ifndef LOGPIXELSY
#define LOGPIXELSY   90
#endif
/* SPI_GETNONCLIENTMETRICS、WM_SETFONT 同理。 */
#ifndef SPI_GETNONCLIENTMETRICS
#define SPI_GETNONCLIENTMETRICS   0x0029
#endif
#ifndef WM_SETFONT
#define WM_SETFONT   0x0030
#endif
#ifndef FW_NORMAL
#define FW_NORMAL   400
#endif
#ifndef DEFAULT_CHARSET
#define DEFAULT_CHARSET   1
#endif
#ifndef OUT_DEFAULT_PRECIS
#define OUT_DEFAULT_PRECIS   0
#endif
#ifndef CLIP_DEFAULT_PRECIS
#define CLIP_DEFAULT_PRECIS   0
#endif
#ifndef DEFAULT_QUALITY
#define DEFAULT_QUALITY   0
#endif
#ifndef DEFAULT_PITCH
#define DEFAULT_PITCH   0
#endif
#ifndef FF_DONTCARE
#define FF_DONTCARE   (0 << 4)
#endif

/* 主显示器的垂直 DPI：100% 是 96，150% 是 144，200% 是 192。只读 Y，
 * 因为 Windows 两个方向用的是同一个倍率。
 *
 * 直接调 GetDeviceCaps() 而不是 GetDpiForWindow() / GetDpiForSystem()：
 * 后两个要 Win10 1607+，还得按指针加载，在这里换不来任何东西。 */
int getdpi(void) {
    HDC dc;
    int dpi = 96;

    dc = GetDC(NULL);
    if (dc) {
        int v = GetDeviceCaps(dc, LOGPIXELSY);
        if (v > 0) dpi = v;
        ReleaseDC(NULL, dc);
    }
    return dpi;
}

/* 96 dpi 的常数 → 当前 DPI 的像素。MulDiv() 会四舍五入，所以 20 在
 * 150% 下是 30、200% 下是 40，始终落在整像素上。 */
int gxS(int v) {
    return MulDiv(v, getdpi(), 96);
}

/* 一个对话框窗口当前的换算基数：1 个水平单位、1 个垂直单位各是多少像素。
 *
 * MapDialogRect() 的换算是 x像素 = x单位 * baseX / 4、y像素 =
 * y单位 * baseY / 8，所以给它一个 4x8 的矩形，回来的右边和底边正好就是
 * baseX 和 baseY。这样不用去猜 Windows 内部那个"平均字符宽 + 悬挂"的
 * 公式，也不用管它每个版本有没有微调。 */
static bool gxDpiBaseUnits(HWND hDlg, int* bx, int* by) {
    RECT u;

    if (!bx || !by) return false;
    *bx = 0;
    *by = 0;
    if (!hDlg) return false;

    u.left = 0; u.top = 0; u.right = 4; u.bottom = 8;
    if (!MapDialogRect(hDlg, &u)) return false;
    if (u.right <= 0 || u.bottom <= 0) return false;

    *bx = (int)u.right;
    *by = (int)u.bottom;
    return true;
}

/* 系统消息字体：Segoe UI 9（Vista+），也就是 MessageBox、所有公共对话框
 * 和所有属性页用的那个。
 *
 * GetStockObject(DEFAULT_GUI_FONT) 是 MS Sans Serif，一个从 Windows 3
 * 留下来的点阵字体，用它才是"这个对话框看着不对劲"的根源：系统里没有别
 * 的东西用它。
 *
 * 缓存按 DPI 失效：窗口被拖到另一块屏上 DPI 就变了，缓存的字体是按上次
 * 那个 DPI 做的，对不上就重建。删掉旧的那个是安全的——持有它的控件在上
 * 一次对话框关闭时就销毁了。
 *
 * 故意从不释放：整个进程一个 HFONT，退出时由 Windows 收回。 */
static HFONT g_gx_uiFont = NULL;
static int   g_gx_uiFontDpi = 0;

HFONT gxUiFont(void) {
    NONCLIENTMETRICSA ncm;
    int dpi = getdpi();

    if (g_gx_uiFont && g_gx_uiFontDpi == dpi) return g_gx_uiFont;
    if (g_gx_uiFont) { DeleteObject(g_gx_uiFont); g_gx_uiFont = NULL; }

    memset(&ncm, 0, sizeof(ncm));
    ncm.cbSize = (UINT)sizeof(ncm);
    /* SystemParametersInfo() 要的是"当前系统期望的大小"，不是 SDK 编译时
     * 那个；在老的 WINVER 上被拒绝是正常的，不是真出错，下面 MS Shell Dlg
     * 那条路就是为此准备的。 */
    if (SystemParametersInfoA(SPI_GETNONCLIENTMETRICS,
                              (UINT)sizeof(ncm), &ncm, 0)) {
        g_gx_uiFont = CreateFontIndirectA(&ncm.lfMessageFont);
    }
    if (!g_gx_uiFont) {
        /* MS Shell Dlg 是对话框模板里那个逻辑名，字体映射器会替它换成
         * Segoe UI（10 / 11）或者 Microsoft Sans Serif（2000 / XP）。
         * 字体本身已经按 DPI 缩放过，所以这里用 -gxS(12)。 */
        LOGFONTA lf;
        memset(&lf, 0, sizeof(lf));
        lf.lfHeight         = -gxS(12);      /* 96 dpi 下 9 pt */
        lf.lfWeight         = FW_NORMAL;
        lf.lfCharSet        = (BYTE)DEFAULT_CHARSET;
        lf.lfOutPrecision   = (BYTE)OUT_DEFAULT_PRECIS;
        lf.lfClipPrecision  = (BYTE)CLIP_DEFAULT_PRECIS;
        lf.lfQuality        = (BYTE)DEFAULT_QUALITY;
        lf.lfPitchAndFamily = (BYTE)(DEFAULT_PITCH | FF_DONTCARE);
        strcpy(lf.lfFaceName, "MS Shell Dlg");
        g_gx_uiFont = CreateFontIndirectA(&lf);
    }
    if (g_gx_uiFont) {
        g_gx_uiFontDpi = dpi;
        return g_gx_uiFont;
    }
    g_gx_uiFontDpi = 0;
    return (HFONT)GetStockObject(DEFAULT_GUI_FONT);   /* 兜底 */
}

/* 收集一个对话框的子控件：句柄 + 相对父窗口客户区的位置。 */
#define GX_DPI_MAXCTRL   64

typedef struct {
    HWND list[GX_DPI_MAXCTRL];
    RECT rc[GX_DPI_MAXCTRL];        /* 父窗口客户区坐标 */
    int  n;
} GxChildList;

static BOOL CALLBACK gxDpiCollectChild(HWND h, LPARAM lp) {
    GxChildList* cl = (GxChildList*)lp;
    RECT r;

    if (!cl || cl->n >= GX_DPI_MAXCTRL) return TRUE;
    if (!GetWindowRect(h, &r)) return TRUE;
    /* 屏幕坐标 → 父窗口客户区坐标，两个点（左上、右下）一起转。 */
    MapWindowPoints(NULL, GetParent(h), (LPPOINT)&r, 2);
    cl->list[cl->n] = h;
    cl->rc[cl->n]   = r;
    cl->n++;
    return TRUE;
}

/* 给对话框和它每一个子控件装上系统字体，并按对话框单位把几何重算一遍。
 *
 * 每个控件都得单独 SETFONT：只给对话框设，控件还会留在系统默认字体上，
 * 于是输入框和按钮是两种字，看着就是"这个对话框字体很怪"。
 *
 * 几何这一步是上面说的 DLU 换算。它乘的不是 dpi/96，而是新基数/旧基数，
 * 所以模板字体本来就是系统字体时它是 1:1——那一刻系统已经把窗口按 DPI
 * 缩放好了（字体跟着 DPI 变大，DLU 换出来的像素就变大），再乘一遍就错了。
 * 只有模板写死了别的字体（MS Sans Serif、Tahoma、某个字号）时才真正生效，
 * 而那正是需要修正的情况。
 *
 * 在 WM_INITDIALOG 里最早的地方调用：控件都建好了、还没显示。 */
void gxDpiRestyle(HWND hDlg) {
    GxChildList cl;
    RECT cr, wr;
    int obx, oby, nbx, nby;
    int i;
    HFONT f;
    DWORD style;
    BOOL  hasMenu;

    if (!hDlg) return;

    /* 换字体前的基数 */
    if (!gxDpiBaseUnits(hDlg, &obx, &oby)) return;

    cl.n = 0;
    EnumChildWindows(hDlg, gxDpiCollectChild, (LPARAM)&cl);
    GetClientRect(hDlg, &cr);
    if (cr.right <= 0 || cr.bottom <= 0) return;

    f = gxUiFont();
    SendMessageA(hDlg, WM_SETFONT, (WPARAM)f, MAKELONG(TRUE, 0));
    for (i = 0; i < cl.n; i++) {
        SendMessageA(cl.list[i], WM_SETFONT, (WPARAM)f, MAKELONG(TRUE, 0));
    }

    /* 换字体后的基数 */
    if (!gxDpiBaseUnits(hDlg, &nbx, &nby)) return;

    /* 窗口：客户区按比例换算，再用 AdjustWindowRect() 把边框和标题栏补回去。
     * 有菜单要告诉它，不然标题栏下面会多出一条菜单高的空隙。 */
    style = (DWORD)GetWindowLongA(hDlg, GWL_STYLE);
    hasMenu = (GetMenu(hDlg) != NULL);
    wr.left   = 0;
    wr.top    = 0;
    wr.right  = MulDiv((int)cr.right,  nbx, obx);
    wr.bottom = MulDiv((int)cr.bottom, nby, oby);
    AdjustWindowRect(&wr, style, hasMenu);
    SetWindowPos(hDlg, NULL, 0, 0,
                 wr.right - wr.left, wr.bottom - wr.top,
                 SWP_NOZORDER | SWP_NOMOVE | SWP_NOACTIVATE);

    /* 控件：位置按 x、尺寸按宽，各走自己的基数。 */
    for (i = 0; i < cl.n; i++) {
        RECT* r = &cl.rc[i];
        int x = MulDiv(r->left,             nbx, obx);
        int y = MulDiv(r->top,              nby, oby);
        int w = MulDiv(r->right - r->left,  nbx, obx);
        int h = MulDiv(r->bottom - r->top,  nby, oby);
        SetWindowPos(cl.list[i], NULL, x, y, w, h,
                     SWP_NOZORDER | SWP_NOACTIVATE);
    }
}

void ComboGetLBText(HWND hCombo, int index, Str* out) {
    int len = (int)SendMessageA(hCombo, CB_GETLBTEXTLEN, (WPARAM)index, 0);
    str_clear(out);
    if (len <= 0) return;
    if (!str_reserve(out, (size_t)len)) return;
    SendMessageA(hCombo, CB_GETLBTEXT, (WPARAM)index, (LPARAM)out->data);
    out->data[len] = '\0';
    out->len = (size_t)len;
}

char* ReadFileAll(const char* path, size_t* outLen) {
    FILE* f;
    long  sz;
    char* buf;
    size_t rd;

    f = fopen(path, "rb");
    if (!f) return NULL;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return NULL; }
    sz = ftell(f);
    if (sz < 0) { fclose(f); return NULL; }
    rewind(f);

    buf = (char*)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return NULL; }

    rd = fread(buf, 1, (size_t)sz, f);
    buf[rd] = '\0';
    fclose(f);
    if (outLen) *outLen = rd;
    return buf;
}

static void PushMenuBitmap(HBITMAP hBmp) {
    if (!hBmp) return;
    if (g_menuBitmapCount == g_menuBitmapCap) {
        size_t nc = g_menuBitmapCap ? g_menuBitmapCap * 2 : 8;
        HBITMAP* p = (HBITMAP*)realloc(g_menuBitmaps, nc * sizeof(HBITMAP));
        if (!p) return;
        g_menuBitmaps = p;
        g_menuBitmapCap = nc;
    }
    g_menuBitmaps[g_menuBitmapCount++] = hBmp;
}

/* 隐藏窗口执行外部命令并取回标准输出/错误 */
Str ExecHidden(const char* cmd) {
    SECURITY_ATTRIBUTES sa = { sizeof(sa), NULL, TRUE };
    HANDLE hRead, hWrite;
    Str result;
    char* mutableCmd;
    PROCESS_INFORMATION pi;
    STARTUPINFOA si;
    BOOL ok;

    str_init(&result);

    if (!CreatePipe(&hRead, &hWrite, &sa, 8192)) return result;

    memset(&pi, 0, sizeof(pi));
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hWrite;
    si.hStdError  = hWrite;
    si.wShowWindow = SW_HIDE;

    mutableCmd = MyStrDup(cmd);
    ok = CreateProcessA(NULL, mutableCmd, NULL, NULL, TRUE,
                        CREATE_NO_WINDOW, NULL, NULL, &si, &pi);
    free(mutableCmd);
    CloseHandle(hWrite);

    if (ok) {
        char buf[1024];
        DWORD read;
        WaitForSingleObject(pi.hProcess, 8000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
        while (ReadFile(hRead, buf, sizeof(buf) - 1, &read, NULL) && read > 0) {
            buf[read] = '\0';
            str_append(&result, buf);
        }
    }
    CloseHandle(hRead);
    return result;
}

void LogStatus(const char* msg) {
    if (g_ctrl.hStaticStatus) {
        SetWindowTextA(g_ctrl.hStaticStatus, msg);
    }
}

void ShowInfo(const char* msg, const char* title) {
    MessageBoxA(g_hMainDlg, msg, title, MB_ICONINFORMATION | MB_OK);
}

void ShowError(const char* msg, const char* title) {
    MessageBoxA(g_hMainDlg, msg, title, MB_ICONERROR | MB_OK);
}

/* ==========================================================================
 * 七、加密算法的获取
 * ========================================================================== */

void GetSupportedCiphers(StrList* out) {
    const char* sshPath = GetSSHPath();
    Str output;
    const char* p;

    sl_init(out);
    if (sshPath[0] == '\0') return;

    {
        Str cmd;
        str_init(&cmd);
        str_appendf(&cmd, "\"%s\" -Q cipher", sshPath);
        output = ExecHidden(cmd.data);
        str_free(&cmd);
    }

    if (output.len == 0) {
        str_free(&output);
        return;
    }

    p = output.data;
    while (*p) {
        const char* e = strchr(p, '\n');
        Str line;
        size_t n = e ? (size_t)(e - p) : strlen(p);
        str_init(&line);
        str_set_n(&line, p, n);
        str_trim(&line);
        if (line.len > 0) sl_push(out, line.data);
        str_free(&line);
        if (!e) break;
        p = e + 1;
    }
    str_free(&output);
}

char** GetCipherArray(size_t* outCount) {
    StrList v;
    StrList all;
    char** arr;
    size_t i;

    GetSupportedCiphers(&v);

    sl_init(&all);
    sl_push(&all, "默认");
    for (i = 0; i < v.count; i++) sl_push(&all, v.items[i]);
    sl_free(&v);

    *outCount = all.count;
    arr = (char**)calloc(all.count ? all.count : 1, sizeof(char*));
    for (i = 0; i < all.count; i++) arr[i] = MyStrDup(all.items[i]);
    sl_free(&all);
    return arr;
}

void FreeCipherArray(char** arr, size_t count) {
    size_t i;
    for (i = 0; i < count; i++) free(arr[i]);
    free(arr);
}

/* ==========================================================================
 * 八、界面初始化与提示
 * ========================================================================== */

void InitControls(HWND hDlg) {
    int parts[2];
    size_t cipherCount = 0;
    char** encs;
    size_t i;
    const char* sshPath;
    Str sshVersion;

    g_hMainDlg = hDlg;

    g_ctrl.hEditHost       = GetDlgItem(hDlg, IDC_EDIT_HOST);
    g_ctrl.hEditPort       = GetDlgItem(hDlg, IDC_EDIT_PORT);
    g_ctrl.hEditUser       = GetDlgItem(hDlg, IDC_EDIT_USER);
    g_ctrl.hEditTimeout    = GetDlgItem(hDlg, IDC_EDIT_TIMEOUT);
    g_ctrl.hEditExtraArgs  = GetDlgItem(hDlg, IDC_EDIT_EXTRA_ARGS);
    g_ctrl.hEditKeyFile    = GetDlgItem(hDlg, IDC_EDIT_KEY_FILE);
    g_ctrl.hRadioPassword  = GetDlgItem(hDlg, IDC_RADIO_PASSWORD);
    g_ctrl.hRadioKey       = GetDlgItem(hDlg, IDC_RADIO_KEY);
    g_ctrl.hChkCompress    = GetDlgItem(hDlg, IDC_CHK_COMPRESS);
    g_ctrl.hChkX11Forward  = GetDlgItem(hDlg, IDC_CHK_X11_FORWARD);
    g_ctrl.hChkVerbose     = GetDlgItem(hDlg, IDC_CHK_VERBOSE);
    g_ctrl.hComboEnc       = GetDlgItem(hDlg, IDC_COMBO_ENCRYPTION);
    g_ctrl.hBtnConnect     = GetDlgItem(hDlg, IDC_BTN_CONNECT);
    g_ctrl.hEditLocalFwd   = GetDlgItem(hDlg, IDC_EDIT_LOCAL_FWD);
    g_ctrl.hEditRemoteFwd  = GetDlgItem(hDlg, IDC_EDIT_REMOTE_FWD);
    g_ctrl.hEditDynamicFwd = GetDlgItem(hDlg, IDC_EDIT_DYNAMIC_FWD);
    g_ctrl.hChkKeepAlive   = GetDlgItem(hDlg, IDC_CHK_KEEPALIVE);
    g_ctrl.hEditProxyJump  = GetDlgItem(hDlg, IDC_EDIT_PROXY_JUMP);

    g_ctrl.hStaticStatus = GetDlgItem(hDlg, IDC_STATUS_BAR);
    /* 第一格放状态文字（"SSH 连接已启动"之类），200 是 96 dpi 下的宽度，
     * 跟着 DPI 一起放大，不然 150% 下字体变大了格子还是那么宽，文字被截断。 */
    parts[0] = gxS(200);
    parts[1] = -1;
    SendMessage(g_ctrl.hStaticStatus, SB_SETPARTS, 2, (LPARAM)parts);
    SetWindowTextA(g_ctrl.hStaticStatus, "");

    if (!g_hCustomBgBrush) {
        g_hCustomBgBrush = CreateSolidBrush(RGB(247, 247, 247));
    }

    SetWindowTextA(g_ctrl.hEditPort, "22");
    SetWindowTextA(g_ctrl.hEditTimeout, "10");
    CheckDlgButton(hDlg, IDC_RADIO_PASSWORD, BST_CHECKED);
    EnableWindow(g_ctrl.hEditKeyFile, FALSE);
    EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_KEY), FALSE);

    encs = GetCipherArray(&cipherCount);
    for (i = 0; i < cipherCount; i++) {
        SendMessageA(g_ctrl.hComboEnc, CB_ADDSTRING, 0, (LPARAM)encs[i]);
    }
    SendMessage(g_ctrl.hComboEnc, CB_SETCURSEL, 0, 0);
    FreeCipherArray(encs, cipherCount);

    sshPath = GetSSHPath();
    sshVersion = GetSSHVersion(sshPath);
    if (sshVersion.len > 0) {
        SendMessageA(g_ctrl.hStaticStatus, SB_SETTEXT, 1, (LPARAM)StrCStr(&sshVersion));
    }
    str_free(&sshVersion);

    if (!g_tooltipInitialized) {
        g_hTooltip = CreateWindowEx(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSA, NULL,
            TTS_BALLOON | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            hDlg, NULL, g_hInstance, NULL
        );
        if (g_hTooltip) {
            SetWindowPos(g_hTooltip, HWND_TOPMOST,
                         0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            g_tooltipInitialized = true;
        }
    }

    LogStatus("就绪 - 填写信息后点击「连接」");
}

void ShowBalloon(HWND hCtrl, const char* title, const char* text, DWORD icon) {
    TOOLINFOA ti;
    RECT rc;
    int x, y;

    if (!g_hTooltip || !hCtrl) return;

    memset(&ti, 0, sizeof(ti));
    ti.cbSize = sizeof(TOOLINFOA);
    ti.hwnd = GetParent(hCtrl);
    ti.uId = (UINT_PTR)hCtrl;
    SendMessageA(g_hTooltip, TTM_DELTOOL, 0, (LPARAM)&ti);

    ti.uFlags = TTF_IDISHWND | TTF_TRACK;
    ti.uId = (UINT_PTR)hCtrl;
    ti.lpszText = (LPSTR)text;
    SendMessageA(g_hTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti);

    SendMessageA(g_hTooltip, TTM_SETTITLE, (WPARAM)icon, (LPARAM)title);

    GetWindowRect(hCtrl, &rc);
    x = rc.left;
    y = rc.bottom + 2;

    SendMessageA(g_hTooltip, TTM_TRACKACTIVATE, TRUE, (LPARAM)&ti);
    SendMessageA(g_hTooltip, TTM_TRACKPOSITION, 0, MAKELPARAM(x, y));
    SetTimer(GetParent(hCtrl), 1001, 2000, NULL);
}

Str GetEditText(HWND hEdit) {
    char buf[512];
    Str s;
    memset(buf, 0, sizeof(buf));
    GetWindowTextA(hEdit, buf, sizeof(buf));
    str_init(&s);
    str_set(&s, buf);
    str_trim(&s);
    return s;
}

Str GetWindowTextTrim(HWND hCtrl) {
    Str s;
    int len = GetWindowTextLengthA(hCtrl);

    str_init(&s);
    if (len <= 0) return s;
    if (!str_reserve(&s, (size_t)len)) return s;
    GetWindowTextA(hCtrl, s.data, len + 1);
    s.len = strlen(s.data);
    str_trim(&s);
    return s;
}

void ReadConfigFromUI(HWND hDlg, SSHConfig* cfg) {
    int sel;

    sshcfg_init(cfg);

    cfg->host      = GetEditText(g_ctrl.hEditHost);
    cfg->port      = GetEditText(g_ctrl.hEditPort);
    cfg->username  = GetEditText(g_ctrl.hEditUser);
    cfg->timeout   = GetEditText(g_ctrl.hEditTimeout);
    cfg->extraArgs = GetEditText(g_ctrl.hEditExtraArgs);
    cfg->keyFile   = GetEditText(g_ctrl.hEditKeyFile);

    cfg->authType   = (IsDlgButtonChecked(g_hMainDlg, IDC_RADIO_KEY) == BST_CHECKED) ? 1 : 0;
    cfg->compress   = (IsDlgButtonChecked(g_hMainDlg, IDC_CHK_COMPRESS)   == BST_CHECKED);
    cfg->x11Forward = (IsDlgButtonChecked(g_hMainDlg, IDC_CHK_X11_FORWARD) == BST_CHECKED);
    cfg->verbose    = (IsDlgButtonChecked(g_hMainDlg, IDC_CHK_VERBOSE)     == BST_CHECKED);

    cfg->localFwd   = GetWindowTextTrim(GetDlgItem(hDlg, IDC_EDIT_LOCAL_FWD));
    cfg->remoteFwd  = GetWindowTextTrim(GetDlgItem(hDlg, IDC_EDIT_REMOTE_FWD));
    cfg->dynamicFwd = GetWindowTextTrim(GetDlgItem(hDlg, IDC_EDIT_DYNAMIC_FWD));
    cfg->keepAlive  = (IsDlgButtonChecked(hDlg, IDC_CHK_KEEPALIVE) == BST_CHECKED);
    cfg->proxyJump  = GetWindowTextTrim(GetDlgItem(hDlg, IDC_EDIT_PROXY_JUMP));

    sel = (int)SendMessage(g_ctrl.hComboEnc, CB_GETCURSEL, 0, 0);
    if (sel >= 0) {
        ComboGetLBText(g_ctrl.hComboEnc, sel, &cfg->encryption);
    }
}

void WriteConfigToUI(HWND hDlg, const SSHConfig* cfg) {
    HWND hCombo;

    SetWindowTextA(g_ctrl.hEditHost,      StrCStr(&cfg->host));
    SetWindowTextA(g_ctrl.hEditPort,      StrCStr(&cfg->port));
    SetWindowTextA(g_ctrl.hEditUser,      StrCStr(&cfg->username));
    SetWindowTextA(g_ctrl.hEditTimeout,   StrCStr(&cfg->timeout));
    SetWindowTextA(g_ctrl.hEditExtraArgs, StrCStr(&cfg->extraArgs));
    SetWindowTextA(g_ctrl.hEditKeyFile,   StrCStr(&cfg->keyFile));

    CheckDlgButton(g_hMainDlg, IDC_RADIO_PASSWORD, cfg->authType == 0 ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hMainDlg, IDC_RADIO_KEY,      cfg->authType == 1 ? BST_CHECKED : BST_UNCHECKED);

    CheckDlgButton(g_hMainDlg, IDC_CHK_COMPRESS,    cfg->compress   ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hMainDlg, IDC_CHK_X11_FORWARD, cfg->x11Forward ? BST_CHECKED : BST_UNCHECKED);
    CheckDlgButton(g_hMainDlg, IDC_CHK_VERBOSE,     cfg->verbose    ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemTextA(hDlg, IDC_EDIT_LOCAL_FWD,   StrCStr(&cfg->localFwd));
    SetDlgItemTextA(hDlg, IDC_EDIT_REMOTE_FWD,  StrCStr(&cfg->remoteFwd));
    SetDlgItemTextA(hDlg, IDC_EDIT_DYNAMIC_FWD, StrCStr(&cfg->dynamicFwd));
    CheckDlgButton(hDlg, IDC_CHK_KEEPALIVE, cfg->keepAlive ? BST_CHECKED : BST_UNCHECKED);

    SetDlgItemTextA(hDlg, IDC_EDIT_PROXY_JUMP, StrCStr(&cfg->proxyJump));

    /* 加密算法（使用 SendMessage） */
    hCombo = g_ctrl.hComboEnc;
    if (cfg->encryption.len > 0) {
        int idx = (int)SendMessageA(hCombo, CB_FINDSTRINGEXACT, (WPARAM)-1, (LPARAM)StrCStr(&cfg->encryption));
        if (idx >= 0) SendMessage(hCombo, CB_SETCURSEL, (WPARAM)idx, 0);
    }
}

bool ValidateConfig(const SSHConfig* cfg, Str* errMsg) {
    if (cfg->host.len == 0) {
        ShowBalloon(g_ctrl.hEditHost, "缺少主机地址", "请输入要连接的主机名或 IP 地址", TTI_ERROR);
        SetFocus(g_ctrl.hEditHost);
        if (errMsg) str_clear(errMsg);   /* 清空，避免上层再弹 MessageBox */
        return false;
    }
    if (cfg->port.len > 0) {
        char* endp = NULL;
        long p = strtol(StrCStr(&cfg->port), &endp, 10);
        if (endp == StrCStr(&cfg->port) || p <= 0 || p > 65535) {
            const char* tip = (endp == StrCStr(&cfg->port)) ? "端口号必须是有效数字"
                                                           : "端口号必须在 1-65535 之间";
            ShowBalloon(g_ctrl.hEditPort, "端口错误", tip, TTI_WARNING);
            SetFocus(g_ctrl.hEditPort);
            if (errMsg) str_clear(errMsg);
            return false;
        }
    }
    if (cfg->authType == 1 && cfg->keyFile.len == 0) {
        ShowBalloon(g_ctrl.hEditKeyFile, "缺少私钥文件", "使用密钥认证时请选择私钥文件", TTI_ERROR);
        SetFocus(g_ctrl.hEditKeyFile);
        if (errMsg) str_clear(errMsg);
        return false;
    }
    return true;
}

/* ==========================================================================
 * 九、核心：构建 ssh 命令并通过 PowerShell 启动
 * ========================================================================== */

Str BuildSSHCommand(const SSHConfig* cfg) {
    Str cmd;
    str_init(&cmd);

    str_append(&cmd, "ssh");

    if (cfg->username.len > 0) str_appendf(&cmd, " -l %s", StrCStr(&cfg->username));
    if (cfg->port.len > 0)     str_appendf(&cmd, " -p %s", StrCStr(&cfg->port));

    if (cfg->authType == 1 && cfg->keyFile.len > 0) {
        str_appendf(&cmd, " -i \"%s\"", StrCStr(&cfg->keyFile));
    }

    if (cfg->timeout.len > 0) str_appendf(&cmd, " -o ConnectTimeout=%s", StrCStr(&cfg->timeout));
    if (cfg->localFwd.len > 0)   str_appendf(&cmd, " -L %s", StrCStr(&cfg->localFwd));
    if (cfg->remoteFwd.len > 0)  str_appendf(&cmd, " -R %s", StrCStr(&cfg->remoteFwd));
    if (cfg->dynamicFwd.len > 0) str_appendf(&cmd, " -D %s", StrCStr(&cfg->dynamicFwd));
    if (cfg->proxyJump.len > 0)  str_appendf(&cmd, " -J %s", StrCStr(&cfg->proxyJump));
    if (cfg->keepAlive)          str_append(&cmd, " -o ServerAliveInterval=60");
    if (cfg->compress)           str_append(&cmd, " -C");
    if (cfg->x11Forward)         str_append(&cmd, " -X");
    if (cfg->verbose)            str_append(&cmd, " -v");

    if (cfg->encryption.len > 0 && strcmp(StrCStr(&cfg->encryption), "默认") != 0) {
        str_appendf(&cmd, " -c %s", StrCStr(&cfg->encryption));
    }

    if (cfg->extraArgs.len > 0) {
        Str ex;
        size_t i, j, w;
        int prevSpace;

        str_init(&ex);
        str_set(&ex, StrCStr(&cfg->extraArgs));

        /* 删除 \r */
        for (i = 0, j = 0; i < ex.len; i++) {
            if (ex.data[i] != '\r') ex.data[j++] = ex.data[i];
        }
        ex.data[j] = '\0';
        ex.len = j;

        /* \n -> 空格 */
        for (i = 0; i < ex.len; i++) {
            if (ex.data[i] == '\n') ex.data[i] = ' ';
        }

        /* 合并连续空格 */
        prevSpace = 0;
        for (i = 0, w = 0; i < ex.len; i++) {
            if (ex.data[i] == ' ' && prevSpace) continue;
            prevSpace = (ex.data[i] == ' ');
            ex.data[w++] = ex.data[i];
        }
        ex.data[w] = '\0';
        ex.len = w;

        str_append(&cmd, " ");
        str_append(&cmd, StrCStr(&ex));
        str_free(&ex);
    }

    str_appendf(&cmd, " %s", StrCStr(&cfg->host));
    return cmd;
}

bool IsSSHAvailable(void) {
    Str output = ExecHidden("where ssh");
    bool ok = (output.len > 0);
    str_free(&output);
    return ok;
}

bool IsFileExist(const char* path) {
    DWORD attr = GetFileAttributesA(path);
    return (attr != INVALID_FILE_ATTRIBUTES && !(attr & FILE_ATTRIBUTE_DIRECTORY));
}

const char* GetSSHPath(void) {
    if (g_userSelectedSSHPath.len > 0) {
        return StrCStr(&g_userSelectedSSHPath);
    }
    return "";
}

Str GetKeygenPath(void) {
    const char* sshPath = GetSSHPath();
    Str out;
    const char* pos;

    str_init(&out);
    if (sshPath[0] == '\0') {
        str_set(&out, "ssh-keygen");
        return out;
    }
    pos = strrchr(sshPath, '\\');
    if (pos) {
        str_set_n(&out, sshPath, (size_t)(pos - sshPath) + 1);
        str_append(&out, "ssh-keygen.exe");
        return out;
    }
    str_set(&out, "ssh-keygen");
    return out;
}

bool CopyTextToClipboard(const char* text) {
    HGLOBAL hMem;
    char* pMem;
    size_t n;

    if (!text || text[0] == '\0') return false;
    if (!OpenClipboard(NULL)) return false;

    EmptyClipboard();
    n = strlen(text);
    hMem = GlobalAlloc(GMEM_MOVEABLE, n + 1);
    if (!hMem) {
        CloseClipboard();
        return false;
    }
    pMem = (char*)GlobalLock(hMem);
    memcpy(pMem, text, n + 1);
    GlobalUnlock(hMem);

    SetClipboardData(CF_TEXT, hMem);
    CloseClipboard();
    return true;
}

Str GetSshDir(void) {
    char buf[MAX_PATH];
    Str dir;
    DWORD len;

    memset(buf, 0, sizeof(buf));
    str_init(&dir);
    len = GetEnvironmentVariableA("USERPROFILE", buf, MAX_PATH);
    if (len == 0 || len > MAX_PATH) return dir;
    str_set(&dir, buf);
    if (dir.len > 0) str_append(&dir, "\\.ssh");
    return dir;
}

Str GetAppDirectory(void) {
    char exePath[MAX_PATH];
    Str path;
    char* pos;

    memset(exePath, 0, sizeof(exePath));
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    str_init(&path);
    str_set(&path, exePath);
    pos = strrchr(StrCStr(&path), '\\');
    if (pos) {
        pos[1] = '\0';                                  /* 保留末尾反斜杠 */
        path.len = (size_t)(pos + 1 - path.data);
    }
    return path;
}

/* 「走环境变量 PATH 解析」那一项的取值与显示标签。
 *
 * 取值是裸的 "ssh"（不是路径，没有盘符没有分隔符），显示是
 * "ssh (环境变量)"。两者必须成对定义：取值给 CreateProcess /
 * ShellExecute 用（由 PATH 解析），显示给人看。
 *
 * 判定一律用下面的 IsEnvSSHPath()，不要在各处散写 strcmp(..., "ssh")：
 * 只要这一项曾经被当成路径处理过（IsFileExist），选它就报"无此路径"。 */
#define GX_SSH_ENV_VALUE   "ssh"
#define GX_SSH_ENV_LABEL   "ssh (环境变量)"

/* 这个取值是不是"交给 PATH 解析"而不是一个文件路径 */
static bool IsEnvSSHPath(const char* p) {
    if (!p || !*p) return false;
    /* 含盘符或分隔符就是路径；其余按裸命令名比较 */
    if (strchr(p, '\\') != NULL || strchr(p, '/') != NULL || strchr(p, ':') != NULL) {
        return false;
    }
    return StrEqCI(p, GX_SSH_ENV_VALUE);
}

void CollectAllSSHCandidates(StrList* out) {
    Str appDir;
    Str p;
    WIN32_FIND_DATAA ffd;
    HANDLE hFind;
    Str searchPattern;

    sl_init(out);
    str_init(&p);
    str_init(&searchPattern);
    appDir = GetAppDirectory();

    /* 环境变量那一项的真实取值是裸的 "ssh" —— 交给 CreateProcess /
     * ShellExecute 时由 PATH 解析，它不是一个文件路径，IsFileExist()
     * 对它没有意义（原来整串 "ssh (环境变量)" 被当成路径，于是
     * IsFileExist("ssh (环境变量)") 必然失败，选择它就报"无此路径"）。
     * " (环境变量)" 只是给人看的说明，见 GX_SSH_ENV_LABEL。 */
    if (IsSSHAvailable()) {
        sl_push(out, GX_SSH_ENV_VALUE);
    }

    str_setf(&p, "%sssh.exe", StrCStr(&appDir));
    if (IsFileExist(StrCStr(&p))) sl_push(out, StrCStr(&p));

    str_setf(&p, "%sOpenSSH\\ssh.exe", StrCStr(&appDir));
    if (IsFileExist(StrCStr(&p))) sl_push(out, StrCStr(&p));

    str_setf(&searchPattern, "%sOpenSSH\\*", StrCStr(&appDir));
    hFind = FindFirstFileA(StrCStr(&searchPattern), &ffd);
    if (hFind != INVALID_HANDLE_VALUE) {
        do {
            if (ffd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
                if (strcmp(ffd.cFileName, ".") != 0 && strcmp(ffd.cFileName, "..") != 0) {
                    str_setf(&p, "%sOpenSSH\\%s\\ssh.exe", StrCStr(&appDir), ffd.cFileName);
                    if (IsFileExist(StrCStr(&p))) sl_push(out, StrCStr(&p));
                }
            }
        } while (FindNextFileA(hFind, &ffd) != 0);
        FindClose(hFind);
    }

    if (IsFileExist("C:\\Program Files\\OpenSSH\\ssh.exe")) {
        sl_push(out, "C:\\Program Files\\OpenSSH\\ssh.exe");
    }
    if (IsFileExist("C:\\Program Files (x86)\\OpenSSH\\ssh.exe")) {
        sl_push(out, "C:\\Program Files (x86)\\OpenSSH\\ssh.exe");
    }

    sl_sort(out);
    sl_unique(out);

    str_free(&appDir);
    str_free(&p);
    str_free(&searchPattern);
}

Str GetSSHVersion(const char* sshPath) {
    Str cmd;
    Str output;

    str_init(&cmd);
    str_appendf(&cmd, "\"%s\" -V 2>&1", sshPath);
    output = ExecHidden(StrCStr(&cmd));
    str_free(&cmd);

    if (output.len > 0) str_chomp(&output);
    return output;
}

bool LaunchSSHViaPowerShell(const char* sshCmd) {
    const char* sshPath = GetSSHPath();
    Str params;
    Str fullCmd;
    Str psArgs;
    HINSTANCE h;

    str_init(&params);
    str_init(&fullCmd);
    str_init(&psArgs);

    /* 去掉命令开头的 "ssh" 三个字符 */
    str_set(&params, sshCmd + 3);
    str_appendf(&fullCmd, "\"%s\"%s", sshPath, StrCStr(&params));
    str_appendf(&psArgs, "-NoExit -Command \"Write-Host 'Executing: %s'; %s\"",
                StrCStr(&fullCmd), StrCStr(&fullCmd));

    h = ShellExecuteA(NULL, "open", "powershell.exe", StrCStr(&psArgs), NULL, SW_SHOWNORMAL);

    str_free(&params);
    str_free(&fullCmd);
    str_free(&psArgs);

    return ((INT_PTR)h > 32);
}

void RefreshAfterSSHChange(HWND hDlg) {
    HWND hComboEnc = GetDlgItem(hDlg, IDC_COMBO_ENCRYPTION);
    const char* sshPath;
    Str versionStr;
    Str logMsg;

    if (hComboEnc) {
        size_t cipherCount = 0;
        char** encs;
        size_t i;
        SendMessage(hComboEnc, CB_RESETCONTENT, 0, 0);
        encs = GetCipherArray(&cipherCount);
        for (i = 0; i < cipherCount; i++) {
            SendMessageA(hComboEnc, CB_ADDSTRING, 0, (LPARAM)encs[i]);
        }
        FreeCipherArray(encs, cipherCount);
        SendMessage(hComboEnc, CB_SETCURSEL, 0, 0);
    }

    sshPath = GetSSHPath();
    versionStr = GetSSHVersion(sshPath);
    if (versionStr.len > 0) {
        SendMessageA(g_ctrl.hStaticStatus, SB_SETTEXT, 1, (LPARAM)StrCStr(&versionStr));
    } else {
        SendMessageA(g_ctrl.hStaticStatus, SB_SETTEXT, 1, (LPARAM)"");
    }
    str_free(&versionStr);

    str_init(&logMsg);
    str_appendf(&logMsg, "SSH 路径切换至: %s", sshPath);
    LogStatus(StrCStr(&logMsg));
    str_free(&logMsg);
}

/* ==========================================================================
 * 十、连接 / 密钥 / 工具菜单
 * ========================================================================== */

void OnConnect(HWND hDlg) {
    SSHConfig cfg;
    Str errMsg;
    Str sshCmd;

    ReadConfigFromUI(hDlg, &cfg);
    str_init(&errMsg);

    if (!ValidateConfig(&cfg, &errMsg)) {
        LogStatus("连接失败");
        sshcfg_free(&cfg);
        str_free(&errMsg);
        return;
    }
    str_free(&errMsg);

    SaveConfigToIni(hDlg);

    sshCmd = BuildSSHCommand(&cfg);
    LogStatus(StrCStr(&sshCmd));

    if (!LaunchSSHViaPowerShell(StrCStr(&sshCmd))) {
        ShowError("启动 SSH 连接失败！\n请确保程序目录下有 SSH 核心或环境变量有 OpenSSH。\n\n", "错误");
        LogStatus("启动失败");
    } else {
        LogStatus("SSH 连接已启动");
    }

    str_free(&sshCmd);
    sshcfg_free(&cfg);
}

void OnBrowseKeyFile(void) {
    OPENFILENAMEA ofn;
    char fileBuf[MAX_PATH];

    memset(&ofn, 0, sizeof(ofn));
    memset(fileBuf, 0, sizeof(fileBuf));

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = g_hMainDlg;
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = "密钥文件\0*.pem;*.ppk;*.key;*.priv\0所有文件\0*.*\0";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = "选择 SSH 私钥文件";

    if (GetOpenFileNameA(&ofn)) {
        SetWindowTextA(g_ctrl.hEditKeyFile, fileBuf);
    }
}

void OnOpenSshDir(HWND hDlg) {
    Str sshDir = GetSshDir();
    Str quoted;
    Str msg;

    (void)hDlg;

    if (sshDir.len == 0) {
        ShowError("无法获取 USERPROFILE 环境变量!", "错误");
        str_free(&sshDir);
        return;
    }

    if (GetFileAttributesA(StrCStr(&sshDir)) == INVALID_FILE_ATTRIBUTES) {
        CreateDirectoryA(StrCStr(&sshDir), NULL);
    }

    str_init(&quoted);
    str_appendf(&quoted, "\"%s\"", StrCStr(&sshDir));

    if ((INT_PTR)ShellExecuteA(NULL, "open", "explorer.exe", StrCStr(&quoted), NULL, SW_SHOWNORMAL) <= 32) {
        str_init(&msg);
        str_appendf(&msg, "无法打开文件夹:\n%s", StrCStr(&sshDir));
        ShowError(StrCStr(&msg), "错误");
        str_free(&msg);
    }
    str_free(&quoted);
    str_free(&sshDir);
}

void OnGenerateKeyPair(HWND hDlg) {
    int ret;
    Str sshDir;
    Str keygenPath;
    Str cmdLine;

    ret = MessageBoxA(hDlg,
        "将生成 Ed25519 密钥对（推荐）。\n"
        "私钥: %USERPROFILE%\\.ssh\\id_ed25519\n"
        "公钥: %USERPROFILE%\\.ssh\\id_ed25519.pub\n\n"
        "点击 [是] 生成 Ed25519 密钥\n"
        "点击 [否] 生成 RSA 4096 密钥",
        "生成 SSH 密钥对",
        MB_YESNOCANCEL | MB_ICONQUESTION);

    if (ret == IDCANCEL) return;

    sshDir     = GetSshDir();
    keygenPath = GetKeygenPath();

    str_init(&cmdLine);
    str_append(&cmdLine, "/k echo Generating SSH key pair... & ");
    str_appendf(&cmdLine, "mkdir \"%s\" 2>nul & ", StrCStr(&sshDir));

    if (ret == IDYES) {
        str_appendf(&cmdLine, "\"%s\" -t ed25519 -f \"%s\\id_ed25519\" -N \"\" -C \"SSHLauncher\"",
                    StrCStr(&keygenPath), StrCStr(&sshDir));
    } else {
        str_appendf(&cmdLine, "\"%s\" -t rsa -b 4096 -f \"%s\\id_rsa\" -N \"\" -C \"SSHLauncher\"",
                    StrCStr(&keygenPath), StrCStr(&sshDir));
    }

    str_appendf(&cmdLine, " & echo. & if exist \"%s%s\" (", StrCStr(&sshDir),
                (ret == IDYES) ? "\\id_ed25519.pub" : "\\id_rsa.pub");
    str_append(&cmdLine, "echo Key pair generated successfully!) else (echo Generation failed!)");
    str_append(&cmdLine, " & echo. & echo Press any key to close... & pause > nul");

    ShellExecuteA(NULL, "open", "cmd.exe", StrCStr(&cmdLine), NULL, SW_SHOWNORMAL);

    str_free(&cmdLine);
    str_free(&keygenPath);
    str_free(&sshDir);
}

void OnCopyPublicKey(HWND hDlg) {
    (void)hDlg;
    Str sshDir;
    Str pubKeyPath;
    static const char* candidates[] = {
        "\\id_ed25519.pub",
        "\\id_ecdsa.pub",
        "\\id_rsa.pub",
        "\\id_dsa.pub",
        "\\id_ed25519_sk.pub"
    };
    size_t i;
    char* content;

    sshDir = GetSshDir();
    str_init(&pubKeyPath);

    for (i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        Str path;
        str_init(&path);
        str_setf(&path, "%s%s", StrCStr(&sshDir), candidates[i]);
        if (IsFileExist(StrCStr(&path))) {
            str_set(&pubKeyPath, StrCStr(&path));
            str_free(&path);
            break;
        }
        str_free(&path);
    }

    if (pubKeyPath.len == 0) {
        Str msg;
        str_init(&msg);
        str_appendf(&msg, "未找到公钥文件！\n\n"
                          "请先使用 [工具 → 生成密钥对] 创建密钥。\n\n"
                          "查找目录: %s", StrCStr(&sshDir));
        ShowError(StrCStr(&msg), "无公钥");
        str_free(&msg);
        str_free(&pubKeyPath);
        str_free(&sshDir);
        return;
    }

    content = ReadFileAll(StrCStr(&pubKeyPath), NULL);
    if (!content) {
        Str msg;
        str_init(&msg);
        str_appendf(&msg, "无法读取公钥文件：\n%s", StrCStr(&pubKeyPath));
        ShowError(StrCStr(&msg), "错误");
        str_free(&msg);
        str_free(&pubKeyPath);
        str_free(&sshDir);
        return;
    }

    {
        Str pubKey;
        str_init(&pubKey);
        str_set(&pubKey, content);
        free(content);
        str_chomp(&pubKey);

        if (CopyTextToClipboard(StrCStr(&pubKey))) {
            Str msg;
            str_init(&msg);
            str_appendf(&msg, "公钥已复制到剪贴板！\n\n"
                              "文件: %s\n\n"
                              "请粘贴到目标服务器的\n"
                              "~/.ssh/authorized_keys 文件中。", StrCStr(&pubKeyPath));
            ShowInfo(StrCStr(&msg), "复制成功");
            str_free(&msg);
        } else {
            ShowError("复制到剪贴板失败！", "错误");
        }
        str_free(&pubKey);
    }

    str_free(&pubKeyPath);
    str_free(&sshDir);
}

void OnSSHConnectionTest(HWND hDlg) {
    Str host;
    SSHConfig cfg;
    const char* sshPath;
    Str testCmd;
    Str cmdLine;

    host = GetEditText(g_ctrl.hEditHost);
    if (host.len == 0) {
        ShowError("请先输入主机地址！", "错误");
        str_free(&host);
        return;
    }

    ReadConfigFromUI(hDlg, &cfg);
    sshPath = GetSSHPath();
    if (sshPath[0] == '\0') {
        ShowError("未选择 OpenSSH 路径！", "错误");
        sshcfg_free(&cfg);
        str_free(&host);
        return;
    }

    {
        const char* port = (cfg.port.len > 0) ? StrCStr(&cfg.port) : "22";
        str_init(&testCmd);
        str_appendf(&testCmd, "\"%s\" -v -N -o ConnectTimeout=5 -o StrictHostKeyChecking=no -o BatchMode=yes -p %s",
                    sshPath, port);
        if (cfg.authType == 1 && cfg.keyFile.len > 0) {
            str_appendf(&testCmd, " -i \"%s\"", StrCStr(&cfg.keyFile));
        }
        str_appendf(&testCmd, " %s@%s 2>&1 | findstr \"Authenticated\"", StrCStr(&cfg.username), StrCStr(&host));
    }

    str_init(&cmdLine);
    str_appendf(&cmdLine, "/k echo Testing SSH connection to %s... & ", StrCStr(&host));
    str_append(&cmdLine, StrCStr(&testCmd));
    str_append(&cmdLine, " & if errorlevel 1 (echo Connection FAILED - authentication failed) else (echo Connection SUCCESS - authenticated)");
    str_append(&cmdLine, " & echo. & echo --- Test completed. Press any key to close --- & pause > nul");

    ShellExecuteA(NULL, "open", "cmd.exe", StrCStr(&cmdLine), NULL, SW_SHOWNORMAL);

    str_free(&cmdLine);
    str_free(&testCmd);
    sshcfg_free(&cfg);
    str_free(&host);
}

void OnAddHostKey(HWND hDlg) {
    (void)hDlg;
    Str host;
    Str sshPath;
    Str keyscanPath;
    Str cmdLine;
    const char* pos;

    host = GetEditText(g_ctrl.hEditHost);
    if (host.len == 0) {
        ShowError("请先输入主机地址！", "错误");
        str_free(&host);
        return;
    }

    str_init(&sshPath);
    str_set(&sshPath, GetSSHPath());
    if (sshPath.len == 0) {
        ShowError("未选择 OpenSSH 路径！", "错误");
        str_free(&sshPath);
        str_free(&host);
        return;
    }

    /* 获取 ssh-keyscan 路径（与 ssh.exe 同目录） */
    str_init(&keyscanPath);
    pos = strrchr(StrCStr(&sshPath), '\\');
    if (pos) {
        str_set_n(&keyscanPath, StrCStr(&sshPath), (size_t)(pos - StrCStr(&sshPath)) + 1);
        str_append(&keyscanPath, "ssh-keyscan.exe");
    } else {
        /* 如果 sshPath 只是 "ssh"（环境变量），则直接使用 "ssh-keyscan" */
        str_set(&keyscanPath, "ssh-keyscan");
    }

    if (strcmp(StrCStr(&keyscanPath), "ssh-keyscan") != 0 && !IsFileExist(StrCStr(&keyscanPath))) {
        Str msg;
        str_init(&msg);
        str_appendf(&msg, "未找到 ssh-keyscan.exe！\n\n请确保它与 ssh.exe 在同一目录。\n\n查找路径: %s",
                    StrCStr(&keyscanPath));
        ShowError(StrCStr(&msg), "错误");
        str_free(&msg);
        str_free(&keyscanPath);
        str_free(&sshPath);
        str_free(&host);
        return;
    }

    str_init(&cmdLine);
    str_appendf(&cmdLine, "/k echo Adding host key for %s ... & ", StrCStr(&host));
    str_appendf(&cmdLine, "\"%s\" -H %s >> \"%%USERPROFILE%%\\.ssh\\known_hosts\"",
                StrCStr(&keyscanPath), StrCStr(&host));
    str_append(&cmdLine, " & echo. & echo Done! Press any key to close... & pause > nul");

    ShellExecuteA(NULL, "open", "cmd.exe", StrCStr(&cmdLine), NULL, SW_SHOWNORMAL);

    str_free(&cmdLine);
    str_free(&keyscanPath);
    str_free(&sshPath);
    str_free(&host);
}

void ExportConfigToBatch(HWND hDlg) {
    SSHConfig cfg;
    Str errMsg;
    OPENFILENAMEA ofn;
    char fileBuf[MAX_PATH];
    Str sshCmd;
    Str sshPath;
    Str fullCmd;

    ReadConfigFromUI(hDlg, &cfg);
    str_init(&errMsg);

    if (!ValidateConfig(&cfg, &errMsg)) {
        ShowError(errMsg.len > 0 ? StrCStr(&errMsg) : "配置无效，请检查输入。", "错误");
        str_free(&errMsg);
        sshcfg_free(&cfg);
        return;
    }
    str_free(&errMsg);

    memset(&ofn, 0, sizeof(ofn));
    memset(fileBuf, 0, sizeof(fileBuf));
    strncpy(fileBuf, "ssh_connect.bat", MAX_PATH - 1);
    fileBuf[MAX_PATH - 1] = '\0';

    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hDlg;
    ofn.lpstrFile   = fileBuf;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrFilter = "批处理文件\0*.bat;*.cmd\0所有文件\0*.*\0";
    ofn.Flags       = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle  = "导出为批处理文件";
    ofn.lpstrDefExt = "bat";

    if (!GetSaveFileNameA(&ofn)) {
        sshcfg_free(&cfg);
        return;
    }

    sshCmd  = BuildSSHCommand(&cfg);
    str_init(&sshPath);
    str_set(&sshPath, GetSSHPath());

    if (sshPath.len >= 2 && sshPath.data[0] == '.' && sshPath.data[1] == '\\') {
        Str appDir = GetAppDirectory();
        Str tmp;
        str_init(&tmp);
        str_setf(&tmp, "%s%s", StrCStr(&appDir), StrCStr(&sshPath) + 2);
        str_set(&sshPath, StrCStr(&tmp));
        str_free(&tmp);
        str_free(&appDir);
    }

    str_init(&fullCmd);
    if (sshPath.len > 0) {
        str_appendf(&fullCmd, "\"%s\"%s", StrCStr(&sshPath), StrCStr(&sshCmd) + 3);
    } else {
        str_set(&fullCmd, StrCStr(&sshCmd));
    }

    {
        FILE* outFile = fopen(fileBuf, "w");
        if (!outFile) {
            Str msg;
            str_init(&msg);
            str_appendf(&msg, "无法创建文件：%s", fileBuf);
            ShowError(StrCStr(&msg), "错误");
            str_free(&msg);
            str_free(&fullCmd);
            str_free(&sshPath);
            str_free(&sshCmd);
            sshcfg_free(&cfg);
            return;
        }

        fprintf(outFile, "@echo off\n");
        fprintf(outFile, "REM ============================================\n");
        fprintf(outFile, "REM SSH 连接批处理 - 由 SSH Terminal Launcher 生成\n");
        fprintf(outFile, "REM 主机: %s\n", StrCStr(&cfg.host));
        fprintf(outFile, "REM 用户: %s\n", StrCStr(&cfg.username));
        fprintf(outFile, "REM 端口: %s\n", StrCStr(&cfg.port));
        fprintf(outFile, "REM ============================================\n");
        fprintf(outFile, "echo 正在连接 %s@%s...\n", StrCStr(&cfg.username), StrCStr(&cfg.host));
        fprintf(outFile, "%s\n", StrCStr(&fullCmd));
        fprintf(outFile, "echo.\n");
        fprintf(outFile, "echo 连接已关闭。\n");
        fprintf(outFile, "pause\n");
        fclose(outFile);
    }

    {
        Str msg;
        str_init(&msg);
        str_appendf(&msg, "配置已导出为：\n%s", fileBuf);
        ShowInfo(StrCStr(&msg), "导出成功");
        str_free(&msg);
    }

    if (MessageBoxA(hDlg, "是否打开文件所在文件夹？", "提示", MB_YESNO | MB_ICONQUESTION) == IDYES) {
        char* pos = strrchr(fileBuf, '\\');
        if (pos) {
            *pos = '\0';
            ShellExecuteA(NULL, "open", "explorer.exe", fileBuf, NULL, SW_SHOWNORMAL);
        }
    }

    str_free(&fullCmd);
    str_free(&sshPath);
    str_free(&sshCmd);
    sshcfg_free(&cfg);
}

/* ==========================================================================
 * 十一、自定义 SSH 路径管理（UserSSH.ini）
 * ========================================================================== */

/* ---------- 路径比较与「显示用相对路径」 ----------
 *
 * Windows 路径有两个坑，比较时必须先抹平：
 *   1. 大小写不敏感   C:\App\ssh.exe 和 c:\app\SSH.EXE 是同一个文件
 *   2. 分隔符两可     / 和 \ 都能用（GetOpenFileNameA 给的是 \，但手写的
 *                     配置里出现 / 并不违法）
 * 所以下面几个函数一律先把 / 折成 \、再逐字符转小写比较，而不是 strcmp。
 */

/* 两个路径是否同一个（分隔符归一 + 大小写不敏感） */
static bool PathEqCI(const char* a, const char* b) {
    if (!a) a = "";
    if (!b) b = "";
    for (; *a && *b; a++, b++) {
        int ca = tolower((unsigned char)*a);
        int cb = tolower((unsigned char)*b);
        if (ca == '/') ca = '\\';
        if (cb == '/') cb = '\\';
        if (ca != cb) return false;
    }
    return (*a == '\0' && *b == '\0');
}

/* abs 是否以 prefix 开头（同样归一后比较）。prefix 通常带末尾反斜杠。 */
static bool PathHasPrefixCI(const char* abs, const char* prefix) {
    size_t n = prefix ? strlen(prefix) : 0;
    size_t i;
    if (!abs || strlen(abs) < n) return false;
    for (i = 0; i < n; i++) {
        int ca = tolower((unsigned char)abs[i]);
        int cp = tolower((unsigned char)prefix[i]);
        if (ca == '/') ca = '\\';
        if (cp == '/') cp = '\\';
        if (ca != cp) return false;
    }
    return true;
}

/* 绝对路径 → 显示文本。
 *
 * 落在程序目录（EXE 所在目录）下的路径相对化成 ".\子路径" —— 程序自带的
 * OpenSSH 就放在那，相对化之后既短又能一眼看出是"跟着程序走的"。
 *
 * 程序目录之外的路径（C:\Program Files\OpenSSH\ssh.exe、用户从别处挑的
 * 自定义路径）原样显示绝对路径，不去硬凑：相对路径只能相对某一个基准，
 * 换一个基准就指向别的文件了，把一个本来无关的路径显示成 "..\..\.."
 * 只会误导。真要缩短只能截中间（"...\OpenSSH\ssh.exe"），那是另一回事，
 * 而且会丢信息 —— 这里不做。
 *
 * "ssh"（环境变量那一项）不是路径，原样返回。 */
static void PathToDisplay(const char* absPath, Str* out) {
    Str appDir;
    size_t baseLen;
    const char* rest;

    str_set(out, absPath ? absPath : "");
    if (!absPath || !*absPath) return;
    /* 不含任何分隔符 => 不是路径（"ssh"），原样 */
    if (strchr(absPath, '\\') == NULL && strchr(absPath, '/') == NULL) return;

    appDir = GetAppDirectory();
    baseLen = strlen(StrCStr(&appDir));
    if (baseLen > 0 && strlen(absPath) > baseLen &&
        PathHasPrefixCI(absPath, StrCStr(&appDir))) {
        rest = absPath + baseLen;
        while (*rest == '\\' || *rest == '/') rest++;   /* 吃掉多余分隔符 */
        if (*rest) {
            /* 余下部分里的 '/' 折成 '\'：显示文本要跟别的项长得一样，
             * 同一目录不应该一会儿 ".\OpenSSH/ssh.exe" 一会儿
             * ".\OpenSSH\ssh.exe"（源文件可能来自注册表、命令行，斜杠方向
             * 不一定统一）。 */
            Str tail;
            size_t k;
            str_init(&tail);
            str_set(&tail, rest);
            for (k = 0; k < tail.len; k++) {
                if (tail.data[k] == '/') tail.data[k] = '\\';
            }
            str_setf(out, ".\\%s", StrCStr(&tail));
            str_free(&tail);
        }
    }
    str_free(&appDir);
}

/* 注意：显示出来的 ".\xxx" 绝不能直接拿去用。
 * CreateProcess / ShellExecute 里的相对路径是相对【当前工作目录】解析的，
 * 而当前工作目录不一定是程序目录（从别的目录起 exe、或被别的程序拉起来
 * 时就不一样）。所以点确定时取的一定是 g_sshComboReal 里的绝对路径，
 * 显示文本只负责好看。 */

Str GetUserSSHIniPath(void) {
    Str appDir = GetAppDirectory();
    Str out;
    str_init(&out);
    str_setf(&out, "%sUserSSH.ini", StrCStr(&appDir));
    str_free(&appDir);
    return out;
}

void LoadCustomSSHPaths(StrList* out) {
    Str iniPath;
    FILE* fp;
    char line[2048];
    bool inSection = false;

    sl_init(out);
    iniPath = GetUserSSHIniPath();
    fp = fopen(StrCStr(&iniPath), "r");
    str_free(&iniPath);
    if (!fp) return;

    while (fgets(line, sizeof(line), fp)) {
        size_t n = strlen(line);
        char* eq;
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r' || line[n - 1] == ' ' || line[n - 1] == '\t')) {
            line[--n] = '\0';
        }
        {
            size_t k = 0;
            while (k < n && (line[k] == ' ' || line[k] == '\t')) k++;
            if (k > 0 && n >= k) memmove(line, line + k, n - k + 1);
        }

        if (strcmp(line, "[CustomSSH]") == 0) {
            inSection = true;
            continue;
        }
        if (line[0] == '\0' || line[0] == '[') {
            inSection = false;
            continue;
        }
        if (inSection) {
            eq = strchr(line, '=');
            if (eq) {
                char* val = eq + 1;
                size_t vlen = strlen(val);
                if (vlen > 0 && val[0] == '"') { val++; vlen--; }
                if (vlen > 0 && val[vlen - 1] == '"') vlen--;
                if (vlen > 0) {
                    char* v = (char*)malloc(vlen + 1);
                    if (v) {
                        memcpy(v, val, vlen);
                        v[vlen] = '\0';
                        sl_push(out, v);
                        free(v);
                    }
                }
            }
        }
    }
    fclose(fp);
}

bool SaveCustomSSHPaths(const StrList* paths) {
    Str iniPath;
    FILE* fp;
    size_t i;

    iniPath = GetUserSSHIniPath();
    fp = fopen(StrCStr(&iniPath), "w");
    str_free(&iniPath);
    if (!fp) return false;

    fprintf(fp, "[CustomSSH]\r\n");
    for (i = 0; i < paths->count; i++) {
        fprintf(fp, "Path%u=\"%s\"\r\n", (unsigned)(i + 1), paths->items[i]);
    }
    fclose(fp);
    return true;
}

/* 某个真实路径是否属于用户的自定义列表。
 *
 * 原来是看显示文本有没有 "[自定义] " 前缀 —— 那等于把「是不是自定义」
 * 这个事实寄存在显示字符串里，一旦显示文本改成相对路径（甚至只是换个
 * 前缀措辞）判断就失效。现在直接查 UserSSH.ini，事实只有一个来源。 */
bool IsCustomRealPath(const char* realPath) {
    StrList paths;
    bool r = false;
    size_t i;

    if (!realPath) return false;
    LoadCustomSSHPaths(&paths);
    for (i = 0; i < paths.count; i++) {
        if (PathEqCI(paths.items[i], realPath)) { r = true; break; }
    }
    sl_free(&paths);
    return r;
}

void OnAddCustomSSH(HWND hDlg) {
    char filePath[MAX_PATH];
    OPENFILENAMEA ofn;
    StrList paths;
    HWND hCombo;
    HWND hOkBtn;
    int count;

    memset(filePath, 0, sizeof(filePath));
    memset(&ofn, 0, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner   = hDlg;
    ofn.lpstrFilter = "SSH 可执行文件 (*.exe)\0*.exe\0所有文件 (*.*)\0*.*\0";
    ofn.lpstrFile   = filePath;
    ofn.nMaxFile    = MAX_PATH;
    ofn.lpstrTitle  = "请选择 ssh.exe 文件";
    ofn.Flags       = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;

    if (!GetOpenFileNameA(&ofn)) return;

    if (!StrContainsCI(filePath, "ssh")) {
        if (MessageBoxA(hDlg,
                "您选择的文件不包含 \"ssh\" 字样。\n确定要使用这个文件吗？",
                "确认", MB_YESNO | MB_ICONWARNING) != IDYES) {
            return;
        }
    }

    LoadCustomSSHPaths(&paths);

    if (sl_contains_ci(&paths, filePath)) {
        MessageBoxA(hDlg, "该路径已存在于自定义列表中！", "提示", MB_OK | MB_ICONINFORMATION);
        sl_free(&paths);
        return;
    }

    sl_push(&paths, filePath);
    SaveCustomSSHPaths(&paths);
    sl_free(&paths);

    hCombo = GetDlgItem(hDlg, IDC_COMBO_SSH_PATH);
    hOkBtn = GetDlgItem(hDlg, IDOK);
    PopulateSSHCombo(hCombo, hOkBtn);

    count = (int)SendMessage(hCombo, CB_GETCOUNT, 0, 0);
    if (count > 0) {
        Str searchPrefix;
        Str item;
        int i;
        str_init(&searchPrefix);
        str_appendf(&searchPrefix, "[自定义] %s", filePath);
        str_init(&item);
        for (i = 0; i < count; i++) {
            ComboGetLBText(hCombo, i, &item);
            if (strcmp(StrCStr(&item), StrCStr(&searchPrefix)) == 0) {
                SendMessage(hCombo, CB_SETCURSEL, i, 0);
                break;
            }
        }
        str_free(&item);
        str_free(&searchPrefix);
    }

    {
        Str msg;
        Str disp;
        str_init(&disp);
        PathToDisplay(filePath, &disp);
        str_init(&msg);
        str_appendf(&msg, "已添加自定义 SSH 路径：\n%s\n\n（完整路径：%s）",
                    StrCStr(&disp), filePath);
        MessageBoxA(hDlg, StrCStr(&msg), "添加成功", MB_OK | MB_ICONINFORMATION);
        str_free(&msg);
        str_free(&disp);
    }
}

void OnDeleteCustomSSH(HWND hDlg) {
    HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_SSH_PATH);
    int sel;
    const char* realPath;
    StrList paths;
    HWND hOkBtn;

    if (!hCombo) return;

    sel = (int)SendMessageA(hCombo, CB_GETCURSEL, 0, 0);
    if (sel == CB_ERR) {
        MessageBoxA(hDlg, "请先选择一个 SSH 路径！", "提示", MB_OK | MB_ICONINFORMATION);
        return;
    }

    realPath = ComboRealPath(hCombo, sel);
    if (!realPath) {
        MessageBoxA(hDlg, "无法识别所选项目，请重新选择！", "删除失败", MB_OK | MB_ICONERROR);
        return;
    }

    /* 是否自定义，查 UserSSH.ini，不看显示文本 */
    if (!IsCustomRealPath(realPath)) {
        MessageBoxA(hDlg, "只能删除自定义添加的 SSH 路径！", "删除失败", MB_OK | MB_ICONERROR);
        return;
    }

    {
        Str msg;
        Str disp;
        str_init(&disp);
        PathToDisplay(realPath, &disp);
        str_init(&msg);
        /* 删除是不可撤销的操作，确认框里给出完整绝对路径，不留歧义 */
        str_appendf(&msg, "确定要删除以下自定义 SSH 路径吗？\n\n%s\n\n（完整路径：%s）",
                    StrCStr(&disp), realPath);
        if (MessageBoxA(hDlg, StrCStr(&msg), "确认删除", MB_OKCANCEL | MB_ICONQUESTION) != IDOK) {
            str_free(&msg);
            str_free(&disp);
            return;
        }
        str_free(&msg);
        str_free(&disp);

        LoadCustomSSHPaths(&paths);
        sl_remove_ci(&paths, realPath);
        SaveCustomSSHPaths(&paths);
        sl_free(&paths);
    }

    hOkBtn = GetDlgItem(hDlg, IDOK);
    PopulateSSHCombo(hCombo, hOkBtn);

    {
        int count = (int)SendMessage(hCombo, CB_GETCOUNT, 0, 0);
        int newSel = 0;
        int i;
        for (i = 0; i < count; i++) {
            const char* r = ComboRealPath(hCombo, i);
            if (r && IsCustomRealPath(r)) { newSel = i; break; }
        }
        SendMessage(hCombo, CB_SETCURSEL, (WPARAM)newSel, 0);
    }

    MessageBoxA(hDlg, "已删除自定义 SSH 路径。", "删除成功", MB_OK | MB_ICONINFORMATION);
}

/* ==========================================================================
 * 十二、SSH 路径配置（SSHPath.ini）
 * ========================================================================== */

Str GetSSHPathFilePath(void) {
    char exePath[MAX_PATH];
    Str path;
    char* pos;

    memset(exePath, 0, sizeof(exePath));
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    str_init(&path);
    str_set(&path, exePath);
    pos = strrchr(StrCStr(&path), '\\');
    if (pos) {
        pos[1] = '\0';
        path.len = (size_t)(pos + 1 - path.data);
    }
    str_append(&path, "SSHPath.ini");
    return path;
}

void SaveSSHPathConfig(const char* sshPath, bool remember) {
    Str path = GetSSHPathFilePath();
    const char* sec = "SSHPath";
    WritePrivateProfileStringA(sec, "Path", sshPath, StrCStr(&path));
    WritePrivateProfileStringA(sec, "Remember", remember ? "1" : "0", StrCStr(&path));
    str_free(&path);
}

bool LoadSSHPathConfig(Str* outPath) {
    Str path = GetSSHPathFilePath();
    const char* sec = "SSHPath";
    char buf[MAX_PATH];
    int remember;

    str_clear(outPath);

    if (GetFileAttributesA(StrCStr(&path)) == INVALID_FILE_ATTRIBUTES) {
        str_free(&path);
        return false;
    }

    remember = GetPrivateProfileIntA(sec, "Remember", 0, StrCStr(&path));
    if (remember != 1) {
        str_free(&path);
        return false;
    }

    memset(buf, 0, sizeof(buf));
    GetPrivateProfileStringA(sec, "Path", "", buf, MAX_PATH, StrCStr(&path));
    str_free(&path);

    if (buf[0] == '\0') return false;

    /* 兼容旧配置：早期版本存过 "ssh (环境变量)" 这种带说明后缀的写法。
     * 它不是可执行的取值，读到就折算成裸的 "ssh"，否则启动时会拿着它
     * 去 IsFileExist() 判定而失败，表现为"记住的路径失效"。 */
    if (StrContainsCI(buf, "环境变量")) {
        str_set(outPath, GX_SSH_ENV_VALUE);
        return true;
    }

    str_set(outPath, buf);
    return true;
}

/* ==========================================================================
 * 十三、会话配置（SSHLauncher.ini）
 * ========================================================================== */

Str GetConfigFilePath(void) {
    char exePath[MAX_PATH];
    Str path;
    char* pos;

    memset(exePath, 0, sizeof(exePath));
    GetModuleFileNameA(NULL, exePath, MAX_PATH);
    str_init(&path);
    str_set(&path, exePath);
    pos = strrchr(StrCStr(&path), '\\');
    if (pos) {
        pos[1] = '\0';
        path.len = (size_t)(pos + 1 - path.data);
    }
    str_append(&path, "SSHLauncher.ini");
    return path;
}

void IniGetString(const char* file, const char* section, const char* key, const char* defVal, Str* out) {
    char buf[512];
    memset(buf, 0, sizeof(buf));
    GetPrivateProfileStringA(section, key, defVal, buf, sizeof(buf), file);
    str_set(out, buf);
}

int IniGetInt(const char* file, const char* section, const char* key, int defVal) {
    return GetPrivateProfileIntA(section, key, defVal, file);
}

void IniWriteString(const char* file, const char* section, const char* key, const char* val) {
    WritePrivateProfileStringA(section, key, val, file);
}

void IniWriteInt(const char* file, const char* section, const char* key, int val) {
    char buf[32];
    sprintf(buf, "%d", val);
    WritePrivateProfileStringA(section, key, buf, file);
}

void SaveConfigToIni(HWND hDlg) {
    SSHConfig cfg;
    Str path;
    Str extra;
    const char* sec = "Session";
    size_t i;

    ReadConfigFromUI(hDlg, &cfg);
    path = GetConfigFilePath();

    IniWriteString(StrCStr(&path), sec, "Host",       StrCStr(&cfg.host));
    IniWriteString(StrCStr(&path), sec, "Port",       StrCStr(&cfg.port));
    IniWriteString(StrCStr(&path), sec, "Username",   StrCStr(&cfg.username));
    IniWriteString(StrCStr(&path), sec, "Timeout",    StrCStr(&cfg.timeout));

    str_init(&extra);
    str_set(&extra, StrCStr(&cfg.extraArgs));
    for (i = 0; i < extra.len; i++) {
        if (extra.data[i] == '\n' || extra.data[i] == '\r') extra.data[i] = ' ';
    }
    IniWriteString(StrCStr(&path), sec, "ExtraArgs", StrCStr(&extra));
    str_free(&extra);

    IniWriteString(StrCStr(&path), sec, "KeyFile",    StrCStr(&cfg.keyFile));
    IniWriteInt   (StrCStr(&path), sec, "AuthType",   cfg.authType);
    IniWriteInt   (StrCStr(&path), sec, "Compress",   cfg.compress   ? 1 : 0);
    IniWriteInt   (StrCStr(&path), sec, "X11Forward", cfg.x11Forward ? 1 : 0);
    IniWriteInt   (StrCStr(&path), sec, "Verbose",    cfg.verbose    ? 1 : 0);
    IniWriteString(StrCStr(&path), sec, "Encryption", StrCStr(&cfg.encryption));
    IniWriteString(StrCStr(&path), sec, "LocalFwd",   StrCStr(&cfg.localFwd));
    IniWriteString(StrCStr(&path), sec, "RemoteFwd",  StrCStr(&cfg.remoteFwd));
    IniWriteString(StrCStr(&path), sec, "DynamicFwd", StrCStr(&cfg.dynamicFwd));
    IniWriteInt   (StrCStr(&path), sec, "KeepAlive",  cfg.keepAlive  ? 1 : 0);
    IniWriteString(StrCStr(&path), sec, "ProxyJump",  StrCStr(&cfg.proxyJump));

    str_free(&path);
    sshcfg_free(&cfg);
}

void LoadConfigFromIni(HWND hDlg) {
    Str path;
    const char* sec = "Session";
    SSHConfig cfg;

    path = GetConfigFilePath();
    if (GetFileAttributesA(StrCStr(&path)) == INVALID_FILE_ATTRIBUTES) {
        str_free(&path);
        return;
    }

    sshcfg_init(&cfg);
    IniGetString(StrCStr(&path), sec, "Host",       "",  &cfg.host);
    IniGetString(StrCStr(&path), sec, "Port",       "22", &cfg.port);
    IniGetString(StrCStr(&path), sec, "Username",   "",   &cfg.username);
    IniGetString(StrCStr(&path), sec, "Timeout",    "10", &cfg.timeout);
    IniGetString(StrCStr(&path), sec, "ExtraArgs",  "",   &cfg.extraArgs);
    IniGetString(StrCStr(&path), sec, "KeyFile",    "",   &cfg.keyFile);
    cfg.authType   = IniGetInt(StrCStr(&path), sec, "AuthType", 0);
    cfg.compress   = IniGetInt(StrCStr(&path), sec, "Compress", 0)   != 0;
    cfg.x11Forward = IniGetInt(StrCStr(&path), sec, "X11Forward", 0) != 0;
    cfg.verbose    = IniGetInt(StrCStr(&path), sec, "Verbose", 0)    != 0;
    IniGetString(StrCStr(&path), sec, "Encryption", "", &cfg.encryption);
    IniGetString(StrCStr(&path), sec, "LocalFwd",   "", &cfg.localFwd);
    IniGetString(StrCStr(&path), sec, "RemoteFwd",  "", &cfg.remoteFwd);
    IniGetString(StrCStr(&path), sec, "DynamicFwd", "", &cfg.dynamicFwd);
    cfg.keepAlive  = IniGetInt(StrCStr(&path), sec, "KeepAlive", 0)  != 0;
    IniGetString(StrCStr(&path), sec, "ProxyJump",  "", &cfg.proxyJump);

    WriteConfigToUI(hDlg, &cfg);

    sshcfg_free(&cfg);
    str_free(&path);
}

/* ==========================================================================
 * 十四、对话框过程
 * ========================================================================== */

#define ADD_TOOLTIP_SW(hCtrl, text) \
        do { \
            TOOLINFOA ti; \
            memset(&ti, 0, sizeof(ti)); \
            ti.cbSize = sizeof(TOOLINFOA); \
            ti.hwnd = hDlg; \
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND; \
            ti.uId = (UINT_PTR)(hCtrl); \
            ti.lpszText = (LPSTR)(text); \
            SendMessageA(hTip, TTM_ADDTOOL, 0, (LPARAM)&ti); \
        } while(0)

#define ADD_TOOLTIP(hCtrl, text) \
        do { \
            TOOLINFOA ti; \
            memset(&ti, 0, sizeof(ti)); \
            ti.cbSize = sizeof(TOOLINFOA); \
            ti.hwnd = hDlg; \
            ti.uFlags = TTF_SUBCLASS | TTF_IDISHWND; \
            ti.uId = (UINT_PTR)(hCtrl); \
            ti.lpszText = (LPSTR)(text); \
            SendMessageA(hMainTooltip, TTM_ADDTOOL, 0, (LPARAM)&ti); \
        } while(0)

/* 往下拉框加一项：显示文本走 PathToDisplay()（可能是相对路径），
 * 真实绝对路径存进 g_sshComboReal，itemdata 存它在列表里的下标。
 *
 * labelOverride 非空时，显示文本直接用它、不再走 PathToDisplay()。
 * 「环境变量」那一项需要它：真实取值是裸的 "ssh"（交给 CreateProcess
 * 时由 PATH 去解析），但显示给人看的是 "ssh (环境变量)"。这两者不是
 * 同一个字符串的两种写法，是「执行用名」和「显示用名」两回事，所以不能
 * 靠解析显示文本还原 —— 只能显式带两个值进来。
 *
 * 顺序是先 CB_ADDSTRING、成功之后才 sl_push —— 反过来的话添加失败时
 * g_sshComboReal 会比下拉框多一项，后面所有下标整体错位。 */
static void ComboAddSSHItemEx(HWND hCombo, const char* realPath,
                              bool isCustom, const char* labelOverride) {
    Str disp;
    Str text;
    int idx;

    if (!hCombo || !realPath) return;

    str_init(&disp);
    if (labelOverride && *labelOverride)      str_set(&disp, labelOverride);
    else if (IsEnvSSHPath(realPath))          str_set(&disp, GX_SSH_ENV_LABEL);
    else                                     PathToDisplay(realPath, &disp);

    str_init(&text);
    if (isCustom) str_appendf(&text, "[自定义] %s", StrCStr(&disp));
    else          str_append(&text, StrCStr(&disp));

    idx = (int)SendMessageA(hCombo, CB_ADDSTRING, 0, (LPARAM)StrCStr(&text));
    str_free(&text);
    str_free(&disp);
    if (idx < 0) return;                 /* CB_ERR / CB_ERRSPACE：放弃这一项 */

    sl_push(&g_sshComboReal, realPath);
    SendMessageA(hCombo, CB_SETITEMDATA, (WPARAM)idx,
                 (LPARAM)(g_sshComboReal.count - 1));
}

static void ComboAddSSHItem(HWND hCombo, const char* realPath, bool isCustom) {
    ComboAddSSHItemEx(hCombo, realPath, isCustom, NULL);
}

/* 取第 idx 项的真实绝对路径。越界 / 没设置过 itemdata 都返回 NULL。 */
const char* ComboRealPath(HWND hCombo, int idx) {
    LPARAM d;
    size_t k;

    if (!hCombo || idx < 0) return NULL;
    d = SendMessageA(hCombo, CB_GETITEMDATA, (WPARAM)idx, 0);
    k = (size_t)d;                        /* CB_ERR(-1) 转出来是巨大值，被下面的判断挡掉 */
    if (k >= g_sshComboReal.count) return NULL;
    return g_sshComboReal.items[k];
}

/* 在下拉框里找真实路径等于 realPath 的项，返回下标，没有则 -1。 */
int ComboFindRealPath(HWND hCombo, const char* realPath) {
    int count, i;

    if (!hCombo || !realPath) return -1;
    count = (int)SendMessageA(hCombo, CB_GETCOUNT, 0, 0);
    for (i = 0; i < count; i++) {
        const char* r = ComboRealPath(hCombo, i);
        if (r && PathEqCI(r, realPath)) return i;
    }
    return -1;
}

void PopulateSSHCombo(HWND hCombo, HWND hOkBtn) {
    StrList customPaths;
    StrList candidates;
    StrList filteredCandidates;
    size_t i, j;

    SendMessage(hCombo, CB_RESETCONTENT, 0, 0);
    sl_free(&g_sshComboReal);
    sl_init(&g_sshComboReal);

    LoadCustomSSHPaths(&customPaths);
    CollectAllSSHCandidates(&candidates);
    sl_init(&filteredCandidates);

    for (i = 0; i < candidates.count; i++) {
        bool isCustom = false;
        for (j = 0; j < customPaths.count; j++) {
            if (StrEqCI(customPaths.items[j], candidates.items[i])) {
                isCustom = true;
                break;
            }
        }
        if (!isCustom) sl_push(&filteredCandidates, candidates.items[i]);
    }

    /* 自定义项在前，其余候选在后；两批都只传绝对路径进去，
     * 显示什么由 ComboAddSSHItem 自己决定。 */
    for (i = 0; i < customPaths.count; i++) {
        ComboAddSSHItem(hCombo, customPaths.items[i], true);
    }

    for (i = 0; i < filteredCandidates.count; i++) {
        ComboAddSSHItem(hCombo, filteredCandidates.items[i], false);
    }

    if (customPaths.count == 0 && filteredCandidates.count == 0) {
        EnableWindow(hOkBtn, FALSE);
    } else {
        SendMessage(hCombo, CB_SETCURSEL, 0, 0);
    }

    sl_free(&customPaths);
    sl_free(&candidates);
    sl_free(&filteredCandidates);
}

INT_PTR CALLBACK SelectSSHDialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);

    switch (msg) {
    case WM_INITDIALOG: {
        HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_SSH_PATH);
        HWND hOkBtn = GetDlgItem(hDlg, IDOK);
        Str savedPath;
        HWND hTip;

        gxDpiRestyle(hDlg);

        PopulateSSHCombo(hCombo, hOkBtn);

        str_init(&savedPath);
        if (LoadSSHPathConfig(&savedPath)) {
            /* 按真实路径匹配（分隔符归一 + 大小写不敏感）。
             * 原来拿显示文本和保存的绝对路径比，还有一句 strstr 兜底 ——
             * 显示文本改成相对路径后，绝对路径不再包含它，匹配必然失败，
             * 于是每次打开都落在第一项上，上次的选择"记不住"。 */
            int idx = ComboFindRealPath(hCombo, StrCStr(&savedPath));
            if (idx >= 0) SendMessage(hCombo, CB_SETCURSEL, (WPARAM)idx, 0);
            else          SendMessage(hCombo, CB_SETCURSEL, 0, 0);
            CheckDlgButton(hDlg, IDC_CHK_REMEMBER_SSH, BST_CHECKED);
        } else {
            CheckDlgButton(hDlg, IDC_CHK_REMEMBER_SSH, BST_UNCHECKED);
        }
        str_free(&savedPath);

        /* ===== 创建工具提示控件（普通矩形样式，非气球）===== */
        hTip = CreateWindowEx(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSA, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            hDlg, NULL, g_hInstance, NULL
        );
        if (hTip) {
            SetWindowPos(hTip, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            ADD_TOOLTIP_SW(GetDlgItem(hDlg, IDOK),                 "确认选择的 SSH 路径并开始连接");
            ADD_TOOLTIP_SW(GetDlgItem(hDlg, IDCANCEL),             "关闭窗口且不更改 SSH 路径");
            ADD_TOOLTIP_SW(GetDlgItem(hDlg, IDC_CHK_REMEMBER_SSH), "勾选后 SSH 路径将以明文保存在 SSHPath.ini");
            ADD_TOOLTIP_SW(GetDlgItem(hDlg, IDC_COMBO_SSH_PATH),   "选择要使用的 OpenSSH 可执行文件");
        }

        return TRUE;
    }

    case WM_COMMAND: {
        int wmId = LOWORD(wParam);

        if (wmId == IDC_BTN_ADD_CUSTOM) {
            OnAddCustomSSH(hDlg);
            return TRUE;
        } else if (wmId == IDC_BTN_DEL_CUSTOM) {
            OnDeleteCustomSSH(hDlg);
            return TRUE;
        }

        if (wmId == IDOK) {
            HWND hCombo = GetDlgItem(hDlg, IDC_COMBO_SSH_PATH);
            int sel = (int)SendMessage(hCombo, CB_GETCURSEL, 0, 0);
            bool valid = false;

            if (sel == CB_ERR) {
                MessageBoxA(hDlg, "请选择一个 SSH 路径！", "提示", MB_OK | MB_ICONWARNING);
                return TRUE;
            }

            /* 生效的永远是真实绝对路径，不从显示文本反推。
             * 原来这里是"看文本猜路径"：含"环境变量"→ssh、"[自定义] "前缀
             * →+9、".\\"→拼程序目录。显示文本一旦改成相对路径，".\\"这条
             * 分支就会在"程序目录下"和"程序目录外"两种情况间产生歧义，
             * 而且任何一条分支改文案都会静默失效。现在改成直接查表。 */
            {
                const char* realPath = ComboRealPath(hCombo, sel);
                if (!realPath) {
                    MessageBoxA(hDlg, "无法识别所选项目，请重新选择！", "错误", MB_OK | MB_ICONERROR);
                    return TRUE;
                }
                str_set(&g_userSelectedSSHPath, realPath);
            }

            /* 验证路径有效性 */
            if (IsEnvSSHPath(StrCStr(&g_userSelectedSSHPath))) {
                valid = IsSSHAvailable();
            } else {
                valid = IsFileExist(StrCStr(&g_userSelectedSSHPath));
            }
            if (!valid) {
                MessageBoxA(hDlg, "所选路径不可用，请重新选择！", "错误", MB_OK | MB_ICONERROR);
                return TRUE;
            }

            /* 检查"记住"复选框状态 */
            if (IsDlgButtonChecked(hDlg, IDC_CHK_REMEMBER_SSH) == BST_CHECKED) {
                SaveSSHPathConfig(StrCStr(&g_userSelectedSSHPath), true);
            } else {
                SaveSSHPathConfig("", false);   /* 取消记住：删除保存的配置 */
            }

            EndDialog(hDlg, IDOK);
        } else if (wmId == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        return TRUE;
    }
    }
    return FALSE;
}

HBITMAP HIconToMenuBitmap(HICON hIcon, int size) {
    HDC hdcScreen;
    HDC hdcMem;
    BITMAPINFO bmi;
    void* pBits = NULL;
    HBITMAP hbmp;
    HGDIOBJ hOld;

    if (!hIcon) return NULL;
    if (size <= 0) size = GetSystemMetrics(SM_CXSMICON);

    hdcScreen = GetDC(NULL);
    hdcMem = CreateCompatibleDC(hdcScreen);

    memset(&bmi, 0, sizeof(bmi));
    bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bmi.bmiHeader.biWidth = size;
    bmi.bmiHeader.biHeight = size;
    bmi.bmiHeader.biPlanes = 1;
    bmi.bmiHeader.biBitCount = 32;
    bmi.bmiHeader.biCompression = BI_RGB;

    hbmp = CreateDIBSection(hdcMem, &bmi, DIB_RGB_COLORS, &pBits, NULL, 0);
    if (!hbmp) {
        DeleteDC(hdcMem);
        ReleaseDC(NULL, hdcScreen);
        return NULL;
    }

    hOld = SelectObject(hdcMem, hbmp);
    DrawIconEx(hdcMem, 0, 0, hIcon, size, size, 0, NULL, DI_NORMAL);
    SelectObject(hdcMem, hOld);

    DeleteDC(hdcMem);
    ReleaseDC(NULL, hdcScreen);
    return hbmp;
}

static void SetMenuIcon(HMENU hMenu, UINT id, HICON hIcon) {
    /* 菜单位图要和系统菜单图标一个尺寸（SM_CXSMICON），否则在菜单里要么
     * 被缩、要么被放大糊掉。写死 16 只在 100% 下对。 */
    HBITMAP hBmp = HIconToMenuBitmap(hIcon, GetSystemMetrics(SM_CXSMICON));
    if (hBmp) {
        SetMenuItemBitmaps(hMenu, id, MF_BYCOMMAND, hBmp, hBmp);
        PushMenuBitmap(hBmp);
    }
}

INT_PTR CALLBACK DialogProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM lParam) {
    UNREFERENCED_PARAMETER(lParam);

    switch (msg) {
    case WM_INITDIALOG: {
        HMENU hMenu;
        HWND hMainTooltip;

        gxDpiRestyle(hDlg);
        InitControls(hDlg);
        LoadConfigFromIni(hDlg);

        hMenu = GetMenu(hDlg);
        if (hMenu) {
            HMENU hFileMenu = GetSubMenu(hMenu, 0);
            if (hFileMenu) {
                SetMenuIcon(hFileMenu, IDM_FILE_TOGGLE,     LoadIcon(NULL, IDI_APPLICATION));
                SetMenuIcon(hFileMenu, IDM_FILE_EXPORT_BAT, LoadIcon(NULL, MAKEINTRESOURCEA(32518)));
                SetMenuIcon(hFileMenu, IDM_FILE_EXIT,       LoadIcon(NULL, IDI_ERROR));
            }
            {
                HMENU hToolsMenu = GetSubMenu(hMenu, 1);
                if (hToolsMenu) {
                    SetMenuIcon(hToolsMenu, IDM_TOOLS_PING,     LoadIcon(NULL, IDI_APPLICATION));
                    SetMenuIcon(hToolsMenu, IDM_TOOLS_SSH_TEST, LoadIcon(NULL, IDI_WARNING));
                }
            }
            {
                HMENU hHelpMenu = GetSubMenu(hMenu, 2);
                if (hHelpMenu) {
                    SetMenuIcon(hHelpMenu, IDM_HELP_GITHUB, LoadIcon(NULL, IDI_QUESTION));
                    SetMenuIcon(hHelpMenu, IDM_HELP_SSH,    LoadIcon(NULL, IDI_QUESTION));
                    SetMenuIcon(hHelpMenu, IDM_HELP_ABOUT,  LoadIcon(NULL, IDI_INFORMATION));
                }
            }
        }

        /* ===== 为主窗口按钮创建工具提示（矩形样式，非气球）===== */
        hMainTooltip = CreateWindowEx(
            WS_EX_TOPMOST,
            TOOLTIPS_CLASSA, NULL,
            WS_POPUP | TTS_NOPREFIX | TTS_ALWAYSTIP,
            CW_USEDEFAULT, CW_USEDEFAULT,
            CW_USEDEFAULT, CW_USEDEFAULT,
            hDlg, NULL, g_hInstance, NULL
        );
        if (hMainTooltip) {
            SetWindowPos(hMainTooltip, HWND_TOPMOST, 0, 0, 0, 0,
                         SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);

            /* ==================== 连接设置分组 ==================== */
            ADD_TOOLTIP(g_ctrl.hEditHost, "输入主机名或 IP 地址");
            ADD_TOOLTIP(g_ctrl.hEditPort, "端口号（默认 22）");
            ADD_TOOLTIP(g_ctrl.hEditUser, "登录用户名");

            /* ==================== 认证方式 ==================== */
            ADD_TOOLTIP(g_ctrl.hRadioPassword, "使用密码进行身份验证");
            ADD_TOOLTIP(g_ctrl.hRadioKey,      "使用私钥文件进行身份验证");
            ADD_TOOLTIP(g_ctrl.hEditKeyFile,   "私钥文件路径（.pem / .ppk）");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_BTN_BROWSE_KEY), "浏览选择私钥文件");

            /* ==================== 超时与加密 ==================== */
            ADD_TOOLTIP(g_ctrl.hEditTimeout, "连接超时时间（秒）");
            ADD_TOOLTIP(g_ctrl.hComboEnc,    "选择加密算法（默认自动协商）");

            /* ==================== 高级选项 ==================== */
            ADD_TOOLTIP(g_ctrl.hChkCompress,   "启用压缩传输（-C）");
            ADD_TOOLTIP(g_ctrl.hChkX11Forward, "启用 X11 转发（-X）");
            ADD_TOOLTIP(g_ctrl.hChkVerbose,    "输出详细调试信息（-v）");
            ADD_TOOLTIP(g_ctrl.hEditExtraArgs, "附加 SSH 命令行参数");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_EDIT_LOCAL_FWD),   "格式: 本地端口:目标主机:目标端口, 如 8080:localhost:80");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_EDIT_REMOTE_FWD),  "格式: 远程端口:目标主机:目标端口, 如 2222:localhost:22");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_EDIT_DYNAMIC_FWD), "输入本地 SOCKS 代理端口, 如 1080");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_EDIT_PROXY_JUMP),  "格式: user@跳板机IP:端口, 如 admin@192.168.1.1:22");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_CHK_KEEPALIVE),    "每 60 秒发送心跳包防止连接中断");

            /* ==================== 按钮 ==================== */
            ADD_TOOLTIP(g_ctrl.hBtnConnect, "使用当前配置连接 SSH 服务器");
            ADD_TOOLTIP(GetDlgItem(hDlg, IDC_BTN_CLEAR_LOG), "清空状态栏信息");
        }
        return TRUE;
    }

    case WM_COMMAND: {
        int ctrlId = LOWORD(wParam);
        int notify = HIWORD(wParam);

        if (notify == EN_CHANGE && g_hTooltip) {
            SendMessageA(g_hTooltip, TTM_TRACKACTIVATE, FALSE, 0);
        }

        switch (ctrlId) {
        case IDC_BTN_CONNECT:
            if (notify == BN_CLICKED) OnConnect(hDlg);
            break;

        case IDC_BTN_BROWSE_KEY:
            if (notify == BN_CLICKED) OnBrowseKeyFile();
            break;

        case IDC_BTN_CLEAR_LOG:
            if (notify == BN_CLICKED) LogStatus("就绪");
            break;

        case IDC_RADIO_PASSWORD:
        case IDC_RADIO_KEY: {
            int id = LOWORD(wParam);
            BOOL isKey = (id == IDC_RADIO_KEY);
            CheckRadioButton(hDlg, IDC_RADIO_PASSWORD, IDC_RADIO_KEY, id);
            EnableWindow(GetDlgItem(hDlg, IDC_EDIT_KEY_FILE), isKey);
            EnableWindow(GetDlgItem(hDlg, IDC_BTN_BROWSE_KEY), isKey);
            break;
        }

        case IDM_FILE_TOGGLE:
            if (notify == 0) {
                INT_PTR ret = DialogBoxA(g_hInstance, MAKEINTRESOURCEA(IDD_SELECT_SSH), hDlg, SelectSSHDialogProc);
                if (ret == IDOK) RefreshAfterSSHChange(hDlg);
            }
            break;

        case IDM_FILE_EXIT:
            DestroyWindow(hDlg);
            break;

        case IDM_FILE_EXPORT_BAT:
            if (notify == 0) ExportConfigToBatch(hDlg);
            break;

        case IDM_FILE_OPEN_SSH_DIR:
            if (notify == 0) OnOpenSshDir(hDlg);
            break;

        case IDM_TOOLS_PING: {
            Str host = GetEditText(g_ctrl.hEditHost);
            if (host.len == 0) {
                ShowError("请先输入主机地址 !", "错误");
                str_free(&host);
                break;
            }
            {
                Str cmd;
                str_init(&cmd);
                str_appendf(&cmd, "/c ping -n 4 %s", StrCStr(&host));
                ShellExecuteA(NULL, "open", "cmd.exe", StrCStr(&cmd), NULL, SW_SHOWNORMAL);
                str_free(&cmd);
            }
            str_free(&host);
            break;
        }

        case IDM_TOOLS_SSH_TEST:
            if (notify == 0) OnSSHConnectionTest(hDlg);
            break;

        case IDM_TOOLS_GEN_KEY:
            if (notify == 0) OnGenerateKeyPair(hDlg);
            break;

        case IDM_TOOLS_COPY_PUB:
            if (notify == 0) OnCopyPublicKey(hDlg);
            break;

        case IDM_TOOLS_ADD_KEY:
            if (notify == 0) OnAddHostKey(hDlg);
            break;

        case IDM_HELP_GITHUB:
            if (notify == 0) {
                ShellExecuteA(NULL, "open", "https://github.com/PowerShell/Win32-OpenSSH", NULL, NULL, SW_SHOWNORMAL);
            }
            break;

        case IDM_HELP_SSH:
            if (notify == 0) {
                const char* sshPath = GetSSHPath();
                if (sshPath[0] == '\0') {
                    ShowError("未选择 OpenSSH !", "错误");
                    break;
                }
                {
                    Str cmdLine;
                    str_init(&cmdLine);
                    str_appendf(&cmdLine, "cmd.exe /k \"%s\"", sshPath);
                    ShellExecuteA(NULL, "open", "cmd.exe", StrCStr(&cmdLine), NULL, SW_SHOWNORMAL);
                    str_free(&cmdLine);
                }
            }
            break;

        case IDM_HELP_ABOUT:
            ShowInfo(
                "SSH Terminal Launcher v1.2\n\n"
                "通过 PowerShell 调用系统 OpenSSH 客户端\n"
                "TWXH 2026\n\n"
                "确保 Windows 已安装 OpenSSH 客户端：（也可将 OpenSSH 目录放在程序同文件夹下）\n"
                "设置 → 应用 → 可选功能 → 添加功能\n"
                "→ OpenSSH 客户端",
                "关于"
            );
            break;
        }
        return TRUE;
    }

    case WM_CTLCOLORDLG:
        return (INT_PTR)g_hCustomBgBrush;

    case WM_TIMER:
        if (wParam == 1001 && g_hTooltip) {
            SendMessageA(g_hTooltip, TTM_TRACKACTIVATE, FALSE, 0);
            KillTimer(hDlg, 1001);
        }
        break;

    case WM_CTLCOLORSTATIC:
        SetBkMode((HDC)wParam, TRANSPARENT);
        return (INT_PTR)g_hCustomBgBrush;

    case WM_SIZE: {
        HWND hStatus = GetDlgItem(hDlg, IDC_STATUS_BAR);
        if (hStatus) SendMessage(hStatus, WM_SIZE, 0, 0);
        return TRUE;
    }

    case WM_CLOSE:
        DestroyWindow(hDlg);
        return TRUE;

    case WM_DESTROY: {
        size_t i;
        SaveConfigToIni(hDlg);
        for (i = 0; i < g_menuBitmapCount; i++) {
            if (g_menuBitmaps[i]) DeleteObject(g_menuBitmaps[i]);
        }
        free(g_menuBitmaps);
        g_menuBitmaps = NULL;
        g_menuBitmapCount = 0;
        g_menuBitmapCap = 0;

        if (g_hCustomBgBrush) {
            DeleteObject(g_hCustomBgBrush);
            g_hCustomBgBrush = NULL;
        }
        PostQuitMessage(0);
        return TRUE;
    }
    }

    return FALSE;
}

/* ==========================================================================
 * 十五、主函数 (ANSI 版使用 WinMain)
 * ========================================================================== */

/* 进程 DPI 感知（系统 DPI，Vista+）。
 *
 * 【这是整套适配的前提，也是最容易被漏掉的一步】
 * Windows 给 DPI 感知留了两条路，不用 manifest 也能走：
 *   1. manifest 里写 <dpiAware>          —— 声明式
 *   2. 运行时调 SetProcessDPIAware()     —— 命令式，本文件走这条
 *
 * easygl.h 用的就是第 2 条，见它的 gxInitScreenScale()（3772 行起）：
 * GetModuleHandleA("user32.dll") + GetProcAddress("SetProcessDPIAware")。
 * 这里逐字照搬同一个写法，连"取不到就算了"的态度都一样。
 *
 * 为什么不直接调而要用 GetProcAddress 取：MinGW 4.9.2 自带的
 * libuser32.a / headers 不一定有这个导出的声明，链接期就会挂；动态取
 * 在老系统上取不到，保持不感知，不会连程序都起不来。
 *
 * 为什么必须自己加这一句（easygl 里却不用）：库里那次调用是
 * getinitscreenscale() 的副作用，而库内部并不主动调用它（它一度是死
 * 代码，现在暴露给宿主），EasyX 程序是在 initgraph 那条流程里间接触发
 * 的。本程序是纯 Win32 对话框，不走 initgraph，也没有任何东西会去调
 * getinitscreenscale()，所以这条后门不存在 —— 不显式声明，getdpi()
 * 就永远返回 96，gxS() 全部退化成 1:1，上面所有缩放白做。
 *
 * 不声明会怎样（不会崩，但白做）：
 *   - 进程被当 96 dpi，GetDeviceCaps 恒返回 96，gxS() 全是原值；
 *   - 系统对整个窗口做位图拉伸（发虚），所以"看起来"仍然是大个儿的，
 *     但那不是真缩放，字是糊的。这种静默失败最难查。
 * 声明之后：GetDeviceCaps 返回真实 120/144/192，gxDpiRestyle() 按字体
 * 基数把窗口和控件放大到实像素，字号跟着走，字是清晰的。
 *
 * 时序：必须在任何窗口创建 / 任何 DPI 相关 HDC 获取之前调用，否则系统
 * 已经把进程的虚拟化模式定死了，再调无效。所以放在 WinMain 最前面。
 * 如果 manifest 里已经声明了 dpiAware，则 manifest 优先，这里会失败
 * 返回 FALSE —— 无害，二者效果一致。
 *
 * 不想感知就编译加 -DSSH_NO_DPI_AWARE。 */
#ifndef SSH_NO_DPI_AWARE
static void sshEnableDpiAwareness(void) {
    typedef BOOL (WINAPI *PFN_SPDA)(void);
    HMODULE hUser;
    PFN_SPDA pSetDPIAware;

    hUser = GetModuleHandleA("user32.dll");
    if (!hUser) return;

    pSetDPIAware = (PFN_SPDA)GetProcAddress(hUser, "SetProcessDPIAware");
    if (pSetDPIAware) pSetDPIAware();   /* Vista+；取不到/失败都无妨 */
}
#endif

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    INITCOMMONCONTROLSEX icc;
    Str rememberedPath;
    bool hasRemembered;
    HWND hDlg;
    MSG msg;
    BOOL ret;

    UNREFERENCED_PARAMETER(hPrevInstance);
    UNREFERENCED_PARAMETER(lpCmdLine);

#ifndef SSH_NO_DPI_AWARE
    sshEnableDpiAwareness();
#endif

    g_hInstance = hInstance;

    icc.dwSize = sizeof(icc);
    icc.dwICC  = 0x00004000 | ICC_WIN95_CLASSES;  /* 0x00004000 = ICC_STANDARD_CLASSES */
    InitCommonControlsEx(&icc);

    str_init(&rememberedPath);
    hasRemembered = LoadSSHPathConfig(&rememberedPath);

    if (hasRemembered && rememberedPath.len > 0) {
        bool valid = false;
        if (IsEnvSSHPath(StrCStr(&rememberedPath))) {
            valid = IsSSHAvailable();
        } else {
            valid = IsFileExist(StrCStr(&rememberedPath));
        }
        if (valid) {
            str_set(&g_userSelectedSSHPath, StrCStr(&rememberedPath));
        } else {
            SaveSSHPathConfig("", false);
            str_clear(&g_userSelectedSSHPath);
        }
    }
    str_free(&rememberedPath);

    if (g_userSelectedSSHPath.len == 0) {
        INT_PTR r = DialogBoxA(hInstance, MAKEINTRESOURCEA(IDD_SELECT_SSH), NULL, SelectSSHDialogProc);
        if (r != IDOK) return 0;
    }

    hDlg = CreateDialogA(
        hInstance,
        MAKEINTRESOURCEA(IDD_MAIN_DIALOG),
        NULL,
        DialogProc
    );

    if (!hDlg) {
        MessageBoxA(NULL, "无法创建主对话框！", "错误", MB_ICONERROR | MB_OK);
        return -1;
    }

    ShowWindow(hDlg, nCmdShow);
    UpdateWindow(hDlg);

    /* 消息循环 */
    while ((ret = GetMessageA(&msg, NULL, 0, 0)) != 0) {
        if (ret == -1) break;
        if (!IsDialogMessageA(hDlg, &msg)) {
            TranslateMessage(&msg);
            DispatchMessageA(&msg);
        }
    }

    return (int)msg.wParam;
}
