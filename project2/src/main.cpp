// ============================================================================
// main.cpp - Project 2 Car Manufacturing DB Application
// CSE4110 / AIE4055 Database Systems, Spring 2026
//
// Menu-driven C++ app that connects to MySQL via the MySQL C API
// (and optionally ODBC) and executes the seven sample queries from Project 1.
//
// Build (MySQL C API only - default, no extra deps):
//   g++ -std=c++17 -O2 -Wall main.cpp -o car_db_app \
//       $(mysql_config --cflags --libs)
//
// Build (MySQL C API + ODBC):
//   g++ -std=c++17 -O2 -Wall -DHAVE_ODBC main.cpp -o car_db_app \
//       $(mysql_config --cflags --libs) -lodbc
//
// Security: every user-supplied value is bound through a true server-side
// prepared statement (mysql_stmt_prepare + mysql_stmt_bind_param for the
// MySQL C API, SQLPrepare + SQLBindParameter for ODBC) so the seven queries
// are immune to SQL injection.
// ============================================================================

#include "database.h"

#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <mysql.h>

#ifdef HAVE_ODBC
  #include <sql.h>
  #include <sqlext.h>
#endif

// ----------------------------------------------------------------------------
// Single active connection (only one backend is active at a time)
// ----------------------------------------------------------------------------
static Backend g_backend   = Backend::MYSQL_C_API;
static bool    g_connected = false;
static MYSQL*  g_mysql     = nullptr;
#ifdef HAVE_ODBC
static SQLHENV g_env       = nullptr;
static SQLHDBC g_dbc       = nullptr;
#endif

// ----------------------------------------------------------------------------
// Print a result set as a fixed-width ASCII table
// ----------------------------------------------------------------------------
static void printTable(const std::vector<std::string>& cols,
                       const std::vector<std::vector<std::string>>& rows) {
    if (cols.empty()) { std::cout << "(no result set)\n"; return; }
    std::vector<size_t> w(cols.size());
    for (size_t i = 0; i < cols.size(); ++i) w[i] = cols[i].size();
    for (const auto& r : rows)
        for (size_t i = 0; i < cols.size() && i < r.size(); ++i)
            w[i] = std::max(w[i], r[i].size());

    auto bar = [&]{
        std::cout << '+';
        for (size_t i = 0; i < cols.size(); ++i) std::cout << std::string(w[i] + 2, '-') << '+';
        std::cout << '\n';
    };
    bar();
    std::cout << '|';
    for (size_t i = 0; i < cols.size(); ++i)
        std::cout << ' ' << std::left << std::setw(w[i]) << cols[i] << " |";
    std::cout << '\n';
    bar();
    for (const auto& r : rows) {
        std::cout << '|';
        for (size_t i = 0; i < cols.size(); ++i)
            std::cout << ' ' << std::left << std::setw(w[i]) << (i < r.size() ? r[i] : "") << " |";
        std::cout << '\n';
    }
    bar();
    std::cout << '(' << rows.size() << " row" << (rows.size() == 1 ? "" : "s") << ")\n";
}

// ============================================================================
// Backend: MySQL C API   (true server-side prepared statements)
// ============================================================================
static bool mysqlConnect(const DBConfig& c) {
    g_mysql = mysql_init(nullptr);
    if (!g_mysql) { std::cerr << "mysql_init failed\n"; return false; }
    mysql_options(g_mysql, MYSQL_SET_CHARSET_NAME, "utf8mb4");
    if (!mysql_real_connect(g_mysql, c.host.c_str(), c.user.c_str(),
                            c.password.c_str(), c.database.c_str(),
                            c.port, nullptr, 0)) {
        std::cerr << "MySQL connect failed: " << mysql_error(g_mysql) << '\n';
        mysql_close(g_mysql); g_mysql = nullptr;
        return false;
    }
    return true;
}

