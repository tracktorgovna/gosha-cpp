#define UNICODE
#define _UNICODE
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <commctrl.h>
#include <sqlite3.h>
#include "resource.h"
#include <string>
#include <vector>

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(linker, "\"/manifestdependency:type='win32' \
name='Microsoft.Windows.Common-Controls' version='6.0.0.0' \
processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

// ============================================================
// UTF-8 <-> UTF-16 helpers
// ============================================================
static std::wstring ToWide(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
    std::wstring w(n - 1, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, &w[0], n);
    return w;
}

static std::string ToUTF8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string s(n - 1, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, &s[0], n, nullptr, nullptr);
    return s;
}

// ============================================================
// Backend — original logic, unchanged
// ============================================================
enum Role { ADMIN = 0, USER = 1, GUEST = 2 };

struct Session {
    int user_id = -1;
    std::string login;
    Role role = GUEST;
    bool authenticated = false;
} current_session;

sqlite3* db = nullptr;

static unsigned long hash_password(const std::string& password) {
    unsigned long hash = 5381;
    for (char c : password)
        hash = ((hash << 5) + hash) + c;
    return hash;
}

static std::string mask_sensitive(const std::string& data) {
    if (data.length() <= 4) return "****";
    return data.substr(0, 2) + std::string(data.length() - 4, '*') + data.substr(data.length() - 2);
}

static std::string mask_phone(const std::string& phone) {
    if (phone.length() < 4) return "****";
    return std::string(phone.length() - 2, '*') + phone.substr(phone.length() - 2);
}

static void log_action(int user_id, const std::string& operation, bool access_granted) {
    sqlite3_stmt* stmt;
    const char* sql = "INSERT INTO security_logs (user_id, operation, access_granted) VALUES (?, ?, ?);";
    sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, user_id);
    sqlite3_bind_text(stmt, 2, operation.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_int(stmt, 3, access_granted ? 1 : 0);
    sqlite3_step(stmt);
    sqlite3_finalize(stmt);
}

static void init_database() {
    const char* schema = R"SQL(
        CREATE TABLE IF NOT EXISTS users (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            login TEXT UNIQUE NOT NULL,
            pass_hash TEXT NOT NULL,
            role TEXT NOT NULL DEFAULT 'guest'
        );
        CREATE TABLE IF NOT EXISTS cloud_storage (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            owner_id INTEGER NOT NULL,
            sensitive_info TEXT,
            phone TEXT,
            FOREIGN KEY (owner_id) REFERENCES users(id)
        );
        CREATE TABLE IF NOT EXISTS security_logs (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            timestamp TEXT DEFAULT (datetime('now','localtime')),
            user_id INTEGER,
            operation TEXT,
            access_granted INTEGER
        );
    )SQL";

    char* err = nullptr;
    sqlite3_exec(db, schema, nullptr, nullptr, &err);
    if (err) { sqlite3_free(err); return; }

    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db, "SELECT COUNT(*) FROM users;", -1, &stmt, nullptr);
    sqlite3_step(stmt);
    int count = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);
    if (count > 0) return;

    std::string users_sql = "INSERT INTO users (login, pass_hash, role) VALUES ";
    users_sql += "('admin', '" + std::to_string(hash_password("admin123")) + "', 'admin'),";
    users_sql += "('user1', '" + std::to_string(hash_password("pass1"))    + "', 'user'),";
    users_sql += "('user2', '" + std::to_string(hash_password("pass2"))    + "', 'user'),";
    users_sql += "('guest', '" + std::to_string(hash_password("guest"))    + "', 'guest');";
    sqlite3_exec(db, users_sql.c_str(), nullptr, nullptr, &err);
    if (err) { sqlite3_free(err); }

    const char* data_sql = R"SQL(
        INSERT INTO cloud_storage (owner_id, sensitive_info, phone) VALUES
        (1, 'Конфиденциальный отчёт Q1 2026', '+79161234567'),
        (2, 'Договор аренды серверов AWS',     '+79031112233'),
        (2, 'Ключи доступа к S3 бакету',       '+79031112233'),
        (3, 'Персональные данные клиентов',     '+79257778899'),
        (1, 'Финансовый отчёт компании',        '+79161234567');
    )SQL";
    sqlite3_exec(db, data_sql, nullptr, nullptr, &err);
    if (err) { sqlite3_free(err); }
}

