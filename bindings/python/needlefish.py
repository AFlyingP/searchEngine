"""
Needlefish Python Client
Zero-copy ctypes binding to the compiled Needlefish C search engine library.
"""

import ctypes
import os
import sys
import threading
from typing import List, Dict, Any, Optional

class _NeedlefishHit(ctypes.Structure):
    _fields_ = [
        ("doc_id", ctypes.c_uint32),
        ("score", ctypes.c_float),
        ("title", ctypes.c_char_p),
        ("snippet", ctypes.c_char_p),
    ]

class _NeedlefishSearchResult(ctypes.Structure):
    _fields_ = [
        ("hits", ctypes.POINTER(_NeedlefishHit)),
        ("num_hits", ctypes.c_size_t),
        ("took_us", ctypes.c_uint64),
    ]

class _NeedlefishSuggestResult(ctypes.Structure):
    _fields_ = [
        ("suggestions", ctypes.POINTER(ctypes.c_char_p)),
        ("num_suggestions", ctypes.c_size_t),
    ]

class NeedlefishIndex:
    def __init__(self, index_path: str, lib_path: Optional[str] = None):
        self._lock = threading.RLock()
        self._handle = None
        self._lib = None

        if lib_path is None:
            # Auto-detect library in build directory
            candidates = [
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "libneedlefish_c.dll"),
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "libneedlefish_c.so"),
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "libneedlefish_c.dylib"),
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "debug", "libneedlefish_c.dll"),
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "debug", "libneedlefish_c.so"),
                os.path.join(os.path.dirname(__file__), "..", "..", "build", "debug", "libneedlefish_c.dylib"),
                "needlefish_c.dll", "libneedlefish_c.so", "libneedlefish_c.dylib"
            ]
            for c in candidates:
                if os.path.exists(c):
                    lib_path = c
                    break
        
        if not lib_path or not os.path.exists(lib_path):
            raise RuntimeError(f"Could not locate Needlefish C library at: {lib_path}")

        if os.name == "nt":
            # Add compiler toolchain bin directory to DLL search path
            for p in ["C:\\msys64\\ucrt64\\bin", "C:\\msys64\\mingw64\\bin", os.path.dirname(os.path.abspath(lib_path))]:
                if os.path.isdir(p):
                    try:
                        os.add_dll_directory(p)
                    except Exception:
                        pass

        self._lib = ctypes.CDLL(os.path.abspath(lib_path))
        self._setup_c_types()
        
        self._handle = self._lib.needlefish_open(index_path.encode("utf-8"))
        if not self._handle:
            raise RuntimeError(f"Failed to open Needlefish index: {index_path}")

    def _setup_c_types(self):
        self._lib.needlefish_open.argtypes = [ctypes.c_char_p]
        self._lib.needlefish_open.restype = ctypes.c_void_p

        self._lib.needlefish_close.argtypes = [ctypes.c_void_p]
        self._lib.needlefish_close.restype = None

        self._lib.needlefish_total_docs.argtypes = [ctypes.c_void_p]
        self._lib.needlefish_total_docs.restype = ctypes.c_uint32

        self._lib.needlefish_search.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        self._lib.needlefish_search.restype = ctypes.POINTER(_NeedlefishSearchResult)

        self._lib.needlefish_free_search_result.argtypes = [ctypes.POINTER(_NeedlefishSearchResult)]
        self._lib.needlefish_free_search_result.restype = None

        self._lib.needlefish_suggest.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_size_t]
        self._lib.needlefish_suggest.restype = ctypes.POINTER(_NeedlefishSuggestResult)

        self._lib.needlefish_free_suggest_result.argtypes = [ctypes.POINTER(_NeedlefishSuggestResult)]
        self._lib.needlefish_free_suggest_result.restype = None

    def search(self, query: str, top_k: int = 10) -> Dict[str, Any]:
        with self._lock:
            if not self._handle or not self._lib:
                raise RuntimeError("NeedlefishIndex is closed")
            res_ptr = self._lib.needlefish_search(self._handle, query.encode("utf-8"), top_k)
            if not res_ptr:
                return {"hits": [], "total_estimate": 0, "took_us": 0}
            
            try:
                res = res_ptr.contents
                hits = []
                for i in range(res.num_hits):
                    hit = res.hits[i]
                    hits.append({
                        "doc_id": hit.doc_id,
                        "score": hit.score,
                        "title": hit.title.decode("utf-8", errors="replace") if hit.title else "",
                        "snippet": hit.snippet.decode("utf-8", errors="replace") if hit.snippet else ""
                    })
                return {"hits": hits, "total_estimate": res.num_hits, "took_us": res.took_us}
            finally:
                self._lib.needlefish_free_search_result(res_ptr)

    def suggest(self, prefix: str, max_results: int = 10) -> List[str]:
        with self._lock:
            if not self._handle or not self._lib:
                raise RuntimeError("NeedlefishIndex is closed")
            res_ptr = self._lib.needlefish_suggest(self._handle, prefix.encode("utf-8"), max_results)
            if not res_ptr:
                return []
            try:
                res = res_ptr.contents
                return [res.suggestions[i].decode("utf-8", errors="replace") for i in range(res.num_suggestions)]
            finally:
                self._lib.needlefish_free_suggest_result(res_ptr)

    def total_docs(self) -> int:
        with self._lock:
            if not self._handle or not self._lib:
                return 0
            return self._lib.needlefish_total_docs(self._handle)

    def close(self):
        with self._lock:
            if getattr(self, "_handle", None) and self._lib:
                self._lib.needlefish_close(self._handle)
                self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        self.close()


if __name__ == "__main__":
    if len(sys.argv) < 2:
        print("Usage: python needlefish.py <path_to_index.idx> [query]")
        sys.exit(1)
    
    idx_path = sys.argv[1]
    query = sys.argv[2] if len(sys.argv) > 2 else "quantum"
    
    print(f"Loading Needlefish index '{idx_path}'...")
    engine = NeedlefishIndex(idx_path)
    print(f"Index loaded. Searching for '{query}'...")
    results = engine.search(query, top_k=5)
    print(f"Found {len(results['hits'])} hits in {results['took_us']} us:")
    for h in results["hits"]:
        print(f"  [{h['doc_id']}] (Score: {h['score']:.2f}) {h['title']}")
        print(f"      {h['snippet']}")
