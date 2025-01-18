//
// Created by ja on 16.01.25.
//

#include "connection_initialize.h"

int initialize_connection(const std::string& ip_address, int port, int send_port) {
    try
    {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            std::cerr << "Nie można utworzyć gniazda." << std::endl;
            return -1;
        }

        // Pobranie portu z pola GtkEntry
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

        // Pobranie ID procesu z pola GtkEntry lub checkboxa
        GtkWidget *entry_process_id = GTK_WIDGET(gtk_builder_get_object(builder, "process_id"));
        const gchar *process_id_text = gtk_entry_get_text(GTK_ENTRY(entry_process_id));
        int process_id = atoi(process_id_text);

        GtkWidget *check_random_id = GTK_WIDGET(gtk_builder_get_object(builder, "random_id"));
        gboolean random_id_checked = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(check_random_id));

        if (random_id_checked) {
            process_id = 0; // Jeśli checkbox jest zaznaczony, ID ustawiamy na 0
        } else if (process_id < 1 || process_id > 99) {
            std::cerr << "Nieprawidłowe ID procesu." << std::endl;
            close(sockfd);
            return -1;
        }

        // Ustawianie lokalnego adresu i port
        sockaddr_in client_addr;
        std::memset(&client_addr, 0, sizeof(client_addr));
        client_addr.sin_family = AF_INET;
        client_addr.sin_addr.s_addr = INADDR_ANY; // Dowolny lokalny adres IP
        client_addr.sin_port = htons(send_port);  // Ustawienie portu wysyłającego

        if (bind(sockfd, (struct sockaddr*)&client_addr, sizeof(client_addr)) < 0) {
            std::cerr << "Nie udało się związać gniazda z portem " << send_port << "." << std::endl;
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
            close(sockfd);
            return -1;
        }

        // Wysyłanie informacji inicjalizacyjnej do serwera
        std::string init_message = "I:" + ip_address + ":" + std::to_string(send_port) + ":" + std::to_string(rec_port) + ":" + std::to_string(process_id) + ";";
        if (send(sockfd, init_message.c_str(), init_message.size(), 0) < 0) {
            std::cerr << "Błąd podczas wysyłania informacji inicjalizacyjnej do serwera." << std::endl;
            close(sockfd);
            return -1;
        }

        std::cout << "Połączono z serwerem: " << ip_address << ":" << port
                  << " i wysłano informacje inicjalizacyjne: "
                  << "IP: " << ip_address << ", Send Port: " << send_port << ", Rec Port: " << rec_port << ", Process ID: " << process_id << std::endl;

        // Zablokuj elementy interfejsu
        GtkWidget *check_random_id2 = GTK_WIDGET(gtk_builder_get_object(builder, "random_id"));
        gtk_widget_set_sensitive(check_random_id2, FALSE);

        GtkWidget *entry_process_id2 = GTK_WIDGET(gtk_builder_get_object(builder, "process_id"));
        gtk_widget_set_sensitive(entry_process_id2, FALSE);

        GtkWidget *entry_address = GTK_WIDGET(gtk_builder_get_object(builder, "entry_address"));
        gtk_widget_set_sensitive(entry_address, FALSE);

        GtkWidget *entry_port = GTK_WIDGET(gtk_builder_get_object(builder, "entry_port"));
        gtk_widget_set_sensitive(entry_port, FALSE);

        GtkWidget *own_port2 = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
        gtk_widget_set_sensitive(own_port2, FALSE);

        GtkWidget *rec_port2 = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
        gtk_widget_set_sensitive(rec_port2, FALSE);

        GtkWidget *random_port_button = GTK_WIDGET(gtk_builder_get_object(builder, "random_port"));
        gtk_widget_set_sensitive(random_port_button, FALSE);

        GtkWidget *random_port1_button = GTK_WIDGET(gtk_builder_get_object(builder, "random_port1"));
        gtk_widget_set_sensitive(random_port1_button, FALSE);

        GtkWidget *button2 = GTK_WIDGET(gtk_builder_get_object(builder, "connect_to_server"));
        gtk_widget_hide(GTK_WIDGET(button2)); // Ukryj przycisk connect_to_server

        GtkWidget *notebook = GTK_WIDGET(gtk_builder_get_object(builder, "main_notebook"));
        if (notebook) {
            gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), 0); // Ustawia pierwszą zakładkę jako aktywną
        } else {
            g_printerr("Nie znaleziono widgetu main_notebook\n");
        }


        close(sockfd); // Zamykanie gniazda wysyłającego, port jest gotowy do dalszego użycia
        std::cout << "[INFO] Port wysyłający " << send_port << " jest teraz dostępny." << std::endl;

        return 0; // Zwracanie sukcesu
    } catch (const std::exception& e) {
        std::cerr << "[ERROR] " << e.what() << std::endl;
        throw;
    }
}