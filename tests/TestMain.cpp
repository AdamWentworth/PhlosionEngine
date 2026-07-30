#include <iostream>
#include <string>

bool test_phlosion_resource_container_contract(std::string& outFail);
bool test_phlosion_scene_archive_contract(std::string& outFail);
bool test_project_descriptor_contract(std::string& outFail);

int main() {
    struct TestCase {
        const char* name;
        bool (*run)(std::string&);
    };

    const TestCase tests[] = {
        {"phlosion_resource_container_contract", &test_phlosion_resource_container_contract},
        {"phlosion_scene_archive_contract", &test_phlosion_scene_archive_contract},
        {"project_descriptor_contract", &test_project_descriptor_contract},
    };

    bool passed = true;
    for (const TestCase& test : tests) {
        std::string failure;
        if (test.run(failure)) {
            std::cout << "[PASS] " << test.name << '\n';
            continue;
        }
        passed = false;
        std::cerr << "[FAIL] " << test.name << ": " << failure << '\n';
    }
    return passed ? 0 : 1;
}
