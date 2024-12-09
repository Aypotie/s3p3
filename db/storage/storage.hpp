#ifndef STORAGE_H
#define STORAGE_H

#include <iostream>
#include <fstream>
#include <cstdio>
#include <filesystem>
#include <regex>
#include <mutex>
#include "../data_structures/vector.hpp"
#include "../data_structures/map.hpp"
#include "../schema/schema.hpp"
#include "./file.hpp"
#include "./condition.hpp"

using namespace std;

namespace fs = filesystem;

struct Storage {
    Schema schema;
    Map<shared_ptr<mutex>> tableMutex;

    Storage(Schema sc) {
        schema = sc;

        Vector<string> tableNames = schema.structure.keys();
        for (int i = 0; i < tableNames.size(); i++) {
            auto mtx = make_shared<mutex>();
            tableMutex.put(tableNames.get(i), mtx);
        }

        createStructure();
    }

    void writeList(string path, string tableName, Vector<string> cols) {
        ofstream file(path);

        file << tableName << "_pk,";
        for (int i = 0; i < cols.size(); i++) {
            if (i == cols.size() - 1) {
                file << cols.get(i);
            } else {
                file << cols.get(i) << ",";
            }
        }
        file << endl;

        file.close();
    }

    // директории создает
    void createStructure() {
        if (!fs::exists(schema.name)) {
            fs::create_directory(schema.name);
        } else {
            cerr << "Storage directory " << schema.name << " already exists" << endl; 
        }
        Vector<string> tables = schema.structure.keys();
        for (int i = 0; i < tables.size(); i++) {
            string tableName = tables.get(i);
            Vector<string> cols = schema.structure.get(tableName);
            string tablePath = schema.name + "/" + tableName;
            if (!fs::exists(tablePath)) {
                fs::create_directory(tablePath);
                
            } else {
                cerr << "Table directory " << tablePath << " already exists" << endl; 
            }

            string pagePath = tablePath + "/1.csv";
            if (!fs::exists(pagePath)) {
                writeList(pagePath, tableName, cols);

                string pkSequencePath = tablePath + "/" + tableName + "_pk_sequence"; // создание файлов pk_sequence
                ofstream pkSeqFile(pkSequencePath);
                pkSeqFile << 0;
                pkSeqFile.close();

                string lockPath = tablePath + "/" + tableName + "_lock"; // создание файлов lock
                ofstream lockFile(lockPath);
                lockFile << 0;
                lockFile.close();
            } else {
                cerr << "Page " << pagePath << " already exists" << endl;
            }
        }
    }

    void insert(string baseDir, string tableName, Vector<string> values) {
        lock_guard<mutex> lock(*tableMutex.get(tableName)); // блокировка таблицы

        string tablePath = baseDir + "/" + tableName;
        Vector<string> pages = getCSVFromDir(tablePath);

        for (int i = 0; i < pages.size(); i++) {
            string pagePath = pages.get(i);
            if (countStringsInFile(pagePath) - 1 < schema.tuplesLimit) {
                AddRowInCSV(pagePath, tablePath, tableName, values);
                return;
            }
        }
        string newPagePath = tablePath + "/" + to_string(pages.size()+1) + ".csv";
        writeList(newPagePath, tableName, schema.structure.get(tableName));
        AddRowInCSV(newPagePath, tablePath, tableName, values);
    }

    void fDelete(Vector<string> tables, string condition) {
        if (condition == "") {
            for (int i = 0; i < tables.size(); i++) {
                string tableName = tables.get(i);
                lock_guard<mutex> lock(*tableMutex.get(tableName)); // блокировка таблицы

                Vector<string> pages = getCSVFromDir(schema.name + "/" + tableName);
                for (int j = 0; j < pages.size(); j++) {
                    cout << pages.get(j) << endl;
                    fs::remove(pages.get(j));
                }
                string pagePath = schema.name + "/" + tableName + "/1.csv";
                writeList(pagePath, tableName, schema.structure.get(tableName));
            }
        } else {
            Node* node = getConditionTree(condition);
            for (int i = 0; i < tables.size(); i++) {
                string tableName = tables.get(i);
                lock_guard<mutex> lock(*tableMutex.get(tableName)); // блокировка таблицы

                Vector<string> pages = getCSVFromDir(schema.name + "/" + tableName);
                for (int j = 0; j < pages.size(); j++) {
                    string pagePath = pages.get(j);
                    Vector<Vector<string>> page = readCSV(pagePath);
                    Vector<string> header = page.get(0);
                    int pageLen = page.size();
                    for (size_t k = 1; k < pageLen; k++) {
                        if (isValidRow(node, page.get(k), header, tables, tableName)) {
                            page.remove(k);
                            pageLen--;
                            k--;
                        }
                    }
                    writeCSV(pagePath, page);
                }
            }
        }
    }