static bool mysqlExecute(const std::string& sql,
                         const std::vector<std::string>& params) {
    // ----- 1. prepare -----
    MYSQL_STMT* stmt = mysql_stmt_init(g_mysql);
    if (!stmt) { std::cerr << "mysql_stmt_init failed\n"; return false; }

    if (mysql_stmt_prepare(stmt, sql.c_str(), (unsigned long)sql.size()) != 0) {
        std::cerr << "stmt_prepare failed: " << mysql_stmt_error(stmt) << '\n';
        mysql_stmt_close(stmt);
        return false;
    }

    // ----- 2. bind input parameters (all as VARCHAR; MySQL coerces) -----
    std::vector<MYSQL_BIND> ib(params.size());
    std::vector<unsigned long> ilen(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        std::memset(&ib[i], 0, sizeof(MYSQL_BIND));
        ib[i].buffer_type   = MYSQL_TYPE_STRING;
        ib[i].buffer        = (void*)params[i].data();
        ib[i].buffer_length = (unsigned long)params[i].size();
        ilen[i]             = (unsigned long)params[i].size();
        ib[i].length        = &ilen[i];
    }
    if (!params.empty() && mysql_stmt_bind_param(stmt, ib.data()) != 0) {
        std::cerr << "stmt_bind_param failed: " << mysql_stmt_error(stmt) << '\n';
        mysql_stmt_close(stmt);
        return false;
    }

    // ----- 3. execute -----
    if (mysql_stmt_execute(stmt) != 0) {
        std::cerr << "stmt_execute failed: " << mysql_stmt_error(stmt) << '\n';
        mysql_stmt_close(stmt);
        return false;
    }

    // ----- 4. fetch metadata; non-SELECT returns nullptr -----
    MYSQL_RES* meta = mysql_stmt_result_metadata(stmt);
    if (!meta) {
        mysql_stmt_close(stmt);
        std::cout << "(no result set)\n";
        return true;
    }

    unsigned int nc = mysql_num_fields(meta);
    MYSQL_FIELD* fields = mysql_fetch_fields(meta);
    std::vector<std::string> cols(nc);
    for (unsigned int i = 0; i < nc; ++i) cols[i] = fields[i].name;

    // ----- 5. bind output buffers (all as CHAR for uniform display) -----
    const unsigned long BUFSZ = 4096;
    std::vector<MYSQL_BIND> ob(nc);
    std::vector<std::vector<char>> obuf(nc, std::vector<char>(BUFSZ));
    std::vector<unsigned long> olen(nc);
    std::unique_ptr<bool[]> onull(new bool[nc]());
    for (unsigned int i = 0; i < nc; ++i) {
        std::memset(&ob[i], 0, sizeof(MYSQL_BIND));
        ob[i].buffer_type   = MYSQL_TYPE_STRING;
        ob[i].buffer        = obuf[i].data();
        ob[i].buffer_length = BUFSZ;
        ob[i].length        = &olen[i];
        ob[i].is_null       = &onull[i];
    }
    if (mysql_stmt_bind_result(stmt, ob.data()) != 0) {
        std::cerr << "stmt_bind_result failed: " << mysql_stmt_error(stmt) << '\n';
        mysql_free_result(meta);
        mysql_stmt_close(stmt);
        return false;
    }

    // ----- 6. fetch rows -----
    std::vector<std::vector<std::string>> rows;
    int fetchRc;
    while ((fetchRc = mysql_stmt_fetch(stmt)) == 0) {
        std::vector<std::string> r(nc);
        for (unsigned int i = 0; i < nc; ++i) {
            if (onull[i]) r[i] = "NULL";
            else r[i] = std::string(obuf[i].data(),
                                    olen[i] < BUFSZ ? olen[i] : BUFSZ);
        }
        rows.push_back(std::move(r));
    }

    mysql_free_result(meta);
    mysql_stmt_close(stmt);
    printTable(cols, rows);
    return true;
}

