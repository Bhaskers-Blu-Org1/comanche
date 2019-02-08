#ifndef __NUPM_REGION_H__
#define __NUPM_REGION_H__

#include <forward_list>
#include <unordered_set>
#include "rc_alloc_avl.h"
#include "mappers.h"
#include <tbb/cache_aligned_allocator.h>
#include <tbb/scalable_allocator.h>

namespace nupm
{
  
class Region
{
  friend class Region_map;

  static constexpr unsigned _debug_level = 3;
  
  struct ptr_t {
    void * ptr;
  } __attribute__((aligned(8)));
  
  //  using list_t = std::forward_list<ptr_t,tbb::cache_aligned_allocator<ptr_t>>;
  using list_t = std::forward_list<ptr_t,tbb::scalable_allocator<ptr_t>>;
  //using list_t = std::forward_list<ptr_t>;
protected:
  Region(void * region_ptr,
         const size_t region_size,
         const size_t object_size)
  {
    if(_debug_level > 1)
      PLOG("Region ctor: region_base=%p region_size=%lu objsize=%lu objcap=%lu",
           region_ptr, region_size, object_size, region_size / object_size);    
    
    if(region_size % object_size)
      throw std::invalid_argument("Region: objects must fit exactly into region size");

    if(object_size < 8)
      throw std::invalid_argument("Region: minimum object size is 8 bytes");

    _base = reinterpret_cast<addr_t>(region_ptr);
    _top = _base + region_size;
    
    byte * rp = static_cast<byte*>(region_ptr);
    const auto count = region_size / object_size;
    for(size_t i=0;i<count; i++) {
      _free.push_front({rp});
      rp+=object_size;
    }
  }

  bool in_range(void * p) {
    auto addr = reinterpret_cast<addr_t>(p);
    return (addr >= _base && addr < _top);
  }
  
  void * allocate() {
    if(_free.empty())
      return nullptr;
    void * p = _free.front().ptr;
    _free.pop_front();
    _used.push_front({p});
    return p;
  }

  bool free(void * p) {
    auto i = _used.begin();
    if(i->ptr == p)
      _used.pop_front();

    auto last = i;
    i++;
    while(i != _used.end()) {
      if(i->ptr == p) {
        _used.erase_after(last);
        _free.push_front({p});
        /* TODO: we could check for total free region */
        return true;
      }
      i++;
      last++;
    }
    return false;
  }

  bool allocate_at(void * ptr) {
    auto i = _free.begin();
    if(i->ptr == ptr) {
      _free.pop_front();
      _used.push_front({ptr});
    }
    auto last = i;
    i++;
    while(i != _free.end()) {
      if(i->ptr == ptr) {
        _free.erase_after(last);  
        _used.push_front({ptr});        
        return true;
      }
    }
    return false;
  }
    
private:
  addr_t _base, _top;
  list_t _free;
  list_t _used; /* we could do with this, but it guards against misuse */
};

  

/**
 * @brief      Region-based heap allocator.  Uses 2^n sized bucket strategy.
 */
class Region_map
{
  static constexpr unsigned NUM_BUCKETS = 64;
  static constexpr int MAX_NUMA_ZONES = 2;
  static constexpr unsigned _debug_level = 3;
  
public:
  Region_map() {
  }
  
  ~Region_map() {
  }

  void add_arena(void * arena_base, size_t arena_length, int numa_node) {
    if(numa_node < 0 || numa_node >= MAX_NUMA_ZONES)
      throw std::invalid_argument("numa node outside max range");
    
    _arena_allocator.add_managed_region(arena_base, arena_length, numa_node);
  }

  void * allocate(size_t size, int numa_node) {
    if(unlikely(numa_node < 0 || numa_node >= MAX_NUMA_ZONES))
      throw std::invalid_argument("numa node outside max range");

    void * p = allocate_from_existing_region(size, numa_node);
    if(!p)
      p = allocate_from_new_region(size, numa_node);
    if(!p)
      throw std::bad_alloc();
    return p;
  }  

