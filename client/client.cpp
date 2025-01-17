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
#include <netinet/in.h>
#include <cstring>
#include <cstdlib>
#include <ctime>


using namespace std;

std::queue<std::string> messageQueue;
std::mutex queueMutex;
std::condition_variable queueCondVar;
bool running = true; // Ustawienie początkowej wartości
bool fullScreen = false;
GtkBuilder* builder = nullptr; // Globalna zmienna builder

void debug(string text)
{
    std::cerr << "Debug: " << text << std::endl;
}

void auto_accept_toggled(GtkToggleButton *toggle_button, gpointer user_data)
{
    GtkWidget *accept_button = GTK_WIDGET(gtk_builder_get_object(builder, "accept_request"));
    gboolean is_active = gtk_toggle_button_get_active(toggle_button);
    gtk_widget_set_sensitive(accept_button, !is_active);
}

void on_connect_button_clicked(GtkButton *button, gpointer user_data) {
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
    GtkWidget *send_port = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
    const gchar *send_port_text = gtk_entry_get_text(GTK_ENTRY(send_port));
    int sockfd = initialize_connection(ip_address, port, atoi(send_port_text));
    if (sockfd < 0) {
        g_printerr("Błąd podczas nawiązywania połączenia.\n");
        return;
    }

    // Uruchom wątki
    std::thread receive_thread(receive_thread_function);
    std::thread send_thread(send_thread_function, sockfd);

    // Odłącz wątki, aby działały niezależnie
    receive_thread.detach();
    send_thread.detach();
}

void insert_free_port(string objectName)
{
    srand(time(NULL));
    int random_port;
    int sockfd;
    sockaddr_in addr;

    while (true) {
        random_port = 10000 + rand() % (65535 - 10000); // Losowy port powyżej 10000
        sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            g_printerr("Nie udało się utworzyć gniazda.\n");
            return;
        }

        memset(&addr, 0, sizeof(addr));
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = INADDR_ANY;
        addr.sin_port = htons(random_port);

        if (bind(sockfd, (struct sockaddr*)&addr, sizeof(addr)) == 0) {
            close(sockfd);
            break; // Znaleziono wolny port
        }

        close(sockfd);
    }

    GtkWidget *entry_own_port = GTK_WIDGET(gtk_builder_get_object(builder, objectName.c_str()));
    char port_text[6];
    snprintf(port_text, sizeof(port_text), "%d", random_port);
    gtk_entry_set_text(GTK_ENTRY(entry_own_port), port_text);
}

void random_port1_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *entry_rec_port = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
    const gchar *rec_port_text = gtk_entry_get_text(GTK_ENTRY(entry_rec_port));
    int rec_port = atoi(rec_port_text);
    int own_port;
    do {
        insert_free_port("rec_port");
        GtkWidget *entry_own_port = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
        const gchar *own_port_text = gtk_entry_get_text(GTK_ENTRY(entry_own_port));
        own_port = atoi(own_port_text);
    } while (own_port == rec_port);
}

void random_port_clicked(GtkButton *button, gpointer user_data) {
    GtkWidget *entry_rec_port = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
    const gchar *rec_port_text = gtk_entry_get_text(GTK_ENTRY(entry_rec_port));
    int rec_port = atoi(rec_port_text);
    int own_port;
    do {
        insert_free_port("own_port");
        GtkWidget *entry_own_port = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
        const gchar *own_port_text = gtk_entry_get_text(GTK_ENTRY(entry_own_port));
        own_port = atoi(own_port_text);
    } while (own_port == rec_port);
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
    builder = gtk_builder_new();
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
        std::string address = getLineFromFile(addressFilePath, 1);
        if (!address.empty()) {
            GtkWidget *entry_address = GTK_WIDGET(gtk_builder_get_object(builder, "entry_address"));
            GtkWidget *entry_port = GTK_WIDGET(gtk_builder_get_object(builder, "entry_port"));

            ifstream config_file("../testfiles/config");
            string line;
            while (std::getline(config_file, line)) {
                // Szukaj linii zaczynającej się od "send_port="
                if (line.rfind("send_port=", 0) == 0) {
                    // Pobierz wartość po "send_port="
                    string port_value = line.substr(std::string("send_port=").length());
                    // Znajdź GtkEntry "own_port"
                    GtkWidget *entry = GTK_WIDGET(gtk_builder_get_object(builder, "own_port"));
                    // Ustaw wartość w GtkEntry
                    gtk_entry_set_text(GTK_ENTRY(entry), port_value.c_str());
                }
                if (line.rfind("rec_port=", 0) == 0) {
                    // Pobierz wartość po "send_port="
                    string port_value = line.substr(std::string("rec_port=").length());
                    // Znajdź GtkEntry "own_port"
                    GtkWidget *entry = GTK_WIDGET(gtk_builder_get_object(builder, "rec_port"));
                    // Ustaw wartość w GtkEntry
                    gtk_entry_set_text(GTK_ENTRY(entry), port_value.c_str());
                }
            }

            if (!entry_address || !entry_port) {
                std::cerr << "Nie udało się znaleźć pól wprowadzania" << std::endl;
            }
            std::string addressLine = getLineFromFile(addressFilePath, 1);
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
    connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "random_port"));
    g_signal_connect(connect_button, "clicked", G_CALLBACK(random_port_clicked), builder);
    connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "random_port1"));
    g_signal_connect(connect_button, "clicked", G_CALLBACK(random_port1_clicked), builder);
    connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "critical_section"));
    g_signal_connect(connect_button, "clicked", G_CALLBACK(send_request), builder);

    connect_button = GTK_WIDGET(gtk_builder_get_object(builder, "auto_accept"));
    g_signal_connect(connect_button, "toggled", G_CALLBACK(auto_accept_toggled), builder);


    // Pokaż nowe okno
    if (fullScreen)
        gtk_window_maximize(GTK_WINDOW(new_window));
    gtk_widget_show_all(new_window);
}
