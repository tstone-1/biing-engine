# Biing! und Trainer: Schnellstart

Du brauchst deine eigenen Spieldateien: deutsche Diskversion 1.4 oder deutsche bzw.
englische GOG-CD-Ausgabe. Dieses experimentelle Projekt enthält keine Originaldateien.
Die [bekannten Unterschiede und Prüfgrenzen](KNOWN-LIMITATIONS.md) stehen auf Englisch
zur Verfügung. Eine vollständige Gleichheit mit dem Original wird nicht versprochen.

## Herunterladen und spielen

Die [Testversion 0.1.0-rc.1](https://github.com/tstone-1/biing-engine/releases/tag/v0.1.0-rc.1)
enthält fertige Apps. Python, Compiler und Entwicklungswerkzeuge sind nicht nötig:

- [Windows 11, x86-64](https://github.com/tstone-1/biing-engine/releases/download/v0.1.0-rc.1/Biing-0.1.0-rc.1-windows-x86_64-signed.zip): die gesamte ZIP-Datei entpacken und `Biing/Biing.exe` öffnen. Signiert und mit Zeitstempel versehen.
- [macOS 27+, Apple Silicon](https://github.com/tstone-1/biing-engine/releases/download/v0.1.0-rc.1/Biing-0.1.0-rc.1-macos-arm64-notarized.zip): die ZIP-Datei entpacken und `Biing.app` öffnen. Signiert, notarisiert und mit Apple-Prüfticket versehen.

**Spieldateien hinzufügen** oder **Spielordner hinzufügen** wählen, die eigene Kopie
importieren und auf **Spielen** klicken. Für den Trainer **Neue Trainersitzung** und
**Trainer starten** wählen. Vor Änderungen das Spiel pausieren. Originaldateien
bleiben unverändert; Spielstände liegen außerhalb der App.

Die Downloads sind experimentell. Manuelle Windows-Prüfungen für Eingabe, Fokus und
Erststart, Trainerbedienung im Browser sowie MIDI/CD-Hörtests stehen noch aus.
Prüfsummen, passende Quellcodearchive und Lizenzhinweise sind enthalten bzw. beim
Download verfügbar.

## Aus dem Quellcode starten (optional)

- **macOS:** [Homebrew](https://brew.sh/) installieren, dann im Terminal
  `brew install cmake ninja sdl3 uv` ausführen.
- **Windows:** [MSYS2](https://www.msys2.org/) installieren und aktualisieren. Im
  **CLANG64-Terminal** den folgenden Befehl ausführen:

  ```sh
  pacman -S --needed mingw-w64-clang-x86_64-clang mingw-w64-clang-x86_64-cmake mingw-w64-clang-x86_64-ninja mingw-w64-clang-x86_64-sdl3
  ```

  [uv](https://docs.astral.sh/uv/getting-started/installation/) installieren und das
  Terminal neu öffnen. `uv --version` muss funktionieren.

Quellcode herunterladen und entpacken oder das Repository klonen. Ein Terminal im
Quellcodeordner öffnen, unter Windows das CLANG64-Terminal. Dann:

```sh
uv run --no-project python tools/play.py
```

Beim ersten Start den Pfad zu deinem Spielarchiv, deiner GOG-App oder deinem
installierten Spielordner eingeben. GOG-.pkg-Installer lassen sich nur auf macOS
entpacken. Ein vorhandener Spielordner funktioniert auf beiden Systemen.
Der Befehl baut das Programm und erstellt eine separate Spielkopie. Die Originale
bleiben unverändert. uv verwendet die festgelegte Python-Version 3.15.1.

Später genügt derselbe Befehl. Spielkopie und Spielstände liegen im Ordner `game`
neben dem Quellcodeordner. Diesen Ordner bei einem Update behalten. Mit `--game PFAD`
kannst du einen anderen Zielordner wählen. Ein gefüllter Ordner wird nicht überschrieben.
Bei Baufehlern die Voraussetzungen und `build/startup.log` prüfen.

## Trainer starten und Fortschritt behalten

In der fertigen App genügt **Trainer starten**. Beim Start aus dem Quellcode nach
dem ersten erfolgreichen Bau:

```sh
uv run tools/biing_inspector.py --game ../game
```

Die ausgegebene lokale Adresse vollständig im Browser öffnen. Der Trainer startet
eine spielbare Kopie neben dem Browser. Er bearbeitet diese Engine, nicht DOSBox.
Krankenhausnamen stammen aus deiner Spielausgabe. Deutsch und Englisch kannst du im
Browser umschalten.

1. **Pause** wählen, bevor du Werte bearbeitest.
2. Mit **Spielstand sichern** einen Ausgangspunkt speichern.
3. Gewünschte Werte bearbeiten. **Letzte Änderung rückgängig** setzt einen Feldwert
   zurück, sofern er noch passt. Die Spielzeit wird dadurch nicht zurückgesetzt.
4. Zur Wiederherstellung einen gesicherten Spielstand auswählen und
   **Spielstand wiederherstellen** drücken. Der aktuelle Stand wird vorher zusätzlich
   gesichert. Danach bleibt das Spiel im Büro pausiert; bei negativem Guthaben erscheint
   der Insolvenzbildschirm. Der alte Rückgängig-Verlauf wird geleert.
5. **Sitzung beenden** sichert einen abschließenden Spielstand. Vor dem direkten
   Schließen des Spielfensters oder Terminals selbst sichern.

**Sitzungsordner öffnen** zeigt die Sicherungen (`save-*.sav`), die Spielkopie und das
Änderungsprotokoll. CLI-Sitzungen liegen unter `extracted/inspector-sessions/`.
Eine frühere Sitzung lässt sich so wieder öffnen:

```sh
uv run tools/biing_inspector.py --resume PFAD-ZUM-SITZUNGSORDNER
```

Dabei entsteht eine neue Kopie; die alte bleibt erhalten. Der zuletzt gesicherte oder
wiederhergestellte Stand wird geladen, das Spiel bleibt pausiert. Ohne Sicherung kannst
du die erhaltenen `GAMES/`-Spielstände im Spiel laden. Der Trainer überträgt nichts
automatisch zurück in deine normale Spielkopie. Bei manueller Übertragung dieselbe
Spielausgabe verwenden und beide Kopien bis zur Prüfung behalten.

## Starter mit Schaltflächen

```sh
uv run --no-project python tools/launcher.py
```

Der Starter verwaltet eine eigene Bibliothek importierter Spiele außerhalb des
Quellcodeordners. Sie ist getrennt vom oben beschriebenen Ordner `../game`.
Du kannst diesen vorhandenen Spielordner über **Spielordner hinzufügen** importieren.
Danach stehen **Spielen**, **Trainer starten** und **Spielstände anzeigen** bereit.
Im Auswahlfeld für Trainersitzungen kannst du eine neue oder eine datierte frühere
Sitzung wählen. **Trainersitzungen** öffnet deren Ordner. Während der Trainer läuft,
öffnet **Trainer im Browser öffnen** seine Oberfläche erneut.

## Einstellungen und Musik

**Intro überspringen** bzw. `tools/play.py --no-intro` lässt den Vorspann aus.
**Optionale Fehlerkorrekturen aktivieren** bzw. `tools/play.py --fixes` schaltet
Korrekturen ursprünglicher Programmfehler ein. Sie betreffen unter anderem Anzeige,
Berechnungen, Geräte und Simulation und können Spielverlauf und Zufallsfolge verändern.
Vorhandene Schäden in Spielständen werden dadurch nicht vollständig repariert.
Standardmäßig bleiben diese Korrekturen aus; Schutz vor Abstürzen und Endlosschleifen
bleibt immer aktiv. Eine neue CLI-Trainersitzung akzeptiert ebenfalls `--fixes`.
Wieder geöffnete Trainersitzungen behalten ihre ursprüngliche Einstellung.

Für die Musik das Spiel schließen und in der **Arbeitskopie** die Datei `SOUND.CFG`
öffnen. Nur den Wert nach dem Bindestrich der Zeile `MCARD` ändern:

| Wert | Musik |
|---|---|
| `SB`, `SBP`, `SB16` | Gesampelte Spielmusik |
| `CD` | Importierte CD-Titel, normalerweise bei GOG |
| `GM` | General MIDI über den Systemsynthesizer |
| `ROL` | Roland MT-32; passendes Gerät oder Emulator erforderlich |

`DCARD` steuert digitale Effekte und Sprache. Andere Einträge unverändert lassen.
`BIING_MIDI_OUT=list` listet MIDI-Ausgänge beim Engine-Start auf, `none` deaktiviert
MIDI. Ein Teil des Gerätenamens wählt einen Ausgang aus. GM und MT-32 verwenden
unterschiedliche Instrumentbelegungen. Hörtests für MIDI und CD stehen noch aus.

Diese Veröffentlichung enthält zunächst Quellcode. Offizielle fertige App-Pakete
enthalten Fremdlizenzhinweise und den passenden Quellcode. Import, Laden/Speichern,
Update und Neuinstallation wurden mit neuen Standardbenutzerkonten auf macOS und
Windows geprüft; Spielstände bleiben erhalten. Prüfungen der nativen Oberfläche und
Audioausgabe stehen teilweise noch aus. Das separate Signierwerkzeug erstellt
Developer-ID-signierte und notarisierte macOS-Kopien; maßgeblich ist der Prüfbericht
des jeweiligen Pakets, nicht allein sein Dateiname.
Unter Windows bereitet `tools/sign_windows.py prepare` eine geprüfte Paketkopie ohne
Anmeldung vor. `sign` signiert die noch unsignierten Programmdateien in einem Durchgang
und prüft Signierer, Zeitstempel und Windows-Vertrauen. Gültige Herstellersignaturen
bleiben erhalten. Die Befehle und Voraussetzungen stehen im Werkzeug selbst.
Bei Fehlerberichten Betriebssystem, Spielausgabe, Revision und Schritte nennen.
Keine Originaldateien, Handbücher oder extrahierten Spielinhalte hochladen.
