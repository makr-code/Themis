# ThemisDB Python SDK

Noch experimentell.

## Installation

```bash
pip install -e .
```

## Beispiel

```python
from themis import ThemisClient

client = ThemisClient(["http://127.0.0.1:8765"], namespace="default")
print(client.health())
```
