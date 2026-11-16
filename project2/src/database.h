// database.h - Connection config and the small set of DB functions used
// by main.cpp. The application keeps a single active connection at a time
// (either MySQL C API or ODBC) inside main.cpp.
#ifndef DATABASE_H
#define DATABASE_H

#include <string>
#include <vector>

struct DBConfig {
    std::string host        = "127.0.0.1";
    unsigned    port        = 3306;
    std::string user        = "admin";
    std::string password    = "1234";
    std::string database    = "CarManufacturingDB";
    std::string odbc_driver = "MySQL ODBC 9.7 Unicode Driver";
};

enum class Backend { MYSQL_C_API, ODBC };

bool    dbConnect(Backend b, const DBConfig& cfg);
void    dbDisconnect();
bool    dbIsConnected();
Backend dbBackend();

// Run a parameterized SELECT; '?' placeholders are bound to params in
// order. Results are printed to stdout. Returns true on success.
bool dbExecute(const std::string& sql,
               const std::vector<std::string>& params = {});

#endif
