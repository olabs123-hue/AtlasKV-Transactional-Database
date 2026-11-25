#include "atlaskv/durable_store.hpp"
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#ifdef __unix__
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#define REQUIRE(x) do{if(!(x)){std::cerr<<"FAILED: "<<#x<<" at "<<__LINE__<<"\n";std::exit(1);}}while(false)
using namespace atlaskv;
static bool rejects(const std::string& p){
    try{DurableStore recovered(p);return false;}catch(const std::runtime_error&){return true;}
}
int main(){
    const std::string path="atlaskv_durable_test.wal";
    std::filesystem::remove(path);
    {
        DurableStore store(path);
        store.put("first",{"finance","one"});
        store.apply({{"first",Record{"finance","two"}},{"second",Record{"tech","hello"}}});
        REQUIRE(store.size()==2);
        REQUIRE(store.version()==2);
        store.erase("second");
        REQUIRE(!store.get("second"));
    }
    {
        DurableStore recovered(path);
        REQUIRE(recovered.get("first")->value=="two");
        REQUIRE(!recovered.get("second"));
        REQUIRE(recovered.version()==3);
        recovered.put("third",{"new","yes"});
    }
    {
        DurableStore recovered(path);
        REQUIRE(recovered.get("third")->value=="yes");
        REQUIRE(recovered.version()==4);
    }
    // Torn final WAL record: incomplete header is safely discarded.
    {
        std::ofstream out(path,std::ios::binary|std::ios::app);
        out.put(char(0x03));out.put(char(0x00));out.flush();
    }
    {
        DurableStore recovered(path);
        REQUIRE(recovered.version()==4);
        REQUIRE(recovered.get("first")->value=="two");
        recovered.put("after-torn",{"checks","ok"});
    }
    {
        DurableStore recovered(path);
        REQUIRE(recovered.get("after-torn")->value=="ok");
        REQUIRE(recovered.version()==5);
    }
    // An intact frame with modified payload must fail checksum validation.
    {
        std::fstream file(path,std::ios::binary|std::ios::in|std::ios::out);
        REQUIRE(bool(file));
        file.seekp(8+12);
        char c=0;file.read(&c,1);file.clear();file.seekp(8+12);file.put(static_cast<char>(c^0x10));
    }
    REQUIRE(rejects(path));
    std::filesystem::remove(path);
    // Empty and malformed inputs.
    {
        DurableStore store(path);
        store.apply({});
        REQUIRE(store.version()==0);
    }
    // Simulate an abrupt process exit after a successful WAL-synced put.
#ifdef __unix__
    pid_t pid=fork();
    REQUIRE(pid>=0);
    if(pid==0){
        try {DurableStore child(path);child.put("crash",{"recovery","survived"});_exit(0);}
        catch(...){_exit(2);}
    }
    int status=0;REQUIRE(waitpid(pid,&status,0)==pid);REQUIRE(WIFEXITED(status)&&WEXITSTATUS(status)==0);
    {DurableStore recovered(path);REQUIRE(recovered.get("crash")->value=="survived");}
#endif
    std::filesystem::remove(path);
    std::cout<<"AtlasKV durable WAL tests passed: restart, atomic batch, delete, torn tail, CRC, crash exit\n";
}
