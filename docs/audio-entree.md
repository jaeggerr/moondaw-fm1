# Faire entrer un disque dans le FM-1 : faisabilité

29/10/2026. Recherche seule : aucun code touché, rien installé, l'appareil
n'a pas été branché pour cette étude.

La question : entendre, à travers le FM-1, un morceau qui joue sur le
téléphone ou l'ordinateur, pendant qu'on écrit, pour le relever à l'oreille.
C'est ce que font REF (le disque, sur la grille du morceau) et AMP (ce qui est
branché, entendu en direct) dans les autres apps MOONDAW
(`../moondaw/docs/ui-ios.md`, lignes REF et AMP).

## En bref

| voie | possible ? | effort | ce qu'on entend |
|---|---|---|---|
| **1. Carte son USB** (l'ordinateur ou le téléphone envoie l'audio au FM-1) | **oui** | moyen : ~1 à 2 semaines | n'importe quelle app (Spotify, YouTube, Logic), en direct, sans durée limite |
| 2. Bluetooth audio (A2DP) | **non**, en pratique | très gros, bloqué par les licences | — |
| 3. Entrée analogique (jack, micro) | **non** | — | il n'y a pas d'entrée sur la carte |
| 4. Un extrait envoyé par le câble et gardé en flash | oui | moyen | **20 à 60 s** de son, en qualité téléphone à radio AM |

**Recommandation : la voie 1, la carte son USB.** Le FM-1 se présenterait à
l'ordinateur ou au téléphone comme une sortie audio de plus. On y envoie le
disque et le FM-1 le mélange à sa sortie, à côté du morceau. Le matériel le
permet (le SDK de JieLi a une classe « haut-parleur USB » pour cette puce),
Felucca fait déjà le chemin inverse (le FM-1 vu comme une entrée audio), et ça
coûte peu de RAM et de CPU. La voie 4 reste un bon complément pour réécouter
une section sans ordinateur. Elle ne remplace pas la 1.

## 1. Le FM-1 vu comme une carte son USB

### Ce qui existe déjà

- **Felucca a une entrée audio USB** (UAC1, compatible sans pilote) :
  `firmware/src/usb.c:15-24`, option `FELUCCA_UAC`. La sortie du FM-1 part
  vers l'ordinateur en 16 bits stéréo à 44,1 ou 48 kHz, sur un point d'accès
  isochrone IN asynchrone (EP4). Ça marche sur macOS 26/27 et iOS/iPadOS
  (commentaire `usb.c:204-206`). C'est le sens **appareil → ordinateur**,
  l'inverse de ce qu'on veut. Dans MOONDAW, l'option est à 0 et la prise de
  son a été retirée d'`audio.c` (`UPSTREAM.md:18`).
- **Le contrôleur sait faire de l'isochrone dans les deux sens.** C'est un
  cœur de type MUSB : chaque point d'accès a une moitié TX et une moitié RX,
  le bit ISO est dans TXCSR2/RXCSR2, et EP4 a déjà un registre d'adresse DMA
  de réception, `FM1_USB_EP4_RADR` (`firmware/hal/fm1_usb.h:30-32`).
  Aujourd'hui, il pointe sur le tampon d'EP0 et ne sert à rien.
- **Le SDK de JieLi a une classe « haut-parleur » pour cette famille**
  (`~/fw-AC79_AIoT_SDK/apps/common/usb/device/uac.c:54-182`) : un terminal
  d'entrée « USB streaming », un terminal de sortie « Speaker », un point
  d'accès isochrone **OUT adaptatif**, lu par `usb_g_iso_read`. Réglages par
  défaut : 48 kHz, 16 bits, stéréo, EP3 (`usb_std_class_def.h:72-90`). C'est
  la preuve que le matériel de la puce reçoit de l'audio par USB. On ne
  reprend pas le code (il dépend du RTOS de JieLi), seulement les
  descripteurs, qui sont ceux de la norme.
- **Le firmware d'origine (V15)** : une revue le décrit comme une interface
  audio USB vers l'ordinateur ([Synth Anatomy](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html)).
  [Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/)
  et les fiches produit n'en parlent pas. Rien ne dit qu'il accepte de
  l'audio venant de l'ordinateur. **Non vérifié.** Ça se vérifie en une
  minute : V15 branchée, Configuration audio et MIDI sur le Mac, regarder si
  « USB Composite Device » a des sorties.

### Ce qu'il faudrait écrire

