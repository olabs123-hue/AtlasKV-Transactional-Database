#include "atlaskv/database.hpp"
#include <algorithm>
#include <cstdio>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
namespace atlaskv {
Transaction::Transaction(Database& db):db_(&db) {
    std::lock_guard<std::mutex> guard(db.mutex_);
    snapshot_=db.version_;
    epoch_=db.epoch_;
}
std::optional<Record> Database::at_version_unlocked(const std::string& key, std::uint64_t version) const {
    auto it=history_.find(key);
    if(it==history_.end()) return std::nullopt;
    const auto& h=it->second;
    auto upper=std::upper_bound(h.begin(),h.end(),version,
        [](std::uint64_t v,const Revision& r){return v<r.version;});
    if(upper==h.begin()) return std::nullopt;
    return std::prev(upper)->value;
}
std::optional<Record> Transaction::get(const std::string& key) {
    if(!active_) throw std::logic_error("transaction closed");
    reads_.insert(key);
    auto it=writes_.find(key);
    if(it!=writes_.end()) return it->second;
    std::lock_guard<std::mutex> guard(db_->mutex_);
    if(epoch_!=db_->epoch_) throw std::logic_error("database reloaded during transaction");
    return db_->at_version_unlocked(key,snapshot_);
}
void Transaction::put(const std::string& key,Record r) {
    if(!active_) throw std::logic_error("transaction closed");
    writes_[key]=std::move(r);
}
void Transaction::erase(const std::string& key) {
    if(!active_) throw std::logic_error("transaction closed");
    writes_[key]=std::nullopt;
}
bool Transaction::commit() {
    if(!active_) throw std::logic_error("transaction closed");
    std::lock_guard<std::mutex> guard(db_->mutex_);
    if(epoch_!=db_->epoch_) {active_=false;return false;}
    auto conflict=[&](const std::string& key) {
        auto it=db_->history_.find(key);
        return it!=db_->history_.end() && !it->second.empty() && it->second.back().version>snapshot_;
    };
    for(const auto& key:reads_) if(conflict(key)) {active_=false;return false;}
    for(const auto& w:writes_) if(conflict(w.first)) {active_=false;return false;}
    if(!writes_.empty()) {
        ++db_->version_;
        for(const auto& w:writes_) {
            const auto& key=w.first;
            auto old=db_->records_.find(key);
            if(old!=db_->records_.end()) {
                auto idx=db_->category_index_.find(old->second.category);
                if(idx!=db_->category_index_.end()) {
                    idx->second.erase(key);
                    if(idx->second.empty()) db_->category_index_.erase(idx);
                }
            }
            if(w.second) {
                db_->records_.insert_or_assign(key,*w.second);
                db_->category_index_[w.second->category].insert(key);
            } else db_->records_.erase(key);
            db_->history_[key].push_back({db_->version_,w.second});
        }
    }
    active_=false;
    return true;
}
void Transaction::rollback() {active_=false;writes_.clear();reads_.clear();}
std::optional<Record> Database::get(const std::string& key) const {
    std::lock_guard<std::mutex> guard(mutex_);
    auto it=records_.find(key);
    return it==records_.end()?std::nullopt:std::optional<Record>(it->second);
}
QueryPlan Database::explain(const Query& q) const {
    return q.category?QueryPlan::CategoryIndex:QueryPlan::FullScan;
}
std::vector<std::pair<std::string,Record>> Database::query(const Query& q,QueryStats* stats) const {
    std::lock_guard<std::mutex> guard(mutex_);
    QueryStats s{explain(q),0,0};
    std::vector<std::pair<std::string,Record>> out;
    auto visit=[&](const std::string& key,const Record& rec) {
        ++s.examined;
        if(q.value_contains && rec.value.find(*q.value_contains)==std::string::npos) return;
        out.emplace_back(key,rec);
        ++s.matched;
    };
    if(q.limit) {
        if(q.category) {
            auto idx=category_index_.find(*q.category);
            if(idx!=category_index_.end()) for(const auto& key:idx->second) {
                visit(key,records_.at(key));
                if(out.size()>=q.limit) break;
            }
        } else for(const auto& pair:records_) {
            visit(pair.first,pair.second);
            if(out.size()>=q.limit) break;
        }
    }
    if(stats) *stats=s;
    return out;
}
std::size_t Database::size() const {std::lock_guard<std::mutex> guard(mutex_);return records_.size();}
std::uint64_t Database::version() const {std::lock_guard<std::mutex> guard(mutex_);return version_;}
namespace {
void write_u64(std::ostream& out,std::uint64_t v) {
    for(int i=0;i<8;++i) out.put(static_cast<char>((v>>(8*i))&255));
    if(!out) throw std::runtime_error("snapshot write failed");
}
std::uint64_t read_u64(std::istream& in) {
    std::uint64_t v=0;
    for(int i=0;i<8;++i) {int c=in.get();if(c==EOF) throw std::runtime_error("truncated snapshot");v|=std::uint64_t(c)<<(8*i);}
    return v;
}
void write_string(std::ostream& out,const std::string& s) {
    write_u64(out,s.size());out.write(s.data(),static_cast<std::streamsize>(s.size()));
    if(!out) throw std::runtime_error("snapshot write failed");
}
std::string read_string(std::istream& in) {
    auto n=read_u64(in);
    if(n>16*1024*1024) throw std::runtime_error("snapshot field too large");
    std::string s(static_cast<std::size_t>(n),'\0');
    if(!in.read(s.data(),static_cast<std::streamsize>(n))) throw std::runtime_error("truncated snapshot");
    return s;
}
}
void Database::save_snapshot(const std::string& path) const {
    std::lock_guard<std::mutex> guard(mutex_);
    const auto tmp=path+".tmp";
    try {
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc);
        if(!out) throw std::runtime_error("cannot open snapshot for writing");
        out.write("ATKV0002",8);
        write_u64(out,version_);
        write_u64(out,records_.size());
        for(const auto& [key,rec]:records_) {
            write_string(out,key);write_string(out,rec.category);write_string(out,rec.value);
        }
        out.flush();if(!out) throw std::runtime_error("snapshot flush failed");
        out.close();if(!out) throw std::runtime_error("snapshot close failed");
        if(std::rename(tmp.c_str(),path.c_str())!=0) throw std::runtime_error("snapshot rename failed");
    } catch(...) {std::remove(tmp.c_str());throw;}
}
void Database::load_snapshot(const std::string& path) {
    std::ifstream in(path,std::ios::binary);
    if(!in) throw std::runtime_error("cannot open snapshot");
    char magic[8];
    if(!in.read(magic,8)||std::string(magic,8)!="ATKV0002") throw std::runtime_error("invalid snapshot header");
    auto version=read_u64(in),count=read_u64(in);
    if(count>10000000) throw std::runtime_error("snapshot record count too large");
    std::map<std::string,Record> next;
    std::map<std::string,std::set<std::string>> index;
    std::unordered_map<std::string,std::vector<Revision>> history;
    for(std::uint64_t i=0;i<count;++i) {
        auto key=read_string(in),cat=read_string(in),val=read_string(in);
        if(next.count(key)) throw std::runtime_error("duplicate key in snapshot");
        Record r{std::move(cat),std::move(val)};
        index[r.category].insert(key);
        history[key].push_back({version,r});
        next.emplace(std::move(key),std::move(r));
    }
    if(in.peek()!=EOF) throw std::runtime_error("trailing bytes in snapshot");
    std::lock_guard<std::mutex> guard(mutex_);
    records_.swap(next);category_index_.swap(index);history_.swap(history);version_=version;++epoch_;
}
}
