// ═══════════════════════════════════════════════════
// IH-2000 — Induction Heater Controller · Web UI
// ═══════════════════════════════════════════════════

'use strict';

/* ─── State ─── */
const state = {
    enable: false,
    frequency: 40.0,
    duty: 45.0,
    deadTimeRed: 200,
    deadTimeFed: 200,
    power: 0,
    voltage: 0,
    current: 0,
    temperature: 25,
    connected: false,
};

/* ─── DOM refs ─── */
const $ = (id) => document.getElementById(id);
const dom = {
    sysStatus: $('sysStatus'),
    statusDot: $('statusDot'),
    statusText: $('statusText'),
    wifiBadge: $('wifiBadge'),
    wifiInfo: $('wifiInfo'),
    btnEStop: $('btnEStop'),
    chkEnable: $('chkEnable'),
    lblEnable: $('lblEnable'),
    powerRing: $('powerRing'),
    ringPowerVal: $('ringPowerVal'),
    metaVoltage: $('metaVoltage'),
    metaCurrent: $('metaCurrent'),
    metaTemp: $('metaTemp'),

    sliderFreq: $('sliderFreq'),
    numFreq: $('numFreq'),
    badgeFreq: $('badgeFreq'),

    sliderDuty: $('sliderDuty'),
    numDuty: $('numDuty'),
    badgeDuty: $('badgeDuty'),

    sliderRed: $('sliderRed'),
    numRed: $('numRed'),
    badgeRed: $('badgeRed'),

    sliderFed: $('sliderFed'),
    numFed: $('numFed'),
    badgeFed: $('badgeFed'),

    ledA: $('ledA'),
    ledB: $('ledB'),
    waveSvg: $('waveSvg'),
};

/* ─── Slider ↔ Number sync ─── */
function bindSliderNumber(sliderId, numId, badgeId, fmtFn) {
    const slider = $(sliderId);
    const num = $(numId);
    const badge = $(badgeId);

    slider.addEventListener('input', () => {
        const v = parseFloat(slider.value);
        num.value = v;
        if (badge) badge.textContent = fmtFn(v);
    });
    num.addEventListener('input', () => {
        let v = parseFloat(num.value);
        const min = parseFloat(num.min);
        const max = parseFloat(num.max);
        if (isNaN(v)) v = min;
        v = Math.min(max, Math.max(min, v));
        num.value = v;
        slider.value = v;
        if (badge) badge.textContent = fmtFn(v);
    });
    num.addEventListener('blur', () => {
        const v = parseFloat(num.value);
        if (isNaN(v) || v < parseFloat(num.min) || v > parseFloat(num.max)) {
            num.value = slider.value;
        }
    });
}

bindSliderNumber('sliderFreq', 'numFreq', 'badgeFreq', (v) => v.toFixed(1) + ' kHz');
bindSliderNumber('sliderDuty', 'numDuty', 'badgeDuty', (v) => v.toFixed(1) + ' %');
bindSliderNumber('sliderRed',  'numRed',  'badgeRed',  (v) => Math.round(v) + ' ns');
bindSliderNumber('sliderFed',  'numFed',  'badgeFed',  (v) => Math.round(v) + ' ns');

/* ─── Debounced API sender ─── */
let sendTimer = null;
function scheduleSend() {
    if (sendTimer) clearTimeout(sendTimer);
    sendTimer = setTimeout(sendConfig, 180);
}

function sendConfig() {
    const body = JSON.stringify({
        enable: dom.chkEnable.checked,
        frequency: parseFloat(dom.sliderFreq.value),
        duty: parseFloat(dom.sliderDuty.value),
        dead_time_red: Math.round(parseFloat(dom.sliderRed.value)),
        dead_time_fed: Math.round(parseFloat(dom.sliderFed.value)),
    });
    fetch('/api/config', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body,
    }).catch(() => { /* connection lost — will be caught by poll */ });
}

// Auto-send on any slider/number change
['sliderFreq','sliderDuty','sliderRed','sliderFed'].forEach((id) => {
    $(id).addEventListener('change', scheduleSend);
});
['numFreq','numDuty','numRed','numFed'].forEach((id) => {
    $(id).addEventListener('change', scheduleSend);
});
dom.chkEnable.addEventListener('change', () => {
    dom.lblEnable.textContent = dom.chkEnable.checked ? 'ON' : 'OFF';
    scheduleSend();
});