// ============================================================================
// Backend: ODBC   (true prepared statements via SQLPrepare/SQLBindParameter)
// ============================================================================
#ifdef HAVE_ODBC
static void odbcReportError(SQLSMALLINT type, SQLHANDLE h, const char* ctx) {
    SQLCHAR state[6] = {0}, msg[1024] = {0};
    SQLINTEGER native = 0; SQLSMALLINT mlen = 0;
    SQLGetDiagRec(type, h, 1, state, &native, msg, sizeof(msg), &mlen);
    std::cerr << "ODBC " << ctx << " failed [" << state << "]: " << msg << '\n';
}

static bool odbcConnect(const DBConfig& c) {
    SQLAllocHandle(SQL_HANDLE_ENV, SQL_NULL_HANDLE, &g_env);
    SQLSetEnvAttr(g_env, SQL_ATTR_ODBC_VERSION, (SQLPOINTER)SQL_OV_ODBC3, 0);
    SQLAllocHandle(SQL_HANDLE_DBC, g_env, &g_dbc);

    std::ostringstream cs;
    cs << "DRIVER={" << c.odbc_driver << "};"
       << "SERVER="   << c.host     << ";PORT="   << c.port
       << ";DATABASE=" << c.database << ";USER="   << c.user
       << ";PASSWORD=" << c.password << ";CHARSET=utf8mb4;";

    SQLCHAR out[1024]; SQLSMALLINT outLen = 0;
    SQLRETURN rc = SQLDriverConnect(g_dbc, nullptr,
                                    (SQLCHAR*)cs.str().c_str(), SQL_NTS,
                                    out, sizeof(out), &outLen, SQL_DRIVER_NOPROMPT);
    if (!SQL_SUCCEEDED(rc)) {
        odbcReportError(SQL_HANDLE_DBC, g_dbc, "SQLDriverConnect");
        SQLFreeHandle(SQL_HANDLE_DBC, g_dbc); g_dbc = nullptr;
        SQLFreeHandle(SQL_HANDLE_ENV, g_env); g_env = nullptr;
        return false;
    }
    return true;
}

static bool odbcExecute(const std::string& sql,
                        const std::vector<std::string>& params) {
    SQLHSTMT stmt;
    SQLAllocHandle(SQL_HANDLE_STMT, g_dbc, &stmt);

    if (!SQL_SUCCEEDED(SQLPrepare(stmt, (SQLCHAR*)sql.c_str(), SQL_NTS))) {
        odbcReportError(SQL_HANDLE_STMT, stmt, "SQLPrepare");
        SQLFreeHandle(SQL_HANDLE_STMT, stmt); return false;
    }

    std::vector<std::vector<char>> bufs(params.size());
    std::vector<SQLLEN> lens(params.size());
    for (size_t i = 0; i < params.size(); ++i) {
        bufs[i].assign(params[i].begin(), params[i].end());
        bufs[i].push_back('\0');
        lens[i] = (SQLLEN)params[i].size();
        SQLBindParameter(stmt, (SQLUSMALLINT)(i + 1), SQL_PARAM_INPUT,
                         SQL_C_CHAR, SQL_VARCHAR, bufs[i].size(), 0,
                         bufs[i].data(), bufs[i].size(), &lens[i]);
    }

    if (!SQL_SUCCEEDED(SQLExecute(stmt))) {
        odbcReportError(SQL_HANDLE_STMT, stmt, "SQLExecute");
        SQLFreeHandle(SQL_HANDLE_STMT, stmt); return false;
    }

    SQLSMALLINT nc = 0; SQLNumResultCols(stmt, &nc);
    std::vector<std::string> cols;
    for (SQLSMALLINT i = 1; i <= nc; ++i) {
        SQLCHAR name[256]; SQLSMALLINT nlen = 0;
        SQLDescribeCol(stmt, i, name, sizeof(name), &nlen,
                       nullptr, nullptr, nullptr, nullptr);
        cols.emplace_back((char*)name, nlen);
    }

    std::vector<std::vector<std::string>> rows;
    while (SQLFetch(stmt) != SQL_NO_DATA) {
        std::vector<std::string> r;
        for (SQLSMALLINT i = 1; i <= nc; ++i) {
            char buf[4096]; SQLLEN ind = 0;
            SQLGetData(stmt, i, SQL_C_CHAR, buf, sizeof(buf), &ind);
            r.emplace_back(ind == SQL_NULL_DATA ? "NULL" : buf);
        }
        rows.push_back(std::move(r));
    }
    SQLFreeHandle(SQL_HANDLE_STMT, stmt);
    printTable(cols, rows);
    return true;
}
#endif // HAVE_ODBC

