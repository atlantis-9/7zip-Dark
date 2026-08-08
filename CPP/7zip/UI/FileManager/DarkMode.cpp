// DarkMode.cpp — File Manager dark theme support

#include "StdAfx.h"

#include "DarkMode.h"
#include "RegistryUtils.h"
#include "App.h"

using namespace NWindows;

// Approximate Windows 11 dark palette
static const COLORREF kDark_Bk       = RGB(32, 32, 32);
static const COLORREF kDark_TextBk   = RGB(32, 32, 32);
static const COLORREF kDark_Text     = RGB(240, 240, 240);
static const COLORREF kDark_SelectBk = RGB(0, 90, 158);
static const COLORREF kDark_Deleted  = RGB(255, 128, 128);
static const COLORREF kDark_EditBk   = RGB(45, 45, 45);
static const COLORREF kDark_Header   = RGB(45, 45, 45);
static const COLORREF kDark_MenuBar  = RGB(32, 32, 32);
static const COLORREF kDark_MenuHot  = RGB(60, 60, 60);
static const COLORREF kDark_Border   = RGB(70, 70, 70);
static const COLORREF kDark_Status   = RGB(40, 40, 40);

static bool g_DarkMode = false;
static HBRUSH g_BkBrush = NULL;
static HBRUSH g_EditBrush = NULL;
static HBRUSH g_HeaderBrush = NULL;
static HBRUSH g_MenuBarBrush = NULL;
static HBRUSH g_MenuHotBrush = NULL;
static HBRUSH g_StatusBrush = NULL;

static void FreeBrushes()
{
  if (g_BkBrush) { DeleteObject(g_BkBrush); g_BkBrush = NULL; }
  if (g_EditBrush) { DeleteObject(g_EditBrush); g_EditBrush = NULL; }
  if (g_HeaderBrush) { DeleteObject(g_HeaderBrush); g_HeaderBrush = NULL; }
  if (g_MenuBarBrush) { DeleteObject(g_MenuBarBrush); g_MenuBarBrush = NULL; }
  if (g_MenuHotBrush) { DeleteObject(g_MenuHotBrush); g_MenuHotBrush = NULL; }
  if (g_StatusBrush) { DeleteObject(g_StatusBrush); g_StatusBrush = NULL; }
}

static void EnsureBrushes()
{
  FreeBrushes();
  if (g_DarkMode)
  {
    g_BkBrush = CreateSolidBrush(kDark_Bk);
    g_EditBrush = CreateSolidBrush(kDark_EditBk);
    g_HeaderBrush = CreateSolidBrush(kDark_Header);
    g_MenuBarBrush = CreateSolidBrush(kDark_MenuBar);
    g_MenuHotBrush = CreateSolidBrush(kDark_MenuHot);
    g_StatusBrush = CreateSolidBrush(kDark_Status);
  }
}

bool DarkMode_IsEnabled()
{
  return g_DarkMode;
}

COLORREF DarkMode_GetBkColor() { return g_DarkMode ? kDark_Bk : GetSysColor(COLOR_WINDOW); }
COLORREF DarkMode_GetTextColor() { return g_DarkMode ? kDark_Text : GetSysColor(COLOR_WINDOWTEXT); }
COLORREF DarkMode_GetTextBkColor() { return g_DarkMode ? kDark_TextBk : GetSysColor(COLOR_WINDOW); }
COLORREF DarkMode_GetHotTextBkColor() { return g_DarkMode ? kDark_SelectBk : RGB(255, 192, 192); }
COLORREF DarkMode_GetDeletedTextColor() { return g_DarkMode ? kDark_Deleted : RGB(255, 0, 0); }

HBRUSH DarkMode_GetBkBrush()
{
  if (g_DarkMode && g_BkBrush)
    return g_BkBrush;
  return (HBRUSH)(COLOR_BTNFACE + 1);
}

#ifndef UNDER_CE

typedef BOOL (WINAPI *Func_AllowDarkModeForWindow)(HWND, BOOL);
typedef BOOL (WINAPI *Func_AllowDarkModeForApp)(BOOL);
enum PreferredAppMode
{
  kAppMode_Default = 0,
  kAppMode_AllowDark = 1,
  kAppMode_ForceDark = 2,
  kAppMode_ForceLight = 3
};
typedef PreferredAppMode (WINAPI *Func_SetPreferredAppMode)(PreferredAppMode);
typedef void (WINAPI *Func_FlushMenuThemes)();
typedef HRESULT (WINAPI *Func_SetWindowTheme)(HWND, LPCWSTR, LPCWSTR);
typedef HRESULT (WINAPI *Func_DwmSetWindowAttribute)(HWND, DWORD, LPCVOID, DWORD);

#ifndef DWMWA_USE_IMMERSIVE_DARK_MODE
#define DWMWA_USE_IMMERSIVE_DARK_MODE 20
#endif
/* Windows 11+ caption theming (match main window chrome). */
#ifndef DWMWA_BORDER_COLOR
#define DWMWA_BORDER_COLOR 34
#endif
#ifndef DWMWA_CAPTION_COLOR
#define DWMWA_CAPTION_COLOR 35
#endif
#ifndef DWMWA_TEXT_COLOR
#define DWMWA_TEXT_COLOR 36
#endif
#ifndef DWMWA_COLOR_DEFAULT
#define DWMWA_COLOR_DEFAULT 0xFFFFFFFF
#endif
#ifndef DWMWA_COLOR_NONE
#define DWMWA_COLOR_NONE 0xFFFFFFFE
#endif
#ifndef WM_DWMCOMPOSITIONCHANGED
#define WM_DWMCOMPOSITIONCHANGED 0x031E
#endif
#ifndef WM_DWMCOLORIZATIONCOLORCHANGED
#define WM_DWMCOLORIZATIONCOLORCHANGED 0x0320
#endif

// Undocumented UAH menu-bar draw messages (Windows 10+)
#ifndef WM_UAHDRAWMENU
#define WM_UAHDRAWMENU 0x0091
#endif
#ifndef WM_UAHDRAWMENUITEM
#define WM_UAHDRAWMENUITEM 0x0092
#endif
#ifndef WM_UAHMEASUREMENUITEM
#define WM_UAHMEASUREMENUITEM 0x0094
#endif

typedef struct tagUAHMENUITEMMETRICS
{
  union
  {
    struct { DWORD cx; DWORD cy; } rgsizeBar[2];
    struct { DWORD cx; DWORD cy; } rgsizePopup[4];
  };
} UAHMENUITEMMETRICS;

typedef struct tagUAHMENUPOPUPMETRICS
{
  DWORD rgcx[4];
  DWORD fUpdateMaxWidths: 2;
} UAHMENUPOPUPMETRICS;

typedef struct tagUAHMENU
{
  HMENU hmenu;
  HDC hdc;
  DWORD dwFlags;
} UAHMENU;

typedef struct tagUAHMENUITEM
{
  int iPosition;
  UAHMENUITEMMETRICS umim;
  UAHMENUPOPUPMETRICS umpm;
} UAHMENUITEM;

typedef struct tagUAHDRAWMENUITEM
{
  DRAWITEMSTRUCT dis;
  UAHMENU um;
  UAHMENUITEM umi;
} UAHDRAWMENUITEM;

static HMODULE g_hUxTheme = NULL;
static HMODULE g_hDwmapi = NULL;
static Func_AllowDarkModeForWindow g_AllowDarkModeForWindow = NULL;
static Func_SetPreferredAppMode g_SetPreferredAppMode = NULL;
static Func_AllowDarkModeForApp g_AllowDarkModeForApp = NULL;
static Func_FlushMenuThemes g_FlushMenuThemes = NULL;
static Func_SetWindowTheme g_SetWindowTheme = NULL;
static Func_DwmSetWindowAttribute g_DwmSetWindowAttribute = NULL;
static bool g_ApisLoaded = false;

