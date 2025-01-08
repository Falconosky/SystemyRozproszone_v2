//
// Created by ja on 07.11.24.
//

#include "checkDebugConfig.h"
#include <iostream>
#include <fstream>
#include <string>

std::string trim(const std::string& str) {
    auto start = str.begin();
    while (start != str.end() && std::isspace(*start)) {
        ++start;
    }

    auto end = str.end();
    do {
        --end;
    } while (std::distance(start, end) > 0 && std::isspace(*end));

    return std::string(start, end + 1);
}

bool checkDebugConfig(int targetLine) {
    const std::string filename = "../debug.txt";
    std::ifstream configFile(filename);
    if (!configFile.is_open()) {
        std::cerr << "Nie można otworzyć pliku " << filename << "\n";
        return false;
    }

    std::string line;
    int currentLine = 0;

    while (std::getline(configFile, line)) {
        // Pomijanie linii komentarzy
        line = trim(line);
        if (line.empty() || line[0] == '#') {
            continue;
        }

        currentLine++;

        // Sprawdzanie, czy to jest docelowa linia
        if (currentLine == targetLine) {
            // Sprawdzanie, czy linia zawiera dokładnie cyfrę '1'
            if (line == "1") {
                configFile.close();
                return true;
            } else {
                configFile.close();
                return false;
            }
        }
    }

    // Jeśli nie znaleziono docelowej linii
    std::cerr << "Plik nie zawiera wystarczającej liczby niekomentarzowych linii.\n";
    configFile.close();
    return false;
}

