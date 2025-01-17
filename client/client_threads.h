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

int initialize_connection(const std::string& ip_address, int port, int send_port);
void receive_thread_function();
void send_thread_function(int sockfd);

#endif // CLIENT_THREADS_H
