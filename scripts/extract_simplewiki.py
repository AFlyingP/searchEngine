import bz2
import json
import os
import re
import sys
import urllib.request
import xml.etree.ElementTree as ET

WIKI_DUMP_URL = "https://dumps.wikimedia.org/simplewiki/latest/simplewiki-latest-pages-articles.xml.bz2"
BZ2_FILENAME = "simplewiki-latest-pages-articles.xml.bz2"
OUTPUT_JSONL = "wikipedia.jsonl"

def download_dump():
    if os.path.exists(BZ2_FILENAME) and os.path.getsize(BZ2_FILENAME) > 10_000_000:
        print(f"Found existing {BZ2_FILENAME} ({os.path.getsize(BZ2_FILENAME) / (1024*1024):.2f} MB), skipping download.")
        return

    print(f"Downloading Simple English Wikipedia dump from {WIKI_DUMP_URL}...")
    headers = {"User-Agent": "NeedlefishSearch/1.0 (https://github.com/AFlyingP/searchEngine)"}
    req = urllib.request.Request(WIKI_DUMP_URL, headers=headers)
    
    with urllib.request.urlopen(req) as resp, open(BZ2_FILENAME, "wb") as out:
        total = int(resp.headers.get("content-length", 0))
        downloaded = 0
        block_size = 1024 * 1024  # 1 MB
        
        while True:
            chunk = resp.read(block_size)
            if not chunk:
                break
            out.write(chunk)
            downloaded += len(chunk)
            if total > 0:
                percent = (downloaded / total) * 100
                print(f"\rDownloading: {downloaded / (1024*1024):.1f} MB / {total / (1024*1024):.1f} MB ({percent:.1f}%)", end="", flush=True)
            else:
                print(f"\rDownloading: {downloaded / (1024*1024):.1f} MB", end="", flush=True)
        print("\nDownload complete.")

def clean_wikitext(text: str) -> str:
    if not text:
        return ""
    # Remove HTML tags & comments
    text = re.sub(r"<!--.*?-->", "", text, flags=re.DOTALL)
    text = re.sub(r"<ref[^>]*>.*?</ref>", "", text, flags=re.DOTALL | re.IGNORECASE)
    text = re.sub(r"<ref[^>]*/>", "", text, flags=re.IGNORECASE)
    text = re.sub(r"<[^>]+>", "", text)
    
    # Remove nested templates {{ ... }}
    text = re.sub(r"\{\{[^{}]*\}\}", "", text)
    text = re.sub(r"\{\{[^{}]*\}\}", "", text)
    
    # Remove tables {| ... |}
    text = re.sub(r"\{\|.*?\|\}", "", text, flags=re.DOTALL)
    
    # Convert internal links [[Target|Anchor]] -> Anchor, [[Target]] -> Target
    def link_repl(match):
        inner = match.group(1)
        if ":" in inner:
            return ""
        parts = inner.split("|")
        return parts[-1]
    
    text = re.sub(r"\[\[(.*?)\]\]", link_repl, text)
    
    # Convert external links [http://... Anchor] -> Anchor
    text = re.sub(r"\[https?://[^\s\]]+\s+([^\]]+)\]", r"\1", text)
    text = re.sub(r"\[https?://[^\s\]]+\]", "", text)
    
    # Remove bold / italic markup
    text = re.sub(r"'{2,5}", "", text)
    
    # Remove headers == ... ==
    text = re.sub(r"={2,}\s*(.*?)\s*={2,}", r"\1", text)
    
    # Normalize whitespace
    lines = [line.strip() for line in text.splitlines() if line.strip()]
    return " ".join(lines)

def process_dump(max_articles=None):
    print(f"Streaming and parsing {BZ2_FILENAME} -> {OUTPUT_JSONL}...")
    doc_id = 1
    skipped = 0
    
    with bz2.open(BZ2_FILENAME, "rt", encoding="utf-8", errors="replace") as xml_file, \
         open(OUTPUT_JSONL, "w", encoding="utf-8") as out_file:
        
        context = ET.iterparse(xml_file, events=("end",))
        
        for event, elem in context:
            tag = elem.tag.split("}")[-1] if "}" in elem.tag else elem.tag
            
            if tag == "page":
                title_elem = elem.find("{*}title") if "{" in elem.tag else elem.find("title")
                ns_elem = elem.find("{*}ns") if "{" in elem.tag else elem.find("ns")
                redirect_elem = elem.find("{*}redirect") if "{" in elem.tag else elem.find("redirect")
                revision_elem = elem.find("{*}revision") if "{" in elem.tag else elem.find("revision")
                
                title = title_elem.text if title_elem is not None and title_elem.text else ""
                ns = ns_elem.text if ns_elem is not None and ns_elem.text else "0"
                
                if ns == "0" and redirect_elem is None and not title.startswith(("Category:", "Template:", "File:", "Wikipedia:")):
                    text_elem = None
                    if revision_elem is not None:
                        text_elem = revision_elem.find("{*}text") if "{" in revision_elem.tag else revision_elem.find("text")
                    
                    raw_text = text_elem.text if text_elem is not None and text_elem.text else ""
                    cleaned = clean_wikitext(raw_text)
                    
                    if len(cleaned) >= 80:
                        record = {"id": doc_id, "title": title, "text": cleaned}
                        out_file.write(json.dumps(record, ensure_ascii=False) + "\n")
                        doc_id += 1
                        
                        if doc_id % 10000 == 0:
                            print(f"Indexed {doc_id:,} articles so far...")
                        
                        if max_articles and doc_id > max_articles:
                            elem.clear()
                            break
                    else:
                        skipped += 1
                else:
                    skipped += 1
                
                elem.clear()

    total_articles = doc_id - 1
    file_size_mb = os.path.getsize(OUTPUT_JSONL) / (1024 * 1024)
    print(f"\nExtraction finished successfully!")
    print(f"Total articles extracted: {total_articles:,}")
    print(f"Skipped non-articles / stubs: {skipped:,}")
    print(f"Output file size: {file_size_mb:.2f} MB ({OUTPUT_JSONL})")

if __name__ == "__main__":
    download_dump()
    process_dump()
