# Le jour où le FM-1 arrive

L'ordre des opérations, écrit avant d'avoir l'appareil. Rien de ce dépôt n'a
jamais tourné sur un vrai FM-1 : le firmware compile, se lie, passe les
contrôles de Felucca, et c'est tout ce qu'on sait.

Deux règles au-dessus de tout le reste :

- **On ne flashe rien avant d'avoir une sauvegarde de la flash d'origine,
  vérifiée, en deux endroits.**
- **On ne flashe rien avant d'avoir prouvé qu'on sait revenir en arrière.**

## 0. À préparer avant (sans l'appareil)

Fait le 10/10/2026, sur ce Mac :

- [x] Le firmware [FM-1-transporter](https://github.com/kurogedelic/FM-1-transporter)
      **compilé** : `~/Documents/Code/FM-1-transporter/build/fm1_transporter.uf2`
      (commit `a632d92`, avec `wl82loader.bin` embarqué).
- [x] `pyserial` installé, `tools/fm1t.py` se lance.

Ce qui a été installé pour ça, hors de tout dépôt :

| Quoi | Où |
|---|---|
| pico-sdk 2.2.0 | `~/pico-sdk` |
| GCC ARM 13.3 (archive d'Arm, sans installation système) | `~/.fm1/arm/` |
| jl-uboot-tool, pour `wl82loader.bin` (24 064 octets) | `~/.fm1/jl-uboot-tool/` |

Pour recompiler :

```bash
cd ~/Documents/Code/FM-1-transporter && ninja -C build
```

Reste à faire :

- [ ] Une **Seeed XIAO RP2040** (pas une autre carte : les autres RP2040 ne
      sont pas testées, la Waveshare RP2350-USB-A ne marche pas).
- [ ] **Charger le firmware sur la XIAO** : la brancher en tenant son bouton
      BOOT, elle apparaît comme un disque `RPI-RP2` ; y copier
      `fm1_transporter.uf2`. Elle redémarre seule.
- [ ] Un câble USB sacrifié pour atteindre D+, D− et la masse du FM-1.
- [x] `pip3 install --user mido python-rtmidi` (fait le 28/10).
- [x] Le fichier **V15 officiel**, rangé dans `~/.fm1/stock/FM-1_015.fwsc` :
      699 956 octets, l'empreinte attendue par `fm1_install.py`, identité
      `FM-1_015`. Pris dans le dépôt de M-VAVE
      (`m-vave/fm1_open`, `sdk/firmware/FM-1_015.fwsc`) : depuis le 21/10,
      le `FM-1.fwsc` de m-vave.com/download est la **V16**, pas la V15.

Câblage, trois fils et pas un de plus :

| XIAO RP2040 | FM-1 |
|---|---|
| D6 (GP0) | D+ |
| D7 (GP1) | D− |
| GND | GND |

**Ne jamais brancher VBUS (le 5 V).** Le FM-1 tourne sur sa batterie.

## 1. L'appareil tel qu'il sort de la boîte

- [x] Reçu le 28/10, en **V14**. Le firmware d'origine n'affiche pas sa
      version au démarrage (il démarre instantanément) ; elle se lit par
      USB : `python3 tools/fm1_install.py --info` (requête d'identité seule,
      n'écrit rien). `FM-1_014` = V14, `FM-1_015` = V15.
- [x] Passé en **V15** par le Workbench de M-VAVE
      (open.m-vave.com/fm1-workbench, Chrome) : bouton « retour à la V15
      d'usine », V15 intégrée à la page. Piège : le bouton par défaut
      installe **FM1 Open** (identité `FM-1_8000014`, port `FM1 Community`),
      pas la V15. `--info` répond maintenant `FM-1_015`.
- [x] Port MIDI de la V15 : `USB Composite Device`. La poignée de main de
      Felucca y répond du premier coup.
- [ ] **Mesurer la diagonale de l'écran** (ou sa largeur visible, au pied à
      coulisse). C'est la donnée qui manque pour fixer la hauteur d'une ligne.
- [x] La disposition correspond à `proto/pattern.html` : quatre potards à
      gauche de l'écran, quatre à droite, deux rangées de six boutons, OCT−
      et OCT+ sous les potards de gauche.

Depuis le 20/10, M-VAVE publie aussi **FM1 Open** (application GPLv3,
moteur en SDK binaire) et un Workbench qui installe et revient à la V15.
Rien n'y dit comment récupérer un appareil dont le firmware plante avant
l'USB : la XIAO reste le seul recours pour ce cas.

## 2. La sauvegarde

**Faite le 28/10 sans XIAO**, avec `fm1_rescue.py` de felucca-bsp
(`tools/`, commit `5942e68`), qui parle au chargeur de la puce
(`WL80UBOOT1.00`, 4C4A:8057) directement depuis le Mac :

1. `sudo ~/fm1-rescue/env/bin/python ~/fm1-rescue/fm1_rescue.py
   ~/fm1-rescue/FM-1.fwsc --loader ~/fm1-rescue/wl82loader.bin`
   (sans `--write` : lit, sauvegarde, compare, n'écrit rien) ;
2. pendant qu'il attend, la V15 reçoit par USB-MIDI `F0 22 24 35 7D F7`
   et passe dans le chargeur ; trouvé en 8 s ;
3. puce 980F, flash 856014, 1 Mo lu en 2,6 s, **0 secteur sur 147 ne
   diffère de la V15** ; le FM-1 redémarre seul en V15.

- [x] `fm1-backup-20261028-181751.bin`, 1 048 576 octets, sha256
      `d562b0e8…534f62`, dans `~/fm1-rescue` et `~/.fm1/backups`.
- [ ] Une copie hors de la machine (disque externe ou nuage).

C'est aussi le chemin de secours sans XIAO : un firmware qui plante au
démarrage finit dans ce chargeur (garde de démarrage), et le même script
avec `--write` y remet la V15.

Avec la XIAO, prévu à l'origine :

```bash
python3 tools/fm1t.py info
```
```bash
python3 tools/fm1t.py dump backup-1.bin
```
```bash
python3 tools/fm1t.py dump backup-2.bin --compare backup-1.bin
```
```bash
shasum -a 256 backup-1.bin backup-2.bin
```

(à lancer depuis `~/Documents/Code/FM-1-transporter`)

- [ ] Les deux empreintes sont identiques, les fichiers font 1 048 576 octets.
- [ ] Une copie hors de la machine (disque externe ou nuage).

À savoir : le transporter lit toute la flash mais **n'écrit que 0x4000 à
0x93000**, la zone du firmware. Il ne restaure donc pas la zone des données
(0x97000 et au-delà), et n'en a pas besoin : nous n'écrivons jamais dans la
sauvegarde du firmware d'origine (0xE8000 et au-delà).

## 3. Prouver le retour avant le premier aller

```bash
python3 tools/fm1t.py write --package FM-1.fwsc --ref backup-1.bin
```

Sans `--write`, cette commande **vérifie et n'écrit rien**. Elle doit passer.

Le transporter est un outil de **retour**, pas d'installation : `write`
n'accepte que le paquet officiel V15 (ou un paquet explicitement validé par
son auteur) et refuse le reste. MOONDAW ne s'installera donc jamais par lui,
mais par la mise à jour USB normale (`tools/fm1_install.py`, celle de
Felucca).

## 4. Felucca d'abord, pas MOONDAW

Ma recommandation : le premier firmware alternatif à installer est
**Felucca**, par son installateur web, pas le nôtre. C'est un firmware connu
pour marcher. S'il s'installe et qu'on sait revenir à la V15 (le bouton
« Return to official V15 » de l'installateur, ou M-VAVE), on a prouvé le
chemin d'installation et le chemin de retour avec autre chose qu'un firmware
qui n'a jamais tourné.

- [x] Felucca 1.7.0 installé le 28/10 par son installateur web, câble
      direct, Chrome : identité `FM-1_917`, port MIDI `Felucca`. Son,
      écran et commandes répondent.
- [x] Retour à la V15 officielle : `FM-1_015`, port `USB Composite Device`.

Felucca n'est plus développé activement depuis le 16/10 (maintenance
« occasionnelle ») ; sa couche matérielle vit dans felucca-bsp, sur
laquelle Felucca 1.7 et la plupart des firmwares alternatifs sont bâtis.
MOONDAW porte une copie à la main des pilotes de Felucca du 10/10 : passer
à felucca-bsp est à décider après le premier démarrage de MOONDAW.

## 5. Le firmware de test (`MOONDAW_APP=hello`)

Le plus petit possible : du texte, toutes les commandes lues et affichées,
une onde carrée sous les touches. 18 Ko.

```bash
MOONDAW_APP=hello ./build.sh
```

À vérifier, dans cet ordre :

- [ ] L'écran s'allume et le texte est à l'endroit, les couleurs justes
      (sinon : ordre des octets ou inversion, `lcd.c`).
- [ ] Chaque **touche** allume sa case, sa LED, et joue sa note, de Fa3 à
      Sol5 de gauche à droite.
- [ ] Chaque **bouton** allume le nom qui est imprimé dessus (sinon la table
      `PANEL_DEFAULT` de `panel.c` ne correspond pas à cet exemplaire).
- [ ] Chaque **potard** compte dans le bon sens, et le bon compteur bouge.
- [ ] MASTER va de 0 à environ 1023 et règle le volume.
- [ ] **CPU et MAX** en bas à droite : la part de son temps que prend
      l'interruption audio, pour une simple onde carrée. C'est le plancher.
- [ ] **OCT− et OCT+ tenus 5 secondes** : l'écran dit UBOOT. C'est la porte
      de sortie ; si elle ne marche pas, on ne va pas plus loin.
- [ ] Une mise à jour par-dessus (réinstaller le même paquet) passe.

## 6. MOONDAW

```bash
./build.sh
```

- [ ] La démo joue. **CPU et MAX** sur l'écran de lecture : c'est LE chiffre
      inconnu. Sous 70 % en moyenne et 90 % au pire, ça tient.
- [ ] EDIT ouvre PATTERN : **est-ce lisible ?** Les notes, les petits
      libellés en 8 px, les mots des boutons.
- [ ] Écrire une note, SAVE, éteindre, rallumer : la note est là.
- [ ] Pendant SAVE le son s'arrête (attendu : un effacement de secteur bloque
      les interruptions une cinquantaine de millisecondes).

