#pragma once

#define IDR_MAINMENU        100
#define IDD_LOGIN           101
#define IDD_ADDRECORD       102

#define ID_MENU_LOGIN       201
#define ID_MENU_LOGOUT      202
#define ID_MENU_EXIT        203
#define ID_MENU_VIEW_DATA   211
#define ID_MENU_ADD_RECORD  212
#define ID_MENU_AUDIT       221
#define ID_MENU_SQL_DEMO    222

#define IDC_LISTVIEW        301
#define IDC_STATUSBAR       302

#define IDC_LOGIN_USER      401
#define IDC_LOGIN_PASS      402
#define IDC_ADD_INFO        403
#define IDC_ADD_PHONE       404

// Windows dialog/control constants — needed by windres (not auto-included unlike rc.exe)
#ifndef DS_SETFONT
#define DS_SETFONT          0x0040
#define DS_MODALFRAME       0x0080
#define DS_CENTER           0x0800
#define WS_POPUP            0x80000000L
#define WS_CAPTION          0x00C00000L
#define WS_SYSMENU          0x00080000L
#define ES_AUTOHSCROLL      0x0080L
#define ES_PASSWORD         0x0020L
#define SS_GRAYTEXT         0x0001L
#define IDC_STATIC          (-1)
#define IDOK                1
#define IDCANCEL            2
#endif
