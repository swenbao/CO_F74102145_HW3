// See LICENSE for license details.

#include "cachesim.h"
#include "common.h"
#include <cstdlib>
#include <iostream>
#include <iomanip>

// Constructor for cache_sim_t
// parameters : sets, ways, linesz(block size / line size), name
cache_sim_t::cache_sim_t(size_t _sets, size_t _ways, size_t _linesz, const char* _name)
: sets(_sets), ways(_ways), linesz(_linesz), name(_name), log(false) //The sets, ways, and linesz members are initialized with the values of the _sets, _ways, and _linesz parameters, respectively. The name member is initialized with the value of the _name parameter. The log member is initialized with false.
{
  init(); // initializes the rest of the object
}

// error message & exit(1)
// handle when encounter wrong input
static void help()
{
  std::cerr << "Cache configurations must be of the form" << std::endl;
  std::cerr << "  sets:ways:blocksize" << std::endl;
  std::cerr << "where sets, ways, and blocksize are positive integers, with" << std::endl;
  std::cerr << "sets and blocksize both powers of two and blocksize at least 8." << std::endl;
  exit(1);
}

// 
cache_sim_t* cache_sim_t::construct(const char* config, const char* name)
{
  // config = "sets:ways:blocksize"
  const char* wp = strchr(config, ':');
  if (!wp++) help(); 
  // if(!wp) help(); 如果沒有第一個 ':' 則 print error msg & exit(1)
  // wp++; wp 指向 ways 的位置
  const char* bp = strchr(wp, ':');
  if (!bp++) help();
  // if(!bp) help(); 如果沒有第二個 ':' 則 print error msg & exit(1)
  // bp++;  bp 指向 blocksize 的位置

  // 把 "sets:ways:blocksize" 分出來三個數字
  size_t sets = atoi(std::string(config, wp).c_str());
  size_t ways = atoi(std::string(wp, bp).c_str());
  size_t linesz = atoi(bp);

  if (ways > 4 /* empirical */ && sets == 1)  // 經驗上來看，如果 ways > 4 且 sets = 1 則 return new fully-associative caches
    return new fa_cache_sim_t(ways, linesz, name);
  return new cache_sim_t(sets, ways, linesz, name); // else return new 正常的 cache
}

// 初始化函數，檢查 sets 和 linesz 是否符合規定，並初始化其他成員變數
void cache_sim_t::init()
{
  if (sets == 0 || (sets & (sets-1))) // if sets = 0 or sets is a power of two, then print error message & exit(1)
    help();
  if (linesz < 8 || (linesz & (linesz-1))) // if linesz < 8 or linesz is a power of two, then print error message & exit(1)
    help();

  idx_shift = 0;
  for (size_t x = linesz; x>1; x >>= 1) // idx_shift = log2(linesz)
    idx_shift++;

  tags = new uint64_t[sets*ways]();  // 一個 entry 有 ways 個 block，總共有 sets 個 entries，所以 tags 有 sets*ways 格
  read_accesses = 0; 
  read_misses = 0;
  bytes_read = 0;
  write_accesses = 0;
  write_misses = 0;
  bytes_written = 0;
  writebacks = 0;

  miss_handler = NULL;
}

// copy constructor
// 複製 rhs 物件的所有成員變數，並複製 tags 陣列
cache_sim_t::cache_sim_t(const cache_sim_t& rhs)
 : sets(rhs.sets), ways(rhs.ways), linesz(rhs.linesz),
   idx_shift(rhs.idx_shift), name(rhs.name), log(false)
{
  tags = new uint64_t[sets*ways];
  memcpy(tags, rhs.tags, sets*ways*sizeof(uint64_t));
}

// 解構子，印出統計資料並釋放 tags 陣列的記憶體
cache_sim_t::~cache_sim_t()
{
  print_stats();
  delete [] tags;
}

// 印出統計資料的函數
void cache_sim_t::print_stats()
{
  // 如果讀取和寫入的次數都為 0，則不印出任何資訊並直接返回
  if (read_accesses + write_accesses == 0)
    return;

  // 計算 miss rate，即 cache miss 的次數除以 cache 存取的總次數，並轉換為百分比
  float mr = 100.0f*(read_misses+write_misses)/(read_accesses+write_accesses);

   // 設定輸出的精度為小數點後三位，並固定輸出小數點
  std::cout << std::setprecision(3) << std::fixed;

  // 印出各項統計資訊
  std::cout << name << " ";
  std::cout << "Bytes Read:            " << bytes_read << std::endl;
  std::cout << name << " ";
  std::cout << "Bytes Written:         " << bytes_written << std::endl;
  std::cout << name << " ";
  std::cout << "Read Accesses:         " << read_accesses << std::endl;
  std::cout << name << " ";
  std::cout << "Write Accesses:        " << write_accesses << std::endl;
  std::cout << name << " ";
  std::cout << "Read Misses:           " << read_misses << std::endl;
  std::cout << name << " ";
  std::cout << "Write Misses:          " << write_misses << std::endl;
  std::cout << name << " ";
  std::cout << "Writebacks:            " << writebacks << std::endl;
  std::cout << name << " ";
  std::cout << "Miss Rate:             " << mr << '%' << std::endl;
}

