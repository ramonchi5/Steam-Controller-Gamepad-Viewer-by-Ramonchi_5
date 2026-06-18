const params = readStyleParams(readSearchParams(window.location.search), window.location.hash);
document.body.classList.toggle("solid", params.get("bg") === "solid");
document.body.classList.toggle("debug", params.has("debug"));
document.body.classList.toggle("clean", params.has("clean"));
document.body.classList.toggle("show-title", params.get("title") === "1");
document.body.classList.toggle("preview-all", params.get("preview") === "all");

const viewStyle = readViewStyle(params);
applyViewStyle(viewStyle);

const trackpadPressPressure = 0.05;

const pressedLayers = {
  default: document.querySelector("[data-pressed-layer]"),
  back: document.querySelector("[data-pressed-back-layer]"),
  trigger: document.querySelector("[data-pressed-trigger-layer]"),
};
const controls = new Map();
const controlPlacements = new WeakMap();
const controlLabels = new Map();
for (const element of document.querySelectorAll("[data-control]")) {
  const name = element.dataset.control;
  if (!controls.has(name)) {
    controls.set(name, []);
  }
  controls.get(name).push(element);
  const marker = document.createComment(`control:${name}`);
  element.after(marker);
  controlPlacements.set(element, {
    parent: marker.parentNode,
    marker,
    pressedParent: pressedParentFor(element),
  });
}
for (const element of document.querySelectorAll("[data-control-label]")) {
  const name = element.dataset.controlLabel;
  if (!controlLabels.has(name)) {
    controlLabels.set(name, []);
  }
  controlLabels.get(name).push(element);
}
const sticks = {
  left: {
    dot: document.querySelector('[data-stick-dot="left"]'),
  },
  right: {
    dot: document.querySelector('[data-stick-dot="right"]'),
  },
};
const pads = {
  left: {
    root: document.querySelector('[data-pad="left"]'),
    dot: document.querySelector('[data-pad-dot="left"]'),
    ring: document.querySelector('[data-pad-ring="left"]'),
    x: 103.296,
    y: 139.723,
    size: 93,
    mirroredX: false,
  },
  right: {
    root: document.querySelector('[data-pad="right"]'),
    dot: document.querySelector('[data-pad-dot="right"]'),
    ring: document.querySelector('[data-pad-ring="right"]'),
    x: -0.81371,
    y: 1.15667,
    size: 93,
    mirroredX: true,
  },
};
const triggers = {
  left: {
    fill: document.querySelector('[data-trigger-fill-rect="lt"]'),
    fade: document.querySelector('[data-trigger-fade-rect="lt"]'),
    outlineFill: document.querySelector('[data-trigger-outline-fill-rect="lt"]'),
    outlineFade: document.querySelector('[data-trigger-outline-fade-rect="lt"]'),
    top: -42,
    bottom: 38,
    outlineTop: -78,
    transition: 8,
  },
  right: {
    fill: document.querySelector('[data-trigger-fill-rect="rt"]'),
    fade: document.querySelector('[data-trigger-fade-rect="rt"]'),
    outlineFill: document.querySelector('[data-trigger-outline-fill-rect="rt"]'),
    outlineFade: document.querySelector('[data-trigger-outline-fade-rect="rt"]'),
    top: -42,
    bottom: 38,
    outlineTop: -78,
    transition: 8,
  },
};
const statusElement = document.querySelector("[data-status]");
const titleElement = document.querySelector("[data-title]");

let lastStateAt = performance.now();

