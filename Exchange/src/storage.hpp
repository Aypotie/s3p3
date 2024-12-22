#ifndef STORAGE_HPP
#define STORAGE_HPP

#include <string>
#include <vector>
#include <map>
#include <stdexcept>
#include <iostream>
#include <cstring>   // Для memset
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>  // Для close()

using namespace std;

class Storage {
public:
    Storage(string host, int port) : host(host), port(port) {}

    vector<string> split(const string& str, char delimiter) {
        vector<string> tokens;
        stringstream ss(str);
        string token;

        while (getline(ss, token, delimiter)) {
            tokens.push_back(token);
        }

        return tokens;
    }

    vector<map<string, string>> Select(const string& query) {
        int sockfd = dbConnect();
        sockSend(sockfd, query);
        string response = sockReceive(sockfd);

        vector<map<string, string>> result;

        vector<string> strs = split(response, '\n');
        int len = strs.size();
        if (strs[len - 1] == "Command executed successfully") {
            vector<string> headers = split(strs[0], ',');

            for (int i = 1; i < len - 1; i++) {
                vector<string> cols = split(strs[i], ',');
                if (cols.size() != headers.size()) {
                    throw runtime_error("Error: row length not equal header length");
                }
                map<string, string> row;
                for (int j = 0; j < headers.size(); j++) {
                    row[headers[j]] = cols[j];
                }
                result.push_back(row);
            }
        } else {
            throw runtime_error("Error: " + strs[0]);
        }

        return result;
    }

    void Insert(const string& query) {
        int sockfd = dbConnect();
        sockSend(sockfd, query);
        string response = sockReceive(sockfd);

        if (response != "Command executed successfully\n") {
            throw runtime_error("Error: " + response);
        }
    }

    void Delete(const string& query) {
        int sockfd = dbConnect();
        sockSend(sockfd, query);
        string response = sockReceive(sockfd);

        if (response != "Command executed successfully\n") {
            throw runtime_error("Error: " + response);
        }
    }

    void createStorage(vector<string> lotsToCreate) {
        makeLots(lotsToCreate);
        makePairs(lotsToCreate);
    }

private:
    string host;
    int port;

    int dbConnect() {
        int sockfd = socket(AF_INET, SOCK_STREAM, 0);
        if (sockfd < 0) {
            throw runtime_error("Failed to create socket");
        }

        struct sockaddr_in server_addr{};
        memset(&server_addr, 0, sizeof(server_addr));
        server_addr.sin_family = AF_INET;
        server_addr.sin_port = htons(port);

        if (inet_pton(AF_INET, host.c_str(), &server_addr.sin_addr) <= 0) {
            throw runtime_error("Invalid address or address not supported");
        }

        if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) < 0) {
            throw runtime_error("Connection failed");
        }

        return sockfd;
    }

    void sockSend(int sockfd, const string& query) {
        if (send(sockfd, query.c_str(), query.size(), 0) < 0) {
            throw runtime_error("Failed to send data");
        }
    }

    string sockReceive(int sockfd) {
        constexpr size_t BUFFER_SIZE = 20000;
        char buffer[BUFFER_SIZE];
        string result;

        while (true) {
            memset(buffer, 0, sizeof(buffer));
            int bytes_read = read(sockfd, buffer, sizeof(buffer) - 1);
            if (bytes_read < 0) {
                throw runtime_error("Failed to receive data");
            }
            if (bytes_read == 0) {
                break; // Прерываем цикл, если данные закончились
            }
            result.append(buffer, bytes_read);

            // Проверяем, завершен ли ответ (например, по соглашению о наличии терминатора)
            if (result.find("Command executed successfully\n") != string::npos) {
                break;
            }
        }
        return result;
    }


    void makeLots(vector<string> lotsToCreate) {
        auto lots = Select("SELECT lot.name FROM lot");
        if (lots.empty()) {
            for (auto lot : lotsToCreate) {
                Insert("INSERT INTO lot VALUES ('" + lot + "')");
            }
        }
    }

    void makePairs(vector<string> lots) {
        auto pairs = Select("SELECT pair.pair_pk FROM pair");
        if (pairs.empty()) {
            for (int i = 0; i < lots.size(); i++) {
                for (int j = 0; j < lots.size(); j++) {
                    if (i != j) {
                        string query = "INSERT INTO pair VALUES ('" + to_string(i + 1) + "','" + to_string(j + 1) + "')";
                        Insert(query);
                    }
                }
            }
        }
    }
};

#endif
