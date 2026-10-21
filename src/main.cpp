#include "atlaskv/database.hpp"
#include <iostream>
#include <sstream>
#include <string>
using namespace atlaskv;
int main(int argc,char** argv){
 Database db;
 if(argc>1)try{db.load_snapshot(argv[1]);}catch(const std::exception& e){std::cerr<<"Load failed: "<<e.what()<<"\n";return 1;}
 std::cout<<"AtlasKV interactive demo. Commands: PUT key category value | GET key | DEL key | QUERY category | ALL | SAVE path | EXIT\n";
 std::string line;
 while(std::cout<<"> " && std::getline(std::cin,line)){
  std::istringstream in(line);std::string cmd,key,category,value;in>>cmd;
  try {
   if(cmd=="EXIT"||cmd=="QUIT")break;
   if(cmd=="PUT") {in>>key>>category;std::getline(in,value);if(!value.empty()&&value[0]==' ')value.erase(0,1);if(key.empty()||category.empty()){std::cout<<"Usage: PUT key category value\n";continue;}auto t=db.begin();t.put(key,{category,value});std::cout<<(t.commit()?"OK":"CONFLICT")<<"\n";}
   else if(cmd=="GET") {in>>key;auto r=db.get(key);if(r)std::cout<<r->category<<" | "<<r->value<<"\n";else std::cout<<"NOT FOUND\n";}
   else if(cmd=="DEL") {in>>key;auto t=db.begin();t.erase(key);std::cout<<(t.commit()?"OK":"CONFLICT")<<"\n";}
   else if(cmd=="QUERY"||cmd=="ALL") {if(cmd=="QUERY")in>>category;Query q;if(cmd=="QUERY")q.category=category;q.limit=1000;QueryStats s;auto results=db.query(q,&s);for(const auto& [k,r]:results)std::cout<<k<<" | "<<r.category<<" | "<<r.value<<"\n";std::cout<<"Plan: "<<(s.plan==QueryPlan::CategoryIndex?"CATEGORY_INDEX":"FULL_SCAN")<<", examined="<<s.examined<<", matched="<<s.matched<<"\n";}
   else if(cmd=="SAVE") {in>>key;if(key.empty()){std::cout<<"Usage: SAVE path\n";continue;}db.save_snapshot(key);std::cout<<"SAVED\n";}
   else std::cout<<"Unknown command\n";
  }catch(const std::exception& e){std::cout<<"ERROR: "<<e.what()<<"\n";}
 }
}
