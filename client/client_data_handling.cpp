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

void update_process_statuses() {
    std::lock_guard<std::mutex> lock(global_client_list_mutex);

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
                if (it->timestamp < lamport_clock) {
                    message = "Zgoda";
                } else {
                    message = "Prośba";
                }
                timestamp = it->timestamp;
            }
        }

        std::get<4>(client) = message;
        std::get<5>(client) = timestamp;
    }
}

void setup_treeview_columns(GtkTreeView *tree_view) {
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
    column = gtk_tree_view_column_new_with_attributes("Czas otrzymania komunikatu", renderer, "text", 5, NULL);
    gtk_tree_view_append_column(tree_view, column);
}

void setup_treeview_columns_logs(GtkTreeView *tree_view) {
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
}

void update_other_processes_view() {
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
}

void update_logs()
{
    auto update_function = [] (gpointer data) -> gboolean {
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

        return FALSE; // Usuwamy funkcję z pętli GTK po jej wykonaniu
    };

    g_idle_add(update_function, nullptr); // Dodajemy operację do głównego wątku GTK
}