/* ─── Emergency Stop ─── */
dom.btnEStop.addEventListener('click', () => {
    if (!confirm('EMERGENCY STOP — immediately cut all power output?')) return;
    dom.chkEnable.checked = false;
    dom.lblEnable.textContent = 'OFF';
    dom.sliderDuty.value = 0;
    dom.numDuty.value = 0;
    dom.badgeDuty.textContent = '0.0 %';
    fetch('/api/estop', { method: 'POST' })
        .then(() => { dom.sysStatus.className = 'status-pill warning'; dom.statusText.textContent = 'ESTOPPED'; })
        .catch(() => {});
});

/* ─── Poll status ─── */
async function pollStatus() {
    try {
        const res = await fetch('/api/status');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();

        state.connected = true;
        dom.sysStatus.className = data.enable ? 'status-pill online' : 'status-pill';
        dom.statusDot.style.background = data.enable ? 'var(--success)' : 'var(--text-dim)';
        dom.statusText.textContent = data.enable ? 'RUNNING' : 'STANDBY';
        dom.wifiInfo.textContent = data.wifi_mode + ' · ' + data.wifi_ip;

        // Update display-only values from hardware feedback
        if (data.power !== undefined) {
            state.power = data.power;
            state.voltage = data.voltage || 0;
            state.current = data.current || 0;
            state.temperature = data.temperature || 25;
            updatePowerRing();
        }
    } catch {
        state.connected = false;
        dom.sysStatus.className = 'status-pill offline';
        dom.statusText.textContent = 'DISCONNECTED';
    }
}

function updatePowerRing() {
    const circumference = 427.26; // 2 * PI * 68
    const maxPower = 30; // 30 kW scale
    const pct = Math.min(1, state.power / maxPower);
    const offset = circumference * (1 - pct);
    dom.powerRing.style.strokeDashoffset = offset;
    dom.ringPowerVal.textContent = state.power.toFixed(1);
    dom.metaVoltage.textContent = state.voltage.toFixed(1) + ' V';
    dom.metaCurrent.textContent = state.current.toFixed(1) + ' A';
    dom.metaTemp.textContent = state.temperature.toFixed(0) + ' °C';

    // Color by power level
    const hue = 220 - pct * 120; // blue → red
    dom.powerRing.style.stroke = pct > 0.8 ? '#da3633' : pct > 0.5 ? '#d29922' : '#2790e6';
}

/* ─── Poll config (sync local with device) ─── */
async function pollConfig() {
    try {
        const res = await fetch('/api/config');
        if (!res.ok) throw new Error('HTTP ' + res.status);
        const data = await res.json();
        // Only update UI if user isn't actively dragging
        if (document.activeElement !== dom.sliderFreq && document.activeElement !== dom.numFreq) {
            if (data.frequency !== undefined) {
                dom.sliderFreq.value = data.frequency;
                dom.numFreq.value = data.frequency;
                dom.badgeFreq.textContent = data.frequency.toFixed(1) + ' kHz';
            }
        }
        if (document.activeElement !== dom.sliderDuty && document.activeElement !== dom.numDuty) {
            if (data.duty !== undefined) {
                dom.sliderDuty.value = data.duty;
                dom.numDuty.value = data.duty;
                dom.badgeDuty.textContent = data.duty.toFixed(1) + ' %';
            }
        }
        if (document.activeElement !== dom.sliderRed && document.activeElement !== dom.numRed) {
            if (data.dead_time_red !== undefined) {
                dom.sliderRed.value = data.dead_time_red;
                dom.numRed.value = data.dead_time_red;
                dom.badgeRed.textContent = Math.round(data.dead_time_red) + ' ns';
            }
        }
        if (document.activeElement !== dom.sliderFed && document.activeElement !== dom.numFed) {
            if (data.dead_time_fed !== undefined) {
                dom.sliderFed.value = data.dead_time_fed;
                dom.numFed.value = data.dead_time_fed;
                dom.badgeFed.textContent = Math.round(data.dead_time_fed) + ' ns';
            }
        }
        if (data.enable !== undefined && document.activeElement !== dom.chkEnable) {
            dom.chkEnable.checked = data.enable;
            dom.lblEnable.textContent = data.enable ? 'ON' : 'OFF';
        }
    } catch { /* status poll handles connectivity */ }
}