Si le CPU ne tient pas, les replis sont dans `NOTES.md` : moins de voix par
piste, le filtre à demi-cadence, un noyau moins cher pour le moteur WAVE,
moins de bandes redessinées par image.

## 7. Les morceaux

Une fois MOONDAW installé et l'appareil sur son écran MENU, branché en USB :

```
tools/fm1_send.py --ping
tools/fm1_send.py ../moondaw/songs/KIDS.trk
```

`--ping` doit répondre avec la place libre (290 Ko sur une flash vide ;
les 13 morceaux en prennent 100, le plus gros 25). L'envoi réduit d'abord
le morceau à ce que le FM-1 porte, puis l'écrit dans la flash de
l'appareil, qui affiche RECEIVING. Rien n'y est remplacé sans `--replace`.

Dans l'autre sens, pour sauvegarder ce qui a été écrit sur l'appareil :

```
tools/fm1_send.py --list
tools/fm1_send.py --get KIDS -o KIDS-fm1.trk
```

Si aucun port MIDI n'est trouvé, l'outil liste ceux qu'il voit :
`--port` avec un bout du bon nom. **Ce chemin MIDI n'a jamais tourné** :
tout le reste du transfert est vérifié sans appareil, mais le nom du port
et le débit réel sont à découvrir ce jour-là.

## Ce qu'on ne sait pas et qu'on saura ce jour-là

- La fréquence du processeur, donc ce que coûtent réellement huit pistes.
- La taille de l'écran, donc si onze pas par écran sont lisibles.
- Si la table des boutons de Felucca vaut pour cet exemplaire.
- Si l'envoi d'un morceau par SysEx passe au débit attendu (le port de la
  V15 s'appelle `USB Composite Device` ; celui de MOONDAW reste à voir).
- Si l'identité `FM-1_900` de notre paquet est acceptée telle quelle par
  l'installateur (c'est celle des versions de développement de Felucca).
