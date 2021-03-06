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

#include <gtest/gtest.h>
#include <api/components.h>
#include <api/sample_itf.h>

using namespace Component;

namespace {

// The fixture for testing class Foo.
class Sample_test : public ::testing::Test {

 protected:

  // If the constructor and destructor are not enough for setting up
  // and cleaning up each test, you can define the following methods:
  
  virtual void SetUp() {
    // Code here will be called immediately after the constructor (right
    // before each test).
  }

  virtual void TearDown() {
    // Code here will be called immediately after each test (right
    // before the destructor).
  }
  
  // Objects declared here can be used by all tests in the test case
  static Component::ISample * _sample;
};

Component::ISample * Sample_test::_sample;



TEST_F(Sample_test, Instantiate)
{
  /* create object instance through factory */
  Component::IBase * comp = Component::load_component("../libpysample-component.so",
                                                      Component::sample_component_factory);

  ASSERT_TRUE(comp);
  ISample_factory * fact = (ISample_factory *) comp->query_interface(ISample_factory::iid());

  _sample = fact->open("Billybob");
  
  fact->release_ref();
}

TEST_F(Sample_test, Invoke)
{
  ASSERT_TRUE(_sample);

  /* invoke instance */
  _sample->say_hello();
}

TEST_F(Sample_test, Release)
{
  ASSERT_TRUE(_sample);

  /* release instance */
  _sample->release_ref();
}


} // namespace

int main(int argc, char **argv) {
  
  ::testing::InitGoogleTest(&argc, argv);
  auto r = RUN_ALL_TESTS();

  return r;
}
