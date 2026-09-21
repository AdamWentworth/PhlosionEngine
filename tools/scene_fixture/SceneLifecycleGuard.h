#pragma once
#include <nlohmann/json.hpp>
#include <string>
namespace engine::scene_fixture::verification {
bool validateSceneLifecycle(const nlohmann::json& evidence, bool unloaded, std::string& error);
}
