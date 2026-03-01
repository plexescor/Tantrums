/* Tantrums Docs — main.js */

// ── SIDEBAR MOBILE ──────────────────────────────
const sidebar = document.querySelector('.sidebar');
const overlay = document.querySelector('.overlay');
const menuBtn = document.querySelector('.menu-btn');

if (menuBtn) menuBtn.addEventListener('click', toggle);
if (overlay)  overlay.addEventListener('click', close_);

function toggle(){ sidebar.classList.toggle('open'); overlay.classList.toggle('on'); }
function close_(){ sidebar.classList.remove('open'); overlay.classList.remove('on'); }

// ── ACTIVE NAV ──────────────────────────────────
const cur = location.pathname.split('/').pop() || 'index.html';
document.querySelectorAll('.nav-link').forEach(a => {
  const h = a.getAttribute('href').split('/').pop();
  if (h === cur) a.classList.add('active');
});

// ── COPY BUTTONS ────────────────────────────────
document.querySelectorAll('.copy-btn').forEach(btn => {
  btn.addEventListener('click', () => {
    const code = btn.closest('pre').querySelector('code');
    navigator.clipboard.writeText(code.innerText).then(() => {
      btn.textContent = 'Copied!';
      btn.classList.add('ok');
      setTimeout(() => { btn.textContent = 'Copy'; btn.classList.remove('ok'); }, 2000);
    });
  });
});

// ── SMOOTH SCROLL ───────────────────────────────
document.querySelectorAll('a[href^="#"]').forEach(a => {
  a.addEventListener('click', e => {
    const t = document.querySelector(a.getAttribute('href'));
    if (t){ e.preventDefault(); window.scrollTo({top: t.getBoundingClientRect().top + scrollY - 72, behavior:'smooth'}); }
  });
});

// ── BREADCRUMB ──────────────────────────────────
const h1 = document.querySelector('h1');
const bc  = document.querySelector('.bc-cur');
if (h1 && bc) bc.textContent = h1.textContent;
