#pragma once
#include <chrono>
#include <functional>
#include <future>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
namespace engine::render {
class PreparationCancelled : public std::runtime_error { public: PreparationCancelled():std::runtime_error("Renderer preparation cancelled"){} };
inline thread_local std::function<void(std::string_view)> preparationProgress;
class PreparationProgressScope {
public:
    explicit PreparationProgressScope(std::function<void(std::string_view)> callback)
        : previous_(std::move(preparationProgress)) { preparationProgress=std::move(callback); }
    ~PreparationProgressScope(){preparationProgress=std::move(previous_);}
    PreparationProgressScope(const PreparationProgressScope&)=delete;
    PreparationProgressScope& operator=(const PreparationProgressScope&)=delete;
private:
    std::function<void(std::string_view)> previous_;
};
template<class Work>
auto prepareWithProgress(std::string_view label,Work&& work) {
    if(!preparationProgress)return work();
    preparationProgress(label);
    // Driver/compiler work does not own the window; its owner pumps events while joining.
    auto result=std::async(std::launch::async,std::forward<Work>(work));
    while(result.wait_for(std::chrono::milliseconds(25))!=std::future_status::ready)
        preparationProgress(label);
    auto value=result.get();preparationProgress({});return value;
}
}
