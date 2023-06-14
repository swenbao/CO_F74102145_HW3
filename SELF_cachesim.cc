// See LICENSE for license details.

#include "cachesim.h"
#include "common.h"
#include <cstdlib>
#include <iostream>
#include <iomanip>
#include <algorithm>
#include <limits>

// ---------------- 以下都在定義 class cache_sim_t 的 functions  ---------------//

// Constructor for cache_sim_t
// parameters : sets, ways, linesz(意思是 block size / line size), name
cache_sim_t::cache_sim_t(size_t _sets, size_t _ways, size_t _linesz, const char* _name)
: sets(_sets), ways(_ways), linesz(_linesz), name(_name), log(false) //The sets, ways, and linesz members are initialized with the values of the _sets, _ways, and _linesz parameters, respectively. The name member is initialized with the value of the _name parameter. The log member is initialized with false.
{
  init(); // initializes the rest of the object of cache_sim_t
  // if you want to 
}

// 處理錯誤參數
// print error message & exit(1)
static void help()
{
  std::cerr << "Cache configurations must be of the form" << std::endl;
  std::cerr << "  sets:ways:blocksize" << std::endl;
  std::cerr << "where sets, ways, and blocksize are positive integers, with" << std::endl;
  std::cerr << "sets and blocksize both powers of two and blocksize at least 8." << std::endl;
  exit(1);
}

// 傳入兩個字串
// config 格式為 "sets:ways:blocksize"， 
// name 名字我不知道要拿來尬麻
cache_sim_t* cache_sim_t::construct(const char* config, const char* name)
{
  // config = "sets:ways:blocksize"
  const char* wp = strchr(config, ':'); //找到 config (指向 "sets:ways:blocksize")字串中第一個出現的 ':'，回傳 pointer 給 wp，沒找到 ':' 則回傳 NULL
  if (!wp++) help(); // 這條程式碼要分兩步驟看，以下：
  // if(!wp) help(); 如果沒有第一個 ':' 則呼叫 help() 來 print error msg & exit(1)
  // wp++; 原本 wp 指向 ':'， ++後則會指向 ways 第一個字元的位置
  const char* bp = strchr(wp, ':'); //找到 wp (指向 "ways:blocksize"）字串中第一個出現的 ':'，回傳 pointer 給 bp，沒找到 ':' 則回傳 NULL
  if (!bp++) help(); // 這條程式碼要分兩步驟看，以下：
  // if(!bp) help(); 如果沒有第一個 ':' 則呼叫 help() 來 print error msg & exit(1)
  // bp++; 原本 bp 指向 ':'， ++後則會指向 blocksize 第一個字元的位置

  // 把 "sets:ways:blocksize" 字串切割成 sets, ways, blocksize，並轉換成 int (atoi)
  size_t sets = atoi(std::string(config, wp).c_str());
  size_t ways = atoi(std::string(wp, bp).c_str());
  size_t linesz = atoi(bp);

  // ---- 注意一下這裡 ---- //
  //if (ways > 4 /* empirical */ && sets == 1)  // 經驗上來看，如果 ways > 4 且 sets = 1 則 return new fully-associative cache，簡稱 fa_cache
  //  return new fa_cache_sim_t(ways, linesz, name);
  return new cache_sim_t(sets, ways, linesz, name); // else return new 正常的 cache
  // fa_cache_sim_t(fully-associative cache) 跟 cache_sim_t (一般cache) 實作不一樣，下面有他們各自的 victimize() & checktag() function
  // 如果不改這段程式碼，那你要修改 fa_cache_sim_t 跟 cache_sim_t 的 victimize() & checktag() function
  // 如果你比較懶一點，只想改正常 cache_sim_t 的 victimize() & chekctag() 那你要把
  // ```
  //  if (ways > 4 /* empirical */ && sets == 1) 
  //    return new fa_cache_sim_t(ways, linesz, name);
  // ```
  // 註解掉，醬子程式就不會動到 fa_cache_sim_t，而都是 new cache_sim_t
}

// 初始化函數，檢查 sets 和 linesz (block size / line size) 是否符合規定，並初始化其他成員變數
void cache_sim_t::init()
{
  if (sets == 0 || (sets & (sets-1))) // if sets = 0 or sets is not a power of two, then print error message & exit(1)
    help();
  if (linesz < 8 || (linesz & (linesz-1))) // if linesz < 8 or linesz is a power of two, then print error message & exit(1)
    help();

  // 這段在做 idx_shift = log2(linesz)
  idx_shift = 0;
  for (size_t x = linesz; x>1; x >>= 1) 
    idx_shift++;

  tags = new uint64_t[sets*ways]();  // 一個 entry 有 ways 個 block，總共有 sets 個 entries，所以 tags 有 sets*ways 格
  timer = new uint64_t[sets*ways](); // timer for every block
  std::fill(timer, timer + sets*ways, 0);
  
  read_accesses = 0; 
  read_misses = 0;
  bytes_read = 0;
  write_accesses = 0;
  write_misses = 0;
  bytes_written = 0;
  writebacks = 0;

  miss_handler = NULL;
}

