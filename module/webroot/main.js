"use strict";
const MOD = "/data/adb/modules/phdroid";
const CMD_STATUS = `sh ${MOD}/service.sh status`;
const CMD_RETRY = `rm -f ${MOD}/blocked ${MOD}/insmod.crash ${MOD}/insmod.inflight; sh ${MOD}/service.sh`;
const CMD_CLEAR_LOG = `: > ${MOD}/phdroid.log`;
let seq = 0;
let busy = false;
function el(id) {
    const node = document.getElementById(id);
    if (!node) {
        throw new Error(`missing element #${id}`);
    }
    return node;
}
const out = el("log");
const sub = el("sub");
// 第三个参数是回调函数名，不是函数本身；回调挂在 window 上由 JSBridge 调用
function sh(command) {
    return new Promise((resolve) => {
        const bridge = window.ksu;
        if (!bridge || typeof bridge.exec !== "function") {
            resolve({ errno: -1, stdout: "", stderr: "WebUI API unavailable (KernelSU/APatch required)" });
            return;
        }
        const name = `ph_cb_${Date.now()}_${seq++}`;
        const holder = window;
        let timer = 0;
        let done = false;
        const finish = (errno, stdout, stderr) => {
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
        }
        catch (e) {
            finish(-1, "", String(e));
            return;
        }
        timer = window.setTimeout(() => finish(-1, "", "timeout after 8s"), 8000);
    });
}
function parseStatus(text) {
    const split = text.indexOf("---log---");
    const head = split >= 0 ? text.slice(0, split) : text;
    const log = split >= 0 ? text.slice(split + 9) : "";
    const map = {};
    const params = [];
    const cparams = [];
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
        }
        else if (key.indexOf("param.") === 0) {
            params.push(`${key.slice(6)} = ${value}`);
        }
        else {
            map[key] = value;
        }
    });
    return { map, params, cparams, log: log.trim() };
}
function setBadge(id, text, cls) {
    const node = el(id);
    node.textContent = text;
    node.className = cls ? `badge ${cls}` : "badge";
}
function render(text) {
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
    }
    else if (wanted === "missing") {
        compatText = "包内缺失";
        compatCls = "warn";
    }
    else if (wanted === "on") {
        compatText = "未加载";
        compatCls = "warn";
    }
    setBadge("compat", compatText, compatCls);
    setBadge("blocked", blocked ? "是（上次 insmod 崩溃）" : "否", blocked ? "warn" : "on");
    el("kernel").textContent = m.kernel || "–";
    el("kmi").textContent = m.kmi || "–";
    el("device").textContent = `${m.brand || "?"} ${m.model || ""}`.trim();
    sub.textContent = `${m.version || ""} · droid_lkm · ${m.manager || ""}`;
    let params = status.params.join("\n");
    if (status.cparams.length) {
        params += `${params ? "\n\n" : ""}— 兼容模块 —\n${status.cparams.join("\n")}`;
    }
    el("params").textContent = params || "（模块未加载）";
    out.textContent = status.log || "（空）";
}
function syncButtons() {
    ["btn-refresh", "btn-load", "btn-log"].forEach((id) => {
        el(id).disabled = busy;
    });
}
async function withBusy(label, task) {
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
    }
    finally {
        busy = false;
        syncButtons();
    }
}
async function fetchStatus() {
    const result = await sh(CMD_STATUS);
    if (result.errno !== 0 && !result.stdout) {
        return `读取失败 (errno ${result.errno})\n${result.stderr}`;
    }
    return result.stdout;
}
function refresh() {
    return withBusy("", async () => {
        render(await fetchStatus());
    });
}
el("btn-refresh").addEventListener("click", () => {
    void refresh();
});
el("btn-load").addEventListener("click", () => {
    void withBusy("解除阻断并重新加载 …", async () => {
        await sh(CMD_RETRY);
        render(await fetchStatus());
    });
});
el("btn-log").addEventListener("click", () => {
    void withBusy("清空日志 …", async () => {
        await sh(CMD_CLEAR_LOG);
        render(await fetchStatus());
    });
});
void refresh();
