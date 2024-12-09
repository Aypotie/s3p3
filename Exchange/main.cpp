#include <iostream>
#include "src/config.hpp"
#include "src/storage.hpp"

using namespace std;

int main() {
    Config config = readConfig("config.json");
    Storage storage(config.dbIp, config.dbPort);

    storage.createStorage(config.lots);
    
}