// ============================================================================
//  Public DB facade (declared in database.h)
// ============================================================================
bool dbConnect(Backend b, const DBConfig& cfg) {
    if (g_connected) { std::cout << "Already connected. Disconnect first.\n"; return false; }
    bool ok = false;
    if (b == Backend::MYSQL_C_API) {
        ok = mysqlConnect(cfg);
    } else {
#ifdef HAVE_ODBC
        ok = odbcConnect(cfg);
#else
        std::cerr << "ODBC support not compiled in. Rebuild with -DHAVE_ODBC -lodbc.\n";
        return false;
#endif
    }
    if (ok) {
        g_backend = b; g_connected = true;
        std::cout << "Connected via " << (b == Backend::MYSQL_C_API ? "MySQL C API" : "ODBC") << ".\n";
    }
    return ok;
}

void dbDisconnect() {
    if (!g_connected) { std::cout << "Not connected.\n"; return; }
    if (g_backend == Backend::MYSQL_C_API) {
        mysql_close(g_mysql); g_mysql = nullptr;
    } else {
#ifdef HAVE_ODBC
        SQLDisconnect(g_dbc);
        SQLFreeHandle(SQL_HANDLE_DBC, g_dbc); g_dbc = nullptr;
        SQLFreeHandle(SQL_HANDLE_ENV, g_env); g_env = nullptr;
#endif
    }
    g_connected = false;
    std::cout << "Disconnected.\n";
}

bool    dbIsConnected() { return g_connected; }
Backend dbBackend()     { return g_backend; }

bool dbExecute(const std::string& sql, const std::vector<std::string>& params) {
    if (!g_connected) { std::cout << "Not connected.\n"; return false; }
    if (g_backend == Backend::MYSQL_C_API) return mysqlExecute(sql, params);
#ifdef HAVE_ODBC
    return odbcExecute(sql, params);
#else
    return false;
#endif
}

// ============================================================================
//  Project 1 sample queries (one function per query)
// ============================================================================

