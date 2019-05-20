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
#include <city.h>
#include <common/cycles.h>
#include <common/utils.h>
#include <unistd.h>

#include "connection.h"
#include "protocol.h"

using namespace Component;

namespace Dawn
{
namespace Client
{
Connection_handler::Connection_handler(Connection_base::Transport* connection)
  : Connection_base(connection)
{
  char* env = getenv("SHORT_CIRCUIT_BACKEND");
  if (env && env[0] == '1') {
    _options.short_circuit_backend = true;
  }
  _max_inject_size = connection->max_inject_size();
}

Connection_handler::~Connection_handler()
{
  PLOG("Connection_handler::dtor (%p)", this);
}

Connection_handler::pool_t Connection_handler::open_pool(const std::string name,
                                                         uint32_t flags)
{
  API_LOCK();

  PMAJOR("open pool: %s", name.c_str());

  /* send pool request message */
  const auto iob = allocate();
  assert(iob);

  status_t status;
  Component::IKVStore::pool_t pool_id;
  
  try {
    const auto msg = new (iob->base()) Dawn::Protocol::Message_pool_request(iob->length(),
                                                                            auth_id(), /* auth id */
                                                                            ++_request_id, 0,         /* size */
                                                                            Dawn::Protocol::OP_OPEN,
                                                                            name);

    iob->set_length(msg->msg_len);

    sync_inject_send(iob);

    sync_recv(iob); /* await response */

    const auto response_msg =
      new (iob->base()) Dawn::Protocol::Message_pool_response();
    
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_POOL_RESPONSE)
      throw Protocol_exception("expected POOL_RESPONSE message - got %x",
                               response_msg->type_id);
    status = S_OK;
    pool_id = response_msg->pool_id;
  }
  catch(...) {
    status = E_FAIL;
    pool_id = Component::IKVStore::POOL_ERROR;
  }
    
  free_buffer(iob);
  return pool_id;
}

Connection_handler::pool_t Connection_handler::create_pool(const std::string name,
                                                           const size_t      size,
                                                           unsigned int      flags,
                                                           uint64_t          expected_obj_count)
{
  API_LOCK();
  PMAJOR("create pool: %s (expected objs=%lu)", name.c_str(), expected_obj_count);

  /* send pool request message */
  const auto iob = allocate();
  assert(iob);

  PLOG("Connection_handler::create_pool");

  status_t status;
  Component::IKVStore::pool_t pool_id;

  try {
    const auto msg = new (iob->base())
      Dawn::Protocol::Message_pool_request(iob->length(),
                                           auth_id(), /* auth id */
                                           ++_request_id,
                                           size,
                                           Dawn::Protocol::OP_CREATE,
                                           name);
    assert(msg->op);
    msg->flags = flags;
    msg->expected_object_count = expected_obj_count;
    
    iob->set_length(msg->msg_len);
    sync_inject_send(iob);

    sync_recv(iob);
    
    auto response_msg = new (iob->base()) Dawn::Protocol::Message_pool_response();
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_POOL_RESPONSE)
      throw Protocol_exception("expected POOL_RESPONSE message - got %x",
                             response_msg->type_id);
    
    status_t rc = response_msg->status;
    
    pool_id = response_msg->pool_id;
    status = msg->status;
  }
  catch(...) {
    status = E_FAIL;
    pool_id = Component::IKVStore::POOL_ERROR;    
  }

  free_buffer(iob);
  return pool_id;
}

status_t Connection_handler::close_pool(pool_t pool)
{
  API_LOCK();
  /* send pool request message */
  auto       iob = allocate();
  const auto msg = new (iob->base()) Dawn::Protocol::Message_pool_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          Dawn::Protocol::OP_CLOSE);
  msg->pool_id = pool;

  iob->set_length(msg->msg_len);
  sync_inject_send(iob);

  sync_recv(iob);

  auto response_msg = new (iob->base()) Dawn::Protocol::Message_pool_response();
  if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_POOL_RESPONSE)
    throw Protocol_exception("expected POOL_RESPONSE message - got %x",
                             response_msg->type_id);
  auto status = response_msg->status;
  free_buffer(iob);
  return status;
}

