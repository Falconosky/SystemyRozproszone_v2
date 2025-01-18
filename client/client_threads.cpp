/*
Slownik typow informacji:
T - Lista polaczonych uzytkownikow (adres IP, port klienta do wysyłania, port klienta do odbioru, ID klienta)
I - Informacja inicjalizacyjna (adres IP, port wychodzący, port odbiorczy klienta, ID klienta)
R - Request (prośba o pozwolenie wejścia do sekcji krytycznej)
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
std::vector<std::tuple<int, std::string, int, std::string, std::string>> logs; // Czas, Typ, Nadawca, Treść, Dodatkowe informacje
std::mutex logs_mutex;

void update_critical_status_label(const std::string& status) {
    GtkWidget *critical_status_label = GTK_WIDGET(gtk_builder_get_object(builder, "critical_status"));
    gtk_label_set_text(GTK_LABEL(critical_status_label), status.c_str());
}

void update_lamport_clock_label() {
    GtkWidget *lamport_label = GTK_WIDGET(gtk_builder_get_object(builder, "lamport"));
    gtk_label_set_text(GTK_LABEL(lamport_label), std::to_string(lamport_clock).c_str());
}

void update_lamport_clock(int message_time)
{
    if (message_time > lamport_clock)
    {
        lamport_clock = message_time + 1;
    }
    else
        lamport_clock++;
    update_lamport_clock_label();
}

void send_request(GtkButton *button, gpointer user_data)
{
    update_lamport_clock(0); // Zwiększ lokalny zegar Lamporta

    std::string message = "R" + std::to_string(lamport_clock-1) + ";"; // Budowanie wiadomości

    {
        std::lock_guard<std::mutex> lock(global_client_list_mutex);
        for (const auto& client : global_client_list) {
            int send_port = std::get<2>(client);
            const std::string& ip = std::get<0>(client);

            int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
            if (sockfd < 0) {
                std::cerr << "Nie można utworzyć gniazda." << std::endl;
                continue;
            }

            GtkWidget *entry_own_port = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
            const gchar *own_port_text = gtk_entry_get_text(GTK_ENTRY(entry_own_port));
            int own_port = atoi(own_port_text);

            sockaddr_in own_addr;
            memset(&own_addr, 0, sizeof(own_addr));
            own_addr.sin_family = AF_INET;
            own_addr.sin_addr.s_addr = INADDR_ANY;
            own_addr.sin_port = htons(own_port);

            if (bind(sockfd, (struct sockaddr*)&own_addr, sizeof(own_addr)) < 0) {
                int err = errno;
                std::cerr << "Nie udało się związać gniazda z portem własnym " << own_port << ". Kod błędu: " << err << " (" << strerror(err) << ")." << std::endl;
                close(sockfd);
                continue;
            }
            if (sockfd < 0) {
                std::cerr << "Nie można utworzyć gniazda." << std::endl;
                continue;
            }

            // Ustawienie opcji SO_REUSEADDR
            int opt = 1;
            if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
                std::cerr << "Nie udało się ustawić opcji SO_REUSEADDR." << std::endl;
                close(sockfd);
                continue;
            }

            sockaddr_in client_addr;
            std::memset(&client_addr, 0, sizeof(client_addr));
            client_addr.sin_family = AF_INET;
            client_addr.sin_port = htons(send_port);
            if (inet_pton(AF_INET, ip.c_str(), &client_addr.sin_addr) <= 0) {
                std::cerr << "Nieprawidłowy adres IP klienta: " << ip << "." << std::endl;
                close(sockfd);
                continue;
            }

            if (sendto(sockfd, message.c_str(), message.size(), 0, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
                std::cerr << "Nie udało się wysłać wiadomości do klienta: " << ip << "." << std::endl;
            } else {
                std::cout << "Wysłano wiadomość do klienta: " << ip << ":" << send_port << " -> " << message << std::endl;
            }

            if (send(sockfd, message.c_str(), message.size(), 0) < 0) {
                std::cerr << "Nie udało się wysłać wiadomości do klienta: " << ip << ":" << send_port<< std::endl;
            } else {
                std::cout << "Wysłano wiadomość do klienta: " << ip << ":" << send_port << " -> " << message << std::endl;
            }

            close(sockfd);
        }
    }

    {
        std::lock_guard<std::mutex> lock(logs_mutex);

        GtkWidget *entry_process_id = GTK_WIDGET(gtk_builder_get_object(builder, "process_id"));
        const gchar *process_id_text = gtk_entry_get_text(GTK_ENTRY(entry_process_id));
        int process_id = atoi(process_id_text);
        logs.emplace_back(lamport_clock, "R", process_id, message, "Wysłano żądanie do wszystkich klientów");
    }
    update_logs();
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

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
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

    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);

    while (running) {
        std::memset(buffer, 0, sizeof(buffer));
        ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&client_addr, &client_len);
        if (bytes_received > 0) {
            buffer[bytes_received] = '\0';
            std::string message(buffer);
            std::cout<<message<<std::endl;

            char message_type = message[0];

            switch (message_type) {
                case 'T': { // Typ "Table" - lista połączonych użytkowników
                    std::vector<std::tuple<std::string, int, int, int, std::string, int>> client_list; // IP, send_port, rec_port, client_id, message, timestamp
                    std::vector<std::tuple<int, std::string, int, std::string, std::string>> log; // Czas, Typ, Nadawca, Treść, Dodatkowe informacje

                    GtkWidget *entry_send_port = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
                    GtkWidget *entry_rec_port = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
                    GtkWidget *entry_process_id = GTK_WIDGET(gtk_builder_get_object(builder, "process_id"));

                    const gchar *self_send_port_text = gtk_entry_get_text(GTK_ENTRY(entry_send_port));
                    const gchar *self_rec_port_text = gtk_entry_get_text(GTK_ENTRY(entry_rec_port));

                    int self_send_port = atoi(self_send_port_text);
                    int self_rec_port = atoi(self_rec_port_text);

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

                            if (send_port == self_send_port && rec_port == self_rec_port) {
                                gtk_entry_set_text(GTK_ENTRY(entry_process_id), std::to_string(client_id).c_str());
                                start = (end == std::string::npos) ? end : end + 1;
                                continue;
                            }
                            client_list.emplace_back(ip, send_port, rec_port, client_id, "Nieznany", 0);
                        }

                        start = (end == std::string::npos) ? end : end + 1;
                    }

                    update_lamport_clock(0);

                    GtkWidget *toggle_button = GTK_WIDGET(gtk_builder_get_object(builder, "type_t"));
                    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle_button))) {
                        {
                            std::lock_guard<std::mutex> lock(logs_mutex);
                            logs.emplace_back(lamport_clock, "T", 0, message, "");
                        }
                        update_logs();
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
                    // std::cout << "Lista połączonych klientów:" << std::endl;
                    // for (const auto& client : client_list) {
                    //     std::cout << "Adres: " << std::get<0>(client) << ", Port wysyłania: " << std::get<1>(client)
                    //               << ", Port odbioru: " << std::get<2>(client) << ", ID: " << std::get<3>(client)
                    //                 << ", Komunikat: " << std::get<4>(client) << ", Timestamp: " << std::get<5>(client) << std::endl;
                    // }
                    break;
                }

                case 'R':
                    {
                        GtkWidget *toggle_button = GTK_WIDGET(gtk_builder_get_object(builder, "type_r"));
                        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle_button))) {
                            size_t start = 1; // Pomijamy pierwszy znak (typ)
                            if (start < message.size()) {
                                size_t end = message.find(';', start); // Znajdujemy średnik kończący wiadomość
                                if (end != std::string::npos) {
                                    std::string single_field = message.substr(start, end - start); // Wyciągamy jedyne pole
                                    int sender_id = 0;

                                    sockaddr_in client_addr;
                                    socklen_t client_len;
                                    //getpeername(client_fd, (struct sockaddr*)&client_addr, &client_len);
                                    int sender_port = ntohs(client_addr.sin_port);

                                    {
                                        std::lock_guard<std::mutex> lock(global_client_list_mutex);
                                        for (const auto& client : global_client_list) {
                                            if (std::get<2>(client) == sender_port) {
                                                sender_id = std::get<3>(client);
                                                break;
                                            }
                                            std::cout<<"sender port:"<<sender_port<<" std::get<1>(client)"<<std::get<1>(client)<<" std::get<3>(client)"<<std::get<3>(client)<<std::endl;
                                        }
                                    }

                                    update_lamport_clock(std::stoi(single_field));

                                    {
                                        std::lock_guard<std::mutex> lock(logs_mutex);
                                        logs.emplace_back(lamport_clock, "R", sender_id, message, "");
                                    }
                                    update_logs();

                                    std::cout << "Odebrano pole: " << single_field << std::endl;
                                } else {
                                    std::cerr << "Błąd: brak średnika kończącego wiadomość.\n";
                                }
                            } else {
                                std::cerr << "Błąd: wiadomość jest zbyt krótka.\n";
                            }
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
