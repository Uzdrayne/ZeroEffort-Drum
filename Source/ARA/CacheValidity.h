#pragma once

#include <atomic>
#include <memory>
#include <unordered_map>

namespace drum
{
class CacheValidity
{
public:
    using Token = std::shared_ptr<std::atomic<bool>>;

    Token observe(const void* object)
    {
        auto& token = objects[object];
        if (!token || !token->load(std::memory_order_relaxed))
            token = std::make_shared<std::atomic<bool>>(true);
        return token;
    }

    void invalidate(const void* object)
    {
        const auto found = objects.find(object);
        if (found != objects.end())
        {
            found->second->store(false, std::memory_order_relaxed);
            objects.erase(found);
        }
    }

private:
    std::unordered_map<const void*, Token> objects;
};
}