# Pocket Studio

Mini DAW per Nintendo DS: campionatore con microfono, 8 tracce con mixer, piano roll, effetti lo-fi e salvataggio dei sample su SD.

> **Stato:** il codice non e' mai stato provato su un DS o su un emulatore. L'interfaccia e' stata verificata solo disegnandola sul PC. Aspettati qualche bug da sistemare.

## Compilare con GitHub (nessuna installazione)
1. Crea un repository su GitHub e carica **tutto il contenuto** di questa cartella (compresa `.github`).
2. Apri la scheda **Actions** e attendi la fine di "Build NDS" (qualche minuto). Puoi anche premere "Run workflow".
3. Apri l'esecuzione completata e scarica l'artefatto **pocket_studio**: dentro c'e' `pocket_studio.nds`.

## Compilare in locale
1. Installa devkitPro (https://devkitpro.org/wiki/Getting_Started) e poi: `sudo dkp-pacman -S nds-dev`
2. Nella cartella del progetto: `make`
3. Ottieni `pocket_studio.nds`. `make clean` per ripulire.

## Provarlo
- **Emulatore:** melonDS (supporta microfono e SD virtuale). In DeSmuME il microfono e' limitato.
- **DS reale:** copia il .nds su una flashcard (R4 e simili) con una SD. I sample vanno in `/POCKET/`.

## Comandi
| Tasto | Azione |
|---|---|
| L / R | cambia tab (CLIP, ROLL, REC, SD, FX, SET) |
| START | play/stop del sequencer (piano roll) |
| SELECT | inverte i ruoli degli schermi |
| Layout normale: D-pad | sinistra/destra traccia, su/giu volume, A mute, B solo |
| Layout invertito: D-pad | muove il cursore su CLIP e ROLL, A conferma, B cambia PEN/ERASE |

Schermo alto = mixer, schermo basso (touch) = pagine. Con SELECT il mixer passa sul touch e le pagine in alto, con D-pad solo su CLIP e ROLL.

## Uso base
1. Tab **CLIP**: tocca una cella per selezionare la clip (traccia/scena).
2. Tab **REC**: premi REC, parla nel microfono. Il sample viene salvato in `/POCKET/RECnnn.WAV` e caricato nella clip.
3. Torna su **CLIP** e tocca la cella per lanciare o fermare il loop.
4. Tab **ROLL**: tocca la griglia per mettere note, suonano il sample della traccia intonato. START avvia il sequencer.
5. Tab **FX**: trascina verticalmente su una manopola, tocca il quadratino per attivarla. BOUNCE applica bitcrush, rate e filtro al sample e lo salva su SD. Wobble, vinyl e pitch agiscono in tempo reale.
6. Tab **SD**: carica WAV (PCM 8/16 bit, mono o stereo) da `/POCKET/`.
7. Tab **SET**: frequenza del microfono, durata massima, BPM.

## Limiti noti
- Il tempo del sequencer e' agganciato ai frame (circa 60 Hz), quindi ha un piccolo jitter.
- I meter del mixer sono animati, non misurano l'audio reale.
- Niente Wi-Fi/Ableton Link in questa versione.

## File
`source/main.c` loop principale, `ui.c` schermate e touch, `audio.c` mic/playback/sequencer/effetti, `sdio.c` WAV e impostazioni su SD.
