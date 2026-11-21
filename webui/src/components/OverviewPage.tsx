// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { parseSavedConfig } from '../api/parser';
import { getLangStrings, isLayoutOkState } from '../i18n';
import type { Lang, StatusSnapshot } from '../types';
import { HoldButton } from './HoldButton';
import { MduiSegmentedGroup, MduiSwitch } from './MduiControls';
import { SpringCard } from './SpringCard';

interface OverviewPageProps {
  lang: Lang;
  snapshot: StatusSnapshot;
  busy: boolean;
  installPulse: number;
  selectedKmi: string;
  savedKmi: string;
  kmiConfirmed: boolean;
  onSelectKmi: (kmi: string) => void;
  onConfirmKmi: (kmi: string) => void;
  onToggleAuto: (enable: boolean) => void;
  onUnblock: () => void;
  onInstallNow: () => void;
  onShortTapInstall: () => void;
  onGoConfig: () => void;
}

function ownerBadge(owner: string, lang: Lang): { text: string; cls: string } {
  const b = getLangStrings(lang).badges;
  if (owner === 'module') return { text: b.module, cls: 'chip-ok' };
  if (owner === 'kernel') return { text: b.kernel, cls: 'chip-info' };
  return { text: b.absent, cls: 'chip-muted' };
}

function stateBadgeClass(kind: string): string {
  if (kind === 'installed' || isLayoutOkState(kind)) return 'chip-ok';
  if (kind === 'refused' || kind === 'unverified') return 'chip-err';
  return 'chip-muted';
}

