# BootThatISO!

**[🇺🇸 English](README.md) | [🇪🇸 Español](README.es.md) | [🇧🇷 Português](README.pt.md) | [🇩🇪 Deutsch](README.de.md) | [🇫🇷 Français](README.fr.md)**

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

## Videos

- [Hirens Boot — No USB Needed — Boot ISO Directly (No USB Required)](https://www.youtube.com/watch?v=RaRJTmek0W8)
- [Install Windows 10/11 Without USB | BootThatISO (No Flash Drive Needed!)](https://www.youtube.com/watch?v=Fo6poEWZNew)

BootThatISO! è uno strumento innovativo per Windows che consente di **avviare sistemi operativi da file ISO senza bisogno di una chiavetta USB**. Ideale per situazioni in cui non si dispone di un dispositivo USB a portata di mano, come durante i viaggi, con attrezzature in prestito o in emergenze. Automatizza la creazione di partizioni EFI e dati sul disco interno, la lettura diretta dell'ISO e l'estrazione di file, e la configurazione BCD, offrendo un'interfaccia grafica intuitiva e supporto per l'esecuzione non presidiata.

Questa utility è particolarmente utile per:
- **Installazioni Rapide**: Avvio diretto da ISO per installazione di Windows, Linux o strumenti di ripristino senza preparare USB.
- **Ambienti di Test**: Testa ISOs di sistemi operativi o utility senza modificare hardware esterno.
- **Ripristino del Sistema**: Accedi a strumenti di riparazione come HBCD_PE o ambienti live senza dipendere da supporti esterni.
- **Automazione**: Integrazione in script per distribuzioni di massa o configurazioni automatizzate.

Sviluppato da **Andrey Rodríguez Araya**.

Sito web: [English](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## Caratteristiche Principali
- Crea o riforma le partizioni `ISOBOOT` (dati) e `ISOEFI` (EFI) sul disco di sistema, con opzioni di formato FAT32, exFAT o NTFS.
- Supporta due modalità di avvio: caricamento completo dell'ISO su disco o modalità RAMDisk (boot.wim in memoria).
- Rileva ISOs Windows e regola automaticamente la configurazione BCD; ISOs non-Windows avviano direttamente dalla partizione EFI.
- Esegue controlli di integrità opzionali (`chkdsk`), genera log dettagliati e consente l'annullamento o il ripristino dello spazio.
- Fornisce modalità non presidiata per integrazioni di script tramite argomenti da riga di comando.
- **Cache hash ISO (ISOBOOTHASH)**: Confronta l'MD5 dell'ISO, la modalità di avvio selezionata e il formato con i valori memorizzati nel file `ISOBOOTHASH` sul target. Se corrispondono, salta la formattazione e la copia del contenuto per accelerare le esecuzioni ripetute.

## ISOs Testati

### Modalità RAM (Avvio dalla Memoria)
- ✅ HBCD_PE_x64.iso (COMPLETAMENTE FUNZIONALE - Carica tutti i programmi dalla RAM)
- ✅ Win11_25H2_Spanish_x64.iso (COMPLETAMENTE FUNZIONALE - Avvio e Installazione)
- ✅ Windows10_22H2_X64.iso (COMPLETAMENTE FUNZIONALE - Avvio e Installazione)

### Modalità EXTRACT (Installazione Completa)
- ✅ HBCD_PE_x64.iso (torna a ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (torna a ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (torna a ISOBOOT_RAM)

## Requisiti
- Windows 10 o 11 64-bit con privilegi di amministratore.
- Almeno 12 GB di spazio libero sull'unità `C:` per creare e formattare le partizioni (lo strumento tenta di ridurre di 12 GB).
- PowerShell, DiskPart, bcdedit e strumenti da riga di comando di Windows disponibili.
- Per la compilazione: Visual Studio 2022 con CMake. Nessun gestore di pacchetti esterno richiesto; l'SDK 7‑Zip è incluso in `third-party/`.

## Compilazione
```powershell
# Configurare e compilare (VS 2022, x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

L'eseguibile finale si trova in `build/Release/BootThatISO!.exe`. È incluso anche `compilar.bat` con passaggi equivalenti.

### Compilazione rapida con compilar.bat (consigliato)
```powershell
# Nella radice del repository
./compilar.bat
```

## Uso
### Interfaccia Grafica
1. Esegui `BootThatISO!.exe` **come amministratore** (il manifest lo richiede già).
2. Seleziona il file ISO e scegli il formato del file system per `ISOBOOT`.
3. Definisci la modalità di avvio:
   - `Installazione Completa`: copia l'intero contenuto dell'ISO sul disco.
   - `Avvia da Memoria`: copia `boot.wim` e dipendenze per avviare dalla RAM.
4. Decidi se eseguire `chkdsk` (deselezionato salta la verifica per accelerare il processo).
5. Fai clic su **Crea Partizione Avviabile** e monitora il progresso tramite la barra principale, la barra dettagliata e il pannello di log.
6. Al completamento, apparirà una conferma di riavvio. Usa il pulsante **Recupera Spazio** se devi rimuovere le partizioni `ISOBOOT`/`ISOEFI` ed estendere `C:`.
7. Il pulsante **Servizi** apre la pagina di supporto `https://agsoft.co.cr/servicios/`.

### Modalità Non Presidiata
Esegui il binario con privilegi elevati e i seguenti argomenti:

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\percorso\immagine.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr|it_it
```

- `-mode=RAM` attiva la modalità *Avvia da Memoria* e copia `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` corrisponde a *Installazione Completa*.
- `-chkdsk=TRUE` forza la verifica del disco (omesso per impostazione predefinita).
- `-lang` imposta il codice lingua corrispondente ai file in `lang/`.
- `-autoreboot` è disponibile per automazioni future; attualmente registra solo la preferenza.

Il processo registra eventi ed esce senza mostrare la finestra principale.

## Log e Diagnostica
Tutte le operazioni generano file in `logs/` (creati accanto all'eseguibile). Tra i più rilevanti:
- `general_log.log`: cronologia generale degli eventi e messaggi UI.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt`: passaggi di partizionamento e riformattazione.
- `iso_extract_log.log`, `iso_content.log`: dettagli del contenuto estratto dall'ISO.
- `bcd_config_log.log`: comandi e risultati di configurazione BCD.
- `copy_error_log.log`, `iso_file_copy_log.log`: copia di file ed errori.

Rivedi questi log durante la diagnosi di guasti o la condivisione di report.

## Sicurezza e Ripristino
- L'operazione modifica il disco di sistema; esegui un backup prima di eseguire lo strumento.
- Durante il processo, evita di chiudere l'applicazione dal Task Manager; usa l'opzione di annullamento integrata.
- Usa il pulsante **Recupera Spazio** per rimuovere le partizioni `ISOBOOT`/`ISOEFI` e ripristinare l'unità `C:` se decidi di annullare la configurazione.

## Limitazioni
- Opera sul Disco 0 e riduce il volume C: di ~10,5 GB; altri layout di disco non sono attualmente supportati.
- Richiede privilegi di amministratore e disponibilità di Windows PowerShell.
- I file di lingua in `lang/` sono necessari; l'app mostra un errore se non ne trova nessuno.

## Crediti
Sviluppato da **Andrey Rodríguez Araya** nel 2025.

## Licenza
Questo progetto è sotto la Licenza GPL 3.0. Vedere il file `LICENSE` per maggiori dettagli.

## Avvisi di terze parti
- SDK 7‑Zip: Parti di questo prodotto includono codice dall'SDK 7‑Zip di Igor Pavlov.
  - Riepilogo licenze (secondo `third-party/DOC/License.txt`):
    - La maggior parte dei file sono concessi in licenza GNU LGPL (v2.1 o successiva).
    - Alcuni file sono di dominio pubblico dove esplicitamente dichiarato negli header.
    - `CPP/7zip/Compress/LzfseDecoder.cpp` è sotto licenza BSD 3‑Clause.
    - `CPP/7zip/Compress/Rar*` sono sotto GNU LGPL con la "restrizione di licenza unRAR".
  - Includiamo un sottoinsieme minimo (gestore ISO e utility comuni). Nessun codice RAR è utilizzato da questo progetto.
  - Testi completi: vedere `third-party/DOC/License.txt`, `third-party/DOC/lzma.txt` e `third-party/DOC/unRarLicense.txt`.
