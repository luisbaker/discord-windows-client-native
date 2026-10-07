# Discord Win3

Client Discord **natif** pour Windows, écrit en **C++/WinRT + WinUI 3** (Windows App SDK 2.5) — sans Electron ni Chromium.

| | Discord officiel (Electron 42) | Discord Win3 |
|---|---|---|
| Processus | 6 | 1 |
| Mémoire privée (connecté) | ~1 Go | ~120–150 Mo |
| Mémoire physique fenêtre réduite | ~600 Mo | ~25 Mo |

## Fonctions

**Compte**
- Connexion par **QR code** (appli mobile) ou par token, token chiffré localement (DPAPI)
- Statut et statut personnalisé dans le panneau utilisateur

**Navigation**
- Serveurs (ordre des dossiers), icônes animées rond ↔ carré arrondi
- Salons triés par catégorie, catégories repliables, permissions respectées
- Messages privés et groupes avec avatars
- Non-lus : pastille blanche, badges rouges de mentions, salons en gras, serveurs/salons en sourdine respectés
- Liste des membres par rôle (couleurs, statut, activités)
- Participants des salons vocaux

**Messages**
- Historique infini, groupement par auteur, séparateurs de date, « Hier à »
- Pseudos de serveur, couleurs de rôle, avatars de serveur, tag de serveur
- Mentions en pastilles, liens cliquables, emojis custom (géants si seuls), **gras**, `code`, blocs de code
- Aperçus de liens (cartes), images redimensionnées par le proxy, pièces jointes, stickers
- Réactions (affichage, clic pour réagir, temps réel)
- Clic droit : réagir, répondre, modifier, copier texte / lien / identifiant, supprimer
- Répondre, modifier (↑ pour éditer son dernier message, Échap pour annuler)
- Envoi de fichiers (20 Mo), indicateur « en train d'écrire »
- Notifications Windows (MP et mentions) + clignotement de la barre des tâches

## Optimisations mémoire & énergie

- UI native WinUI 3 rendue par le GPU (composition DirectX), listes virtualisées
- Images décodées à leur taille d'affichage et demandées réduites au proxy média ; emojis animés en PNG statique
- Segment Heap (manifeste), JSON du `READY` jeté après extraction (modèle C++ compact)
- Historique plafonné à 400 messages, liste des membres limitée aux 100 premières lignes, membres chargés à la demande
- Fenêtre réduite : priorité mémoire basse, EcoQoS, compaction du tas, working set rendu à l'OS
- Économiseur d'énergie / « Effets d'animation » désactivés : plus d'animations + EcoQoS

## Feuille de route (d'après les changelogs Discord 2021 → 2026)

| Fonction | Année officielle | Statut |
|---|---|---|
| Threads | 2021 | à faire |
| Salons forum | 2022 | à faire |
| Messages vocaux | 2023 | à faire |
| Sondages | 2024 | à faire |
| Transfert de messages | 2024 | à faire |
| Super Reactions | 2023 | affichage simple |
| Soundboard | 2023 | à faire (avec le vocal) |
| Appels vocaux / vidéo, Go Live (chiffrement DAVE) | — / 2024 | à faire |
| Tags de serveur | 2025 | ✅ |
| Boutons Précédent / Suivant | 2026 | à faire |
| Profils (bio formatée, bannière, décorations, effets) | 2022–2026 | à faire |
| Liste d'amis, demandes avec note | 2026 | à faire |
| Sélecteurs d'emojis / GIF / stickers | — | à faire |
| Recherche, messages épinglés, favoris | — | à faire |
| Dossiers de serveurs | — | à faire |
| Salons spoiler (opt-in) | 2026 | à faire |
| Paramètres | 2025 (refonte) | à faire |

## Compiler

Prérequis : Visual Studio Build Tools 2026 (charges *C++ desktop* + *Universal build tools*), Windows SDK 10.0.26100.

```powershell
tools\restore.ps1
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" src\DiscordWin3.vcxproj -p:Configuration=Release -p:Platform=x64
bin\x64\Release\DiscordWin3.exe
```

## Avertissement

Projet non officiel, sans lien avec Discord Inc. Les clients tiers ne sont pas autorisés par les conditions d'utilisation de Discord : utilisation à tes risques (risque de suspension du compte). L'icône de l'application est une création originale.
