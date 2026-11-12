# SuperPico Interface V2 — Revision A (Review)

Neuer **KiCad-9-Schaltplan für eine SNES–RP2350B-Adapterplatine** mit aktiver Pegelanpassung. Zehn A3-Blätter, 89 Bauteile. `superpico_interface_v2.kicad_pro` öffnen; die Symbolbibliothek ist lokal enthalten. Stückliste: `BOM.csv`, GPIO-Zuordnung: `GPIO.csv`, vollständige Pin-/Netzzuordnung: `pin_manifest.json`.

**Entwurf, keine Fertigungsfreigabe.** Noch kein PCB-Layout, keine mechanisch passende QSB, keine verbindliche Zuordnung zu PPU-Gehäusepins. Die Steckverbinder definieren einen logischen Kabelbaum. Dafür fehlen die Mainboard-Revision und das genaue RP2350B-Modul. Dieser Adapter setzt einen separat versorgten RP2350B-Host mit Flash, Takt, USB und bestehendem HDMI/HSTX-Ausgang voraus; er enthält keine neue HDMI-Buchse oder vollständige RP2350-Grundschaltung. Keine Firmware wurde durch diesen Schaltplan automatisch erweitert.

## Schaltungsumfang

| Funktion | Bauteile / Verbindung | Stand |
|---|---|---|
| 5-V-SNES → 3,3-V-Logik | U1–U6 SN74LVC245APWR, 46 Eingangssignale | Eingangspuffer, DIR fest A→B, /OE aktiv |
| RGB555, Pixel-/Blanking-Timing | J1–J3, J7 | Bestehende GPIO27–44 bleiben erhalten |
| Audio und Reset | J4, J8 | GPIO6/7/22/23/24 bleiben erhalten |
| PPU-Registerzugriffe | J5 PA0–7, J6 D0–7, /PAWR an J4 | Nur lesender Abgriff, keine Bussteuerung |
| Adressdekodierung | U7–U10 LVC138 + U11 LVC32 | $2100, $2105, $211A, $2133 |
| Registerspeicher | U12–U15 74LVC273 | Helligkeit, Force Blank, Mode, M7SEL, SETINI |
| Moduslogik | U16–U20 | HiRes, Mode-7-Kontext, Write-valid/Blanking |
| Versorgung/Reset | U21 TPS3808G33, lokale Abblockung | Gemeinsame 3,3-V-Hostversorgung |
| Optionaler TST15-Rückkanal | U22 AHCT125 + U23 LVC1G06, JP1 | Standardmäßig offen; erst nach PPU-Prüfung aktivieren |
| Eingangsbias / Serienabschluss | RN20–31: 100 kΩ; RN1–10: 33 Ω | Bourns CAY16, vier isolierte Widerstände |

Die LVC245-Eingänge sind laut Datenblatt bis 5,5 V zulässig, die Ausgänge folgen der 3,3-V-Versorgung. Keine 5-V-SNES-Leitung führt direkt auf einen Host-GPIO. Die 33-Ω-Widerstände sind Serienabschlüsse und keine Spannungsteiler. RGB und PCLK durchlaufen dieselbe Pufferklasse; Laufzeitunterschiede und Flanken müssen trotzdem gemessen werden.

## Anschlüsse und Versorgung

- J1–J6: Signale auf Pins 1–8 wie im jeweiligen Blatt; Pins 9/10 GND. J4 besitzt zwei unbeschaltete Signalpositionen.
- J7: GPIO27–44 auf Pins 1–18; Pins 19/20 GND.
- J8: Pins 1–5 = GPIO6, 7, 22, 23, 24; Pins 6/7 GND.
- J9: Pins 1–14 = GPIO45, 1, 2, 3, 4, 5, 8, 9, 10, 11, 20, 21, 46, 47; Pins 15/16 GND.
- J10: 3,3-V-Diagnoseanschluss für CSYNC, /PARD, MCLK, SETINI-Bits und Register-valid; Pin 9 GND, Pin 10 3,3 V.
- J11: Pin 1 **3V3_HOST**, Pin 2 GND, Pin 3 **5V_SNES**, Pin 4 GND. 3V3_HOST muss dieselbe Versorgung wie RP2350-IOVDD sein. Keine zweite 3,3-V-Quelle parallel anschließen. 5V_SNES wird ausschließlich für U22 und dessen Beschaltung benötigt.
- J12: Pin 1 TST15, Pin 2 GND. JP1 bleibt zunächst offen.

