// See LICENSE for license details.

// .h 檔 這裡都只宣告變數 & functions 
// function 的內容 跟 變數的初始化，都定義在 .cc 檔案裡
// 只需要看一下 cache_sim_t 跟 fa_cache_sim_t 兩個 class 就好
// 其他不要浪費時間看，我認真

#ifndef _RISCV_CACHE_SIM_H
#define _RISCV_CACHE_SIM_H

#include "memtracer.h"
#include "common.h"
#include <cstring>
#include <string>
#include <map>
#include <cstdint>

// 大概晃過去一次
class cache_sim_t
{
 public:
  // size_t 在 64 bits 的電腦裡 就是 unsigned long long
  cache_sim_t(size_t sets, size_t ways, size_t linesz, const char* name); // constructor
  cache_sim_t(const cache_sim_t& rhs); // copy constructor
  virtual ~cache_sim_t(); // destructor

  // 這一區的 function 不用動，不重要
  void access(uint64_t addr, size_t bytes, bool store); // 存取 cache
  void clean_invalidate(uint64_t addr, size_t bytes, bool clean, bool inval); // 清除或無效化 cache
  void print_stats(); // 印出資料
  void set_miss_handler(cache_sim_t* mh) { miss_handler = mh; } // 設定 miss handler
  void set_log(bool _log) { log = _log; } // 設定是否紀錄 log

  // 微重要，建立 cache_sim_t or fa_cache_sim_t
  static cache_sim_t* construct(const char* config, const char* name);

 protected:
  // 常數設定，在 checktag() 和 victimize() 中會用到
  static const uint64_t VALID = 1ULL << 63; // VALID = 二進位 10000000000000000000000000000000000000000000000000000000000000000000000
  static const uint64_t DIRTY = 1ULL << 62; // DIRTY = 二進位 01000000000000000000000000000000000000000000000000000000000000000000000

  // 這次作業最主要的兩個 functions
  virtual uint64_t* check_tag(uint64_t addr); // 看你怎麼寫，大部分人都沒改到這裡
  virtual uint64_t victimize(uint64_t addr); // 這次作業就是要改這裡

  cache_sim_t* miss_handler; // 不知道在尬麻，不重要

  // 變數要看一下，也對照一下作業的 pdf 文件
  // size_t 在 64 bits 的電腦裡 就是 unsigned long long
  size_t sets; // 代表幾條 cache entries
  size_t ways; // 代表一條 entry 有幾個 block
  size_t linesz; // 代表 block size
  size_t idx_shift; // idx_shift = log2(linesz), initialized in init() function

  uint64_t* tags; // 儲存 tag 的 array，可以視為 cache 本體，寫入或取代 cache 的 block 時，就是對這個 array 做操作
  int* cache_way; // 儲存目前 cache 存到哪一個
  
  uint64_t read_accesses;
  uint64_t read_misses;
  uint64_t bytes_read;
  uint64_t write_accesses;
  uint64_t write_misses;
  uint64_t bytes_written;
  uint64_t writebacks;

  std::string name;
  bool log;

  void init();
};

// 以下就不用管了
class cache_memtracer_t : public memtracer_t
{
 public:
  cache_memtracer_t(const char* config, const char* name)
  {
    cache = cache_sim_t::construct(config, name);
  }
  ~cache_memtracer_t()
  {
    delete cache;
  }
  void set_miss_handler(cache_sim_t* mh)
  {
    cache->set_miss_handler(mh);
  }
  void clean_invalidate(uint64_t addr, size_t bytes, bool clean, bool inval)
  {
    cache->clean_invalidate(addr, bytes, clean, inval);
  }
  void set_log(bool log)
  {
    cache->set_log(log);
  }

 protected:
  cache_sim_t* cache;
};

class icache_sim_t : public cache_memtracer_t
{
 public:
  icache_sim_t(const char* config) : cache_memtracer_t(config, "I$") {}
  bool interested_in_range(uint64_t UNUSED begin, uint64_t UNUSED end, access_type type)
  {
    return type == FETCH;
  }
  void trace(uint64_t addr, size_t bytes, access_type type)
  {
    if (type == FETCH) cache->access(addr, bytes, false);
  }
};

class dcache_sim_t : public cache_memtracer_t
{
 public:
  dcache_sim_t(const char* config) : cache_memtracer_t(config, "D$") {}
  bool interested_in_range(uint64_t UNUSED begin, uint64_t UNUSED end, access_type type)
  {
    return type == LOAD || type == STORE;
  }
  void trace(uint64_t addr, size_t bytes, access_type type)
  {
    if (type == LOAD || type == STORE) cache->access(addr, bytes, type == STORE);
  }
};

#endif
