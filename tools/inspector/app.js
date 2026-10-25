"use strict";
// UI translations are separate from names read from the installed game edition.
const german = {
  "LIVE COMPANION": "SPIELBEGLEITER",
  "Inspector": "Trainer",
  "Connecting": "Verbindung wird hergestellt",
  "Simulation controls": "Simulationssteuerung",
  "Pause": "Pause",
  "Resume": "Fortsetzen",
  "Step 1 frame": "1 Bild weiter",
  "Step 1 second": "1 Sekunde weiter",
  "Save checkpoint": "Spielstand sichern",
  "Checkpoint recovery": "Spielstand wiederherstellen",
  "Saved checkpoints": "Gesicherte Spielstände",
  "Restore checkpoint": "Spielstand wiederherstellen",
  "Open session folder": "Sitzungsordner öffnen",
  "No checkpoints yet": "Noch keine Sicherung vorhanden",
  "Restoring first saves a recovery checkpoint, then returns to the office paused. Undo history is cleared.": "Vor der Wiederherstellung wird der aktuelle Stand gesichert. Danach bleibt das Spiel im Büro pausiert. Der Rückgängig-Verlauf wird geleert.",
  "Checkpoint restored. Previous state saved as {name}.": "Spielstand wiederhergestellt. Vorheriger Stand als {name} gesichert.",
  "Session folder opened.": "Sitzungsordner geöffnet.",
  "Pause before restoring a checkpoint": "Vor der Wiederherstellung pausieren",
  "Checkpoint could not be restored": "Spielstand konnte nicht wiederhergestellt werden",
  "Unknown checkpoint in this session": "Unbekannter Spielstand in dieser Sitzung",
  "Checkpoint is outside this session": "Spielstand liegt außerhalb dieser Sitzung",
  "Undo last edit": "Letzte Änderung rückgängig",
  "Live game state": "Aktueller Spielzustand",
  "Game time": "Spielzeit",
  "Player cash": "Eigenes Guthaben",
  "Screen / tick": "Bildschirm / Takt",
  "Applied edits": "Änderungen",
  "Loading the live field catalogue...": "Felder werden geladen...",
  "Explore": "Bereich",
  "Hospital": "Krankenhaus",
  "All hospitals": "Alle Krankenhäuser",
  "At home / applicants": "Zu Hause / Bewerber",
  "Find a record": "Datensatz suchen",
  "Name or record number": "Name oder Datensatznummer",
  "Refresh list": "Liste aktualisieren",
  "Game records": "Spieldatensätze",
  "Select a record": "Datensatz auswählen",
  "Find a field": "Feld suchen",
  "Editable only": "Nur bearbeitbare Felder",
  "Pause to edit. Changes take effect in live memory; the game may recalculate them when it resumes. Linked assignments and structural fields are read-only.": "Zum Bearbeiten pausieren. Änderungen wirken sofort im laufenden Spiel; beim Fortsetzen kann das Spiel Werte neu berechnen. Zuordnungen und strukturelle Felder sind schreibgeschützt.",
  "Field": "Feld",
  "Value": "Wert",
  "Edit": "Bearbeiten",
  "Read only": "Schreibgeschützt",
  "Selected record bytes": "Bytes des ausgewählten Datensatzes",
  "All save blocks - read-only": "Alle Speicherblöcke - schreibgeschützt",
  "Block": "Block",
  "Byte offset": "Byteposition",
  "Read 256 bytes": "256 Bytes lesen",
  "Edit history": "Änderungsverlauf",
  "Owned test copy": "Separate Spielkopie",
  "Stop session": "Sitzung beenden",
  "LIVE VALUE": "AKTUELLER WERT",
  "New value": "Neuer Wert",
  "Cancel": "Abbrechen",
  "Apply change": "Änderung übernehmen",
  "Language": "Sprache",
  "Session ended": "Sitzung beendet",
  "Paused": "Pausiert",
  "Live": "Live",
  "Stepping {n}": "Noch {n} Bilder",
  "Day {day}": "Tag {day}",
  "The current game screen stops the simulation clock": "Der aktuelle Spielbildschirm hält die Simulationsuhr an",
  "Game clock active": "Spieluhr läuft",
  "Unassigned": "Nicht zugeordnet",
  "Hospital {n}": "Krankenhaus {n}",
  "{name} (player)": "{name} (Spieler)",
  "{name} (dealer)": "{name} (Händler)",
  "{n} records": "{n} Datensätze",
  "first 100": "erste 100",
  "Field definitions: {source}. Block {block}, {stride} bytes per record. Offsets are hexadecimal.": "Felddefinitionen: {source}. Block {block}, {stride} Bytes pro Datensatz. Positionen sind hexadezimal.",
  "Edit {name}": "{name} bearbeiten",
  "Current: {value}. Allowed range: {min} to {max}.": "Aktuell: {value}. Zulässiger Bereich: {min} bis {max}.",
  "No active edits.": "Keine aktiven Änderungen.",
  "Saved checkpoint {name} in the session folder.": "Spielstand {name} im Sitzungsordner gesichert.",
  "Session stopped. Your test copy and edit log are retained.": "Sitzung beendet. Spielkopie und Änderungsprotokoll bleiben erhalten.",
  "Paused. Select a value to edit.": "Pausiert. Einen Wert zum Bearbeiten auswählen.",
  "Live values refresh twice per second.": "Aktuelle Werte werden zweimal pro Sekunde aktualisiert.",
  "Advancing the paused game.": "Das pausierte Spiel wird weitergeschaltet.",
  "Enter a whole number.": "Bitte eine ganze Zahl eingeben.",
  "Change applied to the live game. Undo is available while the value still matches.": "Änderung im laufenden Spiel übernommen. Sie kann rückgängig gemacht werden, solange der Wert unverändert bleibt.",
  "Last edit reversed.": "Letzte Änderung rückgängig gemacht.",
  "Open the full local URL printed by the launcher, including its session code.": "Bitte die vollständige lokale Adresse aus dem Startfenster öffnen, einschließlich Sitzungscode.",
  "Session folder: {path}": "Sitzungsordner: {path}",
  "Live values refresh twice per second. Pause to make an edit.": "Aktuelle Werte werden zweimal pro Sekunde aktualisiert. Zum Bearbeiten pausieren.",
  "Request failed ({status})": "Anfrage fehlgeschlagen ({status})",
  "Hospitals": "Krankenhäuser",
  "Staff": "Personal",
  "Patients": "Patienten",
  "Employment slots": "Beschäftigungsplätze",
  "Room slots": "Raumplätze",
  "Receptions": "Rezeptionen",
  "Bed wards": "Krankenzimmer",
  "Neurology wards": "Neurologische Stationen",
  "Blood banks": "Blutbanken",
  "Stores": "Lager",
  "Cash": "Guthaben",
  "In business flag": "Betriebsstatus",
  "Owned plots": "Gekaufte Grundstücke",
  "Rented plots": "Gemietete Grundstücke",
  "Built plots": "Bebaute Grundstücke",
  "Ward price divisor": "Preisteiler: Krankenzimmer",
  "Intensive care price divisor": "Preisteiler: Intensivstation",
  "Operation price divisor": "Preisteiler: Operationen",
  "Equipment price divisor": "Preisteiler: Geräte",
  "Doctor price divisor": "Preisteiler: Ärzte",
  "Nurse price divisor": "Preisteiler: Pflegepersonal",
  "Material price divisor": "Preisteiler: Material",
  "Invoice base fee": "Rechnungsgrundgebühr",
  "Price index (cached)": "Preisindex (zwischengespeichert)",
  "Opinion sum / score (shared cache)": "Meinungssumme / Punktzahl (gemeinsamer Zwischenspeicher)",
  "Discharged and billed": "Entlassen und abgerechnet",
  "Cured patients": "Geheilte Patienten",
  "Uncured / pathology cases": "Ungeheilte / Pathologiefälle",
  "Operations started": "Begonnene Operationen",
  "Deaths counter": "Todesfälle",
  "Donors": "Spender",
  "Blood donated": "Gespendetes Blut",
  "Blood transfused": "Übertragenes Blut",
  "Treatment revenue today": "Behandlungseinnahmen heute",
  "Treatment revenue total": "Behandlungseinnahmen gesamt",
  "Room rent today": "Raummiete heute",
  "Room rent total": "Raummiete gesamt",
  "Plot rent today": "Grundstücksmiete heute",
  "Plot rent total": "Grundstücksmiete gesamt",
  "Personnel costs today": "Personalkosten heute",
  "Personnel costs total": "Personalkosten gesamt",
  "Investment today": "Investitionen heute",
  "Investment total": "Investitionen gesamt",
  "Days played": "Gespielte Tage",
  "Opinions above 69": "Meinungen über 69",
  "Opinions below 30": "Meinungen unter 30",
  "Sex": "Geschlecht",
  "Job group": "Berufsgruppe",
  "Training level": "Ausbildungsstufe",
  "Hospital (-1 = applicant)": "Krankenhaus (-1 = Bewerber)",
  "Duty state": "Dienststatus",
  "Bust / job-specific attribute": "Oberweite / berufsabhängiges Merkmal",
  "Waist / golf handicap": "Taille / Golfhandicap",
  "Hips / male hourly attribute": "Hüfte / stündliches Merkmal bei Männern",
  "Age": "Alter",
  "Height": "Größe",
  "Weight": "Gewicht",
  "Independent work flag": "Selbstständiges Arbeiten",
  "Specific course points": "Fachspezifische Kurspunkte",
  "General course points": "Allgemeine Kurspunkte",
  "Diligence": "Fleiß",
  "IQ": "IQ",
  "Experience": "Erfahrung",
  "Salary demand": "Gehaltsforderung",
  "Current wage (mirrored in employment slot)": "Aktueller Lohn (auch im Beschäftigungsplatz gespeichert)",
  "Mood": "Laune",
  "Dismissed by hospital": "Entlassen durch Krankenhaus",
  "Portrait": "Porträt",
  "Room / course index": "Raum- / Kursnummer",
  "Reserved job opening": "Reservierte Stelle",
  "Qualification text": "Qualifikationstext",
  "Peculiarity text": "Besonderheitstext",
  "Profession": "Beruf",
  "Hospital (-1 = home)": "Krankenhaus (-1 = zu Hause)",
  "Hospital state": "Aufenthaltsstatus",
  "Health": "Gesundheit",
  "Baseline health": "Grundgesundheit",
  "Blood": "Blutmenge",
  "Ailment": "Beschwerde",
  "Diagnosed illness": "Diagnostizierte Krankheit",
  "Treatment": "Behandlung",
  "Actual illness": "Tatsächliche Krankheit",
  "Cosmetic wish": "Kosmetischer Wunsch",
  "In treatment flag": "In Behandlung",
  "Treating hospital": "Behandelndes Krankenhaus",
  "Treatment rank": "Behandlungsrang",
  "Treatment rounds": "Behandlungsrunden",
  "Death counter": "Todeszähler",
  "Treatment days remaining": "Verbleibende Behandlungstage",
  "Opinion of hospital {n}": "Meinung über Krankenhaus {n}",
  "Opinion of {name}": "Meinung über {name}",
  "Staff index (0 = empty)": "Personalnummer (0 = leer)",
  "Room / course": "Raum / Kurs",
  "Wage": "Lohn",
  "Hours since hired": "Stunden seit Einstellung",
  "Days employed": "Beschäftigungstage",
  "Hours on duty": "Stunden im Dienst",
  "Hours off duty": "Dienstfreie Stunden",
  "Mission target": "Einsatzziel",
  "Mission minutes": "Einsatzminuten",
  "Return category": "Rückkehrkategorie",
  "Return room": "Rückkehrraum",
  "Rest countdown": "Verbleibende Ruhezeit",
  "Room catalog type (255 = empty)": "Raumtyp (255 = leer)",
  "Rented flag": "Gemietet",
  "Beds": "Betten",
  "Occupied beds": "Belegte Betten",
  "Assigned staff": "Zugeordnetes Personal",
  "Waiting patient 1": "Wartender Patient 1",
  "Waiting patient 2": "Wartender Patient 2",
  "Waiting patient 3": "Wartender Patient 3",
  "Waiting patient 4": "Wartender Patient 4",
  "Blood stock": "Blutvorrat",
  "Blood capacity": "Blutkapazität",
  "Clerk": "Lagerverwalter",
  "Capacity": "Kapazität",
  "Furniture held": "Gelagerte Möbel",
  "Equipment held": "Gelagerte Geräte",
  "Pause before editing": "Vor dem Bearbeiten pausieren",
  "Value changed since it was read; refresh before editing": "Der Wert hat sich geändert. Vor dem Bearbeiten aktualisieren",
  "Value outside this field's allowed range": "Der Wert liegt außerhalb des zulässigen Bereichs",
  "Read-only field: use the game for structural changes": "Schreibgeschütztes Feld: Zuordnungen und Strukturen im Spiel ändern",
  "Pause before saving a checkpoint": "Vor dem Sichern pausieren",
  "Pause and wait for the previous step first": "Zuerst pausieren und den letzten Schritt abwarten",
  "Expected 1..50 frames": "Erwartet werden 1 bis 50 Bilder",
  "Checkpoint could not be written": "Spielstand konnte nicht gespeichert werden",
  "No edit to undo": "Keine Änderung zum Rückgängigmachen",
  "Session is closed": "Die Sitzung ist beendet",
  "Engine closed its response stream": "Das Spiel hat die Verbindung beendet",
  "Expected an integer": "Eine ganze Zahl wird erwartet",
  "Group out of range": "Ungültiger Bereich",
  "Record out of range": "Ungültige Datensatznummer",
  "Field out of range": "Ungültige Feldnummer",
  "Raw block or offset out of range": "Ungültiger Speicherblock oder ungültige Byteposition",
  "Open this session's printed URL": "Die ausgegebene Adresse dieser Sitzung öffnen",
  "Failed to fetch": "Verbindung fehlgeschlagen",
  "NetworkError when attempting to fetch resource.": "Netzwerkverbindung fehlgeschlagen",
  "Unknown action": "Unbekannte Aktion",
  "Unknown command or invalid integer arguments": "Unbekannter Befehl oder ungültige ganzzahlige Argumente",
  "Invalid command framing": "Ungültiges Befehlsformat",
  "Unexpected host": "Unerwarteter Host",
  "Cross-origin access refused": "Zugriff von einer anderen Website abgelehnt",
  "Expected a non-negative integer parameter": "Eine nicht negative ganze Zahl wird erwartet",
  "Expected application/json": "application/json wird erwartet",
  "Request body must be 1..4096 bytes": "Anfragedaten müssen 1 bis 4096 Bytes umfassen",
  "Expected an object": "Ein Objekt wird erwartet",
  "Not found": "Nicht gefunden"
};
let language = "de";
try { language = localStorage.getItem("biing-trainer-language") === "en" ? "en" : "de"; } catch { /* Storage may be disabled. */ }
function t(key, values = {}) {
  return (language === "de" ? german[key] || key : key).replace(/\{(\w+)\}/g, (_, k) => String(values[k] ?? `{${k}}`));
}
// The DOM application starts here; pure translation checks use the functions above.
const $ = id => document.getElementById(id);
const token = location.hash.slice(1);
let schema, group = 0, record = 0, records = [], snapshot, state, generation = 0, busy = false, stopped = false;
let editing = null;
let hospitals = [];
let checkpoints = [];
const rows = new Map();
const fmt = n => Number(n).toLocaleString(language === "de" ? "de-DE" : "en-US");
function hospitalName(id) {
  if (id < 0) return t("Unassigned");
  const name = hospitals[id]?.name;
  return name && name !== `Hospitals ${id}` ? name : t("Hospital {n}", {n: id});
}
function recordName(r, g = group) {
  if (g === 0) return hospitalName(r.id ?? r.record);
  const generic = `${schema.groups[g].name} ${r.id ?? r.record}`;
  return r.name === generic ? `${t(schema.groups[g].name)} ${r.id ?? r.record}` : r.name;
}
function fieldName(field) {
  const match = /^Opinion of hospital (\d+)$/.exec(field.name);
  return match ? t("Opinion of {name}", {name: hospitalName(Number(match[1]))}) : t(field.name);
}
function translatePage() {
  document.documentElement.lang = language;
  document.title = language === "de" ? "Biing! Trainer" : "Biing! Inspector";
  for (const el of document.querySelectorAll("[data-i18n]")) el.textContent = t(el.dataset.i18n);
  for (const el of document.querySelectorAll("[data-i18n-placeholder]")) el.placeholder = t(el.dataset.i18nPlaceholder);
  for (const el of document.querySelectorAll("[data-i18n-aria]")) el.setAttribute("aria-label", t(el.dataset.i18nAria));
  $("language").value = language;
}
function populateChoices() {
  const hospital = $("hospital").value, block = $("block").value;
  $("group").replaceChildren(...schema.groups.map(g => new Option(t(g.name), g.id)));
  $("group").value = group;
  $("hospital").replaceChildren(new Option(t("All hospitals"), "all"), new Option(t("At home / applicants"), -1));
  for (const h of hospitals) $("hospital").add(new Option(h.id === 0 ? t("{name} (player)", {name: hospitalName(h.id)}) : h.id === 16 ? t("{name} (dealer)", {name: hospitalName(h.id)}) : hospitalName(h.id), h.id));
  $("hospital").value = hospital || "all";
  $("block").replaceChildren(...schema.blocks.map((size, i) => new Option(`${i}: ${fmt(size)} Bytes`, i)));
  $("block").value = block || "0";
  $("session").textContent = t("Session folder: {path}", {path: schema.session});
}
function message(text, error = false) { $("message").textContent = t(text); $("message").classList.toggle("error", error); }
async function api(path, data) {
  const options = {headers: {Authorization: `Bearer ${token}`}, cache: "no-store"};
  if (data !== undefined) { options.method = "POST"; options.headers["Content-Type"] = "application/json"; options.body = JSON.stringify(data); }
  const response = await fetch(`/api/${path}`, options);
  const result = await response.json();
  if (!response.ok) throw new Error(t(result.error || "Request failed ({status})", {status: response.status}));
  return result;
}
function showState(s) {
  state = s;
  if (s.closed || s.quit) {
    stopped = true;
    $("connection").textContent = t("Session ended");
    $("connection").className = "pill";
    for (const id of ["pause", "step", "second", "checkpoint", "undo", "stop", "restore", "folder", "checkpoints"]) $(id).disabled = true;
    for (const row of rows.values()) if (row.button) row.button.disabled = true;
    return;
  }
  const idle = s.paused && !s.steps;
  $("connection").textContent = s.steps ? t("Stepping {n}", {n: s.steps}) : t(s.paused ? "Paused" : "Live");
  $("connection").className = `pill ${s.paused ? "paused" : "live"}`;
  $("time").textContent = `${t("Day {day}", {day: s.day})} · ${String(s.hour).padStart(2, "0")}:${String(s.minute).padStart(2, "0")}`;
  $("cash").textContent = fmt(s.cash);
  $("screen").textContent = `${s.screen} / ${s.sim_tick}`;
  $("screen").title = t(s.clock_stopped ? "The current game screen stops the simulation clock" : "Game clock active");
  $("edits").textContent = fmt(s.edits);
  $("pause").textContent = t(s.paused ? "Resume" : "Pause");
  $("pause").disabled = busy;
  for (const id of ["step", "second", "checkpoint", "undo"]) $(id).disabled = !idle || busy;
  $("restore").disabled = !idle || busy || !checkpoints.length;
  $("checkpoints").disabled = busy || !checkpoints.length;
  $("folder").disabled = busy;
  for (const row of rows.values()) if (row.button) row.button.disabled = !idle || busy;
}
function hex(bytes, start = 0) {
  const lines = [];
  for (let i = 0; i < bytes.length; i += 16) {
    const part = bytes.slice(i, i + 16);
    lines.push(`${(start + i).toString(16).padStart(4, "0")}  ${part.map(x => x.toString(16).padStart(2, "0")).join(" ").padEnd(47)}  ${part.map(x => x >= 32 && x < 127 ? String.fromCharCode(x) : ".").join("")}`);
  }
  return lines.join("\n");
}
function makeFields() {
  rows.clear(); $("fields").replaceChildren();
  const g = schema.groups[group];
  $("category").textContent = t(g.name).toUpperCase();
  $("source").textContent = t("Field definitions: {source}. Block {block}, {stride} bytes per record. Offsets are hexadecimal.", {source: g.source, block: g.block, stride: g.stride});
  for (const field of g.fields) {
    const tr = document.createElement("tr");
    const label = document.createElement("td"); label.textContent = fieldName(field);
    const details = document.createElement("small");
    details.textContent = `+0x${field.offset.toString(16).toUpperCase()} · ${field.signed ? "s" : "u"}${field.width * 8}`;
    label.append(details);
    const value = document.createElement("td"); value.textContent = "--";
    const action = document.createElement("td");
    let button;
    if (field.editable) {
      button = document.createElement("button"); button.className = "quiet"; button.textContent = t("Edit"); button.disabled = true;
      button.setAttribute("aria-label", t("Edit {name}", {name: fieldName(field)}));
      button.onclick = () => openEdit(field);
      action.append(button);
    } else { action.textContent = t("Read only"); action.className = "hint"; }
    tr.append(label, value, action); $("fields").append(tr);
    rows.set(field.id, {tr, value, button, field});
  }
  filterFields();
}
function filterFields() {
  const query = $("field-search").value.toLowerCase();
  for (const row of rows.values()) row.tr.hidden = !`${fieldName(row.field)} ${row.field.name}`.toLowerCase().includes(query) || ($("editable").checked && !row.field.editable);
}
function showRecord(s) {
  if (s.group !== group || s.record !== record) return;
  snapshot = s; showState(s);
  $("name").textContent = recordName(s);
  $("record-id").textContent = `#${record} · ${hospitalName(s.hospital)}`;
  for (const [id, row] of rows) {
    const next = String(s.values[id]);
    if (row.value.textContent !== next) {
      const wasNumber = row.value.textContent !== "--";
      row.value.textContent = next;
      if (wasNumber) { row.value.classList.remove("changed"); void row.value.offsetWidth; row.value.classList.add("changed"); }
    }
  }
  $("bytes").textContent = hex(s.bytes);
}
function filterRecords() {
  const query = $("search").value.toLowerCase(), hospital = $("hospital").value;
  const found = records.filter(r => (hospital === "all" || r.hospital === Number(hospital)) && `${r.id} ${recordName(r)} ${hospitalName(r.hospital)}`.toLowerCase().includes(query));
  $("count").textContent = `${t("{n} records", {n: found.length})}${found.length > 100 ? " · " + t("first 100") : ""}`;
  $("records").replaceChildren();
  for (const r of found.slice(0, 100)) {
    const button = document.createElement("button"); button.className = `record${r.id === record ? " selected" : ""}`;
    button.textContent = recordName(r);
    const sub = document.createElement("small"); sub.textContent = `#${r.id} · ${hospitalName(r.hospital)}`;
    button.append(sub); button.onclick = () => selectRecord(r.id); $("records").append(button);
  }
}
async function selectRecord(id) {
  record = id; snapshot = null; const version = ++generation; filterRecords();
  for (const row of rows.values()) { row.value.textContent = "--"; if (row.button) row.button.disabled = true; }
  try { const s = await api(`record?group=${group}&record=${record}`); if (version === generation) showRecord(s); }
  catch (error) { message(error.message, true); }
}
async function loadRecords(reset = false) {
  const selectedGroup = group;
  const result = await api(`records?group=${group}`);
  if (selectedGroup !== group) return;
  records = result.records;
  if (reset) record = 0;
  filterRecords(); await selectRecord(record);
}
function openEdit(field) {
  if (!snapshot || !state.paused || state.steps || stopped) return;
  editing = {group, record, field: field.id, before: snapshot.values[field.id]};
  $("edit-title").textContent = fieldName(field);
  $("edit-range").textContent = t("Current: {value}. Allowed range: {min} to {max}.", {value: editing.before, min: field.min, max: field.max});
  $("new-value").min = field.min; $("new-value").max = field.max; $("new-value").value = editing.before;
  $("edit-error").textContent = ""; $("edit-dialog").showModal(); $("new-value").select();
}
async function history() {
  const entries = await api("history"); $("history").replaceChildren();
  if (!entries.length) { const li = document.createElement("li"); li.textContent = t("No active edits."); $("history").append(li); }
  for (const entry of entries.slice(-100).reverse()) {
    const g = schema.groups[entry.group], li = document.createElement("li");
    const label = entry.group === 0 ? hospitalName(entry.record) : `${t(g.name)} #${entry.record}`;
    li.textContent = `${label}: ${fieldName(g.fields[entry.field])} ${entry.before} -> ${entry.value}`;
    $("history").append(li);
  }
}
function showCheckpoints() {
  const selected = $("checkpoints").value;
  $("checkpoints").replaceChildren(...checkpoints.map(c => new Option(
    `${new Date(c.created).toLocaleString(language === "de" ? "de-DE" : "en-US")} / ${c.name}`, c.name)));
  if (!checkpoints.length) $("checkpoints").add(new Option(t("No checkpoints yet"), ""));
  else if (checkpoints.some(c => c.name === selected)) $("checkpoints").value = selected;
  if (state) showState(state);
}
async function refreshCheckpoints() { checkpoints = await api("checkpoints"); showCheckpoints(); }
async function action(name, extra = {}) {
  if (busy || stopped) return;
  busy = true; if (state) showState(state);
  if (name === "restore") { generation++; snapshot = null; editing = null; $("edit-dialog").close(); }
  try {
    const result = await api("action", {action: name, ...extra});
    if (name === "checkpoint") {
      showState(result.state); await refreshCheckpoints(); $("checkpoints").value = result.name;
      message(t("Saved checkpoint {name} in the session folder.", {name: result.name}));
    } else if (name === "restore") {
      showState(result); await loadRecords(); await history(); await refreshCheckpoints();
      message(t("Checkpoint restored. Previous state saved as {name}.", {name: result.backup}));
    } else if (name === "folder") message("Session folder opened.");
    else { showState(result); message(name === "stop" ? "Session stopped. Your test copy and edit log are retained." : name === "pause" ? "Paused. Select a value to edit." : name === "resume" ? "Live values refresh twice per second." : "Advancing the paused game."); }
  } catch (error) { message(error.message, true); }
  finally { busy = false; if (state) showState(state); }
}
$("edit-form").onsubmit = async event => {
  event.preventDefault(); if (busy || !editing) return;
  const text = $("new-value").value;
  if (!/^-?\d+$/.test(text) || !Number.isSafeInteger(Number(text))) { $("edit-error").textContent = t("Enter a whole number."); return; }
  busy = true;
  try {
    const result = await api("edit", {...editing, value: Number(text)});
    showRecord(result); $("edit-dialog").close(); message("Change applied to the live game. Undo is available while the value still matches."); await history();
  } catch (error) { $("edit-error").textContent = t(error.message); }
  finally { busy = false; if (state) showState(state); }
};
$("cancel").onclick = () => $("edit-dialog").close();
$("pause").onclick = () => { if (state) void action(state.paused ? "resume" : "pause"); };
$("step").onclick = () => action("step", {frames: 1});
$("second").onclick = () => action("step", {frames: 50});
$("checkpoint").onclick = () => action("checkpoint");
$("restore").onclick = () => action("restore", {name: $("checkpoints").value});
$("folder").onclick = () => action("folder");
$("stop").onclick = () => action("stop");
$("undo").onclick = async () => {
  if (busy) return; busy = true;
  try { showRecord(await api("undo", {})); message("Last edit reversed."); await history(); }
  catch (error) { message(error.message, true); }
  finally { busy = false; if (state) showState(state); }
};
$("group").onchange = async () => {
  group = Number($("group").value); generation++; snapshot = null; $("search").value = ""; makeFields();
  try { await loadRecords(true); } catch (error) { message(error.message, true); }
};
$("hospital").onchange = $("search").oninput = filterRecords;
$("field-search").oninput = $("editable").onchange = filterFields;
$("refresh").onclick = () => loadRecords().catch(error => message(error.message, true));
$("raw-read").onclick = async () => {
  try { const offset = Number($("offset").value); const result = await api(`raw?block=${$("block").value}&offset=${offset}`); $("raw").textContent = hex(result.bytes, offset); }
  catch (error) { message(error.message, true); }
};
$("block").onchange = () => { $("offset").value = 0; $("offset").max = schema.blocks[Number($("block").value)] - 1; };
async function poll() {
  try {
    if (!busy && !stopped) {
      const version = generation;
      const s = await api(`record?group=${group}&record=${record}`);
      if (version === generation) showRecord(s);
    }
  } catch (error) {
    message(error.message, true);
    try { const s = await api("status"); if (s.closed) showState(s); } catch { /* Keep the original error visible. */ }
  }
  if (!stopped) setTimeout(poll, 500);
}
async function start() {
  if (!token) throw new Error(t("Open the full local URL printed by the launcher, including its session code."));
  schema = await api("schema");
  hospitals = (await api("records?group=0")).records;
  populateChoices();
  makeFields(); await loadRecords(true); await history(); await refreshCheckpoints();
  message("Live values refresh twice per second. Pause to make an edit.");
  void poll();
}
$("language").onchange = async () => {
  language = $("language").value;
  try { localStorage.setItem("biing-trainer-language", language); } catch { /* Keep the session choice. */ }
  translatePage();
  if (!schema) return;
  $("edit-dialog").close(); editing = null;
  populateChoices(); makeFields(); filterRecords(); showCheckpoints();
  if (snapshot) showRecord(snapshot); else if (state) showState(state);
  message(stopped ? "Session stopped. Your test copy and edit log are retained." : state?.paused ? "Paused. Select a value to edit." : "Live values refresh twice per second. Pause to make an edit.");
  if (!stopped) { try { await history(); } catch (error) { message(error.message, true); } }
};
translatePage();
start().catch(error => message(error.message, true));
