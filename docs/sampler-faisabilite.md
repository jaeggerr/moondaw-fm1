# Un sampler dans MOONDAW sur FM-1 : faisabilité

29/10/2026. Recherche seule : aucun code touché, rien installé. L'appareil a
été interrogé une fois, en lecture (`fm1_ctl.py status`).

## En bref

- **Flash** : puce de 1 Mio. Il reste **~396 Kio** libres dans l'emplacement
  du firmware et **273 Kio** libres dans le store des morceaux (mesurés sur
  l'appareil). Le reste de la puce est pris par Felucca, l'OTA et le boot.
- **RAM** : 512 Kio en tout, presque tout est pris. Il reste **~65 Kio** dans
  le pool, 8,6 Kio dans .data+.bss, et 32 Kio jamais utilisés en bas de la
  SRAM. Ça fait environ **1 s de son en 16 bits** : trop peu pour garder les
  samples en RAM.
- **Mais la lecture peut se faire directement depuis la flash** : la zone du
  store est déjà mappée en XIP en clair au boot. Un sample s'y lit comme un
  tableau, sans copie en RAM.
- **Ce qui tient** : en IMA ADPCM (4:1), environ **13 s dans le store** et
  **18 s dans le firmware**, soit à peu près **100 coups de batterie de
  0,3 s**, ou une quinzaine de notes de 2 s. En 16 bits brut, c'est quatre
  fois moins.
- **Pas d'entrée audio** sur le FM-1 : les samples viennent de l'ordinateur,
  par le câble.
- **Recommandation** : oui, mais seulement sous une **forme réduite**. Des
  one-shots (batterie, percussions, coups courts) en ADPCM, lus depuis la
  flash, choisis par `fSMP`, sans multi-échantillonnage ni enregistrement sur
  la machine. Pas de pianos ni d'instruments multi-échantillonnés.

## 1. Ce qui est libre aujourd'hui

### Flash

La puce répond à l'ID JEDEC `0x856014`, soit 2^0x14 = **1 Mio**
(`firmware/src/firmware.c:332`). Voici sa carte, tirée de
`firmware/hal/fm1_flash.h:38-47` et `tools/fm1pkg_make.py` :

| zone | adresses | taille | usage |
|---|---|---|---|
| en-tête | 0x00000–0x04000 | 16 Kio | SPL, entrées JLFS |
| zone app | 0x04000–0x93000 | 588 Kio | emplacement app `APP_SLOT` = 0x8DFBC = **581 564 o**, puis cfg_tool |
| (début VM) | 0x93000–0x97000 | 16 Kio | non utilisé par MOONDAW, non vérifié |
| **store** | 0x97000–0xE0000 | **292 Kio** | 73 secteurs de 4 Kio (`PLAT_FLASH_SECTORS`) |
| OTA | 0xE0000–0xE5000 | 20 Kio | staging du loader de mise à jour |
| Felucca | 0xE5000–0xE8000 | 12 Kio | autosave, banque FM6 |
| VM backup, BTIF | 0xE8000–0xEA000 | 8 Kio | SDK, jamais écrit |
| « USR » | 0xEA000–0xFC000 | 72 Kio | déclaré par le paquet, inutilisé ici (à vérifier avant de s'en servir) |
| globals | 0xFC000–0xFF000 | 12 Kio | |
| key_mac | 0xFF000–0x100000 | 4 Kio | |

- **Le firmware** pèse 175 912 o (`build/build.log`). Dans l'emplacement de
  581 564 o, il reste donc **405 652 o, ~396 Kio**. On peut y compiler des
  samples d'usine, lus en XIP comme n'importe quelle constante. Ces octets
  sont aussi ceux dont le firmware aura besoin pour grandir.
- **Le store** contient 32 emplacements de morceaux (`core/store.h`). Un
  morceau fait au plus 25 Kio (NOTES, « Songs from the computer »).
  L'appareil répond `free=273` : **273 Kio libres** sur 292, avec les
  morceaux qui y sont aujourd'hui. Des samples rangés là prendraient la place
  des morceaux, et inversement.
- **En réserve, sans garantie** : la zone « USR » (72 Kio) et
  0x93000–0x97000 (16 Kio). `FL_STORE_OK` ne les autorise pas aujourd'hui.

### RAM

La SRAM fait 512 Kio (0x01C00000–0x01C80000). Le découpage vient de
`firmware/app.ld`, les chiffres de `build/build.log` :

- **.data+.bss** : 64 952 o sur 73 728, soit **8,6 Kio libres**.
- **pool** : 261 016 o sur 335 872, soit 73 Kio libres. `tools/build.py`
  exige d'en garder 8 Kio, il en reste donc **~65 Kio utilisables**. Les gros
  occupants : `scratch` 128 Kio (les lignes du delay, qui servent aussi de
  copie de travail au chargement), `app_song` 79 Kio, la reverb 25 Kio, les
  pistes 14,6 Kio, `vox_pool` 7,8 Kio, et les deux `lanes` 7 Kio (dont la
  seconde est celle du CPU1).
- **RAMTEXT** (le code audio exécuté depuis la RAM) : plein à 1,5 Kio près.
- **0x01C00000–0x01C08000** : 32 Kio inutilisés depuis le 28/10 (commentaire
  de `app.ld`).
- **Le second cœur** n'a pas de mémoire à lui. Il prend sa `Lane` dans le
  pool et ses piles. Sa boucle d'attente tourne depuis la RAM pour qu'un CPU1
  au repos ne lise jamais la flash.

Au total, **~97 Kio** pourraient servir de tampon, mais seulement en rognant
la marge.

## 2. Ce que coûte un sample

Le moteur tourne à `SYNTH_FS` 44 100 Hz (`core/synth.h:15`), 44 117,6 Hz sur
l'appareil. Les samples sont mono.

| format | o/s | ms par Kio |
|---|---|---|
| 16 bits | 88 235 | 11,6 |
| 8 bits ou µ-law (2:1) | 44 118 | 23,2 |
| IMA ADPCM (4:1) | 22 059 | 46,4 |

Voici ce qui tient dans chaque zone, en coups de 0,3 s et en notes de 2 s.
Entre parenthèses, les secondes si on rééchantillonne à 22 kHz :

| où | 16 bits | µ-law | ADPCM |
|---|---|---|---|
| store, 273 Kio | 3,2 s : 10 coups, 1 note | 6,3 s : 21 coups, 3 notes | **12,7 s : 42 coups, 6 notes** (25 s) |
| firmware, 396 Kio | 4,6 s : 15 coups, 2 notes | 9,2 s : 30 coups, 4 notes | **18,4 s : 61 coups, 9 notes** (37 s) |
| RAM, ~97 Kio | 1,1 s : 3 coups | 2,3 s : 7 coups | 4,5 s : 15 coups |

Pour juger avec de vrais kits, ceux de `../moondaw/instruments` :

- **TR-808** : 12 sons, 479 Ko en WAV 16 bits, 5,4 s. En ADPCM, ça fait
  **~120 Kio**, et ~60 Kio à 22 kHz.
- **TR-909** : 10 sons, 495 Ko, 5,6 s. En ADPCM, **~124 Kio**.
- **Une note de piano** (`pf_c4.wav`) : 370 Ko à elle seule. Un piano
  multi-échantillonné est donc hors de portée.

Les deux kits de batterie tiennent ensemble dans le firmware en ADPCM
(~245 Kio).

### Lire depuis la flash, ou depuis la RAM ?

**Depuis la flash.** Au boot, `fl_plain_window_init()`
(`firmware/hal/fm1_flash.h:222`, appelée dans `firmware.c:335`) mappe tout
0x93000–0x100000 en **XIP en clair**. `FM1_XIP(off)` (`fm1_xip.h`) y donne un
pointeur direct, donc la voix lit `ptr[i]` sans rien copier. Les samples
compilés dans le firmware se lisent de la même façon : le XIP les déchiffre
à la volée.

Ce n'est pas le chemin que prend le store aujourd'hui. `plat_flash_read`
(`storage_hw.c`) coupe le XIP et lit par SPI, par fenêtres de 256 o
interruptions coupées. Ça convient au chargement d'un morceau, pas au rendu
audio.

## 3. Ce qu'ont déjà les autres apps MOONDAW

Dans `../moondaw` :

- **La table `smp`** (FORMAT.md, « Samples ») : un index, un nom de fichier
  WAV, et en option `begin`, `loop a b` et `ping`. L'audio n'est jamais dans
  le `.trk`, seulement son nom. Le fichier est cherché dans le bundle, puis
  dans `samples/`, puis dans `instruments/<dossier>/`.
- **Les zones** : chaque instrument SMPL est une carte de touches (`zone 05
  C-2 B-3 root E-2 smp 00`, plus `tune` et `gain`). La première zone qui
  contient la note gagne.
- **`fSMP`** : une étape joue la prise n° NN, quelles que soient les zones,
  avec la note de base à C-4. La commande ne vaut que sur le moteur SMPL et
  ne dure qu'un coup (un one-shot). L'enveloppe, le filtre et le niveau
  restent ceux de l'instrument.
- **Le moteur** (`src/instruments/sample.c`, 348 lignes) : lecture en
  16 bits avec une phase en virgule fixe 32.32, rapport de hauteur calculé en
  `double`, `exp2`, boucle, ping-pong. Une pré-attaque déclenche la note en
  avance pour que `begin` tombe pile sur le temps.
- **Le pool** (`samplepool.c`, 374 lignes) : PCM 16 bits mono en RAM, au taux
  du fichier, avec un budget de 128 Mo.
- **Les prises** (`src/audio/take.c`) : enregistrement depuis l'entrée audio,
  jusqu'à 5 min.

**Côté FM-1 aujourd'hui** (`core/song.h:11-15, 92, 98` et `core/trk.c:228,
296`) : `FX_SMP` et `ENGINE_SAMPLE` existent mais ne sont jamais stockés. Le
lecteur rejette une couche `engine SMPL`, compte les `fSMP` dans son rapport
et les jette, et les `smp`/`zone` ne sont pas lus. Un morceau qui utilise des
samples se charge donc, mais ces pistes restent muettes. C'est le cas du kick
et du rim de DAFT-PUNK-EN (`smp 00 kick/kick_bk.wav`, `smp 14
tr808/tr8_rim.wav`).

**Ce qui se réutilise** : le format (`smp`, `zone`, `fSMP`, `begin`, `loop`)
et ses règles, tels quels. **Ce qui ne se réutilise pas** : le code. Il
repose sur les flottants, `malloc`, les fichiers WAV et des threads, alors
que le FM-1 n'a ni flottants, ni libc, ni allocation dynamique. Une voix en
virgule fixe qui lit en XIP s'écrit comme les autres moteurs de
`core/synth.c`, quelques centaines de lignes. Le gros du travail est
ailleurs : le stockage, le transfert et la conversion côté ordinateur.

## 4. Comment faire entrer un sample, et ce qu'il coûte en CPU

### Par où il entre

- **Pas d'entrée audio.** `fm1_audio.h` ne fait que de la sortie (I2S vers
  le codec). Le SARADC ne lit que le pot MASTER et la batterie
  (`fm1_adc.h`). L'audio USB (`FELUCCA_UAC`) va de l'appareil vers
  l'ordinateur, pas l'inverse. **On ne peut pas échantillonner sur la
  machine.**
- **Par le lien USB** (`app/link.h`), qui envoie déjà des morceaux : SysEx,
  240 o par message, 8 bits dans 7, stop-and-wait, écrit directement dans le
  store et validé par CRC à la fin. Un sample passerait par le même chemin,
  sous un nouveau type de fichier. Le débit n'a jamais été mesuré. En
  comptant au mieux un aller-retour par milliseconde USB, il faut compter
  quelques secondes pour 100 Kio. C'est l'app Mac ou web qui convertirait le
  WAV en ADPCM avant l'envoi.
- **Effacer un secteur bloque les interruptions ~50 ms** (NOTES, « reverb,
  delay, flash store »). Le transfert et l'enregistrement se feraient donc
  musique arrêtée, comme SAVE aujourd'hui.

### Ce que ça coûte en CPU

- Un bloc audio dure 2 902 µs. Sur DPEN-CHORUS, le pire bloc mesuré avec les
  deux cœurs est à **MAX 57 %** (NOTES 29/10) : il reste de la marge.
- Le cœur fait ~51 instructions simples par µs (NOTES 28/10). Une voix sample
  à interpolation linéaire coûterait ~10 instructions par frame, soit **~25 µs
  par bloc**. L'ADPCM en ajoute environ autant. C'est peu à côté de la VOX
  (V = 272 µs pour deux voix) ou de la chaîne par piste (C = 556 µs). Le
  filtre et l'enveloppe existent déjà et se partagent.
- **Inconnue principale : les défauts de cache XIP.** Le code de l'UI tourne
  en XIP et le code audio en RAM. Une voix lit 256 o par bloc, soit une
  poignée de lignes de cache, mais personne n'a mesuré le coût d'un défaut
  depuis l'interruption audio.

## Contraintes et risques

1. **Le second cœur et la flash.** Sur certains FM-1, le CPU1 lit mal la RAM
   ou la flash partagées (NOTES 28/10, felucca-bsp). C'est pour ça que sa
   boucle d'attente tourne en RAM. Une voix sample rendue par le CPU1 lirait
   la flash en plein rendu. Il faudrait soit mesurer, soit réserver les
   pistes SMPL au CPU0.
2. **Le défaut de multiplication 32x32→64** (NOTES 28/10) concerne aussi
   l'interpolation, même si un produit 16x16 suffit.
3. **Un sample stocké dans le store n'est pas contigu** : chaque secteur de
   4 Kio a son en-tête et son CRC. Il faut soit sauter l'en-tête à chaque
   secteur pendant la lecture, soit réserver une zone contiguë aux samples.
4. **Le firmware et les samples d'usine se partagent les 396 Kio.** Chaque
   Kio de kit en moins, c'est un Kio de code en moins. Le paquet `.fwsc` et
   le temps de mise à jour grossissent aussi.
5. **Les noms** : dans un `.trk`, un sample est un nom de fichier
   (`tr808/tr8_rim.wav`). Le FM-1 devra retrouver ce nom dans sa table, et un
   nom absent restera muet, ce qui correspond déjà à la règle de FORMAT.md.
6. **Felucca** range ses propres « user samples » dans la même zone
   (commentaire de `FL_STORE_OK`). Le problème est le même qu'avec les
   morceaux : les deux formats ne se lisent pas l'un l'autre.

## Recommandation

**Oui, ça vaut la peine, sous une forme réduite :**

- **Seulement des one-shots**, joués par `fSMP` et par des zones simples (une
  plage de touches, une note de base). Pas de boucle, pas de `ping` ni de
  multi-échantillonnage au début.
- **En IMA ADPCM, à 44,1 kHz**, ou 22 kHz au choix pour les sons graves.
- **Lus en XIP** depuis la flash, sans tampon en RAM.
- **Deux sources** :
  - un kit d'usine compilé dans le firmware, par exemple TR-808 et TR-909
    (~245 Kio en ADPCM) ;
  - les samples de l'utilisateur dans le store, envoyés par le lien USB et
    convertis par l'app.
- **Ce qui reste exclu** : l'échantillonnage sur la machine (impossible sans
  entrée) et les instruments mélodiques multi-échantillonnés (un piano
  demanderait plus que toute la flash).

Ce que ça apporte : les morceaux du Mac qui utilisent déjà des kits
(DAFT-PUNK-EN) sonneraient enfin entiers sur le FM-1, et la batterie gagnerait
en réalisme par rapport à NOISE et PULSE.

Si on se lance, la première étape est une mesure : un sample de test compilé
dans le firmware et lu par une voix sur le CPU0 puis sur le CPU1, pour voir
le coût réel du XIP dans le bloc audio et s'assurer que le CPU1 lit bien la
flash. C'est elle qui dira si la forme réduite tient.
