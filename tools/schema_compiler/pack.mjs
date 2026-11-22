import { fnv1a64, xxh64 } from './hashes.mjs';
import { compileRecordLayouts } from './schema.mjs';

const NONE16 = 0xFFFF;
const NONE8 = 0xFF;

function toFx32(val) {
  if (val === undefined || val === null) return 0;
  let raw = Math.round(Number(val) * 65536);
  if (raw > 0x7FFFFFFF) raw = 0x7FFFFFFF;
  if (raw < -0x7FFFFFFF) raw = -0x7FFFFFFF;
  return raw;
}

export function packDb(data) {
  const { layouts, layoutHash } = compileRecordLayouts();

  const {
    enums,
    ages,
    resources,
    sensors,
    weapons,
    units,
    buildings,
    techs
  } = data;

  // Build ID to index maps
  const ageMap = new Map(ages.map((a, i) => [a.id, i]));
  const resourceMap = new Map(resources.map((r, i) => [r.id, i]));
  const sensorMap = new Map(sensors.map((s, i) => [s.id, i]));
  const weaponMap = new Map(weapons.map((w, i) => [w.id, i]));
  const unitMap = new Map(units.map((u, i) => [u.id, i]));
  const buildingMap = new Map(buildings.map((b, i) => [b.id, i]));
  const techMap = new Map(techs.map((t, i) => [t.id, i]));

  // String table: offset 0 is \0
  const stringBufs = [Buffer.from([0])];
  const stringOffsetMap = new Map([['', 0]]);
  let currentStringTableLen = 1;

  function internString(str) {
    if (!str) return 0;
    if (stringOffsetMap.has(str)) {
      return stringOffsetMap.get(str);
    }
    const off = currentStringTableLen;
    const buf = Buffer.from(str + '\0', 'utf8');
    stringBufs.push(buf);
    currentStringTableLen += buf.length;
    stringOffsetMap.set(str, off);
    return off;
  }

  // Helper to pack cost to u16[7]
  function packCost(costObj) {
    const arr = [0, 0, 0, 0, 0, 0, 0];
    if (costObj && typeof costObj === 'object') {
      for (const [k, v] of Object.entries(costObj)) {
        const idx = resourceMap.get(k);
        if (idx !== undefined && idx >= 0 && idx < 7) {
          arr[idx] = Number(v) & 0xFFFF;
        }
      }
    }
    return arr;
  }

  // Pre-intern all names
  for (const list of [ages, resources, sensors, weapons, units, buildings, techs]) {
    for (const item of list) {
      internString(item.name || item.id);
    }
  }

  // Pack Ages
  const ageStride = layouts.AgeDef.size;
  const ageBuf = Buffer.alloc(ages.length * ageStride, 0);
  for (let i = 0; i < ages.length; i++) {
    const a = ages[i];
    const off = i * ageStride;
    ageBuf.writeBigUInt64LE(fnv1a64(a.id), off + 0);
    ageBuf.writeUInt32LE(internString(a.name || a.id), off + 8);
    ageBuf.writeUInt8(a.index, off + 12);
    // pad: 13..15
    ageBuf.writeInt32LE(a.year_start, off + 16);
    ageBuf.writeInt32LE(a.year_end, off + 20);
    const costs = packCost(a.advance_cost);
    for (let c = 0; c < 7; c++) {
      ageBuf.writeUInt16LE(costs[c], off + 24 + c * 2);
    }
    ageBuf.writeUInt16LE(a.min_techs ?? 0, off + 38);
  }

  // Pack Resources
  const resStride = layouts.ResourceDef.size;
  const resBuf = Buffer.alloc(resources.length * resStride, 0);
  for (let i = 0; i < resources.length; i++) {
    const r = resources[i];
    const off = i * resStride;
    resBuf.writeBigUInt64LE(fnv1a64(r.id), off + 0);
    resBuf.writeUInt32LE(internString(r.name || r.id), off + 8);
    resBuf.writeUInt8(r.index, off + 12);
    resBuf.writeUInt8(r.unlock_age, off + 13);
    // pad: 14..15
    resBuf.writeInt32LE(r.start_amount ?? 0, off + 16);
    resBuf.writeInt32LE(r.base_income_cap_per_min ?? 0, off + 20);
    resBuf.writeInt32LE(toFx32(r.cap_growth_per_tier), off + 24);
    resBuf.writeInt32LE(toFx32(r.gather_rate_per_citizen_per_sec), off + 28);
  }

  // Pack Sensors
  const sensorStride = layouts.SensorDef.size;
  const sensorBuf = Buffer.alloc(sensors.length * sensorStride, 0);
  for (let i = 0; i < sensors.length; i++) {
    const s = sensors[i];
    const off = i * sensorStride;
    const bandIdx = enums.SensorBand.indexOf(s.band);
    let flagsVal = 0;
    if (Array.isArray(s.flags)) {
      for (const f of s.flags) {
        // e.g. flags
      }
    } else if (typeof s.flags === 'number') {
      flagsVal = s.flags;
    }
    sensorBuf.writeBigUInt64LE(fnv1a64(s.id), off + 0);
    sensorBuf.writeUInt32LE(internString(s.name || s.id), off + 8);
    sensorBuf.writeUInt8(bandIdx >= 0 ? bandIdx : 0, off + 12);
    sensorBuf.writeUInt8(flagsVal, off + 13);
    sensorBuf.writeUInt16LE(s.scan_period_ticks ?? 20, off + 14);
    sensorBuf.writeInt32LE(toFx32(s.range), off + 16);
    sensorBuf.writeUInt32LE(s.power ?? 0, off + 20);
    sensorBuf.writeUInt32LE(s.detect_threshold ?? 0, off + 24);
  }

  // Pack Weapons
  const weaponStride = layouts.WeaponDef.size;
  const weaponBuf = Buffer.alloc(weapons.length * weaponStride, 0);
  for (let i = 0; i < weapons.length; i++) {
    const w = weapons[i];
    const off = i * weaponStride;
    const archIdx = enums.DamageArchetype.indexOf(w.damage_archetype);
    const guidIdx = enums.GuidanceType.indexOf(w.guidance);
    let flagsVal = 0;
    if (Array.isArray(w.flags)) {
      for (const f of w.flags) {
        const fi = enums.WeaponFlag.indexOf(f);
        if (fi >= 0) flagsVal |= (1 << fi);
      }
    }

    weaponBuf.writeBigUInt64LE(fnv1a64(w.id), off + 0);
    weaponBuf.writeUInt32LE(internString(w.name || w.id), off + 8);
    weaponBuf.writeUInt8(archIdx >= 0 ? archIdx : 0, off + 12);
    weaponBuf.writeUInt8(guidIdx >= 0 ? guidIdx : 0, off + 13);
    weaponBuf.writeUInt16LE(flagsVal, off + 14);
    weaponBuf.writeInt32LE(w.damage ?? 0, off + 16);
    weaponBuf.writeInt32LE(toFx32(w.splash_radius), off + 20);
    weaponBuf.writeInt32LE(toFx32(w.range_min), off + 24);
    weaponBuf.writeInt32LE(toFx32(w.range_max), off + 28);
    weaponBuf.writeInt32LE(toFx32(w.projectile_speed), off + 32);
    weaponBuf.writeUInt16LE(w.cooldown_ticks ?? 20, off + 36);
    weaponBuf.writeUInt8(w.burst_count ?? 1, off + 38);
    weaponBuf.writeUInt8(w.interceptable_mass ?? 0, off + 39);
    weaponBuf.writeUInt16LE(w.turn_rate_bam ?? 0, off + 40);
    weaponBuf.writeUInt16LE(w.dispersion_bam ?? 0, off + 42);

    // class_mult_q8 u16[13]
    const baseMult = w.class_mult_all ?? 256;
    for (let c = 0; c < 13; c++) {
      const cName = enums.UnitClass[c];
      let mult = baseMult;
      if (w.class_mult_overrides && w.class_mult_overrides[cName] !== undefined) {
        mult = w.class_mult_overrides[cName];
      }
      weaponBuf.writeUInt16LE(mult & 0xFFFF, off + 44 + c * 2);
    }
  }

  // Pack Units
  const unitStride = layouts.UnitDef.size;
  const unitBuf = Buffer.alloc(units.length * unitStride, 0);
  for (let i = 0; i < units.length; i++) {
    const u = units[i];
    const off = i * unitStride;
    const domIdx = enums.MovementDomain.indexOf(u.domain);
    const clsIdx = enums.UnitClass.indexOf(u.unit_class);
    const upTo = u.upgrades_to ? (unitMap.get(u.upgrades_to) ?? NONE16) : NONE16;
    const trAt = u.trained_at ? (buildingMap.get(u.trained_at) ?? NONE16) : NONE16;
    const wpPri = u.weapon_primary ? (weaponMap.get(u.weapon_primary) ?? NONE16) : NONE16;
    const wpSec = u.weapon_secondary ? (weaponMap.get(u.weapon_secondary) ?? NONE16) : NONE16;
    const snPri = u.sensor_primary ? (sensorMap.get(u.sensor_primary) ?? NONE16) : (sensorMap.get('optical_basic') ?? NONE16);
    const snSec = u.sensor_secondary ? (sensorMap.get(u.sensor_secondary) ?? NONE16) : NONE16;

    let flagsVal = 0;
    if (Array.isArray(u.flags)) {
      for (const f of u.flags) {
        const fi = enums.UnitFlag.indexOf(f);
        if (fi >= 0) flagsVal |= (1 << fi);
      }
    }

    unitBuf.writeBigUInt64LE(fnv1a64(u.id), off + 0);
    unitBuf.writeUInt32LE(internString(u.name || u.id), off + 8);
    unitBuf.writeUInt8(u.age_index, off + 12);
    unitBuf.writeUInt8(domIdx >= 0 ? domIdx : 0, off + 13);
    unitBuf.writeUInt8(clsIdx >= 0 ? clsIdx : 0, off + 14);
    unitBuf.writeUInt8(u.line_id ?? 0, off + 15);
    unitBuf.writeUInt16LE(upTo, off + 16);
    unitBuf.writeUInt16LE(trAt, off + 18);
    const costs = packCost(u.cost);
    for (let c = 0; c < 7; c++) {
      unitBuf.writeUInt16LE(costs[c], off + 20 + c * 2);
    }
    unitBuf.writeUInt16LE(u.train_ticks ?? 200, off + 34);
    unitBuf.writeUInt8(u.pop_cost ?? 1, off + 36);
    // pad: 37..39
    unitBuf.writeUInt32LE(flagsVal, off + 40);
    unitBuf.writeInt32LE(u.max_hp ?? 100, off + 44);
    unitBuf.writeInt32LE(toFx32(u.move_speed), off + 48);
    unitBuf.writeInt32LE(toFx32(u.radius ?? 0.5), off + 52);
    unitBuf.writeUInt16LE(snPri, off + 56);
    unitBuf.writeUInt16LE(snSec, off + 58);
    unitBuf.writeUInt16LE(u.sig_optical_q8 ?? 256, off + 60);
    unitBuf.writeUInt16LE(u.sig_radar_q8 ?? 256, off + 62);
    unitBuf.writeUInt16LE(u.sig_sonar_q8 ?? 256, off + 64);
    unitBuf.writeUInt16LE(u.armor_front ?? 0, off + 66);
    unitBuf.writeUInt16LE(u.armor_flank ?? 0, off + 68);
    unitBuf.writeUInt16LE(u.armor_top ?? 0, off + 70);
    unitBuf.writeUInt16LE(wpPri, off + 72);
    unitBuf.writeUInt16LE(wpSec, off + 74);
    unitBuf.writeUInt8(u.aps_max_charges ?? 0, off + 76);
    unitBuf.writeUInt8(u.smoke_charges ?? 0, off + 77);
    unitBuf.writeUInt16LE(u.aps_reload_ticks ?? 0, off + 78);
    unitBuf.writeUInt16LE(u.shorad_capacitor_kw ?? 0, off + 80);
    unitBuf.writeUInt16LE(u.ew_jam_power ?? 0, off + 82);
  }

  // Pack Buildings
  const bldStride = layouts.BuildingDef.size;
  const bldBuf = Buffer.alloc(buildings.length * bldStride, 0);
  for (let i = 0; i < buildings.length; i++) {
    const b = buildings[i];
    const off = i * bldStride;
    const gRes = b.gather_resource ? (resourceMap.get(b.gather_resource) ?? NONE8) : NONE8;
    const wpPri = b.weapon_primary ? (weaponMap.get(b.weapon_primary) ?? NONE16) : NONE16;
    const snPri = b.sensor_primary ? (sensorMap.get(b.sensor_primary) ?? NONE16) : NONE16;

    let flagsVal = 0;
    if (Array.isArray(b.flags)) {
      for (const f of b.flags) {
        const fi = enums.BuildingFlag.indexOf(f);
        if (fi >= 0) flagsVal |= (1 << fi);
      }
    }

    bldBuf.writeBigUInt64LE(fnv1a64(b.id), off + 0);
    bldBuf.writeUInt32LE(internString(b.name || b.id), off + 8);
    bldBuf.writeUInt8(b.age_index, off + 12);
    bldBuf.writeUInt8(b.footprint_w, off + 13);
    bldBuf.writeUInt8(b.footprint_h, off + 14);
    bldBuf.writeUInt8(gRes, off + 15);
    bldBuf.writeUInt8(b.gather_slots ?? 0, off + 16);
    bldBuf.writeUInt8(b.pop_cap ?? 0, off + 17);
    // pad: 18..19
    bldBuf.writeUInt32LE(flagsVal, off + 20);
    const costs = packCost(b.cost);
    for (let c = 0; c < 7; c++) {
      bldBuf.writeUInt16LE(costs[c], off + 24 + c * 2);
    }
    bldBuf.writeUInt16LE(b.build_ticks ?? 600, off + 38);
    bldBuf.writeInt32LE(b.max_hp ?? 1000, off + 40);
    bldBuf.writeInt32LE(toFx32(b.border_radius ?? 0), off + 44);
    bldBuf.writeUInt16LE(snPri, off + 48);
    bldBuf.writeUInt16LE(wpPri, off + 50);
    bldBuf.writeUInt16LE(b.armor ?? 0, off + 52);

    for (let t = 0; t < 8; t++) {
      const unitId = b.trains && b.trains[t];
      const uIdx = unitId ? (unitMap.get(unitId) ?? NONE16) : NONE16;
      bldBuf.writeUInt16LE(uIdx, off + 54 + t * 2);
    }
  }

  // Pack Techs
  const techStride = layouts.TechDef.size;
  const techBuf = Buffer.alloc(techs.length * techStride, 0);
  for (let i = 0; i < techs.length; i++) {
    const t = techs[i];
    const off = i * techStride;
    const pilIdx = enums.TechPillar.indexOf(t.pillar);
    const prqIdx = t.prereq_tech ? (techMap.get(t.prereq_tech) ?? NONE16) : NONE16;
    const opIdx = enums.TechEffectOp.indexOf(t.effect_op);

    techBuf.writeBigUInt64LE(fnv1a64(t.id), off + 0);
    techBuf.writeUInt32LE(internString(t.name || t.id), off + 8);
    techBuf.writeUInt8(pilIdx >= 0 ? pilIdx : 0, off + 12);
    techBuf.writeUInt8(t.tier, off + 13);
    techBuf.writeUInt8(t.age_required, off + 14);
    // pad: 15
    techBuf.writeUInt16LE(prqIdx, off + 16);
    const costs = packCost(t.cost);
    for (let c = 0; c < 7; c++) {
      techBuf.writeUInt16LE(costs[c], off + 18 + c * 2);
    }
    techBuf.writeUInt16LE(t.research_ticks ?? 600, off + 32);
    techBuf.writeUInt8(opIdx >= 0 ? opIdx : 0, off + 34);
    // pad: 35
    techBuf.writeUInt16LE(t.effect_target ?? 0, off + 36);
    // pad: 38..39
    techBuf.writeInt32LE(toFx32(t.effect_value), off + 40);
  }

  // Concatenate string table
  const stringTableBuf = Buffer.concat(stringBufs);

  // Assemble full binary file
  // Header: 64 bytes
  // Section Table: 8 * 16 = 128 bytes
  // Sections: 0..7
  const sectionBuffers = [
    { buf: ageBuf, count: ages.length, stride: ageStride },
    { buf: resBuf, count: resources.length, stride: resStride },
    { buf: sensorBuf, count: sensors.length, stride: sensorStride },
    { buf: weaponBuf, count: weapons.length, stride: weaponStride },
    { buf: unitBuf, count: units.length, stride: unitStride },
    { buf: bldBuf, count: buildings.length, stride: bldStride },
    { buf: techBuf, count: techs.length, stride: techStride },
    { buf: stringTableBuf, count: stringTableBuf.length, stride: 1 }
  ];

  const HEADER_SIZE = 64;
  const SECTION_TABLE_SIZE = 8 * 16; // 128 bytes
  let curOffset = HEADER_SIZE + SECTION_TABLE_SIZE; // 192, 64-byte aligned!

  const sectionTableBuf = Buffer.alloc(SECTION_TABLE_SIZE, 0);
  const alignedPieces = [];

  for (let s = 0; s < 8; s++) {
    const sec = sectionBuffers[s];
    // align curOffset to 64
    const rem = curOffset % 64;
    if (rem !== 0) {
      const padBytes = 64 - rem;
      alignedPieces.push(Buffer.alloc(padBytes, 0));
      curOffset += padBytes;
    }

    const secOffset = curOffset;
    sectionTableBuf.writeUInt32LE(secOffset, s * 16 + 0);
    sectionTableBuf.writeUInt32LE(sec.count, s * 16 + 4);
    sectionTableBuf.writeUInt32LE(sec.stride, s * 16 + 8);
    sectionTableBuf.writeUInt32LE(0, s * 16 + 12); // pad

    alignedPieces.push(sec.buf);
    curOffset += sec.buf.length;
  }

  // Align final file size to 64
  const finalRem = curOffset % 64;
  if (finalRem !== 0) {
    const padBytes = 64 - finalRem;
    alignedPieces.push(Buffer.alloc(padBytes, 0));
    curOffset += padBytes;
  }

  const totalFileSize = curOffset;
  const strSecOffset = sectionTableBuf.readUInt32LE(7 * 16 + 0);
  const strSecCount = sectionTableBuf.readUInt32LE(7 * 16 + 4);

  // Build header
  const headerBuf = Buffer.alloc(HEADER_SIZE, 0);
  headerBuf.writeUInt32LE(0x444E4F52, 0); // 'ROND'
  headerBuf.writeUInt32LE(1, 4); // schema_version = 1
  headerBuf.writeBigUInt64LE(layoutHash, 8); // layout_hash
  // offset 16 is content_xxh64, filled after assembling rest
  headerBuf.writeUInt32LE(totalFileSize, 24);
  headerBuf.writeUInt32LE(8, 28); // section_count = 8
  headerBuf.writeUInt32LE(strSecOffset, 32); // string_table_offset
  headerBuf.writeUInt32LE(strSecCount, 36); // string_table_size
  // 40..63 is 24-byte zero padding

  const restBuf = Buffer.concat([sectionTableBuf, ...alignedPieces]);
  const contentHash = xxh64(restBuf, 0n);
  headerBuf.writeBigUInt64LE(contentHash, 16);

  const fullFileBuf = Buffer.concat([headerBuf, restBuf]);
  return {
    buffer: fullFileBuf,
    layoutHash,
    contentHash,
    totalFileSize
  };
}
