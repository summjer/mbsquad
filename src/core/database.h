#pragma once

#include <sqlite3.h>
#include <string>
#include <vector>
#include <unordered_map>
#include <functional>
#include <mutex>
#include <memory>
#include <optional>

struct sqlite3_stmt;

namespace sp {

class Database {
public:
    Database();
    ~Database();

    void open(const std::string& path);
    void close();
    void migrate();  // Run all schema migrations

    // Execute (INSERT/UPDATE/DELETE)
    void exec(const std::string& sql);
    void exec(const std::string& sql, const std::vector<std::string>& params);

    // Query one row
    std::unordered_map<std::string, std::string> queryOne(
        const std::string& sql, const std::vector<std::string>& params = {});

    // Query many rows
    std::vector<std::unordered_map<std::string, std::string>> query(
        const std::string& sql, const std::vector<std::string>& params = {});

    // Query scalar (single value)
    std::string queryScalar(const std::string& sql, const std::vector<std::string>& params = {});

    // Last insert row id
    int64_t lastInsertId();

    // Transaction
    void begin();
    void commit();
    void rollback();

    // Run in transaction
    void transaction(std::function<void()> fn);

    // Native handle (for advanced use)
    sqlite3* native() { return db_; }

private:
    sqlite3* db_ = nullptr;
    std::recursive_mutex mtx_;
    std::string path_;

    void bindParams(sqlite3_stmt* stmt, const std::vector<std::string>& params);
    std::unordered_map<std::string, std::string> rowToMap(sqlite3_stmt* stmt);
};

using DatabasePtr = std::shared_ptr<Database>;

} // namespace sp