static bool authenticate(const std::string& login, const std::string& password) {
    sqlite3_stmt* stmt;
    const char* sql = "SELECT id, role FROM users WHERE login = ? AND pass_hash = ?;";
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

    std::string pass_hash = std::to_string(hash_password(password));
    sqlite3_bind_text(stmt, 1, login.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2, pass_hash.c_str(), -1, SQLITE_TRANSIENT);

    bool success = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        current_session.user_id = sqlite3_column_int(stmt, 0);
        current_session.login   = login;
        std::string role_str    = (const char*)sqlite3_column_text(stmt, 1);
        if      (role_str == "admin") current_session.role = ADMIN;
        else if (role_str == "user")  current_session.role = USER;
        else                          current_session.role = GUEST;
        current_session.authenticated = true;
        success = true;
    }
    sqlite3_finalize(stmt);

    log_action(
        success ? current_session.user_id : 0,
        success ? "LOGIN_SUCCESS" : "LOGIN_FAILED",
        success
    );
    return success;
}

static void do_logout() {
    if (current_session.authenticated)
        log_action(current_session.user_id, "LOGOUT", true);
    current_session = Session();
}

// ============================================================
// Data structs for GUI population
// ============================================================
struct CloudRecord { int id; std::string owner, info, phone; };
struct LogRecord   { int id; std::string timestamp, user, operation; bool granted; };

static std::vector<CloudRecord> get_cloud_data() {
    std::vector<CloudRecord> result;
    if (!current_session.authenticated) return result;

    sqlite3_stmt* stmt;
    if (current_session.role == ADMIN) {
        sqlite3_prepare_v2(db,
            "SELECT cs.id, u.login, cs.sensitive_info, cs.phone "
            "FROM cloud_storage cs JOIN users u ON cs.owner_id = u.id;",
            -1, &stmt, nullptr);
    } else if (current_session.role == USER) {
        sqlite3_prepare_v2(db,
            "SELECT cs.id, u.login, cs.sensitive_info, cs.phone "
            "FROM cloud_storage cs JOIN users u ON cs.owner_id = u.id WHERE cs.owner_id = ?;",
            -1, &stmt, nullptr);
        sqlite3_bind_int(stmt, 1, current_session.user_id);
    } else {
        sqlite3_prepare_v2(db,
            "SELECT cs.id, u.login, cs.sensitive_info, cs.phone "
            "FROM cloud_storage cs JOIN users u ON cs.owner_id = u.id;",
            -1, &stmt, nullptr);
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        CloudRecord r;
        r.id    = sqlite3_column_int(stmt, 0);
        r.owner = (const char*)sqlite3_column_text(stmt, 1);
        r.info  = (const char*)sqlite3_column_text(stmt, 2);
        r.phone = (const char*)sqlite3_column_text(stmt, 3);
        if (current_session.role == GUEST) {
            r.owner = mask_sensitive(r.owner);
            r.info  = mask_sensitive(r.info);
            r.phone = mask_phone(r.phone);
        }
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    log_action(current_session.user_id, "VIEW_DATA", true);
    return result;
}

static std::vector<LogRecord> get_audit_log() {
    std::vector<LogRecord> result;
    if (!current_session.authenticated || current_session.role != ADMIN) {
        if (current_session.authenticated)
            log_action(current_session.user_id, "VIEW_AUDIT_DENIED", false);
        return result;
    }
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT sl.id, sl.timestamp, COALESCE(u.login,'N/A'), sl.operation, sl.access_granted "
        "FROM security_logs sl LEFT JOIN users u ON sl.user_id = u.id "
        "ORDER BY sl.id DESC LIMIT 20;",
        -1, &stmt, nullptr);
    while (sqlite3_step(stmt) == SQLITE_ROW) {
        LogRecord r;
        r.id        = sqlite3_column_int(stmt, 0);
        r.timestamp = (const char*)sqlite3_column_text(stmt, 1);
        r.user      = (const char*)sqlite3_column_text(stmt, 2);
        r.operation = (const char*)sqlite3_column_text(stmt, 3);
        r.granted   = sqlite3_column_int(stmt, 4) != 0;
        result.push_back(r);
    }
    sqlite3_finalize(stmt);
    log_action(current_session.user_id, "VIEW_AUDIT_LOG", true);
    return result;
}

static bool add_record_db(const std::string& info, const std::string& phone) {
    if (!current_session.authenticated || current_session.role == GUEST) return false;
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "INSERT INTO cloud_storage (owner_id, sensitive_info, phone) VALUES (?, ?, ?);",
        -1, &stmt, nullptr);
    sqlite3_bind_int(stmt, 1, current_session.user_id);
    sqlite3_bind_text(stmt, 2, info.c_str(),   -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 3, phone.c_str(),  -1, SQLITE_TRANSIENT);
    bool ok = sqlite3_step(stmt) == SQLITE_DONE;
    sqlite3_finalize(stmt);
    log_action(current_session.user_id, ok ? "ADD_RECORD" : "ADD_RECORD_FAILED", ok);
    return ok;
}

