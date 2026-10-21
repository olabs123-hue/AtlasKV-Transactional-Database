#include "atlaskv/database.hpp"
#include <chrono>
#include <iostream>
#include <string>
using namespace atlaskv;
int main() {
    Database db;
    constexpr int n=100000;
    auto start=std::chrono::steady_clock::now();
    for(int i=0;i<n;++i) {auto t=db.begin(); t.put(std::to_string(i),{"bench","value"}); if(!t.commit()) return 1;}
    auto end=std::chrono::steady_clock::now();
    double seconds=std::chrono::duration<double>(end-start).count();
    std::cout<<"Committed "<<n<<" single-key transactions in "<<seconds<<" s ("<<n/seconds<<" tx/s)\n";
    std::cout<<"In-memory only; excludes disk, networking and distributed coordination.\n";
}
