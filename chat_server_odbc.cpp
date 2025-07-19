#include <iostream>
#include <thread>
#include <mutex>
#include <unordered_map>
#include <winsock2.h>
#include <windows.h>
#include <sqlext.h>
#include <locale.h>
#include <string>
#include <sstream>

#pragma comment(lib, "Ws2_32.lib")

#define PORT 12345
#define BUFFER_SIZE 1024

SQLHANDLE sqlEnvHandle = nullptr;
SQLHANDLE sqlConnHandle = nullptr;
std::mutex users_mutex;
std::unordered_map<std::string, SOCKET> online_users;

void send_message(SOCKET sock, const std::string& msg) {
    send(sock, msg.c_str(), msg.size(), 0);
}

bool execute_query(const std::string& query) {
    SQLHANDLE stmt;
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_STMT, sqlConnHandle, &stmt)) return false;
    SQLRETURN ret = SQLExecDirectA(stmt, (SQLCHAR*)query.c_str(), SQL_NTS);
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);
}

bool user_exists(const std::string& login, const std::string& password) {
    SQLHANDLE stmt;
    SQLCHAR sql[] = "SELECT password FROM users WHERE login = ?";
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_STMT, sqlConnHandle, &stmt)) return false;

    SQLBindParameter(stmt, 1, SQL_PARAM_INPUT, SQL_C_CHAR, SQL_CHAR, 255, 0, (SQLPOINTER)login.c_str(), 0, nullptr);
    SQLRETURN ret = SQLExecDirectA(stmt, sql, SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return false;
    }

    SQLCHAR db_pass[256];
    SQLLEN ind;
    if (SQLFetch(stmt) == SQL_SUCCESS) {
        SQLGetData(stmt, 1, SQL_C_CHAR, db_pass, sizeof(db_pass), &ind);

        std::cout << "[DEBUG] Пароль из БД: '" << db_pass << "'\n";
        std::cout << "[DEBUG] Введённый пароль: '" << password << "'\n";

        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return password == (char*)db_pass;
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    return false;
}

bool register_user(const std::string& login, const std::string& password) {
    std::string query = "INSERT INTO users (login, password) VALUES ('" + login + "', '" + password + "')";
    return execute_query(query);
}

void save_message(const std::string& sender, const std::string& receiver, const std::string& text) {
    std::string query = "INSERT INTO messages (sender, receiver, text) VALUES ('" +
        sender + "', '" + receiver + "', '" + text + "')";
    std::cout << "[DEBUG] Сохраняем сообщение: " << sender << " -> " << receiver << ": " << text << "\n";
    execute_query(query);
}

void send_inbox(SOCKET client_socket, const std::string& username) {
    SQLHANDLE stmt;
    std::string sql = "SELECT id, sender, text, timestamp FROM messages WHERE receiver = '" + username + "' ORDER BY timestamp DESC LIMIT 10";
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_STMT, sqlConnHandle, &stmt)) return;

    SQLRETURN ret = SQLExecDirectA(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS);
    if (ret != SQL_SUCCESS && ret != SQL_SUCCESS_WITH_INFO) {
        SQLFreeHandle(SQL_HANDLE_STMT, stmt);
        return;
    }

    SQLINTEGER id;
    SQLCHAR sender[256], text[512], timestamp[64];
    SQLLEN ind1, ind2, ind3, ind4;

    std::ostringstream oss;
    oss << "=== Входящие сообщения ===\n";

    while (SQLFetch(stmt) == SQL_SUCCESS) {
        SQLGetData(stmt, 1, SQL_C_SLONG, &id, 0, &ind1);
        SQLGetData(stmt, 2, SQL_C_CHAR, sender, sizeof(sender), &ind2);
        SQLGetData(stmt, 3, SQL_C_CHAR, text, sizeof(text), &ind3);
        SQLGetData(stmt, 4, SQL_C_CHAR, timestamp, sizeof(timestamp), &ind4);

        oss << "[" << id << "] [" << (char*)timestamp << "] "
            << (char*)sender << ": " << (char*)text << "\n";
    }

    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    oss << "__END__\n";
    send_message(client_socket, oss.str());
}

std::string recv_line(SOCKET sock) {
    std::string result;
    char ch;
    int bytes;
    while ((bytes = recv(sock, &ch, 1, 0)) > 0) {
        if (ch == '\n') break;
        if (ch != '\r') result += ch;
    }
    return result;
}