1. **Les descripteurs** (dans `usb.c`) : dans l'interface de contrôle audio,
   un terminal d'entrée de type USB streaming (0x0101), éventuellement une
   unité de volume, et un terminal de sortie de type haut-parleur (0x0301).
   L'en-tête AC doit lister une interface de plus. Il faut aussi une nouvelle
   interface de streaming : alt 0 sans bande passante, alt 1 en PCM 16 bits
   stéréo à **44,1 kHz seulement**, avec un point d'accès **EP4 OUT
   isochrone adaptatif**, de 180 o (45 trames de 4 o) plus une marge. Ça
   fait une centaine d'octets de tables, construits comme ceux de l'entrée
   existante (`usb.c:229-268`).
   - 44,1 kHz seulement : macOS et iOS rééchantillonnent eux-mêmes vers une
     sortie qui n'offre qu'un taux. On évite ainsi un 48 → 44,1 sur la
     machine (Felucca en a un pour l'autre sens, `uac_fir.h`, qu'on pourrait
     reprendre au besoin).
   - En mode adaptatif, il n'y a pas de point d'accès de retour
     (« feedback ») : c'est l'appareil qui suit le débit de l'hôte. C'est ce
     que fait le SDK. Le mode asynchrone avec retour demanderait un point
     d'accès IN de plus, alors qu'EP1 à EP4 sont déjà tous attribués
     (`usb.c:200-201`).
2. **La réception** : `usb_poll` tourne déjà à 2 kHz dans l'ISR TIMER5
   (`firmware.c:246-276`). À chaque milliseconde USB, il faut vider le
   paquet reçu sur EP4 (taille dans RXCOUNT, `ssync`, `fm1_usb_rx_sync`),
   le copier dans un anneau et réarmer. Un paquet par milliseconde et un
   sondage toutes les 0,5 ms : ça tient, comme pour l'entrée de Felucca.