// ============================================================
// GUI globals
// ============================================================
static HWND hMain, hListView, hStatusBar;

// ============================================================
// ListView helpers
// ============================================================
static void LV_Clear(HWND hLV) {
    ListView_DeleteAllItems(hLV);
    while (ListView_DeleteColumn(hLV, 0)) {}
}

static void LV_AddCol(HWND hLV, int i, const wchar_t* text, int w) {
    LVCOLUMN c  = {};
    c.mask      = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
    c.iSubItem  = i;
    c.pszText   = (LPWSTR)text;
    c.cx        = w;
    ListView_InsertColumn(hLV, i, &c);
}

static void LV_AddRow(HWND hLV, int row, std::vector<std::wstring> cols) {
    LVITEM item  = {};
    item.mask    = LVIF_TEXT;
    item.iItem   = row;
    item.pszText = (LPWSTR)cols[0].c_str();
    ListView_InsertItem(hLV, &item);
    for (int i = 1; i < (int)cols.size(); i++)
        ListView_SetItemText(hLV, row, i, (LPWSTR)cols[i].c_str());
}

// ============================================================
// View renderers
// ============================================================
static void ShowCloudData() {
    LV_Clear(hListView);
    LV_AddCol(hListView, 0, L"ID",        50);
    LV_AddCol(hListView, 1, L"Владелец", 115);
    LV_AddCol(hListView, 2, L"Данные",   300);
    LV_AddCol(hListView, 3, L"Телефон",  145);
    int row = 0;
    for (auto& r : get_cloud_data())
        LV_AddRow(hListView, row++, {
            ToWide(std::to_string(r.id)),
            ToWide(r.owner), ToWide(r.info), ToWide(r.phone)
        });
}

static void ShowAuditLog() {
    LV_Clear(hListView);
    if (current_session.role != ADMIN) {
        MessageBox(hMain,
            L"Журнал доступен только администратору.",
            L"Доступ запрещён", MB_OK | MB_ICONWARNING);
        return;
    }
    LV_AddCol(hListView, 0, L"ID",            50);
    LV_AddCol(hListView, 1, L"Время",         160);
    LV_AddCol(hListView, 2, L"Пользователь",  115);
    LV_AddCol(hListView, 3, L"Операция",      210);
    LV_AddCol(hListView, 4, L"Доступ",         65);
    int row = 0;
    for (auto& r : get_audit_log())
        LV_AddRow(hListView, row++, {
            ToWide(std::to_string(r.id)),
            ToWide(r.timestamp), ToWide(r.user), ToWide(r.operation),
            r.granted ? L"Да" : L"Нет"
        });
}

