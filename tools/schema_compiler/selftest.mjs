import { fnv1a64, xxh64 } from './hashes.mjs';
import { validateAll } from './validate.mjs';
import { loadAllData } from './compile_db.mjs';

function assert(condition, message) {
  if (!condition) {
    console.error(`FAIL: ${message}`);
    process.exit(1);
  }
}

console.log('[selftest] 1. Verifying Hash Golden Vectors...');
assert(xxh64('') === 0xef46db3751d8e999n, 'xxh64("") golden vector failed');
assert(xxh64('', 2654435761n) === 0xac75fda2929b17efn, 'xxh64("", seed) golden vector failed');
assert(fnv1a64('') === 0xcbf29ce484222325n, 'fnv1a64("") golden vector failed');
assert(fnv1a64('citizen') === 0xf6d4efbae9ff23cbn, 'fnv1a64("citizen") deterministic hash failed');

console.log('[selftest] 2. Verifying Valid Baseline Data...');
const baseData = loadAllData();
const baseVal = validateAll(baseData);
assert(baseVal.valid, `Base data should be valid, but got: ${baseVal.errors.join('; ')}`);

console.log('[selftest] 3. Negative Fixtures...');

// Negative 1: Duplicate ID
{
  const testData = JSON.parse(JSON.stringify(baseData));
  testData.units.push({ ...testData.units[0] });
  const val = validateAll(testData);
  assert(!val.valid, 'Duplicate ID should fail validation');
}

// Negative 2: Dangling weapon reference
{
  const testData = JSON.parse(JSON.stringify(baseData));
  testData.units[0].weapon_primary = 'non_existent_weapon';
  const val = validateAll(testData);
  assert(!val.valid, 'Dangling weapon reference should fail validation');
}

// Negative 3: Bad regex ID
{
  const testData = JSON.parse(JSON.stringify(baseData));
  testData.weapons[0].id = 'Bad-Regex-ID!';
  const val = validateAll(testData);
  assert(!val.valid, 'Bad regex ID should fail validation');
}

// Negative 4: Era violation (unit in Age 0 costing silicon)
{
  const testData = JSON.parse(JSON.stringify(baseData));
  testData.units[0].cost.silicon = 50;
  const val = validateAll(testData);
  assert(!val.valid, 'Era violation (age 0 costing silicon) should fail validation');
}

// Negative 5: Upgrade lower age
{
  const testData = JSON.parse(JSON.stringify(baseData));
  // Find a unit that upgrades to another, swap ages
  const u = testData.units.find(x => x.upgrades_to);
  const nextU = testData.units.find(x => x.id === u.upgrades_to);
  nextU.age_index = u.age_index; // invalid: must be >
  const val = validateAll(testData);
  assert(!val.valid, 'Upgrade to same or lower age should fail validation');
}

// Negative 6: Line ID mismatch on upgrade
{
  const testData = JSON.parse(JSON.stringify(baseData));
  const u = testData.units.find(x => x.upgrades_to);
  const nextU = testData.units.find(x => x.id === u.upgrades_to);
  nextU.line_id = u.line_id + 99;
  const val = validateAll(testData);
  assert(!val.valid, 'Line ID mismatch on upgrade should fail validation');
}

// Negative 7: Building trains exceeding 8
{
  const testData = JSON.parse(JSON.stringify(baseData));
  const b = testData.buildings.find(x => x.trains && x.trains.length > 0);
  b.trains.push('citizen');
  while (b.trains.length <= 8) b.trains.push('citizen');
  b.trains.push('citizen'); // 9 or more
  const val = validateAll(testData);
  assert(!val.valid, 'Building trains > 8 should fail validation');
}

// Negative 8: Unknown resource in cost
{
  const testData = JSON.parse(JSON.stringify(baseData));
  testData.units[0].cost.unobtanium = 100;
  const val = validateAll(testData);
  assert(!val.valid, 'Unknown resource in cost should fail validation');
}

// Negative 9: Unit trained_at building mismatch
{
  const testData = JSON.parse(JSON.stringify(baseData));
  testData.units[0].trained_at = 'farm'; // farm doesn't train citizen
  const val = validateAll(testData);
  assert(!val.valid, 'Unit trained_at not matching building trains should fail validation');
}

console.log('[selftest] All 9 selftests passed successfully!');
process.exit(0);
