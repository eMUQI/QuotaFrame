import { languageDestination } from "./i18n/preference.js";
import { translator } from "./i18n/index.js";

const targetFields = [
  "id",
  "name",
  "description",
  "asset",
  "image",
  "manifest",
];
const phaseNames = [
  "initializing",
  "preparing",
  "erasing",
  "writing",
  "finished",
];

async function fetchDevices() {
  const response = await fetch(new URL("../devices.json", import.meta.url));
  if (!response.ok) throw new Error("Device request failed");
  return response.json();
}

async function fetchTargets() {
  const response = await fetch(new URL("../targets.json", import.meta.url), { cache: "no-store" });
  if (!response.ok) throw new Error("Target request failed");
  return response.json();
}

const screenPages = ["overview", "codex", "claude"];
const weekdays = ["SUN", "MON", "TUE", "WED", "THU", "FRI", "SAT"];
const months = [
  "JAN", "FEB", "MAR", "APR", "MAY", "JUN",
  "JUL", "AUG", "SEP", "OCT", "NOV", "DEC",
];

function buildScreen(document, host) {
  host.append(
    document.getElementById("screen-template").content.cloneNode(true),
  );
  return function show(screen) {
    host.dataset.screen = screen;
    host.style.setProperty("--qf-page", String(screenPages.indexOf(screen)));
    host.querySelectorAll("[data-page], [data-nav]").forEach((item) => {
      const name = item.dataset.page ?? item.dataset.nav;
      item.classList.toggle("is-active", name === screen);
    });
  };
}

// CSS cannot turn a container width into a unitless scale factor, so the
// 480 px board is measured and scaled from script.
function fitScreens(document) {
  const screens = [...document.querySelectorAll(".qf-screen")];
  const fit = () => {
    screens.forEach((screen) => {
      if (screen.clientWidth)
        screen.style.setProperty("--qf-scale", String(screen.clientWidth / 480));
    });
  };
  fit();
  const observer = document.defaultView?.ResizeObserver;
  if (observer) {
    const watch = new observer(fit);
    screens.forEach((screen) => watch.observe(screen));
  }
}

function startScreenClock(document) {
  const pad = (value) => String(value).padStart(2, "0");
  const write = (selector, text) => {
    document.querySelectorAll(selector).forEach((node) => {
      node.textContent = text;
    });
  };
  let shown = "";
  function paint() {
    const now = new Date();
    const time = `${pad(now.getHours())}:${pad(now.getMinutes())}`;
    if (time === shown) return;
    shown = time;
    write(".qf-time", time);
    write(".qf-weekday", weekdays[now.getDay()]);
    write(".qf-date", `${months[now.getMonth()]} ${now.getDate()}`);
  }
  paint();
  // Repaint once per wall-clock minute, behind a frame callback so the clock
  // stops advancing while the tab is hidden and needs no timer cleanup.
  const view = document.defaultView;
  const tick = () => {
    paint();
    view.setTimeout(
      () => view.requestAnimationFrame(tick),
      60000 - (Date.now() % 60000),
    );
  };
  if (view?.requestAnimationFrame) view.requestAnimationFrame(tick);
}

