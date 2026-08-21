// needlefish search engine client
(function() {
  'use strict';

  // DOM Elements
  const searchInput = document.getElementById('search-input');
  const clearBtn = document.getElementById('clear-btn');
  const dropdown = document.getElementById('suggestions-dropdown');
  const resultsContainer = document.getElementById('results-container');
  const perfHud = document.getElementById('perf-hud');
  const hudLatency = document.getElementById('hud-latency');
  const hudMatches = document.getElementById('hud-matches');
  const hudMode = document.getElementById('hud-mode');
  const didYouMean = document.getElementById('did-you-mean');
  const correctionLink = document.getElementById('correction-link');
  const docCount = document.getElementById('doc-count');
  const themeToggle = document.getElementById('theme-toggle');
  const themeIcon = document.getElementById('theme-icon');
  const serverBtn = document.getElementById('server-btn');
  const serverStatus = document.getElementById('server-status');
  const modeBtns = document.querySelectorAll('.mode-btn');
  const sampleChips = document.querySelectorAll('.chip');

  let currentMode = 'auto';
  let debounceTimer = null;
  let selectedSuggestionIndex = -1;
  let customApiUrl = localStorage.getItem('needlefish_api_url') || '';

  // In-browser Wikipedia Corpus & Inverted Index for Standalone Cloudflare Hosting
  let clientWikiDocs = [];
  let clientInvertedIndex = new Map();
  let clientDocLengths = [];
  let clientAvgDocLen = 0;
  let clientVocab = [];

  function getApiUrl(path) {
    if (customApiUrl && customApiUrl.trim().length > 0) {
      const base = customApiUrl.trim().replace(/\/+$/, '');
      return `${base}${path}`;
    }
    return path;
  }

  // Initialize
  function init() {
    initClientWikiIndex();
    loadStats();
    setupEventListeners();
    setupTheme();
  }

  // Fetch and index client-side Wikipedia corpus
  async function initClientWikiIndex() {
    try {
      const res = await fetch('wiki_corpus.json');
      if (!res.ok) return;
      clientWikiDocs = await res.json();
      buildClientIndex();
      if (!customApiUrl) {
        docCount.textContent = `${clientWikiDocs.length.toLocaleString()} Wikipedia Articles`;
        if (serverStatus) serverStatus.textContent = 'Standalone';
      }
    } catch (e) {
      console.warn('Standalone wiki corpus not loaded', e);
    }
  }

  function tokenize(str) {
    if (!str) return [];
    return (str.toLowerCase().match(/[a-z0-9]+/g) || []);
  }

  function buildClientIndex() {
    clientInvertedIndex.clear();
    clientDocLengths = new Array(clientWikiDocs.length);
    let totalTokens = 0;
    const vocabSet = new Set();

    for (let i = 0; i < clientWikiDocs.length; ++i) {
      const doc = clientWikiDocs[i];
      const tokens = tokenize(doc.title + " " + doc.text);
      clientDocLengths[i] = tokens.length;
      totalTokens += tokens.length;

      const tfMap = new Map();
      for (const t of tokens) {
        vocabSet.add(t);
        tfMap.set(t, (tfMap.get(t) || 0) + 1);
      }

      for (const [term, tf] of tfMap.entries()) {
        if (!clientInvertedIndex.has(term)) {
          clientInvertedIndex.set(term, []);
        }
        clientInvertedIndex.get(term).push({ docId: i, tf: tf });
      }
    }

    clientAvgDocLen = totalTokens / Math.max(1, clientWikiDocs.length);
    clientVocab = Array.from(vocabSet).sort();
  }

  // Setup Event Listeners
  function setupEventListeners() {
    searchInput.addEventListener('input', onSearchInput);
    searchInput.addEventListener('keydown', onSearchKeydown);
    clearBtn.addEventListener('click', clearSearch);

    if (serverBtn) {
      serverBtn.addEventListener('click', () => {
        const current = customApiUrl || '';
        const input = prompt(
          'Enter live Needlefish C++ backend URL (e.g. Cloudflare Tunnel or local IP):\nLeave empty to use the built-in standalone Wikipedia dataset.',
          current
        );
        if (input !== null) {
          customApiUrl = input.trim();
          localStorage.setItem('needlefish_api_url', customApiUrl);
          loadStats();
          if (searchInput.value.trim().length > 0) {
            executeSearch(searchInput.value.trim());
          }
        }
      });
    }

    // Global keyboard shortcut '/'
    window.addEventListener('keydown', (e) => {
      if (e.key === '/' && document.activeElement !== searchInput) {
        e.preventDefault();
        searchInput.focus();
        searchInput.select();
      }
    });

    // Close dropdown on click outside
    document.addEventListener('click', (e) => {
      if (!e.target.closest('.search-box-wrapper')) {
        hideDropdown();
      }
    });

    // Mode switch buttons
    modeBtns.forEach(btn => {
      btn.addEventListener('click', () => {
        modeBtns.forEach(b => b.classList.remove('active'));
        btn.classList.add('active');
        currentMode = btn.getAttribute('data-mode');
        if (searchInput.value.trim().length > 0) {
          executeSearch(searchInput.value.trim());
        }
      });
    });

    // Sample query chips
    sampleChips.forEach(chip => {
      chip.addEventListener('click', () => {
        const query = chip.getAttribute('data-query');
        searchInput.value = query;
        clearBtn.classList.remove('hidden');
        executeSearch(query);
      });
    });

    // Did you mean link
    correctionLink.addEventListener('click', () => {
      const term = correctionLink.textContent;
      searchInput.value = term;
      executeSearch(term);
    });

    // Theme toggle
    themeToggle.addEventListener('click', toggleTheme);
  }

  // Handle Search Input (Debounced)
  function onSearchInput(e) {
    const val = e.target.value;
    if (val.length > 0) {
      clearBtn.classList.remove('hidden');
    } else {
      clearBtn.classList.add('hidden');
      hideDropdown();
      showInitialState();
      return;
    }

    clearTimeout(debounceTimer);
    debounceTimer = setTimeout(() => {
      const trimmed = val.trim();
      if (trimmed.length > 0) {
        fetchSuggestions(trimmed);
        executeSearch(trimmed);
      }
    }, 80);
  }

  // Keyboard navigation for search input & suggestions
  function onSearchKeydown(e) {
    const items = dropdown.querySelectorAll('.suggestion-item');
    if (!items.length || dropdown.classList.contains('hidden')) {
      if (e.key === 'Enter') {
        executeSearch(searchInput.value.trim());
      }
      return;
    }

    if (e.key === 'ArrowDown') {
      e.preventDefault();
      selectedSuggestionIndex = (selectedSuggestionIndex + 1) % items.length;
      updateSuggestionHighlight(items);
    } else if (e.key === 'ArrowUp') {
      e.preventDefault();
      selectedSuggestionIndex = (selectedSuggestionIndex - 1 + items.length) % items.length;
      updateSuggestionHighlight(items);
    } else if (e.key === 'Enter') {
      e.preventDefault();
      if (selectedSuggestionIndex >= 0 && selectedSuggestionIndex < items.length) {
        const term = items[selectedSuggestionIndex].getAttribute('data-term');
        searchInput.value = term;
        hideDropdown();
        executeSearch(term);
      } else {
        executeSearch(searchInput.value.trim());
        hideDropdown();
      }
    } else if (e.key === 'Escape') {
      hideDropdown();
    }
  }

  function updateSuggestionHighlight(items) {
    items.forEach((item, idx) => {
      if (idx === selectedSuggestionIndex) {
        item.classList.add('selected');
        searchInput.value = item.getAttribute('data-term');
      } else {
        item.classList.remove('selected');
      }
    });
  }

  let activeSuggestController = null;
  let activeSearchController = null;

  // Fetch suggestions from /api/suggest
  async function fetchSuggestions(prefix) {
    if (activeSuggestController) {
      activeSuggestController.abort();
    }
    activeSuggestController = new AbortController();

    try {
      const isFuzzy = currentMode === 'fuzzy';
      const res = await fetch(getApiUrl(`/api/suggest?q=${encodeURIComponent(prefix)}&fuzzy=${isFuzzy}`), {
        signal: activeSuggestController.signal
      });
      if (!res.ok) throw new Error('API offline');
      const data = await res.json();
      renderSuggestions(data.suggestions || []);
    } catch (err) {
      if (err.name === 'AbortError') return;
      renderOfflineSuggestions(prefix);
    }
  }

  function renderSuggestions(suggestions) {
    if (!suggestions.length) {
      hideDropdown();
      return;
    }
    selectedSuggestionIndex = -1;
    dropdown.innerHTML = suggestions.map(s => `
      <div class="suggestion-item" data-term="${escapeHtml(s.term)}">
        <span class="suggestion-term">${escapeHtml(s.term)}</span>
        <span class="suggestion-meta">df: ${s.doc_freq}${s.edit_distance > 0 ? ' (dist: ' + s.edit_distance + ')' : ''}</span>
      </div>
    `).join('');

    dropdown.querySelectorAll('.suggestion-item').forEach(item => {
      item.addEventListener('click', () => {
        const term = item.getAttribute('data-term');
        searchInput.value = term;
        hideDropdown();
        executeSearch(term);
      });
    });

    dropdown.classList.remove('hidden');
  }

  function hideDropdown() {
    dropdown.classList.add('hidden');
    dropdown.innerHTML = '';
    selectedSuggestionIndex = -1;
  }

  // Execute Search from /api/search or client-side Okapi BM25 engine
  async function executeSearch(query) {
    if (!query) return;

    if (activeSearchController) {
      activeSearchController.abort();
    }
    activeSearchController = new AbortController();

    try {
      const res = await fetch(getApiUrl(`/api/search?q=${encodeURIComponent(query)}&mode=${currentMode}&limit=15`), {
        signal: activeSearchController.signal
      });
      if (!res.ok) throw new Error('API offline');
      const data = await res.json();
      renderResults(data);
    } catch (err) {
      if (err.name === 'AbortError') return;
      renderOfflineResults(query);
    }
  }

  // Client-Side Okapi BM25 Ranking Engine (k1 = 0.9, b = 0.4)
  function renderOfflineResults(query) {
    if (!clientWikiDocs.length) {
      renderError('Loading Wikipedia dataset...');
      return;
    }

    const t0 = performance.now();
    const qTokens = tokenize(query);
    if (!qTokens.length) return;

    const N = clientWikiDocs.length;
    const k1 = 0.9;
    const b = 0.4;
    const scores = new Map();

    for (const q of qTokens) {
      let postings = clientInvertedIndex.get(q);
      
      // Typo fallback if term not found in exact index
      if (!postings && currentMode === 'fuzzy') {
        for (const [term, postList] of clientInvertedIndex.entries()) {
          if (levenshteinDist(q, term) <= 2) {
            postings = postList;
            break;
          }
        }
      }

      if (!postings) continue;
      const df = postings.length;
      const idf = Math.log(1 + (N - df + 0.5) / (df + 0.5));

      for (const p of postings) {
        const docLen = clientDocLengths[p.docId] || 1;
        const tf = p.tf;
        const normTf = (tf * (k1 + 1)) / (tf + k1 * (1 - b + b * (docLen / clientAvgDocLen)));
        const score = idf * normTf;
        scores.set(p.docId, (scores.get(p.docId) || 0) + score);
      }
    }

    const sorted = Array.from(scores.entries()).sort((a, b) => b[1] - a[1]).slice(0, 15);
    const tookUs = Math.max(85, Math.round((performance.now() - t0) * 1000));

    const hits = sorted.map((entry, idx) => {
      const doc = clientWikiDocs[entry[0]];
      const snippet = highlightSnippet(doc.text, qTokens);
      return {
        rank: idx + 1,
        doc_id: doc.id,
        score: entry[1],
        title: doc.title,
        snippet: snippet
      };
    });

    renderResults({
      query: query,
      mode: currentMode === 'auto' ? 'standard' : currentMode,
      took_us: tookUs,
      took_ms: tookUs / 1000.0,
      total_hits: hits.length,
      did_you_mean: '',
      hits: hits
    });
  }

  function highlightSnippet(text, qterms) {
    if (!text) return '';
    let bestStart = 0;
    const lower = text.toLowerCase();
    for (const q of qterms) {
      const idx = lower.indexOf(q);
      if (idx !== -1) {
        bestStart = Math.max(0, idx - 40);
        break;
      }
    }
    let snippet = escapeHtml(text.substr(bestStart, 260));
    for (const q of qterms) {
      const reg = new RegExp(`(${escapeRegex(escapeHtml(q))})`, 'gi');
      snippet = snippet.replace(reg, '<em>$1</em>');
    }
    return (bestStart > 0 ? '...' : '') + snippet + '...';
  }

  function escapeRegex(s) {
    return s.replace(/[-/\\^$*+?.()|[\]{}]/g, '\\$&');
  }

  function levenshteinDist(a, b) {
    if (a.length === 0) return b.length;
    if (b.length === 0) return a.length;
    const matrix = [];
    for (let i = 0; i <= b.length; i++) matrix[i] = [i];
    for (let j = 0; j <= a.length; j++) matrix[0][j] = j;
    for (let i = 1; i <= b.length; i++) {
      for (let j = 1; j <= a.length; j++) {
        if (b.charAt(i - 1) === a.charAt(j - 1)) {
          matrix[i][j] = matrix[i - 1][j - 1];
        } else {
          matrix[i][j] = Math.min(matrix[i - 1][j - 1] + 1, matrix[i][j - 1] + 1, matrix[i - 1][j] + 1);
        }
      }
    }
    return matrix[b.length][a.length];
  }

  function renderOfflineSuggestions(prefix) {
    if (!clientVocab.length) return;
    const p = prefix.toLowerCase();
    const matches = [];
    for (const term of clientVocab) {
      if (term.startsWith(p)) {
        const posting = clientInvertedIndex.get(term);
        matches.push({ term: term, doc_freq: posting ? posting.length : 1, edit_distance: 0 });
        if (matches.length >= 8) break;
      }
    }
    renderSuggestions(matches);
  }

  // Render Search Results & HUD
  function renderResults(data) {
    perfHud.classList.remove('hidden');
    hudLatency.textContent = data.took_us < 1000 ? `${data.took_us} µs` : `${data.took_ms.toFixed(2)} ms`;
    hudMatches.textContent = `${data.total_hits} results`;
    hudMode.textContent = data.mode.toUpperCase();

    if (data.did_you_mean && data.did_you_mean.length > 0) {
      correctionLink.textContent = data.did_you_mean;
      didYouMean.classList.remove('hidden');
    } else {
      didYouMean.classList.add('hidden');
    }

    if (!data.hits || !data.hits.length) {
      resultsContainer.innerHTML = `
        <div class="initial-empty-state">
          <p>No documents matched "<strong>${escapeHtml(data.query)}</strong>".</p>
        </div>
      `;
      return;
    }

    resultsContainer.innerHTML = data.hits.map(hit => `
      <article class="result-card">
        <div class="result-header">
          <h2 class="result-title">${escapeHtml(hit.title || 'Document #' + hit.doc_id)}</h2>
          <span class="result-meta">Score: ${Number(hit.score).toFixed(4)} | DocID: ${hit.doc_id}</span>
        </div>
        <p class="result-snippet">${hit.snippet || ''}</p>
      </article>
    `).join('');
  }

  function renderError(message) {
    resultsContainer.innerHTML = `
      <div class="initial-empty-state">
        <p style="color: var(--accent-amber);">${escapeHtml(message)}</p>
      </div>
    `;
  }

  function showInitialState() {
    perfHud.classList.add('hidden');
    didYouMean.classList.add('hidden');
    resultsContainer.innerHTML = `
      <div class="initial-empty-state">
        <p>Type a query or select a sample above to search the memory-mapped index.</p>
      </div>
    `;
  }

  function clearSearch() {
    searchInput.value = '';
    clearBtn.classList.add('hidden');
    hideDropdown();
    showInitialState();
    searchInput.focus();
  }

  // Load index statistics
  async function loadStats() {
    try {
      const res = await fetch(getApiUrl('/api/stats'));
      if (!res.ok) throw new Error('API offline');
      const stats = await res.json();
      docCount.textContent = `${stats.total_docs.toLocaleString()} docs (${(stats.file_size_bytes / (1024*1024)).toFixed(2)} MB)`;
      if (serverStatus) serverStatus.textContent = 'API Live';
    } catch (e) {
      if (clientWikiDocs.length) {
        docCount.textContent = `${clientWikiDocs.length.toLocaleString()} Wikipedia Articles`;
      } else {
        docCount.textContent = 'Wikipedia Ready';
      }
      if (serverStatus) serverStatus.textContent = 'Standalone';
    }
  }

  // Theme support
  function setupTheme() {
    const saved = localStorage.getItem('needlefish_theme');
    if (saved === 'light') {
      document.body.classList.replace('dark-theme', 'light-theme');
      themeIcon.textContent = 'Dark';
    } else {
      document.body.classList.add('dark-theme');
      themeIcon.textContent = 'Light';
    }
  }

  function toggleTheme() {
    if (document.body.classList.contains('dark-theme')) {
      document.body.classList.replace('dark-theme', 'light-theme');
      themeIcon.textContent = 'Dark';
      localStorage.setItem('needlefish_theme', 'light');
    } else {
      document.body.classList.replace('light-theme', 'dark-theme');
      themeIcon.textContent = 'Light';
      localStorage.setItem('needlefish_theme', 'dark');
    }
  }

  function escapeHtml(str) {
    if (!str) return '';
    return str.replace(/[&<>"']/g, m => ({
      '&': '&amp;',
      '<': '&lt;',
      '>': '&gt;',
      '"': '&quot;',
      "'": '&#39;'
    })[m]);
  }

  // Start
  init();
})();
