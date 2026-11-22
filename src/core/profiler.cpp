#include "profiler.hpp"
#include "job_system.hpp"

#include <cstring>
#include <algorithm>

namespace ron {

Result<void> Profiler::init(MemoryArena& permanent_arena, const JobSystem* js) noexcept {
    if (js == nullptr) {
        return Result<void>::err(ErrorCode::InvalidArgument);
    }
    js_ = js;

    streams_ = permanent_arena.alloc<WorkerProfileStream>(MAX_WORKER_THREADS);
    if (streams_ == nullptr) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }

    for (u32 w = 0; w < MAX_WORKER_THREADS; ++w) {
        new (&streams_[w]) WorkerProfileStream();
    }

    stat_count_ = 0;
    for (u32 s = 0; s < MAX_PROFILE_STATS; ++s) {
        stats_[s] = ProfileScopeStat{};
    }
    frame_start_ns_ = clock_now_ns();

    return Result<void>::ok();
}

void Profiler::begin_frame() noexcept {
    if (streams_ == nullptr) return;
    const u32 count = js_ ? js_->worker_count() : 1;
    for (u32 w = 0; w < count; ++w) {
        streams_[w].count = 0;
        streams_[w].current_depth = 0;
    }
    frame_start_ns_ = clock_now_ns();
}

u32 Profiler::push_scope(const char* name, u64 name_hash) noexcept {
    if (streams_ == nullptr || js_ == nullptr) return 0xFFFF'FFFF;
    const u32 wid = js_->current_worker_index();
    if (wid >= MAX_WORKER_THREADS) return 0xFFFF'FFFF;

    WorkerProfileStream& s = streams_[wid];
    if (s.count >= MAX_PROFILE_EVENTS_PER_WORKER) return 0xFFFF'FFFF;

    const u32 slot = s.count++;
    ProfileEvent& ev = s.events[slot];
    ev.name = name;
    ev.name_hash = name_hash;
    ev.start_ns = clock_now_ns();
    ev.end_ns = 0;
    ev.depth = s.current_depth++;
    return slot;
}

void Profiler::pop_scope(u32 slot_handle) noexcept {
    if (streams_ == nullptr || js_ == nullptr || slot_handle == 0xFFFF'FFFF) return;
    const u32 wid = js_->current_worker_index();
    if (wid >= MAX_WORKER_THREADS) return;

    WorkerProfileStream& s = streams_[wid];
    if (slot_handle < s.count) {
        s.events[slot_handle].end_ns = clock_now_ns();
    }
    if (s.current_depth > 0) {
        --s.current_depth;
    }
}

void Profiler::end_frame() noexcept {
    if (streams_ == nullptr || js_ == nullptr) return;
    const u32 count = js_->worker_count();

    for (u32 s = 0; s < stat_count_; ++s) {
        stats_[s].last_call_count = 0;
        stats_[s].last_total_ns = 0;
    }

    for (u32 w = 0; w < count; ++w) {
        WorkerProfileStream& stream = streams_[w];
        const u32 ev_count = stream.count;

        for (u32 i = 0; i < ev_count; ++i) {
            const ProfileEvent& ev = stream.events[i];
            if (ev.end_ns >= ev.start_ns && ev.start_ns > 0) {
                const u64 duration = ev.end_ns - ev.start_ns;

                ProfileScopeStat* target = nullptr;
                for (u32 s = 0; s < stat_count_; ++s) {
                    if (stats_[s].name_hash == ev.name_hash) {
                        target = &stats_[s];
                        break;
                    }
                }

                if (target == nullptr && stat_count_ < MAX_PROFILE_STATS) {
                    target = &stats_[stat_count_++];
                    target->name = ev.name;
                    target->name_hash = ev.name_hash;
                    target->min_ns = 0xFFFF'FFFF'FFFF'FFFFULL;
                    target->max_ns = 0;
                    target->rolling_avg_ns = 0;
                    target->sample_count = 0;
                    target->last_call_count = 0;
                    target->last_total_ns = 0;
                }

                if (target != nullptr) {
                    target->last_call_count++;
                    target->last_total_ns += duration;
                    target->history_ns[target->sample_count % PROFILE_HISTORY_LEN] = duration;
                    target->sample_count++;

                    if (duration < target->min_ns) target->min_ns = duration;
                    if (duration > target->max_ns) target->max_ns = duration;

                    const u32 win = std::min(target->sample_count, static_cast<u32>(PROFILE_HISTORY_LEN));
                    u64 sum = 0;
                    for (u32 k = 0; k < win; ++k) sum += target->history_ns[k];
                    target->rolling_avg_ns = sum / win;
                }
            }
        }

        stream.count = 0;
        stream.current_depth = 0;
    }
}

const ProfileScopeStat* Profiler::find_stat(std::string_view name) const noexcept {
    const u64 h = fnv1a_64(name);
    for (u32 i = 0; i < stat_count_; ++i) {
        if (stats_[i].name_hash == h) {
            return &stats_[i];
        }
    }
    return nullptr;
}

} // namespace ron