status_t Connection_handler::delete_pool(const std::string& name)

{
  API_LOCK();
  /* send pool request message */
  auto       iob = allocate();
  const auto msg = new (iob->base()) Dawn::Protocol::Message_pool_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          0, // size
                                                                          Dawn::Protocol::OP_DELETE,
                                                                          name);
  iob->set_length(msg->msg_len);
  sync_inject_send(iob);

  sync_recv(iob);

  auto response_msg = new (iob->base()) Dawn::Protocol::Message_pool_response();
  if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_POOL_RESPONSE)
    throw Protocol_exception("expected POOL_RESPONSE message - got %x",
                             response_msg->type_id);
  
  auto status = response_msg->status;
  free_buffer(iob);
  return status;
}

status_t Connection_handler::configure_pool(const Component::IKVStore::pool_t pool,
                                            const std::string& json)
{
  API_LOCK();

  const auto iob = allocate();

  status_t status;

  const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                        auth_id(),
                                                                        ++_request_id,
                                                                        pool,
                                                                        Dawn::Protocol::OP_CONFIGURE,  // op
                                                                        json);
  if ((json.length() + sizeof(Dawn::Protocol::Message_IO_request)) >
      Buffer_manager<Component::IFabric_client>::BUFFER_LEN)
    return IKVStore::E_TOO_LARGE;

  iob->set_length(msg->msg_len);

  sync_send(iob);

  sync_recv(iob);

  const auto response_msg =
    new (iob->base()) Dawn::Protocol::Message_IO_response();

  if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
    throw Protocol_exception("expected IO_RESPONSE message - got %x",
                             response_msg->type_id);

  if (option_DEBUG)
    PLOG("got response from CONFIGURE operation: status=%d request_id=%lu",
         response_msg->status, response_msg->request_id);

  status = response_msg->status;
  free_buffer(iob);
  return status;
}

/**
 * Memcpy version; both key and value are copied
 *
 */
status_t Connection_handler::put(const pool_t      pool,
                                 const std::string key,
                                 const void*       value,
                                 const size_t      value_len,
                                 uint32_t      flags)
{
  return put(pool, key.c_str(), key.length(), value, value_len, flags);
}

status_t Connection_handler::put(const pool_t pool,
                                 const void*  key,
                                 const size_t key_len,
                                 const void*  value,
                                 const size_t value_len,
                                 uint32_t flags)
{
  API_LOCK();

  if (option_DEBUG)
    PINF("put: %.*s (key_len=%lu) (value_len=%lu)", (int) key_len, (char*) key,
         key_len, value_len);

  /* check key length */
  if ((key_len + value_len + sizeof(Dawn::Protocol::Message_IO_request)) >
      Buffer_manager<Component::IFabric_client>::BUFFER_LEN) {
    PWRN("Dawn_client::put value length (%lu) too long. Use put_direct.", value_len);
    return IKVStore::E_TOO_LARGE;
  }

  const auto iob = allocate();

  status_t status;

  try {
    const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          pool,
                                                                          Dawn::Protocol::OP_PUT,  // op
                                                                          key,
                                                                          key_len,
                                                                          value,
                                                                          value_len,
                                                                          flags);
    if (_options.short_circuit_backend)
      msg->resvd |= Dawn::Protocol::MSG_RESVD_SCBE;
    
    iob->set_length(msg->msg_len);
    
    sync_send(iob);
    
    sync_recv(iob);
    
    const auto response_msg =
      new (iob->base()) Dawn::Protocol::Message_IO_response();
    
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got %x",
                             response_msg->type_id);

    if (option_DEBUG)
      PLOG("got response from PUT operation: status=%d request_id=%lu",
           response_msg->status, response_msg->request_id);

    status = response_msg->status;
  }
  catch(...) {
    status = E_FAIL;
  }
  
  free_buffer(iob);
  return status;
}

