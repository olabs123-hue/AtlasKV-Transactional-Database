#include "atlaskv/database.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <vector>
#define CHECK(x) do {if(!(x)){std::cerr<<"FAIL "<<#x<<" line "<<__LINE__<<"\n";std::exit(1);}}while(false)
using namespace atlaskv;
int main(){
 Database db;
 CHECK(!db.get("absent"));
 {auto t=db.begin();t.put("a",{"tech","alpha"});t.put("b",{"finance","beta"});CHECK(t.get("a")->value=="alpha");CHECK(t.commit());}
 CHECK(db.size()==2);
 QueryStats s{};
 CHECK(db.query(Query{std::string("tech"),std::nullopt,100},&s).size()==1);
 CHECK(s.plan==QueryPlan::CategoryIndex && s.examined==1);
 CHECK(db.query(Query{std::nullopt,std::string("beta"),100},&s).size()==1);
 CHECK(s.plan==QueryPlan::FullScan && s.examined==2);
 {auto reader=db.begin();CHECK(reader.get("a")->value=="alpha");
  auto writer=db.begin();writer.put("a",{"finance","updated"});CHECK(writer.commit());
  CHECK(reader.get("a")->value=="alpha"); // historical MVCC read
  CHECK(!reader.commit()); // stale read conflict
 }
 CHECK(db.query(Query{std::string("tech"),std::nullopt,100}).empty());
 CHECK(db.query(Query{std::string("finance"),std::string("upd"),100}).size()==1);
 {auto a=db.begin();auto b=db.begin();a.put("x",{"cat","one"});b.put("x",{"cat","two"});CHECK(a.commit());CHECK(!b.commit());}
 {auto a=db.begin();auto b=db.begin();CHECK(!a.get("phantom-key"));b.put("phantom-key",{"c","v"});CHECK(b.commit());CHECK(!a.commit());}
 {auto t=db.begin();t.erase("a");CHECK(t.commit());}
 CHECK(!db.get("a"));
 {auto t=db.begin();t.put("z",{"c","v"});t.rollback();}
 CHECK(!db.get("z"));
 CHECK(db.query(Query{std::nullopt,std::nullopt,0}).empty());
 {auto t=db.begin();t.put("empty",{"cat",""});CHECK(t.commit());}
 const char* path="atlaskv_test.snapshot";
 db.save_snapshot(path);
 Database restored;restored.load_snapshot(path);
 CHECK(restored.size()==db.size());CHECK(restored.version()==db.version());
 CHECK(restored.get("b")==db.get("b"));
 CHECK(restored.query(Query{std::string("finance"),std::nullopt,100}).size()==1);
 {auto t=restored.begin();t.put("after",{"new","yes"});CHECK(t.commit());}
 CHECK(restored.get("after")->value=="yes");
 std::remove(path);
 {std::ofstream out(path,std::ios::binary);out<<"bad";}
 try {restored.load_snapshot(path);CHECK(false);}catch(const std::runtime_error&){}
 CHECK(restored.get("after").has_value());std::remove(path);
 {auto old=restored.begin();restored.save_snapshot(path);restored.load_snapshot(path);CHECK(!old.commit());std::remove(path);}
 std::atomic<int> successes{0};std::vector<std::thread> threads;
 for(int i=0;i<4;++i)threads.emplace_back([&,i]{for(int j=0;j<100;++j){auto t=restored.begin();t.put("thread-"+std::to_string(i)+"-"+std::to_string(j),{"parallel","ok"});if(t.commit())++successes;}});
 for(auto& t:threads)t.join();CHECK(successes==400);
 CHECK(restored.query(Query{std::string("parallel"),std::nullopt,1000}).size()==400);
 std::cout<<"AtlasKV tests passed: MVCC, conflicts, indexes, queries, snapshot recovery, concurrency\n";
}
