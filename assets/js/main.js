const themeToggle = document.querySelector('[data-theme-toggle]');
const html = document.documentElement;
const savedTheme = localStorage.getItem('site-theme');
if (savedTheme) html.classList.toggle('light-mode', savedTheme === 'light');
if (themeToggle) {
  themeToggle.addEventListener('click', () => {
    const isLight = html.classList.toggle('light-mode');
    localStorage.setItem('site-theme', isLight ? 'light' : 'dark');
    themeToggle.textContent = isLight ? 'Dark Mode' : 'Light Mode';
  });
}

const navLinks = document.querySelectorAll('.nav-list a');
navLinks.forEach(link => {
  if (link.href === window.location.href) link.classList.add('active');
});

const searchInput = document.getElementById('search-input');
if (searchInput) {
  searchInput.addEventListener('input', () => {
    const query = searchInput.value.toLowerCase();
    document.querySelectorAll('.searchable').forEach(item => {
      const text = item.textContent.toLowerCase();
      item.style.display = text.includes(query) ? '' : 'none';
    });
  });
}