// 檢查 tags 是否一樣
// parameter : addr 要訪問的記憶體地址
uint64_t* cache_sim_t::check_tag(uint64_t addr)
{
  // Address 是由 | tag | index | offset | 組成

  // 透過 bitwise operator 萃取出 index
  // addr >> idx_shift 是透過右移將 offset 去除
  // AND (sets-1) 留下 index 的 bits
  size_t idx = (addr >> idx_shift) & (sets-1); 

  // 透過 bitwise operator 萃取出 | 1 | tag | index | 我還不知道為什麼
  // addr >> idx_shift 是透過右移將 offset 去除
  // OR VALID, VALID = 二進位 10000000000000000000000000000000000000000000000000000000000000000000000 
  size_t tag = (addr >> idx_shift) | VALID;

  // 對於每一種方式（ways），檢查是否有標籤匹配。如果有，則返回該標籤的指針。 這邊我真的開始看不懂了
  for (size_t i = 0; i < ways; i++)
    if (tag == (tags[idx*ways + i] & ~DIRTY))
      return &tags[idx*ways + i];
  // 如果沒有找到匹配的標籤，則返回 NULL。
  return NULL;
}


uint64_t cache_sim_t::victimize(uint64_t addr)
{
  // 計算 cache 的 index，與 check_tag 函數中的計算方式相同。
  size_t idx = (addr >> idx_shift) & (sets-1);
  // 使用線性反饋移位暫存器（LFSR）生成一個隨機的方式（way）。
  size_t way = lfsr.next() % ways;
  // 取出被選中的 cache 線（即受害者）的標籤。
  uint64_t victim = tags[idx*ways + way];
  // 取出被選中的 cache 線（即受害者）的標籤。
  tags[idx*ways + way] = (addr >> idx_shift) | VALID;
  // 返回受害者的標籤。
  return victim;
}

void cache_sim_t::access(uint64_t addr, size_t bytes, bool store)
{
  // 根據訪問類型（讀取或寫入），增加相應的訪問計數。
  store ? write_accesses++ : read_accesses++;
  // 根據訪問類型（讀取或寫入），增加相應的字節數。
  (store ? bytes_written : bytes_read) += bytes;

  // 檢查該地址是否在 cache 中。
  uint64_t* hit_way = check_tag(addr);
  // 如果該地址在 cache 中（即 cache hit），則檢查是否為寫入操作
  // 如果是寫入操作，則設置 dirty 位，然後返回。
  if (likely(hit_way != NULL))
  {
    if (store)
      *hit_way |= DIRTY;
    return;
  }

  // 如果該地址不在 cache 中（即 cache 未命中），則根據訪問類型（讀取或寫入），增加相應的未命中計數。
  store ? write_misses++ : read_misses++;
  // 如果啟用了日誌，則輸出未命中的訊息。
  if (log)
  {
    std::cerr << name << " "
              << (store ? "write" : "read") << " miss 0x"
              << std::hex << addr << std::endl;
  }

  // 如果 cache 未命中，則選擇一個受害者來替換。
  uint64_t victim = victimize(addr);

  // 如果受害者是有效的並且是 dirty 的，則將其寫回到下一級 cache 或主記憶體，並增加寫回計數
  if ((victim & (VALID | DIRTY)) == (VALID | DIRTY))
  {
    uint64_t dirty_addr = (victim & ~(VALID | DIRTY)) << idx_shift;
    if (miss_handler)
      miss_handler->access(dirty_addr, linesz, true);
    writebacks++;
  }

  // 從下一級 cache 或主記憶體讀取新的資料。
  if (miss_handler)
    miss_handler->access(addr & ~(linesz-1), linesz, false);

  // 如果是寫入操作，則設置新資料的 dirty 位。
  if (store)
    *check_tag(addr) |= DIRTY;
}

void cache_sim_t::clean_invalidate(uint64_t addr, size_t bytes, bool clean, bool inval)
{
  uint64_t start_addr = addr & ~(linesz-1);
  uint64_t end_addr = (addr + bytes + linesz-1) & ~(linesz-1);
  uint64_t cur_addr = start_addr;
  while (cur_addr < end_addr) {
    uint64_t* hit_way = check_tag(cur_addr);
    if (likely(hit_way != NULL))
    {
      if (clean) {
        if (*hit_way & DIRTY) {
          writebacks++;
          *hit_way &= ~DIRTY;
        }
      }

      if (inval)
        *hit_way &= ~VALID;
    }
    cur_addr += linesz;
  }
  if (miss_handler)
    miss_handler->clean_invalidate(addr, bytes, clean, inval);
}

fa_cache_sim_t::fa_cache_sim_t(size_t ways, size_t linesz, const char* name)
  : cache_sim_t(1, ways, linesz, name)
{
}

uint64_t* fa_cache_sim_t::check_tag(uint64_t addr)
{
  // 在快取標籤中查找給定地址的標籤
  auto it = tags.find(addr >> idx_shift);
  // 如果找到了標籤，則返回指向該標籤的指針，否則返回 NULL
  return it == tags.end() ? NULL : &it->second;
}

uint64_t fa_cache_sim_t::victimize(uint64_t addr)
{
  uint64_t old_tag = 0;
   // 如果快取已經滿了（即快取標籤的數量等於快取路徑的數量）
  if (tags.size() == ways)
  {
    // 選擇一個隨機的快取路徑來替換
    auto it = tags.begin();
    std::advance(it, lfsr.next() % ways);

    // 保存被替換的標籤
    // it -> first 得到 key
    // it -> second 得到 value
    old_tag = it->second; 

    // 從快取中移除被替換的標籤
    tags.erase(it);
  }
  // 將新的地址添加到快取中
  tags[addr >> idx_shift] = (addr >> idx_shift) | VALID;
   
  // 返回被替換的標籤
  return old_tag;
}
