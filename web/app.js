// Trie dictionary page. Talks to jss_server's GET /api/search.
// Builds every element with textContent, never innerHTML, so dictionary text
// can't inject markup.
"use strict";

const input = document.getElementById("q");
const form = document.getElementById("search-form");
const romaji = document.getElementById("romaji");
const statusLine = document.getElementById("status");
const results = document.getElementById("results");
const pathView = document.getElementById("path");
const modeButtons = [...document.querySelectorAll(".mode button")];

// A few common JMdict part-of-speech codes; others are shown as the code.
const POS = {
  n: "noun", "n-suf": "noun suffix", "n-pref": "noun prefix", pn: "pronoun",
  "adj-i": "i-adjective", "adj-na": "na-adjective", "adj-no": "no-adjective",
  "adj-pn": "pre-noun adjectival", "adj-t": "taru-adjective", adv: "adverb",
  "adv-to": "adverb (to)", aux: "auxiliary", "aux-v": "auxiliary verb", conj: "conjunction",
  ctr: "counter", exp: "expression", int: "interjection", num: "numeric", pref: "prefix",
  prt: "particle", suf: "suffix", cop: "copula",
  v1: "ichidan verb", v5u: "godan verb (u)", v5k: "godan verb (ku)", "v5k-s": "godan verb (iku)",
  v5g: "godan verb (gu)", v5s: "godan verb (su)", v5t: "godan verb (tsu)", v5n: "godan verb (nu)",
  v5b: "godan verb (bu)", v5m: "godan verb (mu)", v5r: "godan verb (ru)", "v5r-i": "godan verb (ru, irregular)",
  v5aru: "godan verb (aru)", vk: "kuru verb", vs: "suru noun", "vs-i": "suru verb", "vs-s": "suru verb (special)",
  vz: "zuru verb", vt: "transitive", vi: "intransitive",
};

let mode = "prefix";
let pending = null;  // AbortController of the request in flight
let timer = 0;

function el(tag, className, text) {
  const node = document.createElement(tag);
  if (className) node.className = className;
  if (text !== undefined) node.textContent = text;
  return node;
}

function setMode(next) {
  mode = next;
  for (const b of modeButtons) b.setAttribute("aria-checked", String(b.dataset.mode === mode));
}

// --- Rendering ---------------------------------------------------------------

function renderPath(data) {
  pathView.replaceChildren();
  if (data.mode !== "prefix" || !data.searched) {
    pathView.hidden = true;
    return;
  }
  pathView.hidden = false;
  pathView.append(el("span", "label", "Trie walk:"));
  const chars = [...data.searched];
  data.path.forEach((node, i) => {
    if (i > 0) pathView.append(el("span", "edge"));
    const circle = el("span", "node" + (node.terminal ? " terminal" : ""), node.char);
    circle.title = node.terminal ? `${chars.slice(0, i + 1).join("")} is a word` : "not a word on its own";
    pathView.append(circle);
  });
  if (data.walk_stopped) {
    const missing = chars[data.path.length];
    if (data.path.length > 0) pathView.append(el("span", "edge"));
    const node = el("span", "node missing", missing);
    node.title = "no child for this character: the walk stops here";
    pathView.append(node);
  }
  pathView.append(el("span", "legend",
    "Filled = a word ends here. Dashed = no child node, so the walk stops."));
}

function readingText(entry) {
  const span = el("span", "readings");
  entry.readings.forEach((r, i) => {
    if (i > 0) span.append("、");
    span.append(r.text);
    if (r.pitch.length > 0) {
      const p = el("span", "pitch", r.pitch.join(","));
      p.title = "pitch accent: downstep after mora " + r.pitch.join(" or ") + " (0 = no downstep)";
      span.append(p);
    }
  });
  return span;
}

