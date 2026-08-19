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

  function getApiUrl(path) {
    if (customApiUrl && customApiUrl.trim().length > 0) {
      const base = customApiUrl.trim().replace(/\/+$/, '');
      return `${base}${path}`;
    }
    return path;
  }

  // Initialize
  function init() {
    loadStats();
    setupEventListeners();
    setupTheme();
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
          'Enter live Needlefish C++ backend URL (e.g. https://useful-ericsson-exterior-howard.trycloudflare.com):\nLeave empty for default proxy.',
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
    }, 120);
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

  // Fetch suggestions from /api/suggest
  async function fetchSuggestions(prefix) {
    try {
      const isFuzzy = currentMode === 'fuzzy';
      const res = await fetch(getApiUrl(`/api/suggest?q=${encodeURIComponent(prefix)}&fuzzy=${isFuzzy}`));
      if (!res.ok) return;
      const data = await res.json();
      renderSuggestions(data.suggestions || []);
    } catch (err) {
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

  // Execute Search from /api/search
  async function executeSearch(query) {
    if (!query) return;

    try {
      const res = await fetch(getApiUrl(`/api/search?q=${encodeURIComponent(query)}&mode=${currentMode}&limit=15`));
      if (!res.ok) {
        const err = await res.json();
        renderError(err.error || 'Failed to fetch search results');
        return;
      }
      const data = await res.json();
      renderResults(data);
    } catch (err) {
      renderOfflineResults(query);
    }
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

  // Load index statistics from /api/stats
  async function loadStats() {
    try {
      const res = await fetch(getApiUrl('/api/stats'));
      if (!res.ok) throw new Error('API offline');
      const stats = await res.json();
      docCount.textContent = `${stats.total_docs.toLocaleString()} docs (${(stats.file_size_bytes / (1024*1024)).toFixed(2)} MB)`;
      if (serverStatus) serverStatus.textContent = 'API Live';
    } catch (e) {
      docCount.textContent = 'Demo Mode';
      if (serverStatus) serverStatus.textContent = 'Set API';
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

  // Offline demo fallbacks
  function renderOfflineSuggestions(prefix) {
    const demo = ['information retrieval', 'inverted index', 'block-max wand', 'burrows-wheeler transform', 'succinct data structures'];
    const matches = demo.filter(d => d.startsWith(prefix.toLowerCase())).map(t => ({ term: t, doc_freq: 10, edit_distance: 0 }));
    renderSuggestions(matches);
  }

  function renderOfflineResults(query) {
    renderResults({
      query: query,
      mode: currentMode,
      took_us: 142,
      took_ms: 0.14,
      total_hits: 1,
      did_you_mean: '',
      hits: [{
        rank: 1,
        doc_id: 0,
        score: 6.421,
        title: 'Information Retrieval & Search Architecture',
        snippet: `Memory-mapped search engine matching query <em>${escapeHtml(query)}</em> using Block-Max WAND dynamic pruning and SIMD postings decompression.`
      }]
    });
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
