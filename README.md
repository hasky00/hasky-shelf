# Hasky Shelf

A shelf for the books you write and collect, for Omarchy. Books stand cover-out in a cabinet lit by a neon tube that takes the color of your theme.

- Write a book: press `N`, give it a title, and open it with `Enter` to write. It saves by itself as you go.
- Make words bold or italic: pick them and press `Ctrl B` or `Ctrl I`. The book stays plain text, with stars around the words: `*italic*`, `**bold**`, `***both***`. The page shows them styled as you write, and a shared book carries them as real bold and italic.
- Add a book you already have: press `A`, or drag PDF and EPUB files onto the window. Each is copied onto the shelf; the original is never changed.
- Open a book you added with `Enter`, and it opens in your desktop's own reader.
- Put a picture on a cover: pick the book, press `C`, and choose a PNG or JPEG. Or drag one picture onto the window. It is copied into the book's folder; the original is never changed. Do it again to swap the picture.
- Share a book: press `S`, and a copy lands in your Documents folder as one file. A book you wrote becomes an EPUB that any reader app opens and any other Hasky Shelf can take in with `A`; a book you added is copied as the file it is. A copy shared before is never written over.

Your books live in `~/.local/share/haskyshelf/books/`, one folder each.

## Install

On Omarchy, or any Arch-based desktop:

```bash
git clone https://github.com/hasky00/hasky-shelf.git
hasky-shelf/install.sh
```

It builds the app and installs it for you alone, under `~/.local`. Then open "Hasky Shelf" from the app launcher. To update later, run `git pull` in the folder and `install.sh` again.

## Shortcuts

On the shelf:

| Key | Action |
| --- | --- |
| `Enter` | Open the book |
| `N` | Write a new book |
| `A` | Add PDF or EPUB files |
| `S` | Share the book as a file |
| `C` | Put a picture on the cover |
| Arrow keys | Move along the shelf |
| `?` | Show or hide the shortcuts |
| `Q` | Quit |

In a book:

| Key | Action |
| --- | --- |
| `Esc` | Back to the shelf |
| `Ctrl S` | Save now |
| `Ctrl B` | Bold |
| `Ctrl I` | Italic |
| `F1` | Show or hide the shortcuts |
| `Ctrl Q` | Quit |

## Building

```bash
bin/build
bin/test
cmake --install build --prefix ~/.local
```

On Hasky Omarchy, pick **Install > Hasky Labs > Hasky Shelf** in the Omarchy menu instead.

Made by Hasky Labs, Copenhagen. MIT licensed; the file chooser is adapted from Omacut, see `LICENSE.omacut`.
