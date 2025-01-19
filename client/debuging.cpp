//
// Created by ja on 19.01.25.
//

#include "debuging.h"

bool t_debug_last_messages = false;
bool szczegolowy_debug = false;

void debug_last_messages() {
    if (t_debug_last_messages)
    {
        std::lock_guard<std::mutex> lock(last_messages_mutex);
        std::cout<<std::endl << "[DEBUG] Current state of last_messages:" << std::endl;
        int index = 0;
        for (const auto& entry : last_messages) {
            std::cout << "Index: " << index
                      << ", Process ID: " << entry.first
                      << ", Message: " << entry.second.first
                      << ", Timestamp: " << entry.second.second <<std::endl;
            ++index;
        }
    }
}

void textdebug(std::string text)
{
    if (szczegolowy_debug)
    {
        std::cout<<"\t[DEBUG] - "<<text<<std::endl;
    }
}

void textdebug2(std::string text)
{
    if (szczegolowy_debug)
    {
        std::cout<<"\t\t[DEBUG] - "<<text<<std::endl;
    }
}