    void select(int clientSocket, Vector<string> columns, Vector<string> tables, string condition) {
    string output;

    // Если нет условия WHERE
    if (condition.empty()) {
        if (tables.size() == 1) { // Одиночная таблица
            string tableName = tables.get(0);
            Vector<string> availableColumns = schema.structure.get(tableName);
            availableColumns.insert(0, tableName + "_pk");
            Vector<string> columnsToSelect;

            for (int i = 0; i < columns.size(); i++) {
                string fullColumn = columns.get(i);
                size_t dotPos = fullColumn.find(".");
                if (dotPos == string::npos) {
                    output = "Error: Column '" + fullColumn + "' is not in 'table.column' format.\n";
                    send(clientSocket, output.c_str(), output.size(), 0);
                    return;
                }

                string requestedTable = fullColumn.substr(0, dotPos);
                string requestedColumn = fullColumn.substr(dotPos + 1);

                if (requestedTable != tableName) {
                    output = "Error: Column '" + fullColumn + "' does not belong to table '" + tableName + "'.\n";
                    send(clientSocket, output.c_str(), output.size(), 0);
                    return;
                }

                if (availableColumns.find(requestedColumn) == -1) {
                    cout << requestedColumn << endl;
                    output = "Error: Column '" + requestedColumn + "' does not exist in table '" + tableName + "'.\n";
                    send(clientSocket, output.c_str(), output.size(), 0);
                    return;
                }

                columnsToSelect.pushBack(requestedColumn);
            }

            Vector<string> pages = getCSVFromDir(schema.name + "/" + tableName);
            for (int i = 0; i < pages.size(); i++) {
                string pagePath = pages.get(i);
                Vector<Vector<string>> page = readCSV(pagePath);
                Vector<string> header = page.get(0);

                Vector<int> columnIndexes;
                for (int j = 0; j < columnsToSelect.size(); j++) {
                    string col = columnsToSelect.get(j);
                    int index = header.find(col);
                    if (index != -1) {
                        columnIndexes.pushBack(index);
                    }
                }

                output += columns.join() + '\n';
                for (int j = 1; j < page.size(); j++) {
                    Vector<string> row = page.get(j);
                    for (int k = 0; k < columnIndexes.size(); k++) {
                        output += row.get(columnIndexes.get(k));
                        if (k < columnIndexes.size() - 1) {
                            output += ", ";
                        }
                    }
                    output += "\n";
                }
                send(clientSocket, output.c_str(), output.size(), 0);
                output.clear();
            }
        } else { 
            // Ветка обработки для случая с несколькими таблицами без условия WHERE (Декартово произведение)
            Vector<Vector<Vector<string>>> tablesData;
            Vector<Vector<int>> columnIndexesList;

            for (int i = 0; i < tables.size(); i++) {
                string tableName = tables.get(i);
                Vector<string> availableColumns = schema.structure.get(tableName);
                availableColumns.insert(0, tableName + "_pk");
                Vector<string> columnsToSelect;

                for (int j = 0; j < columns.size(); j++) {
                    string fullColumn = columns.get(j);
                    size_t dotPos = fullColumn.find(".");
                    if (dotPos == string::npos) {
                        output = "Error: Column '" + fullColumn + "' is not in 'table.column' format.\n";
                        send(clientSocket, output.c_str(), output.size(), 0);
                        return;
                    }

                    string requestedTable = fullColumn.substr(0, dotPos);
                    string requestedColumn = fullColumn.substr(dotPos + 1);

                    if (requestedTable == tableName) {
                        if (availableColumns.find(requestedColumn) == -1) {
                            output = "Error: Column '" + requestedColumn + "' does not exist in table '" + tableName + "'.\n";
                            send(clientSocket, output.c_str(), output.size(), 0);
                            return;
                        }
                        columnsToSelect.pushBack(requestedColumn);
                    }
                }

                Vector<Vector<string>> tableRows;
                Vector<string> pages = getCSVFromDir(schema.name + "/" + tableName);
                for (int i = 0; i < pages.size(); i++) {
                    string pagePath = pages.get(i);
                    Vector<Vector<string>> page = readCSV(pagePath);
                    Vector<string> header = page.get(0);

                    Vector<int> columnIndexes;
                    for (int j = 0; j < columnsToSelect.size(); j++) {
                        string col = columnsToSelect.get(j);
                        int index = header.find(col);
                        if (index != -1) {
                            columnIndexes.pushBack(index);
                        }
                    }
                    columnIndexesList.pushBack(columnIndexes);

                    for (int j = 1; j < page.size(); j++) {
                        tableRows.pushBack(page.get(j));
                    }
                }
                tablesData.pushBack(tableRows);
            }

            // Выполнение декартова произведения данных
            Vector<Vector<string>> crossProduct;
            Vector<string> currentCombination;

            function<void(int)> generateCombinations = [&](int depth) {
                if (depth == tables.size()) {
                    crossProduct.pushBack(currentCombination.copy());
                    return;
                }

                Vector<Vector<string>> tableData = tablesData.get(depth);
                for (int i = 0; i < tableData.size(); i++) {
                    Vector<string> row = tableData.get(i);
                    Vector<int> columnIndexes = columnIndexesList.get(depth);
                    for (int j = 0; j < columnIndexes.size(); j++) {
                        currentCombination.pushBack(row.get(columnIndexes.get(j)));
                    }
                    generateCombinations(depth + 1);
                    int newLen = currentCombination.size() - columnIndexes.size();
                    currentCombination.resize(newLen);
                }
            };

            generateCombinations(0);

            // Отправка результатов
            output += columns.join() + '\n';
            for (int i = 0; i < crossProduct.size(); i++) {
                Vector<string> combination = crossProduct.get(i);
                for (int j = 0; j < combination.size(); j++) {
                    output += combination.get(j);
                    if (j < combination.size() - 1) {
                        output += ", ";
                    }
                }
                output += "\n";
            }
            send(clientSocket, output.c_str(), output.size(), 0);
            output.clear();
        }
    } else { // с WHERE
            Node* node = getConditionTree(condition);
            if (tables.size() == 1) { // для одной таблицы
                string tableName = tables.get(0);

                // Проверяем, что все колонки относятся к таблице
                Vector<string> availableColumns = schema.structure.get(tableName);
                availableColumns.insert(0, tableName + "_pk");
                Vector<string> columnsToSelect;

                for (int i = 0; i < columns.size(); i++) {
                    string fullColumn = columns.get(i);
                    // Разделяем строку на таблицу и колонку (формат таблица.колонка)
                    size_t dotPos = fullColumn.find(".");
                    if (dotPos == string::npos) {
                        output = "Error: Column '" + fullColumn + "' is not in 'table.column' format" + ".\n";
                        send(clientSocket, output.c_str(), output.size(), 0);
                        return;
                    }

                    string requestedTable = fullColumn.substr(0, dotPos);
                    string requestedColumn = fullColumn.substr(dotPos + 1);

                    // Проверяем, что таблица совпадает с той, что указана в запросе
                    if (requestedTable != tableName) {
                        output = "Error: Column '" + fullColumn + "' does not belong to table '" + tableName + "'.\n";
                        send(clientSocket, output.c_str(), output.size(), 0);
                        return;
                    }

                    // Проверяем, что колонка существует в таблице
                    if (availableColumns.find(requestedColumn) == -1) {
                        output = "Error: Column '" + requestedColumn + "' does not exist in table '" + tableName + "'.\n";
                        send(clientSocket, output.c_str(), output.size(), 0);
                        return;
                    }

                    // Добавляем колонку в список для вывода
                    columnsToSelect.pushBack(requestedColumn);
                }

                // Теперь читаем CSV-файлы и выводим только запрошенные колонки
                Vector<string> pages = getCSVFromDir(schema.name + "/" + tableName);
                for (int i = 0; i < pages.size(); i++) {
                    string pagePath = pages.get(i);
                    Vector<Vector<string>> page = readCSV(pagePath);
                    Vector<string> header = page.get(0);

                    // Определяем индексы нужных колонок
                    Vector<int> columnIndexes;
                    for (int j = 0; j < columnsToSelect.size(); j++) {
                        string col = columnsToSelect.get(j);
                        int index = header.find(col);
                        if (index != -1) {
                            columnIndexes.pushBack(index);
                        }
                    }
                    for (int j = 0; j < page.size(); j++) {
                        Vector<string> row = page.get(j);
                        // Выводим только те колонки, которые были запрошены
                        if (isValidRow(node, row, header, tables, tableName)) {
                            for (int k = 0; k < columnIndexes.size(); k++) {
                                output += row.get(columnIndexes.get(k));
                                if (k < columnIndexes.size() - 1) {
                                    output += ", ";
                                }
                            }
                            output += '\n';
                        }
                    }
                }
                send(clientSocket, output.c_str(), output.size(), 0);
            } else { // для нескольких
                Vector<Vector<Vector<string>>> tablesData;
                Vector<Vector<int>> columnIndexesList;

                // Собираем данные для всех таблиц
                for (int i = 0; i < tables.size(); i++) {
                    string tableName = tables.get(i);
                    Vector<string> availableColumns = schema.structure.get(tableName);
                    availableColumns.insert(0, tableName + "_pk");
                    Vector<string> columnsToSelect;

                    // Определяем, какие колонки нужно выбирать из каждой таблицы
                    for (int j = 0; j < columns.size(); j++) {
                        string fullColumn = columns.get(j);
                        size_t dotPos = fullColumn.find(".");
                        if (dotPos == string::npos) {
                            output = "Error: Column '" + fullColumn + "' is not in 'table.column' format.\n";
                            send(clientSocket, output.c_str(), output.size(), 0);
                            return;
                        }

                        string requestedTable = fullColumn.substr(0, dotPos);
                        string requestedColumn = fullColumn.substr(dotPos + 1);

                        if (requestedTable == tableName) {
                            if (availableColumns.find(requestedColumn) == -1) {
                                output = "Error: Column '" +requestedColumn + "' does not exist in table '" + tableName + "'.\n";
                                send(clientSocket, output.c_str(), output.size(), 0);
                                return;
                            }
                            columnsToSelect.pushBack(requestedColumn);
                        }
                    }

                    // Читаем CSV-файлы таблицы и собираем строки
                    Vector<Vector<string>> tableRows;
                    Vector<string> pages = getCSVFromDir(schema.name + "/" + tableName);
                    for (int i = 0; i < pages.size(); i++) {
                        string pagePath = pages.get(i);
                        Vector<Vector<string>> page = readCSV(pagePath);
                        Vector<string> header = page.get(0);

                        // Определяем индексы нужных колонок
                        Vector<int> columnIndexes;
                        for (int j = 0; j < columnsToSelect.size(); j++) {
                            string col = columnsToSelect.get(j);
                            int index = header.find(col);
                            if (index != -1) {
                                columnIndexes.pushBack(index);
                            }
                        }
                        columnIndexesList.pushBack(columnIndexes);

                        for (int k = 1; k < page.size(); k++) {
                            Vector<string> row = page.get(k);
                            if (isValidRow(node, row, header, tables, tableName)) {
                                tableRows.pushBack(row);
                            }
                        }
                    }
                    tablesData.pushBack(tableRows);
                }

                // Выполняем декартово произведение данных
                Vector<Vector<string>> crossProduct;
                Vector<string> currentCombination;

                function<void(int)> generateCombinations = [&](int depth) {
                    if (depth == tables.size()) {
                        crossProduct.pushBack(currentCombination.copy());
                        return;
                    }

                    Vector<Vector<string>> tableData = tablesData.get(depth);
                    for (int i = 0; i < tableData.size(); i++) {
                        Vector<string> row = tableData.get(i);
                        Vector<int> columnIndexes = columnIndexesList.get(depth);
                        for (int j = 0; j < columnIndexes.size(); j++) {
                            currentCombination.pushBack(row.get(columnIndexes.get(j)));
                        }
                        generateCombinations(depth + 1);
                        int newLen = currentCombination.size() - columnIndexes.size();
                        currentCombination.resize(newLen);
                    }
                };

                generateCombinations(0);

                // Отправка результатов
                output += columns.join() + '\n';
                for (int i = 0; i < crossProduct.size(); i++) {
                    Vector<string> combination = crossProduct.get(i);
                    for (int j = 0; j < combination.size(); j++) {
                        output += combination.get(j);
                        if (j < combination.size() - 1) {
                            output += ", ";
                        }
                    }
                    output += "\n";
                }
                send(clientSocket, output.c_str(), output.size(), 0);
                output.clear();
            }
        }
    }
};
#endif
