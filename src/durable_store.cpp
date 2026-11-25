#include "atlaskv/durable_store.hpp"
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <utility>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

namespace atlaskv {
namespace {
constexpr std::uint32_t kMaxFrame=8*1024*1024;
void u32(std::string& b, std::uint32_t n) {
    for(int i=0;i<4;++i)b.push_back(static_cast<char>((n>>(8*i))&255));
}
void u64(std::string& b, std::uint64_t n) {
    for(int i=0;i<8;++i)b.push_back(static_cast<char>((n>>(8*i))&255));
}
std::uint32_t read32(const std::string& b,std::size_t& p){
    if(b.size()-p<4)throw std::runtime_error("invalid WAL frame");
    std::uint32_t n=0;for(int i=0;i<4;++i)n|=std::uint32_t(static_cast<unsigned char>(b[p++]))<<(8*i);return n;
}
std::uint64_t read64(const std::string& b,std::size_t& p){
    if(b.size()-p<8)throw std::runtime_error("invalid WAL frame");
    std::uint64_t n=0;for(int i=0;i<8;++i)n|=std::uint64_t(static_cast<unsigned char>(b[p++]))<<(8*i);return n;
}
std::uint32_t checksum(const std::string& s){
    std::uint32_t crc=0xffffffffu;
    for(unsigned char c:s){crc^=c;for(int i=0;i<8;++i)crc=(crc>>1)^(0xedb88320u & (0u-(crc&1u)));}
    return ~crc;
}
void bounded_string(std::string& b,const std::string& s){
    if(s.size()>1024*1024)throw std::invalid_argument("WAL field exceeds 1 MiB");
    u32(b,static_cast<std::uint32_t>(s.size()));b+=s;
}
std::string get_string(const std::string& b,std::size_t& p){
    auto n=read32(b,p);
    if(n>1024*1024 || n>b.size()-p)throw std::runtime_error("corrupt WAL string");
    auto result=b.substr(p,n);p+=n;return result;
}
std::string encode(std::uint64_t sequence,const std::vector<DurableMutation>& changes){
    if(changes.size()>10000)throw std::invalid_argument("too many WAL changes");
    std::string body;u64(body,sequence);u32(body,static_cast<std::uint32_t>(changes.size()));
    for(const auto& m:changes){
        bounded_string(body,m.key);
        body.push_back(m.value?'P':'D');
        if(m.value){bounded_string(body,m.value->category);bounded_string(body,m.value->value);}
        if(body.size()>kMaxFrame)throw std::invalid_argument("WAL batch too large");
    }
    return body;
}
std::pair<std::uint64_t,std::vector<DurableMutation>> decode(const std::string& body){
    std::size_t p=0;auto n=read64(body,p);auto count=read32(body,p);
    if(count>10000)throw std::runtime_error("invalid WAL mutation count");
    std::vector<DurableMutation> changes;changes.reserve(count);
    for(std::uint32_t i=0;i<count;++i){
        auto key=get_string(body,p);
        if(p>=body.size())throw std::runtime_error("invalid WAL operation");
        char op=body[p++];
        if(op=='P'){auto category=get_string(body,p),value=get_string(body,p);changes.push_back({std::move(key),Record{std::move(category),std::move(value)}});}
        else if(op=='D')changes.push_back({std::move(key),std::nullopt});
        else throw std::runtime_error("unknown WAL operation");
    }
    if(p!=body.size())throw std::runtime_error("trailing bytes in WAL frame");
    return {n,std::move(changes)};
}
void apply_to(Database& db,const std::vector<DurableMutation>& changes){
    auto tx=db.begin();
    for(const auto& m:changes) {if(m.value)tx.put(m.key,*m.value);else tx.erase(m.key);}
    if(!tx.commit())throw std::runtime_error("unexpected in-process WAL transaction conflict");
}
void durable_append(const std::string& path,const std::string& frame){
    FILE* f=std::fopen(path.c_str(),"ab");
    if(!f)throw std::runtime_error("cannot open WAL for append");
    bool ok=(std::fwrite(frame.data(),1,frame.size(),f)==frame.size()) && std::fflush(f)==0;
    if(ok){
#ifdef _WIN32
        ok=_commit(_fileno(f))==0;
#else
        ok=::fsync(::fileno(f))==0;
#endif
    }
    if(std::fclose(f)!=0)ok=false;
    if(!ok)throw std::runtime_error("WAL write or sync failed; store must be reopened");
}
}
DurableStore::DurableStore(std::string log_path):path_(std::move(log_path)){
    if(path_.empty())throw std::invalid_argument("WAL path is empty");
    recover();
}
void DurableStore::recover(){
    std::ifstream in(path_,std::ios::binary);
    if(!in){
        if(std::filesystem::exists(path_))throw std::runtime_error("WAL exists but cannot be read");
        return;
    }
    std::uintmax_t valid_end=0;
    while(true){
        char head[8];
        in.read(head,8);
        auto got=in.gcount();
        if(got==0)break;
        if(got<8){in.close();std::filesystem::resize_file(path_,valid_end);return;}
        std::string h(head,8);std::size_t p=0;
        auto length=read32(h,p),crc=read32(h,p);
        if(length<12 || length>kMaxFrame)throw std::runtime_error("invalid WAL frame length");
        std::string body(length,'\0');
        in.read(body.data(),length);
        if(in.gcount()!=static_cast<std::streamsize>(length)){
            in.close();std::filesystem::resize_file(path_,valid_end);return;
        }
        if(checksum(body)!=crc)throw std::runtime_error("WAL checksum mismatch");
        auto frame=decode(body);
        if(frame.first!=sequence_+1)throw std::runtime_error("WAL sequence gap");
        apply_to(db_,frame.second);
        ++sequence_;
        valid_end+=8+length;
    }
}
void DurableStore::apply(const std::vector<DurableMutation>& changes){
    std::lock_guard<std::mutex> guard(mutex_);
    if(failed_)throw std::runtime_error("WAL failed; reopen store");
    if(changes.empty())return;
    auto body=encode(sequence_+1,changes);
    std::string frame;u32(frame,static_cast<std::uint32_t>(body.size()));u32(frame,checksum(body));frame+=body;
    try{
        durable_append(path_,frame);
        apply_to(db_,changes);
        ++sequence_;
    }catch(...){failed_=true;throw;}
}
void DurableStore::put(const std::string& key,Record value){apply({{key,std::move(value)}});}
void DurableStore::erase(const std::string& key){apply({{key,std::nullopt}});}
std::optional<Record> DurableStore::get(const std::string& key) const {
    std::lock_guard<std::mutex> guard(mutex_);
    return db_.get(key);
}
std::size_t DurableStore::size() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return db_.size();
}
std::uint64_t DurableStore::version() const {
    std::lock_guard<std::mutex> guard(mutex_);
    return db_.version();
}
}
