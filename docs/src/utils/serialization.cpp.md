# serialization.cpp

Path: `src/utils/serialization.cpp`

Purpose: Serialization and deserialization helpers for entities and change events.

Public functions / symbols:
- `for (int i = 0; i < 8; ++i) {`
- `for (int i = 0; i < 4; ++i) {`
- `writeTag(TypeTag::NULL_VALUE);`
- `writeTag(value ? TypeTag::BOOL_TRUE : TypeTag::BOOL_FALSE);`
- `writeTag(TypeTag::INT32);`
- `writeTag(TypeTag::INT64);`
- `writeTag(TypeTag::UINT32);`
- `writeUInt32(value);`
- `writeTag(TypeTag::UINT64);`
- `writeUInt64(value);`
- `writeTag(TypeTag::FLOAT);`
- `writeUInt32(bits);`
- `writeTag(TypeTag::DOUBLE);`
- `writeUInt64(bits);`
- `writeTag(TypeTag::STRING);`
- `writeTag(TypeTag::BINARY);`
- `writeTag(TypeTag::VECTOR_FLOAT);`
- `writeTag(TypeTag::ARRAY);`
- `writeTag(TypeTag::OBJECT);`
- `readTag(); // Skip type tag`
- `readTag();`
- `return readUInt32();`
- `return readUInt64();`
- ``
- `Serialization::Encoder::Encoder() {`

