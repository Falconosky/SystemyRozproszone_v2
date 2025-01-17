/*
Slownik typow informacji:
T - Lista polaczonych uzytkownikow (adres IP, port klienta do wysyłania, port klienta do odbioru, ID klienta)
I - Informacja inicjalizacyjna (adres IP, port wychodzący, port odbiorczy klienta, ID klienta)
X - Przykładowy typ informacji (dodaj inne typy w miarę potrzeb)
*/

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <gtk/gtk.h>
#include <vector>
#include "client_data_handling.h"
#include "client_threads.h"
#include "connection_initialize.h"

extern std::queue<std::string> messageQueue;
extern std::mutex queueMutex;
extern std::condition_variable queueCondVar;
extern bool running;
extern GtkBuilder *builder;

std::vector<std::tuple<std::string, int, int, int, std::string, int>> global_client_list; // IP, send_port, rec_port, client_id, message, timestamp
std::mutex global_client_list_mutex;

bool in_critical_section = false;
int lamport_clock = 0;

struct CriticalSectionRequest {
    int timestamp;
    int process_id;
};

std::vector<CriticalSectionRequest> request_queue;
std::mutex request_queue_mutex;

void update_critical_status_label(const std::string& status) {
    GtkWidget *critical_status_label = GTK_WIDGET(gtk_builder_get_object(builder, "critical_status"));
    gtk_label_set_text(GTK_LABEL(critical_status_label), status.c_str());
}

void update_lamport_clock_label() {
    GtkWidget *lamport_label = GTK_WIDGET(gtk_builder_get_object(builder, "lamport"));
    gtk_label_set_text(GTK_LABEL(lamport_label), std::to_string(lamport_clock).c_str());
}

void receive_thread_function() {
    char buffer[1024];

    GtkWidget *entry_rec_port = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
    const gchar *rec_port_text = gtk_entry_get_text(GTK_ENTRY(entry_rec_port));
    int rec_port = atoi(rec_port_text);
    if (rec_port <= 0) {
        std::cerr << "Nieprawidłowy port do nasłuchiwania." << std::endl;
        return;
    }

    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Nie można utworzyć gniazda dla odbioru." << std::endl;
        return;
    }

    sockaddr_in recv_addr;
    std::memset(&recv_addr, 0, sizeof(recv_addr));
    recv_addr.sin_family = AF_INET;
    recv_addr.sin_addr.s_addr = INADDR_ANY;
    recv_addr.sin_port = htons(rec_port);

    if (bind(sockfd, (struct sockaddr*)&recv_addr, sizeof(recv_addr)) < 0) {
        std::cerr << "Nie udało się związać gniazda z portem odbioru " << rec_port << "." << std::endl;
        close(sockfd);
        return;
    }

    if (listen(sockfd, 5) < 0) {
        std::cerr << "Błąd podczas uruchamiania nasłuchiwania." << std::endl;
        close(sockfd);
        return;
    }

    while (running) {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        int client_fd = accept(sockfd, (struct sockaddr*)&client_addr, &client_len);
        if (client_fd < 0) {
            std::cerr << "Błąd podczas akceptowania połączenia." << std::endl;
            continue;
        }

        ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string message(buffer);

            // Weryfikacja typu informacji
            if (message.empty()) {
                std::cerr << "Otrzymano pustą wiadomość." << std::endl;
                close(client_fd);
                continue;
            }

            char message_type = message[0];

            switch (message_type) {
                case 'T': { // Typ "Table" - lista połączonych użytkowników
                        std::vector<std::tuple<std::string, int, int, int, std::string, int>> client_list; // IP, send_port, rec_port, client_id, message, timestamp

                    // Parsuj listę klientów
                    size_t start = 1; // Pomijamy pierwszy znak (typ)
                    while (start < message.size()) {
                        size_t end = message.find(';', start);
                        std::string client_info = message.substr(start, end - start);

                        size_t first_separator = client_info.find(":");
                        size_t second_separator = client_info.find(":", first_separator + 1);
                        size_t third_separator = client_info.find(":", second_separator + 1);

                        if (first_separator != std::string::npos && second_separator != std::string::npos && third_separator != std::string::npos) {
                            std::string ip = client_info.substr(0, first_separator);
                            int send_port = std::stoi(client_info.substr(first_separator + 1, second_separator - first_separator - 1));
                            int rec_port = std::stoi(client_info.substr(second_separator + 1, third_separator - second_separator - 1));
                            int client_id = std::stoi(client_info.substr(third_separator + 1));

                            client_list.emplace_back(ip, send_port, rec_port, client_id, "Nieznany", 0);
                        }

                        start = (end == std::string::npos) ? end : end + 1;
                    }

                    // Zaktualizuj globalną listę klientów
                    {
                        std::lock_guard<std::mutex> lock(global_client_list_mutex);
                        global_client_list = client_list;
                    }

                    // Aktualizuj statusy procesów
                    update_process_statuses();

                    // Zaktualizuj widok użytkowników w GtkTreeView
                    update_other_processes_view();

                    // Wyświetl listę klientów w konsoli
                    std::cout << "Lista połączonych klientów:" << std::endl;
                    for (const auto& client : client_list) {
                        std::cout << "Adres: " << std::get<0>(client) << ", Port wysyłania: " << std::get<1>(client)
                                  << ", Port odbioru: " << std::get<2>(client) << ", ID: " << std::get<3>(client)
                                    << ", Komunikat: " << std::get<4>(client) << ", Timestamp: " << std::get<5>(client) << std::endl;
                    }
                    break;
                }

                default:
                    std::cerr << "Nieznany typ wiadomości: " << message_type << std::endl;
                    break;
            }
        } else if (bytes_received == 0) {
            std::cout << "Połączenie zamknięte przez klienta." << std::endl;
        } else {
            std::cerr << "Błąd podczas odbierania danych." << std::endl;
        }
        close(client_fd);
    }

    close(sockfd);
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
