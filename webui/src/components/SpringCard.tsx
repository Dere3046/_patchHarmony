// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2026 dere3046
 */

import { useEffect, useRef, type PointerEvent as ReactPointerEvent, type ReactNode } from 'react';

interface SpringCardProps {
  className: string;
  pulseKey?: number;
  children: ReactNode;
}

interface AxisState {
  val: number;
  vel: number;
  target: number;
  k: number;
  d: number;
}

interface PointerSample {
  x: number;
  y: number;
  t: number;
}

function makeAxis(initial: number, k = 360, d = 28): AxisState {
  return { val: initial, vel: 0, target: initial, k, d };
}

function stepAxis(a: AxisState, dt: number): boolean {
  const force = -a.k * (a.val - a.target) - a.d * a.vel;
  a.vel += force * dt;
  a.val += a.vel * dt;
  if (Math.abs(a.val - a.target) < 0.002 && Math.abs(a.vel) < 0.002) {
    a.val = a.target;
    a.vel = 0;
    return false;
  }
  return true;
}

function rubberband(offset: number, limit: number): number {
  const abs = Math.abs(offset);
  if (abs === 0) return 0;
  return (offset * limit * 0.55) / (limit + 0.55 * abs);
}

export function SpringCard({ className, pulseKey = 0, children }: SpringCardProps) {
  const cardRef = useRef<HTMLDivElement>(null);
  const rafRef = useRef<number | null>(null);
  const lastTimeRef = useRef<number>(0);

  const draggingRef = useRef(false);
  const pointerIdRef = useRef<number | null>(null);
  const startRef = useRef({ x: 0, y: 0 });
  const historyRef = useRef<PointerSample[]>([]);

  const axesRef = useRef({
    x: makeAxis(0, 680, 50),
    y: makeAxis(0, 680, 50),
    scale: makeAxis(1, 480, 32),
    rx: makeAxis(0, 420, 30),
    ry: makeAxis(0, 420, 30),
    glow: makeAxis(0, 320, 28)
  });

  const reducedMotion = () =>
    Boolean(window.matchMedia?.('(prefers-reduced-motion: reduce)').matches);

  const applyTransform = () => {
    const el = cardRef.current;
    if (!el) return;
    const { x, y, scale, rx, ry, glow } = axesRef.current;
    el.style.transform = `perspective(640px) translate3d(${x.val.toFixed(2)}px, ${y.val.toFixed(
      2
    )}px, 0) rotateX(${rx.val.toFixed(2)}deg) rotateY(${ry.val.toFixed(
      2
    )}deg) scale(${scale.val.toFixed(4)})`;
    el.style.setProperty('--hero-glow-o', glow.val.toFixed(3));
  };

  const kickLoop = () => {
    if (rafRef.current !== null) return;
    lastTimeRef.current = performance.now();

    const tick = (now: number) => {
      const dt = Math.min(0.032, Math.max(0.001, (now - lastTimeRef.current) / 1000));
      lastTimeRef.current = now;

      const s = axesRef.current;
      let active = false;
      active = stepAxis(s.x, dt) || active;
      active = stepAxis(s.y, dt) || active;
      active = stepAxis(s.scale, dt) || active;
      active = stepAxis(s.rx, dt) || active;
      active = stepAxis(s.ry, dt) || active;
      active = stepAxis(s.glow, dt) || active;

      applyTransform();

      if (active || draggingRef.current) {
        rafRef.current = requestAnimationFrame(tick);
      } else {
        const el = cardRef.current;
        if (el) el.style.transform = '';
        rafRef.current = null;
      }
    };

    rafRef.current = requestAnimationFrame(tick);
  };

  useEffect(() => {
    return () => {
      if (rafRef.current !== null) cancelAnimationFrame(rafRef.current);
    };
  }, []);

  useEffect(() => {
    if (!pulseKey || reducedMotion()) return;
    const s = axesRef.current;
    s.scale.k = 320;
    s.scale.d = 18;
    s.scale.val = 0.965;
    s.scale.vel = 0.38;
    s.scale.target = 1;
    s.y.k = 300;
    s.y.d = 18;
    s.y.val = 6;
    s.y.target = 0;
    s.glow.val = 0.9;
    s.glow.target = 0;
    kickLoop();
  }, [pulseKey]);

  const updateTiltFromPointer = (clientX: number, clientY: number, dx: number, dy: number) => {
    const el = cardRef.current;
    if (!el) return;
    const rect = el.getBoundingClientRect();
    const nx = rect.width ? ((clientX - rect.left) / rect.width) * 2 - 1 : 0;
    const ny = rect.height ? ((clientY - rect.top) / rect.height) * 2 - 1 : 0;
    const gx = rect.width ? ((clientX - rect.left) / rect.width) * 100 : 50;
    const gy = rect.height ? ((clientY - rect.top) / rect.height) * 100 : 50;

    el.style.setProperty('--hero-gx', `${Math.max(0, Math.min(100, gx)).toFixed(1)}%`);
    el.style.setProperty('--hero-gy', `${Math.max(0, Math.min(100, gy)).toFixed(1)}%`);

    const s = axesRef.current;
    s.rx.target = Math.max(-8.5, Math.min(8.5, -ny * 5.2 - dy * 0.08));
    s.ry.target = Math.max(-8.5, Math.min(8.5, nx * 5.2 + dx * 0.08));
  };

  const handlePointerDown = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (e.button !== 0 && e.pointerType === 'mouse') return;
    const el = cardRef.current;
    if (!el) return;

    try {
      el.setPointerCapture(e.pointerId);
    } catch {
      // ignore synthetic events
    }

    draggingRef.current = true;
    pointerIdRef.current = e.pointerId;
    startRef.current = { x: e.clientX, y: e.clientY };
    historyRef.current = [{ x: e.clientX, y: e.clientY, t: performance.now() }];

    const s = axesRef.current;
    s.x.k = 680;
    s.x.d = 50;
    s.y.k = 680;
    s.y.d = 50;
    s.scale.k = 480;
    s.scale.d = 32;
    s.scale.target = 0.974;
    s.glow.target = 1;

    if (!reducedMotion()) {
      s.y.target = 6;
      updateTiltFromPointer(e.clientX, e.clientY, 0, 0);
    }
    kickLoop();
  };

  const handlePointerMove = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (!draggingRef.current || pointerIdRef.current !== e.pointerId) return;
    const now = performance.now();
    const dx = e.clientX - startRef.current.x;
    const dy = e.clientY - startRef.current.y;

    const hist = historyRef.current;
    hist.push({ x: e.clientX, y: e.clientY, t: now });
    while (hist.length > 5 || (hist.length > 2 && now - hist[0].t > 90)) {
      hist.shift();
    }

    if (reducedMotion()) return;

    const s = axesRef.current;
    s.x.target = rubberband(dx, 36);
    s.y.target = 6 + rubberband(dy, 22);
    updateTiltFromPointer(e.clientX, e.clientY, s.x.target, s.y.target);
    kickLoop();
  };

  const handleRelease = (e: ReactPointerEvent<HTMLDivElement>) => {
    if (!draggingRef.current || pointerIdRef.current !== e.pointerId) return;
    draggingRef.current = false;
    pointerIdRef.current = null;

    const hist = historyRef.current;
    let vx = 0;
    let vy = 0;
    if (hist.length >= 2) {
      const first = hist[0];
      const last = hist[hist.length - 1];
      const dt = (last.t - first.t) / 1000;
      if (dt > 0.005) {
        vx = Math.max(-1400, Math.min(1400, (last.x - first.x) / dt));
        vy = Math.max(-1000, Math.min(1000, (last.y - first.y) / dt));
      }
    }

    const s = axesRef.current;
    s.x.k = 260;
    s.x.d = 20;
    s.y.k = 260;
    s.y.d = 20;
    s.x.vel += vx * 0.18;
    s.y.vel += vy * 0.14;
    s.x.target = 0;
    s.y.target = 0;
    s.scale.target = 1;
    s.rx.target = 0;
    s.ry.target = 0;
    s.glow.target = 0;
    kickLoop();
  };

  return (
    <div
      ref={cardRef}
      className={`${className} hero-interactive`}
      onPointerDown={handlePointerDown}
      onPointerMove={handlePointerMove}
      onPointerUp={handleRelease}
      onPointerCancel={handleRelease}
    >
      <div className="hero-sheen" aria-hidden="true" />
      {pulseKey > 0 && <div key={pulseKey} className="hero-sweep" aria-hidden="true" />}
      <div className="hero-inner">{children}</div>
    </div>
  );
}