Z7_DIAGNOSTIC_IGNORE_CAST_FUNCTION

static void LoadDarkApis()
{
  if (g_ApisLoaded)
    return;
  g_ApisLoaded = true;

  g_hUxTheme = LoadLibraryW(L"uxtheme.dll");
  if (g_hUxTheme)
  {
    g_SetWindowTheme = Z7_GET_PROC_ADDRESS(
        Func_SetWindowTheme, g_hUxTheme, "SetWindowTheme");
    g_AllowDarkModeForWindow = Z7_GET_PROC_ADDRESS(
        Func_AllowDarkModeForWindow, g_hUxTheme, MAKEINTRESOURCEA(133));
    g_SetPreferredAppMode = Z7_GET_PROC_ADDRESS(
        Func_SetPreferredAppMode, g_hUxTheme, MAKEINTRESOURCEA(135));
    if (!g_SetPreferredAppMode)
      g_AllowDarkModeForApp = Z7_GET_PROC_ADDRESS(
          Func_AllowDarkModeForApp, g_hUxTheme, MAKEINTRESOURCEA(135));
    g_FlushMenuThemes = Z7_GET_PROC_ADDRESS(
        Func_FlushMenuThemes, g_hUxTheme, MAKEINTRESOURCEA(136));
  }

  g_hDwmapi = LoadLibraryW(L"dwmapi.dll");
  if (g_hDwmapi)
    g_DwmSetWindowAttribute = Z7_GET_PROC_ADDRESS(
        Func_DwmSetWindowAttribute, g_hDwmapi, "DwmSetWindowAttribute");
}

static void SetPreferredMode(bool dark)
{
  LoadDarkApis();
  if (g_SetPreferredAppMode)
    g_SetPreferredAppMode(dark ? kAppMode_ForceDark : kAppMode_ForceLight);
  else if (g_AllowDarkModeForApp)
    g_AllowDarkModeForApp(dark ? TRUE : FALSE);
  if (g_FlushMenuThemes)
    g_FlushMenuThemes();
}

static void ApplyImmersiveDarkTitleBar(HWND hwnd, bool dark)
{
  LoadDarkApis();
  if (!g_DwmSetWindowAttribute || !hwnd)
    return;

  BOOL value = dark ? TRUE : FALSE;
  /* Attribute 20 = Win10 1903+; 19 = earlier insider builds. */
  if (FAILED(g_DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value))))
    g_DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));

  /*
    Win11: paint caption / border with the same palette as the main app.
    Without this, some dialogs keep a light title bar even with immersive dark mode.
  */
  if (dark)
  {
    COLORREF caption = kDark_Bk;
    COLORREF text = kDark_Text;
    COLORREF border = kDark_Border;
    g_DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &caption, sizeof(caption));
    g_DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &text, sizeof(text));
    g_DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &border, sizeof(border));
  }
  else
  {
    COLORREF def = DWMWA_COLOR_DEFAULT;
    g_DwmSetWindowAttribute(hwnd, DWMWA_CAPTION_COLOR, &def, sizeof(def));
    g_DwmSetWindowAttribute(hwnd, DWMWA_TEXT_COLOR, &def, sizeof(def));
    g_DwmSetWindowAttribute(hwnd, DWMWA_BORDER_COLOR, &def, sizeof(def));
  }
}

/* One-shot frame rebuild after theme attributes change (do not call from NC messages). */
static void RefreshWindowFrame(HWND hwnd)
{
  if (!hwnd)
    return;
  SetWindowPos(hwnd, NULL, 0, 0, 0, 0,
      SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
  RedrawWindow(hwnd, NULL, NULL, RDW_FRAME | RDW_INVALIDATE | RDW_UPDATENOW);
}

static void AllowDarkForWindow(HWND hwnd, bool dark)
{
  LoadDarkApis();
  if (g_AllowDarkModeForWindow && hwnd)
    g_AllowDarkModeForWindow(hwnd, dark ? TRUE : FALSE);
}

static void SetTheme(HWND hwnd, LPCWSTR name)
{
  LoadDarkApis();
  if (g_SetWindowTheme && hwnd)
  {
    /* Empty class + empty subId fully disables visual styles (needed for CTLCOLOR text). */
    if (name && name[0] == 0)
      g_SetWindowTheme(hwnd, L"", L"");
    else
      g_SetWindowTheme(hwnd, name, NULL);
  }
}

/* Radio / checkbox / groupbox must not use visual styles or text stays system (dark) color. */
static bool IsOwnerTextButton(HWND hwnd)
{
  const LONG style = GetWindowLongW(hwnd, GWL_STYLE);
  const LONG type = style & BS_TYPEMASK;
  return type == BS_CHECKBOX
      || type == BS_AUTOCHECKBOX
      || type == BS_RADIOBUTTON
      || type == BS_AUTORADIOBUTTON
      || type == BS_3STATE
      || type == BS_AUTO3STATE
      || type == BS_GROUPBOX;
}

static void DarkMode_ApplyToButton(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
  {
    if (IsOwnerTextButton(hwnd))
    {
      /* Unthemed so WM_CTLCOLORBTN/STATIC supplies light text on dark fill. */
      SetTheme(hwnd, L"");
    }
    else
    {
      /* Push buttons: dark explorer theme. */
      SetTheme(hwnd, L"DarkMode_Explorer");
    }
  }
  else
    SetTheme(hwnd, L"Explorer");
#endif
  InvalidateRect(hwnd, NULL, TRUE);
}

static void DarkMode_ApplyToEdit(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
  {
    SetTheme(hwnd, L"DarkMode_CFD");
    /* Fallback if CFD unavailable: classic + CTLCOLOREDIT */
    if (!g_SetWindowTheme)
      SetTheme(hwnd, L"");
  }
  else
    SetTheme(hwnd, L"Explorer");
#endif
  InvalidateRect(hwnd, NULL, TRUE);
}

static void PaintTabControlDark(HWND hwnd)
{
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  if (!hdc)
    return;

  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  FillRect(hdc, &rcClient, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));

  /* Display area (page host) border */
  RECT rcDisplay = rcClient;
  TabCtrl_AdjustRect(hwnd, FALSE, &rcDisplay);
  /* Area above display = tab strip; fill already done. Draw display edge. */
  {
    HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
    Rectangle(hdc, rcDisplay.left - 1, rcDisplay.top - 1, rcDisplay.right + 1, rcDisplay.bottom + 1);
    SelectObject(hdc, oldBr);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
  }

  const int count = TabCtrl_GetItemCount(hwnd);
  const int sel = TabCtrl_GetCurSel(hwnd);
  for (int i = 0; i < count; i++)
  {
    RECT rc;
    if (!TabCtrl_GetItemRect(hwnd, i, &rc))
      continue;

    /* Overlap selected tab slightly lower into the display edge */
    if (i == sel)
      rc.bottom += 2;

    HBRUSH br = (i == sel)
        ? (g_EditBrush ? g_EditBrush : g_BkBrush)
        : (g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
    FillRect(hdc, &rc, br);

    HPEN pen = CreatePen(PS_SOLID, 1, (i == sel) ? kDark_SelectBk : kDark_Border);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
    LineTo(hdc, rc.left, rc.top);
    LineTo(hdc, rc.right - 1, rc.top);
    LineTo(hdc, rc.right - 1, rc.bottom);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);

    WCHAR text[256];
    text[0] = 0;
    TCITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = TCIF_TEXT;
    item.pszText = text;
    item.cchTextMax = 255;
    TabCtrl_GetItem(hwnd, i, &item);

    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kDark_Text);
    DrawTextW(hdc, text, -1, &rc,
        DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
  }

  EndPaint(hwnd, &ps);
}

