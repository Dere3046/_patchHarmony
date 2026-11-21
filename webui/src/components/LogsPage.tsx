// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { getLangStrings } from '../i18n';
import type { Lang, LogKind } from '../types';
import { MduiFilterChip, MduiTabs } from './MduiControls';

interface LogsPageProps {
  lang: Lang;
  logTab: LogKind;
  moduleLogs: string[];
  dmesgLogs: string[];
  searchQuery: string;
  onlyPhdroid: boolean;
  onlyWarnErr: boolean;
  busy: boolean;
  onSwitchLogTab: (tab: LogKind) => void;
  onSearchChange: (q: string) => void;
  onToggleOnlyPhdroid: (selected: boolean) => void;
  onToggleOnlyWarnErr: (selected: boolean) => void;
  onCopyLogs: (text: string) => void;
  onClearModuleLogs: () => void;
}

function logLevel(line: string): 'err' | 'warn' | 'ok' | 'info' {
  if (/\[E\]/.test(line)) return 'err';
  if (/\[W\]/.test(line)) return 'warn';
  if (/\[[ID]\]/.test(line)) {
    return /ready|loaded|installed|patched/i.test(line) ? 'ok' : 'info';
  }
  if (/\b(fail|failed|error|panic|refused|blocked|fatal|oops)\b|BUG:/i.test(line)) return 'err';
  if (/\b(warn|warning)\b|WARNING:/i.test(line)) return 'warn';
  if (/\b(ready|loaded|installed|patched)\b/i.test(line)) return 'ok';
  return 'info';
}

function lineClass(line: string): string {
  const lvl = logLevel(line);
  if (lvl === 'err') return 'log-line-err';
  if (lvl === 'warn') return 'log-line-warn';
  if (lvl === 'ok') return 'log-line-ok';
  return '';
}

export function LogsPage({
  lang,
  logTab,
  moduleLogs,
  dmesgLogs,
  searchQuery,
  onlyPhdroid,
  onlyWarnErr,
  busy,
  onSwitchLogTab,
  onSearchChange,
  onToggleOnlyPhdroid,
  onToggleOnlyWarnErr,
  onCopyLogs,
  onClearModuleLogs
}: LogsPageProps) {
  const dict = getLangStrings(lang);
  const q = searchQuery.trim().toLowerCase();

  let lines = logTab === 'module' ? [...moduleLogs] : [...dmesgLogs];
  if (logTab === 'dmesg' && onlyPhdroid) {
    lines = lines.filter((l) => /droid_lkm|kallrecon|hookern|kerncall|type_info/i.test(l));
  }
  if (onlyWarnErr) {
    lines = lines.filter((l) => {
      const lvl = logLevel(l);
      return lvl === 'err' || lvl === 'warn';
    });
  }
  if (q) {
    lines = lines.filter((l) => l.toLowerCase().includes(q));
  }

  return (
    <>
      <div className="card">
        <div className="logs-tabs-wrap">
          <MduiTabs
            value={logTab}
            variant="secondary"
            fullWidth
            onChange={(next) => onSwitchLogTab(next as LogKind)}
          >
            <mdui-tab value="module">{dict.tabModuleLog}</mdui-tab>
            <mdui-tab value="dmesg">{dict.tabKernelLog}</mdui-tab>
          </MduiTabs>
        </div>

        <div className="log-toolbar">
          <input
            type="text"
            className="text-input"
            style={{ flex: 1, minWidth: '160px' }}
            placeholder={dict.logSearchPlaceholder}
            value={searchQuery}
            onChange={(e) => onSearchChange(e.target.value)}
          />
          <mdui-button variant="tonal" onClick={() => onCopyLogs(lines.join('\n'))}>
            {dict.copyBtn}
          </mdui-button>
          {logTab === 'module' && (
            <mdui-button variant="outlined" disabled={busy} onClick={onClearModuleLogs}>
              {dict.clearBtn}
            </mdui-button>
          )}
        </div>

        <div className="log-toolbar" style={{ marginTop: '10px' }}>
          <MduiFilterChip
            selected={onlyPhdroid}
            visible={logTab === 'dmesg'}
            onToggle={onToggleOnlyPhdroid}
          >
            {dict.onlyModuleFilter}
          </MduiFilterChip>
          <MduiFilterChip selected={onlyWarnErr} onToggle={onToggleOnlyWarnErr}>
            {dict.onlyWarnErrFilter}
          </MduiFilterChip>
        </div>
      </div>

      <div className="log-box">
        {!lines.length ? (
          dict.emptyLog
        ) : (
          lines.map((line, i) => (
            <div key={i} className={lineClass(line)}>
              {line}
            </div>
          ))
        )}
      </div>
    </>
  );
}
