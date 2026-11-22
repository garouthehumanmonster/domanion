#pragma once

#include "core/types.hpp"
#include "render/entity_types.hpp"
#include <memory>

namespace ron::render {

class EntityFeed {
public:
    EntityFeed();
    ~EntityFeed() = default;

    EntityFeed(const EntityFeed&) = delete;
    EntityFeed& operator=(const EntityFeed&) = delete;
    EntityFeed(EntityFeed&&) noexcept = default;
    EntityFeed& operator=(EntityFeed&&) noexcept = default;

    void clear() noexcept;
    bool add(const EntityInstance& inst) noexcept;
    int count() const noexcept;
    u32 dropped() const noexcept;
    void reset_dropped() noexcept;
    const EntityInstance* data() const noexcept;
    EntityInstance* data() noexcept;

private:
    std::unique_ptr<EntityInstance[]> storage_;
    int count_{0};
    u32 dropped_{0};
};

} // namespace ron::render