static void PaintPushButtonDark(HWND hwnd)
{
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  if (!hdc)
    return;

  RECT rc;
  GetClientRect(hwnd, &rc);

  const UINT state = (UINT)SendMessageW(hwnd, BM_GETSTATE, 0, 0);
  const bool pressed = (state & BST_PUSHED) != 0;
  const bool hot = (state & BST_HOT) != 0;
  const bool disabled = !IsWindowEnabled(hwnd);
  const bool defBtn = (GetWindowLongW(hwnd, GWL_STYLE) & BS_TYPEMASK) == BS_DEFPUSHBUTTON;

  COLORREF face = pressed ? kDark_SelectBk : (hot ? kDark_MenuHot : kDark_EditBk);
  if (disabled)
    face = kDark_Bk;
  HBRUSH br = CreateSolidBrush(face);
  FillRect(hdc, &rc, br);
  DeleteObject(br);

  HPEN pen = CreatePen(PS_SOLID, defBtn ? 2 : 1, kDark_Border);
  HPEN oldPen = (HPEN)SelectObject(hdc, pen);
  HBRUSH oldBr = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
  Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
  SelectObject(hdc, oldBr);
  SelectObject(hdc, oldPen);
  DeleteObject(pen);

  WCHAR text[256];
  const int len = GetWindowTextW(hwnd, text, 256);
  SetBkMode(hdc, TRANSPARENT);
  SetTextColor(hdc, disabled ? RGB(140, 140, 140) : kDark_Text);
  DrawTextW(hdc, text, len, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

  if (GetFocus() == hwnd)
  {
    RECT rf = rc;
    InflateRect(&rf, -3, -3);
    DrawFocusRect(hdc, &rf);
  }

  EndPaint(hwnd, &ps);
}

/* ---- Subclass helpers: reliable paint / CTLCOLOR for stubborn controls ---- */

static const wchar_t kProp_OrigProc[] = L"7zDMproc";
static const wchar_t kProp_Kind[] = L"7zDMkind";

enum
{
  kDarkSub_StatusBar = 1,
  kDarkSub_Header = 2,
  kDarkSub_Combo = 3,     /* ComboBox (parent of edit): WM_CTLCOLOR* */
  kDarkSub_ComboEx = 4,
  kDarkSub_ToolBar = 5,   /* erase + force dark face */
  kDarkSub_ReBar = 6,     /* receives NM_CUSTOMDRAW from band toolbars */
  kDarkSub_PropSheet = 7, /* Options sheet: erase, CTLCOLOR, tab notify */
  kDarkSub_Tab = 8,
  kDarkSub_PushButton = 9 /* owner-paint OK/Cancel-style buttons */
};

static void PaintStatusBarDark(HWND hwnd)
{
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  if (!hdc)
    return;

  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  FillRect(hdc, &rcClient, g_StatusBrush ? g_StatusBrush : g_BkBrush);

  const int parts = (int)SendMessageW(hwnd, SB_GETPARTS, 0, 0);
  for (int i = 0; i < parts; i++)
  {
    RECT rc;
    if (!SendMessageW(hwnd, SB_GETRECT, (WPARAM)i, (LPARAM)&rc))
      continue;

    FillRect(hdc, &rc, g_StatusBrush ? g_StatusBrush : g_BkBrush);

    if (rc.left > 0)
    {
      HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
      HPEN oldPen = (HPEN)SelectObject(hdc, pen);
      MoveToEx(hdc, rc.left, rc.top + 2, NULL);
      LineTo(hdc, rc.left, rc.bottom - 2);
      SelectObject(hdc, oldPen);
      DeleteObject(pen);
    }

    WCHAR text[512];
    text[0] = 0;
    SendMessageW(hwnd, SB_GETTEXTW, (WPARAM)i, (LPARAM)text);

    RECT rcText = rc;
    rcText.left += 4;
    rcText.right -= 2;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kDark_Text);
    DrawTextW(hdc, text, -1, &rcText,
        DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);
  }

  EndPaint(hwnd, &ps);
}

static void PaintHeaderDark(HWND hwnd)
{
  PAINTSTRUCT ps;
  HDC hdc = BeginPaint(hwnd, &ps);
  if (!hdc)
    return;

  RECT rcClient;
  GetClientRect(hwnd, &rcClient);
  FillRect(hdc, &rcClient, g_HeaderBrush ? g_HeaderBrush : g_BkBrush);

  const int count = Header_GetItemCount(hwnd);
  int rightEdge = 0;
  for (int i = 0; i < count; i++)
  {
    RECT rc;
    if (!Header_GetItemRect(hwnd, i, &rc))
      continue;
    if (rc.right > rightEdge)
      rightEdge = rc.right;

    HBRUSH br = g_HeaderBrush ? g_HeaderBrush : g_BkBrush;
    FillRect(hdc, &rc, br);

    {
      HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
      HPEN oldPen = (HPEN)SelectObject(hdc, pen);
      MoveToEx(hdc, rc.right - 1, rc.top + 2, NULL);
      LineTo(hdc, rc.right - 1, rc.bottom - 2);
      MoveToEx(hdc, rc.left, rc.bottom - 1, NULL);
      LineTo(hdc, rc.right, rc.bottom - 1);
      SelectObject(hdc, oldPen);
      DeleteObject(pen);
    }

    WCHAR text[256];
    text[0] = 0;
    HDITEMW item;
    memset(&item, 0, sizeof(item));
    item.mask = HDI_TEXT | HDI_FORMAT;
    item.pszText = text;
    item.cchTextMax = 255;
    Header_GetItem(hwnd, i, &item);

    RECT rcText = rc;
    rcText.left += 6;
    rcText.right -= 6;
    SetBkMode(hdc, TRANSPARENT);
    SetTextColor(hdc, kDark_Text);

    UINT align = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
    if (item.fmt & HDF_CENTER)
      align |= DT_CENTER;
    else if (item.fmt & HDF_RIGHT)
      align |= DT_RIGHT;
    else
      align |= DT_LEFT;
    DrawTextW(hdc, text, -1, &rcText, align);

    if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN))
    {
      const bool up = (item.fmt & HDF_SORTUP) != 0;
      const int cx = 8, cy = 4;
      const int x = rc.right - 12;
      const int y = (rc.top + rc.bottom - cy) / 2;
      POINT pts[3];
      if (up)
      {
        pts[0].x = x; pts[0].y = y + cy;
        pts[1].x = x + cx; pts[1].y = y + cy;
        pts[2].x = x + cx / 2; pts[2].y = y;
      }
      else
      {
        pts[0].x = x; pts[0].y = y;
        pts[1].x = x + cx; pts[1].y = y;
        pts[2].x = x + cx / 2; pts[2].y = y + cy;
      }
      HBRUSH abr = CreateSolidBrush(kDark_Text);
      HPEN pen = CreatePen(PS_SOLID, 1, kDark_Text);
      HBRUSH oldBr = (HBRUSH)SelectObject(hdc, abr);
      HPEN oldPen = (HPEN)SelectObject(hdc, pen);
      Polygon(hdc, pts, 3);
      SelectObject(hdc, oldBr);
      SelectObject(hdc, oldPen);
      DeleteObject(abr);
      DeleteObject(pen);
    }
  }

  /* Remainder to the right of the last column (often left white by default). */
  if (rightEdge < rcClient.right)
  {
    RECT rcRest = rcClient;
    rcRest.left = rightEdge;
    FillRect(hdc, &rcRest, g_HeaderBrush ? g_HeaderBrush : g_BkBrush);
    HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
    HPEN oldPen = (HPEN)SelectObject(hdc, pen);
    MoveToEx(hdc, rcRest.left, rcRest.bottom - 1, NULL);
    LineTo(hdc, rcRest.right, rcRest.bottom - 1);
    SelectObject(hdc, oldPen);
    DeleteObject(pen);
  }

  EndPaint(hwnd, &ps);
}

