import assert from "node:assert/strict";
import test from "node:test";
import { translator } from "../src/i18n/index.js";

test("English translations include punctuation while preserving untranslated Latin text and missing-Chinese validation", () => {
  const t = translator("en");
  assert.equal(t("、"), ", ");
  assert.equal(translator("zh-CN")("、"), "、");
  assert.equal(t("QuotaFrame {version}", { version: "1.0" }), "QuotaFrame 1.0");
  assert.throws(() => t("缺失的翻译"), /Missing English translation/);
});
