// web_page.h - the single-page web interface served at "/".
//
// Copyright (c) 2019-2026 G Cinini
// SPDX-License-Identifier: MIT
//
// This whole file is ONE piece of text: a complete web page (HTML for the
// layout, CSS for the looks, JavaScript for the behaviour). The lamp sends it
// to the browser, and from then on the JavaScript runs *in the browser*, not
// on the ESP8266. It talks to the lamp through small addresses that answer
// with JSON (see section 10 of the sketch):
//   /state, /power, /colour, /brightness, /mode, /palette,
//   /temperature, /speed, /sleep
// It asks /state every few seconds, so changes made by Alexa or Home Assistant show up too.
// Colour selection uses the browser's built-in <input type="color"> picker.
//
// Two C++ details make this work:
//   * R"page( ... )page" is a *raw string literal*: everything between the
//     brackets is taken exactly as written, so the HTML can contain quote
//     marks and backslashes without escaping each one.
//   * PROGMEM keeps the text in flash memory instead of copying it into the
//     ESP8266's small RAM. That's why the sketch sends it with send_P().

#pragma once

static const char WEB_PAGE[] PROGMEM = R"page(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>Lightbox</title>
<style>
  :root {
    --bg: #14161a; --card: #1e2228; --line: #2d333c;
    --text: #e8ebf0; --muted: #98a1ae; --accent: #ffb454; --accent-ink: #1d1400;
  }
  * { box-sizing: border-box; }
  body { margin: 0; background: var(--bg); color: var(--text);
         font-family: system-ui, -apple-system, "Segoe UI", Roboto, sans-serif; }
  main { max-width: 520px; margin: 0 auto; padding: 20px 16px 40px; }
  h1 { font-size: 1.4rem; margin: 0 0 4px; }
  #status { color: var(--muted); margin: 0 0 18px; font-size: .9rem; min-height: 1.2em; }
  section { background: var(--card); border: 1px solid var(--line); border-radius: 14px;
            padding: 14px 16px 16px; margin-bottom: 12px; }
  h2 { display: flex; justify-content: space-between; font-size: .75rem; font-weight: 600;
       letter-spacing: .08em; text-transform: uppercase; color: var(--muted); margin: 0 0 12px; }
  h2 span { color: var(--text); letter-spacing: 0; font-variant-numeric: tabular-nums; }
  .grid { display: grid; grid-template-columns: repeat(auto-fill, minmax(140px, 1fr)); gap: 8px; }
  button { min-height: 44px; padding: 8px 10px; border-radius: 10px; border: 1px solid var(--line);
           background: #262b33; color: var(--text); font: inherit; font-size: .95rem; cursor: pointer; }
  button:hover { border-color: var(--muted); }
  button.active { background: var(--accent); border-color: var(--accent); color: var(--accent-ink); font-weight: 600; }
  input[type=color] { display: block; width: 100%; height: 56px; padding: 0; border: 0;
                      border-radius: 10px; background: none; cursor: pointer; }
  input[type=range] { width: 100%; accent-color: var(--accent); }
  .hint { color: var(--muted); font-size: .8rem; margin: 8px 0 0; }
  dl { display: grid; grid-template-columns: auto 1fr; gap: 6px 14px; margin: 0; font-size: .9rem; }
  dt { color: var(--muted); }
  dd { margin: 0; overflow-wrap: anywhere; font-variant-numeric: tabular-nums; }
</style>
</head>
<body>
<main>
  <h1 id="name">Lightbox</h1>
  <p id="status">Connecting&hellip;</p>

  <section>
    <h2>Power</h2>
    <div class="grid">
      <button id="power-on">On</button>
      <button id="power-off">Off</button>
    </div>
  </section>

  <section>
    <h2>Colour</h2>
    <input type="color" id="colour" value="#ffb46b" aria-label="Lamp colour">
    <p class="hint">Picking a colour switches to Solid colour mode.</p>
  </section>

  <section>
    <h2>White <span id="kelvin-value"></span></h2>
    <input type="range" id="kelvin" min="2000" max="6500" step="100" aria-label="White temperature">
    <p class="hint">Warm (left) to daylight (right). Also switches to Solid colour.</p>
  </section>

  <section>
    <h2>Brightness <span id="brightness-value"></span></h2>
    <input type="range" id="brightness" min="1" max="255" aria-label="Brightness">
  </section>

  <section>
    <h2>Mode</h2>
    <div class="grid" id="modes"></div>
  </section>

  <section>
    <h2>Palette</h2>
    <div class="grid" id="palettes"></div>
    <p class="hint">Used by the two palette modes.</p>
  </section>

  <section>
    <h2>Effect speed <span id="speed-value"></span></h2>
    <input type="range" id="speed" min="1" max="10" step="1" aria-label="Effect speed">
  </section>

  <section>
    <h2>Sleep timer <span id="sleep-value"></span></h2>
    <div class="grid" id="sleep"></div>
    <p class="hint">Fades off slowly when the time is up.</p>
  </section>

  <section>
    <h2>Network</h2>
    <dl id="network"></dl>
  </section>
</main>

<script>
  // Mode numbers and button labels. The numbers must match MODE_... in the sketch.
  const MODES = [
    [1, 'Solid colour'], [2, 'Colour fade'], [3, 'Rainbow'],
    [4, 'Random'], [5, 'Palette, smooth'], [6, 'Palette, stepped'], [7, 'Candle'],
    [8, 'Fireplace'], [9, 'Breathe'], [10, 'Twinkle'], [11, 'Comet'],
    [12, 'Reading light'], [13, 'Sunrise'], [14, 'Night light']
  ];
  // Sleep timer buttons: [minutes, label]. 0 cancels the timer.
  const SLEEP_CHOICES = [[0, 'Off'], [15, '15 min'], [30, '30 min'], [60, '1 hour'], [90, '90 min']];
  // el('status') is shorthand for "the element with id=status". `id => ...` is an arrow function.
  const el = id => document.getElementById(id);
  let paletteNames = null;   // filled in from the lamp's first answer

  // Creates a <button> that runs onClick when pressed.
  function makeButton(label, onClick) {
    const b = document.createElement('button');
    b.textContent = label;
    b.addEventListener('click', onClick);
    return b;
  }

  // Calls one of the lamp's endpoints; every endpoint answers with the full state.
  // fetch() works in the background: .then() runs when the answer arrives,
  // .catch() runs if anything went wrong (lamp offline, bad answer).
  function send(path) {
    return fetch('/' + path)
      .then(r => { if (!r.ok) throw new Error(r.status); return r.json(); })
      .then(render)
      .catch(() => { el('status').textContent = 'Lamp not responding'; });
  }

  // Limits how often a slider or the colour picker sends updates while being dragged.
  function throttle(fn, ms) {
    let last = 0, timer = null;
    return () => {
      clearTimeout(timer);
      const wait = ms - (Date.now() - last);
      if (wait <= 0) { last = Date.now(); fn(); }
      else timer = setTimeout(() => { last = Date.now(); fn(); }, wait);
    };
  }

  // Updates every control from the lamp's state (the JSON object `s`).
  function render(s) {
    // Show this lamp's own name, so two lamps are easy to tell apart
    el('name').textContent = s.name;
    document.title = s.name;

    if (!paletteNames) {
      paletteNames = s.palettes;
      paletteNames.forEach((name, i) => el('palettes').appendChild(makeButton(name, () => send('palette?id=' + i))));
    }
    const mode = MODES.find(m => m[0] === s.mode);
    const paletteMode = s.mode === 5 || s.mode === 6;
    let text = s.on ? 'On' : 'Off';
    if (mode) text += ' · ' + mode[1];
    if (paletteMode) text += ' · ' + paletteNames[s.palette];
    el('status').textContent = text;

    el('power-on').classList.toggle('active', s.on);
    el('power-off').classList.toggle('active', !s.on);
    [...el('modes').children].forEach((b, i) => b.classList.toggle('active', s.on && MODES[i][0] === s.mode));
    [...el('palettes').children].forEach((b, i) => b.classList.toggle('active', paletteMode && i === s.palette));

    // Don't move a control the user is currently dragging
    if (document.activeElement !== el('colour')) el('colour').value = s.colour;
    if (document.activeElement !== el('brightness')) el('brightness').value = s.brightness;
    el('brightness-value').textContent = Math.round(s.brightness * 100 / 255) + '%';
    // kelvin is 0 when an RGB colour was picked rather than a white
    if (document.activeElement !== el('kelvin') && s.kelvin) el('kelvin').value = s.kelvin;
    el('kelvin-value').textContent = s.kelvin ? s.kelvin + ' K' : '';
    if (document.activeElement !== el('speed')) el('speed').value = s.speed;
    el('speed-value').textContent = s.speed + ' / 10';
    el('sleep-value').textContent = s.sleep ? s.sleep + ' min left' : 'off';
    [...el('sleep').children].forEach((b, i) => b.classList.toggle('active', SLEEP_CHOICES[i][0] === 0 && !s.sleep));

    const rows = [
      ['Address', s.host + '.local \u00b7 ' + s.ip],
      ['Wi-Fi', s.rssi + ' dBm (' + signalWord(s.rssi) + ')'],
      ['Home Assistant', (s.mqtt ? 'Connected' : 'Not connected') + ' to MQTT broker ' + s.broker],
      ['Alexa', s.alexa ? '"' + s.alexa + '"' : 'Off'],
      ['Free memory', Math.round(s.heap / 1024) + ' KB'],
      ['Firmware', 'v' + s.version + ', up ' + s.uptime]
    ];
    el('network').replaceChildren(...rows.flatMap(([k, v]) => {
      const dt = document.createElement('dt'); dt.textContent = k;
      const dd = document.createElement('dd'); dd.textContent = v;
      return [dt, dd];
    }));
  }

  function signalWord(rssi) {
    if (rssi >= -60) return 'strong';
    if (rssi >= -72) return 'good';
    if (rssi >= -82) return 'weak';
    return 'very weak';
  }

  MODES.forEach(([id, label]) => el('modes').appendChild(makeButton(label, () => send('mode?id=' + id))));
  el('power-on').addEventListener('click', () => send('power?on=1'));
  el('power-off').addEventListener('click', () => send('power?on=0'));
  el('colour').addEventListener('input', throttle(() => send('colour?hex=' + el('colour').value.slice(1)), 150));
  el('brightness').addEventListener('input', throttle(() => send('brightness?value=' + el('brightness').value), 150));
  el('kelvin').addEventListener('input', throttle(() => send('temperature?kelvin=' + el('kelvin').value), 150));
  el('speed').addEventListener('input', throttle(() => send('speed?value=' + el('speed').value), 150));
  SLEEP_CHOICES.forEach(([minutes, label]) => el('sleep').appendChild(makeButton(label, () => send('sleep?minutes=' + minutes))));

  // Start: ask for the state now, then every 5 seconds while the page is visible
  send('state');
  setInterval(() => { if (!document.hidden) send('state'); }, 5000);
</script>
</body>
</html>
)page";
