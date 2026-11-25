#pragma once
#include "atlaskv/database.hpp"
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace atlaskv {
struct DurableMutation {
    std::string key;
    std::optional<Record> value; // nullopt means delete
};
// Single-process durable facade. Operations must go through this facade;
// do not call the underlying Database directly while a DurableStore is active.
class DurableStore {
public:
    explicit DurableStore(std::string log_path);
    DurableStore(const DurableStore&)=delete;
    DurableStore& operator=(const DurableStore&)=delete;
    void apply(const std::vector<DurableMutation>& mutations);
    void put(const std::string& key, Record value);
    void erase(const std::string& key);
    std::optional<Record> get(const std::string& key) const;
    std::size_t size() const;
    std::uint64_t version() const;
private:
    std::string path_;
    mutable std::mutex mutex_;
    Database db_;
    std::uint64_t sequence_=0;
    bool failed_=false;
    void recover();
};
}
