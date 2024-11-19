#include "../matching_engine.H"

#include <gtest/gtest.h>

namespace ndfex {

// The fixture for testing class Foo.
class MatchingEngineTest : public testing::Test {
 protected:

  // If the constructor and destructor are not enough for setting up
  // and cleaning up each test, you can define the following methods:

  void SetUp() override {
     // Code here will be called immediately after the constructor (right
     // before each test).
  }

  void TearDown() override {
     // Code here will be called immediately after each test (right
     // before the destructor).
  }

  // Class members declared here can be used by all tests in the test suite
  // for Foo.
};

// Tests that the Foo::Bar() method does Abc.
TEST_F(MatchingEngineTest, MethodBarDoesAbc) {

    EXPECT_EQ(1, 1);

}

// Tests that Foo does Xyz.
TEST_F(MatchingEngineTest, DoesXyz) {
  // Exercises the Xyz feature of Foo.
}

}  // namespace

int main(int argc, char **argv) {
  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}