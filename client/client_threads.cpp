#include "client_threads.h"
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

/*
Slownik typow informacji:
T - Lista polaczonych uzytkownikow (adres IP, port klienta do wysyłania, port klienta do odbioru)
I - Informacja inicjalizacyjna (adres IP, port wychodzący, port odbiorczy klienta)
X - Przykładowy typ informacji (dodaj inne typy w miarę potrzeb)
*/

extern std::queue<std::string> messageQueue;
extern std::mutex queueMutex;
extern std::condition_variable queueCondVar;
extern bool running;
extern GtkBuilder *builder;

std::vector<std::tuple<std::string, int, int>> global_client_list;
std::mutex global_client_list_mutex;

int initialize_connection(const std::string& ip_address, int port, int send_port) {
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0) {
        std::cerr << "Nie można utworzyć gniazda." << std::endl;
        return -1;
    }

    GtkWidget *entry_own_port = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
    const gchar *own_port_text = gtk_entry_get_text(GTK_ENTRY(entry_own_port));
    int own_port = atoi(own_port_text);
    if (own_port <= 0) {
        std::cerr << "Nieprawidłowy port własny." << std::endl;
        close(sockfd);
        return -1;
    }

    // Pobranie portu nasłuchującego z pola GtkEntry
    GtkWidget *entry_rec_port = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
    const gchar *rec_port_text = gtk_entry_get_text(GTK_ENTRY(entry_rec_port));
    int rec_port = atoi(rec_port_text);
    if (rec_port <= 0) {
        std::cerr << "Nieprawidłowy port nasłuchujący." << std::endl;
        close(sockfd);
        return -1;
    }

    // Ustawianie lokalnego adresu i port
    sockaddr_in client_addr;
    std::memset(&client_addr, 0, sizeof(client_addr));
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = INADDR_ANY; // Dowolny lokalny adres IP
    client_addr.sin_port = htons(own_port);

    if (bind(sockfd, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
        std::cerr << "Nie udało się związać gniazda z portem " << own_port << "." << std::endl;
        close(sockfd);
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
        GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "status"));
        std::string label_text = "Połączenie nieudane";
        gtk_label_set_text(GTK_LABEL(label), label_text.c_str());
        close(sockfd);
        return -1;
    }
    GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "status"));
    std::string label_text = "Pomyślnie połączono do serwera";
    gtk_label_set_text(GTK_LABEL(label), label_text.c_str());

    // Wysyłanie informacji inicjalizacyjnej do serwera
    std::string init_message = "I:" + ip_address + ":" + std::to_string(own_port) + ":" + std::to_string(rec_port) + ";";
    if (send(sockfd, init_message.c_str(), init_message.size(), 0) < 0) {
        std::cerr << "Błąd podczas wysyłania informacji inicjalizacyjnej do serwera." << std::endl;
        close(sockfd);
        return -1;
    }

    std::cout << "Połączono z serwerem: " << ip_address << ":" << port
              << " i wysłano informacje inicjalizacyjne: "
              << "IP: " << ip_address << ", Send Port: " << own_port << ", Rec Port: " << rec_port << std::endl;

    return sockfd;
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
                    std::vector<std::tuple<std::string, int, int>> client_list; // IP, send_port, rec_port

                    // Parsuj listę klientów
                    size_t start = 1; // Pomijamy pierwszy znak (typ)
                        while (start < message.size()) {
                            size_t end = message.find(';', start);
                            std::string client_info = message.substr(start, end - start);

                            size_t first_separator = client_info.find(":");
                            size_t second_separator = client_info.find(":", first_separator + 1);

                            if (first_separator != std::string::npos && second_separator != std::string::npos) {
                                std::string ip = client_info.substr(0, first_separator);
                                int send_port = std::stoi(client_info.substr(first_separator + 1, second_separator - first_separator - 1));
                                int rec_port = std::stoi(client_info.substr(second_separator + 1));

                                client_list.emplace_back(ip, send_port, rec_port);
                            }

                            start = (end == std::string::npos) ? end : end + 1;
                        }

                    {
                    std::lock_guard<std::mutex> lock(global_client_list_mutex);
                    global_client_list = client_list;
                    }

                    // Wyświetl listę klientów w konsoli
                    std::cout << "Lista połączonych klientów:" << std::endl;
                    for (const auto& client : client_list) {
                        std::cout << "Adres: " << std::get<0>(client) << ", Port wysyłania: " << std::get<1>(client)
                                  << ", Port odbioru: " << std::get<2>(client) << std::endl;
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

        // Przykład użycia globalnej listy klientów w wątku send_thread_function
        {
            std::lock_guard<std::mutex> lock(global_client_list_mutex);
            std::cout << "Globalna lista klientów dostępna w wątku send_thread_function:" << std::endl;
            for (const auto& client : global_client_list) {
                std::cout << "Adres: " << std::get<0>(client) << ", Port wysyłania: " << std::get<1>(client)
                          << ", Port odbioru: " << std::get<2>(client) << std::endl;
            }
        }
    }
}
