#include "render/icon_map.hpp"
#include <string_view>

namespace ron::render {

struct IconMapping {
    std::string_view name;
    u16 icon;
    u8 shape;
};

static const IconMapping K_ICON_MAP[71] = {
    {"inf_spear", 4, 0},
    {"inf_bow", 4, 0},
    {"inf_sword", 4, 0},
    {"inf_musket", 4, 0},
    {"inf_rifle", 4, 0},
    {"inf_at", 4, 0},
    {"inf_special", 9, 0},
    {"inf_worker", 11, 0},
    {"inf_drone_op", 13, 0},
    {"cav_light", 9, 0},
    {"cav_heavy", 5, 0},
    {"siege_catapult", 6, 0},
    {"siege_cannon", 6, 0},
    {"veh_tank_early", 5, 0},
    {"veh_mbt", 5, 0},
    {"veh_ifv", 5, 0},
    {"veh_arty_sp", 6, 0},
    {"veh_apc", 4, 0},
    {"veh_himars", 10, 0},
    {"veh_sam", 10, 0},
    {"veh_ew", 9, 0},
    {"veh_truck", 12, 0},
    {"veh_ugv", 13, 0},
    {"air_biplane", 7, 1},
    {"air_prop", 7, 1},
    {"air_jet", 7, 1},
    {"air_stealth", 9, 1},
    {"air_bomber", 7, 1},
    {"air_helo", 7, 1},
    {"air_fpv", 13, 1},
    {"nav_galley", 8, 2},
    {"nav_galleon", 8, 2},
    {"nav_ironclad", 8, 2},
    {"nav_destroyer", 8, 2},
    {"nav_carrier", 8, 2},
    {"nav_sub", 8, 2},
    {"bld_city_center_a", 12, 3},
    {"bld_city_center_b", 12, 3},
    {"bld_city_center_c", 12, 3},
    {"bld_house", 29, 3},
    {"bld_farm", 14, 3},
    {"bld_lumber", 15, 3},
    {"bld_mine", 16, 3},
    {"bld_market", 17, 3},
    {"bld_barracks", 4, 3},
    {"bld_factory", 30, 3},
    {"bld_airfield", 7, 3},
    {"bld_dock", 8, 3},
    {"bld_library", 18, 3},
    {"bld_tower", 9, 3},
    {"bld_sam_site", 10, 3},
    {"bld_radar", 9, 3},
    {"bld_silo", 10, 3},
    {"bld_refinery", 19, 3},
    {"prop_tree_oak", 0xFFFFu, 0},
    {"prop_tree_pine", 0xFFFFu, 0},
    {"prop_tree_palm", 0xFFFFu, 0},
    {"prop_rock", 0xFFFFu, 0},
    {"res_berry_bush", 0xFFFFu, 0},
    {"res_herd", 0xFFFFu, 0},
    {"res_gold", 0xFFFFu, 0},
    {"res_iron", 0xFFFFu, 0},
    {"res_oil", 0xFFFFu, 0},
    {"res_silicon", 0xFFFFu, 0},
    {"res_relic", 0xFFFFu, 0},
    {"proj_arrow", 0xFFFFu, 0},
    {"proj_shell", 0xFFFFu, 0},
    {"proj_rocket", 0xFFFFu, 0},
    {"proj_missile", 0xFFFFu, 0},
    {"proj_hypersonic", 0xFFFFu, 0},
    {"proj_bomb", 0xFFFFu, 0}
};

void build_model_marks(const ModelCpu* models, int count, ModelMark* out) noexcept {
    for (int i = 0; i < count; ++i) {
        u8 cat = (models != nullptr) ? models[i].category : 0;
        u8 shape = 0;
        if (cat == 2) shape = 1;      // Air
        else if (cat == 3) shape = 2; // Naval
        else if (cat == 4) shape = 3; // Building
        else shape = 0;               // Ground (Infantry / Vehicle)

        u16 icon = (cat >= 5) ? 0xFFFFu : 4u;

        // 1. Try matching by name
        bool matched = false;
        if (models != nullptr && !models[i].name.empty()) {
            for (const auto& item : K_ICON_MAP) {
                if (models[i].name == item.name) {
                    icon = item.icon;
                    shape = item.shape;
                    matched = true;
                    break;
                }
            }
        }

        // 2. If not matched by name, match by catalogue index
        if (!matched && i < 71) {
            icon = K_ICON_MAP[i].icon;
            shape = K_ICON_MAP[i].shape;
        }

        // 3. Enforce category overrides per spec
        if (cat >= 5) {
            icon = 0xFFFFu; // Prop, resource, projectile have no icon
        }
        if (cat == 2) shape = 1;
        else if (cat == 3) shape = 2;
        else if (cat == 4) shape = 3;

        out[i].icon = icon;
        out[i].shape = shape;
    }
}

} // namespace ron::render
