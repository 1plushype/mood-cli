# mood-cli
it is a cli made for your mood in this moment (inspired from apple's mood thingy) and i think it would be good for rices

## Features

* Animated blob that morphs with your mood: small, blue and spiky when you're sad or even angry, big, yellow and round when you're lwk chill or really happy
* Truecolor interface, purple when sad or angry, warm yellow when happy or js chill
* Pick your emotion and what made you happy/chill or oppisite
* Written entirely in C bcs it is fast and isnt bloated

## things you need
To build and run `mood`, you will need:
* `fingers` to type (most important one of the list)
* `gcc compiler` (GNU's compiler)
* `make tool` (GNU's make tool)
* `git` (to clone repo)
* A terminal with truecolor like kitty, alacritty, foot etc etc (basically anything not from 2005 or older)
* Linux (obviously) bcs it reads `/proc` and `/sys`, so winbloat users needs wsl

## Installation
Clone the repo (very important), compile the code (dw it will be fast >.<), and install it on your system 

```bash
git clone https://github.com/1plushype/mood-cli mood
cd mood
make
sudo make install
```

## Usage

```bash
mood          # log how you feel
mood stats    # see your patterns
mood log 20   # last 20 entries (default 10)
```

### Controls

| Key | What it does |
| --- | --- |
| `←` `→` (or `h` `l`) | slide your mood a little |
| `↑` `↓` | big mood jump |
| `Enter` | confirm |
| `1`-`6` | pick emotion, then choose why it is like that |
| `q` / `Esc` | cancel, nothing gets saved |

## important note
`mood stats` needs around 10+ entries before the patterns mean anything, so dont log once and complain that the chart is empty (really usefull tbh)

### license

mood is licensed under the GNU GPL v3.0.
See the [LICENSE](LICENSE) file for the full license

### also visit my TikTok page
```bash
https://www.tiktok.com/@1plushypemainacc
```
