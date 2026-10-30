# Pi Room Correction Box: What to Buy

Prices as of Oct 29, 2026.

## How the box works

Each box is a Raspberry Pi with a USB audio interface, sitting between your sources and your amp or powered speakers. Audio goes into the interface's line input, the Pi runs CamillaDSP with the filters from pump ROOM's **EXPORT FOR PI**, and the corrected audio leaves through the interface's line output.

- **Raspberry Pi**: runs CamillaDSP, which applies the room correction filters. It never touches the audio in analog.
- **USB audio interface**: turns the analog input into digital for the Pi, and the corrected digital back into analog. This part sets the box's sound quality.
- **Your sources**: turntable (through a phono preamp), streamer, phone by cable, or a Bluetooth receiver, all at line level.
- **pump ROOM's export**: a zip with the filters, a CamillaDSP config and `install.sh`. One export per room.

The box has no screen or buttons. Once installed it starts by itself at power-on, and a new correction is a new export copied over.

## The Pi and its basics

Buy the **1 GB Raspberry Pi 4** for the cheapest box, or the **1 GB Raspberry Pi 5** for $10 more and room to spare. pump ROOM's filters (about 16,000 taps per channel at 48 kHz, two channels) are a light load for either, and CamillaDSP needs far less than 1 GB of memory. Raspberry Pi has raised prices three times in 2026 because of memory costs, so the extra RAM in bigger models is expensive and buys you nothing here.

| Part | Pick | Price (USD) | Notes |
| --- | --- | --- | --- |
| Board, cheapest | Raspberry Pi 4, 1 GB | $35 | Plenty for stereo correction at 44.1 or 48 kHz |
| Board, recommended | Raspberry Pi 5, 1 GB | $45 | Headroom for linear phase and 96 kHz |
| Power, Pi 5 | Official 27 W USB-C supply | $14.46 | The Pi 5 warns about low power on weaker supplies |
| Power, Pi 4 | Official 15 W USB-C supply | about $8 | Any good 5 V / 3 A USB-C supply works |
| Case, Pi 5 | Official Pi 5 case (built-in fan) | $12.16 | Keeps it cool in a cabinet |
| Case, Pi 4 | Any Pi 4 case | about $6–17 | A fan isn't needed for this job |
| Storage | 32 GB microSD, A1 or A2 rated (SanDisk, Samsung) | about $8 | Raspberry Pi OS Lite fits easily |

Prices are list prices as of Oct 2026; shops add shipping and sometimes markups when stock is short. Prices marked "about" are typical, not checked against a store that day.