status_t Connection_handler::two_stage_put_direct(const pool_t                         pool,
                                                  const void*                          key,
                                                  const size_t                         key_len,
                                                  const void*                          value,
                                                  const size_t                         value_len,
                                                  Component::IKVStore::memory_handle_t handle,
                                                  uint32_t                             flags)
{
  using namespace Dawn;

  assert(pool);

  assert(value_len <= _max_message_size);
  assert(value_len > 0);
  
  const auto iob = allocate();

  /* send advance leader message */
  const auto request_id = ++_request_id;
  const auto msg        = new (iob->base())
    Protocol::Message_IO_request(iob->length(), 
                                 auth_id(), request_id, pool,
                                 Protocol::OP_PUT_ADVANCE,  // op
                                 key, key_len, value_len, flags);
  msg->flags = flags;
  iob->set_length(msg->msg_len);
  sync_inject_send(iob);

  /* wait for response from header before posting the value */
  {
    sync_recv(iob);
    
    auto response_msg = new (iob->base()) Dawn::Protocol::Message_IO_response();

    if(option_DEBUG)
      PMAJOR("got response (status=%u) from put direct header",response_msg->status);

    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got 0x%x",
                               response_msg->type_id);

    /* if response is not OK, don't follow with the value */
    if(response_msg->status != S_OK) {
      free_buffer(iob);
      return msg->status;
    }
  }
  
  /* send value */
  buffer_t* value_buffer = reinterpret_cast<buffer_t*>(handle);
  value_buffer->set_length(value_len);
  assert(value_buffer->check_magic());

  if (option_DEBUG)
    PLOG("value_buffer: (iov_len=%lu, region=%p, desc=%p)",
         value_buffer->iov->iov_len, value_buffer->region, value_buffer->desc);

  sync_send(value_buffer);  // client owns buffer

  free_buffer(iob);

  if (option_DEBUG) {
    PINF("two_stage_put_direct: complete");
  }

  return S_OK;
}

status_t Connection_handler::put_direct(const pool_t                         pool,
                                        const std::string&                   key,
                                        const void*                          value,
                                        const size_t                         value_len,
                                        Component::IKVStore::memory_handle_t handle,
                                        uint32_t                             flags)
{
  API_LOCK();

  if (handle == IKVStore::HANDLE_NONE) {
    PWRN("put_direct: memory handle should be provided");
    return E_BAD_PARAM;
  }

  assert(_max_message_size);

  if(pool == 0) {
    PWRN("put_direct: invalid pool identifier");
    return E_INVAL;
  }

  buffer_t* value_buffer = reinterpret_cast<buffer_t*>(handle);
  value_buffer->set_length(value_len);
  
  if (!value_buffer->check_magic()) {
    PWRN("put_direct: memory handle is invalid");
    return E_INVAL;
  }
    
  status_t status;

  try {
    
    const auto key_len = key.length();
    if ((key_len + value_len + sizeof(Dawn::Protocol::Message_IO_request)) >
      Buffer_manager<Component::IFabric_client>::BUFFER_LEN) {

      /* check value is not too large for underlying transport */
      if(value_len > _max_message_size) {
        return IKVStore::E_TOO_LARGE;
      }
      
      /* for large puts, where the receiver will not have
       * sufficient buffer space, we use a two-stage protocol */
      return two_stage_put_direct(pool,
                                  key.c_str(),
                                  key_len,
                                  value,
                                  value_len,
                                  handle,
                                  flags);
    }

    const auto iob = allocate();

    if (option_DEBUG ||1) {
      //PLOG("put_direct: key=(%.*s) key_len=%lu value=(%.20s...) value_len=%lu",
      //    (int) key_len, (char*) key.c_str(), key_len, (char*) value, value_len);

      PLOG("value_buffer: (iov_len=%lu, mr=%p, desc=%p)",
           value_buffer->iov->iov_len, value_buffer->region, value_buffer->desc);
    }

    const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          pool,
                                                                          Dawn::Protocol::OP_PUT,  // op
                                                                          key.c_str(),
                                                                          key_len,
                                                                          value_len,
                                                                          flags);

    if (_options.short_circuit_backend)
      msg->resvd |= Dawn::Protocol::MSG_RESVD_SCBE;
  
    msg->flags = flags;

    iob->set_length(msg->msg_len);
    sync_send(iob, value_buffer); /* send two concatentated buffers in single DMA */

    sync_recv(iob); /* get response */

    auto response_msg = new (iob->base()) Dawn::Protocol::Message_IO_response();
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got 0x%x",
                               response_msg->type_id);

    if (option_DEBUG)
      PLOG("got response from PUT_DIRECT operation: status=%d", msg->status);

    status = response_msg->status;

    free_buffer(iob);
  }
  catch(...) {
    status = E_FAIL;
  }
  
  return status;
}

