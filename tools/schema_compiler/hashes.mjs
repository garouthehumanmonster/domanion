// Zero-dependency canonical XXH64 and FNV-1a-64 in JS using BigInt

const PRIME64_1 = 0x9E3779B185EBCA87n;
const PRIME64_2 = 0xC2B2AE3D27D4EB4Fn;
const PRIME64_3 = 0x165667B19E3779F9n;
const PRIME64_4 = 0x85EBCA77C2B2AE63n;
const PRIME64_5 = 0x27D4EB2F165667C5n;
const MASK64 = 0xFFFFFFFFFFFFFFFFn;

function rotl64(x, r) {
  const b = BigInt(r);
  return ((x << b) | (x >> (64n - b))) & MASK64;
}

function round64(acc, input) {
  acc = (acc + ((input * PRIME64_2) & MASK64)) & MASK64;
  acc = rotl64(acc, 31);
  acc = (acc * PRIME64_1) & MASK64;
  return acc;
}

function mergeRound64(acc, val) {
  val = round64(0n, val);
  acc = (acc ^ val) & MASK64;
  acc = ((acc * PRIME64_1) + PRIME64_4) & MASK64;
  return acc;
}

export function fnv1a64(input) {
  let buf;
  if (typeof input === 'string') {
    buf = Buffer.from(input, 'utf8');
  } else if (Buffer.isBuffer(input) || input instanceof Uint8Array) {
    buf = Buffer.from(input);
  } else {
    throw new TypeError('input must be string or Buffer/Uint8Array');
  }

  let hash = 0xCBF29CE484222325n;
  const prime = 0x100000001B3n;
  for (let i = 0; i < buf.length; ++i) {
    hash = (hash ^ BigInt(buf[i])) & MASK64;
    hash = (hash * prime) & MASK64;
  }
  return hash;
}

export function xxh64(input, seed = 0n) {
  let buf;
  if (typeof input === 'string') {
    buf = Buffer.from(input, 'utf8');
  } else if (Buffer.isBuffer(input) || input instanceof Uint8Array) {
    buf = Buffer.from(input);
  } else {
    throw new TypeError('input must be string or Buffer/Uint8Array');
  }

  seed = BigInt(seed) & MASK64;
  const totalLen = BigInt(buf.length);
  let p = 0;
  let remaining = buf.length;
  let h64 = 0n;

  if (remaining >= 32) {
    let v1 = (seed + PRIME64_1 + PRIME64_2) & MASK64;
    let v2 = (seed + PRIME64_2) & MASK64;
    let v3 = seed;
    let v4 = (seed - PRIME64_1) & MASK64;

    while (remaining >= 32) {
      v1 = round64(v1, buf.readBigUInt64LE(p)); p += 8;
      v2 = round64(v2, buf.readBigUInt64LE(p)); p += 8;
      v3 = round64(v3, buf.readBigUInt64LE(p)); p += 8;
      v4 = round64(v4, buf.readBigUInt64LE(p)); p += 8;
      remaining -= 32;
    }

    h64 = (rotl64(v1, 1) + rotl64(v2, 7) + rotl64(v3, 12) + rotl64(v4, 18)) & MASK64;
    h64 = mergeRound64(h64, v1);
    h64 = mergeRound64(h64, v2);
    h64 = mergeRound64(h64, v3);
    h64 = mergeRound64(h64, v4);
  } else {
    h64 = (seed + PRIME64_5) & MASK64;
  }

  h64 = (h64 + totalLen) & MASK64;

  while (remaining >= 8) {
    const k1 = round64(0n, buf.readBigUInt64LE(p));
    h64 = (h64 ^ k1) & MASK64;
    h64 = ((rotl64(h64, 27) * PRIME64_1) + PRIME64_4) & MASK64;
    p += 8;
    remaining -= 8;
  }

  if (remaining >= 4) {
    const k1 = (BigInt(buf.readUInt32LE(p)) * PRIME64_1) & MASK64;
    h64 = (h64 ^ k1) & MASK64;
    h64 = ((rotl64(h64, 23) * PRIME64_2) + PRIME64_3) & MASK64;
    p += 4;
    remaining -= 4;
  }

  while (remaining > 0) {
    const k1 = (BigInt(buf[p]) * PRIME64_5) & MASK64;
    h64 = (h64 ^ k1) & MASK64;
    h64 = (rotl64(h64, 11) * PRIME64_1) & MASK64;
    p += 1;
    remaining -= 1;
  }

  h64 = (h64 ^ (h64 >> 33n)) & MASK64;
  h64 = (h64 * PRIME64_2) & MASK64;
  h64 = (h64 ^ (h64 >> 29n)) & MASK64;
  h64 = (h64 * PRIME64_3) & MASK64;
  h64 = (h64 ^ (h64 >> 32n)) & MASK64;

  return h64;
}
