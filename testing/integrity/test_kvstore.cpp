/* note: we do not include component source, only the API definition */
#include <api/components.h>
#include <api/kvstore_itf.h>
#include <boost/filesystem.hpp>
#include <common/utils.h>
#include <common/str_utils.h>
#include <iostream>
#include <gtest/gtest.h>
#include "component_info.h"

using namespace Component;

ComponentInfo component_info;

class PoolTest: public::testing::Test
{
public:
    size_t _pool_size = MB(100);    

protected:
    Component::IKVStore * _g_store;
    IKVStore_factory * _fact;
    Component::IKVStore::pool_t _pool;
    std::string _pool_name = "test.pool.0";
    std::string pool_path = component_info.pool_path;
    int* _direct_memory_location = nullptr;
        
    void SetUp() override 
    {
        create_pool();

        if (component_info.uses_direct_memory)
        {
            _direct_memory_location = component_info.setup_direct_memory_for_size(_pool_size);
        }
    }

    void TearDown() override
    {
        if (component_info.uses_direct_memory && component_info.memory_handle != nullptr)
        {
            _g_store->unregister_direct_memory(component_info.memory_handle);
        }

        if (_direct_memory_location != nullptr)
        {
            free(_direct_memory_location);
        }

        destroy_pool();
    }

    void create_pool()
    {
        // make sure pool_path directory exists
        boost::filesystem::path dir(pool_path);
        if (boost::filesystem::create_directory(dir))
        {
            std::cout << "Created directory for testing: " << pool_path << std::endl;
        }

        component_info.load_component();
        _g_store = component_info.store;
        _fact = component_info.factory;

        _pool = _g_store->create_pool(pool_path, _pool_name, _pool_size);

        ASSERT_TRUE(_pool > 0) << "failed create_pool";  // just fail here if pool creation didn't work
    }

    virtual void destroy_pool()
    {
        _fact->release_ref();

        if (_pool <= 0)
        {
           _pool = _g_store->open_pool(pool_path, _pool_name);
        }
        
        _g_store->delete_pool(_pool);
    }
};

// sometimes the default pool size is insufficient, but everything else should stay the same for creation/deletion
class PoolTestLarge: public PoolTest
{
public:
    PoolTestLarge()
    {
        _pool_size = GB(4);
    }
};

// derived PoolTest class with very small size so we can test values that are too large easily
class PoolTestSmall: public PoolTest
{
public:
    PoolTestSmall()
    {
        _pool_size = 1;
    }
};

TEST_F(PoolTest, OpenPool)
{
    const Component::IKVStore::pool_t pool = _g_store->open_pool(pool_path, _pool_name);
    
    ASSERT_TRUE(pool > 0);
}

int PutGetRandomKVPWithSizes(Component::IKVStore * store, IKVStore::pool_t pool, const int key_length, const int value_length)
{
    if (pool <= 0)
    {
        return 1;
    }

    const std::string key = Common::random_string(key_length);
    const std::string value = Common::random_string(value_length);

    // put
    status_t rc = store->put(pool, key, value.c_str(), value_length);

    if (rc != S_OK)
    {
        return (int)rc;
    }

    void *pval = NULL;
    size_t pval_len;

    // get
    rc = store->get(pool, key, pval, pval_len);

    if (rc != S_OK) 
    {
        if (pval != NULL)
        {
            free(pval);
        }

        return 1;
    }

    if (strcmp((const char*)pval, value.c_str()) != 0)
    {
        rc = 1;
    }

    if (pval != NULL)
    {
        free(pval);
    }

    return rc;
}

TEST_F(PoolTest, PutGet_RandomKVP)
{
    // randomly generate key and value
    const int key_length = 8;
    const int value_length = 64;
    const std::string key = Common::random_string(key_length);

    std::string value = Common::random_string(value_length);
    status_t rc = _g_store->put(_pool, key, value.c_str(), value_length);

    ASSERT_EQ(rc, S_OK); 

    void * pval = nullptr;
    size_t pval_len = value_length;
    
    rc  = _g_store->get(_pool, key, pval, pval_len);
    
    std::string get_result((const char*)pval, pval_len);  // force limit on return length

    ASSERT_EQ(rc, S_OK);
    ASSERT_STREQ(get_result.c_str(), value.c_str());
    ASSERT_EQ(pval_len, value_length);
   
    if (pval != nullptr)
    {
        free( pval);
    }
}