function initScreenDemo(document) {
  const t = translator(document.documentElement.lang);

  const screenLabels = {
    overview: [t("额度总览"), t("Codex 和 Claude 的短期与每周用量。")],
    codex: [t("Codex 详情"), t("短期用量 42%，每周用量 68%。")],
    claude: [t("Claude 详情"), t("短期用量 87%，每周用量 54%。")],
    clock: [t("时钟"), t("时间下方保留 Codex 与 Claude 的用量提示。")],
  };
  const get = (id) => document.getElementById(id);
  if (!get("preview-toggle") || !get("demo-tray")) return;

  const showPreviewScreen = buildScreen(document, get("preview-screen"));
  let preview = "overview";
  function showPreview(next) {
    preview = next;
    showPreviewScreen(next);
    const after = screenPages[(screenPages.indexOf(next) + 1) % screenPages.length];
    get("preview-toggle").setAttribute(
      "aria-label",
      t("当前为{current}，点击切换到{next}", { current: screenLabels[next][0], next: screenLabels[next === "clock" ? "overview" : after][0] }),
    );
    document.querySelectorAll("[data-preview]").forEach((button) => {
      button.setAttribute("aria-pressed", String(button.dataset.preview === next));
    });
  }
  get("preview-toggle").addEventListener("click", () => {
    const index = screenPages.indexOf(preview);
    showPreview(index < 0 ? "overview" : screenPages[(index + 1) % screenPages.length]);
  });
  document.querySelectorAll("[data-preview]").forEach((button) => {
    button.addEventListener("click", () => showPreview(button.dataset.preview));
  });
  showPreview("overview");

  const showDemoScreen = buildScreen(document, get("demo-screen"));
  let demoPage = 0;
  let demoClock = false;
  const demoTray = get("demo-tray");
  function renderDemo() {
    const screen = demoClock ? "clock" : screenPages[demoPage];
    const [label, description] = screenLabels[screen];
    showDemoScreen(screen);
    get("demo-screen").setAttribute("aria-label", `${label}: ${description}`);
    get("demo-status").textContent = label;
    demoTray.setAttribute("aria-pressed", String(demoClock));
  }
  function turnDemoPage(direction) {
    demoClock = false;
    demoPage = (demoPage + direction + screenPages.length) % screenPages.length;
    renderDemo();
  }
  renderDemo();
  demoTray.addEventListener("click", () => {
    demoClock = !demoClock;
    renderDemo();
  });
  get("demo-previous").addEventListener("click", () => turnDemoPage(-1));
  get("demo-next").addEventListener("click", () => turnDemoPage(1));
  demoTray.addEventListener("keydown", (event) => {
    if (event.key !== "ArrowUp" && event.key !== "ArrowDown") return;
    event.preventDefault();
    turnDemoPage(event.key === "ArrowUp" ? -1 : 1);
  });
  let wheelDelta = 0;
  let lastWheelEvent = -Infinity;
  let lastWheelStep = -Infinity;
  let lastPageWheel = -Infinity;
  document.addEventListener("wheel", (event) => {
    if (demoTray.contains(event.target)) return;
    lastPageWheel = event.timeStamp;
    wheelDelta = 0;
  }, { passive: true, capture: true });
  demoTray.addEventListener("wheel", (event) => {
    if (event.ctrlKey || event.metaKey || !event.deltaY || Math.abs(event.deltaX) > Math.abs(event.deltaY)) return;
    const now = event.timeStamp;
    // Ignore tray scrolling until page-scroll inertia has stopped for 250 ms.
    if (now - lastPageWheel < 250) {
      lastPageWheel = now;
      return;
    }
    event.preventDefault();
    const delta = event.deltaY * (event.deltaMode === 1 ? 16 : event.deltaMode === 2 ? 200 : 1);
    if (now - lastWheelEvent > 500 || wheelDelta * delta < 0) wheelDelta = 0;
    lastWheelEvent = now;
    wheelDelta += delta;
    if (Math.abs(wheelDelta) < 48) return;
    const direction = Math.sign(wheelDelta);
    wheelDelta = 0;
    if (now - lastWheelStep < 200) return;
    lastWheelStep = now;
    turnDemoPage(direction);
  }, { passive: false });

  fitScreens(document);
  startScreenClock(document);
}

