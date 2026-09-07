#pragma once

#include <functional>
#include <mutex>
#include <typeindex>
#include <unordered_map>
#include <vector>

namespace utility {

class EventBus final {
public:
    template <typename Event>
    using Handler = std::function<void(const Event&)>;

    template <typename Event>
    void subscribe(Handler<Event> handler) {
        std::scoped_lock lock(mutex_);
        handlers_[std::type_index(typeid(Event))].emplace_back(
            [handler = std::move(handler)](const void* event) {
                handler(*static_cast<const Event*>(event));
            });
    }

    template <typename Event>
    void publish(const Event& event) const {
        std::vector<ErasedHandler> snapshot;
        {
            std::scoped_lock lock(mutex_);
            const auto found = handlers_.find(std::type_index(typeid(Event)));
            if (found == handlers_.end()) {
                return;
            }
            snapshot = found->second;
        }

        for (const auto& handler : snapshot) {
            handler(&event);
        }
    }

    void clear() {
        std::scoped_lock lock(mutex_);
        handlers_.clear();
    }

private:
    using ErasedHandler = std::function<void(const void*)>;

    mutable std::mutex mutex_;
    std::unordered_map<std::type_index, std::vector<ErasedHandler>> handlers_;
};

} // namespace utility