Kein Spannungsregler auf diesem Adapter: Die Stromreserve des Host-Reglers muss für dynamische Schaltlast, Leiterbahnen und alle Zusatz-ICs geprüft werden. Gemeinsame Masse, kurze Verbindungen und 100 nF unmittelbar an jedem IC sind erforderlich. Bulk-Kondensatoren sind vorgesehen. Die Abgriffe sind interne Verbindungen ohne Hotplug-/ESD-Portschutz.

GPIO0 bleibt wegen möglicher WeAct-PSRAM-Belegung frei; Helligkeitsbit 0 liegt auf GPIO45. GPIO12–19 sind für HDMI reserviert, GPIO25/26 für Menü/Zurück. Bei WeAct-Modulen können **GPIO23 mit dem Onboard-Taster und GPIO25 mit der LED** kollidieren. Modulschaltplan prüfen und störende Beschaltung gegebenenfalls entfernen; insbesondere den GP23-Taster bei LRCK-Nutzung nicht betätigen. Ein RP2350A-Modul mit weniger GPIOs passt nicht zu dieser Zuordnung.

## Welche fehlenden Funktionen vorbereitet sind

**Helligkeit und Force Blank:** U12 speichert INIDISP.D0–3 und D7 getrennt. D4 des Latches ist absichtlich mit HIGH verbunden und dient als Write-valid, nicht als INIDISP.D4. Vor dem ersten gültigen Schreibzugriff bleibt FORCE_BLANK aktiv. Helligkeitswert 0 ist nicht gleich Force Blank. Rasteränderungen müssen später beim Capture dem jeweiligen Pixel-/Zeilenzeitpunkt zugeordnet werden, nicht einem bereits gepufferten HDMI-Frame.

**HiRes / Pseudo-HiRes:** HIRES erkennt Mode 5, Mode 6 oder SETINI.D3. MCLK ist zusätzlich herausgeführt. Das implementiert noch keine 512-Pixel-Abtastung: PIO, DMA, Speicherbedarf, Pixelphase und HDMI-Skalierung brauchen einen eigenen Firmwareentwurf und Messungen.

**Interlace:** SETINI.D0 und FIELD sind getrennt verfügbar; SETINI.D1 liegt am Diagnoseanschluss. Feldreihenfolge und Bildzusammensetzung sind noch nicht implementiert. Overscan liegt als SETINI.D2 an GPIO10; die vorhandene Firmware erkennt die Bildgeometrie weiterhin über das Eingangstiming.

**Mode 7 / EXTBG:** SETINI.D6, OVER und TOUMEI stehen separat bereit. MODE7_CTX bedeutet Mode 7 und M7SEL[7:6]=10. Daraus folgt keine allgemeingültige Schwarzmaske. Die bestehende /OVER-Verbindung zwischen den SNES-ICs wird nicht unterbrochen. Eine aktive Mode-7-Korrektur am PPU-Bus ist **nicht enthalten**, weil deren elektrische Funktion und Polarität am konkreten Board noch nachzuweisen sind. TST15 ist ein separater Testdaten-Rückkanal und kein Mode-7-Patch.

**PAL/NTSC:** Die Eingangspuffer sind nicht regionsabhängig. Die vorhandene PAL-/NTSC-Timingerkennung und 480p60/576p50-Ausgabe bleiben Firmwarefunktionen. Die neue Schaltung ist dafür vorbereitet; reales PAL/NTSC-Verhalten ist noch nicht am Prototyp geprüft.

## Offene Timing- und Inbetriebnahmepunkte