// Q1. Past-3-year sales trends. Per spec ("...broken down by year, month, and
//   week, AND FURTHER by buyer gender and income range") and TA clarification,
//   6 cross-tab breakdowns are produced (time x demographic):
//     [1/6] Brand x Year  x Gender    [2/6] Brand x Year  x Income
//     [3/6] Brand x Month x Gender    [4/6] Brand x Month x Income
//     [5/6] Brand x Week  x Gender    [6/6] Brand x Week  x Income
static void executeSalesTrendsQuery() {
    static const std::string JOIN =
        "FROM Sales_Transactions st "
        "JOIN Vehicles  v ON st.vin = v.vin "
        "JOIN Models    m ON v.model_name = m.model_name AND v.model_year = m.year "
        "JOIN Brands    b ON m.brand_id = b.brand_id "
        "JOIN Customers c ON st.customer_id = c.customer_id "
        "WHERE st.sale_date >= DATE_SUB(CURDATE(), INTERVAL 3 YEAR) ";
    static const std::string INCOME =
        "CASE WHEN c.annual_income IS NULL  THEN 'Unknown' "
        "     WHEN c.annual_income <  50000 THEN 'A:<50k' "
        "     WHEN c.annual_income < 100000 THEN 'B:50-100k' "
        "     WHEN c.annual_income < 150000 THEN 'C:100-150k' "
        "     ELSE 'D:>=150k' END";
    static const std::string AGG =
        ", COUNT(*) AS units_sold, ROUND(SUM(st.sale_price), 2) AS revenue ";

    auto run = [](const char* title, const std::string& sql) {
        std::cout << "\n--- " << title << " ---\n";
        dbExecute(sql);
    };

    run("[1/6] Brand x Year x Gender",
        "SELECT b.name AS brand, YEAR(st.sale_date) AS yr, c.gender" + AGG + JOIN +
        "GROUP BY b.name, yr, c.gender ORDER BY b.name, yr, c.gender");

    run("[2/6] Brand x Year x Income",
        "SELECT b.name AS brand, YEAR(st.sale_date) AS yr, " + INCOME + " AS income_range" + AGG + JOIN +
        "GROUP BY b.name, yr, income_range ORDER BY b.name, yr, income_range");

    run("[3/6] Brand x Month x Gender",
        "SELECT b.name AS brand, MONTH(st.sale_date) AS mo, c.gender" + AGG + JOIN +
        "GROUP BY b.name, mo, c.gender ORDER BY b.name, mo, c.gender");

    run("[4/6] Brand x Month x Income",
        "SELECT b.name AS brand, MONTH(st.sale_date) AS mo, " + INCOME + " AS income_range" + AGG + JOIN +
        "GROUP BY b.name, mo, income_range ORDER BY b.name, mo, income_range");

    run("[5/6] Brand x Week x Gender",
        "SELECT b.name AS brand, WEEK(st.sale_date, 3) AS wk, c.gender" + AGG + JOIN +
        "GROUP BY b.name, wk, c.gender ORDER BY b.name, wk, c.gender");

    run("[6/6] Brand x Week x Income",
        "SELECT b.name AS brand, WEEK(st.sale_date, 3) AS wk, " + INCOME + " AS income_range" + AGG + JOIN +
        "GROUP BY b.name, wk, income_range ORDER BY b.name, wk, income_range");
}

// Q2. Defective part tracking (parameterized: parts_type, supplier_id, date range)
static void executeDefectivePartTrackingQuery() {
    std::string ptype, sid, sd, ed;
    std::cout << "Part type      (e.g. Transmission): "; std::getline(std::cin, ptype);
    std::cout << "Supplier ID    (e.g. 204 for ZF):   "; std::getline(std::cin, sid);
    std::cout << "Start date     (YYYY-MM-DD):        "; std::getline(std::cin, sd);
    std::cout << "End date       (YYYY-MM-DD):        "; std::getline(std::cin, ed);
    dbExecute(
        "SELECT DISTINCT v.vin, v.model_name, v.model_year, "
        "       p.part_id, p.parts_type, "
        "       p.mf_date AS part_mf, v.mf_date AS vehicle_mf, "
        "       COALESCE(c.customer_id, 0)            AS customer_id, "
        "       COALESCE(c.name, '(unsold/in stock)') AS customer_name, "
        "       st.sale_date "
        "FROM Parts p "
        "JOIN Plants   prod ON p.production_plant_id = prod.plant_id "
        "JOIN Vehicles v    ON v.plant_id = p.assembly_plant_id "
        "                  AND v.mf_date  BETWEEN p.mf_date "
        "                                    AND DATE_ADD(p.mf_date, INTERVAL 90 DAY) "
        "LEFT JOIN Sales_Transactions st ON st.vin = v.vin "
        "LEFT JOIN Customers          c  ON st.customer_id = c.customer_id "
        "WHERE p.parts_type = ? AND prod.supplier_id = ? "
        "  AND p.mf_date BETWEEN ? AND ? "
        "ORDER BY v.vin",
        {ptype, sid, sd, ed});
}

// Q3. Top 2 brands by revenue (past year)
static void executeTopBrandsRevenueQuery() {
    dbExecute(
        "SELECT b.name AS brand, COUNT(*) AS units_sold, "
        "       ROUND(SUM(st.sale_price), 2) AS revenue "
        "FROM Sales_Transactions st "
        "JOIN Vehicles v ON st.vin = v.vin "
        "JOIN Models   m ON v.model_name = m.model_name AND v.model_year = m.year "
        "JOIN Brands   b ON m.brand_id = b.brand_id "
        "WHERE st.sale_date >= DATE_SUB(CURDATE(), INTERVAL 1 YEAR) "
        "GROUP BY b.name ORDER BY revenue DESC, units_sold DESC LIMIT 2");
}

