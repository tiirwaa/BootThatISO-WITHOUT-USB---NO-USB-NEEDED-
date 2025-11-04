# BootThatISO!

**[🇺🇸 English Version](README.md) | [🇪🇸 Versión en Español](README.es.md) | [🇧🇷 Versão em Português](README.pt.md)**

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

BootThatISO! ist ein innovatives Windows-Tool, das das **Booten von Betriebssystemen aus ISO-Dateien ohne USB-Laufwerk** ermöglicht. Ideal für Situationen, in denen Sie kein USB-Gerät zur Hand haben, wie auf Reisen, bei geliehenen Geräten oder in Notfällen. Es automatisiert die Erstellung von EFI- und Datenpartitionen auf der internen Festplatte, direktes Lesen und Extrahieren von ISO-Dateien sowie die BCD-Konfiguration und bietet eine intuitive grafische Oberfläche und Unterstützung für unbeaufsichtigte Ausführung.

Dieses Dienstprogramm ist besonders nützlich für:
- **Schnellinstallationen**: Direkter Start von ISO für Windows-, Linux-Installation oder Wiederherstellungstools ohne USB-Vorbereitung.
- **Testumgebungen**: Testen Sie OS-ISOs oder Dienstprogramme, ohne externe Hardware zu ändern.
- **Systemwiederherstellung**: Zugriff auf Reparaturtools wie HBCD_PE oder Live-Umgebungen ohne Abhängigkeit von externen Medien.
- **Automatisierung**: Integration in Skripte für Massenbereitstellungen oder automatisierte Konfigurationen.

Entwickelt von **Andrey Rodríguez Araya**.