static LRESULT CALLBACK DarkMode_SubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
  WNDPROC orig = (WNDPROC)GetPropW(hwnd, kProp_OrigProc);
  const INT_PTR kind = (INT_PTR)GetPropW(hwnd, kProp_Kind);

  if (g_DarkMode)
  {
    switch (kind)
    {
      case kDarkSub_StatusBar:
        if (message == WM_ERASEBKGND)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_StatusBrush ? g_StatusBrush : g_BkBrush);
          return 1;
        }
        if (message == WM_PAINT)
        {
          PaintStatusBarDark(hwnd);
          return 0;
        }
        break;

      case kDarkSub_Header:
        if (message == WM_ERASEBKGND)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_HeaderBrush ? g_HeaderBrush : g_BkBrush);
          return 1;
        }
        if (message == WM_PAINT)
        {
          PaintHeaderDark(hwnd);
          return 0;
        }
        break;

      case kDarkSub_Combo:
      case kDarkSub_ComboEx:
        if (message == WM_CTLCOLOREDIT ||
            message == WM_CTLCOLORLISTBOX ||
            message == WM_CTLCOLORSTATIC)
        {
          HDC hdc = (HDC)wParam;
          SetTextColor(hdc, kDark_Text);
          SetBkColor(hdc, kDark_EditBk);
          if (g_EditBrush)
            return (LRESULT)g_EditBrush;
        }
        if (message == WM_ERASEBKGND && kind == kDarkSub_ComboEx)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_EditBrush ? g_EditBrush : g_BkBrush);
          return 1;
        }
        break;

      case kDarkSub_ToolBar:
        if (message == WM_ERASEBKGND)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
          return 1;
        }
        break;

      case kDarkSub_ReBar:
        if (message == WM_ERASEBKGND)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
          return 1;
        }
        if (message == WM_NOTIFY)
        {
          LPNMHDR hdr = (LPNMHDR)lParam;
          if (hdr && hdr->code == (UINT)NM_CUSTOMDRAW)
          {
            WCHAR cls[64];
            cls[0] = 0;
            GetClassNameW(hdr->hwndFrom, cls, 64);
            if (lstrcmpiW(cls, L"ToolbarWindow32") == 0)
            {
              LRESULT res = 0;
              if (DarkMode_OnToolBarCustomDraw((LPNMTBCUSTOMDRAW)hdr, res))
                return res;
            }
          }
        }
        break;

      case kDarkSub_PropSheet:
        /* Re-assert DWM caption colors without FRAMECHANGED (avoids NC recursion). */
        if (message == WM_THEMECHANGED ||
            message == WM_DWMCOMPOSITIONCHANGED ||
            message == WM_DWMCOLORIZATIONCOLORCHANGED)
        {
          ApplyImmersiveDarkTitleBar(hwnd, true);
        }
        if (message == WM_ERASEBKGND)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
          return 1;
        }
        if (message == WM_CTLCOLORDLG ||
            message == WM_CTLCOLORSTATIC ||
            message == WM_CTLCOLORBTN ||
            message == WM_CTLCOLOREDIT ||
            message == WM_CTLCOLORLISTBOX ||
            message == WM_CTLCOLORSCROLLBAR)
        {
          LRESULT res = 0;
          if (DarkMode_OnCtlColor(message, wParam, lParam, res))
            return res;
        }
        if (message == WM_NOTIFY)
        {
          LPNMHDR hdr = (LPNMHDR)lParam;
          if (hdr && hdr->code == (UINT)NM_CUSTOMDRAW)
          {
            WCHAR cls[64];
            cls[0] = 0;
            GetClassNameW(hdr->hwndFrom, cls, 64);
            LRESULT res = 0;
            if (lstrcmpiW(cls, WC_TABCONTROLW) == 0 ||
                lstrcmpiW(cls, L"SysTabControl32") == 0)
            {
              if (DarkMode_OnTabCustomDraw((LPNMCUSTOMDRAW)hdr, res))
                return res;
            }
            else if (lstrcmpiW(cls, L"SysHeader32") == 0)
            {
              if (DarkMode_OnHeaderCustomDraw((LPNMCUSTOMDRAW)hdr, res))
                return res;
            }
            else if (lstrcmpiW(cls, L"ToolbarWindow32") == 0)
            {
              if (DarkMode_OnToolBarCustomDraw((LPNMTBCUSTOMDRAW)hdr, res))
                return res;
            }
          }
        }
        break;

      case kDarkSub_Tab:
        if (message == WM_ERASEBKGND)
        {
          RECT rc;
          GetClientRect(hwnd, &rc);
          FillRect((HDC)wParam, &rc, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
          return 1;
        }
        if (message == WM_PAINT)
        {
          PaintTabControlDark(hwnd);
          return 0;
        }
        break;

      case kDarkSub_PushButton:
        if (message == WM_ERASEBKGND)
          return 1;
        if (message == WM_PAINT)
        {
          PaintPushButtonDark(hwnd);
          return 0;
        }
        /* Keep hot/press visuals updating */
        if (message == WM_MOUSEMOVE || message == WM_MOUSELEAVE ||
            message == WM_LBUTTONDOWN || message == WM_LBUTTONUP ||
            message == WM_SETFOCUS || message == WM_KILLFOCUS ||
            message == BM_SETSTATE || message == WM_ENABLE ||
            message == WM_UPDATEUISTATE)
        {
          LRESULT r = orig ? CallWindowProcW(orig, hwnd, message, wParam, lParam)
                           : DefWindowProcW(hwnd, message, wParam, lParam);
          InvalidateRect(hwnd, NULL, FALSE);
          return r;
        }
        break;

      default:
        break;
    }
  }

  if (orig)
    return CallWindowProcW(orig, hwnd, message, wParam, lParam);
  return DefWindowProcW(hwnd, message, wParam, lParam);
}

static void DarkMode_Subclass(HWND hwnd, INT_PTR kind)
{
  if (!hwnd)
    return;
  if (GetPropW(hwnd, kProp_OrigProc))
  {
    /* Already subclassed — just update kind. */
    SetPropW(hwnd, kProp_Kind, (HANDLE)kind);
    return;
  }
  WNDPROC orig = (WNDPROC)SetWindowLongPtrW(hwnd, GWLP_WNDPROC, (LONG_PTR)DarkMode_SubclassProc);
  if (!orig)
    return;
  SetPropW(hwnd, kProp_OrigProc, (HANDLE)orig);
  SetPropW(hwnd, kProp_Kind, (HANDLE)kind);
}

static void DarkMode_ApplyToTab(HWND hwnd);

/* Paint residual white line under the menu bar in the non-client area. */
static void PaintMenuBarBottomLine(HWND hwnd)
{
  if (!g_DarkMode || !g_MenuBarBrush)
    return;
  MENUBARINFO mbi;
  memset(&mbi, 0, sizeof(mbi));
  mbi.cbSize = sizeof(mbi);
  if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
    return;

  RECT rcWindow;
  GetWindowRect(hwnd, &rcWindow);

  // Line just under the menu bar, in window coordinates then screen->client of NC
  RECT rcLine = mbi.rcBar;
  rcLine.top = rcLine.bottom;
  rcLine.bottom = rcLine.top + 1;

  // Convert to window-relative for GetWindowDC
  OffsetRect(&rcLine, -rcWindow.left, -rcWindow.top);

  HDC hdc = GetWindowDC(hwnd);
  if (hdc)
  {
    FillRect(hdc, &rcLine, g_MenuBarBrush);
    ReleaseDC(hwnd, hdc);
  }
}

