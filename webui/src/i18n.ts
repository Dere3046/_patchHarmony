// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import type { Lang, ParamSpec, ScenarioKey } from './types';

export const DEFAULT_SEED_COLOR = '#3b6939';

export const PALETTE_PRESETS = [
  { id: 'green', hex: '#3b6939' },
  { id: 'violet', hex: '#6750a4' },
  { id: 'blue', hex: '#1b6ef3' },
  { id: 'teal', hex: '#006a6a' },
  { id: 'amber', hex: '#a04100' },
  { id: 'rose', hex: '#9b4057' },
  { id: 'indigo', hex: '#3f5aa9' },
  { id: 'slate', hex: '#4c616b' }
] as const;

export const PARAMS_SCHEMA: {
  common: ParamSpec[];
  vendor: ParamSpec[];
  diag: ParamSpec[];
} = {
  common: [
    {
      key: 'route',
      type: 'enum',
      def: '0',
      opts: {
        zh: [['0', '自动'], ['1', '入口写'], ['2', '断点']],
        en: [['0', 'auto'], ['1', 'text'], ['2', 'kprobe']]
      },
      label: { zh: '挂钩路线', en: 'route' },
      desc: { zh: '挂钩实现策略', en: 'Hook installation route' }
    },
    {
      key: 'gate',
      type: 'bool',
      def: '0',
      label: { zh: '容器门控', en: 'gate' },
      desc: { zh: '开启后仅对容器生效', en: 'Serve container processes only' }
    },
    {
      key: 'mqueue',
      type: 'bool',
      def: '1',
      label: { zh: '消息队列', en: 'mqueue' },
      desc: { zh: '启用 POSIX 消息队列与系统调用', en: 'Ported mqueuefs and syscalls' }
    },
    {
      key: 'nspid',
      type: 'bool',
      def: '1',
      label: { zh: '进程状态模拟', en: 'nspid' },
      desc: { zh: '输出各层级进程号信息', en: 'Emit NStgid NSpid lines in status' }
    },
    {
      key: 'nspid_append',
      type: 'bool',
      def: '0',
      label: { zh: '末尾追加状态', en: 'nspid_append' },
      desc: { zh: '将状态行追加在文件末尾', en: 'Append NS block at end of status' }
    },
    {
      key: 'pid_impl',
      type: 'enum',
      def: 'main',
      opts: {
        zh: [['main', '默认'], ['native', '原生']],
        en: [['main', 'main'], ['native', 'native']]
      },
      label: { zh: 'PID 后端', en: 'pid_impl' },
      desc: { zh: '命名空间实现分支', en: 'PID namespace backend' }
    },
    {
      key: 'nsproxy',
      type: 'bool',
      def: '0',
      label: { zh: '接管生命周期', en: 'nsproxy' },
      desc: { zh: '仅原生后端生效', en: 'Take over nsproxy lifecycle' }
    },
    {
      key: 'degrade',
      type: 'bool',
      def: '0',
      label: { zh: '降级容错', en: 'degrade' },
      desc: { zh: '关键挂钩失败时继续加载', en: 'Keep running when critical hook fails' }
    },
    {
      key: 'verbose',
      type: 'bool',
      def: '0',
      label: { zh: '详细日志', en: 'verbose' },
      desc: { zh: '记录挂钩入口与符号探测', en: 'Log hook entries and symbol probes' }
    }
  ],
  vendor: [
    {
      key: 'ghost',
      type: 'bool',
      def: '1',
      label: { zh: '幽灵任务回退', en: 'ghost' },
      desc: { zh: '任务查找未命中时返回占位对象', en: 'Return sentinel task on miss' }
    },
    {
      key: 'ghost_match',
      type: 'str',
      def: 'oplus_',
      label: { zh: '模块名前缀', en: 'ghost_match' },
      desc: { zh: '允许触发的调用方模块前缀', en: 'Caller module name prefix' }
    }
  ],
  diag: [
    {
      key: 'skip_sysvipc',
      type: 'bool',
      def: '0',
      label: { zh: '跳过 IPC 槽位', en: 'skip_sysvipc' },
      desc: { zh: '不接管十八个进程通信槽位', en: 'Skip 18 IPC syscall slots' }
    },
    {
      key: 'no_fake_ns',
      type: 'bool',
      def: '0',
      label: { zh: '空附着诊断', en: 'no_fake_ns' },
      desc: { zh: '假成功但不附着命名空间', en: 'Fake unshare without attach' }
    },
    {
      key: 'force_ipc_owner',
      type: 'bool',
      def: '0',
      label: { zh: '强制接管 IPC', en: 'force_ipc_owner' },
      desc: { zh: '强制走模块维护分支', en: 'Force module owned IPC branch' }
    },
    {
      key: 'layout_fault',
      type: 'num',
      def: '0',
      label: { zh: '布局故障注入', en: 'layout_fault' },
      desc: { zh: '测试锚点拒绝', en: 'Refuse layout anchors for test' }
    },
    {
      key: 'layout_shift',
      type: 'num',
      def: '0',
      label: { zh: '布局偏移注入', en: 'layout_shift' },
      desc: { zh: '测试符号偏移校验', en: 'Shift anchor symbols for test' }
    }
  ]
};