void handle_client(SOCKET client_socket) {
    std::cout << "[DEBUG] Клиент подключился\n";

    while (true) {
        std::cout << "[DEBUG] Новый цикл авторизации\n";
        std::string username;
        bool authenticated = false;

        while (!authenticated) {
            std::cout << "[DEBUG] Жду LOGIN/REGISTER\n";
            std::string command = recv_line(client_socket);
            std::cout << "[DEBUG] Получена команда: '" << command << "'\n";
            if (command.empty()) {
                std::cout << "[DEBUG] Клиент отключился до входа\n";
                closesocket(client_socket);
                return;
            }

            if (command == "LOGIN" || command == "REGISTER") {
                std::string login = recv_line(client_socket);
                std::string password = recv_line(client_socket);

                if (command == "LOGIN") {
                    if (!user_exists(login, password)) {
                        send_message(client_socket, "Ошибка: неправильный логин или пароль.\n");
                    }
                    else {
                        authenticated = true;
                        username = login;
                        send_message(client_socket, "Welcome, " + username + "!\n");
                    }
                }
                else {
                    if (register_user(login, password)) {
                        authenticated = true;
                        username = login;
                        send_message(client_socket, "Добро пожаловать, " + username + "!\n");
                    }
                    else {
                        send_message(client_socket, "Ошибка: не удалось зарегистрировать пользователя.\n");
                    }
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(users_mutex);
            online_users[username] = client_socket;
        }

        while (true) {
            send_message(client_socket,
                "\n=== Меню пользователя ===\n"
                "1. Отправить сообщение (@имя текст)\n"
                "2. Входящие\n"
                "3. Выйти из аккаунта\n"
                "4. Выйти из программы\n"
                "\n");

            std::string input = recv_line(client_socket);
            std::cout << "[DEBUG] Получена команда в меню: '" << input << "'\n";
            if (input.empty()) break;

            if (input == "2") {
                send_inbox(client_socket, username);
            }
            else if (input.rfind("@", 0) == 0) {
                auto space_pos = input.find(" ");
                if (space_pos != std::string::npos) {
                    std::string target = input.substr(1, space_pos - 1);
                    std::string message = input.substr(space_pos + 1);
                    save_message(username, target, message);

                    std::lock_guard<std::mutex> lock(users_mutex);
                    if (online_users.count(target)) {
                        send_message(online_users[target], "(Private) " + username + ": " + message + "\n");
                        send_message(client_socket, "Сообщение доставлено онлайн.\n");
                    }
                    else {
                        send_message(client_socket, "Пользователь офлайн. Сообщение сохранено.\n");
                    }
                }
                else {
                    send_message(client_socket, "Неверный формат. Используйте: @имя сообщение\n");
                }
            }
            else if (input == "3") {
                send_message(client_socket, "Вы вышли из аккаунта.\n");
                {
                    std::lock_guard<std::mutex> lock(users_mutex);
                    online_users.erase(username);
                }
                break; // вернуться к авторизации
            }
            else if (input == "4") {
                send_message(client_socket, "До свидания!\n");
                {
                    std::lock_guard<std::mutex> lock(users_mutex);
                    online_users.erase(username);
                }
                closesocket(client_socket);
                return;
            }
            else {
                send_message(client_socket, "Неизвестная команда.\n");
            }
        }
    }
}

bool init_odbc() {
    SQLWCHAR retconstring[1024];
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &sqlEnvHandle)) return false;
    if (SQL_SUCCESS != SQLSetEnvAttr(sqlEnvHandle, SQL_ATTR_ODBC_VERSION, (void*)SQL_OV_ODBC3, 0)) return false;
    if (SQL_SUCCESS != SQLAllocHandle(SQL_HANDLE_DBC, sqlEnvHandle, &sqlConnHandle)) return false;

    SQLRETURN ret = SQLDriverConnectW(
        sqlConnHandle,
        NULL,
        (SQLWCHAR*)L"DRIVER={MySQL ODBC 9.3 ANSI Driver};SERVER=localhost;PORT=3306;DATABASE=chat;UID=Ghost;PWD=1775;",
        SQL_NTS,
        retconstring,
        1024,
        NULL,
        SQL_DRIVER_COMPLETE
    );

    return (ret == SQL_SUCCESS || ret == SQL_SUCCESS_WITH_INFO);
}

int main() {
    setlocale(LC_ALL, "Russian");
    WSAData wsaData;
    WSAStartup(MAKEWORD(2, 2), &wsaData);

    if (!init_odbc()) {
        std::cerr << "Ошибка подключения к базе через ODBC\n";
        return 1;
    }

    SOCKET server_socket = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(PORT);
    addr.sin_addr.s_addr = INADDR_ANY;

    bind(server_socket, (sockaddr*)&addr, sizeof(addr));
    listen(server_socket, SOMAXCONN);

    std::cout << "Server is working on port " << PORT << "\n";

    while (true) {
        SOCKET client_socket = accept(server_socket, nullptr, nullptr);
        std::thread(handle_client, client_socket).detach();
    }

    closesocket(server_socket);
    SQLDisconnect(sqlConnHandle);
    SQLFreeHandle(SQL_HANDLE_DBC, sqlConnHandle);
    SQLFreeHandle(SQL_HANDLE_ENV, sqlEnvHandle);
    WSACleanup();
    return 0;
}