#endif // !UNDER_CE

void DarkMode_SetEnabled(bool enabled)
{
  g_DarkMode = enabled;
  EnsureBrushes();
#ifndef UNDER_CE
  SetPreferredMode(enabled);
#endif
}

void DarkMode_LoadFromSettings()
{
  CFmSettings st;
  st.Load();
  DarkMode_SetEnabled(st.DarkMode);
}

void DarkMode_ApplyToWindow(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  ApplyImmersiveDarkTitleBar(hwnd, g_DarkMode);
  if (g_DarkMode)
    SetTheme(hwnd, L"DarkMode_Explorer");
  else
    SetTheme(hwnd, L"Explorer");
  RefreshWindowFrame(hwnd);
  // Force menu bar repaint after theme change
  DrawMenuBar(hwnd);
  if (g_DarkMode)
    PaintMenuBarBottomLine(hwnd);
#endif
}

void DarkMode_ApplyToListView(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  /* Keep Explorer theme on the list; colors come from ListView_Set* and custom draw.
     DarkMode_Explorer often leaves the header light on some Windows builds. */
  if (g_DarkMode)
    SetTheme(hwnd, L"Explorer");
  else
    SetTheme(hwnd, L"Explorer");
#endif
  ListView_SetBkColor(hwnd, DarkMode_GetBkColor());
  ListView_SetTextBkColor(hwnd, DarkMode_GetTextBkColor());
  ListView_SetTextColor(hwnd, DarkMode_GetTextColor());
#ifndef UNDER_CE
  HWND header = ListView_GetHeader(hwnd);
  if (header)
  {
    AllowDarkForWindow(header, g_DarkMode);
    /* Strip visual styles so subclass WM_PAINT / custom-draw fully control colors. */
    if (g_DarkMode)
    {
      SetTheme(header, L"");
      DarkMode_Subclass(header, kDarkSub_Header);
    }
    else
      SetTheme(header, L"ItemsView");
    InvalidateRect(header, NULL, TRUE);
  }
#endif
  InvalidateRect(hwnd, NULL, TRUE);
}

void DarkMode_ApplyToToolBar(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
  {
    SetTheme(hwnd, L"");
    DarkMode_Subclass(hwnd, kDarkSub_ToolBar);
  }
  else
    SetTheme(hwnd, L"Explorer");
#endif
  if (g_DarkMode)
  {
    COLORSCHEME cs;
    memset(&cs, 0, sizeof(cs));
    cs.dwSize = sizeof(cs);
    cs.clrBtnHighlight = kDark_EditBk;
    cs.clrBtnShadow = RGB(20, 20, 20);
    SendMessage(hwnd, TB_SETCOLORSCHEME, 0, (LPARAM)&cs);
  }
  InvalidateRect(hwnd, NULL, TRUE);
  UpdateWindow(hwnd);
}

#ifndef TBCDRF_USECDCOLORS
#define TBCDRF_USECDCOLORS 0x00800000
#endif
#ifndef TBCDRF_HILITEHOTTRACK
#define TBCDRF_HILITEHOTTRACK 0x00020000
#endif
#ifndef TBCDRF_NOBACKGROUND
#define TBCDRF_NOBACKGROUND 0x00080000
#endif

bool DarkMode_OnToolBarCustomDraw(LPNMTBCUSTOMDRAW tbcd, LRESULT &result)
{
  if (!g_DarkMode || !tbcd)
    return false;

  switch (tbcd->nmcd.dwDrawStage)
  {
    case CDDS_PREPAINT:
    {
      RECT rc;
      if (GetClientRect(tbcd->nmcd.hdr.hwndFrom, &rc))
        FillRect(tbcd->nmcd.hdc, &rc, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
      result = CDRF_NOTIFYITEMDRAW;
      return true;
    }
    case CDDS_ITEMPREPAINT:
    {
      const UINT state = tbcd->nmcd.uItemState;
      /* Paint button face ourselves so standard imagelist buttons are not white. */
      HBRUSH face = g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH);
      if (state & (CDIS_HOT | CDIS_SELECTED | CDIS_CHECKED | CDIS_MARKED))
        face = g_MenuHotBrush ? g_MenuHotBrush : (g_EditBrush ? g_EditBrush : face);
      if (tbcd->nmcd.rc.right > tbcd->nmcd.rc.left)
        FillRect(tbcd->nmcd.hdc, &tbcd->nmcd.rc, face);

      tbcd->clrText = (state & CDIS_DISABLED) ? RGB(140, 140, 140) : kDark_Text;
      tbcd->clrMark = kDark_Text;
      tbcd->clrTextHighlight = kDark_Text;
      tbcd->clrBtnFace = kDark_Bk;
      tbcd->clrBtnHighlight = kDark_EditBk;
      tbcd->clrHighlightHotTrack = kDark_SelectBk;
      tbcd->nStringBkMode = TRANSPARENT;
      tbcd->nHLStringBkMode = TRANSPARENT;
      /* Skip default face/edge; still allow image + text. */
      result = (LRESULT)(CDRF_NEWFONT | TBCDRF_USECDCOLORS | TBCDRF_NOBACKGROUND);
      if (state & CDIS_HOT)
        result |= TBCDRF_HILITEHOTTRACK;
      return true;
    }
    default:
      break;
  }
  return false;
}

