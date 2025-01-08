#include "client_threads.h"
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <condition_variable>

extern std::queue<std::string> messageQueue;
extern std::mutex queueMutex;
extern std::condition_variable queueCondVar;
extern bool running;

int initialize_connection(const std::string& ip_address, int port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Nie można utworzyć gniazda." << std::endl;
        return -1;
    }

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    if (inet_pton(AF_INET, ip_address.c_str(), &server_addr.sin_addr) <= 0) {
        std::cerr << "Nieprawidłowy adres IP." << std::endl;
        close(sockfd);
        return -1;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
        std::cerr << "Połączenie nieudane." << std::endl;
        close(sockfd);
        return -1;
    }

    std::cout << "Połączono z serwerem: " << ip_address << ":" << port << std::endl;
    return sockfd;
}

void receive_thread_function(int sockfd) {
    char buffer[1024];
    while (running) {
        ssize_t bytes_received = recv(sockfd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string message(buffer);

            {
                std::lock_guard<std::mutex> lock(queueMutex);
                messageQueue.push(message);
            }
            queueCondVar.notify_one();
        } else if (bytes_received == 0) {
            std::cout << "Połączenie zamknięte przez serwer." << std::endl;
            running = false;
        } else {
            std::cerr << "Błąd podczas odbierania danych." << std::endl;
            running = false;
        }
    }
}

void send_thread_function(int sockfd) {
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        queueCondVar.wait(lock, [] { return !messageQueue.empty() || !running; });

        while (!messageQueue.empty()) {
            std::string message = messageQueue.front();
            messageQueue.pop();
            lock.unlock();

            // Przetwarzanie odebranej wiadomości
            std::cout << "Odebrano: " << message << std::endl;

            // Przykładowa odpowiedź
            std::string response = "Otrzymano: " + message;
            send(sockfd, response.c_str(), response.size(), 0);

            lock.lock();
        }
    }
}