static void ShowSQLDemo() {
    std::string malicious_login = "admin' OR '1'='1";
    sqlite3_stmt* stmt;
    sqlite3_prepare_v2(db,
        "SELECT id FROM users WHERE login = ? AND pass_hash = ?;",
        -1, &stmt, nullptr);
    sqlite3_bind_text(stmt, 1, malicious_login.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_text(stmt, 2,
        std::to_string(hash_password("anything")).c_str(), -1, SQLITE_TRANSIENT);
    int rows = 0;
    while (sqlite3_step(stmt) == SQLITE_ROW) rows++;
    sqlite3_finalize(stmt);

    log_action(current_session.authenticated ? current_session.user_id : 0,
               "SQL_INJECTION_DEMO", true);

    LV_Clear(hListView);
    LV_AddCol(hListView, 0, L"Параметр", 220);
    LV_AddCol(hListView, 1, L"Значение", 500);
    LV_AddRow(hListView, 0, {L"Тест",                 L"Демонстрация защиты от SQL-инъекций"});
    LV_AddRow(hListView, 1, {L"Вредоносный ввод",     ToWide(malicious_login)});
    LV_AddRow(hListView, 2, {L"Уязвимый SQL",         L"SELECT * FROM users WHERE login = 'admin' OR '1'='1'"});
    LV_AddRow(hListView, 3, {L"Результат без защиты", L"Вернул бы всех пользователей"});
    LV_AddRow(hListView, 4, {L"Prepared Statement",   L"Ввод обработан как данные, не как код"});
    LV_AddRow(hListView, 5, {L"Строк найдено",
        rows == 0 ? L"0 — инъекция заблокирована" : L"УЯЗВИМОСТЬ ОБНАРУЖЕНА"});
}

// ============================================================
// Status bar and menu state
// ============================================================
static void UpdateStatus() {
    std::wstring s;
    if (current_session.authenticated) {
        std::wstring role_w = current_session.role == ADMIN ? L"Администратор"
                            : current_session.role == USER  ? L"Пользователь"
                                                            : L"Гость";
        s = L"  Пользователь: " + ToWide(current_session.login)
          + L"   |   Роль: " + role_w;
    } else {
        s = L"  Не авторизован";
    }
    SendMessage(hStatusBar, SB_SETTEXT, 0, (LPARAM)s.c_str());
}

static void UpdateMenu(HWND hwnd) {
    HMENU hMenu  = GetMenu(hwnd);
    bool auth    = current_session.authenticated;
    bool isAdmin = auth && current_session.role == ADMIN;
    bool canEdit = auth && current_session.role != GUEST;

    auto En = [&](UINT id, bool enabled) {
        EnableMenuItem(hMenu, id, MF_BYCOMMAND | (enabled ? MF_ENABLED : MF_GRAYED));
    };
    En(ID_MENU_LOGIN,      !auth);
    En(ID_MENU_LOGOUT,      auth);
    En(ID_MENU_VIEW_DATA,   auth);
    En(ID_MENU_ADD_RECORD,  canEdit);
    En(ID_MENU_AUDIT,       isAdmin);
    DrawMenuBar(hwnd);
}

// ============================================================
// Dialog: Login
// ============================================================
static std::wstring g_dlgLogin, g_dlgPass;

static INT_PTR CALLBACK LoginDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hDlg, IDC_LOGIN_USER));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[256];
            GetDlgItemText(hDlg, IDC_LOGIN_USER, buf, 256); g_dlgLogin = buf;
            GetDlgItemText(hDlg, IDC_LOGIN_PASS, buf, 256); g_dlgPass  = buf;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return FALSE;
}

// ============================================================
// Dialog: Add Record
// ============================================================
static std::wstring g_dlgInfo, g_dlgPhone;

static INT_PTR CALLBACK AddRecordDlgProc(HWND hDlg, UINT msg, WPARAM wParam, LPARAM) {
    switch (msg) {
    case WM_INITDIALOG:
        SetFocus(GetDlgItem(hDlg, IDC_ADD_INFO));
        return FALSE;
    case WM_COMMAND:
        if (LOWORD(wParam) == IDOK) {
            wchar_t buf[512];
            GetDlgItemText(hDlg, IDC_ADD_INFO,  buf, 512); g_dlgInfo  = buf;
            GetDlgItemText(hDlg, IDC_ADD_PHONE, buf, 512); g_dlgPhone = buf;
            EndDialog(hDlg, IDOK);
        } else if (LOWORD(wParam) == IDCANCEL) {
            EndDialog(hDlg, IDCANCEL);
        }
        break;
    }
    return FALSE;
}