status_t Connection_handler::get(const pool_t       pool,
                                 const std::string& key,
                                 std::string&       value)
{
  API_LOCK();

  const auto iob = allocate();
  assert(iob);

  status_t status;

  try {
    
    const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          pool,
                                                                          Dawn::Protocol::OP_GET,  // op
                                                                          key,
                                                                          "",
                                                                          0);

    if (_options.short_circuit_backend)
      msg->resvd |= Dawn::Protocol::MSG_RESVD_SCBE;

    iob->set_length(msg->msg_len);
    sync_inject_send(iob);

    sync_recv(iob);

    auto response_msg = new (iob->base()) Dawn::Protocol::Message_IO_response();
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got %x",
                               response_msg->type_id);

    if (option_DEBUG)
      PLOG("got response from GET operation: status=%d (%s)", msg->status,
           response_msg->data);

    status = response_msg->status;
    value.reserve(response_msg->data_len + 1);
    value.insert(0, response_msg->data, response_msg->data_len);
    assert(response_msg->data);
  }
  catch(...) {
    status = E_FAIL;
  }

  free_buffer(iob);
  return status;
}

status_t Connection_handler::get(const pool_t       pool,
                                 const std::string& key,
                                 void*&             value,
                                 size_t&            value_len)
{
  API_LOCK();

  const auto iob = allocate();
  assert(iob);

  status_t status;

  try {
    
    const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          pool,
                                                                          Dawn::Protocol::OP_GET,  // op
                                                                          key.c_str(),
                                                                          key.length(),
                                                                          0);

    /* indicate how much space has been allocated on this side. For
       get this is based on buffer size
    */
    msg->val_len = iob->original_length - sizeof(Dawn::Protocol::Message_IO_response);

    if (_options.short_circuit_backend)
      msg->resvd |= Dawn::Protocol::MSG_RESVD_SCBE;

    iob->set_length(msg->msg_len);
    sync_inject_send(iob);

    sync_recv(iob); /* TODO; could we issue the recv and send together? */

    const auto response_msg =
      new (iob->base()) Dawn::Protocol::Message_IO_response();
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got %x",
                               response_msg->type_id);

    if (option_DEBUG)
      PLOG("got response from GET operation: status=%d request_id=%lu data_len=%lu",
           response_msg->status, response_msg->request_id,
           response_msg->data_length());

    if (response_msg->status != S_OK) return response_msg->status;

    if (option_DEBUG) PLOG("message value:(%s)", response_msg->data);

    if (response_msg->is_set_twostage_bit()) {
      /* two-stage get */
      const auto data_len = response_msg->data_length() + 1;
      value               = ::aligned_alloc(MiB(2), data_len);
      madvise(value, data_len, MADV_HUGEPAGE);

      auto region = register_memory(value, data_len); /* we could have some pre-registered? */
      auto desc = get_memory_descriptor(region);

      iovec iov{value, data_len - 1};
      post_recv(&iov, (&iov) + 1, &desc, &iov);

      /* synchronously wait for receive to complete */
      wait_for_completion(&iov);

      deregister_memory(region);
    }
    else {
      /* copy off value from IO buffer */
      value     = ::malloc(response_msg->data_len + 1);
      value_len = response_msg->data_len;

      memcpy(value, response_msg->data, response_msg->data_len);
      ((char*) value)[response_msg->data_len] = '\0';
    }
    
    status = response_msg->status;
  }
  catch(...) {
    status = E_FAIL;
  }

  free_buffer(iob);
  return status;
}

