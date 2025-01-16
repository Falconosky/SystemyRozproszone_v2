#include <gtk/gtk.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include <thread>
#include <vector>
#include <mutex>
#include <fstream>
#include <chrono>
#include <string>
#include "connection_manager.h"
#include "../main.h"

std::vector<std::tuple<std::string, int, int>> connected_clients; // IP, send_port, rec_port
std::mutex clients_mutex;

extern GtkBuilder *builder;

int get_refresh_interval(const std::string& config_path) {
    std::ifstream config_file(config_path);
    if (!config_file.is_open()) {
        std::cerr << "Nie można otworzyć pliku konfiguracyjnego: " << config_path << std::endl;
        return 5; // Domyślny interwał w sekundach
    }

    std::string line;
    while (std::getline(config_file, line)) {
        size_t pos = line.find("connected_user_refresh=");
        if (pos != std::string::npos) {
            try {
                return std::stoi(line.substr(pos + std::string("connected_user_refresh=").length()));
            } catch (...) {
                std::cerr << "Nieprawidłowa wartość interwału w pliku konfiguracyjnym." << std::endl;
                return 5;
            }
        }
    }

    std::cerr << "Nie znaleziono ustawienia 'connected_user_refresh' w pliku konfiguracyjnym." << std::endl;
    return 5; // Domyślny interwał w sekundach
}

// Funkcja do aktualizacji widoku listy klientów
void update_clients_list(GtkTreeView *treeview) {
    GtkListStore *list_store = GTK_LIST_STORE(gtk_tree_view_get_model(treeview));
    gtk_list_store_clear(list_store);

    std::lock_guard<std::mutex> lock(clients_mutex);
    for (const auto &client : connected_clients) {
        GtkTreeIter iter;
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                           0, std::get<0>(client).c_str(),
                           1, std::get<1>(client),
                           2, std::get<2>(client),
                           -1);
    }
}

// Funkcja do wysyłania listy klientów do nowo połączonego klienta
void send_connected_clients_list(int client_fd) {
    std::lock_guard<std::mutex> lock(clients_mutex);
    std::string message = "T"; // Typ wiadomości: Table (lista klientów)
    for (const auto &client : connected_clients) {
        message += std::get<0>(client) + ":" + std::to_string(std::get<1>(client)) + ":" + std::to_string(std::get<2>(client)) + ";";
    }
    send(client_fd, message.c_str(), message.size(), 0);
    std::cout << "Wysłano wiadomość do klienta: " << message << std::endl;
}

void broadcast_clients_list() {
    std::lock_guard<std::mutex> lock(clients_mutex);
    std::string message = "T";
    for (const auto &client : connected_clients) {
        message += std::get<0>(client) + ":" + std::to_string(std::get<1>(client)) + ":" + std::to_string(std::get<2>(client)) + ";";
    }

    for (const auto &client : connected_clients) {
        int client_fd = socket(AF_INET, SOCK_STREAM, 0);
        if (client_fd < 0) {
            std::cerr << "Błąd otwarcia gniazda dla klienta." << std::endl;
            continue;
        }

        sockaddr_in client_addr;
        memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        client_addr.sin_port = htons(std::get<2>(client));
        inet_pton(AF_INET, std::get<0>(client).c_str(), &client_addr.sin_addr);

        if (connect(client_fd, (struct sockaddr *)&client_addr, sizeof(client_addr)) < 0) {
            std::cerr << "Nie udało się połączyć z klientem " << std::get<0>(client) << ":" << std::get<2>(client) << std::endl;
            close(client_fd);
            continue;
        }

        send(client_fd, message.c_str(), message.size(), 0);
        close(client_fd);
        std::cout << "Wysłano wiadomość do klienta " << std::get<0>(client) << ":" << std::get<2>(client) << " -> " << message << std::endl;
    }
}

