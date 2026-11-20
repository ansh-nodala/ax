(() => {
  "use strict";

  const root = document.documentElement;
  const search = document.querySelector("#guide-search");
  const sections = Array.from(document.querySelectorAll(".guide-section"));
  const tocLinks = Array.from(document.querySelectorAll(".toc a"));
  const sidebar = document.querySelector("#sidebar");
  const menuToggle = document.querySelector(".menu-toggle");
  const themeToggle = document.querySelector(".theme-toggle");

  const searchStatus = document.createElement("p");
  searchStatus.className = "search-status";
  searchStatus.setAttribute("aria-live", "polite");
  document.querySelector(".sidebar-intro").insertAdjacentElement("afterend", searchStatus);

  function applySearch(rawQuery) {
    const query = rawQuery.trim().toLocaleLowerCase();
    let visibleCount = 0;

    for (const section of sections) {
      const searchable = `${section.dataset.search || ""} ${section.textContent}`.toLocaleLowerCase();
      const visible = query === "" || searchable.includes(query);
      section.hidden = !visible;
      if (visible) visibleCount += 1;
    }

    for (const link of tocLinks) {
      const section = document.querySelector(link.getAttribute("href"));
      link.hidden = Boolean(section && section.hidden);
    }

    searchStatus.textContent = query
      ? `${visibleCount} section${visibleCount === 1 ? "" : "s"} match “${rawQuery.trim()}”.`
      : "";
  }

  search.addEventListener("input", () => applySearch(search.value));
  document.addEventListener("keydown", (event) => {
    if (event.key === "/" && !event.ctrlKey && !event.metaKey && !event.altKey) {
      const target = event.target;
      const editing = target instanceof HTMLElement &&
        (target.isContentEditable || ["INPUT", "TEXTAREA", "SELECT"].includes(target.tagName));
      if (!editing) {
        event.preventDefault();
        search.focus();
      }
    }
    if (event.key === "Escape" && document.activeElement === search) {
      search.value = "";
      applySearch("");
      search.blur();
    }
  });

  async function copyText(value) {
    if (navigator.clipboard && window.isSecureContext) {
      await navigator.clipboard.writeText(value);
      return;
    }
    const temporary = document.createElement("textarea");
    temporary.value = value;
    temporary.setAttribute("readonly", "");
    temporary.className = "clipboard-fallback";
    document.body.append(temporary);
    temporary.select();
    const copied = document.execCommand("copy");
    temporary.remove();
    if (!copied) throw new Error("Clipboard access is unavailable in this browser.");
  }

  for (const block of document.querySelectorAll("pre")) {
    if (!block.querySelector("code")) continue;
    const button = document.createElement("button");
    button.className = "copy-button";
    button.type = "button";
    button.textContent = "Copy";
    button.setAttribute("aria-label", "Copy code block");
    button.addEventListener("click", async () => {
      const original = button.textContent;
      try {
        await copyText(block.querySelector("code").innerText);
        button.textContent = "Copied";
      } catch {
        button.textContent = "Select code";
      }
      window.setTimeout(() => { button.textContent = original; }, 1500);
    });
    block.append(button);
  }

  function setTheme(theme) {
    root.dataset.theme = theme;
    try { window.localStorage.setItem("ax-guide-theme", theme); } catch { /* Private browsing may disable storage. */ }
    themeToggle.setAttribute("aria-label", theme === "dark" ? "Switch to light theme" : "Switch to dark theme");
    themeToggle.title = theme === "dark" ? "Switch to light theme" : "Switch to dark theme";
  }

  let savedTheme = "light";
  try { savedTheme = window.localStorage.getItem("ax-guide-theme") || "light"; } catch { /* Keep the default theme. */ }
  setTheme(savedTheme === "dark" ? "dark" : "light");
  themeToggle.addEventListener("click", () => {
    setTheme(root.dataset.theme === "dark" ? "light" : "dark");
  });

  function setMenuOpen(open) {
    sidebar.classList.toggle("open", open);
    menuToggle.setAttribute("aria-expanded", String(open));
    menuToggle.setAttribute("aria-label", open ? "Close navigation" : "Open navigation");
  }

  menuToggle.addEventListener("click", () => {
    setMenuOpen(!sidebar.classList.contains("open"));
  });
  for (const link of tocLinks) link.addEventListener("click", () => setMenuOpen(false));
  document.addEventListener("keydown", (event) => {
    if (event.key === "Escape") setMenuOpen(false);
  });

  if ("IntersectionObserver" in window) {
    const linkById = new Map(tocLinks.map((link) => [link.hash.slice(1), link]));
    const observer = new IntersectionObserver((entries) => {
      for (const entry of entries) {
        if (!entry.isIntersecting || entry.target.hidden) continue;
        for (const link of tocLinks) link.classList.remove("active");
        const activeLink = linkById.get(entry.target.id);
        if (activeLink && !activeLink.hidden) activeLink.classList.add("active");
      }
    }, { rootMargin: "-85px 0px -72% 0px", threshold: 0 });
    for (const section of sections) observer.observe(section);
  }
})();
