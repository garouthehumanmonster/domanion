import { xxh64 } from './hashes.mjs';

export const TYPE_INFO = {
  u8:   { size: 1, align: 1, cpp: 'u8' },
  u16:  { size: 2, align: 2, cpp: 'u16' },
  u32:  { size: 4, align: 4, cpp: 'u32' },
  i32:  { size: 4, align: 4, cpp: 'i32' },
  fx32: { size: 4, align: 4, cpp: 'fx32' },
  u64:  { size: 8, align: 8, cpp: 'u64' },
};

function parseFieldType(typeStr) {
  const match = typeStr.match(/^([a-z0-9]+)(?:\[(\d+)\])?$/);
  if (!match) throw new Error(`Invalid type: ${typeStr}`);
  const baseName = match[1];
  const count = match[2] ? parseInt(match[2], 10) : null;
  const baseInfo = TYPE_INFO[baseName];
  if (!baseInfo) throw new Error(`Unknown base type: ${baseName}`);

  if (count !== null) {
    return {
      base: baseName,
      count,
      size: baseInfo.size * count,
      align: baseInfo.align,
      cpp: `${baseInfo.cpp}[${count}]`,
      isFixedArray: true,
      elemCpp: baseInfo.cpp
    };
  }
  return {
    base: baseName,
    count: null,
    size: baseInfo.size,
    align: baseInfo.align,
    cpp: baseInfo.cpp,
    isFixedArray: false,
    elemCpp: baseInfo.cpp
  };
}

// Declared field definitions per record type (excluding the universal prefix id_hash and name_off)
export const RECORD_DECLARATIONS = {
  AgeDef: [
    ['index', 'u8'],
    ['year_start', 'i32'],
    ['year_end', 'i32'],
    ['advance_cost', 'u16[7]'],
    ['min_techs', 'u16']
  ],
  ResourceDef: [
    ['index', 'u8'],
    ['unlock_age', 'u8'],
    ['start_amount', 'i32'],
    ['base_income_cap_per_min', 'i32'],
    ['cap_growth_per_tier', 'fx32'],
    ['gather_rate_per_citizen_per_sec', 'fx32']
  ],
  SensorDef: [
    ['band', 'u8'],
    ['flags', 'u8'],
    ['scan_period_ticks', 'u16'],
    ['range', 'fx32'],
    ['power', 'u32'],
    ['detect_threshold', 'u32']
  ],
  WeaponDef: [
    ['damage_archetype', 'u8'],
    ['guidance', 'u8'],
    ['flags', 'u16'],
    ['damage', 'i32'],
    ['splash_radius', 'fx32'],
    ['range_min', 'fx32'],
    ['range_max', 'fx32'],
    ['projectile_speed', 'fx32'],
    ['cooldown_ticks', 'u16'],
    ['burst_count', 'u8'],
    ['interceptable_mass', 'u8'],
    ['turn_rate_bam', 'u16'],
    ['dispersion_bam', 'u16'],
    ['class_mult_q8', 'u16[13]']
  ],
  UnitDef: [
    ['age_index', 'u8'],
    ['domain', 'u8'],
    ['unit_class', 'u8'],
    ['line_id', 'u8'],
    ['upgrades_to', 'u16'],
    ['trained_at', 'u16'],
    ['cost', 'u16[7]'],
    ['train_ticks', 'u16'],
    ['pop_cost', 'u8'],
    ['flags', 'u32'],
    ['max_hp', 'i32'],
    ['move_speed', 'fx32'],
    ['radius', 'fx32'],
    ['sensor_primary', 'u16'],
    ['sensor_secondary', 'u16'],
    ['sig_optical_q8', 'u16'],
    ['sig_radar_q8', 'u16'],
    ['sig_sonar_q8', 'u16'],
    ['armor_front', 'u16'],
    ['armor_flank', 'u16'],
    ['armor_top', 'u16'],
    ['weapon_primary', 'u16'],
    ['weapon_secondary', 'u16'],
    ['aps_max_charges', 'u8'],
    ['smoke_charges', 'u8'],
    ['aps_reload_ticks', 'u16'],
    ['shorad_capacitor_kw', 'u16'],
    ['ew_jam_power', 'u16']
  ],
  BuildingDef: [
    ['age_index', 'u8'],
    ['footprint_w', 'u8'],
    ['footprint_h', 'u8'],
    ['gather_resource', 'u8'],
    ['gather_slots', 'u8'],
    ['pop_cap', 'u8'],
    ['flags', 'u32'],
    ['cost', 'u16[7]'],
    ['build_ticks', 'u16'],
    ['max_hp', 'i32'],
    ['border_radius', 'fx32'],
    ['sensor_primary', 'u16'],
    ['weapon_primary', 'u16'],
    ['armor', 'u16'],
    ['trains', 'u16[8]']
  ],
  TechDef: [
    ['pillar', 'u8'],
    ['tier', 'u8'],
    ['age_required', 'u8'],
    ['prereq_tech', 'u16'],
    ['cost', 'u16[7]'],
    ['research_ticks', 'u16'],
    ['effect_op', 'u8'],
    ['effect_target', 'u16'],
    ['effect_value', 'fx32']
  ]
};

export function compileRecordLayouts() {
  const layouts = {};
  let canonicalTokens = [];

  for (const [structName, declaredFields] of Object.entries(RECORD_DECLARATIONS)) {
    // Universal prefix for every record: id_hash (u64 at offset 0), name_off (u32 at offset 8)
    const allFields = [
      ['id_hash', 'u64'],
      ['name_off', 'u32'],
      ...declaredFields
    ];

    let offset = 0;
    let padIndex = 0;
    const compiledFields = [];

    for (const [fieldName, fieldTypeStr] of allFields) {
      const info = parseFieldType(fieldTypeStr);
      const rem = offset % info.align;
      if (rem !== 0) {
        const padBytes = info.align - rem;
        compiledFields.push({
          name: `_pad${padIndex++}`,
          isPad: true,
          offset,
          size: padBytes,
          align: 1,
          cpp: padBytes === 1 ? 'u8' : `u8[${padBytes}]`,
          elemCpp: 'u8',
          count: padBytes === 1 ? null : padBytes,
          isFixedArray: padBytes > 1,
          base: 'u8'
        });
        offset += padBytes;
      }

      compiledFields.push({
        name: fieldName,
        isPad: false,
        offset,
        size: info.size,
        align: info.align,
        cpp: info.cpp,
        elemCpp: info.elemCpp,
        count: info.count,
        isFixedArray: info.isFixedArray,
        base: info.base
      });
      offset += info.size;
    }

    // Tail pad to multiple of 8
    const tailRem = offset % 8;
    if (tailRem !== 0) {
      const padBytes = 8 - tailRem;
      compiledFields.push({
        name: `_pad${padIndex++}`,
        isPad: true,
        offset,
        size: padBytes,
        align: 1,
        cpp: padBytes === 1 ? 'u8' : `u8[${padBytes}]`,
        elemCpp: 'u8',
        count: padBytes === 1 ? null : padBytes,
        isFixedArray: padBytes > 1,
        base: 'u8'
      });
      offset += padBytes;
    }

    layouts[structName] = {
      name: structName,
      size: offset,
      fields: compiledFields,
      contentFields: compiledFields.filter(f => !f.isPad)
    };

    for (const f of compiledFields) {
      canonicalTokens.push(`${structName}:${f.name}:${f.cpp}:${f.offset}`);
    }
  }

  const canonicalString = canonicalTokens.join(';') + ';';
  const layoutHash = xxh64(canonicalString, 0n);

  return {
    layouts,
    canonicalString,
    layoutHash
  };
}