bool DarkMode_OnHeaderCustomDraw(LPNMCUSTOMDRAW cd, LRESULT &result)
{
  if (!g_DarkMode || !cd)
    return false;

  switch (cd->dwDrawStage)
  {
    case CDDS_PREPAINT:
    {
      RECT rc;
      if (GetClientRect(cd->hdr.hwndFrom, &rc))
        FillRect(cd->hdc, &rc, g_HeaderBrush ? g_HeaderBrush : g_BkBrush);
      result = CDRF_NOTIFYITEMDRAW;
      return true;
    }
    case CDDS_ITEMPREPAINT:
    {
      // Fully paint each header item so text is light on dark
      HWND hHeader = cd->hdr.hwndFrom;
      const int index = (int)cd->dwItemSpec;

      RECT rc = cd->rc;
      HBRUSH br = g_HeaderBrush ? g_HeaderBrush : g_BkBrush;
      if (cd->uItemState & CDIS_SELECTED)
        br = g_MenuHotBrush ? g_MenuHotBrush : br;
      FillRect(cd->hdc, &rc, br);

      // Separator line on the right edge
      {
        HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
        HPEN oldPen = (HPEN)SelectObject(cd->hdc, pen);
        MoveToEx(cd->hdc, rc.right - 1, rc.top + 2, NULL);
        LineTo(cd->hdc, rc.right - 1, rc.bottom - 2);
        SelectObject(cd->hdc, oldPen);
        DeleteObject(pen);
      }

      // Bottom border under header
      {
        HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
        HPEN oldPen = (HPEN)SelectObject(cd->hdc, pen);
        MoveToEx(cd->hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(cd->hdc, rc.right, rc.bottom - 1);
        SelectObject(cd->hdc, oldPen);
        DeleteObject(pen);
      }

      WCHAR text[256];
      text[0] = 0;
      HDITEMW item;
      memset(&item, 0, sizeof(item));
      item.mask = HDI_TEXT | HDI_FORMAT;
      item.pszText = text;
      item.cchTextMax = 255;
      Header_GetItem(hHeader, index, &item);

      RECT rcText = rc;
      rcText.left += 6;
      rcText.right -= 6;

      SetBkMode(cd->hdc, TRANSPARENT);
      SetTextColor(cd->hdc, kDark_Text);

      UINT align = DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX;
      if (item.fmt & HDF_CENTER)
        align |= DT_CENTER;
      else if (item.fmt & HDF_RIGHT)
        align |= DT_RIGHT;
      else
        align |= DT_LEFT;

      DrawTextW(cd->hdc, text, -1, &rcText, align);

      // Sort arrow (if present)
      if (item.fmt & (HDF_SORTUP | HDF_SORTDOWN))
      {
        const bool up = (item.fmt & HDF_SORTUP) != 0;
        const int cx = 8;
        const int cy = 4;
        const int x = rc.right - 12;
        const int y = (rc.top + rc.bottom - cy) / 2;
        POINT pts[3];
        if (up)
        {
          pts[0].x = x; pts[0].y = y + cy;
          pts[1].x = x + cx; pts[1].y = y + cy;
          pts[2].x = x + cx / 2; pts[2].y = y;
        }
        else
        {
          pts[0].x = x; pts[0].y = y;
          pts[1].x = x + cx; pts[1].y = y;
          pts[2].x = x + cx / 2; pts[2].y = y + cy;
        }
        HBRUSH abr = CreateSolidBrush(kDark_Text);
        HPEN pen = CreatePen(PS_SOLID, 1, kDark_Text);
        HBRUSH oldBr = (HBRUSH)SelectObject(cd->hdc, abr);
        HPEN oldPen = (HPEN)SelectObject(cd->hdc, pen);
        Polygon(cd->hdc, pts, 3);
        SelectObject(cd->hdc, oldBr);
        SelectObject(cd->hdc, oldPen);
        DeleteObject(abr);
        DeleteObject(pen);
      }

      result = CDRF_SKIPDEFAULT;
      return true;
    }
    default:
      break;
  }
  return false;
}

bool DarkMode_OnStatusBarCustomDraw(LPNMCUSTOMDRAW cd, LRESULT &result)
{
  if (!g_DarkMode || !cd)
    return false;

  switch (cd->dwDrawStage)
  {
    case CDDS_PREPAINT:
    {
      RECT rc;
      if (GetClientRect(cd->hdr.hwndFrom, &rc))
        FillRect(cd->hdc, &rc, g_StatusBrush ? g_StatusBrush : g_BkBrush);
      result = CDRF_NOTIFYITEMDRAW;
      return true;
    }
    case CDDS_ITEMPREPAINT:
    {
      // Draw each pane ourselves
      RECT rc = cd->rc;
      FillRect(cd->hdc, &rc, g_StatusBrush ? g_StatusBrush : g_BkBrush);

      // Subtle left separator between panes
      if (rc.left > 0)
      {
        HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
        HPEN oldPen = (HPEN)SelectObject(cd->hdc, pen);
        MoveToEx(cd->hdc, rc.left, rc.top + 2, NULL);
        LineTo(cd->hdc, rc.left, rc.bottom - 2);
        SelectObject(cd->hdc, oldPen);
        DeleteObject(pen);
      }

      // Status bar item text via SB_GETTEXT
      WCHAR text[512];
      text[0] = 0;
      const int part = (int)cd->dwItemSpec;
      // SB_GETTEXT returns length in low word when buffer provided
      SendMessageW(cd->hdr.hwndFrom, SB_GETTEXTW, (WPARAM)part, (LPARAM)text);

      RECT rcText = rc;
      rcText.left += 4;
      rcText.right -= 2;
      SetBkMode(cd->hdc, TRANSPARENT);
      SetTextColor(cd->hdc, kDark_Text);
      DrawTextW(cd->hdc, text, -1, &rcText,
          DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX | DT_END_ELLIPSIS);

      result = CDRF_SKIPDEFAULT;
      return true;
    }
    default:
      break;
  }
  return false;
}

void DarkMode_ApplyToStatusBar(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  /* Empty theme + subclass WM_PAINT: NM_CUSTOMDRAW alone is unreliable for status bars. */
  if (g_DarkMode)
  {
    SetTheme(hwnd, L"");
    DarkMode_Subclass(hwnd, kDarkSub_StatusBar);
  }
  else
    SetTheme(hwnd, L"Explorer");
#endif
  SendMessage(hwnd, CCM_SETBKCOLOR, 0, (LPARAM)(g_DarkMode ? kDark_Status : GetSysColor(COLOR_BTNFACE)));
  InvalidateRect(hwnd, NULL, TRUE);
  UpdateWindow(hwnd);
}

void DarkMode_ApplyToReBar(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
  {
    SetTheme(hwnd, L"");
    /* Header toolbar is reparented into the rebar, so NM_CUSTOMDRAW arrives here. */
    DarkMode_Subclass(hwnd, kDarkSub_ReBar);
  }
  else
    SetTheme(hwnd, L"Explorer");
#endif
  /* ReBar fill behind toolbar / path combo (band-level colors need matching cbSize per OS). */
  SendMessage(hwnd, RB_SETBKCOLOR, 0, (LPARAM)(g_DarkMode ? kDark_Bk : GetSysColor(COLOR_BTNFACE)));
  SendMessage(hwnd, RB_SETTEXTCOLOR, 0, (LPARAM)(g_DarkMode ? kDark_Text : GetSysColor(COLOR_BTNTEXT)));
  InvalidateRect(hwnd, NULL, TRUE);
}

void DarkMode_ApplyToComboBoxEx(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
  {
    /* CFD theme is the correct dark style for combo / address fields. */
    SetTheme(hwnd, L"DarkMode_CFD");
    DarkMode_Subclass(hwnd, kDarkSub_ComboEx);
  }
  else
    SetTheme(hwnd, L"CFD");

  HWND combo = (HWND)SendMessage(hwnd, CBEM_GETCOMBOCONTROL, 0, 0);
  if (combo)
  {
    AllowDarkForWindow(combo, g_DarkMode);
    if (g_DarkMode)
    {
      SetTheme(combo, L"DarkMode_CFD");
      /* Edit's parent is this ComboBox — CTLCOLOR must be handled here. */
      DarkMode_Subclass(combo, kDarkSub_Combo);
    }
    else
      SetTheme(combo, L"CFD");
    InvalidateRect(combo, NULL, TRUE);
  }

  HWND edit = (HWND)SendMessage(hwnd, CBEM_GETEDITCONTROL, 0, 0);
  if (edit)
  {
    AllowDarkForWindow(edit, g_DarkMode);
    if (g_DarkMode)
      SetTheme(edit, L"DarkMode_Explorer");
    else
      SetTheme(edit, L"Explorer");
    InvalidateRect(edit, NULL, TRUE);
  }
#endif
  InvalidateRect(hwnd, NULL, TRUE);
  UpdateWindow(hwnd);
}

bool DarkMode_OnMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result)
{
#ifndef UNDER_CE
  if (!g_DarkMode)
    return false;

  switch (message)
  {
    case WM_UAHDRAWMENU:
    {
      // Fill entire menu bar background
      UAHMENU *pUahMenu = (UAHMENU *)lParam;
      MENUBARINFO mbi;
      memset(&mbi, 0, sizeof(mbi));
      mbi.cbSize = sizeof(mbi);
      if (!GetMenuBarInfo(hwnd, OBJID_MENU, 0, &mbi))
        return false;

      RECT rcWindow;
      GetWindowRect(hwnd, &rcWindow);

      RECT rc = mbi.rcBar;
      OffsetRect(&rc, -rcWindow.left, -rcWindow.top);

      FillRect(pUahMenu->hdc, &rc, g_MenuBarBrush ? g_MenuBarBrush : g_BkBrush);
      result = 0;
      return true;
    }

    case WM_UAHDRAWMENUITEM:
    {
      UAHDRAWMENUITEM *pUDMI = (UAHDRAWMENUITEM *)lParam;
      const HMENU hMenu = pUDMI->um.hmenu;
      const int iPos = pUDMI->umi.iPosition;

      WCHAR text[256];
      text[0] = 0;
      MENUITEMINFOW mii;
      memset(&mii, 0, sizeof(mii));
      mii.cbSize = sizeof(mii);
      mii.fMask = MIIM_STRING | MIIM_STATE;
      mii.dwTypeData = text;
      mii.cch = 255;
      GetMenuItemInfoW(hMenu, (UINT)iPos, TRUE, &mii);

      const bool hot = (pUDMI->dis.itemState & (ODS_HOTLIGHT | ODS_SELECTED)) != 0;
      const bool disabled = (pUDMI->dis.itemState & (ODS_INACTIVE | ODS_DISABLED)) != 0;

      HBRUSH br = hot ? (g_MenuHotBrush ? g_MenuHotBrush : g_MenuBarBrush)
                      : (g_MenuBarBrush ? g_MenuBarBrush : g_BkBrush);
      FillRect(pUDMI->um.hdc, &pUDMI->dis.rcItem, br);

      SetBkMode(pUDMI->um.hdc, TRANSPARENT);
      SetTextColor(pUDMI->um.hdc, disabled ? RGB(140, 140, 140) : kDark_Text);

      DrawTextW(pUDMI->um.hdc, text, -1, &pUDMI->dis.rcItem,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_EXPANDTABS);

      result = 0;
      return true;
    }

    case WM_NCPAINT:
    case WM_NCACTIVATE:
    {
      // Let DefWindowProc paint frame first, then fix menu bar line
      result = DefWindowProc(hwnd, message, wParam, lParam);
      PaintMenuBarBottomLine(hwnd);
      return true;
    }

    default:
      break;
  }
#else
  (void)hwnd; (void)message; (void)wParam; (void)lParam; (void)result;
#endif
  return false;
}

