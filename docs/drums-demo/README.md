# Batteries : quatre moteurs candidats, à écouter

**Choix fait le 29/10 : A, Felucca.** Son moteur est maintenant celui de
MOONDAW, l'engine **DRUM** (voir « Le moteur DRUM, intégré » en bas).
`1-drum.wav` est ce moteur-là, le vrai, rendu par le cœur du FM-1 ; le
reste de cette page est l'étude qui a mené au choix.

Rien n'a été installé sur le FM-1. Tout se régénère avec

```sh
python3 tools/drums/drums.py
```

(numpy ; la référence « actuel » est rendue par `tests/fm_render.c`, le cœur
du FM-1 lui-même, à partir de `tools/drums/current-kit.trk`).

## Les fichiers

Tous en WAV mono 16 bits à 44 100 Hz (`SYNTH_FS`), à la même intensité
(le rythme de chaque fichier à −17 dBFS RMS) : on compare des couleurs, pas
des volumes.

| Fichier | Contenu |
| --- | --- |
| `0-actuel.wav` | les batteries d'avant DRUM (voir plus bas) |
| `1-drum.wav` | **le moteur DRUM intégré** (kit STD de Felucca), même grille |
| `A-felucca.wav` | candidat A, la conception du moteur DRUM de Felucca |
| `B-analog.wav` | candidat B, les modèles « analogiques » de Plaits/Peaks (808) |
| `C-synth.wav` | candidat C, les modèles « synthétiques » de Plaits (909-ish) |
| `D-chipplus.wav` | candidat D, les moteurs actuels avec ce qui leur manque |
| `comparatif-rythmes.wav` | la mesure du rythme avec son break, dans l'ordre actuel, DRUM, A, B, C, D, séparés par 0,6 s |
| `comparatif-petit-haut-parleur.wav` | le même, passé dans un passe-haut à 200 Hz (12 dB/oct), un seul gain pour tous : à peu près ce que laisse le haut-parleur du FM-1 |

Chaque fichier de kit suit la même grille (120 bpm, 16 pas par mesure,
la grille de `current-kit.trk`) :