status_t Connection_handler::get_direct(const pool_t                         pool,
                                        const std::string&                   key,
                                        void*                                value,
                                        size_t&                              out_value_len,
                                        Component::IKVStore::memory_handle_t handle)
{
  API_LOCK();

  if (!value || out_value_len == 0)
    return E_BAD_PARAM;

  buffer_t* value_iob = reinterpret_cast<buffer_t*>(handle);
  if (!value_iob->check_magic()) {
    PWRN("bad handle parameter to get_direct");
    return E_BAD_PARAM;
  }

  /* check value is not too large for underlying transport */
  if(out_value_len > _max_message_size)
    return IKVStore::E_TOO_LARGE;

  const auto iob = allocate();
  assert(iob);

  status_t status;
  try {
    const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          pool,
                                                                          Dawn::Protocol::OP_GET,
                                                                          key.c_str(),
                                                                          key.length(),
                                                                          0);

    /* indicate that this is a direct request and register
       how much space has been allocated on this side. For
       get_direct this is allocated by the client */
    msg->resvd = Protocol::MSG_RESVD_DIRECT;
    msg->val_len = out_value_len;
    
    iob->set_length(msg->msg_len);
    sync_inject_send(iob);

    sync_recv(iob); /* get response */

    auto response_msg = new (iob->base()) Dawn::Protocol::Message_IO_response();
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got %x",
                               response_msg->type_id);

    if(option_DEBUG)
      PLOG("get_direct: got initial response (two_stage=%s)",
           response_msg->is_set_twostage_bit() ?  "true" : "false");

    /* insufficent space should have been dealt with already */
    assert(out_value_len >= response_msg->data_length());

    status = response_msg->status;

    /* if response not S_OK, do not do anything else */
    if (status != S_OK) {
      free_buffer(iob);
      return status;
    }
  
    /* set out_value_len to receiving length */
    out_value_len = response_msg->data_length();

    if (response_msg->is_set_twostage_bit()) {
      /* two-stage get */
      post_recv(value_iob);

      /* synchronously wait for receive to complete */
      wait_for_completion(value_iob);
    }
    else {
      memcpy(value, response_msg->data, response_msg->data_len);
    }

    status = response_msg->status;

  }
  catch(...) {
    status = E_FAIL;
  }

  free_buffer(iob);
  return status;
}

status_t Connection_handler::erase(const pool_t pool,
                                   const std::string& key)
{
  API_LOCK();

  const auto iob = allocate();
  assert(iob);

  status_t status;
  
  try {
    const auto msg = new (iob->base()) Dawn::Protocol::Message_IO_request(iob->length(),
                                                                          auth_id(),
                                                                          ++_request_id,
                                                                          pool,
                                                                          Dawn::Protocol::OP_ERASE,
                                                                          key.c_str(),
                                                                          key.length(),
                                                                          0);
    
    iob->set_length(msg->msg_len);
    sync_inject_send(iob);
    
    sync_recv(iob);
    
    auto response_msg = new (iob->base()) Dawn::Protocol::Message_IO_response();
    
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_IO_RESPONSE)
      throw Protocol_exception("expected IO_RESPONSE message - got %x", response_msg->type_id);

    if (option_DEBUG)
      PLOG("got response from ERASE operation: status=%d request_id=%lu data_len=%lu",
           response_msg->status, response_msg->request_id,
           response_msg->data_length());
    status = response_msg->status;
  }
  catch(...) {
    status = E_FAIL;
  }
  
  free_buffer(iob);
  return status;
}

size_t Connection_handler::count(const pool_t pool)
{
  API_LOCK();

  const auto iob = allocate();
  assert(iob);
  const auto msg = new (iob->base()) Dawn::Protocol::Message_INFO_request(auth_id());
  msg->pool_id = pool;
  msg->type = Component::IKVStore::Attribute::COUNT;
  iob->set_length(msg->base_message_size());

  sync_inject_send(iob);

  sync_recv(iob);

  auto response_msg = new (iob->base()) Dawn::Protocol::Message_INFO_response();
  if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_INFO_RESPONSE) {
    PWRN("expected INFO_RESPONSE message - got %x", response_msg->type_id);
    free_buffer(iob);
    return 0;
  }

  auto val = response_msg->value;
  free_buffer(iob);
  return val;
}