function readViewStyle(searchParams) {
  const bodyColor = readHexColor(searchParams.get("bodyColor"), "000000");
  const linesColor = readHexColor(readParam(searchParams, "linesColor", "LinesColor"), "ffffff");
  const linesOpacity = readPercentOpacity(readParam(searchParams, "linesOpac", "LinesOpac", "linesOpacity", "LinesOpacity"), 55) * linesColor.alpha;
  const buttonPressed = readHexColor(readParam(searchParams, "btnPressed", "buttonPressed"), "25a7ff");
  const buttonIdle = readIdleButtonColor(
    readParam(searchParams, "btnIdle", "buttonIdle"),
    readParam(searchParams, "btnIdleOpac", "buttonIdleOpacity"));
  const buttonPressedOpacity = readPercentOpacity(readParam(searchParams, "btnPressedOpac", "buttonPressedOpacity"), 100) * buttonPressed.alpha;
  const shine = readPercentOpacity(readParam(searchParams, "shine", "shinePct", "shinePercent"), 100);

  return {
    bodyOutline: readLineScale(readParam(searchParams, "bodyLines", "bodyOutline"), 10),
    innerBodyOutline: readLineScale(readParam(searchParams, "innerBodyLines", "innerBodyOutline"), 10),
    joystickOutline: readLineScale(readParam(searchParams, "joystickLines", "joystickOutline"), 10),
    buttonOutline: readLineScale(readParam(searchParams, "btnLines", "buttonOutline"), 10),
    backButtonOutline: readLineScale(readParam(searchParams, "backBtnLines", "backButtonOutline"), 10),
    bodyOpacity: readPercentOpacity(readParam(searchParams, "bodyOpac", "bodyOpacity"), 30) * bodyColor.alpha,
    triggerIdleOpacity: readPercentOpacity(readParam(searchParams, "triggerIdleOpac", "triggerIdleOpacity"), 0) * buttonIdle.alpha,
    triggerIdleOutlineOpacity: readPercentOpacity(readParam(searchParams, "triggerIdleLinesOpac", "triggerIdleOutlineOpacity"), 0),
    bodyColor,
    linesColor,
    linesOpacity,
    buttonIdle,
    buttonPressed,
    buttonPressedOpacity,
    shine,
  };
}

function applyViewStyle(style) {
  const rootStyle = document.documentElement.style;
  rootStyle.setProperty("--body-line-scale", style.bodyOutline.toFixed(3));
  rootStyle.setProperty("--outline-scale", style.buttonOutline.toFixed(3));
  rootStyle.setProperty("--joystick-line-scale", style.joystickOutline.toFixed(3));
  rootStyle.setProperty("--back-button-outline-scale", style.backButtonOutline.toFixed(3));
  rootStyle.setProperty("--body-color", style.bodyColor.css);
  rootStyle.setProperty("--body-opacity", style.bodyOpacity.toFixed(3));
  rootStyle.setProperty("--idle-line-color", style.linesColor.css);
  rootStyle.setProperty("--idle-line-opacity", style.linesOpacity.toFixed(3));
  rootStyle.setProperty("--button-idle", style.buttonIdle.css);
  rootStyle.setProperty("--button-idle-opacity", style.buttonIdle.opacity.toFixed(3));
  rootStyle.setProperty("--button-pressed", style.buttonPressed.css);
  rootStyle.setProperty("--button-pressed-rgb", style.buttonPressed.rgb);
  rootStyle.setProperty("--button-pressed-opacity", style.buttonPressedOpacity.toFixed(3));
  rootStyle.setProperty("--trigger-idle-opacity", style.triggerIdleOpacity.toFixed(3));
  rootStyle.setProperty("--trigger-idle-line-opacity", style.triggerIdleOutlineOpacity.toFixed(3));
  rootStyle.setProperty("--shine-opacity", style.shine.toFixed(3));
  applyShineFilterOpacity(style.shine);

  const art = document.querySelector("[data-controller-art]");
  if (art) {
    const artParams = new URLSearchParams({
      bodyLines: (style.bodyOutline * 10).toFixed(3),
      innerBodyLines: (style.innerBodyOutline * 10).toFixed(3),
      joystickLines: (style.joystickOutline * 10).toFixed(3),
      btnLines: (style.buttonOutline * 10).toFixed(3),
      linesColor: style.linesColor.hex,
      linesOpac: (style.linesOpacity * 100).toFixed(3),
      v: "20260618v3",
    });
    art.setAttribute("href", `/controller-art.svg?${artParams}`);
  }
}

function applyShineFilterOpacity(shine) {
  for (const flood of document.querySelectorAll("[data-shine-base]")) {
    const base = Number(flood.dataset.shineBase);
    if (Number.isFinite(base)) {
      flood.setAttribute("flood-opacity", (base * shine).toFixed(3));
    }
  }
}

