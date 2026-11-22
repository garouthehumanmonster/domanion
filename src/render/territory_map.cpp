#include "render/territory_map.hpp"
#include <cmath>
#include <vector>
#include <algorithm>

namespace ron::render {

void territory_rasterize(const TerritorySource* src, int n, const HeightSampler* land_or_null, u8* out /*512*512*/) {
    if (!out) return;

    for (int j = 0; j < 512; ++j) {
        float cz = (static_cast<float>(j) + 0.5f) * 8.0f;
        for (int i = 0; i < 512; ++i) {
            float cx = (static_cast<float>(i) + 0.5f) * 8.0f;

            if (land_or_null && land_or_null->height_m(cx, cz) < 0.0f) {
                out[j * 512 + i] = 0;
                continue;
            }

            float best_score = -1.0f;
            u8 best_owner = 0;
            int best_idx = -1;

            for (int s = 0; s < n; ++s) {
                float dx = cx - src[s].x;
                float dz = cz - src[s].z;
                float dist = std::sqrt(dx * dx + dz * dz);
                if (dist < src[s].radius) {
                    float score = 1.0f - (dist / src[s].radius);
                    if (best_idx < 0) {
                        best_score = score;
                        best_owner = src[s].owner;
                        best_idx = s;
                    } else if (score > best_score) {
                        best_score = score;
                        best_owner = src[s].owner;
                        best_idx = s;
                    } else if (score == best_score) {
                        // ties -> lower owner id, then lower index
                        if (src[s].owner < best_owner) {
                            best_score = score;
                            best_owner = src[s].owner;
                            best_idx = s;
                        } else if (src[s].owner == best_owner && s < best_idx) {
                            best_score = score;
                            best_owner = src[s].owner;
                            best_idx = s;
                        }
                    }
                }
            }

            out[j * 512 + i] = (best_idx >= 0) ? best_owner : 0;
        }
    }
}

int pick_land_sites(const HeightSampler& sampler, int count, float min_sep_m, Site* out) {
    if (!out || count <= 0) return 0;

    struct Candidate {
        float x;
        float z;
        int i;
        int j;
    };

    std::vector<Candidate> candidates;
    candidates.reserve(32 * 32);

    for (int j = 0; j < 32; ++j) {
        float z = 64.0f + 128.0f * static_cast<float>(j);
        for (int i = 0; i < 32; ++i) {
            float x = 64.0f + 128.0f * static_cast<float>(i);
            float h = sampler.height_m(x, z);
            if (h >= 5.0f && h <= 250.0f) {
                float h_xp = sampler.height_m(x + 32.0f, z);
                float h_xm = sampler.height_m(x - 32.0f, z);
                float h_zp = sampler.height_m(x, z + 32.0f);
                float h_zm = sampler.height_m(x, z - 32.0f);
                if (h_xp >= 2.0f && h_xm >= 2.0f && h_zp >= 2.0f && h_zm >= 2.0f) {
                    candidates.push_back({x, z, i, j});
                }
            }
        }
    }

    if (candidates.empty()) return 0;

    // First site: candidate closest to (2048, 2048) (ties: lowest j then i)
    float best_d2 = 1e30f;
    int first_idx = -1;
    for (size_t k = 0; k < candidates.size(); ++k) {
        float dx = candidates[k].x - 2048.0f;
        float dz = candidates[k].z - 2048.0f;
        float d2 = dx * dx + dz * dz;
        if (d2 < best_d2) {
            best_d2 = d2;
            first_idx = static_cast<int>(k);
        } else if (d2 == best_d2) {
            if (candidates[k].j < candidates[first_idx].j ||
               (candidates[k].j == candidates[first_idx].j && candidates[k].i < candidates[first_idx].i)) {
                best_d2 = d2;
                first_idx = static_cast<int>(k);
            }
        }
    }

    int placed = 0;
    out[placed++] = {candidates[first_idx].x, candidates[first_idx].z};

    while (placed < count) {
        float best_max_min_d = -1.0f;
        int next_best_k = -1;

        for (size_t k = 0; k < candidates.size(); ++k) {
            bool already = false;
            for (int p = 0; p < placed; ++p) {
                if (candidates[k].x == out[p].x && candidates[k].z == out[p].z) {
                    already = true;
                    break;
                }
            }
            if (already) continue;

            float min_d = 1e30f;
            for (int p = 0; p < placed; ++p) {
                float dx = candidates[k].x - out[p].x;
                float dz = candidates[k].z - out[p].z;
                float d = std::sqrt(dx * dx + dz * dz);
                if (d < min_d) {
                    min_d = d;
                }
            }

            if (min_d > best_max_min_d) {
                best_max_min_d = min_d;
                next_best_k = static_cast<int>(k);
            } else if (min_d == best_max_min_d && next_best_k >= 0) {
                // ties: lowest j then i
                if (candidates[k].j < candidates[next_best_k].j ||
                   (candidates[k].j == candidates[next_best_k].j && candidates[k].i < candidates[next_best_k].i)) {
                    best_max_min_d = min_d;
                    next_best_k = static_cast<int>(k);
                }
            }
        }

        if (next_best_k < 0 || best_max_min_d < min_sep_m) {
            break;
        }

        out[placed++] = {candidates[next_best_k].x, candidates[next_best_k].z};
    }

    return placed;
}

} // namespace ron::render
