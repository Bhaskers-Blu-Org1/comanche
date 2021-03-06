/*
   Copyright [2017-2019] [IBM Corporation]
   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at
       http://www.apache.org/licenses/LICENSE-2.0
   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License.
*/
#include <stdint.h>
#include <string.h>
#include <memory>
#include <api/block_itf.h>
#include "exceptions.h"

#define VOLUME_INFO_MAX_NAME 64

typedef int      status_t;
typedef uint64_t io_buffer_t;
typedef uint64_t workid_t;
typedef uint64_t addr_t;
typedef void (*io_callback_t)(uint64_t, void*, void*);

class Block;

/* Class to manage release of io_buffer_t */
class IO_buffer
{
public:
  IO_buffer() {}
  IO_buffer(Component::IBlock_device * owner, uint64_t iob);
  ~IO_buffer();
  void show() const;

  void operator=(const IO_buffer& obj);
  
  uint64_t get_iob() const { return _iob; }
  
private:
  Component::IBlock_device * _owner;
  uint64_t _iob;
};

class Block
{
  friend class IO_buffer;
  
public:
  Block(const char * config, unsigned long cpu_mask, const char * lib_name = nullptr);
  ~Block();

  IO_buffer* allocate_io_buffer(size_t size, unsigned alignment, int numa_node) {
    return new IO_buffer(_obj,_obj->allocate_io_buffer(size, alignment, numa_node));
  }

  std::string  get_volume_info() {
    std::stringstream ss;
    Component::VOLUME_INFO vi;
    _obj->get_volume_info(vi);
    ss << "{\"name\":\"" << vi.volume_name << "\"";
    ss << ",\"block_size\":" << vi.block_size;
    ss << ",\"max_lba\":" << vi.max_lba;
    ss << ",\"max_dma_len\":" << vi.max_dma_len;
    ss << ",\"hash_id\":" << vi.hash_id;
    ss << ",\"sw_queue_count\":" << vi.sw_queue_count;
    ss << "}";
    return ss.str();
  }

protected:
  void free_io_buffer(io_buffer_t iob) { _obj->free_io_buffer(iob); }
private:
  Component::IBlock_device * _obj;
};
/*
  unsigned block_size;
  uint64_t hash_id;
  uint64_t max_lba;
  uint64_t max_dma_len;
  unsigned distributed : 1;
  unsigned sw_queue_count
*/

// #ifdef __cplusplus
// extern "C"
// {
// #endif
  
//   typedef int      status_t;
//   typedef uint64_t io_buffer_t;
//   typedef uint64_t workid_t;
//   typedef uint64_t addr_t;
//   typedef void (*io_callback_t)(uint64_t, void*, void*);  
//   typedef struct {
//     void * obj;
//   } IBlock_ref; /* allows stricter type checking */
  
//   typedef struct // see block_itf.h
//   {
//     char volume_name[VOLUME_INFO_MAX_NAME];  
//     unsigned block_size;
//     uint64_t hash_id;
//     uint64_t max_lba;
//     uint64_t max_dma_len;
//     unsigned distributed : 1;
//     unsigned sw_queue_count : 7; /* counting from 0, i.e. 0 equals 1 queue */
//   } VOLUME_INFO;



//   IBlock_ref IBlock_factory__create(const char * config,
//                                     unsigned long cpu_mask,
//                                     const char * lib_name = NULL);

//   status_t IBlock__release(IBlock_ref ref);

//   /* IZerocopy_memory */
//   io_buffer_t  IBlock__allocate_io_buffer(IBlock_ref ref, size_t size, unsigned alignment, int numa_node);
//   status_t     IBlock__realloc_io_buffer(IBlock_ref ref, io_buffer_t io_mem, size_t size, unsigned alignment);
//   status_t     IBlock__free_io_buffer(IBlock_ref ref, io_buffer_t io_mem);
//   io_buffer_t  IBlock__register_memory_for_io(IBlock_ref ref, void * vaddr, addr_t paddr, size_t len);
//   void         IBlock__unregister_memory_for_io(IBlock_ref ref, void * vaddr, size_t len);
//   void *       IBlock__virt_addr(IBlock_ref ref, io_buffer_t buffer);
//   addr_t       IBlock__phys_addr(IBlock_ref ref, io_buffer_t buffer);
//   size_t       IBlock__get_size(IBlock_ref ref, io_buffer_t buffer);

//   /* IBlock_device */
//   workid_t IBlock__async_read(IBlock_ref ref,
//                               io_buffer_t buffer,
//                               uint64_t buffer_offset,
//                               uint64_t lba,
//                               uint64_t lba_count,
//                               int queue_id = 0,
//                               io_callback_t cb = NULL,
//                               void * cb_arg0 = NULL,
//                               void * cb_arg1 = NULL);

//   void IBlock__read(IBlock_ref ref,
//                     io_buffer_t buffer,
//                     uint64_t buffer_offset,
//                     uint64_t lba,
//                     uint64_t lba_count,
//                     int queue_id = 0);

//   workid_t IBlock__async_write(IBlock_ref ref,
//                                io_buffer_t buffer,
//                                uint64_t buffer_offset,
//                                uint64_t lba,
//                                uint64_t lba_count,
//                                int queue_id = 0,
//                                io_callback_t cb = NULL,
//                                void * cb_arg0 = NULL,
//                                void * cb_arg1 = NULL);

//   void IBlock__write(IBlock_ref ref,
//                      io_buffer_t buffer,
//                      uint64_t buffer_offset,
//                      uint64_t lba,
//                      uint64_t lba_count,
//                      int queue_id = 0);

//   bool IBlock__check_completion(IBlock_ref ref, workid_t gwid, int queue_id = 0);

//   status_t IBlock__get_volume_info(IBlock_ref ref, VOLUME_INFO* devinfo);

// #ifdef __cplusplus
// }
// #endif

