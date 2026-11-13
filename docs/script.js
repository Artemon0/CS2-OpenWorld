(() => {
    let language = localStorage.getItem('ow-docs-language') ||
        (navigator.language.toLowerCase().startsWith('ru') ? 'ru' : 'en');
    if (!['en', 'ru'].includes(language)) language = 'en';
    const toggle = document.getElementById('language-toggle');
    let translations = {};

    function renderLanguage() {
        const dict = translations[language] || {};
        document.documentElement.lang = language;
        document.querySelectorAll('[data-i18n]').forEach(element => {
            const value = dict[element.dataset.i18n];
            if (value !== undefined) element.innerHTML = value;
        });
        if (toggle) {
            toggle.textContent = language === 'en' ? 'RU' : 'EN';
            toggle.setAttribute('aria-label', language === 'en' ? 'Switch to Russian' : 'Переключить на английский');
        }
        document.querySelectorAll('.copy-btn').forEach(button => {
            button.textContent = language === 'ru' ? 'Копировать' : 'Copy';
        });
        renderContents();
    }

    function renderContents() {
        const old = document.querySelector('.toc');
        if (old) old.remove();
        const doc = document.querySelector('.doc');
        if (!doc) return;
        const headings = [...doc.querySelectorAll('h2, h3')];
        if (headings.length < 3) return;
        const contents = document.createElement('details');
        contents.className = 'toc';
        const summary = document.createElement('summary');
        summary.textContent = language === 'ru' ? 'Содержание' : 'On this page';
        contents.append(summary);
        const list = document.createElement('ol');
        headings.forEach((heading, index) => {
            if (!heading.id) heading.id = `heading-${index}`;
            const item = document.createElement('li');
            item.className = heading.tagName === 'H3' ? 'toc-level-2' : 'toc-level-1';
            const link = document.createElement('a');
            link.href = `#${heading.id}`;
            link.textContent = heading.textContent;
            item.append(link);
            list.append(item);
        });
        contents.append(list);
        headings[0].before(contents);
    }

    function addCopyButtons() {
        document.querySelectorAll('.doc pre').forEach(pre => {
            const code = pre.querySelector('code');
            if (!code) return;
            const button = document.createElement('button');
            button.className = 'copy-btn';
            button.type = 'button';
            button.textContent = language === 'ru' ? 'Копировать' : 'Copy';
            button.addEventListener('click', async () => {
                try {
                    await navigator.clipboard.writeText(code.textContent);
                    button.textContent = language === 'ru' ? 'Скопировано' : 'Copied';
                } catch {
                    button.textContent = language === 'ru' ? 'Ошибка' : 'Failed';
                }
                setTimeout(() => { button.textContent = language === 'ru' ? 'Копировать' : 'Copy'; }, 1800);
            });
            pre.append(button);
        });
    }

    document.addEventListener('DOMContentLoaded', async () => {
        const path = location.pathname.split('/').pop() || 'index.html';
        document.querySelectorAll('header nav a').forEach(link => {
            if (link.getAttribute('href') === path) link.setAttribute('aria-current', 'page');
        });
        addCopyButtons();
        try {
            const response = await fetch('translations.json');
            if (!response.ok) throw new Error(`HTTP ${response.status}`);
            translations = await response.json();
        } catch (error) {
            console.warn('Translations failed to load', error);
            if (toggle) toggle.hidden = true;
            language = 'en';
        }
        renderLanguage();
        toggle?.addEventListener('click', () => {
            language = language === 'en' ? 'ru' : 'en';
            localStorage.setItem('ow-docs-language', language);
            renderLanguage();
        });
    });
})();
