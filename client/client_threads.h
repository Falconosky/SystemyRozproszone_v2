// client_threads.h
#ifndef CLIENT_THREADS_H
#define CLIENT_THREADS_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <gtk/gtk.h>
#include <algorithm>

extern std::queue<std::string> messageQueue;
extern std::mutex queueMutex;
extern std::condition_variable queueCondVar;
extern bool running;
extern GtkBuilder* builder; // Deklaracja globalnej zmiennej
extern int lamport_clock;
extern std::vector<std::tuple<std::string, int, int, int, std::string, int>> global_client_list; // IP, send_port, rec_port, client_id, message, timestamp
extern std::vector<std::tuple<int, std::string, int, std::string, std::string>> logs; // Czas, Typ, Nadawca, Treść, Dodatkowe informacje
extern std::mutex global_client_list_mutex;
extern std::mutex logs_mutex;
extern std::vector<std::pair<int, std::pair<std::string, int>>> last_messages;
extern std::mutex last_messages_mutex;

int initialize_connection(const std::string& ip_address, int port, int send_port);
void receive_thread_function();
void send_thread_function(int sockfd);
void accept_request(GtkButton *button, gpointer user_data);
void critical_exit(GtkButton *button, gpointer user_data);
extern void send_request(GtkButton *button, gpointer user_data);

#endif // CLIENT_THREADS_H
