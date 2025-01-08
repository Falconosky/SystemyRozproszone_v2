#include <gtk/gtk.h>
#include "client.h"
#include "../main.h"
#include "../checkDebugConfig.h"
#include "client_threads.h"
#include "iostream"
#include <fstream>
#include <thread>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cstring>


using namespace std;

std::queue<std::string> messageQueue;
std::mutex queueMutex;
std::condition_variable queueCondVar;
bool running = true; // Ustawienie początkowej wartości

void on_connect_button_clicked(GtkButton *button, gpointer user_data) {
    GtkBuilder *builder = GTK_BUILDER(user_data);

    // Pobierz wartości z pól wprowadzania
    GtkWidget *entry_address = GTK_WIDGET(gtk_builder_get_object(builder, "entry_address"));
    GtkWidget *entry_port = GTK_WIDGET(gtk_builder_get_object(builder, "entry_port"));

    const gchar *ip_address = gtk_entry_get_text(GTK_ENTRY(entry_address));
    const gchar *port_text = gtk_entry_get_text(GTK_ENTRY(entry_port));

    if (strlen(ip_address) == 0 || strlen(port_text) == 0) {
        g_printerr("Adres IP i port muszą być podane.\n");
        return;
    }

    int port = atoi(port_text);
    if (port <= 0) {
        g_printerr("Nieprawidłowy port.\n");
        return;
    }

    // Inicjalizacja połączenia
    int sockfd = initialize_connection(ip_address, port);
    if (sockfd < 0) {
        g_printerr("Błąd podczas nawiązywania połączenia.\n");
        return;
    }

    // Uruchom wątki
    std::thread receive_thread(receive_thread_function, sockfd);
    std::thread send_thread(send_thread_function, sockfd);

    // Odłącz wątki, aby działały niezależnie
    receive_thread.detach();
    send_thread.detach();
}


// Funkcja pomocnicza do odczytu konkretnej linii z pliku
std::string getLineFromFile(const std::string& filepath, int lineNumber) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Nie można otworzyć pliku: " << filepath << std::endl;
        return "";
    }

    std::string line;
    int currentLine = 0;

    while (std::getline(file, line)) {
        if (++currentLine == lineNumber) {
            file.close();
            return line;
        }
    }

    file.close();
    std::cerr << "Plik " << filepath << " nie zawiera linii numer " << lineNumber << std::endl;
    return "";
}

std::pair<std::string, std::string> splitString(const std::string& str, char delimiter) {
    size_t pos = str.find(delimiter);
    if (pos != std::string::npos) {
        return {str.substr(0, pos), str.substr(pos + 1)};
    } else {
        return {str, ""};
    }
}

// Funkcja otwierająca nowe okno
void open_client_window() {
    // Inicjalizacja GtkBuilder
    GtkBuilder *builder = gtk_builder_new();
    GError *error = NULL;

    // Ładowanie pliku connecation_manager.glade
    if (gtk_builder_add_from_file(builder, "../client/client.glade", &error) == 0) {
        g_printerr("Error loading file: %s\n", error->message);
        g_clear_error(&error);
        return;
    }

    // Pobierz główne okno z connection_manager.glade
    GtkWidget *new_window = GTK_WIDGET(gtk_builder_get_object(builder, "connection_window"));
    g_signal_connect(new_window, "destroy", G_CALLBACK(on_window_destroy), NULL);

    // Sprawdź, czy okno zostało poprawnie załadowane
    if (!new_window) {
        g_printerr("Could not load the window from client.glade\n");
        return;
    }

    // Ustaw zakładkę "Połączenie" jako aktywną
    GtkWidget *notebook = GTK_WIDGET(gtk_builder_get_object(builder, "main_notebook"));
    GtkWidget *connection_tab = GTK_WIDGET(gtk_builder_get_object(builder, "tab2_box"));
    gint page_num = gtk_notebook_page_num(GTK_NOTEBOOK(notebook), connection_tab);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(notebook), page_num);

    int liniaDoSprawdzenia = 1; // Pierwsza niekomentarzowa linia
    if (checkDebugConfig(liniaDoSprawdzenia)) {
        // Ścieżka do pliku z adresami
        std::string addressFilePath = "../testfiles/address.txt";
        // Pobranie odpowiedniej linii na podstawie instance_id
        std::string address = getLineFromFile(addressFilePath, instance_id);
        if (!address.empty()) {
            GtkWidget *entry_address = GTK_WIDGET(gtk_builder_get_object(builder, "entry_address"));
            GtkWidget *entry_port = GTK_WIDGET(gtk_builder_get_object(builder, "entry_port"));
            if (!entry_address || !entry_port) {
                std::cerr << "Nie udało się znaleźć pól wprowadzania" << std::endl;
            }
            std::string addressLine = getLineFromFile(addressFilePath, instance_id);
            if (!addressLine.empty()) {
                // Podział linii na adres IP i port
                auto [ip_address, port] = splitString(addressLine, ':');

                // Ustawienie wartości w polach wprowadzania
                gtk_entry_set_text(GTK_ENTRY(entry_address), ip_address.c_str());
                gtk_entry_set_text(GTK_ENTRY(entry_port), port.c_str());
            } else {
                std::cerr << "Nie udało się pobrać linii numer " << instance_id << " z pliku " << addressFilePath << std::endl;
            }
        } else {
            g_printerr("Nie udało się pobrać linii numer %d z pliku %s\n", instance_id, addressFilePath.c_str());
        }
    }

    GtkWidget *connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "connect_to_server"));
    g_signal_connect(connect_button, "clicked", G_CALLBACK(on_connect_button_clicked), builder);


    // Pokaż nowe okno
    gtk_window_maximize(GTK_WINDOW(new_window));
    gtk_widget_show_all(new_window);
}
