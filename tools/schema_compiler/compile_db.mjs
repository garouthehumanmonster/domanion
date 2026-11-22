import fs from 'fs';
import path from 'path';
import { fileURLToPath } from 'url';
import { validateAll } from './validate.mjs';
import { packDb } from './pack.mjs';
import { generateCppHeader } from './codegen_cpp.mjs';

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);
const rootDir = path.resolve(__dirname, '../..');

function loadJson(relPath) {
  const fullPath = path.join(rootDir, relPath);
  return JSON.parse(fs.readFileSync(fullPath, 'utf8'));
}

export function loadAllData() {
  return {
    enums: loadJson('data/enums.json'),
    ages: loadJson('data/ages.json'),
    resources: loadJson('data/resources.json'),
    sensors: loadJson('data/sensors.json'),
    weapons: loadJson('data/weapons.json'),
    units: loadJson('data/units.json'),
    buildings: loadJson('data/buildings.json'),
    techs: loadJson('data/techs.json')
  };
}

export function compile() {
  const checkOnly = process.argv.includes('--check');

  const data = loadAllData();
  const valResult = validateAll(data);
  if (!valResult.valid) {
    console.error('Validation failed:');
    for (const err of valResult.errors) {
      console.error(`  - ${err}`);
    }
    process.exit(1);
  }

  const { buffer, layoutHash, contentHash, totalFileSize } = packDb(data);
  const cppHeader = generateCppHeader(data);

  const binPath = path.join(rootDir, 'assets/compiled/game.dbbin');
  const hppPath = path.join(rootDir, 'src/sim/generated_db_types.hpp');

  if (checkOnly) {
    if (!fs.existsSync(binPath)) {
      console.error(`--check failed: ${binPath} does not exist`);
      process.exit(1);
    }
    if (!fs.existsSync(hppPath)) {
      console.error(`--check failed: ${hppPath} does not exist`);
      process.exit(1);
    }

    const existingBin = fs.readFileSync(binPath);
    if (!existingBin.equals(buffer)) {
      console.error(`--check failed: ${binPath} content mismatch`);
      process.exit(1);
    }

    const existingHpp = fs.readFileSync(hppPath, 'utf8');
    if (existingHpp !== cppHeader) {
      console.error(`--check failed: ${hppPath} content mismatch`);
      process.exit(1);
    }

    console.log('[compile_db] --check verified OK. Binary and header match generated content.');
    return;
  }

  fs.mkdirSync(path.dirname(binPath), { recursive: true });
  fs.writeFileSync(binPath, buffer);

  fs.mkdirSync(path.dirname(hppPath), { recursive: true });
  fs.writeFileSync(hppPath, cppHeader, 'utf8');

  console.log('[compile_db] Successfully compiled game.dbbin and generated_db_types.hpp');
  console.log(`  File size:    ${totalFileSize} bytes`);
  console.log(`  Layout hash:  0x${layoutHash.toString(16).padStart(16, '0')}`);
  console.log(`  Content hash: 0x${contentHash.toString(16).padStart(16, '0')}`);
  console.log(`  Records:      ${data.ages.length} ages, ${data.resources.length} resources, ${data.sensors.length} sensors, ` +
              `${data.weapons.length} weapons, ${data.units.length} units, ${data.buildings.length} buildings, ${data.techs.length} techs`);
}

if (process.argv[1] && process.argv[1].replace(/\\/g, '/').endsWith('compile_db.mjs')) {
  compile();
}
