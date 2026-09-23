import english from "./en.js";

export function translator(locale) {
  return (message, values = {}) => {
    let text = message;
    if (locale === "en") {
      if (Object.hasOwn(english, message)) {
        text = english[message];
      } else if (/[\u4e00-\u9fff]/.test(message)) {
        throw new Error(`Missing English translation: ${message}`);
      }
    }
    return text.replace(/\{(\w+)\}/g, (match, key) => values[key] ?? match);
  };
}
