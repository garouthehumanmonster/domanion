#include "command_bus.hpp"
#include <algorithm>
#include <cstring>

namespace ron {

Result<void> CommandBus::init(MemoryArena& match_arena) noexcept {
    pending_ = match_arena.alloc<SimCommand>(MAX_PENDING_COMMANDS);
    history_ = match_arena.alloc<SimCommand>(MAX_REPLAY_COMMANDS);
    if (pending_ == nullptr || history_ == nullptr) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }
    pending_count_ = 0;
    history_count_ = 0;
    next_sequence_id_ = 0;
    return Result<void>::ok();
}

Result<void> CommandBus::enqueue(const SimCommand& cmd) noexcept {
    if (pending_count_ >= MAX_PENDING_COMMANDS) {
        return Result<void>::err(ErrorCode::OutOfMemory);
    }
    SimCommand c = cmd;
    if (c.sequence_id == 0) {
        c.sequence_id = ++next_sequence_id_;
    }
    c.reserved_pad = 0;
    pending_[pending_count_++] = c;
    return Result<void>::ok();
}

std::span<const SimCommand> CommandBus::extract_sorted_for_tick(u32 tick, MemoryArena& scratch) noexcept {
    u32 match_count = 0;
    for (u32 i = 0; i < pending_count_; ++i) {
        if (pending_[i].execute_tick <= tick) {
            match_count++;
        }
    }

    if (match_count == 0) {
        return {};
    }

    SimCommand* extracted = scratch.alloc<SimCommand>(match_count);
    if (extracted == nullptr) {
        return {};
    }

    u32 extract_idx = 0;
    u32 remaining_idx = 0;
    for (u32 i = 0; i < pending_count_; ++i) {
        SimCommand cmd = pending_[i];
        if (cmd.execute_tick <= tick) {
            if (cmd.execute_tick < tick) {
                cmd.execute_tick = tick;
            }
            extracted[extract_idx++] = cmd;
        } else {
            pending_[remaining_idx++] = cmd;
        }
    }
    pending_count_ = remaining_idx;

    // Insertion sort: (execute_tick, player_id, sequence_id)
    for (u32 i = 1; i < match_count; ++i) {
        SimCommand key = extracted[i];
        i32 j = static_cast<i32>(i) - 1;
        while (j >= 0) {
            const SimCommand& cur = extracted[j];
            bool greater = false;
            if (cur.execute_tick != key.execute_tick) {
                greater = cur.execute_tick > key.execute_tick;
            } else if (cur.player_id != key.player_id) {
                greater = cur.player_id > key.player_id;
            } else {
                greater = cur.sequence_id > key.sequence_id;
            }

            if (!greater) break;
            extracted[j + 1] = extracted[j];
            --j;
        }
        extracted[j + 1] = key;
    }

    // Append to replay history log
    for (u32 i = 0; i < match_count; ++i) {
        if (history_count_ < MAX_REPLAY_COMMANDS) {
            history_[history_count_++] = extracted[i];
        }
    }

    return {extracted, match_count};
}

Result<std::span<u8>> CommandBus::serialize_replay_lz4(
    u64 match_seed,
    u32 total_ticks,
    u64 final_state_hash,
    MemoryArena& dst_arena,
    MemoryArena& scratch) const noexcept {

    const usize payload_bytes = sizeof(ReplayHeader) + sizeof(SimCommand) * history_count_;
    u8* raw_buf = scratch.alloc<u8>(payload_bytes);
    if (raw_buf == nullptr) {
        return Result<std::span<u8>>::err(ErrorCode::OutOfMemory);
    }

    ReplayHeader header{
        .magic = 0x36325052u, // 'RP26'
        .version = 1u,
        .match_seed = match_seed,
        .total_ticks = total_ticks,
        .total_commands = history_count_,
        .final_state_xxh64 = final_state_hash
    };

    std::memcpy(raw_buf, &header, sizeof(ReplayHeader));
    if (history_count_ > 0) {
        std::memcpy(raw_buf + sizeof(ReplayHeader), history_, sizeof(SimCommand) * history_count_);
    }

    return lz4_compress_to_arena(std::span<const u8>{raw_buf, payload_bytes}, dst_arena);
}

Result<ReplayHeader> CommandBus::load_replay_lz4(
    std::span<const u8> compressed_blob,
    MemoryArena& scratch) noexcept {

    auto decomp_res = lz4_decompress_to_arena(compressed_blob, scratch);
    if (!decomp_res.is_ok()) {
        return Result<ReplayHeader>::err(decomp_res.error);
    }

    std::span<u8> raw = decomp_res.value;
    if (raw.size() < sizeof(ReplayHeader)) {
        return Result<ReplayHeader>::err(ErrorCode::CorruptData);
    }

    ReplayHeader header{};
    std::memcpy(&header, raw.data(), sizeof(ReplayHeader));

    if (header.magic != 0x36325052u || header.version != 1u) {
        return Result<ReplayHeader>::err(ErrorCode::InvalidMagic);
    }

    const usize expected_bytes = sizeof(ReplayHeader) + sizeof(SimCommand) * header.total_commands;
    if (raw.size() != expected_bytes) {
        return Result<ReplayHeader>::err(ErrorCode::CorruptData);
    }

    if (header.total_commands > MAX_PENDING_COMMANDS) {
        return Result<ReplayHeader>::err(ErrorCode::OutOfMemory);
    }

    pending_count_ = 0;
    history_count_ = 0;

    if (header.total_commands > 0) {
        std::memcpy(pending_, raw.data() + sizeof(ReplayHeader), sizeof(SimCommand) * header.total_commands);
        pending_count_ = header.total_commands;
    }

    return Result<ReplayHeader>::ok(header);
}

} // namespace ron