1. Boardrevision und PPU-Ausführung bestimmen. Abgriffpunkte, Signalrichtungen, Polaritäten und zulässige Testpin-Ansteuerung aus dem passenden Mainboardplan abgleichen. Dies ist insbesondere kein ungeprüfter 1CHIP-Adapter.
2. Zunächst nur Versorgung und Eingangspuffer prüfen. JP1 offen lassen. 3,3 V an jedem Hostanschluss und keine Rückspeisung zwischen ausgeschalteten Baugruppen nachweisen.
3. PCLK/RGB/HBLANK/VBLANK am PPU und hinter den Puffern gleichzeitig messen. Der LVC245 hat eine spezifizierte maximale Laufzeit in der Größenordnung 6,3 ns bei 3,3 V unter den Datenblattbedingungen; Leitungskapazität und Serienabschluss kommen hinzu. Bestehende PIO-Abtastphase danach erneut bewerten.
4. **Registertakt ist ein Review-Risiko:** CLK = /SEL OR /PAWR. Die steigende Flanke am Schreibende taktet U12–15. Der Adresspfad enthält zwei Decoder (LVC138 jeweils bis ungefähr 5,8 ns bei 3,3 V unter den Datenblattbedingungen) und ein OR-Gatter. Der Datenpfad und der /PAWR-Pfad haben unterschiedliche Verzögerungen. Daten müssen bis nach der verzögerten Taktflanke gültig bleiben; Adresse darf keine zusätzliche Flanke verursachen. Worst-Case-Setup/Hold, Glitches, Temperatur und Last müssen gegen LVC273-Spezifikationen geprüft werden. Bei Verletzung ist eine synchronisierte Busaufnahme/programmierbare Logik nötig; dieser Entwurf garantiert das noch nicht.
5. U21 und SNES-Reset setzen die Register zurück. Die Einschaltverzögerung kann frühe Registerwrites verpassen. Gemeinsamen Start und Resetdauer messen; Hot-Attach wird nicht unterstützt. Write-valid verhindert undefinierte Helligkeit, kann nach verpasstem INIDISP-Write aber dauerhaft Blank halten. Diese Startup-Frage ist vor Hardwarefreigabe zu lösen.
6. Erst dann den optionalen TST15-Pfad prüfen: U22 arbeitet mit SNES-5 V und TTL-kompatiblem 3,3-V-Eingang. U23 zieht dessen /OE nur bei gültiger Versorgung und freigegebenem Reset herunter; R2 deaktiviert den Treiber andernfalls. Keine allgemeine Hotplug-Garantie für AHCT125. JP1 erst nach Prüfung der PPU-Testfunktion schließen.
7. PAL/NTSC-Umschaltung, 224/239 Zeilen, Reset, Rasterhelligkeit, Mode 7, HiRes und Interlace am Oszilloskop und mit Testsoftware einzeln validieren. Erst anschließend Firmwareerweiterungen aktivieren.

## Reproduzieren und prüfen

```sh
python3 generate_schematic.py
kicad-cli sch erc superpico_interface_v2.kicad_sch -o ERC.rpt --exit-code-violations
kicad-cli sch export netlist superpico_interface_v2.kicad_sch --format kicadxml -o netlist.xml
python3 verify_schematic.py
kicad-cli sch export pdf superpico_interface_v2.kicad_sch -o superpico_interface_v2.pdf
```

KiCad 9.0.2: **0 ERC-Verstöße**. Der Zusatzcheck vergleicht 732 Pin-/Netzzuordnungen mit der exportierten Netzliste, alle 89 Footprint-Pad-Sätze, sechs Eingangspuffer, 512 statische Adress-/Strobe-Kombinationen und 37 eindeutige GPIOs. Die PDF-Blätter wurden visuell kontrolliert. Das prüft weder analoge Eigenschaften noch zeitliche Busfehler. Keine SPICE-Simulation, kein PCB und kein Hardwaretest durchgeführt. Eigene Symbole besitzen reale Gehäusepinnummern; deren Paketzuordnung bleibt vor Fertigung zusätzlich unabhängig zu prüfen. Der Generator überschreibt manuelle Schaltplanänderungen.

## Primärquellen

- [TI SN74LVC245A](https://www.ti.com/lit/ds/symlink/sn74lvc245a.pdf): Eingangstoleranz, Ioff, Pinbelegung und Laufzeiten.
- [TI SN74LVC138A](https://www.ti.com/lit/ds/symlink/sn74lvc138a.pdf): Decoder und Timing.
- [Nexperia 74LVC273](https://assets.nexperia.com/documents/data-sheet/74LVC273.pdf): Register, Reset und Setup/Hold.
- [TI TPS3808](https://www.ti.com/lit/ds/symlink/tps3808.pdf): Versorgungssupervisor und Resetverzögerung.
- [TI SN74AHCT125](https://www.ti.com/lit/ds/symlink/sn74ahct125.pdf), [SN74LVC1G06](https://www.ti.com/lit/ds/symlink/sn74lvc1g06.pdf): optionaler Rückkanal.
- [Bourns CAT/CAY](https://www.bourns.com/docs/product-datasheets/CATCAY.pdf): CAY16-Netzwerke; nicht mit konkaven CAT-Footprints verwechseln.
- [INIDISP-Messungen](https://www.undisbeliever.net/snesdev/registers/inidisp.html): Helligkeit und Force Blank.
- Weitere SNES_TST-Quellen und Abgrenzung der offenen Funktionen: [OPEN_POINTS.md](../../docs/OPEN_POINTS.md).
