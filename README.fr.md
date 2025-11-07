# BootThatISO!

**[🇺🇸 English Version](README.md) | [🇪🇸 Versión en Español](README.es.md) | [🇧🇷 Versão em Português](README.pt.md) | [🇩🇪 Deutsche Version](README.de.md)**

<div style="display: flex; justify-content: center; align-items: center;">
<img src="res/logo.png" alt="Logo" style="margin-right: 20px;">
<img src="res/ag.png" alt="Company Logo">
</div>

## Videos

- [Hirens Boot — No USB Needed — Boot ISO Directly (No USB Required)](https://www.youtube.com/watch?v=RaRJTmek0W8)
- [Install Windows 10/11 Without USB | BootThatISO (No Flash Drive Needed!)](https://www.youtube.com/watch?v=Fo6poEWZNew)

BootThatISO! est un outil Windows innovant qui permet de **démarrer des systèmes d'exploitation à partir de fichiers ISO sans avoir besoin d'une clé USB**. Idéal pour les situations où vous n'avez pas de périphérique USB à portée de main, comme lors de voyages, d'équipements empruntés ou d'urgences. Il automatise la création de partitions EFI et de données sur le disque interne, la lecture directe d'ISO et l'extraction de fichiers, ainsi que la configuration BCD, offrant une interface graphique intuitive et un support pour l'exécution sans surveillance.

Cet utilitaire est particulièrement utile pour :
- **Installations Rapides** : Démarrage direct depuis ISO pour l'installation de Windows, Linux ou les outils de récupération sans préparer de USB.
- **Environnements de Test** : Testez des ISOs de systèmes d'exploitation ou des utilitaires sans modifier le matériel externe.
- **Récupération Système** : Accédez aux outils de réparation comme HBCD_PE ou aux environnements live sans dépendre de supports externes.
- **Automatisation** : Intégration dans des scripts pour des déploiements massifs ou des configurations automatisées.

Développé par **Andrey Rodríguez Araya**.

Site web : [English](https://agsoft.co.cr/en/software-and-services/) | [Español](https://agsoft.co.cr/servicios/)

![Screenshot](screenshot.png?v=1)

![Boot screen](boot_screen.png?v=1)

## Caractéristiques Principales
- Crée ou reforme les partitions `ISOBOOT` (données) et `ISOEFI` (EFI) sur le disque système, avec des options de format FAT32, exFAT ou NTFS.
- Prend en charge deux modes de démarrage : chargement complet de l'ISO sur le disque ou mode RAMDisk (boot.wim en mémoire).
- Détecte les ISOs Windows et ajuste automatiquement la configuration BCD ; les ISOs non-Windows démarrent directement depuis la partition EFI.
- Exécute des vérifications d'intégrité optionnelles (`chkdsk`), génère des journaux détaillés et permet l'annulation ou la récupération d'espace.
- Fournit un mode sans surveillance pour les intégrations de scripts via des arguments de ligne de commande.
- **Cache de hash ISO (ISOBOOTHASH)** : Compare le MD5 de l'ISO, le mode de démarrage sélectionné et le format avec les valeurs stockées dans le fichier `ISOBOOTHASH` sur la cible. S'ils correspondent, il ignore le formatage et la copie de contenu pour accélérer les exécutions répétées.

## ISOs Testés

### Mode RAM (Démarrage depuis la Mémoire)
- ✅ HBCD_PE_x64.iso (ENTIÈREMENT FONCTIONNEL - Charge tous les programmes depuis la RAM)
- ✅ Win11_25H2_Spanish_x64.iso (ENTIÈREMENT FONCTIONNEL - Démarrage et Installation)
- ✅ Windows10_22H2_X64.iso (ENTIÈREMENT FONCTIONNEL - Démarrage et Installation)

### Mode EXTRACT (Installation Complète)
- ✅ HBCD_PE_x64.iso (revient à ISOBOOT_RAM)
- ✅ Win11_25H2_Spanish_x64.iso (revient à ISOBOOT_RAM)
- ✅ Windows10_22H2_X64.iso (revient à ISOBOOT_RAM)

## Exigences
- Windows 10 ou 11 64-bit avec privilèges administrateur.
- Au moins 12 Go d'espace libre sur le lecteur `C:` pour créer et formater les partitions (l'outil tente de réduire de 12 Go).
- PowerShell, DiskPart, bcdedit et les outils de ligne de commande Windows disponibles.
- Pour la compilation : Visual Studio 2022 avec CMake. Aucun gestionnaire de paquets externe requis ; le SDK 7‑Zip est inclus sous `third-party/`.

## Compilation
```powershell
# Configurer et compiler (VS 2022, x64)
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

L'exécutable final se trouve dans `build/Release/BootThatISO!.exe`. `compilar.bat` est également inclus avec des étapes équivalentes.

### Compilation rapide avec compilar.bat (recommandé)
```powershell
# À la racine du dépôt
./compilar.bat
```

## Utilisation
### Interface Graphique
1. Exécutez `BootThatISO!.exe` **en tant qu'administrateur** (le manifeste le demande déjà).
2. Sélectionnez le fichier ISO et choisissez le format du système de fichiers pour `ISOBOOT`.
3. Définissez le mode de démarrage :
   - `Installation Complète` : copie tout le contenu de l'ISO sur le disque.
   - `Démarrer depuis la Mémoire` : copie `boot.wim` et les dépendances pour démarrer depuis la RAM.
4. Décidez si vous souhaitez exécuter `chkdsk` (décoché pour ignorer la vérification et accélérer le processus).
5. Cliquez sur **Créer une Partition Amorçable** et surveillez la progression via la barre principale, la barre détaillée et le panneau de journal.
6. À la fin, une confirmation de redémarrage apparaîtra. Utilisez le bouton **Récupérer l'Espace** si vous devez supprimer les partitions `ISOBOOT`/`ISOEFI` et étendre `C:`.
7. Le bouton **Services** ouvre la page de support `https://agsoft.co.cr/servicios/`.

### Mode Sans Surveillance
Exécutez le binaire avec des privilèges élevés et les arguments suivants :

```
BootThatISO!.exe ^
  -unattended ^
  -iso="C:\chemin\image.iso" ^
  -mode=RAM|EXTRACT ^
  -format=NTFS|FAT32|EXFAT ^
  -chkdsk=TRUE|FALSE ^
  -autoreboot=y|n ^
  -lang=en_us|es_cr|fr_fr
```

- `-mode=RAM` active le mode *Démarrer depuis la Mémoire* et copie `boot.wim`/`boot.sdi`.
- `-mode=EXTRACT` correspond à *Installation Complète*.
- `-chkdsk=TRUE` force la vérification du disque (omis par défaut).
- `-lang` définit le code de langue correspondant aux fichiers sous `lang/`.
- `-autoreboot` est disponible pour les automations futures ; enregistre actuellement uniquement la préférence.

Le processus enregistre les événements et se termine sans afficher la fenêtre principale.

## Journaux et Diagnostics
Toutes les opérations génèrent des fichiers dans `logs/` (créés à côté de l'exécutable). Parmi les plus pertinents :
- `general_log.log` : chronologie générale des événements et messages de l'UI.
- `diskpart_log.log`, `reformat_log.log`, `recover_script_log.txt` : étapes de partitionnement et reformatage.
- `iso_extract_log.log`, `iso_content.log` : détails du contenu extrait de l'ISO.
- `bcd_config_log.log` : commandes et résultats de configuration BCD.
- `copy_error_log.log`, `iso_file_copy_log.log` : copie de fichiers et erreurs.

Consultez ces journaux lors du diagnostic des échecs ou du partage de rapports.

## Sécurité et Récupération
- L'opération modifie le disque système ; sauvegardez avant d'exécuter l'outil.
- Pendant le processus, évitez de fermer l'application depuis le Gestionnaire des tâches ; utilisez l'option d'annulation intégrée.
- Utilisez le bouton **Récupérer l'Espace** pour supprimer les partitions `ISOBOOT`/`ISOEFI` et restaurer le lecteur `C:` si vous décidez d'annuler la configuration.

## Limitations
- Fonctionne sur le Disque 0 et réduit le volume C: de ~10,5 Go ; d'autres dispositions de disque ne sont actuellement pas prises en charge.
- Nécessite des privilèges administrateur et la disponibilité de Windows PowerShell.
- Les fichiers de langue sous `lang/` sont requis ; l'application affiche une erreur si aucun n'est trouvé.

## Crédits
Développé par **Andrey Rodríguez Araya** en 2025.

## Licence
Ce projet est sous licence GPL 3.0. Consultez le fichier `LICENSE` pour plus de détails.

## Avis de tiers
- SDK 7‑Zip : Des parties de ce produit incluent du code du SDK 7‑Zip d'Igor Pavlov.
  - Résumé de licence (selon `third-party/DOC/License.txt`) :
    - La plupart des fichiers sont sous licence GNU LGPL (v2.1 ou ultérieure).
    - Certains fichiers sont dans le domaine public lorsque cela est explicitement indiqué dans les en-têtes.
    - `CPP/7zip/Compress/LzfseDecoder.cpp` est sous licence BSD 3‑Clause.
    - `CPP/7zip/Compress/Rar*` sont sous GNU LGPL avec la "restriction de licence unRAR".
  - Nous incluons un sous-ensemble minimal (gestionnaire ISO et utilitaires communs). Aucun code RAR n'est utilisé par ce projet.
  - Textes complets : voir `third-party/DOC/License.txt`, `third-party/DOC/lzma.txt` et `third-party/DOC/unRarLicense.txt`.
