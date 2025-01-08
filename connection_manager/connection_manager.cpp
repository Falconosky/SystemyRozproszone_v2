#include <gtk/gtk.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>
#include <iostream>
#include "connection_manager.h"
#include "../main.h"

// Funkcja obsługi sygnału kliknięcia przycisku "Start serwer"
extern "C" void start_server_clicked(GtkButton *button, gpointer user_data) {
    int position = 3;
    // Pobierz rodzica przycisku (kontener) i usuń przycisk
    GtkWidget *parent = gtk_widget_get_parent(GTK_WIDGET(button));
    gtk_container_remove(GTK_CONTAINER(parent), GTK_WIDGET(button));

    GtkBuilder *builder = GTK_BUILDER(user_data);
    GtkEntry *ip_entry = GTK_ENTRY(gtk_builder_get_object(builder, "address_ip"));
    GtkEntry *port_entry = GTK_ENTRY(gtk_builder_get_object(builder, "address_port"));

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

    // Utwórz nową etykietę z napisem "Serwer włączony"
    std::string label_text = "Serwer włączony: " + std::string(ip_address) + ":" + std::to_string(port);
    GtkWidget *label = gtk_label_new(label_text.c_str());

    // Dodaj etykietę do tego samego kontenera
    gtk_box_pack_start(GTK_BOX(parent), label, TRUE, TRUE, 10);
    gtk_box_reorder_child(GTK_BOX(parent), label, position);

    // Wyświetl nową etykietę
    gtk_widget_show(label);

    g_print("Serwer nasłuchuje na %s:%d\n", ip_address, port);

    gtk_editable_set_editable(GTK_EDITABLE(ip_entry), FALSE);
    gtk_editable_set_editable(GTK_EDITABLE(port_entry), FALSE);
}

// Funkcja otwierająca nowe okno
void open_connection_manager_window() {
    // Inicjalizacja GtkBuilder
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;

    // Ładowanie pliku connection_manager.glade
    if (gtk_builder_add_from_file(builder, "../connection_manager/connection_manager.glade", &error) == 0) {
        g_printerr("Error loading file: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    // Pobierz główne okno z connection_manager.glade
    GtkWidget *new_window = GTK_WIDGET(gtk_builder_get_object(builder, "connection_window"));
    g_signal_connect(new_window, "destroy", G_CALLBACK(on_window_destroy), NULL);
    gtk_builder_connect_signals(builder, builder);

    // Sprawdź, czy okno zostało poprawnie załadowane
    if (!new_window) {
        g_printerr("Could not load the window from connection_manager.glade\n");
        return;
    }

    // Pokaż nowe okno
    gtk_widget_show_all(new_window);
}
