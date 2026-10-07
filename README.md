# Discord Win3

Client Discord **natif** pour Windows, écrit en **C++/WinRT + WinUI 3** (Windows App SDK 2.5) — sans Electron ni Chromium.

| | Discord officiel (Electron) | Discord Win3 |
|---|---|---|
| Processus | 6 | 1 |
| Mémoire privée | ~1 Go | ~80 Mo |

## Fonctions (v0.1)

- Connexion par **QR code** (appli mobile) ou par token
- Token chiffré localement avec DPAPI (`%LOCALAPPDATA%\DiscordWin3\token.bin`)
- Liste des serveurs, salons (permissions respectées), messages privés
- Lecture des messages (historique infini en remontant), envoi, éditions/suppressions en temps réel
- Mentions, emojis custom, réponses, images (redimensionnées côté serveur), pièces jointes
- Gateway v9 avec reprise de session automatique

## Optimisations mémoire

- UI native WinUI 3, listes virtualisées
- Images décodées à leur taille d'affichage (`DecodePixelWidth`) et demandées réduites au proxy média
- Segment Heap activé via le manifeste
- JSON du `READY` jeté après extraction (modèle compact en C++)
- Historique plafonné à 400 messages par salon
- Working set rendu à l'OS quand la fenêtre est réduite

## Compiler

Prérequis : Visual Studio Build Tools 2026 (charges *C++ desktop* + *Universal build tools*), Windows SDK 10.0.26100.

```powershell
tools\restore.ps1
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" src\DiscordWin3.vcxproj -p:Configuration=Release -p:Platform=x64
bin\x64\Release\DiscordWin3.exe
```

## Avertissement

Les clients tiers ne sont pas autorisés par les conditions d'utilisation de Discord. Utilisation à tes risques (risque de suspension du compte).
