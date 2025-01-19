#ifndef CONNECTION_MANAGER_H
#define CONNECTION_MANAGER_H

#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>

extern GtkBuilder* builder; // Deklaracja globalnej zmiennej
// Deklaracja funkcji otwierającej nowe okno
void open_connection_manager_window();

#endif // CONNECTION_MANAGER_H