| Temps | Ce qu'on entend |
| --- | --- |
| 0 s | grosse caisse seule |
| 1 s | caisse claire seule |
| 2 s | charleston fermé seul |
| 3 s | charleston ouvert seul |
| 4 s | clap seul |
| 5 s | tom (aigu) seul |
| 6 → 10 s | deux mesures de rythme : grosse caisse à la noire, caisse claire sur 2 et 4, charleston fermé sur les temps, ouvert à contretemps (le fermé coupe l'ouvert), puis à la fin de la 2ᵉ mesure un clap et deux toms |

Les prototypes sont en flottant, échantillon par échantillon : ils disent ce
que fait chaque moteur, pas comment le FM-1 le calculerait. Les réglages sont
ceux de conception de chaque source, retouchés à l'oreille sur le papier
seulement — je n'ai pas pu écouter.

## Aujourd'hui

MOONDAW n'a pas de moteur de batterie. Une batterie se fait avec les trois
moteurs « chip » de `core/synth.c` :

- **grosse caisse** : WAVE (la forme triangle à 32 pas), `drive`, et
  `fSWP` qui fait chuter la hauteur — par paliers, un par tick (≈ 20 ms à
  120 bpm) ; DAFT-PUNK : `fSWP2E`, −5,8 demi-tons par tick ;
- **caisse claire, clap, charlestons** : NOISE, le registre à décalage de
  15 bits (ou 7 bits pour le « métallique », timbre 1) sous l'ADSR et le filtre
  de piste ; pas de vrai corps pour la caisse claire (ici une couche PULSE) ;
- **toms** : WAVE avec un `fSWP` plus doux.

Les mesures sur `0-actuel.wav` disent ce qu'on entend : la grosse caisse
tombe de 30 dB en **60 ms** (les candidats : 360 à 490 ms) et son centre de
gravité spectral est à 1,5 kHz — c'est le bourdonnement des marches du
triangle et du drive, pas un « boum » ; le tom glisse jusqu'à 76 Hz. Pas
d'enveloppe de hauteur continue, pas de clic d'attaque, pas de bruit filtré
en bande, pas de source métallique.

(KIDS, lui, joue ses batteries sur des échantillons 909 — muets sur le FM-1,
qui n'a pas de SMPL.)

## Les candidats

### A — FELUCCA (recommandé)

**D'où** : `firmware/src/drum_voice.c` et `eng_drum.c` de Felucca, le
firmware libre du FM-1 de Leo Kuroshita (Hügelton Instruments) dont MOONDAW
tire déjà sa couche matérielle. Conception propre de Felucca, réglée à
l'oreille, avec 5 kits « modèles » (80, 10, 66, 55, 77) et 3 composés.

**Licence** : **GPL-3.0-only**, comme MOONDAW : le code peut être repris
tel quel, en gardant l'en-tête de copyright.

**Technique** : tout ce qui est accordé est un sinus amorti (accumulateur de
phase + table SINE + enveloppe), donc stable et juste, sans résonateur à
fort Q :

- grosse caisse : sinus balayé en deux étages (+2 octaves sur 1,5 ms, puis
  +1,5 octave tenu 12 ms qui retombe), 2ᵉ et 3ᵉ harmoniques calées en phase,
  un clic (deux périodes d'un sinus aigu sous une fenêtre en cosinus) et un
  soft-clip. Conçue pour garder au moins −12 dB d'énergie au-dessus de
  150 Hz, **pour s'entendre sur le petit haut-parleur** — c'est le seul
  candidat qui le vise ;
- caisse claire : deux sinus (f, 1,6 f) qui glissent un peu vers le bas,
  du bruit en passe-bande sous une pointe et un corps, un peu de bruit
  passe-haut sur la pointe ;
- clap : bruit en passe-bande vers 1,1 kHz, frappé 3 fois à 10 ms, puis
  une queue ;
- charlestons : la source « métal » de la 808 (six carrés à 205, 304, 370,
  523, 540 et 800 Hz) dans un passe-bande à 7,1 kHz puis un passe-haut
  (12 kHz fermé, 8,8 kHz ouvert), plus un « grésillement » ; le fermé
  étouffe l'ouvert ;
- tom : sinus dont la hauteur suit le carré de l'amplitude, un peu de
  bruit grave pour la baguette.

Il a aussi RIM, BELL, CONGA, CLAVE, CYM, non rendus ici.

**Coût CPU** : déjà écrit en entiers 32 bits pour ce processeur (que des
produits 32 bits dans les boucles, enveloppes en Q15/Q16, filtres SVF
trapézoïdaux). La référence de Felucca (`tests/cpu_baseline.txt`) donne un
kit DRUM entier à ≈ 1 100–1 270 instructions hôte par échantillon, autant
qu'un preset de son moteur ANALOG (1 000–1 450) ; à la louche, de 20 à
40 opérations par échantillon et par voix qui sonne, les six carrés partagés
entre charlestons. Une voix éteinte (−84 dB) ne coûte rien.

**Attention** : `A-felucca.wav` est ma recréation en flottant d'après la
description et les chiffres du code, **pas** le son exact de Felucca. Son
test `tests/drum_test.c` rend ses propres démos (`build/drum_demo/`), mais
je n'ai pas eu le droit d'exécuter le code téléchargé ici ; à faire tourner
de ton côté pour entendre l'original.

### B — ANALOG (Plaits / Peaks, façon 808)

**D'où** : Mutable Instruments Plaits (`plaits/dsp/drums/analog_bass_drum.h`,
`analog_snare_drum.h`, `hi_hat.h`) et Peaks (`peaks/drums/`), d'Émilie
Gillet ; portés aussi dans DaisySP (`Source/Drums/`, Electrosmith).

**Licence** : **MIT** (Plaits, Peaks, DaisySP) — compatible GPL-3.0 ; on
garde la notice MIT.

**Technique** : des circuits analogiques simulés.

- grosse caisse : un filtre SVF à très fort Q (le pont en T de la 808)
  frappé par une impulsion de 1 ms ; une FM d'attaque et une auto-FM
  courbent sa hauteur ;
- caisse claire : cinq modes (1, 2, 3,18, 4,16, 5,62 × f) rendus par cinq
  résonateurs, et du bruit redressé dans un passe-bande à 16 × f ;
- charlestons : les six carrés, un passe-bande résonant, un VCA asymétrique
  (« swing »), un passe-haut ;
- clap : bruit vers 1 kHz sous trois dents en dent de scie, puis une queue
  (la 808 ; Plaits n'a pas de clap) ;
- tom : le même résonateur que la grosse caisse, plus haut.

**Le son** : le plus « vrai » au casque, rond et profond — mais la grosse
caisse met 98 % de son énergie sous 150 Hz : sur le haut-parleur du FM-1
elle disparaît presque (écouter `comparatif-petit-haut-parleur.wav`).

**Coût CPU** : le plus cher. Un SVF dont la fréquence bouge à chaque
échantillon (il faut tan(πf) : une table), et la caisse claire fait tourner
**six** SVF par échantillon (≈ 80–100 opérations). À fort Q, un SVF en
virgule fixe demande des coefficients en Q30 et du soin (Felucca a écarté
ce choix pour cette raison : « no high-Q resonator »). À réécrire en entiers.

### C — SYNTH (Plaits, façon 909)

**D'où** : Plaits, `synthetic_bass_drum.h`, `synthetic_snare_drum.h` et
le second modèle de `hi_hat.h` ; DaisySP `SyntheticBassDrum`,
`SyntheticSnareDrum`. **Licence MIT.**

**Technique** :

- grosse caisse : un sinus tenu à sa crête 1,3 ms puis lâché sous une
  enveloppe de FM (la chute de hauteur), un sinus « triangle arrondi » dans
  un VCA de transistor, un clic et un souffle d'attaque ;
- caisse claire : deux sinus déformés (f, 1,47 f) sous une enveloppe FM, et
  du bruit blanc entre un passe-haut à 10 f et un passe-bas à 35 f, tenu
  40 ms avant de décroître ;
- charlestons : trois paires carré × dent de scie modulées en anneau
  (200 × 7 530, 510 × 8 075, 730 × 10 500 Hz), passe-bande, passe-haut, une
  décroissance en deux temps ;
- clap : bruit à 1,2 kHz, quatre dents serrées, une couche brillante, une
  queue de 120 ms ;
- tom : sinus à chute de hauteur rapide et un coup de bruit.

**Le son** : plus sec et plus « numérique » que B ; même souci de grave
pour la grosse caisse sur le haut-parleur.

**Coût CPU** : moyen, ≈ 40–60 opérations par voix (une ou trois SVF, des
divisions dans les saturations, à remplacer par des tables). Pas de
résonateur à fort Q : plus facile en virgule fixe que B.

### D — CHIP+ (le moins cher)

**D'où** : MOONDAW lui-même. Pas de code étranger.

**Technique** : garder WAVE et NOISE, mais leur donner ce qui leur manque :

- une **enveloppe de hauteur exponentielle calculée à chaque échantillon**
  (au lieu du `fSWP` par tick) : la grosse caisse part de 8 × f et retombe
  en 22 ms ; plus un front PULSE de 1,5 ms pour le clic ;
- une **décroissance par échantillon** (pas un ADSR par tick) ;
- le bruit du registre à décalage, **passe-haut** (7 kHz pour les
  charlestons, 1,5 kHz pour la caisse claire, une bande 0,8–2,5 kHz pour le
  clap à trois dents) ;
- caisse claire : triangle + bruit dans un soft-clip.

**Le son** : reste « chiptune » (les marches du triangle, le bruit du
registre), mais avec un vrai « boum » et des attaques nettes. Le meilleur
rapport amélioration/effort si on veut rester dans l'esprit puce.

**Coût CPU** : quasi nul, 5–15 opérations par voix ; le bruit et la WAVE
restent dans le tampon à bandes limitées, l'enveloppe de hauteur se calcule
une fois par front.

## Et ailleurs

- **Firmware d'origine M-VAVE** : un FM à six opérateurs façon DX7,
  128 presets, arpégiateur, séquenceur 16 pas ; aucune batterie documentée,
  et code fermé.
- **FM1 Open** (le firmware ouvert annoncé par M-VAVE, octobre 2026) cite des
  sons « 808-style » ; **Sloop** (isod89/sloop-fm1) annonce 37 kits de
  batterie, **Hortator** 45 moteurs de batterie. Je n'ai pas vérifié leurs
  sources ni leurs licences : à regarder si A ne suffit pas.
- **LMMS / kicker** (GPL-2.0+) : la technique de Kicker est celle de D et
  de A (un sinus, une enveloppe de hauteur début→fin avec une pente, un
  clic, une distorsion), en flottant ; rien à prendre de plus que A.

## Comment ça entrerait dans MOONDAW

**Un nouveau moteur `ENGINE_DRUM`**, sur le chemin de VOX : un moteur qui
calcule ses échantillons et les ajoute à ceux de la piste après la lecture
du tampon (`vox_render` dans `player.c`), pas un moteur à fronts.

- `timbre` = la pièce : KICK, SNARE, CLAP, HAT CL, HAT OP, TOM, RIM, BELL
  (et les variantes de Felucca) ; un instrument = une pièce, comme on écrit
  déjà KICK, HATC, HATO, CLAP en instruments séparés ;
- la **note** accorde la pièce autour de sa hauteur de conception (C-4 =
  telle que conçue, comme le TUNE de Felucca) ;
- le **decay** de l'ADSR donne DECAY ; `cutoff` du filtre pourrait donner
  TONE ; il manque un champ pour l'« extra » (SNAP : drive, caisse claire
  plus ou moins claquante, écart des dents du clap…) — un champ de plus
  dans `InstrumentDef`, ou réutiliser `tract`, qui n'a de sens que pour VOX ;
- la vélocité / `fVOL` donne l'accent ; deux instruments HAT sur la même
  piste s'étouffent déjà (piste mono) ;
- le format `.trk` est partagé : il faut que **moondaw** (iOS, Mac, web,
  console) connaisse aussi `engine DRUM`, sinon ces instruments y retombent
  sur la pulse. Le cœur de synthèse étant partagé, le même `drum_voice.c`
  peut y entrer.

D, lui, se ferait **sans nouveau moteur** : deux paramètres sur WAVE et
NOISE (une enveloppe de hauteur : profondeur et temps ; un passe-haut sur
NOISE), ou un `fSWP` lissé par échantillon.

## Recommandation

**A, le moteur DRUM de Felucca, porté tel quel.** Sur le papier c'est le
meilleur choix de loin :

1. il a été conçu **pour cet appareil** : ce processeur (entiers 32 bits,
   pas de résonateur instable), ce haut-parleur (la grosse caisse garde son
   énergie au-dessus de 150 Hz), ce budget (une voix éteinte ne coûte rien) ;
2. même licence que MOONDAW (GPL-3.0-only), même auteur que la couche
   matérielle déjà reprise : pas de réécriture, des tests qui viennent avec
   (`drum_test.c` : hauteurs, durées, pas d'écrêtage, étouffement des
   charlestons) ;
3. il apporte 8 pièces et 8 kits, pas seulement les 6 de cette démo.

B sonne le plus « 808 » au casque mais se perd sur le haut-parleur et coûte
le plus cher ; C est un bon second si on veut un caractère 909 ; D est le
plan de repli si l'on veut rester 100 % chiptune et ne pas toucher au
format.

**Avant de décider** : écouter `comparatif-rythmes.wav` au casque, puis
`comparatif-petit-haut-parleur.wav`, et faire tourner le `drum_test` de
Felucca pour entendre l'original de A (ma version n'en est qu'une
approximation).

**Ce que demanderait l'intégration de A** :

1. copier `drum_voice.c` (≈ 1 000 lignes) avec ce qu'il prend à `dsp.c` de
   Felucca-Engines (la table SINE, `pitch_inc`, le SVF trapézoïdal,
   `softclip` ; MIT) dans `core/`, l'adapter à notre bloc (128 échantillons) ;
2. `ENGINE_DRUM` dans `song.h`, `trk.c` (lire et écrire `engine DRUM`), le
   rendu dans `player.c` à côté de VOX, la répartition entre les deux cœurs ;
3. l'écran d'instrument du FM-1 (pièce, TUNE, DECAY, TONE, SNAP) ;
4. un test hôte (reprendre les cibles de `drum_test.c`) et une mesure CPU sur
   l'appareil (un rythme dense sur DAFT-PUNK-EN) ;
5. le même moteur dans moondaw, pour que les morceaux restent les mêmes
   partout.

## Sources

- Felucca : <https://github.com/hugelton/Felucca> (`firmware/src/drum_voice.c`,
  `eng_drum.c`, `tests/drum_test.c`, `tests/cpu_baseline.txt`, `LICENSING.md`)