// Funkcja obsługi sygnału kliknięcia przycisku "Start serwer"
extern "C" void start_server_clicked(GtkButton *button, gpointer user_data) {
    builder = GTK_BUILDER(user_data);
    GtkEntry *ip_entry = GTK_ENTRY(gtk_builder_get_object(builder, "address_ip"));
    GtkEntry *port_entry = GTK_ENTRY(gtk_builder_get_object(builder, "address_port"));
    GtkTreeView *clients_treeview = GTK_TREE_VIEW(gtk_builder_get_object(builder, "clients_address"));

    const gchar *ip_address = gtk_entry_get_text(GTK_ENTRY(ip_entry));
    const gchar *port_text = gtk_entry_get_text(GTK_ENTRY(port_entry));

    if (strlen(ip_address) == 0 || strlen(port_text) == 0) {
        g_printerr("Adres IP i port muszą być podane.\n");
        return;
    }

    int port = atoi(port_text);
    if (port <= 0) {
        g_printerr("Nieprawidłowy port.\n");
        return;
    }

    // Tworzenie gniazda serwera TCP
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        g_printerr("Nie udało się utworzyć gniazda.\n");
        return;
    }

    sockaddr_in server_addr;
    std::memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if (inet_pton(AF_INET, ip_address, &server_addr.sin_addr) <= 0) {
        g_printerr("Nieprawidłowy adres IP.\n");
        close(server_fd);
        return;
    }

    if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
        g_printerr("Błąd podczas bindowania adresu.\n");
        close(server_fd);
        return;
    }

    if (listen(server_fd, 5) < 0) {
        g_printerr("Błąd podczas uruchamiania nasłuchiwania.\n");
        close(server_fd);
        return;
    }

    GtkWidget *label = GTK_WIDGET(gtk_builder_get_object(builder, "server_address"));
    std::string label_text = "Serwer włączony: " + std::string(ip_address) + ":" + std::to_string(port);
    gtk_label_set_text(GTK_LABEL(label), label_text.c_str());

    // Ukryj przycisk "Start serwer"
    GtkWidget *start_button = GTK_WIDGET(button);
    gtk_widget_hide(start_button);

    std::string config_path = "../testfiles/config";
    int refresh_interval = get_refresh_interval(config_path);

    std::thread connection_thread([server_fd, clients_treeview]() {
        while (true) {
            sockaddr_in client_addr;
            socklen_t client_len = sizeof(client_addr);
            int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, &client_len);

            if (client_fd < 0) {
                std::cerr << "Błąd podczas akceptowania połączenia." << std::endl;
                continue;
            }

            // Odbieranie informacji od klienta i debugowanie
            char buffer[1024];
            ssize_t bytes_received = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
            if (bytes_received > 0) {
                buffer[bytes_received] = '\0';
                std::cerr << buffer << std::endl;

                // Parsowanie wiadomości inicjalizacyjnej (I:127.0.0.1:10100:10200;)
                if (buffer[0] == 'I') {
                    std::string message(buffer);
                    size_t first_colon = message.find(':');
                    size_t second_colon = message.find(':', first_colon + 1);
                    size_t third_colon = message.find(':', second_colon + 1);
                    size_t semicolon = message.find(';', third_colon + 1);

                    if (first_colon != std::string::npos && second_colon != std::string::npos &&
                        third_colon != std::string::npos && semicolon != std::string::npos) {
                        std::string ip = message.substr(first_colon + 1, second_colon - first_colon - 1);
                        int send_port = std::stoi(message.substr(second_colon + 1, third_colon - second_colon - 1));
                        int rec_port = std::stoi(message.substr(third_colon + 1, semicolon - third_colon - 1));

                        {
                            std::lock_guard<std::mutex> lock(clients_mutex);
                            connected_clients.emplace_back(ip, send_port, rec_port);
                        }

                        update_clients_list(clients_treeview);

                        std::cout << "Zaktualizowano listę klientów: " << ip << ", Send Port: " << send_port
                                  << ", Rec Port: " << rec_port << std::endl;
                    }
                }
            } else if (bytes_received == 0) {
                std::cout << "Klient zakończył połączenie." << std::endl;
            } else {
                std::cerr << "Błąd podczas odbierania danych od klienta." << std::endl;
            }

            char client_ip[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr.sin_addr, client_ip, INET_ADDRSTRLEN);
            int client_port = ntohs(client_addr.sin_port);

            close(client_fd);
        }
    });

    std::thread refresh_thread([refresh_interval]() {
        while (true) {
            {
                std::lock_guard<std::mutex> lock(clients_mutex);
                std::cout << "Odświeżanie listy klientów:" << std::endl;
                for (const auto &client : connected_clients) {
                    std::cout << "Adres: " << std::get<0>(client) << ", Port wysyłania: " << std::get<1>(client)
                              << ", Port odbioru: " << std::get<2>(client) << std::endl;
                }
            }
            broadcast_clients_list();
            std::this_thread::sleep_for(std::chrono::seconds(refresh_interval));
        }
    });
    connection_thread.detach();
    refresh_thread.detach();
}

// Funkcja otwierająca nowe okno
void open_connection_manager_window() {
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;

    if (gtk_builder_add_from_file(builder, "../connection_manager/connection_manager.glade", &error) == 0) {
        g_printerr("Error loading file: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    GtkWidget *new_window = GTK_WIDGET(gtk_builder_get_object(builder, "connection_window"));
    g_signal_connect(new_window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    gtk_builder_connect_signals(builder, builder);

    GtkTreeView *clients_treeview = GTK_TREE_VIEW(gtk_builder_get_object(builder, "clients_address"));
    GtkListStore *list_store = gtk_list_store_new(3, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT); // Dodano trzecią kolumnę dla rec_port
    gtk_tree_view_set_model(clients_treeview, GTK_TREE_MODEL(list_store));
    g_object_unref(list_store);

    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("IP Address", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(clients_treeview, column);

    column = gtk_tree_view_column_new_with_attributes("Send Port", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(clients_treeview, column);

    column = gtk_tree_view_column_new_with_attributes("Receive Port", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(clients_treeview, column);

    gtk_widget_show_all(new_window);
}
