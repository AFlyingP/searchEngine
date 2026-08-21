# Needlefish API Documentation

Needlefish provides multiple programmatic and network interfaces:
1. **C++ API** (header-only / linked core library)
2. **C ABI Shared Library** (`needlefish_c.h`)
3. **Python Client** (`bindings/python/needlefish.py`)
4. **HTTP REST API** (`/api/search`, `/api/suggest`, `/api/health`, `/api/stats`)

---

## 1. HTTP REST API

The HTTP server runs on port 8080 by default (`needlefish serve --index corpus.idx --port 8080`).

### `GET /api/search`
Execute BM25, phrase, regex, or fuzzy queries.
- **Parameters**:
  - `q` (string): Query string (max 256 characters).
  - `limit` (integer, optional): Maximum hits to return (default: 10).
  - `mode` (string, optional): `"standard"`, `"phrase"`, `"fuzzy"`, `"regex"`, `"substring"`, `"auto"`.
- **Response**:
```json
{
  "query": "quantum computing",
  "mode": "standard",
  "took_us": 142,
  "took_ms": 0.142,
  "total_hits": 5,
  "did_you_mean": "",
  "hits": [
    {
      "rank": 1,
      "doc_id": 42,
      "score": 8.4123,
      "title": "Quantum Computing",
      "snippet": "Introduction to <em>quantum</em> <em>computing</em> algorithms..."
    }
  ]
}
```

### `GET /api/suggest`
Retrieve prefix or fuzzy autocomplete term suggestions.
- **Parameters**:
  - `q` (string): Prefix string.
  - `limit` (integer, optional): Maximum suggestions (default: 10).
  - `fuzzy` (boolean, optional): Set `true` for fuzzy matching.

### `GET /api/health`
Health check and metadata endpoint.
- **Response**:
```json
{
  "status": "ok",
  "version": "1.0.0",
  "total_docs": 100000,
  "index_checksum": "0x89abcdef",
  "uptime_seconds": 120
}
```

---

## 2. Python Client (`bindings/python/needlefish.py`)

```python
from needlefish import NeedlefishIndex

with NeedlefishIndex("corpus.idx") as index:
    print(f"Total documents: {index.total_docs()}")
    
    # Standard BM25 search
    results = index.search("information retrieval", top_k=10)
    for hit in results["hits"]:
        print(f"[{hit['doc_id']}] {hit['title']} (Score: {hit['score']:.2f})")
        print(f"    {hit['snippet']}")
        
    # Prefix autocomplete
    suggestions = index.suggest("algo", max_results=5)
    print("Suggestions:", suggestions)
```

---

## 3. C API Reference (`needlefish_c.h`)

```c
#include "needlefish_c.h"

needlefish_index_t* handle = needlefish_open("corpus.idx");
if (handle) {
    needlefish_search_result_t* res = needlefish_search(handle, "neural networks", 10);
    if (res) {
        for (size_t i = 0; i < res->num_hits; ++i) {
            printf("[%u] %s (Score: %.2f)\n", res->hits[i].doc_id, res->hits[i].title, res->hits[i].score);
        }
        needlefish_free_search_result(res);
    }
    needlefish_close(handle);
}
```
