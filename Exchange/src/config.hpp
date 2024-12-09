#ifndef CONFIG_HPP
#define CONFIG_HPP

#include "../lib/json.hpp"
#include <iostream>
#include <fstream>

using json = nlohmann::json;
using namespace std;

struct Config {
   int dbPort;
   int exchangePort;
   string dbIp;
   string exchangeIp;
   vector<string> lots;
};

Config readConfig(string filename) {
    Config* cf = new Config();

    ifstream file(filename);

    json jsonSchema;
    file>>jsonSchema;

    cf->dbPort = jsonSchema["database_port"];
    cf->dbIp = jsonSchema["database_ip"];
    auto lots = jsonSchema["lots"]; 
    for (auto it = lots.begin(); it != lots.end(); it++) {
        cf->lots.push_back(it.value());
    }

    return *cf;
}

#endif