TEST_F(PoolTestLarge, PutGet_VaryingSizedKVPs)
{
    int key_lengths[] = {8, 64};  // 7/26/2018: at 256 key length, boost::filesystem throws 'filename too long'
    int value_lengths[] = {8, 64, 128, MB(1), MB(2), MB(4), MB(32), MB(128), GB(1)};

    int num_keys = sizeof(key_lengths) / sizeof(key_lengths[0]);
    int num_values = sizeof(value_lengths) / sizeof(value_lengths[0]);

    int rc;

    // validate size of all info to put into pool
    IKVStore::pool_t total_size = 0;
    for (int i = 0; i < num_keys; i++)
    {
        for (int j = 0; j < num_values; j++)
        {
            total_size += key_lengths[i] + value_lengths[j];
        }
    }

    ASSERT_TRUE(_pool_size >= total_size) << "attempting to put too much data in pool. Have " << _pool_size << ", but want " << total_size << ". Increase the size of PoolTestLarge class.";

    // put/get all combinations of key/value pairs
    for (int i = 0; i < num_keys; i++ )
    {
        for (int j = 0; j < num_values; j++ )
        {
            printf("key: %d value: %d\n", key_lengths[i], value_lengths[j]);
            rc = PutGetRandomKVPWithSizes(_g_store, _pool, key_lengths[i], value_lengths[j]);
        }

        ASSERT_EQ(rc, S_OK);
    }
}

TEST_F(PoolTest, Get_NoValidKey)
{
    // randomly generate key to look up (we shouldn't find it)
    const int key_length = 8;
    const std::string key = Common::random_string(key_length);

    void * pval = NULL;  // initialize so we can check if value has been changed
    size_t pval_len;
    
    status_t rc  = _g_store->get(_pool, key, pval, pval_len);
    ASSERT_EQ((int)rc, (int)IKVStore::E_KEY_NOT_FOUND);  // expect failure code here

    if(pval != NULL)
    {
        free(pval);
    }
}

TEST_F(PoolTest, Put_DuplicateKey)
{
    // randomly generate key and value
    const int key_length = 8;
    const int value_length = 64;

    const std::string key = Common::random_string(key_length);
    std::string value = Common::random_string(value_length);

    status_t rc = _g_store->put(_pool, key, value.c_str(), value_length);

    ASSERT_EQ(rc, S_OK); 

    // now try to add another value to that key
    rc = _g_store->put(_pool, key, value.c_str(), value_length);

    ASSERT_EQ(rc, IKVStore::E_KEY_EXISTS);
}


TEST_F(PoolTest, Put_Erase)
{
    // randomly generate key and value
    const int key_length = 8;
    const int value_length = 64;
    const std::string key = Common::random_string(key_length);
    const std::string value = Common::random_string(value_length);

    status_t rc = _g_store->put(_pool, key, value.c_str(), value_length);

    ASSERT_EQ(rc, S_OK) << "put return code failed"; 

    // now delete key and try to get it again post-deletion
    void * pval = NULL;  // initialize so we can check if value has changed
    size_t pval_len;

    rc = _g_store->erase(_pool, key);

    ASSERT_EQ(rc, S_OK) << "erase return code failed";

    rc  = _g_store->get(_pool, key, pval, pval_len);

    ASSERT_EQ(rc, IKVStore::E_KEY_NOT_FOUND) << "get return code failed";

    if (pval != NULL)
    {
        free(pval);
    }
}

TEST_F(PoolTest, Put_EraseInvalid)
{
    // randomly generate key and value
    const int key_length = 8;

    const std::string key = Common::random_string(key_length);
    void * pval = NULL;
    size_t pval_len;

    // make sure key isn't somehow in pool already
    int rc  = _g_store->get(_pool, key, pval, pval_len);
    ASSERT_EQ(rc, IKVStore::E_KEY_NOT_FOUND) << "get return code failed";

    rc = _g_store->erase(_pool, key);

    if (pval != NULL)
    {
        free(pval);
    }

    ASSERT_EQ(rc, IKVStore::E_KEY_NOT_FOUND) << "erase return code failed";
}


TEST_F(PoolTest, DISABLED_Count_Changes)
{
    const int key_length = 8;
    const int value_length = 64;

    size_t size;
    
    // check size on creation (0)
    size = _g_store->count(_pool);
    ASSERT_EQ(size, 0);
    
    // put random key and value, check size again (1)
    const std::string key_1 = Common::random_string(key_length);
    std::string value = Common::random_string(value_length);

    status_t rc = _g_store->put(_pool, key_1, value.c_str(), value_length);  
    ASSERT_EQ(rc, S_OK);
    ASSERT_EQ(_g_store->count(_pool), 1);

    // put another random key, check size again (2) 
    const std::string key_2 = Common::random_string(key_length);
    value = Common::random_string(value_length);

    rc = _g_store->put(_pool, key_2, value.c_str(), value_length);  

    ASSERT_EQ(_g_store->count(_pool), 2);

    // check size on delete (1)
    rc = _g_store->erase(_pool, key_1);
    ASSERT_EQ(rc, S_OK);
    ASSERT_EQ(_g_store->count(_pool), 1);

    // check size on delete again (0)
    rc = _g_store->erase(_pool, key_2);
    ASSERT_EQ(rc, S_OK);
    ASSERT_EQ(_g_store->count(_pool), 0);
}

