#pragma once

// UI translations, compiled into the exe (no resource files to load, switchable at runtime).
// One line per string: key, then fr, en, pt-PT, pt-BR, es, de, it. "{0}" / "{1}" are placeholders.
#define DISCORDWIN3_STRINGS(X) \
    X(LoginWelcome, L"Ha, te revoilà !", L"Welcome back!", L"Que bom ver-te de novo!", L"Que bom te ver de novo!", L"¡Hola de nuevo!", L"Willkommen zurück!", L"Bentornato!") \
    X(LoginSubtitle, L"Client natif WinUI 3 — sans Electron.", L"Native WinUI 3 client — no Electron.", L"Cliente nativo WinUI 3 — sem Electron.", L"Cliente nativo WinUI 3 — sem Electron.", L"Cliente nativo WinUI 3, sin Electron.", L"Nativer WinUI-3-Client – ohne Electron.", L"Client nativo WinUI 3, senza Electron.") \
    X(LoginTokenLabel, L"TOKEN (option avancée)", L"TOKEN (advanced)", L"TOKEN (opção avançada)", L"TOKEN (opção avançada)", L"TOKEN (opción avanzada)", L"TOKEN (erweitert)", L"TOKEN (opzione avanzata)") \
    X(LoginTokenPlaceholder, L"Colle ton token ici", L"Paste your token here", L"Cola o teu token aqui", L"Cole seu token aqui", L"Pega tu token aquí", L"Token hier einfügen", L"Incolla qui il tuo token") \
    X(LoginButton, L"Connexion", L"Log In", L"Iniciar sessão", L"Entrar", L"Iniciar sesión", L"Anmelden", L"Accedi") \
    X(LoginNeedToken, L"Entre un token.", L"Enter a token.", L"Introduz um token.", L"Digite um token.", L"Introduce un token.", L"Gib einen Token ein.", L"Inserisci un token.") \
    X(LoginInvalidToken, L"Token invalide ou expiré. Reconnecte-toi.", L"Invalid or expired token. Please log in again.", L"Token inválido ou expirado. Inicia sessão novamente.", L"Token inválido ou expirado. Entre novamente.", L"Token inválido o caducado. Vuelve a iniciar sesión.", L"Ungültiges oder abgelaufenes Token. Bitte melde dich erneut an.", L"Token non valido o scaduto. Accedi di nuovo.") \
    X(QrTitle, L"Se connecter avec un code QR", L"Log in with QR Code", L"Iniciar sessão com código QR", L"Entrar com código QR", L"Iniciar sesión con código QR", L"Mit QR-Code anmelden", L"Accedi con codice QR") \
    X(QrHint, L"Scanne ce code avec l'application mobile Discord pour te connecter instantanément.", L"Scan this with the Discord mobile app to log in instantly.", L"Lê este código com a app móvel do Discord para iniciares sessão de imediato.", L"Escaneie com o app do Discord no celular para entrar na hora.", L"Escanéalo con la app móvil de Discord para iniciar sesión al instante.", L"Scanne den Code mit der Discord-App auf dem Handy, um dich sofort anzumelden.", L"Scansiona con l'app mobile di Discord per accedere subito.") \
    X(QrRetry, L"Générer un nouveau code", L"Generate a new code", L"Gerar um novo código", L"Gerar um novo código", L"Generar un nuevo código", L"Neuen Code erzeugen", L"Genera un nuovo codice") \
    X(QrCheckPhone, L"Regarde ton téléphone !", L"Check your phone!", L"Verifica o teu telemóvel!", L"Confira seu celular!", L"¡Mira tu teléfono!", L"Schau auf dein Handy!", L"Controlla il telefono!") \
    X(QrConfirmAs, L"Connexion en tant que {0} — confirme sur l'appli mobile.", L"Logging in as {0} — confirm in the mobile app.", L"A iniciar sessão como {0} — confirma na app móvel.", L"Entrando como {0} — confirme no app do celular.", L"Iniciando sesión como {0}: confírmalo en la app móvil.", L"Anmeldung als {0} – bestätige in der Handy-App.", L"Accesso come {0}: conferma nell'app mobile.") \
    X(QrExpired, L"Code expiré", L"Code expired", L"Código expirado", L"Código expirado", L"Código caducado", L"Code abgelaufen", L"Codice scaduto") \
    X(QrCancelled, L"Connexion annulée sur le téléphone.", L"Login cancelled on the phone.", L"Início de sessão cancelado no telemóvel.", L"Login cancelado no celular.", L"Inicio de sesión cancelado en el teléfono.", L"Anmeldung am Handy abgebrochen.", L"Accesso annullato sul telefono.") \
    X(QrConnectFailed, L"Connexion QR impossible : {0}", L"QR login unavailable: {0}", L"Não foi possível ligar ao QR: {0}", L"Não foi possível conectar o QR: {0}", L"No se pudo conectar el QR: {0}", L"QR-Anmeldung nicht möglich: {0}", L"Accesso QR non disponibile: {0}") \
    X(QrClosed, L"Session QR fermée (code {0}).", L"QR session closed (code {0}).", L"Sessão QR fechada (código {0}).", L"Sessão QR fechada (código {0}).", L"Sesión QR cerrada (código {0}).", L"QR-Sitzung geschlossen (Code {0}).", L"Sessione QR chiusa (codice {0}).") \
    X(QrValidateFailed, L"Échec de la validation du QR : {0}", L"QR validation failed: {0}", L"Falha na validação do QR: {0}", L"Falha na validação do QR: {0}", L"Error al validar el QR: {0}", L"QR-Bestätigung fehlgeschlagen: {0}", L"Convalida QR non riuscita: {0}") \
    X(Connecting, L"Connexion…", L"Connecting…", L"A ligar…", L"Conectando…", L"Conectando…", L"Verbinde…", L"Connessione…") \
    X(Reconnecting, L"Reconnexion…", L"Reconnecting…", L"A religar…", L"Reconectando…", L"Reconectando…", L"Verbinde erneut…", L"Riconnessione…") \
    X(Loading, L"Chargement…", L"Loading…", L"A carregar…", L"Carregando…", L"Cargando…", L"Lädt…", L"Caricamento…") \
    X(DirectMessages, L"Messages privés", L"Direct Messages", L"Mensagens diretas", L"Mensagens diretas", L"Mensajes directos", L"Direktnachrichten", L"Messaggi diretti") \
    X(Friends, L"Amis", L"Friends", L"Amigos", L"Amigos", L"Amigos", L"Freunde", L"Amici") \
    X(QuickSwitch, L"Recherche ou lance une conversation", L"Find or start a conversation", L"Encontra ou inicia uma conversa", L"Encontre ou comece uma conversa", L"Busca o inicia una conversación", L"Unterhaltung finden oder beginnen", L"Trova o avvia una conversazione") \
    X(QuickSwitchPlaceholder, L"Où veux-tu aller ?", L"Where would you like to go?", L"Para onde queres ir?", L"Para onde você quer ir?", L"¿Adónde quieres ir?", L"Wohin möchtest du?", L"Dove vuoi andare?") \
    X(Close, L"Fermer", L"Close", L"Fechar", L"Fechar", L"Cerrar", L"Schließen", L"Chiudi") \
    X(Cancel, L"Annuler", L"Cancel", L"Cancelar", L"Cancelar", L"Cancelar", L"Abbrechen", L"Annulla") \
    X(Save, L"Sauvegarder", L"Save", L"Guardar", L"Salvar", L"Guardar", L"Speichern", L"Salva") \
    X(Back, L"Précédent (Alt+←)", L"Back (Alt+←)", L"Anterior (Alt+←)", L"Voltar (Alt+←)", L"Atrás (Alt+←)", L"Zurück (Alt+←)", L"Indietro (Alt+←)") \
    X(Forward, L"Suivant (Alt+→)", L"Forward (Alt+→)", L"Seguinte (Alt+→)", L"Avançar (Alt+→)", L"Adelante (Alt+→)", L"Vorwärts (Alt+→)", L"Avanti (Alt+→)") \
    X(Logout, L"Se déconnecter", L"Log Out", L"Terminar sessão", L"Sair", L"Cerrar sesión", L"Abmelden", L"Esci") \
    X(Settings, L"Paramètres", L"User Settings", L"Definições", L"Configurações", L"Ajustes", L"Einstellungen", L"Impostazioni") \
    X(Language, L"Langue", L"Language", L"Idioma", L"Idioma", L"Idioma", L"Sprache", L"Lingua") \
    X(ThemeLabel, L"Thème", L"Theme", L"Tema", L"Tema", L"Tema", L"Design", L"Tema") \
    X(AddServer, L"Ajouter un serveur", L"Add a Server", L"Adicionar um servidor", L"Adicionar um servidor", L"Añadir un servidor", L"Server hinzufügen", L"Aggiungi un server") \
    X(JoinServerHint, L"Lien d'invitation (ex. https://discord.gg/abc)", L"Invite link (e.g. https://discord.gg/abc)", L"Link de convite (ex. https://discord.gg/abc)", L"Link de convite (ex. https://discord.gg/abc)", L"Enlace de invitación (p. ej. https://discord.gg/abc)", L"Einladungslink (z. B. https://discord.gg/abc)", L"Link di invito (es. https://discord.gg/abc)") \
    X(JoinServer, L"Rejoindre", L"Join", L"Entrar", L"Entrar", L"Unirse", L"Beitreten", L"Unisciti") \
    X(PinnedMessages, L"Messages épinglés", L"Pinned Messages", L"Mensagens afixadas", L"Mensagens fixadas", L"Mensajes fijados", L"Angeheftete Nachrichten", L"Messaggi fissati") \
    X(NoPins, L"Ce salon n'a aucun message épinglé.", L"This channel doesn't have any pinned messages.", L"Este canal não tem mensagens afixadas.", L"Este canal não tem mensagens fixadas.", L"Este canal no tiene mensajes fijados.", L"Dieser Kanal hat keine angehefteten Nachrichten.", L"Questo canale non ha messaggi fissati.") \
    X(SearchIn, L"Rechercher {0}", L"Search {0}", L"Pesquisar {0}", L"Buscar {0}", L"Buscar en {0}", L"{0} durchsuchen", L"Cerca in {0}") \
    X(SearchResults, L"{0} résultats", L"{0} Results", L"{0} resultados", L"{0} resultados", L"{0} resultados", L"{0} Ergebnisse", L"{0} risultati") \
    X(NoResults, L"Aucun résultat.", L"No results.", L"Sem resultados.", L"Nenhum resultado.", L"Sin resultados.", L"Keine Ergebnisse.", L"Nessun risultato.") \
    X(NoSendPermission, L"Tu n'as pas la permission d'envoyer des messages dans ce salon.", L"You do not have permission to send messages in this channel.", L"Não tens permissão para enviar mensagens neste canal.", L"Você não tem permissão para enviar mensagens neste canal.", L"No tienes permiso para enviar mensajes en este canal.", L"Du hast keine Berechtigung, in diesem Kanal Nachrichten zu senden.", L"Non hai il permesso di inviare messaggi in questo canale.") \
    X(Discover, L"Découvrir", L"Discover", L"Descobrir", L"Descobrir", L"Descubrir", L"Entdecken", L"Esplora") \
    X(ShowMembers, L"Afficher la liste des membres", L"Show Member List", L"Mostrar lista de membros", L"Mostrar lista de membros", L"Mostrar lista de miembros", L"Mitgliederliste anzeigen", L"Mostra elenco membri") \
    X(AttachFile, L"Envoyer un fichier", L"Upload a File", L"Carregar um ficheiro", L"Enviar um arquivo", L"Subir un archivo", L"Datei hochladen", L"Carica un file") \
    X(CancelEsc, L"Annuler (Échap)", L"Cancel (Esc)", L"Cancelar (Esc)", L"Cancelar (Esc)", L"Cancelar (Esc)", L"Abbrechen (Esc)", L"Annulla (Esc)") \
    X(VoiceSoon, L"Les appels vocaux arrivent dans une prochaine version.", L"Voice calls are coming in a future version.", L"As chamadas de voz chegam numa próxima versão.", L"As chamadas de voz chegam em uma próxima versão.", L"Las llamadas de voz llegarán en una próxima versión.", L"Sprachanrufe kommen in einer späteren Version.", L"Le chiamate vocali arriveranno in una prossima versione.") \
    X(StatusOnline, L"En ligne", L"Online", L"Disponível", L"Disponível", L"Disponible", L"Online", L"Online") \
    X(StatusIdle, L"Inactif", L"Idle", L"Ausente", L"Ausente", L"Ausente", L"Abwesend", L"Inattivo") \
    X(StatusDnd, L"Ne pas déranger", L"Do Not Disturb", L"Não incomodar", L"Não perturbe", L"No molestar", L"Bitte nicht stören", L"Non disturbare") \
    X(StatusInvisible, L"Invisible", L"Invisible", L"Invisível", L"Invisível", L"Invisible", L"Unsichtbar", L"Invisibile") \
    X(StatusOffline, L"Hors ligne", L"Offline", L"Offline", L"Offline", L"Desconectado", L"Offline", L"Offline") \
    X(TabAll, L"Tous", L"All", L"Todos", L"Todos", L"Todos", L"Alle", L"Tutti") \
    X(TabPending, L"En attente", L"Pending", L"Pendentes", L"Pendente", L"Pendiente", L"Ausstehend", L"In attesa") \
    X(TabAdd, L"Ajouter", L"Add Friend", L"Adicionar amigo", L"Adicionar amigo", L"Añadir amigo", L"Freund hinzufügen", L"Aggiungi amico") \
    X(AllFriends, L"Tous les amis", L"All Friends", L"Todos os amigos", L"Todos os amigos", L"Todos los amigos", L"Alle Freunde", L"Tutti gli amici") \
    X(Search, L"Rechercher", L"Search", L"Pesquisar", L"Buscar", L"Buscar", L"Suchen", L"Cerca") \
    X(AddFriendHint, L"Tu peux ajouter des amis grâce à leur nom d'utilisateur Discord.", L"You can add friends with their Discord username.", L"Podes adicionar amigos com o nome de utilizador do Discord.", L"Você pode adicionar amigos com o nome de usuário do Discord.", L"Puedes añadir amigos con su nombre de usuario de Discord.", L"Du kannst Freunde über ihren Discord-Benutzernamen hinzufügen.", L"Puoi aggiungere amici con il loro nome utente Discord.") \
    X(SendFriendRequest, L"Envoyer une demande d'ami", L"Send Friend Request", L"Enviar pedido de amizade", L"Enviar pedido de amizade", L"Enviar solicitud de amistad", L"Freundschaftsanfrage senden", L"Invia richiesta di amicizia") \
    X(FriendRequestSent, L"Ta demande d'ami a été envoyée à {0}.", L"Success! Your friend request to {0} was sent.", L"O teu pedido de amizade foi enviado a {0}.", L"Seu pedido de amizade foi enviado para {0}.", L"Tu solicitud de amistad se envió a {0}.", L"Deine Freundschaftsanfrage an {0} wurde gesendet.", L"La tua richiesta di amicizia è stata inviata a {0}.") \
    X(FriendRequestCaptcha, L"Discord demande une vérification (captcha) : envoie cette demande depuis l'appli officielle.", L"Discord requires a captcha: send this request from the official app.", L"O Discord pede uma verificação (captcha): envia este pedido pela app oficial.", L"O Discord pede uma verificação (captcha): envie este pedido pelo app oficial.", L"Discord pide un captcha: envía esta solicitud desde la app oficial.", L"Discord verlangt ein Captcha: Sende diese Anfrage über die offizielle App.", L"Discord richiede un captcha: invia questa richiesta dall'app ufficiale.") \
    X(FriendRequestFailed, L"Hum, ça n'a pas marché. Vérifie le nom d'utilisateur.", L"Hm, didn't work. Double check that the username is correct.", L"Hum, não funcionou. Confirma o nome de utilizador.", L"Hmm, não funcionou. Confira o nome de usuário.", L"Mmm, no funcionó. Revisa el nombre de usuario.", L"Hm, das hat nicht geklappt. Prüfe den Benutzernamen.", L"Mmm, non ha funzionato. Controlla il nome utente.") \
    X(FriendIncoming, L"Demande d'ami reçue", L"Incoming Friend Request", L"Pedido de amizade recebido", L"Pedido de amizade recebido", L"Solicitud de amistad recibida", L"Eingehende Freundschaftsanfrage", L"Richiesta di amicizia ricevuta") \
    X(FriendOutgoing, L"Demande d'ami envoyée", L"Outgoing Friend Request", L"Pedido de amizade enviado", L"Pedido de amizade enviado", L"Solicitud de amistad enviada", L"Ausgehende Freundschaftsanfrage", L"Richiesta di amicizia inviata") \
    X(Message, L"Message", L"Message", L"Mensagem", L"Mensagem", L"Mensaje", L"Nachricht", L"Messaggio") \
    X(Accept, L"Accepter", L"Accept", L"Aceitar", L"Aceitar", L"Aceptar", L"Annehmen", L"Accetta") \
    X(Decline, L"Refuser", L"Ignore", L"Recusar", L"Recusar", L"Rechazar", L"Ablehnen", L"Rifiuta") \
    X(CancelRequest, L"Annuler la demande", L"Cancel Request", L"Cancelar pedido", L"Cancelar pedido", L"Cancelar solicitud", L"Anfrage abbrechen", L"Annulla richiesta") \
    X(RemoveFriend, L"Retirer l'ami", L"Remove Friend", L"Remover amigo", L"Remover amigo", L"Eliminar amigo", L"Freund entfernen", L"Rimuovi amico") \
    X(RemoveFriendTitle, L"Retirer « {0} »", L"Remove '{0}'", L"Remover \"{0}\"", L"Remover \"{0}\"", L"Eliminar a «{0}»", L"„{0}“ entfernen", L"Rimuovi \"{0}\"") \
    X(RemoveFriendConfirm, L"Tu es sûr de vouloir retirer cette personne de tes amis ?", L"Are you sure you want to remove this person from your friends?", L"Tens a certeza de que queres remover esta pessoa dos teus amigos?", L"Tem certeza de que deseja remover esta pessoa dos seus amigos?", L"¿Seguro que quieres eliminar a esta persona de tus amigos?", L"Möchtest du diese Person wirklich aus deinen Freunden entfernen?", L"Vuoi davvero rimuovere questa persona dagli amici?") \
    X(ActiveNow, L"Actifs maintenant", L"Active Now", L"Ativo agora", L"Ativo agora", L"Activo ahora", L"Gerade aktiv", L"Attivi ora") \
    X(QuietNow, L"C'est calme pour le moment…", L"It's quiet for now…", L"Está tudo calmo por agora…", L"Está tranquilo por enquanto…", L"Todo está tranquilo por ahora…", L"Gerade ist es ruhig…", L"Per ora è tutto tranquillo…") \
    X(Playing, L"Joue à {0}", L"Playing {0}", L"A jogar {0}", L"Jogando {0}", L"Jugando a {0}", L"Spielt {0}", L"Sta giocando a {0}") \
    X(Streaming, L"Streame {0}", L"Streaming {0}", L"A transmitir {0}", L"Transmitindo {0}", L"Transmitiendo {0}", L"Streamt {0}", L"In streaming: {0}") \
    X(Listening, L"Écoute {0}", L"Listening to {0}", L"A ouvir {0}", L"Ouvindo {0}", L"Escuchando {0}", L"Hört {0}", L"Sta ascoltando {0}") \
    X(Watching, L"Regarde {0}", L"Watching {0}", L"A ver {0}", L"Assistindo {0}", L"Viendo {0}", L"Schaut {0}", L"Sta guardando {0}") \
    X(Competing, L"Participe à {0}", L"Competing in {0}", L"A competir em {0}", L"Competindo em {0}", L"Compitiendo en {0}", L"Tritt an in {0}", L"In gara in {0}") \
    X(SendMessageIn, L"Envoyer un message dans #{0}", L"Message #{0}", L"Enviar mensagem para #{0}", L"Conversar em #{0}", L"Enviar mensaje a #{0}", L"Nachricht an #{0}", L"Invia un messaggio in #{0}") \
    X(SendMessageTo, L"Envoyer un message à @{0}", L"Message @{0}", L"Enviar mensagem para @{0}", L"Conversar com @{0}", L"Enviar mensaje a @{0}", L"Nachricht an @{0}", L"Invia un messaggio a @{0}") \
    X(TodayAt, L"Aujourd'hui à {0}", L"Today at {0}", L"Hoje às {0}", L"Hoje às {0}", L"Hoy a las {0}", L"Heute um {0}", L"Oggi alle {0}") \
    X(YesterdayAt, L"Hier à {0}", L"Yesterday at {0}", L"Ontem às {0}", L"Ontem às {0}", L"Ayer a las {0}", L"Gestern um {0}", L"Ieri alle {0}") \
    X(Edited, L" (modifié)", L" (edited)", L" (editada)", L" (editado)", L" (editado)", L" (bearbeitet)", L" (modificato)") \
    X(TypingOne, L"{0} est en train d'écrire…", L"{0} is typing…", L"{0} está a escrever…", L"{0} está digitando…", L"{0} está escribiendo…", L"{0} schreibt…", L"{0} sta scrivendo…") \
    X(TypingTwo, L"{0} et {1} sont en train d'écrire…", L"{0} and {1} are typing…", L"{0} e {1} estão a escrever…", L"{0} e {1} estão digitando…", L"{0} y {1} están escribiendo…", L"{0} und {1} schreiben…", L"{0} e {1} stanno scrivendo…") \
    X(TypingMany, L"Plusieurs personnes sont en train d'écrire…", L"Several people are typing…", L"Várias pessoas estão a escrever…", L"Várias pessoas estão digitando…", L"Varias personas están escribiendo…", L"Mehrere Personen schreiben…", L"Più persone stanno scrivendo…") \
    X(Someone, L"Quelqu'un", L"Someone", L"Alguém", L"Alguém", L"Alguien", L"Jemand", L"Qualcuno") \
    X(UnnamedGroup, L"Groupe sans nom", L"Unnamed Group", L"Grupo sem nome", L"Grupo sem nome", L"Grupo sin nombre", L"Unbenannte Gruppe", L"Gruppo senza nome") \
    X(NoAccess, L"Tu n'as pas accès à ce salon.", L"You don't have access to this channel.", L"Não tens acesso a este canal.", L"Você não tem acesso a este canal.", L"No tienes acceso a este canal.", L"Du hast keinen Zugriff auf diesen Kanal.", L"Non hai accesso a questo canale.") \
    X(LoadError, L"Erreur de chargement : {0}", L"Failed to load: {0}", L"Erro ao carregar: {0}", L"Erro ao carregar: {0}", L"Error al cargar: {0}", L"Laden fehlgeschlagen: {0}", L"Errore di caricamento: {0}") \
    X(SendFailed, L"Envoi impossible : {0}", L"Couldn't send: {0}", L"Não foi possível enviar: {0}", L"Não foi possível enviar: {0}", L"No se pudo enviar: {0}", L"Senden fehlgeschlagen: {0}", L"Invio non riuscito: {0}") \
    X(DeleteFailed, L"Suppression impossible : {0}", L"Couldn't delete: {0}", L"Não foi possível eliminar: {0}", L"Não foi possível excluir: {0}", L"No se pudo eliminar: {0}", L"Löschen fehlgeschlagen: {0}", L"Eliminazione non riuscita: {0}") \
    X(FileTooBig, L"Fichiers trop lourds (20 Mo max sans Nitro).", L"Files too large (20 MB max without Nitro).", L"Ficheiros demasiado grandes (máx. 20 MB sem Nitro).", L"Arquivos muito grandes (máx. 20 MB sem Nitro).", L"Archivos demasiado grandes (máx. 20 MB sin Nitro).", L"Dateien zu groß (max. 20 MB ohne Nitro).", L"File troppo grandi (max 20 MB senza Nitro).") \
    X(Uploading, L"Envoi de {0}…", L"Uploading {0}…", L"A enviar {0}…", L"Enviando {0}…", L"Subiendo {0}…", L"{0} wird hochgeladen…", L"Caricamento di {0}…") \
    X(Attachment, L"Pièce jointe", L"Attachment", L"Anexo", L"Anexo", L"Archivo adjunto", L"Anhang", L"Allegato") \
    X(ImageWord, L"Image", L"Image", L"Imagem", L"Imagem", L"Imagen", L"Bild", L"Immagine") \
    X(ClickToSeeAttachment, L"Clique pour voir les pièces jointes", L"Click to see attachment", L"Clica para ver o anexo", L"Clique para ver o anexo", L"Haz clic para ver el adjunto", L"Klicke, um den Anhang anzusehen", L"Clicca per vedere l'allegato") \
    X(StickerLabel, L"[Autocollant : {0}]", L"[Sticker: {0}]", L"[Autocolante: {0}]", L"[Figurinha: {0}]", L"[Sticker: {0}]", L"[Sticker: {0}]", L"[Sticker: {0}]") \
    X(SysJoined, L"→ a rejoint le serveur.", L"→ joined the server.", L"→ entrou no servidor.", L"→ entrou no servidor.", L"→ se unió al servidor.", L"→ ist dem Server beigetreten.", L"→ è entrato nel server.") \
    X(SysPinned, L"📌 a épinglé un message.", L"📌 pinned a message.", L"📌 afixou uma mensagem.", L"📌 fixou uma mensagem.", L"📌 fijó un mensaje.", L"📌 hat eine Nachricht angeheftet.", L"📌 ha fissato un messaggio.") \
    X(SysBoost, L"🚀 a boosté le serveur !", L"🚀 boosted the server!", L"🚀 impulsionou o servidor!", L"🚀 impulsionou o servidor!", L"🚀 mejoró el servidor!", L"🚀 hat den Server geboostet!", L"🚀 ha potenziato il server!") \
    X(RoleWord, L"rôle", L"role", L"cargo", L"cargo", L"rol", L"Rolle", L"ruolo") \
    X(RoleTitle, L"Rôle", L"Role", L"Cargo", L"Cargo", L"Rol", L"Rolle", L"Ruolo") \
    X(UnknownUser, L"utilisateur", L"unknown-user", L"utilizador", L"usuário", L"usuario", L"Benutzer", L"utente") \
    X(UnknownChannel, L"salon-inconnu", L"unknown-channel", L"canal-desconhecido", L"canal-desconhecido", L"canal-desconocido", L"unbekannter-kanal", L"canale-sconosciuto") \
    X(AddReaction, L"Ajouter une réaction", L"Add Reaction", L"Adicionar reação", L"Adicionar reação", L"Añadir reacción", L"Reaktion hinzufügen", L"Aggiungi reazione") \
    X(EditMessage, L"Modifier le message", L"Edit Message", L"Editar mensagem", L"Editar mensagem", L"Editar mensaje", L"Nachricht bearbeiten", L"Modifica messaggio") \
    X(Reply, L"Répondre", L"Reply", L"Responder", L"Responder", L"Responder", L"Antworten", L"Rispondi") \
    X(ForwardAction, L"Transférer", L"Forward", L"Reencaminhar", L"Encaminhar", L"Reenviar", L"Weiterleiten", L"Inoltra") \
    X(ForwardTitle, L"Transférer vers…", L"Forward to…", L"Reencaminhar para…", L"Encaminhar para…", L"Reenviar a…", L"Weiterleiten an…", L"Inoltra a…") \
    X(Forwarded, L"↪ Transféré", L"↪ Forwarded", L"↪ Reencaminhada", L"↪ Encaminhada", L"↪ Reenviado", L"↪ Weitergeleitet", L"↪ Inoltrato") \
    X(CopyText, L"Copier le texte", L"Copy Text", L"Copiar texto", L"Copiar texto", L"Copiar texto", L"Text kopieren", L"Copia testo") \
    X(CopyLink, L"Copier le lien du message", L"Copy Message Link", L"Copiar link da mensagem", L"Copiar link da mensagem", L"Copiar enlace del mensaje", L"Nachrichtenlink kopieren", L"Copia link messaggio") \
    X(CopyId, L"Copier l'identifiant", L"Copy Message ID", L"Copiar ID da mensagem", L"Copiar ID da mensagem", L"Copiar ID del mensaje", L"Nachrichten-ID kopieren", L"Copia ID messaggio") \
    X(DeleteMessage, L"Supprimer le message", L"Delete Message", L"Eliminar mensagem", L"Excluir mensagem", L"Eliminar mensaje", L"Nachricht löschen", L"Elimina messaggio") \
    X(DeleteConfirm, L"Tu es sûr de vouloir supprimer ce message ?", L"Are you sure you want to delete this message?", L"Tens a certeza de que queres eliminar esta mensagem?", L"Tem certeza de que deseja excluir esta mensagem?", L"¿Seguro que quieres eliminar este mensaje?", L"Möchtest du diese Nachricht wirklich löschen?", L"Vuoi davvero eliminare questo messaggio?") \
    X(Delete, L"Supprimer", L"Delete", L"Eliminar", L"Excluir", L"Eliminar", L"Löschen", L"Elimina") \
    X(ReplyingTo, L"Réponse à @{0}", L"Replying to @{0}", L"A responder a @{0}", L"Respondendo a @{0}", L"Respondiendo a @{0}", L"Antwort an @{0}", L"Rispondi a @{0}") \
    X(EditingHint, L"Modification du message — Échap pour annuler, Entrée pour enregistrer", L"Editing message — Esc to cancel, Enter to save", L"A editar a mensagem — Esc para cancelar, Enter para guardar", L"Editando mensagem — Esc para cancelar, Enter para salvar", L"Editando mensaje: Esc para cancelar, Intro para guardar", L"Nachricht bearbeiten – Esc zum Abbrechen, Enter zum Speichern", L"Modifica messaggio: Esc per annullare, Invio per salvare") \
    X(More, L"Plus", L"More", L"Mais", L"Mais", L"Más", L"Mehr", L"Altro") \
    X(Edit, L"Modifier", L"Edit", L"Editar", L"Editar", L"Editar", L"Bearbeiten", L"Modifica") \
    X(EditAttachment, L"Modifier la pièce jointe", L"Modify Attachment", L"Modificar anexo", L"Modificar anexo", L"Modificar archivo adjunto", L"Anhang bearbeiten", L"Modifica allegato") \
    X(RemoveAttachment, L"Supprimer la pièce jointe", L"Remove Attachment", L"Remover anexo", L"Remover anexo", L"Eliminar archivo adjunto", L"Anhang entfernen", L"Rimuovi allegato") \
    X(FileName, L"Nom de fichier", L"Filename", L"Nome do ficheiro", L"Nome do arquivo", L"Nombre de archivo", L"Dateiname", L"Nome file") \
    X(AltText, L"Description (texte alternatif)", L"Description (alt text)", L"Descrição (texto alternativo)", L"Descrição (texto alternativo)", L"Descripción (texto alternativo)", L"Beschreibung (Alternativtext)", L"Descrizione (testo alternativo)") \
    X(AddDescription, L"Ajouter une description", L"Add a description", L"Adicionar uma descrição", L"Adicionar uma descrição", L"Añadir una descripción", L"Beschreibung hinzufügen", L"Aggiungi una descrizione") \
    X(MarkSpoiler, L"Définir comme spoiler", L"Mark as spoiler", L"Marcar como spoiler", L"Marcar como spoiler", L"Marcar como spoiler", L"Als Spoiler markieren", L"Segna come spoiler") \
    X(PickEmoji, L"Choisir un emoji", L"Select emoji", L"Escolher emoji", L"Escolher emoji", L"Elegir emoji", L"Emoji auswählen", L"Scegli emoji") \
    X(ServerEmojis, L"Emojis du serveur", L"Server Emojis", L"Emojis do servidor", L"Emojis do servidor", L"Emojis del servidor", L"Server-Emojis", L"Emoji del server") \
    X(FrequentEmojis, L"Les plus utilisés", L"Frequently Used", L"Mais usados", L"Mais usados", L"Más usados", L"Häufig verwendet", L"Più usati") \
    X(VoiceConnecting, L"Connexion au vocal…", L"Connecting to voice…", L"A ligar à voz…", L"Conectando à voz…", L"Conectando a voz…", L"Verbinde mit Sprachkanal…", L"Connessione alla voce…") \
    X(VoiceConnected, L"Connecté au vocal", L"Voice Connected", L"Voz ligada", L"Voz conectada", L"Voz conectada", L"Sprachverbindung steht", L"Voce connessa") \
    X(VoiceFailed, L"Échec de la connexion vocale : {0}", L"Voice connection failed: {0}", L"Falha na ligação de voz: {0}", L"Falha na conexão de voz: {0}", L"Error en la conexión de voz: {0}", L"Sprachverbindung fehlgeschlagen: {0}", L"Connessione vocale non riuscita: {0}") \
    X(VoiceDisconnect, L"Se déconnecter", L"Disconnect", L"Desligar", L"Desconectar", L"Desconectar", L"Trennen", L"Disconnetti") \
    X(MuteMic, L"Rendre muet", L"Mute", L"Silenciar", L"Silenciar", L"Silenciar", L"Stummschalten", L"Silenzia") \
    X(UnmuteMic, L"Réactiver le micro", L"Unmute", L"Ativar microfone", L"Ativar microfone", L"Activar micrófono", L"Stummschaltung aufheben", L"Riattiva microfono") \
    X(Deafen, L"Mettre en sourdine", L"Deafen", L"Desativar som", L"Desativar áudio", L"Ensordecer", L"Taub schalten", L"Disattiva audio") \
    X(Undeafen, L"Réactiver le son", L"Undeafen", L"Ativar som", L"Ativar áudio", L"Activar audio", L"Ton einschalten", L"Riattiva audio") \
    X(StatsForNerds, L"Stats pour les nerds", L"Stats for Nerds", L"Estatísticas para nerds", L"Estatísticas para nerds", L"Estadísticas para nerds", L"Statistiken für Nerds", L"Statistiche per nerd") \
    X(ShareScreen, L"Partager ton écran", L"Share Your Screen", L"Partilhar o ecrã", L"Compartilhar sua tela", L"Compartir pantalla", L"Bildschirm teilen", L"Condividi lo schermo") \
    X(ScreenShareSoon, L"Le partage d'écran (Go Live) arrive dans la prochaine version.", L"Screen sharing (Go Live) is coming in the next version.", L"A partilha de ecrã (Go Live) chega na próxima versão.", L"O compartilhamento de tela (Go Live) chega na próxima versão.", L"Compartir pantalla (Go Live) llegará en la próxima versión.", L"Bildschirmübertragung (Go Live) kommt in der nächsten Version.", L"La condivisione dello schermo (Go Live) arriverà nella prossima versione.") \
    X(StopSharing, L"Arrêter le partage", L"Stop Streaming", L"Parar a partilha", L"Parar transmissão", L"Dejar de compartir", L"Übertragung beenden", L"Interrompi la condivisione") \
    X(ShareNeedsVoice, L"Rejoins d'abord un salon vocal pour partager ton écran.", L"Join a voice channel first to share your screen.", L"Entra primeiro num canal de voz para partilhar o ecrã.", L"Entre primeiro em um canal de voz para compartilhar sua tela.", L"Únete primero a un canal de voz para compartir tu pantalla.", L"Tritt zuerst einem Sprachkanal bei, um deinen Bildschirm zu teilen.", L"Entra prima in un canale vocale per condividere lo schermo.") \
    X(Live, L"EN DIRECT", L"LIVE", L"EM DIRETO", L"AO VIVO", L"EN DIRECTO", L"LIVE", L"IN DIRETTA") \
    X(StartCall, L"Démarrer un appel vocal", L"Start Voice Call", L"Iniciar chamada de voz", L"Iniciar chamada de voz", L"Iniciar llamada de voz", L"Sprachanruf starten", L"Avvia chiamata vocale")

namespace DiscordWin3::I18n
{
    enum class Lang : int { Fr, En, PtPT, PtBR, Es, De, It, Count };

    enum class S : int
    {
#define DISCORDWIN3_KEY(key, ...) key,
        DISCORDWIN3_STRINGS(DISCORDWIN3_KEY)
#undef DISCORDWIN3_KEY
        Count
    };

    void Initialize();                       // saved choice, else Windows display language
    void SetLanguage(Lang lang);             // also persisted
    Lang Current();
    wchar_t const* NativeName(Lang lang);    // "Français", "English"...
    wchar_t const* LocaleName(Lang lang);    // "fr-FR"... for dates
    wchar_t const* DiscordLocale(Lang lang); // X-Discord-Locale header value
    std::wstring SettingsFile();             // %LOCALAPPDATA%/DiscordWin3/settings.ini (shared by other settings)

    wchar_t const* Tr(S key);
    std::wstring Fmt(S key, std::wstring_view a0, std::wstring_view a1 = {});
}
