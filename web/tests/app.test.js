import assert from "node:assert/strict";
import { readFile } from "node:fs/promises";
import test from "node:test";
import { JSDOM } from "jsdom";
import { initialize } from "../src/app.js";

const targetDocument = {
  version: "1.2.3",
  targets: ["alpha", "beta"].map((id) => ({
    id,
    name: `${id} Board`,
    description: `${id} display`,
    asset: `assets/devices/${id}.svg`,
    image: `${id}-full-v1.2.3.bin`,
    manifest: `manifests/${id}.json`,
  })),
};
const tick = () => new Promise((resolve) => setImmediate(resolve));
async function page(options = {}, locale = "zh-CN") {
  const dom = new JSDOM(
    await readFile(new URL(locale === "en" ? "../dist/en/index.html" : "../dist/index.html", import.meta.url), "utf8"),
    { url: locale === "en" ? "https://example.test/flasher/en/#setup" : "https://example.test/flasher/" },
  );
  const started = initialize({
    document: dom.window.document,
    navigator: { serial: { requestPort: async () => ({}) } },
    isSecureContext: true,
    loadTargets: async () => targetDocument,
    loadDevices: async () => ({ targets: targetDocument.targets }),
    loadInstaller: async () => ({ runFlash: async () => {} }),
    ...options,
  });
  return {
    dom,
    document: dom.window.document,
    started,
    get: (id) => dom.window.document.getElementById(id),
  };
}
function select(p, id = "alpha") {
  p.document.querySelector(`[data-target-id="${id}"]`).click();
}
function confirm(p) {
  p.get("confirm-target").checked = true;
  p.get("confirm-target").dispatchEvent(new p.dom.window.Event("change"));
}

test("screen preview switches independently of release loading", async () => {
  const p = await page({ loadTargets: async () => { throw new Error("offline"); } });
  await p.started;
  const screen = p.get("preview-screen");
  const active = () => screen.querySelector(".qf-page.is-active")?.dataset.page;
  assert.equal(active(), "overview");
  assert.equal(screen.querySelectorAll(".qf-page").length, 3);
  p.get("preview-toggle").click();
  assert.equal(active(), "codex");
  assert.equal(screen.style.getPropertyValue("--qf-page"), "1");
  assert.equal(screen.querySelector(".qf-nav-item.is-active").dataset.nav, "codex");
  p.document.querySelector('[data-preview="clock"]').click();
  assert.equal(screen.dataset.screen, "clock");
  assert.equal(active(), undefined, "时钟页不应同时点亮额度页");
  assert.equal(p.document.querySelector('[data-preview="clock"]').getAttribute("aria-pressed"), "true");
  p.get("preview-toggle").click();
  assert.equal(active(), "overview", "从时钟点屏幕应回到额度总览");
  assert.match(p.get("preview-toggle").getAttribute("aria-label"), /点击切换到Codex 详情/);
  assert.notEqual(p.document.querySelector(".qf-time").textContent, "--:--");
});