- Mutable Instruments : <https://github.com/pichenettes/eurorack>
  (`plaits/dsp/drums/`, `peaks/drums/`)
- DaisySP : <https://github.com/electro-smith/DaisySP> (`Source/Drums/`)
- FM-1 : [Synthtopia, FM1 Open](https://synthtopia.com/content/2026/10/20/m-vave-officially-opens-fm-1-firmware-to-developers-with-new-workbench-sdk),
  [Synthtopia, firmwares alternatifs](https://www.synthtopia.com/?p=155069),
  [Synth Anatomy, test du FM-1](https://synthanatomy.com/2026/07/m-vave-fm-1-review-low-budget-pocket-fm-ynthesizer-with-iconic-sounds.html)

## Le moteur DRUM, intégré

Les voix de batterie de Felucca (`core/drum_voice.inc`, reprises telles
quelles) dans un nouveau moteur, `core/drum.c`. `UPSTREAM.md` dit ce qui
vient d'où.

- **Un instrument = un kit.** TIMBRE choisit lequel : STD (celui de
  Felucca), ROUND (STD avec la grosse caisse ronde), puis les kits
  modèles de Felucca : 80, 10, 66, 55, 77, DEEP, TIGHT, PERC.
- **La note choisit la pièce**, sur le plan General MIDI comme chez
  Felucca : C-2 grosse caisse, C#2 rim, D-2 caisse claire, D#2 clap, F#2
  charleston fermé, A#2 charleston ouvert, toms sur F G A B C D, cymbales,
  cloches, congas et claves jusqu'à A-5 (81). Hors de 35..81, une note
  joue sa classe de hauteur entre C-2 et B-2. Un accord frappe plusieurs
  pièces.
- **La ligne DRUM de l'écran INSTRUMENT** : TUNE, TONE, DECAY, SNAP, 64 =
  tel que conçu. Dans le fichier : `drum 64 64 64 64`, écrit seulement si
  l'un n'est pas à 64. La vélocité donne l'accent.
- **La frappe est l'enveloppe** : la piste d'un kit reste ouverte, chaque
  pièce d'une piste est une voie mono, les voies sonnent ensemble, le
  charleston fermé étouffe l'ouvert. Un OFF ou un GAT ferme quand même la
  piste avec le release de l'instrument. Sur INSTRUMENT, PATTERN et
  PHRASE, la première touche C (la 8ᵉ) est la grosse caisse.
- **12 voies partagées** par toutes les pistes, 3,4 Ko de RAM ; environ
  20 Ko de flash.
- **Coût estimé** (pas encore mesuré sur l'appareil) : environ 2 % d'un
  cœur par pièce qui sonne, 18 % pour huit à la fois.
- **Morceaux** : la démo intégrée au firmware joue maintenant un kit. Dans
  `songs/`, DAFT-PUNK, DAFT-PUNK-EN et KIDS convertis par
  `tools/drums/to_drum.py` (KIDS, dont les batteries étaient des samples
  muets sur le FM-1, a enfin les siennes). Les originaux du dépôt moondaw
  ne sont pas touchés : moondaw ne connaît pas encore DRUM et y jouerait
  la pulse.
