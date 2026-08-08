// DarkMode.h — File Manager dark theme support

#ifndef ZIP7_INC_DARK_MODE_H
#define ZIP7_INC_DARK_MODE_H

#include "../../../Common/MyWindows.h"

#include <CommCtrl.h>

bool DarkMode_IsEnabled();

/* Load DarkMode flag from FM settings and apply process-wide preferred mode. */
void DarkMode_LoadFromSettings();

/* Enable/disable and refresh brushes / preferred app mode. Does not repaint UI. */
void DarkMode_SetEnabled(bool enabled);

/* Title bar (DWM), UxTheme "DarkMode_*", and list-view colors for hwnd. */
void DarkMode_ApplyToWindow(HWND hwnd);
void DarkMode_ApplyToListView(HWND hwnd);
void DarkMode_ApplyToToolBar(HWND hwnd);
void DarkMode_ApplyToStatusBar(HWND hwnd);
void DarkMode_ApplyToReBar(HWND hwnd);
void DarkMode_ApplyToComboBoxEx(HWND hwnd);

/* Enumerate immediate children and apply dark themes where useful. */
void DarkMode_ApplyToChildControls(HWND parent);

/* Full app refresh after settings change (main window + panels). */
void DarkMode_ApplyApp(HWND mainHwnd);

COLORREF DarkMode_GetBkColor();
COLORREF DarkMode_GetTextColor();
COLORREF DarkMode_GetTextBkColor();
COLORREF DarkMode_GetHotTextBkColor();
COLORREF DarkMode_GetDeletedTextColor();
HBRUSH DarkMode_GetBkBrush();

/* For WM_CTLCOLOR* handlers. Returns true if result was set. */
bool DarkMode_OnCtlColor(UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result);

/* Toolbar NM_CUSTOMDRAW: light text / dark button faces. Returns true if result was set. */
bool DarkMode_OnToolBarCustomDraw(LPNMTBCUSTOMDRAW tbcd, LRESULT &result);

/* List-view header (column titles) NM_CUSTOMDRAW. */
bool DarkMode_OnHeaderCustomDraw(LPNMCUSTOMDRAW cd, LRESULT &result);

/* Status bar NM_CUSTOMDRAW. */
bool DarkMode_OnStatusBarCustomDraw(LPNMCUSTOMDRAW cd, LRESULT &result);

/*
  Main-window messages for dark menu bar (Win10+ UAH).
  Returns true if *result should be returned from WndProc.
*/
bool DarkMode_OnMainWindowMessage(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam, LRESULT &result);

/* After WM_INITDIALOG: theme dialog + children. */
void DarkMode_OnInitDialog(HWND hwnd);

/* Property sheet (Options, etc.): title bar, tabs, bottom buttons. */
void DarkMode_ApplyToPropSheet(HWND hwnd);
int CALLBACK DarkMode_PropSheetCallback(HWND hwnd, UINT msg, LPARAM lParam);

/* Tab control NM_CUSTOMDRAW. */
bool DarkMode_OnTabCustomDraw(LPNMCUSTOMDRAW cd, LRESULT &result);

void DarkMode_Cleanup();

#endif
