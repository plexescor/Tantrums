/* sidebar.js — injects sidebar + overlay into every page */
(function(){
  const root = document.documentElement.dataset.root || '';
  const html = `
<aside class="sidebar">
  <a href="${root}index.html" class="sidebar-logo">
    <div class="logo-badge">.42</div>
    <div><span class="logo-name">Tantrums</span><span class="logo-ver">v0.2.0</span></div>
  </a>
  <nav class="sidebar-nav">
    <div class="nav-group">
      <span class="nav-group-label">Getting Started</span>
      <a href="${root}index.html" class="nav-link"><span class="nav-dot"></span>Introduction</a>
      <a href="${root}pages/quickstart.html" class="nav-link"><span class="nav-dot"></span>Quick Start</a>
      <a href="${root}pages/building.html" class="nav-link"><span class="nav-dot"></span>Building from Source</a>
    </div>
    <div class="nav-group">
      <span class="nav-group-label">Language</span>
      <a href="${root}pages/syntax.html" class="nav-link"><span class="nav-dot"></span>Syntax & Variables</a>
      <a href="${root}pages/types.html" class="nav-link"><span class="nav-dot"></span>Types & Modes</a>
      <a href="${root}pages/functions.html" class="nav-link"><span class="nav-dot"></span>Functions</a>
      <a href="${root}pages/control-flow.html" class="nav-link"><span class="nav-dot"></span>Control Flow</a>
      <a href="${root}pages/operators.html" class="nav-link"><span class="nav-dot"></span>Operators</a>
      <a href="${root}pages/imports.html" class="nav-link"><span class="nav-dot"></span>Imports</a>
    </div>
    <div class="nav-group">
      <span class="nav-group-label">Memory</span>
      <a href="${root}pages/memory.html" class="nav-link"><span class="nav-dot"></span>Memory Model</a>
      <a href="${root}pages/pointers.html" class="nav-link"><span class="nav-dot"></span>Pointers & alloc</a>
      <a href="${root}pages/escape-analysis.html" class="nav-link"><span class="nav-dot"></span>Escape Analysis</a>
      <a href="${root}pages/leak-detection.html" class="nav-link"><span class="nav-dot"></span>Leak Detection</a>
    </div>
    <div class="nav-group">
      <span class="nav-group-label">Builtins</span>
      <a href="${root}pages/builtins.html" class="nav-link"><span class="nav-dot"></span>Built-in Functions</a>
      <a href="${root}pages/profiling.html" class="nav-link"><span class="nav-dot"></span>Profiling</a>
      <a href="${root}pages/error-handling.html" class="nav-link"><span class="nav-dot"></span>Error Handling</a>
    </div>
    <div class="nav-group">
      <span class="nav-group-label">Deep Dive</span>
      <a href="${root}pages/pipeline.html" class="nav-link"><span class="nav-dot"></span>Compiler Pipeline</a>
      <a href="${root}pages/performance.html" class="nav-link"><span class="nav-dot"></span>Performance</a>
      <a href="${root}pages/examples.html" class="nav-link"><span class="nav-dot"></span>Examples</a>
      <a href="${root}pages/roadmap.html" class="nav-link"><span class="nav-dot"></span>Roadmap</a>
    </div>
  </nav>
  <div class="sidebar-footer">
    <a href="https://github.com/plexescor/Tantrums" target="_blank" class="gh-link">
      <svg width="13" height="13" viewBox="0 0 24 24" fill="currentColor"><path d="M12 0C5.37 0 0 5.37 0 12c0 5.31 3.435 9.795 8.205 11.385.6.105.825-.255.825-.57 0-.285-.015-1.23-.015-2.235-3.015.555-3.795-.735-4.035-1.41-.135-.345-.72-1.41-1.23-1.695-.42-.225-1.02-.78-.015-.795.945-.015 1.62.87 1.845 1.23 1.08 1.815 2.805 1.305 3.495.99.105-.78.42-1.305.765-1.605-2.67-.3-5.46-1.335-5.46-5.925 0-1.305.465-2.385 1.23-3.225-.12-.3-.54-1.53.12-3.18 0 0 1.005-.315 3.3 1.23.96-.27 1.98-.405 3-.405s2.04.135 3 .405c2.295-1.56 3.3-1.23 3.3-1.23.66 1.65.24 2.88.12 3.18.765.84 1.23 1.905 1.23 3.225 0 4.605-2.805 5.625-5.475 5.925.435.375.81 1.095.81 2.22 0 1.605-.015 2.895-.015 3.3 0 .315.225.69.825.57A12.02 12.02 0 0 0 24 12c0-6.63-5.37-12-12-12z"/></svg>
      GitHub
    </a>
  </div>
</aside>
<div class="overlay"></div>`;
  document.body.insertAdjacentHTML('afterbegin', html);
})();
