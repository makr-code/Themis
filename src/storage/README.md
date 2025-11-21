# Storage Module

RocksDB wrapper and storage layer abstractions for ThemisDB.

## Components

- RocksDB wrapper
- Base entity storage
- Key schema management
- Storage abstractions

## Features

- LSM-tree based storage (RocksDB)
- Entity serialization/deserialization
- Key encoding and schema management
- Column families for different data types
- Snapshot isolation

## Documentation

For storage documentation, see:
- [Base Entity](../../docs/src/storage/base_entity.cpp.md)
- [Key Schema](../../docs/src/storage/key_schema.cpp.md)
- [RocksDB Wrapper](../../docs/src/storage/rocksdb_wrapper.cpp.md)
- [RocksDB Layout](../../docs/storage/rocksdb_layout.md)
- [Cloud Blob Backends](../../docs/storage/CLOUD_BLOB_BACKENDS.md)
- [Base Entity Documentation](../../docs/base_entity.md)