status_t Connection_handler::get_attribute(const IKVStore::pool_t pool,
                                           const IKVStore::Attribute attr,
                                           std::vector<uint64_t>& out_attr,
                                           const std::string* key)
{

  API_LOCK();
  
  const auto iob = allocate();
  assert(iob);

  status_t status;

  try {
    const auto msg = new (iob->base()) Dawn::Protocol::Message_INFO_request(auth_id());
    msg->pool_id = pool;

    msg->type = attr;
    msg->set_key(iob->length(), *key);
    iob->set_length(msg->message_size());

    sync_inject_send(iob);

    sync_recv(iob);

    auto response_msg = new (iob->base()) Dawn::Protocol::Message_INFO_response();
    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_INFO_RESPONSE) {
      PWRN("expected INFO_RESPONSE message - got %x", response_msg->type_id);
      free_buffer(iob);
      return 0;
    }

    out_attr.clear();
    out_attr.push_back(response_msg->value);
    status = response_msg->status;
  }
  catch(...) {
    status = E_FAIL;
  }
  
  free_buffer(iob);
  return status;
}

status_t Connection_handler::find(const IKVStore::pool_t pool,
                                  const std::string& key_expression,
                                  const offset_t offset,
                                  offset_t& out_matched_offset,
                                  std::string& out_matched_key)
{
  API_LOCK();

  const auto iob = allocate();
  assert(iob);

  status_t status;

  try {
    const auto msg = new (iob->base()) Dawn::Protocol::Message_INFO_request(auth_id());
    msg->pool_id = pool;
    msg->type = Dawn::Protocol::INFO_TYPE_FIND_KEY;
    msg->offset = offset;
  
    msg->set_key(iob->length(), key_expression);
    iob->set_length(msg->message_size());

    sync_inject_send(iob);

    sync_recv(iob);

    auto response_msg = new (iob->base()) Dawn::Protocol::Message_INFO_response();

    if (response_msg->type_id != Dawn::Protocol::MSG_TYPE_INFO_RESPONSE)
      throw Protocol_exception("expected INFO_RESPONSE message - got %x", response_msg->type_id);

    status = response_msg->status;
    
    if(status == S_OK) {
      out_matched_key = response_msg->c_str();
      out_matched_offset = response_msg->offset;
    }       
  }
  catch(...) {
    status = E_FAIL;
  }

  free_buffer(iob);
  return status;
}



int Connection_handler::tick()
{
  using namespace Dawn::Protocol;

  switch (_state) {
  case INITIALIZE: {
    set_state(HANDSHAKE_SEND);
    break;
  }
  case HANDSHAKE_SEND: {
    PMAJOR("client : HANDSHAKE_SEND");
    auto iob = allocate();
    auto msg = new (iob->base()) Dawn::Protocol::Message_handshake(0, 1);

    iob->set_length(msg->msg_len);
    post_send(iob->iov, iob->iov + 1, &iob->desc, iob);

    wait_for_completion(iob);

    free_buffer(iob);
    set_state(HANDSHAKE_GET_RESPONSE);
    break;
  }
  case HANDSHAKE_GET_RESPONSE: {
    auto iob = allocate();
    post_recv(iob->iov, iob->iov + 1, &iob->desc, iob);

    wait_for_completion(iob);

    Message_handshake_reply* msg = (Message_handshake_reply*) iob->base();
    if (msg->type_id == Dawn::Protocol::MSG_TYPE_HANDSHAKE_REPLY)
      set_state(READY);
    else
      throw Protocol_exception("client: expecting handshake reply got type_id=%u len=%lu",
                               msg->type_id, msg->msg_len);

    PMAJOR("client : HANDSHAKE_GET_RESPONSE");

    _max_message_size = max_message_size(); /* from fabric component */
    free_buffer(iob);
    break;
  }
  case READY: {
    return 0;
    break;
  }
  case SHUTDOWN: {
    auto iob = allocate();
    auto msg = new (iob->base())
      Dawn::Protocol::Message_close_session((uint64_t) this);

    iob->set_length(msg->msg_len);
    post_send(iob->iov, iob->iov + 1, &iob->desc, iob);

    wait_for_completion(iob);

    free_buffer(iob);
    set_state(STOPPED);
    PLOG("Dawn_client: connection %p shutdown.", this);
    return 0;
  }
  case STOPPED: {
    assert(0);
    return 0;
  }
  }  // end switch

  return 1;
}

}  // namespace Client
}  // namespace Dawn