// 這不重要
// copy constructor 
// 複製 rhs 物件的所有成員變數，並複製 tags 陣列
cache_sim_t::cache_sim_t(const cache_sim_t& rhs)
 : sets(rhs.sets), ways(rhs.ways), linesz(rhs.linesz),
   idx_shift(rhs.idx_shift), name(rhs.name), log(false)
{
  tags = new uint64_t[sets*ways];
  memcpy(tags, rhs.tags, sets*ways*sizeof(uint64_t));
}

// 這不重要
// 解構子，印出統計資料並釋放 tags 陣列的記憶體
cache_sim_t::~cache_sim_t()
{
  print_stats();
  delete [] tags;
}

// 這不重要
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
  for(uint64_t i = 0; i < sets*ways; i++) // 跑 cache 中所有的 block
    if(tags[i] & VALID) // if VALID
      timer[i] ++; // timer count up 1
  
  // Address 是由 | tag | index | offset | 組成

  // 透過 bitwise operator 萃取出 index
  // addr >> idx_shift 是透過右移將 offset 去除
  // AND (sets-1) 將 tag 區域設為 0，留下 index 的 bits
  size_t idx = (addr >> idx_shift) & (sets-1); 

  // 透過 bitwise operator 組合出 | 1 | 0 | tag | index | ，前面的 1 是 Valid bit，0 是 Dirty bit
  // addr >> idx_shift 是透過右移將 offset 去除
  // VALID 在 .h 檔中有定義 = 二進位 10000000000000000000000000000000000000000000000000000000000000000000000 
  // OR VALID 可以設定 最左邊那個 bit（MSB) 為 1，就是設置其為 Valid 的意思
  // offset 會 >= 3 個 bits，因為 blocksize 規定要 >= 8 = 2^3
  // 所以右移將 offset 去除後，最左邊會空出至少 3 個 bits 的空間
  // 而 最左邊的 bit (MSB) 充當 Valid bit 的空間，第二左邊的 bit (second-most significant bit) 充當 Dirty bit 的空間
  size_t tag = (addr >> idx_shift) | VALID;

  // 對於 index 那一條，檢查後面所有 tags 是否有匹配。
  // 如果有，則返回該標籤的指針。
  for (size_t i = 0; i < ways; i++) // 這個迴圈是為了跑過同一個 index 後面所有 block
    // tags[] 儲存了前面放入的所有 tag
    // 檢查這次的 tag 是否已經存在於 tag[] 中（意思是是否存在 cache 中）
    // DIRTY 在 .h 檔中有定義 = 二進位 01000000000000000000000000000000000000000000000000000000000000000000000 
    // & ~DIRTY 是因為，存在於 tags[] 中的 舊tag 們，其 Dirty bit 有可能會在 access() 中被設置為 1
    // 但這個階段的 tag 的 dirty bit 為 0（原因看上一面那一段），可能會發生 | tag | index | 明明一樣，但 dirty bit 不同而被判定為 miss 的情況
    // 所以從 tags[] 中抓出來判斷時，要把 dirty bit 屏蔽掉，即 & ~DIRTY
    if (tag == (tags[idx*ways + i] & ~DIRTY)){ // hit
      timer[idx*ways + i] = 0;
      return &tags[idx*ways + i]; 
    }
  // miss，則返回 NULL。
  return NULL;
}


uint64_t cache_sim_t::victimize(uint64_t addr)
{
  // 計算 cache 的 index，與 check_tag 函數中的計算方式相同。
  size_t idx = (addr >> idx_shift) & (sets-1);
  
  uint64_t* min_ptr = std::min_element(timer+(idx*ways), timer+(idx*ways) + ways);
  uint64_t way = min_ptr - (timer+(idx*ways));

  // 取出被選中的 tag
  uint64_t victim = tags[idx*ways + way];
  // 將原本的位置，塞入新的 tag 
  tags[idx*ways + way] = (addr >> idx_shift) | VALID;
  // 將新的 block timer 設置為 0
  timer[idx*ways + way] = 0;
  // return 被選中丟掉的 tag 值
  return victim;
}

// 可以看過去這一段，但不要執著，不太是實作的重點
void cache_sim_t::access(uint64_t addr, size_t bytes, bool store)
{
  // 根據訪問類型（讀取或寫入），增加相應的訪問計數。
  store ? write_accesses++ : read_accesses++;
  // 根據訪問類型（讀取或寫入），增加相應的字節數。
  (store ? bytes_written : bytes_read) += bytes;

  // 檢查該地址是否在 cache 中。
  uint64_t* hit_way = check_tag(addr);
  // 如果該地址在 cache 中（即 cache hit），則檢查是否為寫入操作
  // 如果是寫入操作，則設置 Dirty bit，然後返回。
  // Dirty bit 就是在這裡被變成 1 的，回想在 check tag 中為什麼要屏蔽 Dirty bit
  if (likely(hit_way != NULL))
  {
    if (store)
      *hit_way |= DIRTY;
    return;
  }

  // 如果該地址不在 cache 中（即 cache 未命中），則根據訪問類型（讀取或寫入），增加相應的未命中計數。
  store ? write_misses++ : read_misses++;
  // 如果啟用了 log，則輸出未命中的訊息。
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

// 不用看，我也不想看
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