export async function initialize({
  document,
  navigator,
  isSecureContext,
  loadTargets = fetchTargets,
  loadDevices = fetchDevices,
  loadInstaller = () => import("./flash-engine.js"),
}) {
  const t = translator(document.documentElement.lang);
  const phaseTitles = {
    initializing: t("正在连接设备"),
    preparing: t("正在下载固件"),
    erasing: t("正在擦除设备"),
    writing: t("正在写入固件"),
    finished: t("正在复位设备"),
  };

  const errorGuidance = {
    fetch_manifest_failed:
      t("固件信息暂时无法下载。检查网络后重试，设备尚未开始写入。"),
    failed_initialize:
      t("无法连接设备。关闭占用串口的程序，确认 USB 线支持数据传输，并按厂商说明让设备进入下载模式后重试。"),
    not_supported:
      t("检测到的芯片与固件不匹配。请重新核对设备型号，不要安装其他设备的固件。"),
    failed_firmware_download: t("固件下载失败。请检查网络后重新连接并安装。"),
    firmware_checksum_failed:
      t("固件校验未通过，下载的文件与发布版本不一致。请刷新页面重试，设备尚未开始写入。"),
    write_failed:
      t("固件写入中断。请保持供电和 USB 连接稳定，重新连接并完成安装。已擦除的数据不会恢复。"),
    disconnect_failed:
      t("无法释放设备连接。请拔插 USB 后刷新页面，再检查设备是否正常启动。"),
  };

  const get = (id) => document.getElementById(id);
  initScreenDemo(document);
  const view = document.defaultView;
  const siteRoot = new URL(document.documentElement.lang === "en" ? "../" : "./", document.baseURI);
  const languageSwitch = get("language-switch");
  languageSwitch?.addEventListener("change", (event) => {
    if (busy) {
      languageSwitch.value = document.documentElement.lang;
      event.preventDefault();
      return;
    }
    if (languageSwitch.value === document.documentElement.lang) return;
    view.location.assign(languageDestination(view, languageSwitch.value, siteRoot));
  });
  const button = get("install-button");
  const confirm = get("confirm-target");
  const erase = get("erase-device");
  const canFlash = Boolean(navigator.serial?.requestPort) && isSecureContext;
  let target;
  let registry;
  let devices;
  let firmwareUnavailable = t("固件暂不可用，请稍后重新加载页面。");
  let runFlash;
  let busy = false;
  let activePhase = "initializing";
  let engineFailed = false;
  let completed = false;

  function journey(step) {
    const order = ["device", "flash", "desktop"];
    document.querySelectorAll("[data-journey]").forEach((item) => {
      item.removeAttribute("aria-current");
      item.classList.toggle(
        "is-done",
        order.indexOf(item.dataset.journey) < order.indexOf(step),
      );
      if (item.dataset.journey === step)
        item.setAttribute("aria-current", "step");
    });
  }
  function controls() {
    if (languageSwitch) languageSwitch.disabled = busy;
    get("flash-progress").classList.toggle("is-complete", completed);
    button.classList.toggle("primary", !completed);
    button.classList.toggle("secondary", completed);
    button.disabled =
      busy || !target?.manifest || !confirm.checked || !canFlash || !runFlash;
    get("change-device").disabled = busy;
    confirm.disabled = busy || !target?.manifest;
    erase.disabled = busy || !target?.manifest;
    document.querySelectorAll("[data-target-id]").forEach((card) => {
      card.disabled = busy;
    });
    button.textContent = target && !target.manifest
      ? firmwareUnavailable
      : busy
      ? t("安装中，请保持连接")
      : completed
        ? t("重新连接并安装")
        : t("连接设备并安装");
  }
  function resetProgress() {
    get("flash-progress").hidden = true;
    get("flash-error").hidden = true;
    get("continue-desktop").hidden = true;
    get("write-progress").hidden = true;
    get("write-progress").value = 0;
    get("progress-percentage").textContent = "";
    engineFailed = false;
    completed = false;
    activePhase = "initializing";
    document.querySelectorAll("[data-phase]").forEach((item) => {
      item.removeAttribute("data-state");
      item.removeAttribute("aria-current");
      item.querySelector("small").textContent =
        item.dataset.phase === "erasing" && !erase.checked
          ? t("跳过整片擦除")
          : t("等待开始");
    });
  }
  function markPhase(phase, done = false) {
    const index = phaseNames.indexOf(phase);
    if (index < 0) return;
    activePhase = phase;
    document.querySelectorAll("[data-phase]").forEach((item, position) => {
      const skipped = item.dataset.phase === "erasing" && !erase.checked;
      const status =
        position < index || (position === index && done)
          ? "done"
          : position === index
            ? "active"
            : "pending";
      item.dataset.state = skipped ? "skipped" : status;
      item.removeAttribute("aria-current");
      if (status === "active" && !skipped)
        item.setAttribute("aria-current", "step");
      item.querySelector("small").textContent = skipped
        ? t("跳过整片擦除")
        : { done: t("已完成"), active: t("进行中"), pending: t("等待开始") }[status];
    });
  }
  function progress(event) {
    if (engineFailed) return;
    if (event.state === "error") {
      engineFailed = true;
      return;
    }
    if (!phaseNames.includes(event.state)) return;
    // Completion is shown only after the engine resolves and releases the port.
    const phase =
      event.state === "writing" && event.details?.percentage === 100
        ? "finished"
        : event.state;
    markPhase(phase, Boolean(event.details?.done));
    get("progress-title").textContent = phaseTitles[phase];
    get("progress-message").textContent =
      phase === "finished"
        ? t("固件已写入，正在完成设备复位。请暂时保持连接。")
        : t("请保持页面打开和 USB 连接稳定。");
    if (event.state === "writing") {
      const percentage = Math.max(
        0,
        Math.min(100, Number(event.details?.percentage) || 0),
      );
      get("write-progress").hidden = false;
      get("write-progress").value = percentage;
      get("progress-percentage").textContent = `${percentage}%`;
    }
  }
  get("browser-support").textContent = canFlash
    ? t("请使用当前桌面浏览器完成 USB 安装。")
    : !isSecureContext
      ? t("请通过 HTTPS 打开页面，并使用桌面版 Chrome 或 Edge 安装。")
      : t("此浏览器不支持 USB 安装，请在电脑上使用桌面版 Chrome 或 Edge。");
  get("browser-support").classList.toggle("unsupported", !canFlash);
  get("change-device").addEventListener("click", () => {
    if (busy) return;
    get("device-picker").hidden = false;
    get("device-summary").hidden = true;
    get("flash-panel").hidden = true;
    confirm.checked = false;
    journey("device");
    controls();
    document.querySelector(`[data-target-id="${target.id}"]`).focus();
  });
  confirm.addEventListener("change", controls);
  erase.addEventListener("change", () => {
    resetProgress();
    controls();
  });
  get("reload-page").addEventListener("click", () => view.location.reload());
  view.addEventListener("beforeunload", (event) => {
    if (busy) {
      event.preventDefault();
      event.returnValue = "";
    }
  });
  button.addEventListener("click", async () => {
    if (button.disabled || busy) return;
    busy = true;
    resetProgress();
    controls();
    get("install-options").hidden = true;
    get("flash-progress").hidden = false;
    get("progress-title").textContent = t("选择 USB 设备");
    get("progress-message").textContent =
      t("在浏览器列表中选择已连接的设备。取消选择不会开始安装。");
    markPhase("initializing");
    get("progress-title").focus();
    let port;
    try {
      // requestPort must run directly in the user's click to retain browser activation.
      port = await navigator.serial.requestPort();
      get("progress-title").textContent = t("正在准备固件信息");
      get("progress-message").textContent = t("正在读取所选设备的固件清单。");
      await runFlash({
        port,
        manifestUrl: new URL(target.manifest, siteRoot).href,
        eraseFirst: erase.checked,
        onEvent: progress,
      });
      if (engineFailed) throw new Error(t("安装未完成"));
      markPhase("finished", true);
      completed = true;
      journey("desktop");
      get("progress-title").textContent = t("固件安装完成");
      get("progress-message").textContent =
        t("设备已发送复位指令。确认屏幕启动后，继续连接电脑端；如果屏幕没变化，请尝试重新上电。");
      get("continue-desktop").hidden = false;
    } catch (error) {
      if (!port && error.name === "NotFoundError") {
        get("install-options").hidden = false;
        get("progress-title").textContent = t("已取消连接");
        get("progress-message").textContent =
          t("没有选择设备，尚未开始安装。准备好后可以重新连接。");
        const item = document.querySelector('[data-phase="initializing"]');
        item.dataset.state = "pending";
        item.removeAttribute("aria-current");
        item.querySelector("small").textContent = t("已取消");
      } else {
        const item = document.querySelector(`[data-phase="${activePhase}"]`);
        item.dataset.state = "error";
        item.removeAttribute("aria-current");
        item.querySelector("small").textContent = t("未完成");
        get("progress-title").textContent = t("安装未完成");
        get("progress-message").textContent =
          t("按下面的提示处理后，可以重新连接。");
        get("flash-error").hidden = false;
        if (error.requiresReload) {
          runFlash = undefined;
          get("reload-page").hidden = false;
        }
        get("flash-error").textContent = error.requiresReload
          ? t("设备连接未能完全释放。请拔插 USB 后重新加载页面，再尝试安装。")
          : errorGuidance[error.code] ||
            (!port
              ? t("无法打开设备选择器。请允许串口访问，并使用桌面版 Chrome 或 Edge。")
              : t("设备连接或复位异常。关闭占用串口的程序，拔插 USB 后重试。已擦除的数据不会恢复。"));
      }
    } finally {
      busy = false;
      controls();
      get("progress-title").focus({ preventScroll: true });
    }
  });

  const platforms = {
    mac: [
      t("macOS 14 或更新版本 · 仅 Apple Silicon · 需要蓝牙"),
      "CodexBar",
      "https://github.com/steipete/CodexBar",
      t("在发布页下载 macos-arm64 .dmg 安装包，安装并打开 QuotaFrame Bridge。"),
    ],
    windows: [
      t("Windows 10 / 11 · 需要蓝牙"),
      "Win-CodexBar",
      "https://github.com/nesszer/Win-CodexBar",
      t("在发布页下载 windows setup.exe 安装包，安装并打开 QuotaFrame Bridge。"),
    ],
  };
  function selectPlatform(platform) {
    const [requirement, name, href, hint] = platforms[platform];
    document
      .querySelectorAll("[data-platform]")
      .forEach((item) =>
        item.setAttribute(
          "aria-pressed",
          String(item.dataset.platform === platform),
        ),
      );
    get("platform-requirement").textContent = requirement;
    get("source-cli-hint").textContent = platform === "mac"
      ? t("在 CodexBar 的 Preferences → Advanced 中选择 Install CLI，让电脑端能够读取用量。")
      : t("Bridge 缺少 Win-CodexBar CLI 时会自动下载固定版本，也可使用本机已安装的 CLI。");
    get("source-download").textContent = name;
    get("source-download").href = href;
    get("desktop-download-hint").textContent = hint;
  }
  document
    .querySelectorAll("[data-platform]")
    .forEach((item) =>
      item.addEventListener("click", () =>
        selectPlatform(item.dataset.platform),
      ),
    );
  selectPlatform(/Win/.test(navigator.platform || "") ? "windows" : "mac");

  try {
    devices = await loadDevices();
    if (!Array.isArray(devices.targets) || !devices.targets.length ||
        devices.targets.some((item) => !item || ["id", "name", "description", "asset"].some(
          (field) => typeof item[field] !== "string" || !item[field].trim(),
        )) || new Set(devices.targets.map((item) => item.id)).size !== devices.targets.length)
      throw new Error("Invalid devices");
  } catch {
    get("load-error").hidden = false;
    get("load-error").textContent = t("暂时无法加载设备信息，请重新加载页面。");
    get("reload-page").hidden = false;
    return;
  }
  try {
    registry = await loadTargets();
    if (registry.version === null && Array.isArray(registry.targets) && !registry.targets.length) {
      firmwareUnavailable = t("当前预览未加载固件");
    } else if (
      typeof registry.version !== "string" ||
      !registry.version.trim() ||
      !Array.isArray(registry.targets) ||
      !registry.targets.length ||
      registry.targets.some((item) =>
        !item || targetFields.some(
          (field) => typeof item[field] !== "string" || !item[field].trim(),
        ),
      )
    )
      throw new Error("Invalid targets");
  } catch {
    registry = null;
    get("load-error").hidden = false;
    get("load-error").textContent =
      t("暂时无法加载固件信息，请检查网络后重新加载。");
    get("reload-page").hidden = false;
  }
  get("target-grid").replaceChildren();
  get("release-version").textContent = registry?.version ? `v${registry.version}` : t("固件未加载");
  get("release-version").hidden = false;
  const displayTargets = devices.targets.map((device) => {
    const firmware = registry?.targets.find((item) => item.id === device.id);
    return { ...device, asset: new URL(device.asset, siteRoot).href, description: t(device.description), image: firmware?.image, manifest: firmware?.manifest };
  }).sort(
    (a, b) => Number(b.id === "waveshare_amoled_216") - Number(a.id === "waveshare_amoled_216"),
  );
  for (const item of displayTargets) {
    const card = get(
      "target-card-template",
    ).content.firstElementChild.cloneNode(true);
    card.dataset.targetId = item.id;
    card.querySelector("img").src = item.asset;
    card.querySelector(".target-name").textContent = item.name;
    card.querySelector(".target-description").textContent = item.description;
    card.addEventListener("click", () => {
      if (busy) return;
      target = item;
      confirm.checked = false;
      resetProgress();
      get("selected-device").textContent = item.name;
      get("selected-image").textContent = item.image || firmwareUnavailable;
      get("selected-version").textContent = item.manifest ? `v${registry.version}` : "—";
      get("install-options").hidden = false;
      get("device-picker").hidden = true;
      get("device-summary").hidden = false;
      get("chosen-device-name").textContent = item.name;
      get("chosen-device-image").src = item.asset;
      get("flash-panel").hidden = false;
      get("flash-title").focus({ preventScroll: true });
      document.querySelectorAll("[data-target-id]").forEach((other) => {
        const selected = other === card;
        other.classList.toggle("is-selected", selected);
        other.setAttribute("aria-pressed", String(selected));
        other.querySelector(".target-choice").textContent = selected
          ? t("已选择")
          : t("选择设备");
      });
      journey("flash");
      controls();
    });
    get("target-grid").append(card);
  }
  if (canFlash && displayTargets.some((item) => item.manifest)) {
    try {
      ({ runFlash } = await loadInstaller());
      if (typeof runFlash !== "function") throw new Error("Missing installer");
    } catch {
      runFlash = undefined;
      get("load-error").hidden = false;
      get("load-error").textContent =
        t("烧录组件加载失败，尚未连接设备。请重新加载页面后重试。");
      get("reload-page").hidden = false;
    }
  }
  controls();
}

if (typeof window !== "undefined") {
  initialize({
    document: window.document,
    navigator: window.navigator,
    isSecureContext: window.isSecureContext,
  });
}
