#include <gtk/gtk.h>
#include "connection_manager/connection_manager.h"
#include "client/client.h"
#include "main.h"

#include <iostream>
#include <string>

using namespace std;

// Funkcje obsługi kliknięcia dla przycisków
extern "C" void klient_click(GtkButton *button, gpointer user_data) {
    GtkWidget *current_window = GTK_WIDGET(user_data);
    gtk_widget_hide(current_window);

    // Otwórz nowe okno z connection_manager.cpp
    open_client_window();
}

extern "C" void serwer_click(GtkButton *button, gpointer user_data) {
    GtkWidget *current_window = GTK_WIDGET(user_data);
    gtk_widget_hide(current_window);

    // Otwórz nowe okno z connection_manager.cpp
    open_connection_manager_window();
}

// Funkcja zamykająca program
extern "C" void on_window_destroy() {
    gtk_main_quit();
}

int instance_id=0;

int main(int argc, char *argv[]) {
    if (argc > 1) {
        instance_id = stoi(argv[1]);
    } else {
        cout << "Brak argumentu identyfikatora instancji.\n";
        return 1;
    }

    // Inicjalizacja GTK
    gtk_init(&argc, &argv);

    // Tworzenie obiektu GtkBuilder i ładowanie pliku .glade
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;

    // Ładowanie pliku Glade
    if (gtk_builder_add_from_file(builder, "../design.glade", &error) == 0) {
        g_printerr("Error loading file: %s\n", error->message);
        g_clear_error(&error);
        return 1;
    }

    // Pobieranie głównego okna
    GtkWidget *window = GTK_WIDGET(gtk_builder_get_object(builder, "main_window"));
    g_signal_connect(window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    // Łączenie sygnałów z funkcjami
    gtk_builder_connect_signals(builder, window);

    // Wyświetlanie okna
    gtk_widget_show_all(window);

    // Wejście do głównej pętli GTK
    gtk_main();

    return 0;
}
