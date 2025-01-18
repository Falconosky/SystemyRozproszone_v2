/*
 * Created by ja on 16.01.25.
 */

#include "client_data_handling.h"
#include "client_threads.h"
#include <gtk/gtk.h>
#include <iostream>
#include <vector>
#include <mutex>
#include <tuple>
#include <string>

extern GtkBuilder *builder;
extern std::vector<std::tuple<std::string, int, int, int, std::string, int>> global_client_list; // IP, send_port, rec_port, client_id
extern std::mutex global_client_list_mutex;
extern std::map<int, std::string> process_states; // Mapowanie ID procesu na jego stan
extern std::mutex process_states_mutex;

struct CriticalSectionRequest {
    int timestamp;
    int process_id;
};

extern std::vector<CriticalSectionRequest> request_queue;
extern std::mutex request_queue_mutex;

std::map<int, std::string> process_states;
std::mutex process_states_mutex;

bool debug = false;

void update_process_statuses() {
    if (debug)
        std::cout << "[DEBUG] Updating process statuses." << std::endl;
    std::lock_guard<std::mutex> lock(global_client_list_mutex);

    int min_timestamp = INT_MAX;
    int next_process_id = -1;

    {
        std::lock_guard<std::mutex> queue_lock(request_queue_mutex);
        for (const auto& request : request_queue) {
            if (request.timestamp < min_timestamp) {
                min_timestamp = request.timestamp;
                next_process_id = request.process_id;
            }
        }
    }

    if (next_process_id != -1) {
        GtkWidget *next_process_label = GTK_WIDGET(gtk_builder_get_object(builder, "next_process"));
        std::string label_text = "P" + std::to_string(next_process_id);
        gtk_label_set_text(GTK_LABEL(next_process_label), label_text.c_str());
        if (debug)
            std::cout << "[DEBUG] Next process set to: " << label_text << std::endl;
    }

    for (auto& client : global_client_list) {
        int client_id = std::get<3>(client);
        std::string message = "Nieznany"; // Domyślny komunikat
        int timestamp = 0; // Domyślny timestamp

        {
            std::lock_guard<std::mutex> queue_lock(request_queue_mutex);
            auto it = std::find_if(request_queue.begin(), request_queue.end(), [client_id](const CriticalSectionRequest& req) {
                return req.process_id == client_id;
            });

            if (it != request_queue.end()) {
                message = "Prośba";
                timestamp = it->timestamp;
                if (debug)
                    std::cout << "[DEBUG] Process ID " << client_id << ": Status updated to " << message << ", Timestamp: " << timestamp << std::endl;
            } else {
                if (debug)
                    std::cout << "[DEBUG] Process ID " << client_id << ": No matching request in queue." << std::endl;
            }
        }

        std::get<4>(client) = message;
        std::get<5>(client) = timestamp;
    }
}

void update_critical_status_label(const std::string& status) {
    GtkWidget *critical_status_label = GTK_WIDGET(gtk_builder_get_object(builder, "critical_status"));
    gtk_label_set_text(GTK_LABEL(critical_status_label), status.c_str());
}

void update_lamport_clock_label() {
    GtkWidget *lamport_label = GTK_WIDGET(gtk_builder_get_object(builder, "lamport"));
    gtk_label_set_text(GTK_LABEL(lamport_label), std::to_string(lamport_clock).c_str());
}

void setup_treeview_columns(GtkTreeView *tree_view) {
    if (debug)
        std::cout << "[DEBUG] Setting up treeview columns." << std::endl;
    // Remove existing columns
    while (GtkTreeViewColumn *column = gtk_tree_view_get_column(tree_view, 0)) {
        gtk_tree_view_remove_column(tree_view, column);
    }

    // Add columns to the tree view
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Adres IP", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Port wysyłania", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Port odbioru", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("ID klienta", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Komunikat", renderer, "text", 4, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Znacznik czasu otrzymany w wiadomości", renderer, "text", 5, NULL);
    gtk_tree_view_append_column(tree_view, column);
    if (debug)
        std::cout << "[DEBUG] Treeview columns set up successfully." << std::endl;
}

