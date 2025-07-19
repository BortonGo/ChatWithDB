#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <iostream>
#include <string>
#include <winsock2.h>
#pragma comment(lib, "ws2_32.lib")

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 1024

SOCKET client_socket;

bool init_connection() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return false;

    client_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (client_socket == INVALID_SOCKET) return false;

    sockaddr_in server{};
    server.sin_family = AF_INET;
    server.sin_port = htons(SERVER_PORT);
    server.sin_addr.s_addr = inet_addr(SERVER_IP);

    if (connect(client_socket, (sockaddr*)&server, sizeof(server)) < 0) return false;
    return true;
}

void send_line(const std::string& msg) {
    send(client_socket, msg.c_str(), msg.length(), 0);
}

std::string recv_line() {
    std::string result;
    char ch;
    int bytes;
    while ((bytes = recv(client_socket, &ch, 1, 0)) > 0) {
        if (ch == '\n') break;
        if (ch != '\r') result += ch;
    }
    if (bytes <= 0 && result.empty()) {
        return ""; // ���������� ���������
    }
    return result;
}

bool login_or_register() {
    int choice;
    std::cout << "��������:\n1. �����\n2. ������������������\n3. �����\n> ";
    std::cin >> choice;
    std::cin.ignore();

    if (choice == 3) return false;

    std::string login, password;
    std::cout << "�����: ";
    std::getline(std::cin, login);
    std::cout << "������: ";
    std::getline(std::cin, password);

    switch (choice) {
    case 1:
        send_line("LOGIN\n");
        break;
    case 2:
        send_line("REGISTER\n");
        break;
    default:
        std::cout << "�������� �����\n";
        return false;
    }

    send_line(login + "\n");
    send_line(password + "\n");

    std::string response = recv_line();
    if (response.empty()) {
        std::cout << "������ �������� ����������.\n";
        return false;
    }

    std::cout << response << "\n";
    return response.find("Welcome") != std::string::npos || response.find("����� ����������") != std::string::npos;
}

void user_menu() {
    while (true) {
        std::string menu_block;
        char buffer[1];
        int received;
        while ((received = recv(client_socket, buffer, 1, 0)) > 0) {
            menu_block += buffer[0];
            if (menu_block.size() >= 2 && menu_block.substr(menu_block.size() - 2) == "\n\n")
                break;
        }
        if (received <= 0) {
            std::cout << "������ �������� ����������.\n";
            break;
        }

        std::cout << menu_block;
        std::string choice;
        std::getline(std::cin, choice);
        send_line(choice + "\n");

        if (choice == "1") {
            std::string target, msg;
            std::cout << "���� (�����): ";
            std::getline(std::cin, target);
            std::cout << "���������: ";
            std::getline(std::cin, msg);
            send_line("@" + target + " " + msg + "\n");
        }
        else if (choice == "2") {
            std::string line;
            while (true) {
                line = recv_line();
                if (line.empty()) {
                    std::cout << "������ �������� ����������.\n";
                    return;
                }
                if (line == "__END__") break;
                std::cout << line << "\n";
            }
        }
        else if (choice == "3") {
            std::string response = recv_line();
            if (response.empty()) {
                std::cout << "������ ������ ����������.\n";
                return;
            }
            std::cout << response << "\n";
            return; // ������� � login_or_register
        }
        else if (choice == "4") {
            std::string response = recv_line();
            if (response.empty()) {
                std::cout << "������ ������ ����������.\n";
                return;
            }
            std::cout << response << "\n";
            exit(0); // ������ �����
        }
    }
}

int main() {
    setlocale(LC_ALL, "Russian");
    if (!init_connection()) {
        std::cerr << "�� ������� ������������ � �������\n";
        return 1;
    }

    while (true) {
        if (!login_or_register()) {
            break;
        }

        user_menu();

        std::cout << "�� ����� �� ��������.\n";
    }

    closesocket(client_socket);
    WSACleanup();
    return 0;
}
