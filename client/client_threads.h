// client_threads.h
#ifndef CLIENT_THREADS_H
#define CLIENT_THREADS_H

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>

extern std::queue<std::string> messageQueue;
extern std::mutex queueMutex;
extern std::condition_variable queueCondVar;
extern bool running;

int initialize_connection(const std::string& ip_address, int port, int send_port);
void receive_thread_function(int sockfd);
void send_thread_function(int sockfd);

#endif // CLIENT_THREADS_H
