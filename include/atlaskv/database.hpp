#pragma once
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace atlaskv {
struct Record {
    std::string category, value;
    bool operator==(const Record& r) const { return category==r.category && value==r.value; }
};
struct Query {
    std::optional<std::string> category, value_contains;
    std::size_t limit=100;
};
enum class QueryPlan { FullScan, CategoryIndex };
struct QueryStats { QueryPlan plan; std::size_t examined=0, matched=0; };
class Database;
class Transaction {
public:
    explicit Transaction(Database& db);
    Transaction(const Transaction&)=delete;
    Transaction& operator=(const Transaction&)=delete;
    std::optional<Record> get(const std::string& key);
    void put(const std::string& key, Record value);
    void erase(const std::string& key);
    bool commit(); // false on optimistic conflict; no writes applied
    void rollback();
    std::uint64_t snapshot_version() const { return snapshot_; }
private:
    Database* db_;
    std::uint64_t snapshot_;
    std::uint64_t epoch_;
    std::map<std::string, std::optional<Record>> writes_;
    std::set<std::string> reads_;
    bool active_=true;
};
class Database {
public:
    Transaction begin() { return Transaction(*this); }
    std::optional<Record> get(const std::string& key) const;
    std::vector<std::pair<std::string,Record>> query(const Query& q, QueryStats* stats=nullptr) const;
    QueryPlan explain(const Query& q) const;
    std::size_t size() const;
    std::uint64_t version() const;
    void save_snapshot(const std::string& path) const;
    void load_snapshot(const std::string& path); // replaces state; only when no transactions active
private:
    friend class Transaction;
    struct Revision { std::uint64_t version; std::optional<Record> value; };
    mutable std::mutex mutex_;
    std::map<std::string,Record> records_;
    std::map<std::string,std::set<std::string>> category_index_;
    std::unordered_map<std::string,std::vector<Revision>> history_;
    std::uint64_t version_=0;
    std::uint64_t epoch_=0;
    std::optional<Record> at_version_unlocked(const std::string& key, std::uint64_t version) const;
};
}
