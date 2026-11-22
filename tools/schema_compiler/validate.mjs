import { RECORD_DECLARATIONS } from './schema.mjs';

const ID_REGEX = /^[a-z][a-z0-9_]{1,31}$/;

export function validateAll(data) {
  const errors = [];

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

  // Build lookup maps
  const ageMap = new Map();
  const resourceMap = new Map();
  const sensorMap = new Map();
  const weaponMap = new Map();
  const unitMap = new Map();
  const buildingMap = new Map();
  const techMap = new Map();

  function checkIds(list, name, map) {
    for (let i = 0; i < list.length; i++) {
      const item = list[i];
      if (!item.id || !ID_REGEX.test(item.id)) {
        errors.push(`${name}[${i}] id "${item.id}" does not match regex ^[a-z][a-z0-9_]{1,31}$`);
      }
      if (map.has(item.id)) {
        errors.push(`Duplicate ${name} id "${item.id}" at index ${i}`);
      }
      map.set(item.id, { ...item, _index: i });
    }
  }

  checkIds(ages, 'Age', ageMap);
  checkIds(resources, 'Resource', resourceMap);
  checkIds(sensors, 'Sensor', sensorMap);
  checkIds(weapons, 'Weapon', weaponMap);
  checkIds(units, 'Unit', unitMap);
  checkIds(buildings, 'Building', buildingMap);
  checkIds(techs, 'Tech', techMap);

  // Validate Resource unlock_age
  for (const res of resources) {
    if (res.unlock_age < 0 || res.unlock_age > 8) {
      errors.push(`Resource ${res.id} unlock_age ${res.unlock_age} out of range [0, 8]`);
    }
  }

  // Helper to validate cost objects and era-access
  function validateCost(costObj, ageIndex, itemType, itemId) {
    if (!costObj || typeof costObj !== 'object') return;
    for (const [resKey, amount] of Object.entries(costObj)) {
      if (!resourceMap.has(resKey)) {
        errors.push(`${itemType} ${itemId} references unknown resource "${resKey}" in cost`);
        continue;
      }
      const resDef = resourceMap.get(resKey);
      if (resDef.unlock_age > ageIndex) {
        errors.push(`${itemType} ${itemId} (age ${ageIndex}) costs "${resKey}" unlocked in later age ${resDef.unlock_age}`);
      }
      if (amount < 0 || amount > 65535) {
        errors.push(`${itemType} ${itemId} cost amount for "${resKey}" (${amount}) out of u16 range`);
      }
    }
  }

  // Validate Ages
  for (const age of ages) {
    validateCost(age.advance_cost, age.index, 'Age', age.id);
  }

  // Validate Weapons
  for (const w of weapons) {
    if (w.damage < 0) errors.push(`Weapon ${w.id} damage < 0`);
    if (w.range_min < 0 || w.range_max < w.range_min) {
      errors.push(`Weapon ${w.id} invalid range: [${w.range_min}, ${w.range_max}]`);
    }
  }

  // Validate Units
  for (const u of units) {
    validateCost(u.cost, u.age_index, 'Unit', u.id);

    // Cross-ref: trained_at
    if (!u.trained_at || !buildingMap.has(u.trained_at)) {
      errors.push(`Unit ${u.id} trained_at "${u.trained_at}" does not exist in buildings`);
    } else {
      const b = buildingMap.get(u.trained_at);
      if (!b.trains || !b.trains.includes(u.id)) {
        errors.push(`Unit ${u.id} trained_at "${u.trained_at}", but building trains list does not include ${u.id}`);
      }
    }

    // Cross-ref: upgrades_to
    if (u.upgrades_to !== null && u.upgrades_to !== undefined) {
      if (!unitMap.has(u.upgrades_to)) {
        errors.push(`Unit ${u.id} upgrades_to unknown unit "${u.upgrades_to}"`);
      } else {
        const nextUnit = unitMap.get(u.upgrades_to);
        if (nextUnit.age_index <= u.age_index) {
          errors.push(`Unit ${u.id} (age ${u.age_index}) upgrades_to ${nextUnit.id} with age <= current (${nextUnit.age_index})`);
        }
        if (nextUnit.line_id !== u.line_id) {
          errors.push(`Unit ${u.id} line_id (${u.line_id}) != upgrade ${nextUnit.id} line_id (${nextUnit.line_id})`);
        }
      }
    }

    // Cross-ref: weapons
    if (u.weapon_primary && !weaponMap.has(u.weapon_primary)) {
      errors.push(`Unit ${u.id} weapon_primary "${u.weapon_primary}" does not exist`);
    }
    if (u.weapon_secondary && !weaponMap.has(u.weapon_secondary)) {
      errors.push(`Unit ${u.id} weapon_secondary "${u.weapon_secondary}" does not exist`);
    }

    // Cross-ref: sensors
    if (u.sensor_primary && !sensorMap.has(u.sensor_primary)) {
      errors.push(`Unit ${u.id} sensor_primary "${u.sensor_primary}" does not exist`);
    }
    if (u.sensor_secondary && !sensorMap.has(u.sensor_secondary)) {
      errors.push(`Unit ${u.id} sensor_secondary "${u.sensor_secondary}" does not exist`);
    }
  }

  // Validate Buildings
  for (const b of buildings) {
    validateCost(b.cost, b.age_index, 'Building', b.id);

    if (b.trains) {
      if (b.trains.length > 8) {
        errors.push(`Building ${b.id} trains array length (${b.trains.length}) exceeds maximum 8`);
      }
      for (const t of b.trains) {
        if (!unitMap.has(t)) {
          errors.push(`Building ${b.id} trains unknown unit "${t}"`);
        }
      }
    }

    if (b.gather_resource !== null && b.gather_resource !== undefined) {
      if (!resourceMap.has(b.gather_resource)) {
        errors.push(`Building ${b.id} references unknown gather_resource "${b.gather_resource}"`);
      }
    }

    if (b.weapon_primary && !weaponMap.has(b.weapon_primary)) {
      errors.push(`Building ${b.id} weapon_primary "${b.weapon_primary}" does not exist`);
    }

    if (b.sensor_primary && !sensorMap.has(b.sensor_primary)) {
      errors.push(`Building ${b.id} sensor_primary "${b.sensor_primary}" does not exist`);
    }
  }

  // Validate Techs
  for (const t of techs) {
    validateCost(t.cost, t.age_required, 'Tech', t.id);

    if (t.prereq_tech !== null && t.prereq_tech !== undefined) {
      if (!techMap.has(t.prereq_tech)) {
        errors.push(`Tech ${t.id} references unknown prereq_tech "${t.prereq_tech}"`);
      } else {
        const prereq = techMap.get(t.prereq_tech);
        if (prereq.tier >= t.tier) {
          errors.push(`Tech ${t.id} (tier ${t.tier}) has prereq ${prereq.id} with tier >= current (${prereq.tier})`);
        }
        if (prereq.age_required > t.age_required) {
          errors.push(`Tech ${t.id} (age ${t.age_required}) has prereq ${prereq.id} with age_required > current (${prereq.age_required})`);
        }
      }
    }
  }

  // Check fx32 bounds helper
  function checkFx32(val, name, id) {
    if (val !== undefined && val !== null) {
      if (val < -32767.9999 || val > 32767.9999) {
        errors.push(`${name} ${id} fx32 value ${val} out of bounds [-32767.9999, 32767.9999]`);
      }
    }
  }

  return {
    valid: errors.length === 0,
    errors
  };
}
