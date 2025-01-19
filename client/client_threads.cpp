/*
Slownik typow informacji:
T - Lista polaczonych uzytkownikow (adres IP, port klienta do wysyłania, port klienta do odbioru, ID klienta)
I - Informacja inicjalizacyjna (adres IP, port wychodzący, port odbiorczy klienta, ID klienta)
R - Request (prośba o pozwolenie wejścia do sekcji krytycznej)
A - Akceptacja prośby o wejście do sekcji krytycznej
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
#include "debuging.h"

extern std::queue<std::string> messageQueue;
extern std::mutex queueMutex;
extern std::condition_variable queueCondVar;
extern bool running;
extern GtkBuilder *builder;

std::vector<std::tuple<std::string, int, int, int, std::string, int>> global_client_list; // IP, send_port, rec_port, client_id, message, timestamp
std::mutex global_client_list_mutex;
std::vector<std::pair<int, std::pair<std::string, int>>> last_messages;
std::mutex last_messages_mutex;

bool in_critical_section = false;
bool critical_requested = false;
bool debug3 = false;
std::vector<int> acceptance_list;
std::mutex acceptance_list_mutex;
int lamport_clock = 0;
int critical_entrance_timestamp = 0;
int oldest_request = 0;

struct CriticalSectionRequest {
    int timestamp;
    int process_id;
};

std::vector<CriticalSectionRequest> request_queue;
std::mutex request_queue_mutex;
std::vector<std::tuple<int, std::string, int, std::string, std::string>> logs; // Czas, Typ, Nadawca, Treść, Dodatkowe informacje
std::mutex logs_mutex;

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
    critical_entrance_timestamp = lamport_clock;

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
                close(sockfd);
                return;
            } else {
                std::cout << "Wysłano wiadomość do klienta: " << ip << ":" << send_port << " -> " << message << std::endl;
                critical_requested = true;
                GtkWidget *critical_exit_button = GTK_WIDGET(gtk_builder_get_object(builder, "critical_section"));
                gtk_widget_set_sensitive(critical_exit_button, false);
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

    {
        std::lock_guard<std::mutex> lock(last_messages_mutex);
        for (const auto& client : global_client_list) {
            int process_id = std::get<3>(client);

            // Znajdź istniejący wpis dla danego procesu
            auto it = std::find_if(
                last_messages.begin(),
                last_messages.end(),
                [process_id](const std::pair<int, std::pair<std::string, int>>& entry) {
                    return entry.first == process_id;
                });

            if (it != last_messages.end()) {
                // Jeśli istnieje wpis, sprawdź jego typ
                if (it->second.first == "Request") {
                    // Jeśli to "Request", dodaj "Waiting" bez usuwania "Request"
                    last_messages.emplace_back(process_id, std::make_pair("Waiting", lamport_clock));
                    std::cout << "[DEBUG] Dodano 'Waiting' dla procesu ID: " << process_id << std::endl;
                } else {
                    // Jeśli to inny typ, usuń istniejący wpis
                    std::cout << "[DEBUG] Usunięto wpis dla procesu ID: " << process_id
                              << " o typie: " << it->second.first << std::endl;
                    last_messages.erase(it);

                    // Dodaj "Waiting" w miejsce usuniętego wpisu
                    last_messages.emplace_back(process_id, std::make_pair("Waiting", lamport_clock));
                    std::cout << "[DEBUG] Dodano 'Waiting' dla procesu ID: " << process_id << std::endl;
                }
            } else {
                // Jeśli wpisu nie ma, dodaj nowy "Waiting"
                last_messages.emplace_back(process_id, std::make_pair("Waiting", lamport_clock));
                std::cout << "[DEBUG] Dodano nowy wpis 'Waiting' dla procesu ID: " << process_id << std::endl;
            }
        }
    }

    gdk_threads_add_idle([](void*) -> gboolean {
        update_process_statuses();
        update_other_processes_view();
        return FALSE; // Wykonaj funkcję tylko raz
    }, nullptr);

    update_logs();
}

void accept_request(GtkButton *button, gpointer user_data)
{
    {
        if (in_critical_section) {
            std::cerr << "Nie można zaakceptować prośby - proces jest w sekcji krytycznej." << std::endl;
            return;
        }

        update_lamport_clock(0); // Zwiększ lokalny zegar Lamporta

        std::lock_guard<std::mutex> lock(request_queue_mutex);
        if (request_queue.empty()) {
            std::cerr << "Brak prośb w kolejce do zaakceptowania." << std::endl;
            return;
        }

        // Znajdź najstarsze żądanie
        auto oldest_request = std::min_element(
            request_queue.begin(),
            request_queue.end(),
            [](const CriticalSectionRequest &a, const CriticalSectionRequest &b) {
                return a.timestamp < b.timestamp;
            }
        );

        int target_process_id = oldest_request->process_id;

        GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "next_process"));
        if (request_queue.size() > 1) {
            auto next_request = std::next(std::min_element(request_queue.begin(), request_queue.end(), [](const CriticalSectionRequest &a, const CriticalSectionRequest &b) {
                return a.timestamp < b.timestamp;
            }));

            if (next_request != request_queue.end()) {
                std::string label_text = "P" + std::to_string(next_request->process_id);
                gtk_label_set_text(GTK_LABEL(label), label_text.c_str());
            } else {
                gtk_label_set_text(GTK_LABEL(label), " ");
                GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
                gtk_widget_set_sensitive(accept_button, false);
            }
        } else {
            gtk_label_set_text(GTK_LABEL(label), " ");
            GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
            gtk_widget_set_sensitive(accept_button, false);
        }

        // Przygotuj wiadomość akceptacji
        std::string message = "A;";

        // Wyślij akceptację do klienta
        {
            std::lock_guard<std::mutex> client_lock(global_client_list_mutex);
            for (const auto &client : global_client_list) {
                if (std::get<3>(client) == target_process_id) {
                    int send_port = std::get<2>(client);
                    const std::string &ip = std::get<0>(client);

                    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);
                    if (sockfd < 0) {
                        std::cerr << "Nie można utworzyć gniazda do wysyłania akceptacji." << std::endl;
                        return;
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
                        return;
                    }

                    if (sendto(sockfd, message.c_str(), message.size(), 0, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
                        std::cerr << "Nie udało się wysłać wiadomości do klienta: " << ip << "." << std::endl;
                    } else {
                        std::cout << "Wysłano wiadomość akceptacji do klienta: " << ip << ":" << send_port << " -> " << message << std::endl;
                    }

                    GtkWidget *entry_process_id = GTK_WIDGET(gtk_builder_get_object(builder, "process_id"));
                    const gchar *entry_process_id_text = gtk_entry_get_text(GTK_ENTRY(entry_process_id));

                    {
                        std::lock_guard<std::mutex> lock2(logs_mutex);
                        logs.emplace_back(lamport_clock, "A", std::stoi(entry_process_id_text), message, "Wysłano akceptacje");
                    }
                    update_logs();

                    close(sockfd);
                    break;
                }
            }
        }

        {
            std::lock_guard<std::mutex> lock(last_messages_mutex);
            int index = 0;
            for (const auto& [process_id, message_info] : last_messages) {
                const std::string& message = message_info.first;
                int timestamp = message_info.second;
                index++;
            }
        }

        // Usuń najstarsze żądanie z kolejki
        request_queue.erase(oldest_request);

        {
            std::lock_guard<std::mutex> lock2(last_messages_mutex);

            // Debug: wyświetlenie stanu przed modyfikacją
            std::cout << "[DEBUG] Stan przed modyfikacją last_messages:" << std::endl;
            for (size_t index = 0; index < last_messages.size(); ++index) {
                const auto& [process_id, message_info] = last_messages[index];
                std::cout << "Index: " << index
                          << ", Process ID: " << process_id
                          << ", Message: " << message_info.first
                          << ", Timestamp: " << message_info.second << std::endl;
            }

            // Znajdowanie wpisu `Request` dla `target_process_id`
            auto it = std::find_if(
                last_messages.begin(),
                last_messages.end(),
                [target_process_id](const std::pair<int, std::pair<std::string, int>>& entry) {
                    return entry.first == target_process_id && entry.second.first == "Request";
                });

            if (it != last_messages.end()) {
                // Usunięcie wpisu `Request`
                std::cout << "[DEBUG] Usuwanie wpisu dla Process ID: " << it->first
                          << ", Message: " << it->second.first
                          << ", Timestamp: " << it->second.second << std::endl;
                last_messages.erase(it);

                // Dodanie nowego wpisu `Accepted`
                last_messages.emplace_back(target_process_id, std::make_pair("Accepted", lamport_clock));
                std::cout << "[DEBUG] Dodano wpis: Process ID: " << target_process_id
                          << ", Message: Accepted"
                          << ", Timestamp: " << lamport_clock << std::endl;
            } else {
                std::cerr << "[ERROR] Nie znaleziono wpisu Request dla Process ID: " << target_process_id << std::endl;
            }

            // Debug: wyświetlenie stanu po modyfikacji
            std::cout << "[DEBUG] Stan po modyfikacji last_messages:" << std::endl;
            for (size_t index = 0; index < last_messages.size(); ++index) {
                const auto& [process_id, message_info] = last_messages[index];
                std::cout << "Index: " << index
                          << ", Process ID: " << process_id
                          << ", Message: " << message_info.first
                          << ", Timestamp: " << message_info.second << std::endl;
            }
        }

        // Sprawdzenie znacznika czasu
        GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
        if (critical_entrance_timestamp < oldest_request->timestamp && critical_entrance_timestamp!=0) {
            gtk_widget_set_sensitive(accept_button, false);
            std::cout << "[DEBUG] Własny znacznik czasu jest mniejszy. Przycisk zablokowany." << std::endl;
        } else {
            gtk_widget_set_sensitive(accept_button, true);
            std::cout << "[DEBUG] Własny znacznik czasu jest większy. Przycisk odblokowany." << std::endl;
        }
        if (request_queue.empty())
        {
            gtk_widget_set_sensitive(accept_button, false);
        }
        textdebug2(std::to_string(request_queue.size()));
    }

    gdk_threads_add_idle([](void*) -> gboolean {
        update_process_statuses();
        update_other_processes_view();
        return FALSE; // Wykonaj funkcję tylko raz
    }, nullptr);
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
        textdebug("Przepisywanie bufora");
        std::memset(buffer, 0, sizeof(buffer));
        textdebug("ssize_t");
        ssize_t bytes_received = recvfrom(sockfd, buffer, sizeof(buffer) - 1, 0, (struct sockaddr*)&client_addr, &client_len);
        textdebug("sprawdzanie czy bity>0");
        if (bytes_received > 0) {
            textdebug("Odebrano wiadomość");
            buffer[bytes_received] = '\0';
            std::string message(buffer);
            std::cout<<message<<std::endl;

            char message_type = message[0];

            switch (message_type) {
                case 'T': { // Typ "Table" - lista połączonych użytkowników
                    textdebug("Wiadomość typu T");
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
                        textdebug("Parsowanie klientow");
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

                                GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "connection_window"));
                                std::string title = "Klient P" + std::to_string(client_id);
                                gtk_window_set_title(GTK_WINDOW(window), title.c_str());

                                continue;
                            }
                            client_list.emplace_back(ip, send_port, rec_port, client_id, "Nieznany", 0);
                        }

                        start = (end == std::string::npos) ? end : end + 1;
                    }

                        textdebug("Aktualizacja zegaru");
                    update_lamport_clock(0);

                        textdebug("Skladowanie logu o tablicy uzytkownikow");
                    GtkWidget *toggle_button = GTK_WIDGET(gtk_builder_get_object(builder, "type_t"));
                    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle_button))) {
                        {
                            std::lock_guard<std::mutex> lock(logs_mutex);
                            logs.emplace_back(lamport_clock, "T", 0, message, "");
                        }
                        update_logs();
                    }

                        textdebug("Aktualizacja listy klientow w global_client_list");
                    // Zaktualizuj globalną listę klientów
                    {
                        std::lock_guard<std::mutex> lock(global_client_list_mutex);
                        global_client_list = client_list;
                    }


                    gdk_threads_add_idle([](void*) -> gboolean {
                        textdebug("update_process_statuses");
                        update_process_statuses();
                        textdebug("update_other_processes_view");
                        update_other_processes_view();
                        return FALSE; // Wykonaj funkcję tylko raz
                    }, nullptr);

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
                        textdebug("Wiadomosc typu R");
                        int sender_id = 0;
                        std::string single_field;
                        size_t start = 1; // Pomijamy pierwszy znak (typ)
                        if (start < message.size()) {
                            textdebug("Znajdowanie ;");
                            size_t end = message.find(';', start); // Znajdujemy średnik kończący wiadomość
                            if (end != std::string::npos) {
                                textdebug("Wyciagnieto pole");
                                single_field = message.substr(start, end - start); // Wyciągamy jedyne pole
                                int sender_port = ntohs(client_addr.sin_port);

                                {
                                    textdebug("Ustawianie sender id");
                                    std::lock_guard<std::mutex> lock(global_client_list_mutex);
                                    for (const auto& client : global_client_list) {
                                        if (std::get<1>(client) == sender_port) {
                                            sender_id = std::get<3>(client);
                                            break;
                                        }
                                    }
                                }

                                textdebug("aktualizacja zegara");
                                update_lamport_clock(std::stoi(single_field));

                                GtkWidget *toggle_button = GTK_WIDGET(gtk_builder_get_object(builder, "type_r"));
                                if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle_button)))
                                {
                                    textdebug("Skladowanie logu");
                                    {
                                        std::lock_guard<std::mutex> lock(logs_mutex);
                                        logs.emplace_back(lamport_clock, "R", sender_id, message, "");
                                    }
                                    update_logs();
                                }

                                textdebug("Dodawanie do kolejki żądań");
                                {
                                    std::lock_guard<std::mutex> lock(request_queue_mutex);
                                    request_queue.push_back({std::stoi(single_field), sender_id});
                                }
                                gdk_threads_add_idle([](void*) -> gboolean {
                                    textdebug("update_process_statuses");
                                    update_process_statuses();
                                    textdebug("update_other_processes_view");
                                    update_other_processes_view();
                                    return FALSE; // Wykonaj funkcję tylko raz
                                }, nullptr);

                                std::cout << "Odebrano pole: " << single_field << std::endl;
                            } else {
                                std::cerr << "Błąd: brak średnika kończącego wiadomość.\n";
                            }
                        } else {
                            std::cerr << "Błąd: wiadomość jest zbyt krótka.\n";
                        }
                        textdebug("odblokowywanie guzika accept_request");
                        if (!in_critical_section && (std::stoi(single_field)<=critical_entrance_timestamp || critical_entrance_timestamp==0))
                        {
                            GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
                            gtk_widget_set_sensitive(accept_button, true);
                        }

                        // Aktualizacja last_messages
                        textdebug("aktualizacja wiadomosci");
                        {
                            std::lock_guard<std::mutex> lock(last_messages_mutex);

                            // Sprawdzanie, czy istnieje wpis dla sender_id
                            textdebug("sprawdzanie czy istnieje wpis dla sender_id");
                            auto it = std::find_if(
                                last_messages.begin(),
                                last_messages.end(),
                                [sender_id](const std::pair<int, std::pair<std::string, int>>& entry) {
                                    return entry.first == sender_id;
                                });

                            if (it != last_messages.end()) {
                                // Jeśli istnieje, sprawdź timestamp i ewentualnie zaktualizuj
                                textdebug("wpisywanie danych");
                                it->second = {"Request", std::stoi(single_field)};
                            } else {
                                // Jeśli nie istnieje, dodaj nowy wpis
                                textdebug("dodawanie nowego wpisu");
                                oldest_request = std::stoi(single_field);
                                last_messages.emplace_back(sender_id, std::make_pair("Request", std::stoi(single_field)));
                            }
                        }

                        gdk_threads_add_idle([](void*) -> gboolean {
                            textdebug("update_process_statuses");
                            update_process_statuses();
                            textdebug("update_other_processes_view");
                            update_other_processes_view();
                            return FALSE; // Wykonaj funkcję tylko raz
                        }, nullptr);

                        break;
                    }

                case 'A': {
                        textdebug("wiadomosc typu A");
                        int sender_id = 0;
                        int sender_port = ntohs(client_addr.sin_port);

                        {
                            std::lock_guard<std::mutex> lock(global_client_list_mutex);
                            for (const auto& client : global_client_list) {
                                if (std::get<1>(client) == sender_port) {
                                    sender_id = std::get<3>(client);
                                    break;
                                }
                            }
                        }

                        GtkWidget *toggle_button = GTK_WIDGET(gtk_builder_get_object(builder, "type_a"));
                        if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(toggle_button)))
                        {
                            {
                                std::lock_guard<std::mutex> lock(logs_mutex);
                                logs.emplace_back(lamport_clock, "A", sender_id, message, "Otrzymano akceptację");
                            }
                            update_logs();
                        }

                        // Znalezienie sender_id na podstawie portu
                        {
                            std::lock_guard<std::mutex> lock(global_client_list_mutex);
                            for (const auto& client : global_client_list) {
                                std::cout << "[DEBUG] Sprawdzanie klienta: Port wysyłający: " << std::get<1>(client)
                                          << ", Oczekiwany port: " << sender_port << std::endl;
                                if (std::get<1>(client) == sender_port) {
                                    sender_id = std::get<3>(client);
                                    std::cout << "[DEBUG] Znaleziono sender_id: " << sender_id << std::endl;
                                    break;
                                }
                            }
                        }

                        // Aktualizacja last_messages
                        {
                            std::lock_guard<std::mutex> lock(last_messages_mutex);
                            for (size_t index = 0; index < last_messages.size(); ++index) {
                                const auto& [process_id, message_info] = last_messages[index];
                            }

                            // Znajdowanie wpisu dla sender_id
                            auto it = std::find_if(
                                last_messages.begin(),
                                last_messages.end(),
                                [sender_id](const std::pair<int, std::pair<std::string, int>>& entry) {
                                    return entry.first == sender_id;
                                });

                            if (it != last_messages.end()) {
                                last_messages.erase(it);
                            }
                            last_messages.emplace_back(sender_id, std::make_pair("Reply", lamport_clock));

                        }

                        {
                            std::lock_guard<std::mutex> lock(acceptance_list_mutex);
                            acceptance_list.push_back(sender_id);

                            std::lock_guard<std::mutex> lock2(global_client_list_mutex);
                            // Sprawdź, czy wszystkie akceptacje zostały zebrane
                            if (acceptance_list.size() == global_client_list.size()) {
                                // Opróżnij listę akceptacji
                                acceptance_list.clear();
                                in_critical_section = true;
                                std::cout << "[INFO] Wchodzimy do sekcji krytycznej." << std::endl;

                                GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "critical_status"));
                                std::string label_text = "Tak";
                                gtk_label_set_text(GTK_LABEL(label), label_text.c_str());

                                GtkWidget *critical_exit_button = GTK_WIDGET(gtk_builder_get_object(builder, "critical_exit"));
                                gtk_widget_set_sensitive(critical_exit_button, TRUE);

                                GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
                                gtk_widget_set_sensitive(accept_button, false);

                                // Usunięcie wszystkich elementów z last_messages, których wiadomość == "Reply"
                                {
                                    std::lock_guard<std::mutex> lock(last_messages_mutex);
                                    auto new_end = std::remove_if(
                                        last_messages.begin(),
                                        last_messages.end(),
                                        [](const std::pair<int, std::pair<std::string, int>> &entry) {
                                            return entry.second.first == "Reply";
                                        });
                                    last_messages.erase(new_end, last_messages.end());
                                }
                            }
                        }

                        gdk_threads_add_idle([](void*) -> gboolean {
                            textdebug("update_process_statuses");
                            update_process_statuses();
                            textdebug("update_other_processes_view");
                            update_other_processes_view();
                            return FALSE; // Wykonaj funkcję tylko raz
                        }, nullptr);
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

void critical_exit(GtkButton *button, gpointer user_data) {
    if (in_critical_section)
    {
        in_critical_section = false;
        GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "critical_status"));
        std::string label_text = "Nie";
        gtk_label_set_text(GTK_LABEL(label), label_text.c_str());

        GtkWidget *critical_exit_button = GTK_WIDGET(gtk_builder_get_object(builder, "critical_exit"));
        gtk_widget_set_sensitive(critical_exit_button, false);
        GtkWidget *critical_section_button = GTK_WIDGET(gtk_builder_get_object(builder, "critical_section"));
        gtk_widget_set_sensitive(critical_section_button, true);

        {
            std::lock_guard<std::mutex> lock(request_queue_mutex);
            if (!request_queue.empty() && (oldest_request<=critical_entrance_timestamp || critical_entrance_timestamp==0))
            {
                GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
                gtk_widget_set_sensitive(accept_button, true);
            }
        }

        critical_entrance_timestamp = 0;
    }
}
