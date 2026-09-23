const storageKey = "quotaframe.language";
const supported = (language) => language === "zh-CN" || language === "en";

export function rememberLanguage(view, language) {
  try {
    view.localStorage.setItem(storageKey, language);
    return true;
  } catch {
    return false;
  }
}

export function languageDestination(view, language, siteRoot) {
  const destination = new URL(language === "en" ? "en/" : "./", siteRoot);
  destination.search = view.location.search;
  destination.hash = view.location.hash;
  destination.searchParams.delete("lang");
  // Preserve an explicit choice even when browser storage is unavailable.
  if (!rememberLanguage(view, language)) destination.searchParams.set("lang", language);
  return destination.href;
}

export function redirectToPreferredLanguage(view) {
  if (view.document.documentElement.lang !== "zh-CN") return;
  const current = new URL(view.location.href);
  let language = current.searchParams.get("lang");
  if (!supported(language)) {
    try {
      language = view.localStorage.getItem(storageKey);
    } catch {
      language = null;
    }
  }
  if (!supported(language)) {
    const browserLanguage = view.navigator.languages?.[0] || view.navigator.language || "zh-CN";
    language = /^zh(?:-|$)/i.test(browserLanguage) ? "zh-CN" : "en";
  }
  if (language === "en") {
    const destination = new URL("en/", current);
    destination.search = current.search;
    destination.hash = current.hash;
    view.location.replace(destination.href);
  }
}