struct CEnumThemeData
{
  bool Dark;
};

static BOOL CALLBACK EnumChildThemeProc(HWND hwnd, LPARAM lParam)
{
  CEnumThemeData *data = (CEnumThemeData *)lParam;
  WCHAR className[64];
  if (GetClassNameW(hwnd, className, 64) == 0)
    return TRUE;

#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, data->Dark);

  if (lstrcmpiW(className, WC_LISTVIEWW) == 0)
  {
    DarkMode_ApplyToListView(hwnd);
  }
  else if (lstrcmpiW(className, L"ToolbarWindow32") == 0)
  {
    DarkMode_ApplyToToolBar(hwnd);
  }
  else if (lstrcmpiW(className, L"msctls_statusbar32") == 0)
  {
    DarkMode_ApplyToStatusBar(hwnd);
  }
  else if (lstrcmpiW(className, L"ReBarWindow32") == 0)
  {
    DarkMode_ApplyToReBar(hwnd);
  }
  else if (lstrcmpiW(className, L"SysHeader32") == 0)
  {
    if (data->Dark)
    {
      SetTheme(hwnd, L"");
      DarkMode_Subclass(hwnd, kDarkSub_Header);
    }
    else
      SetTheme(hwnd, L"ItemsView");
    InvalidateRect(hwnd, NULL, TRUE);
  }
  else if (lstrcmpiW(className, L"ComboBoxEx32") == 0)
  {
    /* Must use CFD — DarkMode_Explorer leaves address bars white. */
    DarkMode_ApplyToComboBoxEx(hwnd);
  }
  else if (lstrcmpiW(className, WC_COMBOBOXW) == 0)
  {
    AllowDarkForWindow(hwnd, data->Dark);
    if (data->Dark)
    {
      SetTheme(hwnd, L"DarkMode_CFD");
      DarkMode_Subclass(hwnd, kDarkSub_Combo);
    }
    else
      SetTheme(hwnd, L"CFD");
  }
  else if (lstrcmpiW(className, WC_TABCONTROLW) == 0 ||
           lstrcmpiW(className, L"SysTabControl32") == 0)
  {
    DarkMode_ApplyToTab(hwnd);
  }
  else if (lstrcmpiW(className, WC_BUTTONW) == 0)
  {
    DarkMode_ApplyToButton(hwnd);
    if (data->Dark && !IsOwnerTextButton(hwnd))
      DarkMode_Subclass(hwnd, kDarkSub_PushButton);
  }
  else if (lstrcmpiW(className, WC_EDITW) == 0)
  {
    DarkMode_ApplyToEdit(hwnd);
  }
  else if (lstrcmpiW(className, WC_STATICW) == 0)
  {
    /* Static labels / icons — no theme so CTLCOLORSTATIC text is light. */
    AllowDarkForWindow(hwnd, data->Dark);
    if (data->Dark)
      SetTheme(hwnd, L"");
    else
      SetTheme(hwnd, L"Explorer");
  }
  else if (lstrcmpiW(className, WC_TREEVIEWW) == 0 ||
           lstrcmpiW(className, L"SysTreeView32") == 0 ||
           lstrcmpiW(className, L"msctls_trackbar32") == 0 ||
           lstrcmpiW(className, L"msctls_updown32") == 0)
  {
    if (data->Dark)
      SetTheme(hwnd, L"DarkMode_Explorer");
    else
      SetTheme(hwnd, L"Explorer");
  }
#else
  (void)data;
#endif
  return TRUE;
}

void DarkMode_ApplyToChildControls(HWND parent)
{
  if (!parent)
    return;
  CEnumThemeData data;
  data.Dark = g_DarkMode;
  EnumChildWindows(parent, EnumChildThemeProc, (LPARAM)&data);
}

