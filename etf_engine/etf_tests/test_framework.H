#pragma once

#include <iostream>
#include <string>
#include <vector>
#include <functional>
#include <stdexcept>

namespace test {

struct TestResult {
    std::string test_name;
    bool passed;
    std::string error_message;
};

class TestSuite {
public:
    TestSuite(const std::string& name) : name(name) {}

    void add_test(const std::string& test_name, std::function<void()> test_func) {
        tests.push_back({test_name, test_func});
    }

    std::vector<TestResult> run_all() {
        std::vector<TestResult> results;
        std::cout << "Running test suite: " << name << std::endl;
        
        for (const auto& test : tests) {
            std::cout << "  Running test: " << test.name << " ... ";
            TestResult result;
            result.test_name = test.name;
            try {
                test.func();
                result.passed = true;
                std::cout << "PASSED" << std::endl;
            } catch (const std::exception& e) {
                result.passed = false;
                result.error_message = e.what();
                std::cout << "FAILED: " << e.what() << std::endl;
            } 
            results.push_back(result);
        }
        return results;
    }
private:
    std::string name;
    struct Test {
        std::string name;
        std::function<void()> func;
    };
    std::vector<Test> tests;
};

void assert_true(bool condition, const std::string& message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void assert_false(bool condition, const std::string& message) {
    if (condition) {
        throw std::runtime_error(message);
    }
}

void assert_equal(int expected, int actual, const std::string& message) {
    if (expected != actual) {
        throw std::runtime_error(message + " (expected " + std::to_string(expected) + 
                                ", got " + std::to_string(actual) + ")");
    }
}

} // namespace test