/* ─── PWM Waveform Drawing ─── */
function drawWaveform() {
    const svg = dom.waveSvg;
    const w = 800, h = 120;
    const freq = parseFloat(dom.sliderFreq.value) || 40;
    const duty = parseFloat(dom.sliderDuty.value) || 0;
    const dtRed = parseFloat(dom.sliderRed.value) || 0;
    const dtFed = parseFloat(dom.sliderFed.value) || 0;
    const enabled = dom.chkEnable.checked;

    // Scale dead time for visualization (show at most 1/4 of on-time)
    const period = 1000 / freq; // period in μs
    const dutyOn = period * (duty / 100);
    const offTime = period - dutyOn;
    const scaleNs = 25; // ns per pixel
    const dtVisA = Math.min(dtRed / scaleNs / 50, dutyOn * 0.25);
    const dtVisB = Math.min(dtFed / scaleNs / 50, offTime * 0.25);

    const yA = 22, yB = 82;
    const amp = 12;

    let pathA = '', pathB = '';
    const steps = 200;
    const pStep = (period * 2) / steps;

    for (let i = 0; i <= steps; i++) {
        const t = i * pStep; // time in μs
        const cycleT = t % period;
        const x = (t / (period * 2)) * w;

        // Channel A (high-side): ON from dtVisA to dutyOn, OFF otherwise
        let valA = 0;
        if (cycleT >= dtVisA && cycleT < dutyOn) valA = 1;

        // Channel B (low-side, complementary): ON from dutyOn+dtVisB to period
        let valB = 0;
        if (cycleT >= dutyOn + dtVisB && cycleT < period) valB = 1;

        pathA += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + (valA ? (yA - amp) : (yA + 2));
        pathB += (i === 0 ? 'M' : 'L') + x.toFixed(1) + ',' + (valB ? (yB - amp) : (yB + 2));
    }

    // Remove old paths
    svg.querySelectorAll('.pwm-wave').forEach(el => el.remove());

    const nsA = 'http://www.w3.org/2000/svg';
    const makePath = (d, cls, col) => {
        const p = document.createElementNS(nsA, 'path');
        p.setAttribute('d', d);
        p.setAttribute('class', 'pwm-wave');
        p.setAttribute('fill', 'none');
        p.setAttribute('stroke', col);
        p.setAttribute('stroke-width', '2');
        p.setAttribute('stroke-linejoin', 'round');
        svg.appendChild(p);
    };

    makePath(pathA, 'pwm-wave', enabled ? '#2790e6' : '#484f58');
    makePath(pathB, 'pwm-wave', enabled ? '#d29922' : '#484f58');

    // Phase label
    const phaseEl = svg.querySelector('.phase-label');
    if (phaseEl) {
        if (enabled) phaseEl.textContent = `${freq.toFixed(1)} kHz · ${duty.toFixed(1)}% duty`;
        else phaseEl.textContent = 'Output Disabled';
    }

    // LEDs
    dom.ledA.classList.toggle('active', enabled);
    dom.ledB.classList.toggle('active', enabled);
}

/* ─── Poll cycle ─── */
let pollInterval;

function startPoll() {
    pollConfig();
    pollStatus();
    drawWaveform();
    pollInterval = setInterval(() => {
        pollStatus();
        drawWaveform();
    }, 600);
    // Config poll less frequently (unless changed by user)
    setInterval(pollConfig, 3000);
}

/* ─── Init ─── */
document.addEventListener('DOMContentLoaded', () => {
    // Set initial badge values
    dom.badgeFreq.textContent = parseFloat(dom.sliderFreq.value).toFixed(1) + ' kHz';
    dom.badgeDuty.textContent = parseFloat(dom.sliderDuty.value).toFixed(1) + ' %';
    dom.badgeRed.textContent = Math.round(parseFloat(dom.sliderRed.value)) + ' ns';
    dom.badgeFed.textContent = Math.round(parseFloat(dom.sliderFed.value)) + ' ns';

    startPoll();
});