// Q4. Top 2 brands by unit sales (past year)
static void executeTopBrandsUnitsQuery() {
    dbExecute(
        "SELECT b.name AS brand, COUNT(*) AS units_sold, "
        "       ROUND(SUM(st.sale_price), 2) AS revenue "
        "FROM Sales_Transactions st "
        "JOIN Vehicles v ON st.vin = v.vin "
        "JOIN Models   m ON v.model_name = m.model_name AND v.model_year = m.year "
        "JOIN Brands   b ON m.brand_id = b.brand_id "
        "WHERE st.sale_date >= DATE_SUB(CURDATE(), INTERVAL 1 YEAR) "
        "GROUP BY b.name ORDER BY units_sold DESC, revenue DESC LIMIT 2");
}

// Q5. Seasonal sales patterns by body style (parameterized: body_style)
//   Returns only the peak month(s) — months where units_sold equals the maximum.
//   Multiple rows are returned when several months tie for peak.
static void executeSeasonalPatternsQuery() {
    std::string body;
    std::cout << "Body style (e.g. Convertible, Sedan, SUV): ";
    std::getline(std::cin, body);
    dbExecute(
        "WITH monthly AS ( "
        "  SELECT MONTH(st.sale_date) AS sale_month, COUNT(*) AS units_sold "
        "  FROM Sales_Transactions st "
        "  JOIN Vehicles v ON st.vin = v.vin "
        "  JOIN Models   m ON v.model_name = m.model_name AND v.model_year = m.year "
        "  WHERE m.body_style = ? "
        "  GROUP BY MONTH(st.sale_date)) "
        "SELECT sale_month, units_sold "
        "FROM monthly "
        "WHERE units_sold = (SELECT MAX(units_sold) FROM monthly) "
        "ORDER BY sale_month",
        {body});
}

// Q6. Dealer inventory efficiency (longest average inventory time)
//   Returns only the dealer(s) with the maximum average inventory days.
//   When multiple dealers tie, all are returned (consistent with Q5 peak handling).
static void executeDealerInventoryQuery() {
    dbExecute(
        "WITH dealer_avg AS ( "
        "  SELECT d.dealer_id, d.name AS dealer_name, COUNT(*) AS vehicles_sold, "
        "         AVG(DATEDIFF(st.sale_date, i.insert_date)) AS avg_days "
        "  FROM Inventory          i "
        "  JOIN Sales_Transactions st ON i.vin = st.vin "
        "  JOIN Dealers            d  ON i.dealer_id = d.dealer_id "
        "  WHERE st.sale_date >= i.insert_date "
        "  GROUP BY d.dealer_id, d.name ) "
        "SELECT dealer_id, dealer_name, vehicles_sold, "
        "       ROUND(avg_days, 1) AS avg_inventory_days "
        "FROM dealer_avg "
        "WHERE avg_days = (SELECT MAX(avg_days) FROM dealer_avg) "
        "ORDER BY dealer_id");
}