Website: [English](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## Hauptmerkmale
- Erstellt oder reformiert `ISOBOOT` (Daten) und `ISOEFI` (EFI) Partitionen auf der Systemfestplatte mit FAT32-, exFAT- oder NTFS-Formatoptionen.
- Unterstützt zwei Boot-Modi: vollständiges Laden der ISO auf die Festplatte oder RAMDisk-Modus (boot.wim im Speicher).
- Erkennt Windows-ISOs und passt die BCD-Konfiguration automatisch an; Nicht-Windows-ISOs starten direkt von der EFI-Partition.
- Führt optional Integritätsprüfungen (`chkdsk`) durch, erstellt detaillierte Protokolle und ermöglicht Abbruch oder Speicherwiederherstellung.
- Bietet einen unbeaufsichtigten Modus für Skript-Integrationen über Befehlszeilenargumente.
- **ISO-Hash-Cache (ISOBOOTHASH)**: Vergleicht MD5 der ISO, ausgewählten Boot-Modus und Format mit den im `ISOBOOTHASH`-Datei gespeicherten Werten. Bei Übereinstimmung werden Formatierung und Inhaltskopie übersprungen, um wiederholte Läufe zu beschleunigen.

## Getestete ISOs

### RAM-Modus (Boot vom Speicher)
- ✅ HBCD_PE_x64.iso (VOLL FUNKTIONSFÄHIG - Lädt alle Programme vom RAM)
- ✅ Win11_25H2_Spanish_x64.iso (VOLL FUNKTIONSFÄHIG - Boot und Installation)
- ✅ Windows10_22H2_X64.iso (VOLL FUNKTIONSFÄHIG - Boot und Installation)

### EXTRACT-Modus (Vollständige Installation)
- ✅ HBCD_PE_x64.iso (fällt zurück auf ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (fällt zurück auf ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (fällt zurück auf ISOBOOT_RAM)

## Anforderungen
- Windows 10 oder 11 64-Bit mit Administratorrechten.
- Mindestens 12 GB freier Speicherplatz auf Laufwerk `C:` zum Erstellen und Formatieren von Partitionen (das Tool versucht, 12 GB zu verkleinern).
- PowerShell, DiskPart, bcdedit und verfügbare Windows-Befehlszeilentools.
- Für die Kompilierung: Visual Studio 2022 mit CMake. Kein externer Paketmanager erforderlich; das 7‑Zip SDK ist unter `third-party/` enthalten.

## Kompilierung
```powershell
# Konfigurieren und kompilieren (VS 2022, x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Die finale ausführbare Datei befindet sich unter `build/Release/BootThatISO!.exe`. Ebenfalls enthalten ist `compilar.bat` mit äquivalenten Schritten.

### Schnellkompilierung mit compilar.bat (empfohlen)
```powershell
# Im Repository-Stammverzeichnis
./compilar.bat
```

## Verwendung
### Grafische Oberfläche
1. Führen Sie `BootThatISO!.exe` **als Administrator** aus (das Manifest fordert dies bereits an).
2. Wählen Sie die ISO-Datei und das Dateisystemformat für `ISOBOOT`.
3. Definieren Sie den Boot-Modus:
   - `Vollständige Installation`: kopiert den gesamten ISO-Inhalt auf die Festplatte.
   - `Von RAM booten`: kopiert `boot.wim` und Abhängigkeiten zum Booten vom RAM.
4. Entscheiden Sie, ob `chkdsk` ausgeführt werden soll (deaktiviert überspringt die Überprüfung zur Beschleunigung).
5. Klicken Sie auf **Bootfähige Partition erstellen** und überwachen Sie den Fortschritt über die Hauptleiste, detaillierte Leiste und das Protokollfeld.
6. Nach Abschluss erscheint eine Neustart-Bestätigung. Verwenden Sie die Schaltfläche **Speicherplatz wiederherstellen**, wenn Sie die Partitionen `ISOBOOT`/`ISOEFI` entfernen und `C:` erweitern müssen.
7. Die Schaltfläche **Dienste** öffnet die Support-Seite `https://agsoft.co.cr/servicios/`.

### Unbeaufsichtigter Modus
Führen Sie die Binärdatei mit erhöhten Rechten und den folgenden Argumenten aus:

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\pfad\image.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr|de_de
```

- `-mode=RAM` aktiviert den *Von RAM booten* Modus und kopiert `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` entspricht *Vollständige Installation*.
- `-chkdsk=TRUE` erzwingt Festplattenüberprüfung (standardmäßig weggelassen).
- `-lang` setzt den Sprachcode entsprechend den Dateien unter `lang/`.
- `-autoreboot` ist für zukünftige Automatisierungen verfügbar; protokolliert derzeit nur die Präferenz.

Der Prozess protokolliert Ereignisse und beendet sich, ohne das Hauptfenster anzuzeigen.

## Protokolle und Diagnose
Alle Operationen generieren Dateien in `logs/` (erstellt neben der ausführbaren Datei). Zu den relevantesten gehören:
- `general_log.log`: allgemeine Ereigniszeitleiste und UI-Nachrichten.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt`: Partitionierungs- und Neuformatierungsschritte.
- `iso_extract_log.log`, `iso_content.log`: Details des extrahierten ISO-Inhalts.
- `bcd_config_log.log`: BCD-Konfigurationsbefehle und Ergebnisse.
- `copy_error_log.log`, `iso_file_copy_log.log`: Dateikopieren und Fehler.

Überprüfen Sie diese Protokolle bei der Diagnose von Fehlern oder beim Teilen von Berichten.

## Sicherheit und Wiederherstellung
- Der Vorgang ändert die Systemfestplatte; sichern Sie vor der Ausführung des Tools.
- Schließen Sie die Anwendung während des Prozesses nicht über den Task-Manager; verwenden Sie die integrierte Abbruchoption.
- Verwenden Sie die Schaltfläche **Speicherplatz wiederherstellen**, um die Partitionen `ISOBOOT`/`ISOEFI` zu entfernen und das Laufwerk `C:` wiederherzustellen, wenn Sie die Konfiguration rückgängig machen möchten.

## Einschränkungen
- Arbeitet auf Disk 0 und verkleinert Volume C: um ~10,5 GB; andere Disk-Layouts werden derzeit nicht unterstützt.
- Erfordert Administratorrechte und Windows PowerShell-Verfügbarkeit.
- Sprachdateien unter `lang/` sind erforderlich; die App zeigt einen Fehler an, wenn keine gefunden werden.

## Credits
Entwickelt von **Andrey Rodríguez Araya** im Jahr 2025.

## Lizenz
Dieses Projekt steht unter der GPL 3.0 Lizenz. Siehe die Datei `LICENSE` für weitere Details.

## Hinweise zu Drittanbietern
- 7‑Zip SDK: Teile dieses Produkts enthalten Code aus dem 7‑Zip SDK von Igor Pavlov.
  - Lizenzierungszusammenfassung (gemäß `third-party/DOC/License.txt`):
    - Die meisten Dateien sind unter GNU LGPL (v2.1 oder später) lizenziert.
    - Einige Dateien sind gemeinfrei, wo dies explizit in Headern angegeben ist.
    - `CPP/7zip/Compress/LzfseDecoder.cpp` steht unter der BSD 3‑Clause Lizenz.
    - `CPP/7zip/Compress/Rar*` stehen unter GNU LGPL mit der "unRAR-Lizenzbeschränkung".
  - Wir bündeln eine minimale Teilmenge (ISO-Handler und gemeinsame Dienstprogramme). Kein RAR-Code wird von diesem Projekt verwendet.
  - Vollständige Texte: siehe `third-party/DOC/License.txt`, `third-party/DOC/lzma.txt` und `third-party/DOC/unRarLicense.txt`.
