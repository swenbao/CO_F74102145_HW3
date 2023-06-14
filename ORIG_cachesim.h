// See LICENSE for license details.

#ifndef _RISCV_CACHE_SIM_H
#define _RISCV_CACHE_SIM_H

#include "memtracer.h"
#include "common.h"
#include <cstring>
#include <string>
#include <map>
#include <cstdint>

class lfsr_t
{
 public:
  lfsr_t() : reg(1) {}
  lfsr_t(const lfsr_t& lfsr) : reg(lfsr.reg) {}
  uint32_t next() { return reg = (reg>>1)^(-(reg&1) & 0xd0000001); }
 private:
  uint32_t reg;
};

class cache_sim_t
{
 public:
  // size_t 就是 unsigned long long 在 64 bits 的電腦裡
  cache_sim_t(size_t sets, size_t ways, size_t linesz, const char* name); // constructor
  cache_sim_t(const cache_sim_t& rhs); // copy constructor
  virtual ~cache_sim_t(); // destructor

  void access(uint64_t addr, size_t bytes, bool store); // 存取 cache
  void clean_invalidate(uint64_t addr, size_t bytes, bool clean, bool inval); // 清除或無效化 cache
  void print_stats(); // 印出統計資料
  void set_miss_handler(cache_sim_t* mh) { miss_handler = mh; } // 設定 miss handler
  void set_log(bool _log) { log = _log; } // 設定是否紀錄 log

  // 建立 cache_sim_t or fully associative cache
  static cache_sim_t* construct(const char* config, const char* name);

 protected:
  static const uint64_t VALID = 1ULL << 63; // VALID = 二進位 10000000000000000000000000000000000000000000000000000000000000000000000
  static const uint64_t DIRTY = 1ULL << 62; // DIRTY = 二進位 01000000000000000000000000000000000000000000000000000000000000000000000

  virtual uint64_t* check_tag(uint64_t addr);
  virtual uint64_t victimize(uint64_t addr);

  lfsr_t lfsr;
  cache_sim_t* miss_handler;

  size_t sets; // 幾條 cache entries
  size_t ways; // 一條 entry 有幾個 block
  size_t linesz; // block size
  size_t idx_shift; // idx_shift = log2(linesz), initialized in init()

  uint64_t* tags;
  
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

// fa_cache_sim_t 是一個 Fully Associative 的 cache 模擬類別
class fa_cache_sim_t : public cache_sim_t 
{
 public:
  fa_cache_sim_t(size_t ways, size_t linesz, const char* name);
  uint64_t* check_tag(uint64_t addr); // 檢查 tag
  uint64_t victimize(uint64_t addr); // 選一個 victim
 private:
  static bool cmp(uint64_t a, uint64_t b);
  std::map<uint64_t, uint64_t> tags; // tags 用 map 實作 (python 裡面的 dictionary)
};

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