  void free(void * p, int numa_node, size_t object_size) {
    if(unlikely(numa_node < 0 || numa_node >= MAX_NUMA_ZONES))
      throw std::invalid_argument("numa node outside max range");

    if(object_size > 0) {
      auto bucket = _mapper.bucket(object_size);
      if(bucket >= NUM_BUCKETS)
        throw std::out_of_range("object size beyond available buckets");
      /* search regions */
      for(auto& region : _buckets[numa_node][bucket]) {
        if(region->in_range(p) && region->free(p))
          return;
      }
    }
    else {
      /* search all the buckets */
      for(unsigned i=0;i<NUM_BUCKETS;i++) {
        /* search regions */
        for(auto& region : _buckets[numa_node][i]) {          
          if(region->in_range(p) && region->free(p))
            return;
        }
      }
    }
    throw std::invalid_argument("bad pointer to free");
  }

  /**
   * @brief      Inject a prior allocation.  Marks the memory as allocated.
   *
   * @param      ptr        The pointer
   * @param[in]  size       The size
   * @param[in]  numa_node  The numa node
   */
  void inject_allocation(void * ptr, size_t size, int numa_node) {
    assert(ptr);
    assert(size > 0);
    if(numa_node < 0 || numa_node >= MAX_NUMA_ZONES)
      throw std::invalid_argument("numa node outside max range");
    
    auto bucket = _mapper.bucket(size);
    
    /* check existing regions */
    for(auto& region : _buckets[numa_node][bucket]) {
      if(region->in_range(ptr) && region->allocate_at(ptr))
        return;
    }
    /* otherwise we have to create the region at the correct position  */
    auto region_size = _mapper.region_size(size);
    assert(region_size > 0);
    auto region_base = _mapper.base(ptr);
    assert(region_base);
    _arena_allocator.inject_allocation(region_base, region_size, numa_node);
    auto region_object_size = _mapper.rounded_up_object_size(size);
    Region * new_region = new Region(region_base, region_size, region_object_size);
    attach_region(new_region, region_object_size, numa_node);
    new_region->allocate_at(ptr);
  }

private:

  
  void * allocate_from_existing_region(size_t object_size, int numa_node) {
    auto bucket = _mapper.bucket(object_size);
    if(bucket >= NUM_BUCKETS)
      throw std::out_of_range("object size beyond available buckets");
    for(auto& region : _buckets[numa_node][bucket]) {
      void * p = region->allocate();
      if(p)
        return p;
    }
    return nullptr;
  }

  void * allocate_from_new_region(size_t object_size, int numa_node) {
    auto bucket = _mapper.bucket(object_size);
    if(bucket >= NUM_BUCKETS)
      throw std::out_of_range("object size beyond available buckets");
    auto region_size = _mapper.region_size(object_size);
    assert(region_size > 0);
    auto region_base = _arena_allocator.alloc(region_size, numa_node, object_size); /* align by object size */
    assert(region_base);
    auto region_object_size = _mapper.rounded_up_object_size(object_size);
    Region * new_region = new Region(region_base, region_size, region_object_size);
    void * rp = new_region->allocate();
    attach_region(new_region, object_size, numa_node);
    return rp;
  }

  void attach_region(Region * region, size_t object_size, int numa_node) {
    auto bucket = _mapper.bucket(object_size);
    if(bucket >= NUM_BUCKETS)
      throw std::out_of_range("object size beyond available buckets");
    _buckets[numa_node][bucket].push_front(region);
  };

  void delete_region(Region * region) {
    for(unsigned z=0;z<MAX_NUMA_ZONES;z++) {
      for(unsigned i=0;i<NUM_BUCKETS;i++) {
        auto iter = _buckets[z][i].begin();
        do {
          if(*iter == region) {
            _buckets[z][i].erase(iter);
            return;
          }
          iter++;
        }
        while(iter != _buckets[z][i].end());
      }
    }
    throw std::invalid_argument("delete_region: region not found");
  }

private:
  Log2_bucket_mapper  _mapper;
  nupm::Rca_AVL       _arena_allocator;
  std::list<Region *> _buckets[MAX_NUMA_ZONES][NUM_BUCKETS];
};

} // nupm
#endif