export function OverviewPage({
  lang,
  snapshot,
  busy,
  installPulse,
  selectedKmi,
  savedKmi,
  kmiConfirmed,
  onSelectKmi,
  onConfirmKmi,
  onToggleAuto,
  onUnblock,
  onInstallNow,
  onShortTapInstall,
  onGoConfig
}: OverviewPageProps) {
  const dict = getLangStrings(lang);
  const zh = lang === 'zh';
  const f = snapshot.features;

  let heroCls = 'tonal';
  let heroKey: keyof typeof dict.hero = 'full';
  if (snapshot.meta.blocked) {
    heroCls = 'hero-err';
    heroKey = 'blocked';
  } else if (!snapshot.loaded || !f) {
    heroCls = 'tonal';
    heroKey = 'unloaded';
  } else if (!f.layoutTrusted) {
    heroCls = 'hero-warn';
    heroKey = 'untrusted';
  } else if (f.runtime.criticalHooks !== 'ok' || f.runtime.moduleReady !== 'yes') {
    heroCls = 'hero-warn';
    heroKey = 'degraded';
  } else {
    heroCls = 'hero-ok';
    heroKey = 'full';
  }

  const loadedMods: { mod: string; detail?: string }[] = [];
  if (snapshot.loaded) {
    const rtBack = f?.runtime.pidnsBackends || snapshot.meta.pidBackends;
    const rtDetail =
      rtBack === 'native'
        ? 'native'
        : rtBack === 'both'
          ? `both/${f?.runtime.pidnsImpl || 'main'}`
          : undefined;
    loadedMods.push({ mod: 'droid_lkm', detail: rtDetail });
  }
  if (snapshot.vendorLoaded) {
    loadedMods.push({ mod: 'droid_lkm_vendor' });
  }

  const parsedSaved = parseSavedConfig(snapshot.savedConfigText);
  const displayKmi = snapshot.meta.kmi || parsedSaved.kmi || snapshot.kmis[0] || 'unknown';
  const savedPills: { mod: string; val: string }[] = [];
  parsedSaved.modules.forEach((m) => {
    const rawLine = parsedSaved.raw[m] || '';
    const pairs = rawLine.split(/\s+/).filter(Boolean);
    if (!pairs.length) {
      savedPills.push({ mod: m, val: dict.defaultParamsTag });
    } else {
      pairs.forEach((p) => {
        if (p.includes('=')) savedPills.push({ mod: m, val: p });
      });
    }
  });

  const idleStageVals = [
    dict.stageVals.wait,
    dict.stageVals.unprobed,
    dict.stageVals.uninstalled,
    dict.stageVals.nsOff,
    dict.stageVals.slotOff
  ];

  const stages = f
    ? [
        {
          name: dict.stages[0],
          val: f.runtime.moduleReady === 'yes' ? dict.stageVals.ready : dict.stageVals.bad,
          cls: f.runtime.moduleReady === 'yes' ? 'done' : 'fail'
        },
        {
          name: dict.stages[1],
          val: f.layoutTrusted ? dict.stageVals.trusted : dict.stageVals.untrusted,
          cls: f.layoutTrusted ? 'done' : 'fail'
        },
        {
          name: dict.stages[2],
          val: f.runtime.criticalHooks === 'ok' ? dict.stageVals.ok : dict.stageVals.degraded,
          cls: f.runtime.criticalHooks === 'ok' ? 'done' : 'warn'
        },
        {
          name: dict.stages[3],
          val:
            f.runtime.pidns === 'ready' && f.runtime.ipcns === 'ready'
              ? dict.stageVals.nsAll
              : f.runtime.pidns === 'ready' || f.runtime.ipcns === 'ready'
                ? dict.stageVals.nsPartial
                : dict.stageVals.nsOff,
          cls: f.runtime.pidns === 'ready' && f.runtime.ipcns === 'ready' ? 'done' : 'warn'
        },
        {
          name: dict.stages[4],
          val: f.runtime.syscallSlots === 'ready' ? dict.stageVals.slotOk : dict.stageVals.slotOff,
          cls: f.runtime.syscallSlots === 'ready' ? 'done' : 'fail'
        }
      ]
    : dict.stages.map((name, i) => ({ name, val: idleStageVals[i], cls: '' }));

  const doneCount = stages.filter((st) => st.cls === 'done').length;
  const summaryText =
    !snapshot.loaded || !f
      ? dict.notLoaded
      : zh
        ? `${doneCount}/5 就绪`
        : `${doneCount}/5 ready`;
  const summaryCls =
    !snapshot.loaded || !f
      ? 'chip-muted'
      : doneCount === 5
        ? 'chip-ok'
        : doneCount >= 3
          ? 'chip-warn'
          : 'chip-err';

  const subsystemRows = f
    ? [
        {
          title: dict.subsystems.pidns,
          sub:
            f.runtime.pidns === 'ready'
              ? zh
                ? `后端 ${f.runtime.pidnsImpl}`
                : `backend ${f.runtime.pidnsImpl}`
              : zh
                ? '未启用'
                : 'disabled',
          badge:
            f.runtime.pidns === 'ready'
              ? { text: dict.badges.active, cls: 'chip-ok' }
              : { text: dict.badges.off, cls: 'chip-muted' }
        },
        {
          title: dict.subsystems.ipcns,
          sub:
            f.runtime.ipcns === 'ready'
              ? f.capabilities.ipc_ns.owner === 'kernel'
                ? zh
                  ? '复用内核对象'
                  : 'kernel owned'
                : zh
                  ? '模块独立维护'
                  : 'module owned'
              : zh
                ? '未启动'
                : 'inactive',
          badge:
            f.runtime.ipcns === 'ready'
              ? ownerBadge(f.capabilities.ipc_ns.owner, lang)
              : { text: dict.badges.off, cls: 'chip-muted' }
        },
        {
          title: dict.subsystems.sysvipc,
          sub:
            f.capabilities.sysvipc.owner === 'module'
              ? zh
                ? '模块补齐调用与对象表'
                : 'module syscalls & tables'
              : f.capabilities.sysvipc.owner === 'kernel'
                ? zh
                  ? '内核自带支持'
                  : 'kernel built-in'
                : zh
                  ? '布局未校验'
                  : 'unverified',
          badge: ownerBadge(f.capabilities.sysvipc.owner, lang)
        },
        {
          title: dict.subsystems.mqueue,
          sub:
            f.runtime.mqueueFs === 'up'
              ? zh
                ? '文件系统已挂载'
                : 'mqueuefs mounted'
              : f.capabilities.posix_mqueue.owner === 'kernel'
                ? zh
                  ? '内核自带支持'
                  : 'kernel built-in'
                : zh
                  ? '未挂载或关闭'
                  : 'not mounted',
          badge:
            f.runtime.mqueueFs === 'up'
              ? { text: dict.badges.module, cls: 'chip-ok' }
              : ownerBadge(f.capabilities.posix_mqueue.owner, lang)
        },
        {
          title: dict.subsystems.procNspid,
          sub: `${
            f.runtime.procNs === 'ready' ? (zh ? '链接已接管' : 'ns links ok') : zh ? '链接关闭' : 'ns links off'
          }  |  ${
            f.runtime.nspidStatus === 'ready' ? f.runtime.nspidInsert : zh ? '模拟关闭' : 'status off'
          }`,
          badge:
            f.runtime.procNs === 'ready' && f.runtime.nspidStatus === 'ready'
              ? { text: dict.badges.active, cls: 'chip-ok' }
              : { text: dict.badges.partial, cls: 'chip-muted' }
        },
        {
          title: dict.subsystems.vendor,
          sub: snapshot.vendorLoaded
            ? `ghost=${snapshot.activeVendorParams.ghost ?? '1'}  ${
                snapshot.activeVendorParams.ghost_match ?? 'oplus_'
              }`
            : zh
              ? '未加载'
              : 'not loaded',
          badge: snapshot.vendorLoaded
            ? { text: dict.badges.loaded, cls: 'chip-ok' }
            : { text: dict.badges.unloaded, cls: 'chip-muted' }
        }
      ]
    : [];

  const okHooksCount = f ? f.hooks.filter((h) => h.state === 'installed').length : 0;

  return (
    <>
      {snapshot.meta.blocked && (
        <div className="card hero-err">
          <div className="banner-blocked">
            <div>
              <strong>{dict.blockedTitle}</strong>
              <div className="row-desc" style={{ color: 'inherit', opacity: 0.9 }}>
                {dict.blockedDesc}
              </div>
            </div>
            <mdui-button variant="filled" disabled={busy} onClick={onUnblock}>
              {dict.unblockBtn}
            </mdui-button>
          </div>
        </div>
      )}

      <SpringCard className={`card ${heroCls}`} pulseKey={installPulse}>
        <span className="hero-badge">
          {heroKey === 'full' && (
            <svg
              key={installPulse}
              className={`hero-check ${installPulse > 0 ? 'animate' : ''}`}
              width="14"
              height="14"
              viewBox="0 0 24 24"
              fill="none"
              stroke="currentColor"
              strokeWidth="2.8"
              strokeLinecap="round"
              strokeLinejoin="round"
              aria-hidden="true"
            >
              <path d="M5 13l4 4L19 7" />
            </svg>
          )}
          <span>{dict.hero[heroKey][0]}</span>
        </span>
        <h2 className="hero-title">{dict.hero[heroKey][1]}</h2>
      </SpringCard>

      <div className="card">
        <div className="card-title">
          <span>{dict.controlTitle}</span>
          <span className="chip chip-muted">{snapshot.meta.data || '/data/adb/phdroid'}</span>
        </div>

        {snapshot.kmis.length > 1 && !snapshot.loaded && (
          <div className="row" style={{ flexWrap: 'wrap', gap: '10px' }}>
            <div className="row-info">
              <div className="row-label">
                <span>{dict.kmiConfirmTitle}</span>
                <span className={`chip ${kmiConfirmed ? 'chip-ok' : 'chip-warn'}`}>
                  {kmiConfirmed ? `${dict.kmiConfirmedTag}: ${savedKmi}` : dict.kmiPendingTag}
                </span>
              </div>
              <div className="row-desc">{`${dict.kmiConfirmDesc} (${snapshot.meta.release})`}</div>
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: '8px', flexWrap: 'wrap' }}>
              {snapshot.kmis.length <= 3 ? (
                <MduiSegmentedGroup value={selectedKmi} onChange={onSelectKmi}>
                  {snapshot.kmis.map((k) => (
                    <mdui-segmented-button key={k} value={k}>
                      {k}
                    </mdui-segmented-button>
                  ))}
                </MduiSegmentedGroup>
              ) : (
                <select
                  className="select-pill"
                  value={selectedKmi}
                  onChange={(e) => onSelectKmi(e.target.value)}
                >
                  {snapshot.kmis.map((k) => (
                    <option key={k} value={k}>
                      {k}
                    </option>
                  ))}
                </select>
              )}
              <mdui-button
                variant={kmiConfirmed ? 'tonal' : 'filled'}
                disabled={busy || !selectedKmi || kmiConfirmed}
                onClick={() => onConfirmKmi(selectedKmi)}
              >
                {savedKmi && snapshot.kmis.includes(savedKmi)
                  ? dict.kmiUpdateBtn
                  : dict.kmiConfirmBtn}
              </mdui-button>
            </div>
          </div>
        )}

        <div className="row">
          <div className="row-info">
            <div className="row-label">{dict.autoLabel}</div>
            <div className="row-desc">
              {snapshot.meta.blocked
                ? dict.autoDescBlocked
                : !kmiConfirmed
                  ? dict.autoDescNeedKmi
                  : dict.autoDescNormal}
            </div>
          </div>
          <MduiSwitch
            checked={snapshot.meta.auto}
            disabled={busy || (!kmiConfirmed && !snapshot.meta.auto)}
            onChange={onToggleAuto}
          />
        </div>

        <div className="row">
          <div className="row-info">
            <div className="row-label">{dict.manualLabel}</div>
            <div className="row-desc">
              {snapshot.meta.blocked
                ? dict.manualDescBlocked
                : !kmiConfirmed
                  ? dict.manualDescNeedKmi
                  : snapshot.loaded
                    ? dict.manualDescLoaded
                    : dict.manualDescUnloaded}
            </div>
          </div>
          <HoldButton
            label={dict.installNow}
            holdingLabel={dict.holdingInstall}
            busyLabel={dict.installingNow}
            doneLabel={dict.installedDone}
            disabled={snapshot.meta.blocked || !kmiConfirmed}
            busy={busy}
            done={snapshot.loaded}
            pulseKey={installPulse}
            onConfirm={onInstallNow}
            onShortTap={onShortTapInstall}
          />
        </div>

        <div className="metric-strip" style={{ alignItems: 'center', justifyContent: 'space-between' }}>
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: '8px', alignItems: 'center', flex: 1 }}>
            <span className="row-desc" style={{ margin: 0, fontWeight: 600 }}>
              {dict.savedConfigLabel}
            </span>
            {savedPills.length ? (
              savedPills.map((p, idx) => (
                <div className="metric-pill" key={`${p.mod}-${idx}`}>
                  <span className="metric-k">{p.mod}</span>
                  <span className="metric-v">{p.val}</span>
                </div>
              ))
            ) : (
              <span className="row-desc" style={{ margin: 0 }}>
                {dict.defaultParamsTag}
              </span>
            )}
          </div>
          <mdui-button variant="tonal" onClick={onGoConfig}>
            {dict.editParamsBtn}
          </mdui-button>
        </div>
      </div>

      <div className="card">
        <div className="card-title">
          <span>{dict.readinessTitle}</span>
          <span className={`chip ${summaryCls}`}>{summaryText}</span>
        </div>

        <div className="runtime-meta-strip">
          <div className="metric-pill">
            <span className="metric-k">KMI</span>
            <span className="metric-v">{displayKmi}</span>
          </div>
          {loadedMods.length ? (
            loadedMods.map((item) => (
              <div className="metric-pill" key={item.mod}>
                <span className="status-dot ok" />
                <span className="metric-v">{item.mod}</span>
                {item.detail && <span className="metric-k">{item.detail}</span>}
              </div>
            ))
          ) : (
            <div className="metric-pill">
              <span className="status-dot" />
              <span className="metric-k">{dict.notLoaded}</span>
            </div>
          )}
        </div>

        <div className="stage-track">
          {stages.map((st) => (
            <div className={`stage-step ${st.cls}`} key={st.name}>
              <div className="stage-bar" />
              <div className="stage-name">{st.name}</div>
              <div className="stage-val">{st.val}</div>
            </div>
          ))}
        </div>

        {!snapshot.loaded || !f ? (
          <div className="row-desc">{dict.unloadedHint}</div>
        ) : (
          <>
            <div className="subsys-list">
              {subsystemRows.map((row) => (
                <div className="subsys-row" key={row.title}>
                  <div className="subsys-left">
                    <div className="subsys-title">{row.title}</div>
                    <div className="subsys-sub" title={row.sub}>
                      {row.sub}
                    </div>
                  </div>
                  <span className={`chip ${row.badge.cls}`}>{row.badge.text}</span>
                </div>
              ))}
            </div>
            <div className="metric-strip">
              <div className="metric-pill">
                <span className="metric-k">{dict.metrics.route}</span>
                <span className="metric-v">{f.runtime.hookRoutePolicy}</span>
              </div>
              <div className="metric-pill">
                <span className="metric-k">{dict.metrics.gate}</span>
                <span className="metric-v">
                  {f.runtime.gate === 'global' ? dict.metrics.global : dict.metrics.gated}
                </span>
              </div>
              <div className="metric-pill">
                <span className="metric-k">{dict.metrics.degrade}</span>
                <span className="metric-v">
                  {f.runtime.degrade === 'yes' ? dict.metrics.yes : dict.metrics.no}
                </span>
              </div>
              <div className="metric-pill">
                <span className="metric-k">{dict.metrics.compat32}</span>
                <span className="metric-v">{f.capabilities.compat32.owner}</span>
              </div>
              <div className="metric-pill">
                <span className="metric-k">{dict.metrics.slots}</span>
                <span className="metric-v">
                  {`clone=${f.runtime.slotsClone} 64=${f.runtime.slotsIpc64} 32=${f.runtime.slotsIpc32}`}
                </span>
              </div>
            </div>
          </>
        )}
      </div>

      <details className="fold">
        <summary>
          <span>{dict.hooksTitle}</span>
          <span className="chip chip-muted">
            {f ? `${okHooksCount}/${f.hooks.length}` : '0/0'}
          </span>
        </summary>
        <div className="fold-body">
          <table className="data-table">
            <thead>
              <tr>
                <th>{dict.thTarget}</th>
                <th>{dict.thRoute}</th>
                <th>{dict.thState}</th>
              </tr>
            </thead>
            <tbody>
              {!snapshot.loaded || !f || !f.hooks.length ? (
                <tr>
                  <td colSpan={3}>{dict.notLoaded}</td>
                </tr>
              ) : (
                f.hooks.map((h) => (
                  <tr key={h.name}>
                    <td>{h.name}</td>
                    <td>{h.route}</td>
                    <td>
                      <span className={`chip ${stateBadgeClass(h.state)}`}>
                        {dict.badges[h.state] || h.state}
                      </span>
                    </td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </details>

      <details className="fold">
        <summary>
          <span>{dict.layoutTitle}</span>
          <span
            className={`chip ${
              !snapshot.loaded || !f
                ? 'chip-muted'
                : f.layoutTrusted
                  ? 'chip-ok'
                  : 'chip-err'
            }`}
          >
            {!snapshot.loaded || !f
              ? dict.notLoaded
              : f.layoutTrusted
                ? dict.badges.trustedYes
                : dict.badges.trustedNo}
          </span>
        </summary>
        <div className="fold-body">
          <table className="data-table">
            <thead>
              <tr>
                <th>{dict.thField}</th>
                <th>{dict.thState}</th>
                <th>{dict.thLive}</th>
              </tr>
            </thead>
            <tbody>
              {!snapshot.loaded || !f || !f.layout.length ? (
                <tr>
                  <td colSpan={3}>{dict.notLoaded}</td>
                </tr>
              ) : (
                f.layout.map((item) => (
                  <tr key={item.field}>
                    <td>{item.field}</td>
                    <td>
                      <span className={`chip ${stateBadgeClass(item.state)}`}>
                        {isLayoutOkState(item.state) ? dict.badges.verified : dict.badges.unverified}
                      </span>
                    </td>
                    <td>{item.live}</td>
                  </tr>
                ))
              )}
            </tbody>
          </table>
        </div>
      </details>
    </>
  );
}
