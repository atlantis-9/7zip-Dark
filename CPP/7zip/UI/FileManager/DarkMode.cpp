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
  if (FAILED(g_DwmSetWindowAttribute(hwnd, DWMWA_USE_IMMERSIVE_DARK_MODE, &value, sizeof(value))))
    g_DwmSetWindowAttribute(hwnd, 19, &value, sizeof(value));
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
    g_SetWindowTheme(hwnd, name, NULL);
}

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
  if (g_DarkMode)
    SetTheme(hwnd, L"DarkMode_Explorer");
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
    // Empty theme so our custom-draw colors apply reliably
    if (g_DarkMode)
      SetTheme(header, L"");
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
    SetTheme(hwnd, L"");
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
      tbcd->clrText = (state & CDIS_DISABLED) ? RGB(140, 140, 140) : kDark_Text;
      tbcd->clrMark = kDark_Text;
      tbcd->clrTextHighlight = kDark_Text;
      tbcd->clrBtnFace = kDark_Bk;
      tbcd->clrBtnHighlight = kDark_EditBk;
      tbcd->clrHighlightHotTrack = kDark_SelectBk;
      tbcd->nStringBkMode = TRANSPARENT;
      tbcd->nHLStringBkMode = TRANSPARENT;
      result = (LRESULT)(CDRF_NEWFONT | TBCDRF_USECDCOLORS);
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
  // Empty theme so custom draw paints the whole bar
  if (g_DarkMode)
    SetTheme(hwnd, L"");
  else
    SetTheme(hwnd, L"Explorer");
#endif
  SendMessage(hwnd, CCM_SETBKCOLOR, 0, (LPARAM)(g_DarkMode ? kDark_Status : GetSysColor(COLOR_BTNFACE)));
  InvalidateRect(hwnd, NULL, TRUE);
}

void DarkMode_ApplyToReBar(HWND hwnd)
{
  if (!hwnd)
    return;
#ifndef UNDER_CE
  AllowDarkForWindow(hwnd, g_DarkMode);
  if (g_DarkMode)
    SetTheme(hwnd, L"");
  else
    SetTheme(hwnd, L"Explorer");
#endif
  InvalidateRect(hwnd, NULL, TRUE);
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
      SetTheme(hwnd, L"");
    else
      SetTheme(hwnd, L"ItemsView");
    InvalidateRect(hwnd, NULL, TRUE);
  }
  else if (lstrcmpiW(className, WC_COMBOBOXW) == 0 ||
           lstrcmpiW(className, L"ComboBoxEx32") == 0 ||
           lstrcmpiW(className, WC_EDITW) == 0 ||
           lstrcmpiW(className, WC_BUTTONW) == 0 ||
           lstrcmpiW(className, WC_TREEVIEWW) == 0 ||
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
    {
      AllowDarkForWindow(panel._headerComboBox, g_DarkMode);
      if (g_DarkMode)
        SetTheme(panel._headerComboBox, L"DarkMode_CFD");
      else
        SetTheme(panel._headerComboBox, L"CFD");
      HWND edit = (HWND)SendMessage(panel._headerComboBox, CBEM_GETEDITCONTROL, 0, 0);
      if (edit)
      {
        AllowDarkForWindow(edit, g_DarkMode);
        if (g_DarkMode)
          SetTheme(edit, L"DarkMode_Explorer");
        else
          SetTheme(edit, L"Explorer");
      }
    }
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
  DarkMode_ApplyToWindow(hwnd);
  DarkMode_ApplyToChildControls(hwnd);
  if (g_DarkMode)
    InvalidateRect(hwnd, NULL, TRUE);
}

void DarkMode_Cleanup()
{
  FreeBrushes();
}