export const I18N = {
  zh: {
    navOverview: '概览',
    navConfig: '参数',
    navLogs: '日志',
    blockedTitle: '检测到开机熔断保护',
    blockedDesc: '上次开机加载未返回 已暂停自启',
    unblockBtn: '解除熔断',
    controlTitle: '控制与自启',
    autoLabel: '开机自动安装',
    autoDescNormal: '每次开机按已保存配置自动加载',
    autoDescBlocked: '已开启自启 当前被熔断暂停',
    autoDescNeedKmi: '请先在上方确认目标 KMI 版本再开启自启',
    manualLabel: '手动安装',
    manualDescLoaded: '模块已加载 改参保存后下次生效',
    manualDescUnloaded: '按已保存配置加载',
    manualDescBlocked: '开机熔断生效中 需先解除熔断',
    manualDescNeedKmi: '请先确认目标 KMI 版本后安装',
    kmiConfirmTitle: '确认目标 KMI',
    kmiConfirmDesc: '内核版本未带 Android 标签 请确认与系统一致的版本后加载',
    kmiPendingTag: '待确认',
    kmiConfirmedTag: '已确认',
    kmiConfirmBtn: '确认选用',
    kmiUpdateBtn: '切换确认',
    installNow: '长按安装',
    holdingInstall: '按住不放',
    installingNow: '正在安装',
    installedDone: '已安装',
    holdToConfirmHint: '请长按以确认安装',
    savedConfigLabel: '已保存配置',
    defaultParamsTag: '默认参数',
    editParamsBtn: '调整',
    readinessTitle: 'LKM 启动程度与开启内容',
    hooksTitle: 'Hook 安装详情',
    layoutTitle: '结构体布局校验',
    thTarget: '目标',
    thRoute: '路线',
    thState: '状态',
    thField: '字段',
    thLive: 'live',
    payloadTitle: '模块载荷',
    mainModLabel: '主模块',
    mainModDesc: '命名空间与进程通信核心',
    kmiSelectLabel: '目标 KMI 版本',
    kmiSelectDesc: '内核版本号未带 Android 标签 请选择要加载的 Android KMI',
    flavorNames: {
      standard: '版本: 标准版',
      native: '版本: 单原生 native',
      'main-native': '版本: 双模 main&native'
    },
    vendorModLabel: '厂商兼容模块',
    vendorModDesc: '任务查找回退与边界修复',
    commonParamsTitle: '常用参数',
    vendorParamsTitle: '厂商参数',
    diagParamsTitle: '诊断参数',
    advancedTag: '高级',
    previewTitle: '配置预览',
    addExtraBtn: '+ 添加参数',
    extraKeyPh: '参数名',
    extraValPh: '参数值',
    resetBtn: '恢复默认',
    runTempBtn: '仅本次安装',
    saveBtn: '保存配置',
    unsavedTag: '未保存',
    tabModuleLog: '模块日志',
    tabKernelLog: '内核日志',
    copyBtn: '复制',
    clearBtn: '清空',
    logSearchPlaceholder: '搜索日志',
    onlyModuleFilter: '仅模块相关',
    onlyWarnErrFilter: '仅警告与错误',
    emptyLog: '无匹配日志',
    notLoaded: '未加载',
    unloadedHint: '加载模块后显示各子系统实时状态',
    currentValPrefix: '当前:',
    scenarios: {
      full: '预览: 完全就绪',
      degraded: '预览: 降级运行',
      untrusted: '预览: 布局未信任',
      multiKmi: '预览: 多KMI回退',
      unloaded: '预览: 未加载',
      blocked: '预览: 开机熔断'
    } as Record<ScenarioKey, string>,
    hero: {
      blocked: ['开机熔断', '模块未加载 已触发防砖保护'],
      unloaded: ['未加载', '内核模块尚未加载'],
      untrusted: ['布局未信任', '核心功能处于关闭状态'],
      degraded: ['降级运行', '部分挂钩或子系统未启动'],
      full: ['完全就绪', '全部核心子系统已激活']
    },
    stages: ['初始化', '布局', '关键挂钩', '命名空间', '系统调用'],
    stageVals: {
      ready: '已就绪',
      bad: '异常',
      wait: '等待',
      trusted: '已校验',
      untrusted: '未信任',
      unprobed: '未探测',
      ok: '全部就绪',
      degraded: '降级',
      uninstalled: '未安装',
      nsAll: '完整',
      nsPartial: '部分关闭',
      nsOff: '未激活',
      slotOk: '已接管',
      slotOff: '未接管'
    },
    subsystems: {
      pidns: 'PID 命名空间',
      ipcns: 'IPC 命名空间',
      sysvipc: 'SysV 进程通信',
      mqueue: 'POSIX 消息队列',
      procNspid: '进程视图与状态',
      vendor: '厂商兼容'
    },
    badges: {
      module: '模块接管',
      kernel: '内核原生',
      absent: '未开启',
      active: '已激活',
      off: '未开启',
      partial: '部分',
      loaded: '已加载',
      unloaded: '未加载',
      installed: '已安装',
      refused: '已拒绝',
      verified: '已校验',
      unverified: '未校验',
      trustedYes: '已校验',
      trustedNo: '未信任'
    } as Record<string, string>,
    metrics: {
      route: '挂钩策略',
      gate: '门控',
      degrade: '降级容错',
      compat32: '32 位兼容',
      slots: '槽位',
      global: '全局',
      gated: '仅容器',
      yes: '开启',
      no: '关闭'
    },
    paletteTitle: '主题配色',
    paletteDesc: '选择主色调并实时生成整套浅深色板',
    paletteCustom: '自定义主色',
    doneBtn: '完成',
    paletteNames: {
      green: '森林绿',
      violet: '丁香紫',
      blue: '晴空蓝',
      teal: '青碧',
      amber: '琥珀橘',
      rose: '绯红',
      indigo: '靛青',
      slate: '墨灰'
    } as Record<string, string>,
    toasts: {
      refreshed: '已刷新状态',
      autoOn: '已开启开机自动安装',
      autoOff: '已关闭开机自动安装',
      unblocked: '已清除熔断标记',
      blockedGuard: '请先解除开机熔断',
      alreadyLoaded: '模块已在运行中',
      installed: '手动安装成功',
      installFail: '安装失败',
      reset: '已恢复默认参数',
      saved: '配置已保存',
      saveFail: '保存失败',
      copied: '已复制日志',
      cleared: '已清空模块日志',
      kmiConfirmed: '已确认目标 KMI 版本',
      kmiNeedConfirm: '请先在概览确认目标 KMI 版本',
      themeDark: '深色主题',
      themeLight: '浅色主题',
      themeAuto: '跟随系统'
    }
  },
  en: {
    navOverview: 'Overview',
    navConfig: 'Parameters',
    navLogs: 'Logs',
    blockedTitle: 'Boot guard active',
    blockedDesc: 'Previous boot load did not return auto boot paused',
    unblockBtn: 'Clear',
    controlTitle: 'Control & Boot',
    autoLabel: 'Load on boot',
    autoDescNormal: 'Auto load saved config at boot',
    autoDescBlocked: 'Enabled but paused by boot guard',
    autoDescNeedKmi: 'Confirm target KMI version before enabling auto boot',
    manualLabel: 'Manual load',
    manualDescLoaded: 'Already loaded save params for next load',
    manualDescUnloaded: 'Load using saved config',
    manualDescBlocked: 'Blocked clear boot guard before load',
    manualDescNeedKmi: 'Confirm target KMI version before load',
    kmiConfirmTitle: 'Confirm target KMI',
    kmiConfirmDesc: 'Kernel release lacks Android tag pick the matching KMI before load',
    kmiPendingTag: 'Unconfirmed',
    kmiConfirmedTag: 'Confirmed',
    kmiConfirmBtn: 'Confirm KMI',
    kmiUpdateBtn: 'Apply KMI',
    installNow: 'Hold to load',
    holdingInstall: 'Keep holding',
    installingNow: 'Loading',
    installedDone: 'Loaded',
    holdToConfirmHint: 'Hold button to confirm load',
    savedConfigLabel: 'Saved config',
    defaultParamsTag: 'Defaults',
    editParamsBtn: 'Edit',
    readinessTitle: 'LKM Readiness & Features',
    hooksTitle: 'Hook routes',
    layoutTitle: 'Struct layout',
    thTarget: 'Target',
    thRoute: 'Route',
    thState: 'State',
    thField: 'Field',
    thLive: 'live',
    payloadTitle: 'Payloads',
    mainModLabel: 'droid_lkm',
    mainModDesc: 'Core namespace and IPC shim',
    kmiSelectLabel: 'Target KMI',
    kmiSelectDesc: 'Kernel release lacks Android tag pick which KMI to load',
    flavorNames: {
      standard: 'Flavor: standard',
      native: 'Flavor: native',
      'main-native': 'Flavor: main&native'
    },
    vendorModLabel: 'droid_lkm_vendor',
    vendorModDesc: 'Vendor task lookup and bounds fix',
    commonParamsTitle: 'Common parameters',
    vendorParamsTitle: 'Vendor parameters',
    diagParamsTitle: 'Diagnostic parameters',
    advancedTag: 'Advanced',
    previewTitle: 'Config preview',
    addExtraBtn: '+ Add param',
    extraKeyPh: 'key',
    extraValPh: 'value',
    resetBtn: 'Reset',
    runTempBtn: 'Load once',
    saveBtn: 'Save',
    unsavedTag: 'Unsaved',
    tabModuleLog: 'Module log',
    tabKernelLog: 'Kernel log',
    copyBtn: 'Copy',
    clearBtn: 'Clear',
    logSearchPlaceholder: 'Filter logs...',
    onlyModuleFilter: 'Module only',
    onlyWarnErrFilter: 'Warn & error',
    emptyLog: 'No matching logs',
    notLoaded: 'Unloaded',
    unloadedHint: 'Load droid_lkm to inspect live subsystem state',
    currentValPrefix: 'val:',
    scenarios: {
      full: 'Preview: Full',
      degraded: 'Preview: Degraded',
      untrusted: 'Preview: Untrusted',
      multiKmi: 'Preview: Multi-KMI',
      unloaded: 'Preview: Unloaded',
      blocked: 'Preview: Blocked'
    } as Record<ScenarioKey, string>,
    hero: {
      blocked: ['Blocked', 'Boot guard prevented auto load'],
      unloaded: ['Unloaded', 'Kernel modules not loaded'],
      untrusted: ['Untrusted', 'Core features stay off'],
      degraded: ['Degraded', 'Some hooks or features are off'],
      full: ['Ready', 'All core subsystems active']
    },
    stages: ['Init', 'Layout', 'Hooks', 'Namespaces', 'Syscalls'],
    stageVals: {
      ready: 'Ready',
      bad: 'Error',
      wait: 'Idle',
      trusted: 'Trusted',
      untrusted: 'Untrusted',
      unprobed: 'Idle',
      ok: 'OK',
      degraded: 'Degraded',
      uninstalled: 'Idle',
      nsAll: 'All up',
      nsPartial: 'Partial',
      nsOff: 'Off',
      slotOk: 'Patched',
      slotOff: 'Off'
    },
    subsystems: {
      pidns: 'PID namespace',
      ipcns: 'IPC namespace',
      sysvipc: 'SysV IPC',
      mqueue: 'POSIX mqueue',
      procNspid: 'Proc NS & NSpid',
      vendor: 'Vendor compat'
    },
    badges: {
      module: 'module',
      kernel: 'kernel',
      absent: 'off',
      active: 'active',
      off: 'off',
      partial: 'partial',
      loaded: 'loaded',
      unloaded: 'off',
      installed: 'installed',
      refused: 'refused',
      verified: 'ok',
      unverified: 'fail',
      trustedYes: 'trusted',
      trustedNo: 'untrusted'
    } as Record<string, string>,
    metrics: {
      route: 'Route',
      gate: 'Gate',
      degrade: 'Degrade',
      compat32: 'Compat32',
      slots: 'Slots',
      global: 'global',
      gated: 'gated',
      yes: 'on',
      no: 'off'
    },
    paletteTitle: 'Theme Color',
    paletteDesc: 'Pick a primary seed to derive light and dark palettes',
    paletteCustom: 'Custom color',
    doneBtn: 'Done',
    paletteNames: {
      green: 'Forest',
      violet: 'Violet',
      blue: 'Ocean',
      teal: 'Teal',
      amber: 'Amber',
      rose: 'Rose',
      indigo: 'Indigo',
      slate: 'Slate'
    } as Record<string, string>,
    toasts: {
      refreshed: 'Refreshed',
      autoOn: 'Auto load enabled',
      autoOff: 'Auto load disabled',
      unblocked: 'Boot guard cleared',
      blockedGuard: 'Clear boot guard first',
      alreadyLoaded: 'Already loaded',
      installed: 'Modules loaded',
      installFail: 'Install failed',
      reset: 'Defaults restored',
      saved: 'Config saved',
      saveFail: 'Save failed',
      copied: 'Logs copied',
      cleared: 'Module log cleared',
      kmiConfirmed: 'Target KMI confirmed',
      kmiNeedConfirm: 'Confirm target KMI on Overview first',
      themeDark: 'Dark theme',
      themeLight: 'Light theme',
      themeAuto: 'System theme'
    }
  }
} as const;

export function getLangStrings(lang: Lang) {
  return I18N[lang];
}

export function majorMinor(release: string): string {
  const m = String(release || '').match(/^(\d+\.\d+)/);
  return m ? m[1] : release;
}

export function isLayoutOkState(state: string): boolean {
  return state === 'ok' || state === 'verified' || state === 'anchored' || state === 'manual' || state === 'build';
}