function readStyleParams(searchParams, hash) {
  const merged = new URLSearchParams(searchParams);
  const fragment = normalizeParamSeparators(hash.replace(/^#/, ""));
  if (!fragment) {
    return merged;
  }

  const firstSeparator = fragment.indexOf("&");
  const firstFragment = firstSeparator >= 0 ? fragment.slice(0, firstSeparator) : fragment;
  const rest = firstSeparator >= 0 ? fragment.slice(firstSeparator + 1) : "";
  const rawColorTarget = lastEmptyColorParam(merged);

  if (rawColorTarget && looksLikeHexColor(firstFragment)) {
    merged.set(rawColorTarget, `#${firstFragment.replace(/^#/, "")}`);
    mergeParams(merged, rest);
  } else {
    mergeParams(merged, fragment);
  }

  return merged;
}

function readSearchParams(search) {
  return new URLSearchParams(normalizeParamSeparators(search.replace(/^\?/, "")));
}

function normalizeParamSeparators(value) {
  return value.replaceAll("___", "&");
}

function mergeParams(target, value) {
  if (!value) {
    return;
  }

  const parsed = new URLSearchParams(value);
  parsed.forEach((paramValue, key) => target.set(key, paramValue));
}

function readParam(searchParams, ...names) {
  for (const name of names) {
    if (searchParams.has(name)) {
      return searchParams.get(name);
    }
  }

  return null;
}

function lastEmptyColorParam(searchParams) {
  let target = null;
  const colorParams = new Set(["bodyColor", "linesColor", "LinesColor", "btnIdle", "buttonIdle", "btnPressed", "buttonPressed"]);
  searchParams.forEach((value, key) => {
    if (colorParams.has(key) && value === "") {
      target = key;
    }
  });
  return target;
}

function pressedParentFor(element) {
  if (element.classList.contains("trigger-fill") || element.classList.contains("trigger-outline-control")) {
    return pressedLayers.trigger;
  }

  if (element.classList.contains("back-button")) {
    return pressedLayers.back;
  }

  return pressedLayers.default;
}

function setPressedLayer(element, active) {
  const placement = controlPlacements.get(element);
  if (!placement) {
    return;
  }

  if (active) {
    const parent = placement.pressedParent;
    if (parent && element.parentNode !== parent) {
      parent.appendChild(element);
    }
    return;
  }

  if (element.parentNode !== placement.parent) {
    placement.parent.insertBefore(element, placement.marker);
  }
}

function readIdleButtonColor(value, opacityValue) {
  const color = readHexColor(value, "25a7ff");
  const normalized = normalizeHex(value);
  const explicitOpacity = opacityValue === null ? null : readPercentOpacity(opacityValue, 0);
  let opacity = 0;

  if (explicitOpacity !== null) {
    opacity = explicitOpacity * color.alpha;
  } else if (normalized && color.hasAlpha) {
    opacity = color.alpha;
  } else if (normalized && normalized !== "000000") {
    opacity = 0.35;
  }

  return { ...color, opacity };
}

function readHexColor(value, fallback) {
  const normalized = normalizeHex(value) ?? fallback;
  let hex = normalized;
  if (hex.length === 3 || hex.length === 4) {
    hex = [...hex].map((digit) => digit + digit).join("");
  }

  const hasAlpha = hex.length === 8;
  const rgbHex = hasAlpha ? hex.slice(0, 6) : hex;
  const alpha = hasAlpha ? parseInt(hex.slice(6, 8), 16) / 255 : 1;
  const r = parseInt(rgbHex.slice(0, 2), 16);
  const g = parseInt(rgbHex.slice(2, 4), 16);
  const b = parseInt(rgbHex.slice(4, 6), 16);

  return {
    css: `rgb(${r} ${g} ${b})`,
    rgb: `${r} ${g} ${b}`,
    hex: rgbHex,
    alpha,
    hasAlpha,
  };
}

function normalizeHex(value) {
  if (!value) {
    return null;
  }

  const hex = value.trim().replace(/^#/, "").toLowerCase();
  if (/^[0-9a-f]{3}$/.test(hex) || /^[0-9a-f]{4}$/.test(hex) || /^[0-9a-f]{6}$/.test(hex) || /^[0-9a-f]{8}$/.test(hex)) {
    return hex;
  }

  return null;
}

function looksLikeHexColor(value) {
  const hex = value.trim().replace(/^#/, "");
  return /^[0-9a-f]{3}$/i.test(hex) ||
    /^[0-9a-f]{4}$/i.test(hex) ||
    /^[0-9a-f]{6}$/i.test(hex) ||
    /^[0-9a-f]{8}$/i.test(hex);
}

function readNumber(value, fallback, min, max) {
  const normalized = value === null || value === undefined
    ? ""
    : String(value).trim().replace(/%$/, "");
  const number = Number(normalized);
  if (!Number.isFinite(number)) {
    return fallback;
  }

  return Math.min(max, Math.max(min, number));
}

function readLineScale(value, fallbackUnits) {
  return readNumber(value, fallbackUnits, 0, 40) / 10;
}

function readPercentOpacity(value, fallbackPercent) {
  return readNumber(value, fallbackPercent, 0, 100) / 100;
}

function setActive(name, active, strength = 1) {
  const elements = controls.get(name);
  const labels = controlLabels.get(name);
  const isActive = Boolean(active);

  if (elements) {
    for (const element of elements) {
      element.classList.toggle("active", isActive);
      element.style.setProperty("--press", clamp01(strength).toFixed(3));
      setPressedLayer(element, isActive);
    }
  }

  if (labels) {
    for (const label of labels) {
      label.classList.toggle("active", isActive);
      label.style.setProperty("--press", clamp01(strength).toFixed(3));
    }
  }
}

function setStick(name, x, y, pressed, touched = true) {
  const stick = sticks[name];
  if (!stick?.dot) {
    return;
  }

  const nx = clampSigned(x);
  const ny = clampSigned(y);
  const moved = Math.hypot(nx, ny) > 0.08;
  stick.dot.classList.toggle("touched", Boolean(touched) || moved);
  stick.dot.classList.toggle("moved", moved);
  stick.dot.classList.toggle("pressed", Boolean(pressed));
  stick.dot.setAttribute("transform", `translate(${(nx * 23).toFixed(2)} ${(ny * 23).toFixed(2)})`);
  setActive(name === "left" ? "left-stick-press" : "right-stick-press", pressed);
}

function setTouchpad(name, pad) {
  const view = pads[name];
  if (!view) {
    return;
  }

  const pressure = clamp01(pad?.pressure ?? 0);
  const touched = Boolean(pad?.touched) || pressure > 0.02;
  const trackerExpanded = pressure >= trackpadPressPressure;
  const clicked = touched && trackerExpanded;
  view.root?.classList.toggle("touched", touched);
  view.root?.classList.toggle("pressed", clicked);

  const normalizedX = clamp01(pad?.x ?? 0.5);
  const localX = view.x + (view.mirroredX ? 1 - normalizedX : normalizedX) * view.size;
  const localY = view.y + clamp01(pad?.y ?? 0.5) * view.size;
  const radius = 12 + (trackerExpanded ? pressure * 8 : 0);

  view.dot?.setAttribute("cx", localX.toFixed(2));
  view.dot?.setAttribute("cy", localY.toFixed(2));
  view.ring?.setAttribute("cx", localX.toFixed(2));
  view.ring?.setAttribute("cy", localY.toFixed(2));
  view.ring?.setAttribute("r", radius.toFixed(2));
}

function setTrigger(name, value) {
  const depth = clamp01(value);
  const controlName = name === "left" ? "lt" : "rt";
  const view = triggers[name];

  setActive(controlName, depth > 0.001, 1);

  if (!view?.fill || !view?.fade) {
    return;
  }

  const total = view.bottom - view.top;
  const filled = total * depth;
  const boundary = view.bottom - filled;
  const fadeHeight = depth <= 0 || depth >= 1 ? 0 : Math.min(view.transition, Math.max(0, boundary - view.top));
  const fadeY = boundary - fadeHeight;

  view.fill.setAttribute("y", boundary.toFixed(2));
  view.fill.setAttribute("height", filled.toFixed(2));
  view.fade.setAttribute("y", fadeY.toFixed(2));
  view.fade.setAttribute("height", fadeHeight.toFixed(2));

  if (view.outlineFill && view.outlineFade) {
    const fullPressPadding = depth > 0.97 ? (depth - 0.97) / 0.03 : 0;
    const outlineBoundary = Math.max(view.outlineTop, boundary - (36 * fullPressPadding));
    const outlineFilled = view.bottom - outlineBoundary;
    view.outlineFill.setAttribute("y", outlineBoundary.toFixed(2));
    view.outlineFill.setAttribute("height", outlineFilled.toFixed(2));
    view.outlineFade.setAttribute("y", fadeY.toFixed(2));
    view.outlineFade.setAttribute("height", fadeHeight.toFixed(2));
  }
}

function applyState(state) {
  lastStateAt = performance.now();
  document.body.classList.toggle("disconnected", !state.connected);

  if (titleElement && state.connected && state.name) {
    titleElement.textContent = friendlyName(state.name);
  }

  if (statusElement) {
    statusElement.textContent = statusText(state);
  }

  const b = state.buttons ?? {};
  const a = state.axes ?? {};
  const leftTrigger = clamp01(a.leftTrigger ?? 0);
  const rightTrigger = clamp01(a.rightTrigger ?? 0);

  setActive("a", b.a);
  setActive("b", b.b);
  setActive("x", b.x);
  setActive("y", b.y);
  setActive("view", b.view);
  setActive("menu", b.menu);
  setActive("steam", b.steam);
  setActive("touch-click", b.quickAccess);
  setActive("lb", b.leftBumper);
  setActive("rb", b.rightBumper);
  setTrigger("left", leftTrigger);
  setTrigger("right", rightTrigger);
  setActive("dpad-up", b.dpadUp);
  setActive("dpad-right", b.dpadRight);
  setActive("dpad-down", b.dpadDown);
  setActive("dpad-left", b.dpadLeft);
  setActive("dpad-outline", b.dpadUp || b.dpadRight || b.dpadDown || b.dpadLeft);
  setActive("lg2", b.leftGripUpper);
  setActive("lg1", b.leftGripLower);
  setActive("rg2", b.rightGripUpper);
  setActive("rg1", b.rightGripLower);
  setActive("left-grip-touch", b.leftGripTouch);
  setActive("right-grip-touch", b.rightGripTouch);

  setStick("left", a.leftStickX ?? 0, a.leftStickY ?? 0, b.leftStick, b.leftStickTouch ?? true);
  setStick("right", a.rightStickX ?? 0, a.rightStickY ?? 0, b.rightStick, b.rightStickTouch ?? true);
  setTouchpad("left", state.leftTouchpad);
  setTouchpad("right", state.rightTouchpad);
}

function statusText(state) {
  if (state.connected) {
    const vendor = state.vendorId ? state.vendorId.toString(16).padStart(4, "0") : "0000";
    const product = state.productId ? state.productId.toString(16).padStart(4, "0") : "0000";
    return `${friendlyName(state.name)} connected (${vendor}:${product})`;
  }

  return state.name || state.status || "Waiting for Steam Controller";
}

function friendlyName(name) {
  if (!name) {
    return "Steam Controller";
  }

  return name.replace(/\s*\(.*?\)\s*$/, "").trim();
}

function clamp01(value) {
  return Math.min(1, Math.max(0, Number(value) || 0));
}

function clampSigned(value) {
  return Math.min(1, Math.max(-1, Number(value) || 0));
}

function connectEvents() {
  const events = new EventSource("/events");
  events.onmessage = (event) => applyState(JSON.parse(event.data));
  events.onerror = () => {
    document.body.classList.add("disconnected");
    if (performance.now() - lastStateAt > 2500 && statusElement) {
      statusElement.textContent = "Waiting for local reader";
    }
  };
}

async function loadInitialState() {
  try {
    const response = await fetch("/api/state", { cache: "no-store" });
    if (response.ok) {
      applyState(await response.json());
    }
  } catch {
    document.body.classList.add("disconnected");
  }
}

loadInitialState();
connectEvents();
