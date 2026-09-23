import assert from "node:assert/strict";
import test from "node:test";
import { languageDestination, redirectToPreferredLanguage } from "../src/i18n/preference.js";

function browser({ locale = "en-US", saved = null, url = "https://example.test/site/?from=share#compare", blocked = false, pageLanguage = "zh-CN" } = {}) {
  const location = new URL(url);
  let redirected;
  location.replace = (destination) => { redirected = destination; };
  const view = {
    document: { documentElement: { lang: pageLanguage } },
    navigator: { languages: [locale], language: locale },
    location,
    get localStorage() {
      if (blocked) throw new Error("Storage blocked");
      return { getItem: () => saved, setItem: (_key, value) => { saved = value; } };
    },
  };
  return { view, redirected: () => redirected, saved: () => saved };
}

test("homepage detection respects primary browser language and saved manual preference", () => {
  for (const [locale, saved, expected] of [
    ["zh-CN", null, false], ["zh-TW", null, false], ["zh-HK", null, false],
    ["en-US", null, true], ["ja-JP", null, true],
    ["en-US", "zh-CN", false], ["zh-CN", "en", true], ["en-US", "invalid", true],
  ]) {
    const b = browser({ locale, saved });
    redirectToPreferredLanguage(b.view);
    assert.equal(b.redirected(), expected ? "https://example.test/site/en/?from=share#compare" : undefined);
  }
});

test("direct English URLs are respected even with a Chinese saved preference", () => {
  const b = browser({ locale: "zh-CN", saved: "zh-CN", pageLanguage: "en", url: "https://example.test/site/en/" });
  redirectToPreferredLanguage(b.view);
  assert.equal(b.redirected(), undefined);
});

test("manual choice is saved and retains query parameters and section", () => {
  const b = browser();
  assert.equal(languageDestination(b.view, "zh-CN", "https://example.test/site/"), "https://example.test/site/?from=share#compare");
  assert.equal(b.saved(), "zh-CN");
  redirectToPreferredLanguage(b.view);
  assert.equal(b.redirected(), undefined);
});

test("blocked storage preserves a manual Chinese choice without redirecting back", () => {
  const b = browser({ blocked: true });
  const destination = languageDestination(b.view, "zh-CN", "https://example.test/site/");
  const next = browser({ blocked: true, url: destination });
  assert.equal(new URL(destination).searchParams.get("lang"), "zh-CN");
  redirectToPreferredLanguage(next.view);
  assert.equal(next.redirected(), undefined);
  redirectToPreferredLanguage(b.view);
  assert.equal(b.redirected(), "https://example.test/site/en/?from=share#compare");
});
