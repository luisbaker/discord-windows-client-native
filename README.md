# Discord Win3

Client Discord **natif** pour Windows, écrit en **C++/WinRT + WinUI 3** (Windows App SDK 2.5) — sans Electron ni Chromium.

| | Discord officiel (Electron 42) | Discord Win3 |
|---|---|---|
| Processus | 6 | 1 |
| Mémoire privée (connecté, fenêtre ouverte) | ~600 Mo – 1 Go | ~130–140 Mo |
| Gestionnaire des tâches après 30 s en arrière-plan | ~600 Mo | ~1–70 Mo |

Langues : Français, English, Português (Portugal), Português (Brasil), Español, Deutsch, Italiano — langue de Windows par défaut, changeable dans ⚙.

## Fonctions

**Compte & accueil**
- Connexion par **QR code** (appli mobile) ou par token, token chiffré localement (DPAPI)
- Page **Amis** : En ligne / Tous / En attente / Ajouter, accepter / refuser / retirer, recherche
- Colonne **Actifs maintenant**, statuts et activités des amis, points de statut dans les MP
- Recherche rapide **Ctrl+K**, boutons **Précédent / Suivant** (Alt+← / Alt+→)
- Statut et statut personnalisé dans le panneau utilisateur, menu ⚙ (langue, déconnexion)

**Serveurs & salons**
- Serveurs (ordre des dossiers), icônes animées rond ↔ carré arrondi
- Salons triés par catégorie, catégories repliables, permissions respectées
- Non-lus : pastille blanche, badges rouges de mentions, salons en gras, sourdines respectées
- Liste des membres par rôle (couleurs, statut, activités)

**Messages**
- Historique infini, groupement par auteur, séparateurs de date, « Hier à »
- Pseudos et avatars de serveur, couleurs de rôle, tag de serveur
- Mentions en pastilles, liens cliquables, emojis custom (géants si seuls), **gras**, `code`, blocs de code
- Aperçus de liens (cartes), images, **vidéos** (lecteur intégré façon Discord, plein écran), téléchargement
- Réactions (clic pour réagir, temps réel), barre d'actions au survol, sélecteur d'emojis
- Clic droit : réagir, répondre, modifier, transférer, copier texte / lien / identifiant, supprimer
- Répondre, modifier (↑ pour éditer, Échap pour annuler), **transférer** des messages
- Pièces jointes avant envoi : aperçu, nom, description (texte alternatif), spoiler, coller une capture, glisser-déposer
- Indicateur « en train d'écrire », notifications Windows (MP et mentions)

**Vocal** *(nouveau, à tester)*
- Rejoindre un salon vocal (clic), appeler en MP (bouton téléphone + sonnerie)
- Audio Opus 48 kHz, chiffrement de transport AES-256-GCM et **DAVE** (chiffrement de bout en bout obligatoire depuis mars 2026, via `libdave` officiel)
- Muet / sourdine, panneau « Connecté au vocal », anneau vert sur les personnes qui parlent

## Optimisations mémoire & énergie

- UI native WinUI 3 rendue par le GPU (composition DirectX), listes virtualisées
- **Parseur JSON maison** (`SlimJson`) pour les données du gateway : ~8 Mo au lieu de ~55 Mo pour le paquet READY
- Champs inutiles du READY retirés avant analyse, utilisateurs dédupliqués, tas compacté après chargement
- Images décodées à leur taille d'affichage et demandées réduites au proxy média ; emojis animés en PNG statique
- Lecteur vidéo créé seulement au clic et détruit dès que le message sort de l'écran
- Historique plafonné à 400 messages, membres chargés à la demande
- Arrière-plan / fenêtre réduite : priorité mémoire basse, EcoQoS, working set rendu à Windows
- Économiseur d'énergie / « Effets d'animation » désactivés : plus d'animations + EcoQoS

## Feuille de route (d'après les changelogs Discord 2021 → 2026)

| Fonction | Année officielle | Statut |
|---|---|---|
| Appels vocaux (salons + MP, DAVE) | — / 2024 | 🟡 implémenté, à tester |
| Partage d'écran / Go Live, caméra | 2017 / 2024 | à faire (prochaine étape) |
| Boutons Précédent / Suivant | 2026 | ✅ |
| Liste d'amis | — | ✅ (note de demande : à faire) |
| Transfert de messages | 2024 | ✅ |
| Tags de serveur | 2025 | ✅ |
| Sélecteur d'emojis | — | ✅ (GIF / stickers : à faire) |
| Super Reactions | 2023 | affichage simple |
| Threads | 2021 | à faire |
| Salons forum | 2022 | à faire |
| Messages vocaux | 2023 | à faire |
| Sondages | 2024 | à faire |
| Soundboard | 2023 | à faire |
| Profils (bio, bannière, décorations, effets) | 2022–2026 | à faire |
| Recherche, messages épinglés, favoris | — | à faire |
| Dossiers de serveurs | — | à faire |
| Salons spoiler (opt-in) | 2026 | à faire |
| Paramètres complets | 2025 (refonte) | à faire |

## Compiler

Prérequis : Visual Studio Build Tools 2026 (charges *C++ desktop* + *Universal build tools*), Windows SDK 10.0.26100.

```powershell
tools\restore.ps1              # paquets NuGet (Windows App SDK, C++/WinRT)
tools\build-voice-deps.ps1     # une seule fois : OpenSSL, mlspp, Opus, libdave (~15 min)
& "C:\Program Files (x86)\Microsoft Visual Studio\18\BuildTools\MSBuild\Current\Bin\amd64\MSBuild.exe" src\DiscordWin3.vcxproj -p:Configuration=Release -p:Platform=x64
bin\x64\Release\DiscordWin3.exe
```

Mesure mémoire : lancer avec `DISCORDWIN3_MEMLOG=1`, les points de mesure sont écrits dans `%TEMP%\discordwin3-mem.log`.

## Avertissement

Projet non officiel, sans lien avec Discord Inc. Les clients tiers ne sont pas autorisés par les conditions d'utilisation de Discord : utilisation à tes risques (risque de suspension du compte). L'icône de l'application est une création originale.