TEST_F(PoolTest, PutDirectGetDirect_RandomKVP)
{
    // randomly generate key and value
    const size_t key_length = 8;
    const size_t value_length = 64;
    const std::string key = Common::random_string(key_length);
    Component::IKVStore::memory_handle_t memory_handle;

    std::string value = Common::random_string(value_length);

    if (component_info.uses_direct_memory)
    {
        // copy existing into memory
        memory_handle = component_info.memory_handle;
        memcpy(_direct_memory_location, key.c_str(), key_length);
        memcpy(_direct_memory_location + 1, value.c_str(), value_length);
    }
    else 
    {
        memory_handle = Component::IKVStore::HANDLE_NONE;  
    }

    status_t rc = _g_store->put_direct(_pool, key.c_str(), key_length, value.c_str(), value_length, memory_handle);

    ASSERT_EQ(rc, S_OK) << "put_direct return code failed";

    void * pval = malloc(sizeof(char) * value_length);  // get_direct requires memory allocation
    size_t pval_len = value_length;
 
    rc  = _g_store->get_direct(_pool, key, pval, pval_len, 0, memory_handle);  // offset = 0

    ASSERT_EQ(rc, S_OK) << "get_direct return code failed";
    ASSERT_STREQ((const char*)pval, value.c_str()) << "strings didn't match";

    if (pval != nullptr)
    {
        free(pval);
    }
}

TEST_F(PoolTest, GetDirect_NoValidKey)
{
    // randomly generate key to look up (we shouldn't find it)
    const int key_length = 8;
    const std::string key = Common::random_string(key_length);

    void * pval = NULL;  // initialize so we can check if value has been changed
    size_t pval_len;
 
    status_t rc  = _g_store->get_direct(_pool, key, pval, pval_len, 0);  // offset = 0
    
    if(pval != NULL)
    {
        free(pval);
    } 

    ASSERT_EQ((int)rc, (int)IKVStore::E_KEY_NOT_FOUND) << "get_direct return code failed";
}


TEST_F(PoolTest, PutDirect_DuplicateKey)
{
    // randomly generate key and value
    const int key_length = 8;
    const int value_length = 64;

    const std::string key = Common::random_string(key_length);
    std::string value = Common::random_string(value_length);
    Component::IKVStore::memory_handle_t memory_handle;

    if (component_info.uses_direct_memory)
    {
        // copy existing into memory
        memory_handle = component_info.memory_handle;
        memcpy(_direct_memory_location, key.c_str(), key_length);
        memcpy(_direct_memory_location + 1, value.c_str(), value_length);
    }
    else 
    {
        memory_handle = Component::IKVStore::HANDLE_NONE;  
    }

    status_t rc = _g_store->put_direct(_pool, key.c_str(), key_length, value.c_str(), value_length, memory_handle);

    ASSERT_EQ(rc, S_OK) << "put_direct return code failed";

    // now try to add another value to that key
    rc = _g_store->put_direct(_pool, key.c_str(), key_length, value.c_str(), value_length, memory_handle);

    ASSERT_EQ(rc, IKVStore::E_KEY_EXISTS) << "second put_direct return code failed";
}

TEST_F(PoolTestSmall, Put_TooLarge)
{
    // randomly generate key and value
    const int key_length = 8;
    const int value_length = 64;
    const std::string key = Common::random_string(key_length);

    std::string value = Common::random_string(value_length);
    status_t rc = _g_store->put(_pool, key, value.c_str(), value_length);

    ASSERT_EQ(rc, S_OK); 

    void * pval;
    size_t pval_len;
    
    rc  = _g_store->get(_pool, key, pval, pval_len);

    ASSERT_EQ(rc, S_OK) << "put return code failed";
    ASSERT_STREQ((const char*)pval, value.c_str());
    ASSERT_EQ(value_length, pval_len);

    if (pval != nullptr)
    {
        free(pval);
    }
}

struct {
  std::string test;
  std::string component;
  unsigned cores;
  unsigned time_secs;
} Options;

int main(int argc, char **argv) 
{
    component_info.initialize_component(argc, argv);

    testing::InitGoogleTest(&argc, argv);

    return RUN_ALL_TESTS();
}
