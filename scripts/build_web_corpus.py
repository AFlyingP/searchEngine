import json
import os
import re

INPUT_JSONL = "wikipedia.jsonl"
OUTPUT_JSON = "web/wiki_corpus.json"

TARGET_KEYWORDS = [
    "computer", "software", "hardware", "algorithm", "operating system", "linux", "windows",
    "programming", "c++", "python", "quantum", "physics", "relativity", "einstein", "newton",
    "mathematics", "calculus", "geometry", "turing", "machine learning", "neural network",
    "artificial intelligence", "database", "index", "retrieval", "compiler", "memory",
    "internet", "web", "server", "network", "cryptography", "encryption", "rsa", "security",
    "biology", "evolution", "dna", "cell", "chemistry", "atom", "molecule", "energy",
    "space", "galaxy", "planet", "astronomy", "telescope", "nasa", "earth", "sun", "moon",
    "history", "philosophy", "logic", "language", "linguistics", "wikipedia", "encyclopedia"
]

def build_web_corpus(max_docs=1500):
    if not os.path.exists(INPUT_JSONL):
        print(f"Error: {INPUT_JSONL} not found.")
        return

    print(f"Extracting top {max_docs} high-value Wikipedia articles from {INPUT_JSONL}...")
    selected_docs = []
    seen_titles = set()

    with open(INPUT_JSONL, "r", encoding="utf-8") as f:
        for line in f:
            if not line.strip():
                continue
            doc = json.loads(line)
            title = doc.get("title", "")
            text = doc.get("text", "")

            if title in seen_titles or len(text) < 150:
                continue

            # Prioritize rich substantive articles on scientific / technical / general knowledge
            lower_comb = (title + " " + text[:500]).lower()
            is_relevant = any(kw in lower_comb for kw in TARGET_KEYWORDS)

            if is_relevant or len(selected_docs) < 500:
                seen_titles.add(title)
                # Keep concise summary snippet + body for client
                selected_docs.append({
                    "id": len(selected_docs) + 1,
                    "title": title,
                    "text": text[:1200]  # First 1200 chars for fast loading and crisp snippets
                })

            if len(selected_docs) >= max_docs:
                break

    os.makedirs(os.path.dirname(OUTPUT_JSON), exist_ok=True)
    with open(OUTPUT_JSON, "w", encoding="utf-8") as out:
        json.dump(selected_docs, out, ensure_ascii=False)

    size_kb = os.path.getsize(OUTPUT_JSON) / 1024
    print(f"Created {OUTPUT_JSON}: {len(selected_docs)} articles ({size_kb:.1f} KB).")

if __name__ == "__main__":
    build_web_corpus()
