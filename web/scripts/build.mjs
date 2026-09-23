import { translator } from "../src/i18n/index.js";
import { execFileSync } from "node:child_process";
import { build } from "esbuild";
import { cp, copyFile, mkdir, readFile, writeFile } from "node:fs/promises";

await mkdir("dist/assets", { recursive: true });
const devices = execFileSync(process.env.PYTHON || (process.platform === "win32" ? "python" : "python3"), [
  "../scripts/export_web_devices.py",
], { encoding: "utf8" });
for (const device of JSON.parse(devices).targets) translator("en")(device.description);
await writeFile("dist/devices.json", devices);
await writeFile("dist/targets.json", JSON.stringify({ version: null, targets: [] }));
await cp("assets", "dist/assets", { recursive: true });
await build({
  entryPoints: ["src/app.js"],
  outfile: "dist/assets/app.js",
  bundle: true,
  external: ["./flash-engine.js"],
  format: "esm",
  platform: "browser",
  target: "es2022",
});
await build({
  entryPoints: ["src/language-redirect.js"],
  outfile: "dist/assets/language-redirect.js",
  bundle: true,
  format: "iife",
  platform: "browser",
  target: "es2022",
  minify: true,
});
await copyFile("src/styles.css", "dist/assets/styles.css");
await build({
  entryPoints: ["src/flash-engine.js"],
  outfile: "dist/assets/flash-engine.js",
  bundle: true,
  // The pinned source package exposes its browser entry as TypeScript.
  alias: { "esptool-js": "./node_modules/esptool-js/src/index.ts" },
  format: "esm",
  platform: "browser",
  target: "es2022",
  minify: true,
  legalComments: "inline",
});
const notices = [];
for (const [name, file] of [
  ["esptool-js", "LICENSE"],
  ["pako", "LICENSE"],
  ["tslib", "CopyrightNotice.txt"],
  ["atob-lite", "LICENSE.md"],
]) {
  notices.push(
    `${name}\n${await readFile(`node_modules/${name}/${file}`, "utf8")}`,
  );
}
await writeFile("dist/THIRD-PARTY-NOTICES", notices.join("\n\n"));
