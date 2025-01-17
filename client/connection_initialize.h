//
// Created by ja on 16.01.25.
//

#ifndef CONNECTION_INITIALIZE_H
#define CONNECTION_INITIALIZE_H

#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <gtk/gtk.h>
#include <vector>
#include "client_data_handling.h"
#include "client_threads.h"

int initialize_connection(const std::string& ip_address, int port, int send_port);

#endif //CONNECTION_INITIALIZE_H
