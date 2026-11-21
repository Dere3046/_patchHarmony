// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import type { LogKind, ResolvedFlavor, ScenarioKey, StatusSnapshot } from '../types';
import { mockBackend } from './mock';
import { parseStatusOutput } from './parser';

interface KsuInterface {
  exec(cmd: string, options: string, callback: string): void;
  moduleInfo?(): string;
}

declare global {
  interface Window {
    ksu?: KsuInterface;
    [key: string]: unknown;
  }
}

const DATA_DIR = '/data/adb/phdroid';
const LOG_MAX = 262144;
let cbSeq = 0;

export function isNativeHost(): boolean {
  return typeof window !== 'undefined' && typeof window.ksu?.exec === 'function';
}

function shQuote(val: string): string {
  return "'" + String(val).replace(/'/g, "'\\''") + "'";
}

function resolveModDirHint(): string {
  try {
    if (typeof window.ksu?.moduleInfo === 'function') {
      const parsed = JSON.parse(window.ksu.moduleInfo()) as { moduleDir?: string };
      if (parsed && parsed.moduleDir) return parsed.moduleDir;
    }
  } catch {
    // APatch does not implement moduleInfo()
  }
  return '/data/adb/modules/phdroid';
}

function buildShellContext(body: string): string {
  const hint = shQuote(resolveModDirHint());
  return [
    `MODDIR=${hint}`,
    `DATA=${DATA_DIR}`,
    'if [ ! -f "$MODDIR/service.sh" ]; then',
    '  for d in /data/adb/modules/phdroid /data/adb/ksu/modules/phdroid /data/adb/ap/modules/phdroid; do',
    '    if [ -f "$d/service.sh" ]; then MODDIR=$d; break; fi',
    '  done',
    'fi',
    'export MODDIR DATA',
    body
  ].join('\n');
}

function appendLogShell(msg: string): string {
  const q = shQuote(msg);
  return [
    'mkdir -p "$DATA"',
    `if [ -f "$DATA/phdroid.log" ]; then sz=$(wc -c < "$DATA/phdroid.log" 2>/dev/null); [ "\${sz:-0}" -gt ${LOG_MAX} ] && rm -f "$DATA/phdroid.log"; fi`,
    `printf '%s %s\\n' "$(date '+%m-%d %H:%M:%S')" ${q} >> "$DATA/phdroid.log"`
  ].join('\n');
}

function execRoot(cmd: string): Promise<{ code: number; stdout: string; stderr: string }> {
  return new Promise((resolve, reject) => {
    const ksu = window.ksu;
    if (!ksu || typeof ksu.exec !== 'function') {
      reject(new Error('ksu.exec unavailable'));
      return;
    }
    const cbName = `__phdroid_cb_${++cbSeq}`;
    window[cbName] = (code: unknown, stdout: unknown, stderr: unknown) => {
      delete window[cbName];
      resolve({
        code: Number(code),
        stdout: String(stdout ?? ''),
        stderr: String(stderr ?? '')
      });
    };
    try {
      ksu.exec(cmd, '{}', cbName);
    } catch (err) {
      delete window[cbName];
      reject(err);
    }
  });
}

async function runRootShell(body: string): Promise<string> {
  const res = await execRoot(buildShellContext(body));
  if (res.code !== 0) {
    throw new Error(res.stderr.trim() || res.stdout.trim() || `exit ${res.code}`);
  }
  return res.stdout;
}

export async function fetchStatus(): Promise<StatusSnapshot> {
  if (!isNativeHost()) {
    return mockBackend.getStatus();
  }
  const script = [
    'mkdir -p "$DATA"',
    'if ! grep -q "^modules=[^[:space:]]" "$DATA/config.conf" 2>/dev/null; then',
    '  if grep -q "^modules=[^[:space:]]" "$MODDIR/config.conf" 2>/dev/null; then',
    '    cp -f "$MODDIR/config.conf" "$DATA/config.conf" && chmod 0644 "$DATA/config.conf"',
    '  else',
    '    MODS=""',
    '    FIRST_KMI=""',
    '    for d in "$MODDIR"/bin/*; do',
    '      [ -d "$d" ] || continue',
    '      FIRST_KMI=${d##*/}',
    '      break',
    '    done',
    '    for f in "$MODDIR"/bin/"$FIRST_KMI"/droid_lkm.ko; do [ -f "$f" ] && MODS="droid_lkm"; break; done',
    '    for f in "$MODDIR"/bin/"$FIRST_KMI"/droid_lkm_vendor.ko; do [ -f "$f" ] && { MODS="${MODS:+$MODS }droid_lkm_vendor"; break; }; done',
    '    if [ -n "$MODS" ]; then',
    '      printf "modules=%s\\n" "$MODS" > "$DATA/config.conf" && chmod 0644 "$DATA/config.conf"',
    '    fi',
    '  fi',
    'fi',
    'CFG=""',
    'if [ -f "$DATA/config.conf" ]; then CFG="$DATA/config.conf"; elif [ -f "$MODDIR/config.conf" ]; then CFG="$MODDIR/config.conf"; fi',
    'REL=$(uname -r)',
    'VER=$(printf "%s" "$REL" | sed -n "s/^\\([0-9][0-9]*\\.[0-9][0-9]*\\).*/\\1/p")',
    'TAG=$(printf "%s" "$REL" | sed -n "s/.*\\(android[0-9][0-9]*\\).*/\\1/p")',
    'KMI=""; [ -n "$VER" ] && [ -n "$TAG" ] && KMI="$TAG-$VER"',
    'CFG_KMI=""',
    '[ -n "$CFG" ] && [ -f "$CFG" ] && CFG_KMI=$(sed -n "s/^kmi=//p" "$CFG" 2>/dev/null | tail -n 1)',
    'KO=""',
    'if [ -n "$CFG_KMI" ] && [ -f "$MODDIR/bin/$CFG_KMI/droid_lkm.ko" ]; then KO="$MODDIR/bin/$CFG_KMI/droid_lkm.ko"; fi',
    'if [ -z "$KO" ] && [ -n "$KMI" ] && [ -f "$MODDIR/bin/$KMI/droid_lkm.ko" ]; then KO="$MODDIR/bin/$KMI/droid_lkm.ko"; fi',
    'if [ -z "$KO" ] && [ -n "$VER" ]; then for f in "$MODDIR"/bin/*-"$VER"/droid_lkm.ko; do [ -f "$f" ] && { KO=$f; break; }; done; fi',
    'if [ -z "$KO" ]; then for f in "$MODDIR"/bin/*/droid_lkm.ko; do [ -f "$f" ] && { KO=$f; break; }; done; fi',
    'BACKENDS="unknown"',
    'if [ -f "$KO" ]; then',
    '  BTAG=$(grep -aoE "pid_backends=(main|native|both)" "$KO" 2>/dev/null | head -n 1 | cut -d= -f2)',
    '  if [ -n "$BTAG" ]; then',
    '    BACKENDS=$BTAG',
    '  else',
    '    HAS_N=0; HAS_M=1',
    '    grep -aqE "pidns native ready|droid_native_pidns_init" "$KO" 2>/dev/null && HAS_N=1',
    '    grep -aq "pid_impl=main is not built" "$KO" 2>/dev/null && HAS_M=0',
    '    if [ "$HAS_N" -eq 1 ] && [ "$HAS_M" -eq 1 ]; then BACKENDS="both"; elif [ "$HAS_N" -eq 1 ]; then BACKENDS="native"; else BACKENDS="main"; fi',
    '  fi',
    'fi',
    '[ -f "$DATA/auto" ] && AUTO=1 || AUTO=0',
    '[ -f "$DATA/blocked" ] && BLOCKED=1 || BLOCKED=0',
    '[ -f "$DATA/insmod" ] && MARK=1 || MARK=0',
    'grep -q "[[:space:]]mqueue$" /proc/filesystems 2>/dev/null && MQ=1 || MQ=0',
    'echo "###PHDROID:meta###"',
    'printf "release=%s\\n" "$REL"',
    'printf "kmi=%s\\n" "$KMI"',
    'printf "pid_backends=%s\\n" "$BACKENDS"',
    'printf "moddir=%s\\n" "$MODDIR"',
    'printf "data=%s\\n" "$DATA"',
    'printf "auto=%s\\n" "$AUTO"',
    'printf "blocked=%s\\n" "$BLOCKED"',
    'printf "insmod_mark=%s\\n" "$MARK"',
    'printf "mqueue_fs=%s\\n" "$MQ"',
    'echo "###PHDROID:kmis###"',
    'for d in "$MODDIR"/bin/*; do [ -d "$d" ] && printf "%s\\n" "${d##*/}"; done',
    'echo "###PHDROID:kos###"',
    'for f in "$MODDIR"/bin/*/*.ko; do [ -f "$f" ] && printf "%s\\n" "${f##*/}"; done | sort -u',
    'echo "###PHDROID:modules###"',
    'grep -E "^droid_lkm(_vendor|_misc)? " /proc/modules 2>/dev/null || true',
    'echo "###PHDROID:config###"',
    '[ -n "$CFG" ] && [ -f "$CFG" ] && cat "$CFG"',
    'echo "###PHDROID:params_droid_lkm###"',
    'for p in /sys/module/droid_lkm/parameters/*; do [ -e "$p" ] && printf "%s=%s\\n" "${p##*/}" "$(cat "$p" 2>/dev/null)"; done',
    'echo "###PHDROID:params_droid_lkm_vendor###"',
    'for p in /sys/module/droid_lkm_vendor/parameters/*; do [ -e "$p" ] && printf "%s=%s\\n" "${p##*/}" "$(cat "$p" 2>/dev/null)"; done',
    'echo "###PHDROID:features###"',
    '[ -r /proc/droid_lkm/features ] && cat /proc/droid_lkm/features 2>/dev/null || true'
  ].join('\n');
  const out = await runRootShell(script);
  return parseStatusOutput(out);
}

export async function setAutoBoot(enable: boolean): Promise<void> {
  if (!isNativeHost()) {
    mockBackend.setAuto(enable);
    return;
  }
  const op = enable ? ': > "$DATA/auto"' : 'rm -f "$DATA/auto"';
  const msg = enable ? '[ctl] auto enabled' : '[ctl] auto disabled';
  await runRootShell([op, appendLogShell(msg)].join('\n'));
}

export async function clearBootBlock(): Promise<void> {
  if (!isNativeHost()) {
    mockBackend.unblock();
    return;
  }
  await runRootShell(['rm -f "$DATA/blocked" "$DATA/insmod"', appendLogShell('[ctl] block cleared')].join('\n'));
}

export async function saveConfigText(configText: string): Promise<void> {
  if (!isNativeHost()) {
    mockBackend.saveConfig(configText);
    return;
  }
  const quoted = shQuote(configText);
  await runRootShell(
    [
      'mkdir -p "$DATA"',
      'TMP="$DATA/config.conf.tmp.$$"',
      `printf '%s\\n' ${quoted} > "$TMP"`,
      'grep -q "^modules=" "$TMP" 2>/dev/null || { rm -f "$TMP"; echo "invalid config missing modules" >&2; exit 1; }',
      'chmod 0644 "$TMP" && mv -f "$TMP" "$DATA/config.conf"',
      appendLogShell('[ctl] config saved')
    ].join('\n')
  );
}

export async function installModules(overrideConfigText?: string): Promise<void> {
  if (!isNativeHost()) {
    mockBackend.install(overrideConfigText);
    return;
  }
  if (!overrideConfigText) {
    await runRootShell('sh "$MODDIR/service.sh" install');
    return;
  }
  const quoted = shQuote(overrideConfigText);
  await runRootShell(
    [
      'mkdir -p "$DATA"',
      'TMP="$DATA/.webui_tmp.conf"',
      `printf '%s\\n' ${quoted} > "$TMP"`,
      'sh "$MODDIR/service.sh" install "$TMP"',
      'rc=$?',
      'rm -f "$TMP"',
      'exit $rc'
    ].join('\n')
  );
}

export async function fetchLogs(kind: LogKind): Promise<string[]> {
  if (!isNativeHost()) {
    return mockBackend.getLogs(kind);
  }
  const cmd =
    kind === 'module'
      ? '[ -f "$DATA/phdroid.log" ] && tail -n 500 "$DATA/phdroid.log" || true'
      : 'dmesg 2>/dev/null | tail -n 500 || true';
  const out = await runRootShell(cmd);
  if (!out.trim()) return [];
  return out.replace(/\r?\n$/, '').split(/\r?\n/);
}

export async function clearModuleLogs(): Promise<void> {
  if (!isNativeHost()) {
    mockBackend.clearModuleLogs();
    return;
  }
  await runRootShell('mkdir -p "$DATA" && : > "$DATA/phdroid.log"');
}

export function setMockScenario(s: ScenarioKey) {
  mockBackend.setScenario(s);
}

export function getMockScenario(): ScenarioKey {
  return mockBackend.getScenario();
}

export function setMockFlavor(f: ResolvedFlavor) {
  mockBackend.setFlavor(f);
}

export function getMockFlavor(): ResolvedFlavor {
  return mockBackend.getFlavor();
}