// Q7. Supplier coverage (most distinct models)
//   Returns only the supplier(s) with the maximum number of distinct models.
//   When multiple suppliers tie, all are returned (consistent with Q5/Q6).
//   Assembly plants never produce parts (Parts.production_plant_id only references
//   Manufacturing plants), so Internal suppliers naturally drop out at the Parts
//   join — no explicit plant_type filter required.
static void executeSupplierCoverageQuery() {
    dbExecute(
        "WITH supplier_coverage AS ( "
        "  SELECT s.supplier_id, s.name AS supplier_name, "
        "         COUNT(DISTINCT v.model_name, v.model_year) AS distinct_models, "
        "         COUNT(DISTINCT m.brand_id) AS distinct_brands "
        "  FROM Suppliers s "
        "  JOIN Plants   prod ON s.supplier_id = prod.supplier_id "
        "  JOIN Parts    p    ON prod.plant_id = p.production_plant_id "
        "  JOIN Vehicles v    ON v.plant_id    = p.assembly_plant_id "
        "  JOIN Models   m    ON v.model_name  = m.model_name AND v.model_year = m.year "
        "  GROUP BY s.supplier_id, s.name ) "
        "SELECT supplier_id, supplier_name, distinct_models, distinct_brands "
        "FROM supplier_coverage "
        "WHERE distinct_models = (SELECT MAX(distinct_models) FROM supplier_coverage) "
        "ORDER BY supplier_id");
}

// ============================================================================
//  Menu / main
// ============================================================================
static void displayMenu() {
    std::cout << "\n============================================================\n";
    std::cout << "  Car Manufacturing DB - Project 2\n";
    std::cout << "  Status: ";
    if (dbIsConnected())
        std::cout << "CONNECTED via " << (dbBackend() == Backend::MYSQL_C_API ? "MySQL C API" : "ODBC");
    else
        std::cout << "DISCONNECTED";
    std::cout << "\n------------------------------------------------------------\n"
                 "   1. Connect (MySQL C API)\n"
#ifdef HAVE_ODBC
                 "   2. Connect (ODBC)\n"
#else
                 "   2. Connect (ODBC)   [not compiled in]\n"
#endif
                 "   3. Disconnect\n"
                 "   4. Q1  Sales Trends (past 3 years)\n"
                 "   5. Q2  Defective Part Tracking\n"
                 "   6. Q3  Top 2 Brands by Revenue (past year)\n"
                 "   7. Q4  Top 2 Brands by Unit Sales (past year)\n"
                 "   8. Q5  Seasonal Sales Patterns (by body style)\n"
                 "   9. Q6  Dealer Inventory Efficiency\n"
                 "  10. Q7  Supplier Coverage\n"
                 "   0. Exit\n"
                 "Choice: ";
}

static int getUserChoice() {
    std::string line;
    if (!std::getline(std::cin, line)) return 0;
    try { return std::stoi(line); } catch (...) { return -1; }
}

static DBConfig readConfig() {
    DBConfig c;
    auto ask = [](const std::string& label, const std::string& dflt) {
        std::cout << label << " [" << dflt << "]: ";
        std::string s; std::getline(std::cin, s);
        return s.empty() ? dflt : s;
    };
    c.host = ask("Host", c.host);
    try { c.port = (unsigned)std::stoul(ask("Port", std::to_string(c.port))); } catch (...) {}
    c.user        = ask("User",        c.user);
    c.password    = ask("Password",    c.password);
    c.database    = ask("Database",    c.database);
#ifdef HAVE_ODBC
    c.odbc_driver = ask("ODBC driver", c.odbc_driver);
#endif
    return c;
}

int main() {
    std::cout << "Car Manufacturing DB Application - Project 2 (CSE4110/AIE4055)\n";
    DBConfig cfg = readConfig();

    while (true) {
        displayMenu();
        switch (getUserChoice()) {
            case  1: dbConnect(Backend::MYSQL_C_API, cfg);  break;
            case  2: dbConnect(Backend::ODBC,        cfg);  break;
            case  3: dbDisconnect();                        break;
            case  4: executeSalesTrendsQuery();             break;
            case  5: executeDefectivePartTrackingQuery();   break;
            case  6: executeTopBrandsRevenueQuery();        break;
            case  7: executeTopBrandsUnitsQuery();          break;
            case  8: executeSeasonalPatternsQuery();        break;
            case  9: executeDealerInventoryQuery();         break;
            case 10: executeSupplierCoverageQuery();        break;
            case  0: dbDisconnect(); std::cout << "Goodbye.\n"; return 0;
            default: std::cout << "Invalid choice.\n";      break;
        }
    }
}