test("Bridge demo preserves the previous page and handles scoped wheel bursts without a device", async () => {
  let portRequests = 0;
  const p = await page({
    loadTargets: async () => { throw new Error("offline"); },
    navigator: { serial: { requestPort: async () => { portRequests++; } } },
  });
  await p.started;
  const tray = p.get("demo-tray");
  const label = () => p.get("demo-status").textContent;
  p.get("demo-next").click();
  assert.equal(label(), "Codex 详情");
  tray.click();
  assert.equal(label(), "时钟");
  assert.equal(p.get("demo-screen").dataset.screen, "clock");
  tray.click();
  assert.equal(label(), "Codex 详情");
  p.get("demo-next").click();
  p.get("demo-next").click();
  assert.equal(label(), "额度总览");
  p.get("demo-previous").click();
  assert.equal(label(), "Claude 详情");
  tray.click();
  tray.dispatchEvent(new p.dom.window.KeyboardEvent("keydown", { key: "ArrowDown", cancelable: true }));
  assert.equal(label(), "额度总览");

  const wheel = (deltaY, time, extra = {}, target = tray) => {
    const event = new p.dom.window.WheelEvent("wheel", { deltaY, bubbles: true, cancelable: true, ...extra });
    Object.defineProperty(event, "timeStamp", { value: time });
    target.dispatchEvent(event);
    return event.defaultPrevented;
  };
  wheel(120, 600, {}, p.document.body);
  assert.equal(wheel(120, 700), false, "整页滚动途中扫过图标不应吞掉滚轮");
  assert.equal(label(), "额度总览");
  wheel(20, 1000);
  wheel(20, 1010);
  assert.equal(label(), "额度总览");
  assert.equal(wheel(20, 1020), true);
  assert.equal(label(), "Codex 详情");
  wheel(120, 1050);
  assert.equal(label(), "Codex 详情");
  wheel(20, 1260);
  assert.equal(label(), "Codex 详情", "冷却期内被抑制的滚轮步不应留在累积量里");
  wheel(-120, 1400);
  assert.equal(label(), "额度总览");
  assert.equal(wheel(120, 1700, { ctrlKey: true }), false);
  assert.equal(wheel(10, 1800, { deltaX: 100 }), false);
  assert.equal(wheel(120, 1900, {}, p.document.body), false);
  assert.equal(label(), "额度总览");
  wheel(3, 2200, { deltaMode: 1 });
  assert.equal(label(), "Codex 详情");
  wheel(20, 3000);
  wheel(120, 3100, {}, p.document.body);
  wheel(40, 3400);
  assert.equal(label(), "Codex 详情", "整页滚动后不应残留图标上的半截累积量");
  wheel(120, 4000, {}, p.document.body);
  assert.equal(wheel(120, 4100), false);
  assert.equal(wheel(120, 4300), false, "长惯性滚动途中不应中途接管滚轮");
  assert.equal(label(), "Codex 详情");
  assert.equal(p.get("preview-screen").dataset.screen, "overview");
  assert.equal(portRequests, 0);
});

test("model selection resets confirmation and preserves the registry-specific firmware", async () => {
  const p = await page();
  await p.started;
  select(p);
  confirm(p);
  assert.equal(p.get("install-button").disabled, false);
  p.get("change-device").click();
  assert.equal(p.get("device-picker").hidden, false);
  assert.equal(p.get("flash-panel").hidden, true);
  select(p, "beta");
  assert.equal(p.get("selected-device").textContent, "beta Board");
  assert.equal(p.get("selected-image").textContent, "beta-full-v1.2.3.bin");
  assert.equal(p.get("install-button").disabled, true);
  assert.equal(p.get("flash-progress").hidden, true);
});

test("unsupported and insecure browsers cannot request a port", async () => {
  for (const options of [{ navigator: {} }, { isSecureContext: false }]) {
    const p = await page(options);
    await p.started;
    select(p);
    confirm(p);
    assert.equal(p.get("install-button").disabled, true);
    assert.match(p.get("browser-support").textContent, /桌面版 Chrome 或 Edge/);
  }
});

test("malformed registry offers reload and leaves desktop instructions usable", async () => {
  const p = await page({
    loadTargets: async () => ({ version: "", targets: [] }),
  });
  await p.started;
  assert.equal(p.document.querySelectorAll("[data-target-id]").length, 2);
  assert.equal(p.get("reload-page").hidden, false);
  assert.match(p.get("load-error").textContent, /暂时无法加载固件信息/);
  p.document.querySelector('[data-platform="windows"]').click();
  assert.equal(p.get("source-download").textContent, "Win-CodexBar");
});

test("engine loading gates installation and reports a failed import", async () => {
  let reject;
  const p = await page({
    loadInstaller: () =>
      new Promise((_, no) => {
        reject = no;
      }),
  });
  await tick();
  select(p);
  confirm(p);
  assert.equal(p.get("install-button").disabled, true);
  reject(new Error("offline"));
  await p.started;
  assert.equal(p.get("install-button").disabled, true);
  assert.match(p.get("load-error").textContent, /尚未连接设备/);
  assert.equal(p.get("reload-page").hidden, false);
});

