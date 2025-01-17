//
// Created by ja on 16.01.25.
//

#ifndef CLIENT_DATA_HANDLING_H
#define CLIENT_DATA_HANDLING_H

#include <gtk/gtk.h>
#include <map>
#include <string>
#include <mutex>

extern std::map<int, std::string> process_states;
extern std::mutex process_states_mutex;


void setup_treeview_columns(GtkTreeView *tree_view);
void update_other_processes_view();
void update_process_statuses();

#endif //CLIENT_DATA_HANDLING_H