3. **La dérive d'horloge** : c'est la vraie difficulté. L'I2S du FM-1 tourne
   à ~44 117,6 Hz et l'hôte envoie 44 100 trames par seconde (`usb.c:67`,
   `core/synth.h:15`). L'écart est de **+400 ppm** : le FM-1 consomme
   ~17,6 trames par seconde de plus qu'il n'en reçoit. Sans correction,
   l'anneau se vide en quelques secondes, puis claque à chaque
   sous-alimentation. La correction se fait avec un petit
   **rééchantillonneur** (interpolation linéaire ou d'Hermite) dont le
   rapport (~0,9996) est asservi au remplissage de l'anneau, par une boucle
   PI lente. Dupliquer une trame toutes les 57 ms marcherait aussi, mais
   s'entendrait sur un disque tenu.
4. **Le mélange** : dans `app_audio`, après le master du morceau et avant le
   passage en 24 bits (`OUT_SHIFT`, `audio.c:13`), sans passer dans la
   réverbération ni le delay. Un niveau, et le cycle DISQUE / MOI / DEUX du
   bouton RÉF d'iOS. L'audio entrant n'entre jamais dans un export ni dans
   un enregistrement.
5. **L'interface** : une ligne ou un écran à la manière d'AMP (niveau, et un
   vumètre pour répondre à la question « est-ce que ça arrive »). Ce sera à
   dessiner le moment venu, un écran à la fois.

### Ce que ça coûte

- **RAM** : un anneau de 1 024 trames stéréo 16 bits (23 ms), 4 Kio, plus un
  tampon de paquet de 184 o. Il reste 8,6 Kio dans .data+.bss et ~65 Kio
  dans le pool (`docs/sampler-faisabilite.md`, § 1). Ça tient.
- **CPU** : la copie de 45 trames par milliseconde ne coûte rien. Le
  rééchantillonneur stéréo coûte ~10 à 20 instructions par trame, soit
  **~30 à 60 µs par bloc de 2 902 µs (1 à 2 %)**, sur une machine qui
  culmine à MAX 57 % sur DPEN-CHORUS (NOTES 29/10).
- **Flash** : quelques Kio de code.
- **Temps** : les descripteurs et la réception en 2-3 jours, l'asservissement
  de la dérive et les tests en 2-3 jours, l'écran et les essais sur Mac et
  iPhone en quelques jours. Soit **une à deux semaines**.

### Les risques

- **`usb.c` est une copie mot pour mot** de Felucca (`UPSTREAM.md:16`). Y
  ajouter une sortie audio en fait un fichier à nous. C'est à décider avec le
  passage à felucca-bsp (`docs/ARRIVEE.md`, fin du § 4).
- **Les interruptions coupées** : effacer un secteur de flash bloque ~50 ms
  (NOTES, « reverb, delay, flash store »). Pendant SAVE, le disque saute.
  C'est déjà le cas du morceau, donc acceptable.
- **macOS 13-15 et le port série (CDC)** : sur ces versions, le pilote CDC
  d'Apple empêche le pilote audio de prendre les interfaces d'un appareil
  composite qui a un CDC (`usb.c:204-206`). Il faut tester avec
  `usb_cdc_on = 0`, ou sur macOS 26+, ce qu'a Jaeger.
- **Windows** n'est pas testé (Felucca ne dit rien du sens OUT).
- **Le téléphone** : un iPhone en USB-C (ou Lightning avec l'adaptateur
  appareil photo) envoie tout son audio vers une sortie USB compatible, sans
  réglage. Il alimente aussi le FM-1 par VBUS, donc il le charge et vide la
  batterie du téléphone plus vite. Le MIDI (et donc le lien de MOONDAW) passe
  par le même câble.
- **Sur le Mac**, il faut choisir le FM-1 comme sortie (ou faire un
  « appareil à sorties multiples ») : le disque ne sort plus des
  haut-parleurs du Mac.

## 2. Bluetooth

### Le matériel : oui, il y a une radio et une antenne

- L'AC791N (« WL82 » dans le SDK) a du Wi-Fi 802.11b/g/n, du Bluetooth
  classique et du BLE ([comparatif AC791N/AC792N](https://gitcode.csdn.net/69e9857554b52172bc6bb671.html),
  source secondaire, pas de fiche officielle trouvée).
- Le FM-1 d'origine fait du **MIDI Bluetooth (BLE MIDI)**, utilisable en même
  temps que l'USB et le TRS ([Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/),
  [Sonicstate](https://sonicstate.com/news/2026/07/10/m-vave-fm-1-reviews-/)).
  Synth Anatomy écrit « Bluetooth MIDI and audio », mais aucune fiche ne
  parle d'audio Bluetooth : à prendre comme non confirmé.
- Le firmware alternatif **fm1-dock** a fait marcher le BLE MIDI sur un vrai
  FM-1 le 25/10 : il émet à -45 dBm au bureau et un Mac s'y connecte sans
  appairage ([fm1-dock, PLAN.md « BLE-MIDI »](https://github.com/joakimeriksson/fm1-dock)).
  L'antenne est donc bien là et la radio se pilote hors du firmware
  d'origine.
- Felucca, et donc MOONDAW, n'allument jamais la radio
  (`docs/FELUCCA-LICENSING.md`, « Radio »).

### Ce qui se fait sans JieLi : le BLE, pas l'audio

- fm1-dock utilise la pile d'**Optimist** (w0ts/optimist, GPL-3.0), écrite
  d'après les normes Bluetooth : couche liaison, hôte, ATT, BLE-MIDI et un
  pilote du bloc radio WL82, sans code du fabricant. Elle pèse **31 Kio de
  flash et 8 Kio de RAM**. Les tables de démarrage de la radio ont été
  capturées sur la V15, et les réglages d'usine de chaque appareil sont lus
  dans la zone VM de la V15, à 0xE8000 (`fm1-dock/firmware/src/ble/README.md`,
  PLAN.md). MOONDAW ne touche jamais cette zone (NOTES, « Rules »), ce qui la
  préserve.
- **Le BLE ne transporte pas d'audio qu'un téléphone sache envoyer.** L'audio
  BLE (LE Audio, LC3) demande des canaux isochrones Bluetooth 5.2, alors que
  la puce est annoncée en BLE 5.0. Un flux maison sur GATT demanderait une
  app à nous des deux côtés : on perdrait l'intérêt de « n'importe quelle
  app du téléphone ».

### L'A2DP (le FM-1 en enceinte Bluetooth) : non

- L'A2DP demande le **Bluetooth classique** (BR/EDR) avec L2CAP, SDP, AVDTP,
  l'appairage sécurisé et un décodeur SBC. La seule pile disponible pour
  cette puce est celle de JieLi : `btctrler.a` (5,2 Mo d'archive) et
  `btstack.a` (3,1 Mo) dans `~/fw-AC79_AIoT_SDK/cpu/wl82/liba/`, avec
  l'A2DP dans `include_lib/btstack/a2dp_media_codec.h`.
- Cette pile est **fermée** (du bitcode), construite autour du RTOS de JieLi,
  et pèse 120 à 200 Kio une fois liée (fm1-dock, PLAN.md). Un firmware sous
  GPL-3.0 comme MOONDAW, issu de Felucca, ne peut pas la lier. Il faudrait
  aussi porter MOONDAW sur le RTOS du SDK.
- Écrire une pile Bluetooth classique et un pilote BR/EDR de zéro, comme
  Optimist l'a fait pour le BLE, se compte en mois, sans aucune garantie : le
  bloc radio classique n'est pas documenté.
- **Conclusion : pas réaliste.** Le BLE MIDI, lui, est à portée (reprendre
  Optimist, GPL-3.0 comme nous), mais il ne répond pas à la question.

## 3. Une entrée analogique

**Il n'y en a pas.**

- Les prises du FM-1 : une sortie stéréo 3,5 mm (le signal mono recopié sur
  les deux côtés), une entrée MIDI TRS 3,5 mm, l'USB-C et le haut-parleur. Ni
  entrée ligne, ni micro ([Synth Anatomy](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html),
  [Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/)).
- `fm1_audio.h` ne fait que de la sortie : I2S ALNK0 vers un codec externe,
  piloté par PC0/1/2/6. Rien n'est lu dans l'autre sens.
- Le SARADC (`firmware/hal/fm1_adc.h`) lit le potard MASTER (PB6) et la
  batterie (PB1), sur 10 bits, par sondage. Même à 44 kHz, ce serait 10 bits
  sans filtre anti-repliement, et aucune broche libre n'est câblée vers une
  prise.
- L'AC791N a peut-être des entrées micro internes, mais rien n'indique
  qu'elles soient reliées sur cette carte. Il faudrait souder, et ce n'est
  pas l'objet.

## 4. Un extrait gardé dans la flash (sans rien de nouveau côté matériel)

L'idée : l'app Mac découpe la partie du disque qu'on relève (un couplet, un
refrain), la compresse et l'envoie par le lien USB-MIDI qui envoie déjà les
morceaux (`app/link.h`). Le FM-1 la range dans la flash et la joue à côté du
morceau, comme REF sur iOS.

### La place

La flash fait 1 Mio et presque tout est pris (`docs/sampler-faisabilite.md`,
§ 1) :

- **le store** : 273 Kio libres sur 292, partagés avec les morceaux ;
- la zone « USR » (0xEA000–0xFC000, 72 Kio) : déclarée mais non utilisée,
  pas autorisée par `FL_STORE_OK` aujourd'hui, à vérifier avant usage ;
- le reste de l'emplacement firmware (~396 Kio) : il ne s'écrit que par une
  mise à jour, et c'est la place dont le code a besoin pour grandir. À
  exclure.

En réservant **256 Kio** du store à un seul extrait, mono :

| format | débit | durée dans 256 Kio | qualité |
|---|---|---|---|
| 16 bits, 44,1 kHz | 86 Kio/s | 3 s | CD |
| IMA ADPCM 4 bits, 22 kHz | 10,8 Kio/s | **24 s** | radio FM, aigus un peu granuleux |
| IMA ADPCM 4 bits, 16 kHz | 7,8 Kio/s | **33 s** | bande jusqu'à 8 kHz : tout s'entend sauf l'air des cymbales |
| IMA ADPCM 4 bits, 11 kHz | 5,4 Kio/s | **47 s** | radio AM : basse, voix et accords nets |
| IMA ADPCM 4 bits, 8 kHz | 3,9 Kio/s | **66 s** | téléphone : la mélodie et la basse restent lisibles |

Pour relever des notes, **16 kHz en ADPCM** est le bon compromis : ~30 s,
soit 16 mesures à 120 bpm. Un MP3 ou un Opus doublerait à peu près la durée,
mais le décodeur pèse plusieurs dizaines de Kio de code et coûte cher sur un
cœur à ~51 instructions simples par µs (NOTES 28/10). Ça n'en vaut pas la
peine.

### Le coût

- **CPU** : un décodeur ADPCM et une lecture à pas fixe, ~25 µs par bloc
  (même calcul que la voix sample de `docs/sampler-faisabilite.md`, § 4).
  Lu en XIP depuis la fenêtre en clair (`fl_plain_window_init`), sans copie
  en RAM.
- **Le transfert** : 256 Kio par SysEx, 8 bits dans 7, en stop-and-wait. Le
  débit n'a jamais été mesuré ; il faut compter **de quelques secondes à une
  minute**, musique arrêtée. Il y a aussi 64 secteurs à effacer, à ~50 ms
  pièce, soit ~3 s.
- **L'usure** : chaque nouvel extrait réécrit 64 secteurs. À ~100 000 cycles
  par secteur, ce n'est pas un problème pour un usage d'atelier.
- **La place des morceaux** : un extrait de 256 Kio laisse ~17 Kio aux
  morceaux, moins d'un morceau plein (25 Kio). Il faudrait donc soit réduire
  l'extrait à ~128 Kio (15 s à 16 kHz), soit libérer la zone USR après
  vérification.
- **Le code** : ce chemin est partagé avec le sampler (le même lecteur
  ADPCM, le même type de fichier dans le store, la même conversion côté Mac).
  Si le sampler se fait, l'extrait coûte peu de plus.

### Ce que ça donne

Ça marche sans ordinateur une fois l'extrait chargé, et l'extrait reste calé
sur la grille du morceau. En revanche, on n'a qu'un bout du disque, en
qualité réduite, et il faut repasser par le Mac à chaque section. Pour
« relever un morceau entier à l'oreille », la voie 1 est bien meilleure.

### Entre les deux : un flux en direct par le lien MIDI

On pourrait aussi envoyer le disque **en continu** par le SysEx existant,
sans nouveau descripteur USB : 16 kHz en ADPCM, c'est 8 Kio/s, et un paquet
USB-MIDI de 64 o porte ~42 o utiles. Mais il faudrait un programme à nous qui
lit le fichier et l'envoie, ce qui ne marche pas depuis Spotify ou le
téléphone. Le protocole du lien (stop-and-wait) n'est pas fait pour ça, et
il faudrait quand même gérer la dérive d'horloge. Ça prend la difficulté de la
voie 1 sans son avantage. À écarter.

## Recommandation

1. **Faire la carte son USB (voie 1).** C'est la seule voie qui donne « je
   lance le disque sur mon téléphone ou mon Mac et je l'entends dans le
   FM-1 », sans limite de durée et avec n'importe quelle app. Le matériel le
   permet, la RAM et le CPU suffisent, et la méthode est connue : la
   classe haut-parleur du SDK JieLi, l'entrée UAC de Felucca, l'asservissement
   de dérive qu'il faut de toute façon.
2. **Première étape, une mesure** : une version de test qui annonce
   seulement la sortie audio et joue l'anneau tel quel, sans
   rééchantillonneur. On vérifie que macOS 26 et l'iPhone la voient, qu'EP4
   OUT reçoit bien les paquets, et on lit sur l'écran la vitesse à laquelle
   l'anneau dérive (on attend ~17,6 trames/s). On ajoute ensuite
   l'asservissement, puis le niveau et le bouton DISQUE / MOI / DEUX.
3. **L'extrait en flash (voie 4)** : seulement si le sampler se fait, comme
   un usage de plus de son lecteur ADPCM. Pas pour lui-même.
4. **Bluetooth** : ne rien faire pour l'audio. Le BLE MIDI d'Optimist est une
   autre question, à poser séparément si elle intéresse.

## Sources

- Local : `firmware/src/usb.c` (l.3-24, 66-130, 198-268), `firmware/hal/fm1_usb.h`,
  `firmware/hal/fm1_audio.h`, `firmware/hal/fm1_adc.h`, `firmware/src/audio.c`,
  `firmware/src/firmware.c:246-276`, `core/synth.h:15`, `NOTES.md`, `UPSTREAM.md`,
  `docs/FELUCCA-LICENSING.md`, `docs/ARRIVEE.md`, `docs/sampler-faisabilite.md`,
  `../moondaw/docs/ui-ios.md` (REF, AMP).
- SDK JieLi AC79 (`~/fw-AC79_AIoT_SDK`) : `apps/common/usb/device/uac.c`,
  `apps/common/usb/usb_std_class_def.h`, `cpu/wl82/liba/btctrler.a`, `btstack.a`,
  `include_lib/btstack/a2dp_media_codec.h`.
- fm1-dock : [github.com/joakimeriksson/fm1-dock](https://github.com/joakimeriksson/fm1-dock)
  (PLAN.md « BLE-MIDI », `firmware/src/ble/README.md`), et sa
  [version 0.6](https://newreleases.io/project/github/joakimeriksson/fm1-dock/release/v0.6) ;
  la pile [Optimist](https://github.com/w0ts/optimist).
- Fiches et revues du FM-1 : [Synthtopia](https://www.synthtopia.com/content/2026/07/13/m-vave-introduces-fm-1-fm-pocket-synthesizer/),
  [Synth Anatomy](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html),
  [Sonicstate](https://sonicstate.com/news/2026/07/10/m-vave-fm-1-reviews-/),
  [M-VAVE ouvre le firmware (Synthtopia)](https://synthtopia.com/content/2026/10/20/m-vave-officially-opens-fm-1-firmware-to-developers-with-new-workbench-sdk).
- AC791N : [comparatif AC791N / AC792N](https://gitcode.csdn.net/69e9857554b52172bc6bb671.html)
  (secondaire ; pas de fiche technique officielle trouvée).
