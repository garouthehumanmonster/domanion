#include "render/entity_feed.hpp"

namespace ron::render {

EntityFeed::EntityFeed()
    : storage_(std::make_unique<EntityInstance[]>(MAX_INSTANCES)),
      count_(0),
      dropped_(0) {
}

void EntityFeed::clear() noexcept {
    count_ = 0;
}

bool EntityFeed::add(const EntityInstance& inst) noexcept {
    if (count_ < static_cast<int>(MAX_INSTANCES)) {
        storage_[count_++] = inst;
        return true;
    }
    ++dropped_;
    return false;
}

int EntityFeed::count() const noexcept {
    return count_;
}

u32 EntityFeed::dropped() const noexcept {
    return dropped_;
}

void EntityFeed::reset_dropped() noexcept {
    dropped_ = 0;
}

const EntityInstance* EntityFeed::data() const noexcept {
    return storage_.get();
}

EntityInstance* EntityFeed::data() noexcept {
    return storage_.get();
}

} // namespace ron::render