void setup_treeview_columns_logs(GtkTreeView *tree_view) {
    if (debug)
        std::cout << "[DEBUG] Setting up treeview columns for logs." << std::endl;
    // Remove existing columns
    while (GtkTreeViewColumn *column = gtk_tree_view_get_column(tree_view, 0)) {
        gtk_tree_view_remove_column(tree_view, column);
    }

    // Add columns to the tree view
    GtkCellRenderer *renderer = gtk_cell_renderer_text_new();
    GtkTreeViewColumn *column = gtk_tree_view_column_new_with_attributes("Czas otrzymania", renderer, "text", 0, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Typ wiadomości", renderer, "text", 1, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Nadawca wiadomości", renderer, "text", 2, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Treść wiadomości", renderer, "text", 3, NULL);
    gtk_tree_view_append_column(tree_view, column);

    renderer = gtk_cell_renderer_text_new();
    column = gtk_tree_view_column_new_with_attributes("Dodatkowe informacje", renderer, "text", 4, NULL);
    gtk_tree_view_append_column(tree_view, column);
    if (debug)
        std::cout << "[DEBUG] Treeview columns for logs set up successfully." << std::endl;
}

void update_other_processes_view() {
    if (debug)
        std::cout << "[DEBUG] Updating other processes view." << std::endl;
    GtkWidget *tree_view = GTK_WIDGET(gtk_builder_get_object(builder, "other_processes"));
    GtkListStore *list_store = gtk_list_store_new(6, G_TYPE_STRING, G_TYPE_INT, G_TYPE_INT, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT);

    setup_treeview_columns(GTK_TREE_VIEW(tree_view));

    for (const auto &client : global_client_list) {
        GtkTreeIter iter;
        gtk_list_store_append(list_store, &iter);
        gtk_list_store_set(list_store, &iter,
                           0, std::get<0>(client).c_str(),
                           1, std::get<1>(client),
                           2, std::get<2>(client),
                           3, std::get<3>(client),
                           4, std::get<4>(client).c_str(),
                           5, std::get<5>(client),
                           -1);
    }

    gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(list_store));
    g_object_unref(list_store);
    if (debug)
        std::cout << "[DEBUG] Other processes view updated successfully." << std::endl;
}

void update_logs()
{
    auto update_function = [] (gpointer data) -> gboolean {
        if (debug)
            std::cout << "[DEBUG] Entering logs update function." << std::endl;
        GtkWidget *tree_view = GTK_WIDGET(gtk_builder_get_object(builder, "logs"));
        GtkListStore *list_store = gtk_list_store_new(5, G_TYPE_INT, G_TYPE_STRING, G_TYPE_INT, G_TYPE_STRING, G_TYPE_STRING);

        setup_treeview_columns_logs(GTK_TREE_VIEW(tree_view));

        for (const auto &client : logs) {
            GtkTreeIter iter;
            gtk_list_store_append(list_store, &iter);
            gtk_list_store_set(list_store, &iter,
                               0, std::get<0>(client),
                               1, std::get<1>(client).c_str(),
                               2, std::get<2>(client),
                               3, std::get<3>(client).c_str(),
                               4, std::get<4>(client).c_str(),
                               -1);
        }

        gtk_tree_view_set_model(GTK_TREE_VIEW(tree_view), GTK_TREE_MODEL(list_store));
        g_object_unref(list_store);

        if (debug)
            std::cout << "[DEBUG] Logs view updated successfully." << std::endl;
        return FALSE; // Usuwamy funkcję z pętli GTK po jej wykonaniu
    };

    g_idle_add(update_function, nullptr); // Dodajemy operację do głównego wątku GTK

    if (debug)
        std::cout << "[DEBUG] Queued logs update in GTK main thread." << std::endl;
}