// ============================================================
// Main window procedure
// ============================================================
static LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
    case WM_CREATE: {
        HINSTANCE hInst = ((CREATESTRUCT*)lParam)->hInstance;

        hListView = CreateWindowEx(0, WC_LISTVIEW, L"",
            WS_CHILD | WS_VISIBLE | LVS_REPORT | LVS_SHOWSELALWAYS,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_LISTVIEW, hInst, nullptr);
        ListView_SetExtendedListViewStyle(hListView,
            LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_HEADERDRAGDROP);

        hStatusBar = CreateWindowEx(0, STATUSCLASSNAME, nullptr,
            WS_CHILD | WS_VISIBLE | SBARS_SIZEGRIP,
            0, 0, 0, 0, hwnd, (HMENU)(UINT_PTR)IDC_STATUSBAR, hInst, nullptr);

        UpdateStatus();
        UpdateMenu(hwnd);

        PostMessage(hwnd, WM_COMMAND, ID_MENU_LOGIN, 0);
        return 0;
    }

    case WM_SIZE: {
        SendMessage(hStatusBar, WM_SIZE, wParam, lParam);
        RECT rc, sbrc;
        GetClientRect(hwnd, &rc);
        GetWindowRect(hStatusBar, &sbrc);
        int sbh = sbrc.bottom - sbrc.top;
        MoveWindow(hListView, 0, 0, rc.right, rc.bottom - sbh, TRUE);
        return 0;
    }

    case WM_COMMAND:
        switch (LOWORD(wParam)) {

        case ID_MENU_LOGIN: {
            g_dlgLogin.clear(); g_dlgPass.clear();
            INT_PTR r = DialogBox(GetModuleHandle(nullptr),
                MAKEINTRESOURCE(IDD_LOGIN), hwnd, LoginDlgProc);
            if (r == IDOK && !g_dlgLogin.empty()) {
                do_logout();
                if (authenticate(ToUTF8(g_dlgLogin), ToUTF8(g_dlgPass))) {
                    std::wstring m = L"Добро пожаловать, "
                        + ToWide(current_session.login) + L"!";
                    MessageBox(hwnd, m.c_str(), L"Вход выполнен",
                        MB_OK | MB_ICONINFORMATION);
                    ShowCloudData();
                } else {
                    MessageBox(hwnd, L"Неверный логин или пароль.",
                        L"Ошибка", MB_OK | MB_ICONWARNING);
                }
                UpdateStatus();
                UpdateMenu(hwnd);
            }
            break;
        }

        case ID_MENU_LOGOUT:
            do_logout();
            LV_Clear(hListView);
            UpdateStatus();
            UpdateMenu(hwnd);
            PostMessage(hwnd, WM_COMMAND, ID_MENU_LOGIN, 0);
            break;

        case ID_MENU_EXIT:
            DestroyWindow(hwnd);
            break;

        case ID_MENU_VIEW_DATA:
            if (current_session.authenticated) ShowCloudData();
            break;

        case ID_MENU_ADD_RECORD: {
            if (!current_session.authenticated || current_session.role == GUEST) {
                MessageBox(hwnd,
                    L"Недостаточно прав для добавления записей.",
                    L"Доступ запрещён", MB_OK | MB_ICONWARNING);
                break;
            }
            g_dlgInfo.clear(); g_dlgPhone.clear();
            INT_PTR r = DialogBox(GetModuleHandle(nullptr),
                MAKEINTRESOURCE(IDD_ADDRECORD), hwnd, AddRecordDlgProc);
            if (r == IDOK && !g_dlgInfo.empty()) {
                if (add_record_db(ToUTF8(g_dlgInfo), ToUTF8(g_dlgPhone)))
                    MessageBox(hwnd, L"Запись добавлена.", L"OK",
                        MB_OK | MB_ICONINFORMATION);
                else
                    MessageBox(hwnd, L"Не удалось добавить запись.",
                        L"Ошибка", MB_OK | MB_ICONERROR);
                ShowCloudData();
            }
            break;
        }

        case ID_MENU_AUDIT:
            ShowAuditLog();
            break;

        case ID_MENU_SQL_DEMO:
            ShowSQLDemo();
            break;
        }
        return 0;

    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

// ============================================================
// Entry point
// ============================================================
int WINAPI WinMain(HINSTANCE hInst, HINSTANCE, LPSTR, int nCmdShow) {
    INITCOMMONCONTROLSEX icc = { sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_BAR_CLASSES };
    InitCommonControlsEx(&icc);

    wchar_t exePath[MAX_PATH];
    GetModuleFileName(nullptr, exePath, MAX_PATH);
    wchar_t* slash = wcsrchr(exePath, L'\\');
    if (slash) *(slash + 1) = L'\0';
    std::string dbPath = ToUTF8(std::wstring(exePath) + L"cloud_audit.db");

    if (sqlite3_open(dbPath.c_str(), &db) != SQLITE_OK) {
        MessageBox(nullptr, L"Не удалось открыть базу данных.",
            L"Ошибка запуска", MB_OK | MB_ICONERROR);
        return 1;
    }
    init_database();

    WNDCLASSEX wc    = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = MainWndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszMenuName  = MAKEINTRESOURCE(IDR_MAINMENU);
    wc.lpszClassName = L"CloudAuditWnd";
    wc.hIcon         = LoadIcon(nullptr, IDI_APPLICATION);
    wc.hIconSm       = LoadIcon(nullptr, IDI_APPLICATION);
    RegisterClassEx(&wc);

    hMain = CreateWindowEx(0, L"CloudAuditWnd",
        L"Система защиты и аудита данных",
        WS_OVERLAPPEDWINDOW,
        CW_USEDEFAULT, CW_USEDEFAULT, 800, 540,
        nullptr, nullptr, hInst, nullptr);

    ShowWindow(hMain, nCmdShow);
    UpdateWindow(hMain);

    MSG msg;
    while (GetMessage(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    sqlite3_close(db);
    return (int)msg.wParam;
}