function renderEntry(entry, key) {
  const card = el("article", "card");
  const head = el("div", "card-head");

  // Headword: the form that matched if it is a kanji form, else the first kanji form,
  // else the first reading. Other kanji forms are listed beside it.
  const kanji = entry.kanji.map((k) => k.text);
  if (kanji.length > 0) {
    const main = kanji.includes(key) ? key : kanji[0];
    head.append(el("span", "headword", main));
    head.append(readingText(entry));
    const others = kanji.filter((k) => k !== main);
    if (others.length > 0) head.append(el("span", "alt", "also " + others.join("、")));
  } else {
    head.append(el("span", "headword", entry.readings[0].text));
    if (entry.readings.length > 1 || entry.readings[0].pitch.length > 0) head.append(readingText(entry));
  }
  if (entry.common) head.append(el("span", "badge", "common"));
  card.append(head);

  const senses = el("ol", "senses");
  for (const sense of entry.senses) {
    const li = el("li");
    for (const code of sense.pos) {
      const chip = el("span", "pos", POS[code] || code);
      chip.title = code;
      li.append(chip);
    }
    li.append(sense.glosses.join("; "));
    senses.append(li);
  }
  card.append(senses);
  return card;
}

function render(data) {
  renderPath(data);
  results.replaceChildren();

  const count = data.total;
  let status = count === 0 ? "No matches." : `${count} ${count === 1 ? "word" : "words"}`;
  if (data.mode === "predictive" && count > data.matches.length) status += `, showing the ${data.matches.length} most common`;
  if (count > 0) status += ` · ${(data.elapsed_us / 1000).toFixed(2)} ms on the server`;
  if (data.searched !== data.query) status += ` · searched as ${data.searched}`;
  if (data.truncated) status += " · query cut to 64 characters";
  statusLine.textContent = status;

  for (const match of data.matches) {
    const section = el("section", "match");
    const head = el("h2", "match-head");
    head.append(el("span", "key", match.key));
    if (data.mode === "prefix") {
      const rest = data.searched.slice(match.key.length);  // both strings start with the key
      if (rest) head.append(el("span", "rest ja", rest));
      head.append(`${match.length} ${match.length === 1 ? "character" : "characters"}`);
    }
    section.append(head);
    for (const entry of match.entries) section.append(renderEntry(entry, match.key));
    results.append(section);
  }
}

// --- Searching ---------------------------------------------------------------

async function search() {
  const q = input.value.trim();
  const url = new URL(location.href);
  url.search = q ? new URLSearchParams({ q, mode }).toString() : "";
  history.replaceState(null, "", url);

  if (pending) pending.abort();
  if (!q) {
    results.replaceChildren();
    pathView.hidden = true;
    statusLine.textContent = "";
    return;
  }
  pending = new AbortController();
  try {
    const params = new URLSearchParams({ q, mode, limit: "50" });
    const res = await fetch(`/api/search?${params}`, { signal: pending.signal });
    const data = await res.json();
    if (!res.ok) throw new Error(data.error || res.statusText);
    render(data);
  } catch (err) {
    if (err.name === "AbortError") return;
    statusLine.textContent = "Search failed: " + err.message + " (is jss_server running?)";
  }
}

function searchSoon() {
  clearTimeout(timer);
  timer = setTimeout(search, 120);
}

// --- Romaji input (wanakana, loaded from a CDN; the page works without it) -----

function setRomaji(on) {
  if (!window.wanakana) {
    romaji.disabled = true;
    romaji.parentElement.title = "wanakana did not load (offline?)";
    return;
  }
  if (on) window.wanakana.bind(input, { IMEMode: true });
  else window.wanakana.unbind(input);
}

// --- Wiring ------------------------------------------------------------------

input.addEventListener("input", searchSoon);
form.addEventListener("submit", (e) => { e.preventDefault(); search(); });
romaji.addEventListener("change", () => setRomaji(romaji.checked));
for (const b of modeButtons) {
  b.addEventListener("click", () => { setMode(b.dataset.mode); search(); });
}
for (const b of document.querySelectorAll(".examples button")) {
  b.addEventListener("click", () => {
    input.value = b.dataset.q;
    setMode(b.dataset.mode);
    search();
  });
}

const initial = new URLSearchParams(location.search);
if (initial.get("mode") === "predictive") setMode("predictive");
if (initial.get("q")) input.value = initial.get("q");
setRomaji(romaji.checked);
if (input.value) search();