Sources: [Pi prices after the Oct 2026 increase (Tom's Hardware)](https://www.tomshardware.com/raspberry-pi/component-shortages-drive-raspberry-pi-prices-up-by-up-to-23-percent-escalating-lpddr4-lpddr5-costs-trigger-the-third-price-hike-of-the-year) · [27 W supply](https://reach.dog/shop/vilroscom/products/raspberry-pi-5-official-power-supply) · [Pi 5 case](https://reach.dog/shop/vilroscom/products/raspberry-pi-5-official-case) · [PiShop US kits](https://www.pishop.us/pishop-us)

## The audio interface

The interface sets the box's sound quality: the Pi only does math, but every note passes through this part's converters twice. It needs a stereo line input, a stereo line output, and to be USB class compliant (works on Linux with no driver). Anything sold as "plug and play on Mac" usually qualifies.

| Interface | Price (USD) | In / out | Quality | Fits when |
| --- | --- | --- | --- | --- |
| [Behringer UCA202](https://thomannmusic.com/behringer_ucontrol_uca_202.htm) | $15.90 | RCA in, RCA out, headphone out | 16-bit / 48 kHz | A cheap box for a bedroom, kitchen or TV. Works with the export as it is (it shows up as "CODEC"). |
| [Behringer UMC202HD](https://equipboard.com/items/behringer-u-phoria-umc202hd) | $86.90 | two 1/4" combo in, two 1/4" TRS out | 24-bit / 192 kHz | The main stereo and turntable. Quieter, more headroom, balanced outputs for studio monitors. |
| [Behringer UMC204HD](https://www.sweetwater.com/c695--Behringer_2_x_4_USB_Audio_Interfaces) | $97.98 | two 1/4" combo in, 1/4" TRS and RCA out | 24-bit / 192 kHz | Same quality, plus RCA outputs for a hi-fi amp without adapters. |
| [HiFiBerry DAC+ ADC Pro](https://thepihut.com/products/hifiberry-dac-adc-pro) | £65.90 (UK price) | input on a 6-pin header, RCA out | 24-bit / 192 kHz | A tidy single board on the Pi (no USB box), if you don't mind wiring the input. |

- **The 16-bit UCA202 is fine for most rooms.** Its noise floor sits well below what a living room lets you hear, and it is the one box that needs no edits to the export. The UCA222 used to be the usual pick, but it is discontinued.
- **For a 24-bit interface, edit two settings.** In the export's `pump-room.yml`, set `device` to the card name from `arecord -l`, and `format` to the one the card takes: `S24_3_LE` for the Behringer UMC models. If CamillaDSP's log says a format isn't supported, try `S32_LE`. README.txt in the export walks through it.
- **Input level matters.** Line inputs on the UMC models sit behind mic preamps: turn their gain knobs nearly down and set them once so the loudest record doesn't light the clip LED.
- **Avoid Bluetooth-only or USB-C "dongle" DACs**: they have no input.

Prices: Thomann (UCA202), the US average across Sweetwater, Guitar Center and Amazon (UMC202HD), Sweetwater (UMC204HD), The Pi Hut (HiFiBerry), Oct 2026.

## Your sources

Every source has to reach the interface at line level, and the interface has one stereo input, so a room with several sources needs a switch in front of it.

| Source | What to add | Price (USD) | Notes |
| --- | --- | --- | --- |
| Turntable | [Behringer PP400](https://hollywooddj.com/products/behringer-pp400-ultra-compact-phono-preamp) phono preamp | about $18–23 | RCA in and out, wall adapter included. Skip it if the turntable has a built-in preamp (a LINE/PHONO switch on the back): set it to LINE. |
| Streamer, CD, TV | Nothing | — | Use its analog line out (RCA). If it has only optical or coax digital out, add a small DAC, or pick an interface with a digital input. |
| Phone by cable | A 3.5 mm to 2×RCA cable | about $8 | iPhones and most new Androids need a USB-C or Lightning headphone adapter first. Set the phone's volume to maximum. |
| Phone by Bluetooth | A Bluetooth receiver with RCA out, e.g. [1Mii B03](https://bgr.com/2158206/best-bluetooth-audio-receiver-according-experts/) | about $50 | Pairs once, then plays like any other source. Cheaper receivers work but some hiss. |
| Several sources | A passive input switch, e.g. [Rolls SS32](https://reach.dog/shop/avlgear/products/rolls-ss32-mini-route-3-passive-stereo-mono-signal-switcher) | $69 | 3 inputs, RCA and 3.5 mm jacks, no power. Generic 3-way RCA switch boxes do the same for about $15–25. |

The Pi has Bluetooth built in, but using it as a receiver means a second audio path into CamillaDSP and more setup. A separate receiver plugged into the switch keeps the box simple. PP400 price: $17.90 on sale, $23.27 regular (sold out at that shop in Oct 2026).

## Cables

Buy cables to match the interface: RCA for the UCA202, 1/4" for the UMC models. Short runs (1–2 m) are fine for line level.

| Connection | Cable | Price (USD, about) |
| --- | --- | --- |
| Source or switch → UCA202 | 2×RCA to 2×RCA | $6–10 |
| UCA202 → amp or powered speakers with RCA in | 2×RCA to 2×RCA | $6–10 |
| Source or switch → UMC202HD / 204HD | 2×RCA to 2× 1/4" TS | $8–12 |
| UMC → studio monitors (balanced) | two 1/4" TRS to XLR male, or TRS to TRS | $15–25 |
| UMC204HD → hi-fi amp | 2×RCA to 2×RCA from its RCA outputs | $6–10 |
| Pi ↔ interface | the USB cable that comes with the interface | included |

**Hum?** A buzz that stops when you unplug the box's output is a ground loop, usually from the Pi's power and the amp sharing different outlets. Plug everything into one power strip first. If it stays, add an RCA ground loop isolator on the line out (about $10–15).

## Three builds

A single-source room costs about **$90**; the main living room with turntable, streamer and phone about **$255**; a studio box for monitors about **$195**. Four cheap boxes for four rooms come to about $355.

| Build | Parts | Total (USD, about) |
| --- | --- | --- |
| **Cheapest**: TV, kitchen, bedroom (one source) | Pi 4 1 GB $35 · 15 W supply $8 · case $8 · microSD $8 · UCA202 $15.90 · two RCA cables $14 | **$89** |
| **Studio**: monitors, one source (the computer or a mixer) | Pi 5 1 GB $45 · 27 W supply $14.46 · Pi 5 case $12.16 · microSD $8 · UMC202HD $86.90 · TRS to XLR pair $20 · RCA to TS cable $10 | **$197** |
| **Main listening room**: turntable, streamer, phone | Pi 5 1 GB $45 · 27 W supply $14.46 · Pi 5 case $12.16 · microSD $8 · UMC204HD $97.98 · PP400 $23.27 · 3-way RCA switch $20 · phone cable $8 · three RCA cables $30 · RCA to TS cable $10 · *Bluetooth receiver +$50 if you want it* | **$257** (+ $50) |

The studio build needs no switch: feed it the line out of whatever plays. For a second set of monitors on the same desk, one box can't switch between two corrections by itself; give each pair its own box, or switch them on the computer with System EQ or Room Profiles instead.

## Setting it up

1. **Make the correction.** Measure the room in pump ROOM, open CORRECTION and press **EXPORT FOR PI**. It downloads a zip with the filters, `pump-room.yml`, `install.sh` and README.txt.
2. **Prepare the card.** Write **Raspberry Pi OS Lite (64-bit)** to the microSD with Raspberry Pi Imager. In its settings, set a user name and password, turn on SSH, and enter your Wi-Fi.
3. **Wire it.** Interface into a USB port on the Pi; sources (through the switch) into the interface's input; the interface's output into the amp or speakers. Turn the amp down first.
4. **Copy and install.** Boot the Pi, copy the unzipped export over from your computer (`scp -r` or any SFTP app), then run `sudo sh install.sh` in that folder. It installs CamillaDSP and starts it at every boot.
5. **Check the device.** The install prints your audio devices. A UCA202 works as written. For another interface, edit `device` and `format` in `/etc/pump-room/pump-room.yml`, then `sudo systemctl restart pump-room`.
6. **Set levels.** Play the loudest thing you own. Raise the source until it is loud, without the interface's clip light; then set the listening volume on the amp.
7. **New correction later?** Export again, copy it over, run `install.sh` again. Your device settings are kept.

If there's no sound: `systemctl status pump-room` and `journalctl -u pump-room` show what CamillaDSP is unhappy about, usually the device name or format.

## Things that catch people off guard

- **Volume control stays on the amp.** The box has no volume knob. Run sources at full output and turn down on the amp or the speakers: the correction needs the full signal to keep its quality.
- **The box makes things a little quieter.** The filters include the correction's preamp (often −4 to −8 dB) so boosts can't clip. Turn the amp up to make up for it.
- **Latency.** With the export's default no-latency filters, the box adds about 21 ms (chunk size 1024 at 48 kHz) plus the interface's own. That's fine with a TV. Linear-phase exports add about 0.17 s more, which puts speech out of sync with the picture: use them for music-only rooms.
- **One sample rate.** The box runs at 48 kHz: analog in, analog out, so the source's own rate doesn't matter.
- **Power-on order.** Turn the amp or powered speakers on last and off first, so the Pi starting up can't thump them.
- **Measure through the box's own speakers and room.** Each export belongs to one room and one speaker set; moving the speakers or swapping amps means measuring again.
- **No screen needed, but keep Wi-Fi.** You only need it to copy new corrections over. The box keeps running if Wi-Fi drops.
- **Prices move.** Pi prices rose three times in 2026; check the retailer before buying several.