void DarkMode_ApplyApp(HWND mainHwnd)
{
  if (mainHwnd)
  {
    DarkMode_ApplyToWindow(mainHwnd);
    SetClassLongPtr(mainHwnd, GCLP_HBRBACKGROUND,
      g_DarkMode ? (LONG_PTR)DarkMode_GetBkBrush() : (LONG_PTR)(COLOR_BTNFACE + 1));
    InvalidateRect(mainHwnd, NULL, TRUE);
#ifndef UNDER_CE
    // Redraw frame (menu bar) when toggling
    SetWindowPos(mainHwnd, NULL, 0, 0, 0, 0,
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    DrawMenuBar(mainHwnd);
#endif
  }

  if (g_App._toolBar)
    DarkMode_ApplyToToolBar(g_App._toolBar);

  for (unsigned i = 0; i < kNumPanelsMax; i++)
  {
    CPanel &panel = g_App.Panels[i];
    if (!panel.PanelCreated)
      continue;
    DarkMode_ApplyToWindow(panel);
    if (panel._listView)
      DarkMode_ApplyToListView(panel._listView);
    if (panel._headerToolBar)
      DarkMode_ApplyToToolBar(panel._headerToolBar);
    if (panel._headerReBar)
      DarkMode_ApplyToReBar(panel._headerReBar);
    if (panel._statusBar)
      DarkMode_ApplyToStatusBar(panel._statusBar);
#ifndef UNDER_CE
    if (panel._headerComboBox)
      DarkMode_ApplyToComboBoxEx(panel._headerComboBox);
#endif
    DarkMode_ApplyToChildControls(panel);
    panel.InvalidateRect(NULL, TRUE);
  }

  if (mainHwnd)
    DarkMode_ApplyToChildControls(mainHwnd);
}

bool DarkMode_OnCtlColor(UINT message, WPARAM wParam, LPARAM /* lParam */, LRESULT &result)
{
  if (!g_DarkMode)
    return false;

  HDC hdc = (HDC)wParam;
  switch (message)
  {
    case WM_CTLCOLORDLG:
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLORBTN:
    {
      SetTextColor(hdc, kDark_Text);
      SetBkColor(hdc, kDark_Bk);
      result = (LRESULT)g_BkBrush;
      return g_BkBrush != NULL;
    }
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORLISTBOX:
    {
      SetTextColor(hdc, kDark_Text);
      SetBkColor(hdc, kDark_EditBk);
      result = (LRESULT)g_EditBrush;
      return g_EditBrush != NULL;
    }
    case WM_CTLCOLORSCROLLBAR:
    {
      result = (LRESULT)g_BkBrush;
      return g_BkBrush != NULL;
    }
    default:
      break;
  }
  return false;
}

void DarkMode_OnInitDialog(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  ApplyImmersiveDarkTitleBar(hwnd, g_DarkMode);
  /*
    Do not apply DarkMode_Explorer to the dialog itself — it leaves
    group-box frames and radio/checkbox label colors on the light palette.
    Empty theme + WM_CTLCOLOR* gives readable light text on dark fill.
  */
  if (g_DarkMode)
    SetTheme(hwnd, L"");
  else
    SetTheme(hwnd, L"Explorer");
#endif
  DarkMode_ApplyToChildControls(hwnd);
  if (g_DarkMode)
  {
    SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)DarkMode_GetBkBrush());
    InvalidateRect(hwnd, NULL, TRUE);
  }
}

bool DarkMode_OnTabCustomDraw(LPNMCUSTOMDRAW cd, LRESULT &result)
{
  if (!g_DarkMode || !cd)
    return false;

  switch (cd->dwDrawStage)
  {
    case CDDS_PREPAINT:
    {
      RECT rc;
      if (GetClientRect(cd->hdr.hwndFrom, &rc))
        FillRect(cd->hdc, &rc, g_BkBrush ? g_BkBrush : (HBRUSH)GetStockObject(DKGRAY_BRUSH));
      result = CDRF_NOTIFYITEMDRAW;
      return true;
    }
    case CDDS_ITEMPREPAINT:
    {
      HWND hTab = cd->hdr.hwndFrom;
      const int index = (int)cd->dwItemSpec;

      RECT rc;
      if (!TabCtrl_GetItemRect(hTab, index, &rc))
        rc = cd->rc;

      const bool selected = (cd->uItemState & CDIS_SELECTED) != 0;
      const bool hot = (cd->uItemState & CDIS_HOT) != 0;

      HBRUSH br = selected
          ? (g_EditBrush ? g_EditBrush : g_BkBrush)
          : (hot && g_MenuHotBrush ? g_MenuHotBrush : g_BkBrush);
      if (!br)
        br = (HBRUSH)GetStockObject(DKGRAY_BRUSH);
      FillRect(cd->hdc, &rc, br);

      /* Top edge for active tab */
      if (selected)
      {
        HPEN pen = CreatePen(PS_SOLID, 1, kDark_SelectBk);
        HPEN oldPen = (HPEN)SelectObject(cd->hdc, pen);
        MoveToEx(cd->hdc, rc.left, rc.top, NULL);
        LineTo(cd->hdc, rc.right, rc.top);
        SelectObject(cd->hdc, oldPen);
        DeleteObject(pen);
      }
      else
      {
        HPEN pen = CreatePen(PS_SOLID, 1, kDark_Border);
        HPEN oldPen = (HPEN)SelectObject(cd->hdc, pen);
        MoveToEx(cd->hdc, rc.left, rc.bottom - 1, NULL);
        LineTo(cd->hdc, rc.right, rc.bottom - 1);
        SelectObject(cd->hdc, oldPen);
        DeleteObject(pen);
      }

      WCHAR text[256];
      text[0] = 0;
      TCITEMW item;
      memset(&item, 0, sizeof(item));
      item.mask = TCIF_TEXT;
      item.pszText = text;
      item.cchTextMax = 255;
      TabCtrl_GetItem(hTab, index, &item);

      SetBkMode(cd->hdc, TRANSPARENT);
      SetTextColor(cd->hdc, kDark_Text);
      DrawTextW(cd->hdc, text, -1, &rc,
          DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

      result = CDRF_SKIPDEFAULT;
      return true;
    }
    default:
      break;
  }
  return false;
}

static void DarkMode_ApplyToTab(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
  {
    SetTheme(hwnd, L"");
    DarkMode_Subclass(hwnd, kDarkSub_Tab);
  }
  else
    SetTheme(hwnd, L"Explorer");
#endif
  InvalidateRect(hwnd, NULL, TRUE);
}

void DarkMode_ApplyToPropSheet(HWND hwnd)
{
  if (!hwnd || !g_DarkMode)
    return;

#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, true);
  /*
    Keep a themed top-level window (same as the main app).
    SetTheme("", "") forces classic 3D borders + light caption — the mismatch
    the Options dialog was showing. Child controls still get empty/custom themes.
  */
  SetTheme(hwnd, L"DarkMode_Explorer");
  ApplyImmersiveDarkTitleBar(hwnd, true);
  RefreshWindowFrame(hwnd);
  DarkMode_Subclass(hwnd, kDarkSub_PropSheet);
#endif

  SetClassLongPtr(hwnd, GCLP_HBRBACKGROUND, (LONG_PTR)DarkMode_GetBkBrush());

  /* Theme direct children: tab control, OK/Cancel/Apply/Help. */
  HWND child = GetWindow(hwnd, GW_CHILD);
  while (child)
  {
    WCHAR cls[64];
    cls[0] = 0;
    GetClassNameW(child, cls, 64);

#ifndef UNDER_CE
    AllowDarkForWindow(child, true);
#endif

    if (lstrcmpiW(cls, WC_TABCONTROLW) == 0 ||
        lstrcmpiW(cls, L"SysTabControl32") == 0)
    {
      DarkMode_ApplyToTab(child);
    }
    else if (lstrcmpiW(cls, WC_BUTTONW) == 0)
    {
      DarkMode_ApplyToButton(child);
      if (!IsOwnerTextButton(child))
        DarkMode_Subclass(child, kDarkSub_PushButton);
    }
    else if (lstrcmpiW(cls, WC_LISTVIEWW) == 0)
    {
      DarkMode_ApplyToListView(child);
    }
    else if (lstrcmpiW(cls, WC_EDITW) == 0)
    {
      DarkMode_ApplyToEdit(child);
    }

    child = GetWindow(child, GW_HWNDNEXT);
  }

  DarkMode_ApplyToChildControls(hwnd);
  InvalidateRect(hwnd, NULL, TRUE);
  RedrawWindow(hwnd, NULL, NULL,
      RDW_INVALIDATE | RDW_ERASE | RDW_ALLCHILDREN | RDW_UPDATENOW | RDW_FRAME);
}

int CALLBACK DarkMode_PropSheetCallback(HWND hwnd, UINT msg, LPARAM /* lParam */)
{
  /*
    PSCB_PRECREATE  = 2 : hwnd is NULL, lParam -> DLGTEMPLATE*
    PSCB_INITIALIZED = 1 : hwnd is the sheet
  */
  if (msg == PSCB_INITIALIZED)
  {
    if (DarkMode_IsEnabled())
      DarkMode_ApplyToPropSheet(hwnd);
  }
  return 0;
}

void DarkMode_Cleanup()
{
  FreeBrushes();
}