test("port picker cancellation never starts the engine and can be retried", async () => {
  let calls = 0;
  const p = await page({
    navigator: {
      serial: {
        requestPort: async () => {
          throw Object.assign(new Error(), { name: "NotFoundError" });
        },
      },
    },
    loadInstaller: async () => ({
      runFlash: async () => {
        calls++;
      },
    }),
  });
  await p.started;
  select(p);
  confirm(p);
  p.get("install-button").click();
  await tick();
  assert.equal(calls, 0);
  assert.equal(p.get("progress-title").textContent, "已取消连接");
  assert.equal(p.get("install-button").disabled, false);
  assert.equal(p.get("flash-error").hidden, true);
});

test("locks target and erase controls, passes the chosen manifest, waits beyond 100% for completion", async () => {
  let args, resolve;
  const p = await page({
    loadInstaller: async () => ({
      runFlash: (options) => {
        args = options;
        return new Promise((yes) => {
          resolve = yes;
        });
      },
    }),
  });
  await p.started;
  select(p, "beta");
  confirm(p);
  p.get("erase-device").checked = false;
  p.get("erase-device").dispatchEvent(new p.dom.window.Event("change"));
  p.get("install-button").click();
  await tick();
  assert.equal(
    args.manifestUrl,
    "https://example.test/flasher/manifests/beta.json",
  );
  assert.equal(args.eraseFirst, false);
  assert.equal(p.get("erase-device").disabled, true);
  assert.equal(p.get("confirm-target").disabled, true);
  assert.equal(
    p.document.querySelector('[data-target-id="alpha"]').disabled,
    true,
  );
  args.onEvent({ state: "writing", details: { percentage: 100 } });
  assert.equal(p.get("progress-title").textContent, "正在复位设备");
  assert.equal(p.get("continue-desktop").hidden, true);
  args.onEvent({ state: "finished" });
  assert.equal(p.get("continue-desktop").hidden, true);
  assert.equal(p.get("install-button").disabled, true);
  resolve();
  await tick();
  assert.equal(p.get("progress-title").textContent, "固件安装完成");
  assert.equal(p.get("continue-desktop").hidden, false);
  assert.equal(p.get("install-button").disabled, false);
  select(p);
  assert.equal(p.get("flash-progress").hidden, true);
});

test("engine failures stay on the failed phase and retry clears stale progress", async () => {
  let attempts = 0;
  const p = await page({
    loadInstaller: async () => ({
      runFlash: async ({ onEvent }) => {
        attempts++;
        onEvent({ state: "writing", details: { percentage: 35 } });
        onEvent({ state: "error" });
        onEvent({ state: "finished" });
        throw Object.assign(new Error("write"), { code: "write_failed" });
      },
    }),
  });
  await p.started;
  select(p);
  confirm(p);
  p.get("install-button").click();
  await tick();
  assert.equal(
    p.document.querySelector('[data-phase="writing"]').dataset.state,
    "error",
  );
  assert.match(p.get("flash-error").textContent, /已擦除的数据不会恢复/);
  assert.equal(p.get("continue-desktop").hidden, true);
  p.get("install-button").click();
  assert.equal(p.get("write-progress").value, 0);
  assert.equal(p.get("flash-error").hidden, true);
  await tick();
  assert.equal(attempts, 2);
});

test("retains erase and bluetooth recovery guidance", async () => {
  const p = await page();
  await p.started;
  const notice = p.document.querySelector(".erase-notice").textContent;
  assert.match(notice, /电脑上的蓝牙配对记录不会被擦除/);
  assert.match(notice, /不能保证保留设备配置或绑定信息/);
  assert.match(p.get("flash-recovery").textContent, /下载模式/);
});

test("cleanup failure exposes reload outside the collapsed device picker", async () => {
  const p = await page({
    loadInstaller: async () => ({
      runFlash: async () => {
        throw Object.assign(new Error("locked"), { requiresReload: true });
      },
    }),
  });
  await p.started;
  select(p);
  confirm(p);
  p.get("install-button").click();
  await tick();
  assert.equal(p.get("install-button").disabled, true);
  assert.equal(p.get("reload-page").closest("[hidden]"), null);
  assert.match(p.get("flash-error").textContent, /重新加载页面/);
});


