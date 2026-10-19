interface ExecResult {
  errno: number;
  stdout: string;
  stderr: string;
}

type ExecCallback = (errno: number, stdout: string, stderr: string) => void;

interface KsuBridge {
  // KernelSU: exec(command, optionsJson, callbackName)
  exec(command: string, options: string, callback: string): void;
}

interface Window {
  ksu?: KsuBridge;
}

interface DeviceStatus {
  map: Record<string, string>;
  params: string[];
  cparams: string[];
  log: string;
}

const MOD = "/data/adb/modules/phdroid";
const CMD_STATUS = `sh ${MOD}/service.sh status`;
const CMD_RETRY = `rm -f ${MOD}/blocked ${MOD}/insmod.crash ${MOD}/insmod.inflight; sh ${MOD}/service.sh`;
const CMD_CLEAR_LOG = `: > ${MOD}/phdroid.log`;

let seq = 0;
let busy = false;

function el<T extends HTMLElement>(id: string): T {
  const node = document.getElementById(id);
  if (!node) {
    throw new Error(`missing element #${id}`);
  }
  return node as T;
}

const out = el<HTMLPreElement>("log");
const sub = el<HTMLElement>("sub");

// 第三个参数是回调函数名，不是函数本身；回调挂在 window 上由 JSBridge 调用
function sh(command: string): Promise<ExecResult> {
  return new Promise<ExecResult>((resolve) => {
    const bridge = window.ksu;
    if (!bridge || typeof bridge.exec !== "function") {
      resolve({ errno: -1, stdout: "", stderr: "WebUI API unavailable (KernelSU/APatch required)" });
      return;
    }

    const name = `ph_cb_${Date.now()}_${seq++}`;
    const holder = window as unknown as Record<string, ExecCallback>;
    let timer = 0;
    let done = false;

    const finish: ExecCallback = (errno, stdout, stderr) => {
      if (done) {
        return;
      }
      done = true;
      if (timer) {
        window.clearTimeout(timer);
      }
      delete holder[name];
      resolve({ errno: errno | 0, stdout: stdout || "", stderr: stderr || "" });
    };

    holder[name] = finish;
    try {
      bridge.exec(command, "{}", name);
    } catch (e) {
      finish(-1, "", String(e));
      return;
    }
    timer = window.setTimeout(() => finish(-1, "", "timeout after 8s"), 8000);
  });
}

function parseStatus(text: string): DeviceStatus {
  const split = text.indexOf("---log---");
  const head = split >= 0 ? text.slice(0, split) : text;
  const log = split >= 0 ? text.slice(split + 9) : "";
  const map: Record<string, string> = {};
  const params: string[] = [];
  const cparams: string[] = [];

  head.split("\n").forEach((raw) => {
    const line = raw.trim();
    if (!line) {
      return;
    }
    const eq = line.indexOf("=");
    if (eq < 0) {
      return;
    }
    const key = line.slice(0, eq);
    const value = line.slice(eq + 1);
    if (key.indexOf("cparam.") === 0) {
      cparams.push(`${key.slice(7)} = ${value}`);
    } else if (key.indexOf("param.") === 0) {
      params.push(`${key.slice(6)} = ${value}`);
    } else {
      map[key] = value;
    }
  });

  return { map, params, cparams, log: log.trim() };
}

function setBadge(id: string, text: string, cls: string): void {
  const node = el<HTMLSpanElement>(id);
  node.textContent = text;
  node.className = cls ? `badge ${cls}` : "badge";
}

function render(text: string): void {
  const status = parseStatus(text);
  const m = status.map;

  const loaded = m.loaded === "1";
  const blocked = m.blocked === "1";
  const failed = !loaded && !blocked && (m.last || "").indexOf("fail") === 0;
  const oplus = m.oplus === "1";
  const wanted = m.compat_wanted || "off";

  const stateText = loaded ? "已加载" : blocked ? "已阻断" : failed ? "加载失败" : "未加载";
  setBadge("state", stateText, loaded ? "on" : blocked || failed ? "warn" : "off");

  setBadge("oplus", oplus ? "是" : "否", oplus ? "on" : "");

  let compatText = "不需要";
  let compatCls = "";
  if (m.compat === "1") {
    compatText = "已加载";
    compatCls = "on";
  } else if (wanted === "missing") {
    compatText = "包内缺失";
    compatCls = "warn";
  } else if (wanted === "on") {
    compatText = "未加载";
    compatCls = "warn";
  }
  setBadge("compat", compatText, compatCls);
  setBadge("blocked", blocked ? "是（上次 insmod 崩溃）" : "否", blocked ? "warn" : "on");

  el<HTMLElement>("kernel").textContent = m.kernel || "–";
  el<HTMLElement>("kmi").textContent = m.kmi || "–";
  el<HTMLElement>("device").textContent = `${m.brand || "?"} ${m.model || ""}`.trim();
  sub.textContent = `${m.version || ""} · droid_lkm · ${m.manager || ""}`;

  let params = status.params.join("\n");
  if (status.cparams.length) {
    params += `${params ? "\n\n" : ""}— 兼容模块 —\n${status.cparams.join("\n")}`;
  }
  el<HTMLElement>("params").textContent = params || "（模块未加载）";
  out.textContent = status.log || "（空）";
}

function syncButtons(): void {
  ["btn-refresh", "btn-load", "btn-log"].forEach((id) => {
    el<HTMLButtonElement>(id).disabled = busy;
  });
}

async function withBusy(label: string, task: () => Promise<void>): Promise<void> {
  if (busy) {
    return;
  }
  busy = true;
  syncButtons();
  if (label) {
    out.textContent = label;
  }
  try {
    await task();
  } finally {
    busy = false;
    syncButtons();
  }
}

async function fetchStatus(): Promise<string> {
  const result = await sh(CMD_STATUS);
  if (result.errno !== 0 && !result.stdout) {
    return `读取失败 (errno ${result.errno})\n${result.stderr}`;
  }
  return result.stdout;
}

function refresh(): Promise<void> {
  return withBusy("", async () => {
    render(await fetchStatus());
  });
}

el<HTMLButtonElement>("btn-refresh").addEventListener("click", () => {
  void refresh();
});

el<HTMLButtonElement>("btn-load").addEventListener("click", () => {
  void withBusy("解除阻断并重新加载 …", async () => {
    await sh(CMD_RETRY);
    render(await fetchStatus());
  });
});

el<HTMLButtonElement>("btn-log").addEventListener("click", () => {
  void withBusy("清空日志 …", async () => {
    await sh(CMD_CLEAR_LOG);
    render(await fetchStatus());
  });
});

void refresh();