test("preview and unavailable firmware retain devices without loading an installer or requesting USB", async () => {
  for (const loadTargets of [
    async () => ({ version: null, targets: [] }),
    async () => { throw new Error("offline"); },
  ]) {
    let installerLoads = 0;
    let portRequests = 0;
    const p = await page({
      loadTargets,
      loadInstaller: async () => { installerLoads++; },
      navigator: { serial: { requestPort: async () => { portRequests++; } } },
    });
    await p.started;
    assert.equal(p.document.querySelectorAll("[data-target-id]").length, 2);
    select(p);
    confirm(p);
    p.get("install-button").click();
    assert.equal(p.get("install-button").disabled, true);
    assert.equal(p.get("confirm-target").disabled, true);
    assert.equal(p.get("erase-device").disabled, true);
    assert.equal(installerLoads, 0);
    assert.equal(portRequests, 0);
    assert.match(p.get("install-button").textContent, /固件/);
  }
});

test("a device without firmware cannot use another device's image", async () => {
  const p = await page({ loadTargets: async () => ({
    ...targetDocument, targets: [targetDocument.targets[0]],
  }) });
  await p.started;
  select(p, "alpha"); confirm(p);
  assert.equal(p.get("install-button").disabled, false);
  p.get("change-device").click();
  select(p, "beta"); confirm(p);
  assert.equal(p.get("install-button").disabled, true);
  assert.doesNotMatch(p.get("selected-image").textContent, /alpha/);
});


test("English installation uses shared firmware paths and locks language switching until cleanup", async () => {
  let args, finish;
  const p = await page({ loadInstaller: async () => ({ runFlash: (options) => {
    args = options;
    return new Promise((resolve) => { finish = resolve; });
  } }) }, "en");
  await p.started;
  assert.equal(p.document.documentElement.lang, "en");
  assert.equal(p.get("language-switch").value, "en");
  select(p, "beta"); confirm(p);
  p.get("install-button").click(); await tick();
  assert.equal(args.manifestUrl, "https://example.test/flasher/manifests/beta.json");
  assert.equal(p.get("chosen-device-image").src, "https://example.test/flasher/assets/devices/beta.svg");
  assert.equal(p.get("language-switch").disabled, true);
  p.get("language-switch").value = "zh-CN";
  const click = new p.dom.window.Event("change", { cancelable: true });
  p.get("language-switch").dispatchEvent(click);
  assert.equal(click.defaultPrevented, true);
  args.onEvent({ state: "writing", details: { percentage: 100 } });
  assert.equal(p.get("progress-title").textContent, "Resetting device");
  assert.equal(p.get("continue-desktop").hidden, true);
  finish(); await tick();
  assert.equal(p.get("progress-title").textContent, "Firmware installation complete");
  assert.equal(p.get("language-switch").value, "en");
  assert.equal(p.get("language-switch").disabled, false);
});

test("English errors, preview, and desktop instructions stay localized", async () => {
  const p = await page({ loadInstaller: async () => ({ runFlash: async () => {
    throw Object.assign(new Error("checksum"), { code: "firmware_checksum_failed" });
  } }) }, "en");
  await p.started;
  p.document.querySelector('[data-platform="windows"]').click();
  assert.match(p.get("platform-requirement").textContent, /Bluetooth required/);
  select(p); confirm(p); p.get("install-button").click(); await tick();
  assert.match(p.get("flash-error").textContent, /verification failed/);
  assert.match(p.get("flash-error").textContent, /Nothing has been written/);
  assert.equal(p.get("language-switch").disabled, false);
  const preview = await page({ loadTargets: async () => ({ version: null, targets: [] }) }, "en");
  await preview.started; select(preview);
  assert.equal(preview.get("install-button").textContent, "Firmware is not included in this preview");
  assert.equal(preview.get("install-button").disabled, true);
});
